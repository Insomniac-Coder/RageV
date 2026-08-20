#pragma once

// Engine-wide startup settings, resolved once before the window exists and
// immutable afterwards.
//
// The graphics backend is deliberately a restart-time choice rather than a
// runtime toggle: the window itself is created differently per backend (Vulkan
// needs GLFW_CLIENT_API=GLFW_NO_API before creation, OpenGL needs a context),
// so switching live would mean tearing down and recreating the window, the
// swapchain and every GPU resource. A flag plus a restart is the honest
// version of that.
//
// Resolution order, later wins:
//   1. built-in defaults
//   2. ragev.ini next to the executable
//   3. command line
//
// Command line:
//   --rhi=vulkan|opengl     graphics backend
//   --vsync=on|off
//   --validation=on|off|gpu Vulkan validation layers (no effect without the SDK);
//                           gpu adds GPU-assisted validation, which is the only
//                           check that sees a bad bindless index
//   --frames-in-flight=N
//   --fixed-hz=N            simulation steps per second (default 60)
//   --width=N --height=N    window size
//   --audio=on|off          open an output device at all
//   --import-cache=on|off   read and write cooked assets (default on)
//   --depth-sort=on|off     order opaque batches front to back (default on)
//   --bindless=on|off       material textures through the bindless heap where
//                           the device can (default on; no effect on OpenGL)
//   --aa=none|fxaa|smaa|ssaa|msaa|taa  override the scene's anti-aliasing choice
//                           (also written to ragev.ini by the editor's
//                           Render Settings dropdown, so a choice sticks)
//   --raytracing=on|off     override the project's ray tracing checkbox (on
//                           falls back to shadow maps on a device without ray
//                           queries)
//   --rt-reflections=on|off override the ray-traced reflections option (needs
//                           ray tracing on and bindless materials)
//   --rt-ao=on|off          override the ray-traced ambient occlusion option
//   --gi-bounces=1|2        how many times traced light bounces (7ax)
//   --render-defaults=on|off  ignore the project's Render Settings and use
//                           the struct's defaults, so a check depends only on
//                           its own command line (ENGINE-NOTES 7ba). The
//                           overrides above still apply on top.
//   --ssaa=N                how many times larger SSAA draws each axis
//   --msaa=N                coverage samples per pixel for MSAA
//   --project=<path>        the .rvproject to open, or a folder containing one
//   --screenshot=<file>     write a PNG of one frame and exit
//   --loading-screenshot=<file>  write a PNG of a frame drawn while loading
//   --screenshot-frame=N    which frame to capture (default 30, to let the
//                           scene settle and any first-frame allocation pass)
//   --screenshot-count=N    capture N consecutive frames from that one, as
//                           <file>_<frame>.png, then exit (default 1: the
//                           file as named)
//   --benchmark=N           run N frames, print a frame-time summary, exit
//   --scene=<path>          open this scene instead of the project's start scene
//   --ui-scale=N|auto       editor UI scale; auto follows the monitor
//   --theme=dark|light      editor theme; default is whatever was last used
//   --camera=x,y,z,d,yaw,pitch   where the editor camera starts (degrees)

#include "RageV/Renderer/RHI/RHITypes.h"
#include "RageV/Renderer/RenderSettings.h"
#include "RageV/Math/Math.h"
#include <string>
#include <filesystem>

namespace RageV
{
	struct EngineConfig
	{
		RHI::Backend Backend        = RHI::Backend::OpenGL;
		bool         VSync          = true;
		uint32_t     FramesInFlight = 2;

		// Simulation rate. 60 matches what most physics engines are tuned for;
		// higher costs CPU, lower makes fast collisions tunnel.
		uint32_t     FixedHz = 60;
		// Whether --fixed-hz was actually given. Without this there is no way
		// to tell "the user asked for 60" from "nobody said", and the project's
		// own rate could never win over the default.
		bool         FixedHzExplicit = false;

		// Pretend every frame took exactly this long, in seconds. Zero means
		// use the real clock, which is what a game does.
		//
		// **For capture, not for playing.** Anything driven by frame time --
		// particles, OnFrame scripts, the interpolation alpha -- otherwise
		// depends on how fast this machine happened to run, so two screenshots
		// of the same scene are never quite the same picture. That makes
		// comparing them against each other measure the scheduler rather than
		// the change under test, which is exactly what it did before this
		// existed: a particle comparison swung 0.78 to 0.23 between two runs of
		// an identical build.
		float        FrameTime = 0.0f;

		// Start the editor in Play as soon as the scene is loaded.
		//
		// **This exists because a whole class of failure was unreachable
		// without it.** Play mode is where scripts run, where particles emit,
		// where skinned meshes animate -- and in the editor it is where a
		// *second* frame graph renders the Game panel beside the scene view.
		// None of that could be exercised without a person pressing a button,
		// so a crash that needed all of it at once could only ever be found by
		// hand and reported second-hand.
		bool         StartPlaying = false;

		// Record a full pipeline barrier between the editor's two render
		// graphs. A diagnostic, not a fix: it exists to answer one question
		// about the play-mode device loss, and costs enough GPU overlap that
		// nothing should ship with it on.
		bool         DebugGraphBarrier = false;

		// Write a PNG of one frame and exit.
		//
		// The only way to check what the engine actually put on screen without
		// a person looking at it, which matters because the interesting
		// failures are not crashes: a blank window, an image cleared after it
		// was drawn, a scene rendered through the wrong camera.
		std::string ScreenshotPath;
		uint32_t    ScreenshotFrame = 30;
		// How many consecutive frames from ScreenshotFrame to write. One is
		// the file as named; more writes <stem>_<frame><ext> for each, in one
		// run, which is the only way to look at a flicker: separate runs are
		// separate clocks, and consecutive frames of one are not.
		uint32_t    ScreenshotCount = 1;

		// Write a PNG of a frame drawn *during* loading, and keep going.
		//
		// A separate flag because the loading screen is over before the first
		// ordinary frame, so --screenshot can never catch it -- and a screen
		// nothing can capture is a screen nobody can check on two backends.
		// Which frame is deliberately not configurable: the second, because
		// the first is where ImGui is still deciding what size it is.
		std::string LoadingScreenshotPath;

		// Run this many frames, print what they cost, and exit. Zero is off.
		//
		// Exists because the only frame-time number this engine had came from a
		// panel a person read while vsync was on, which measured the display.
		// A flag makes the measurement repeatable, comparable between backends
		// and quotable in a commit message -- and it prints the vsync state
		// alongside the number, so the two cannot be separated again.
		uint32_t     BenchmarkFrames = 0;

		// Open this scene rather than the project's start scene. Relative to
		// the asset root, like every other scene reference.
		//
		// Added for benchmarking: a measurement needs a scene chosen to stress
		// something, and making that the project's start scene would change
		// what everyone else opens. It is generally useful beyond that -- a
		// bug report is much easier to act on with the scene attached.
		std::string  ScenePath;
		// --select=<entity name>: which entity the editor opens with selected.
		// A verification affordance in the same family as --screenshot and
		// --scene: an inspector widget can only be checked with something
		// selected, and driving the hierarchy by hand is not a check anybody
		// repeats. Ignored by the runtime, which has no inspector.
		std::string  SelectEntity;
		// --brush=mode,x,z,radius,strength,seconds[,layer]: one terrain brush
		// stroke on the selected entity's terrain, applied and saved once the
		// editor has loaded (ENGINE-NOTES 7ar). The same family as --select:
		// what lets a check hold the brush. Ignored by the runtime.
		std::string  BrushScript;

		// --camera=x,y,z,distance,yaw,pitch: where the editor's viewport camera
		// starts. Angles in degrees.
		//
		// The same family again, and added for the ground grid: an infinite
		// plane looks completely different at a grazing angle and from a long
		// way out, and both are exactly the cases that alias. Driving the camera
		// by hand to check them is not a check anybody repeats, and "it looked
		// fine from the default angle" is how a horizon full of moire ships.
		// Ignored by the runtime, which renders through the scene's camera.
		bool  HasCameraPose = false;
		Vec3  CameraFocus{ 0.0f, 0.0f, 0.0f };
		float CameraDistance = 10.0f;
		float CameraYaw = 0.0f;
		float CameraPitch = 0.0f;

		// How much to scale the editor's font and spacing.
		//
		// Zero means follow the monitor's content scale. It is *not* the
		// default, and that is deliberate: on a 150% display "follow the
		// monitor" gives a 27px font, which is correct by the OS's reckoning
		// and too large by the only reckoning that matters. Whether a scaled UI
		// is wanted depends on the panel, the viewing distance and the person,
		// so it is a setting rather than a detection -- and ragev.ini is the
		// natural place to put it once.
		float UIScale = 1.0f;

		// Which editor theme to open with: "dark", "light", or empty for
		// whatever was last chosen. A flag rather than only a menu item so a
		// screenshot of either is one argument, which is what makes checking
		// both themes at several window sizes a script instead of an afternoon.
		// Ignored outside the editor.
		std::string Theme;

		// The project to open. Empty falls back to RV_DEFAULT_PROJECT, which
		// CMake bakes in for builds run out of the build tree, and then to
		// nothing -- which is a valid state the editor shows rather than
		// pretending it has a project.
		std::string ProjectPath;

		// Which anti-aliasing filter to use, whatever the scene asked for.
		//
		// The same family as --scene and --camera. Comparing two filters means
		// rendering the identical frame three ways, and the alternative --
		// editing the scene file between runs -- changes the input as well as
		// the thing under test. It is also the only way to capture "no
		// anti-aliasing at all", which is the control every claim here is
		// measured against.
		// Also written by the editor's dropdown, into ragev.ini, so a choice
		// survives a restart without saving a scene -- see
		// SaveAntiAliasingPreference.
		bool         HasAAOverride = false;
		AntiAliasing AAOverride = AntiAliasing::None;

		// --ssaa=N. Zero leaves the scene's factor alone. Separate from the
		// mode because the two questions are separate: whether to supersample
		// is a look, and how far is a budget.
		int          SupersampleOverride = 0;

		// --raytracing=on|off. The same family as --aa: the check renders one
		// scene both ways and compares, and the alternative is editing the
		// project between runs. On, on a device without ray queries, resolves
		// to off -- shadow maps -- and the log says so (ENGINE-NOTES 7am, 7an).
		bool         HasRayTracingOverride = false;
		bool         RayTracingOverride = false;
		// --rt-reflections=on|off and --rt-ao=on|off (ENGINE-NOTES 7ao), for
		// the checks that render one scene both ways.
		bool         HasRayReflectionsOverride = false;
		bool         RayReflectionsOverride = false;
		bool         HasRayAoOverride = false;
		bool         RayAoOverride = false;
		bool         HasRayGiOverride = false;
		bool         RayGiOverride = false;
		// --voxel-gi=on|off (8.1, ENGINE-NOTES 7bc): the project's
		// VoxelGlobalIllumination, overridden for a run.
		bool         HasVoxelGiOverride = false;
		bool         VoxelGiOverride = false;
		// 0 means "not given"; 1 and 2 are the only values the flag accepts,
		// so there is no third state to mean anything else.
		int          GiBouncesOverride = 0;
		// A check's affordance (ENGINE-NOTES 7ba): the project's Render Settings
		// are replaced by `RenderSettings{}` at load, so a run depends only on
		// what its command line says. The other overrides still apply on top.
		// Never set by the editor -- it would save the defaults over the
		// user's project on the next Render Settings edit.
		// --graph=<relative path>: open a .rvgraph on the canvas at start
		// (8.10, ENGINE-NOTES 7bh). The editor's affordance for looking at one
		// without clicking through the browser, and what a check will drive.
		std::string  GraphPath;

		// --graph-drop-unknown=on: take the refusal page's "open without it"
		// as soon as the graph is opened, so a check can reach a path that
		// otherwise needs a click. Same route as the button, not a second one
		// -- a flag that took its own would be checking itself.
		bool         GraphDropUnknown = false;

		// --generate-graphs: turn every .rvgraph in the project into C# and
		// say what happened. What a build server runs instead of opening the
		// editor, and what the check drives.
		bool         GenerateGraphs = false;

		// --graph-zoom=<factor>: open the canvas at a zoom, so a screenshot can
		// show what one looks like. Zero leaves it at the framing default.
		float        GraphZoom = 0.0f;

		bool         RenderDefaults = false;
		// Whether `render-defaults` was stated at all, by either the ini or the
		// command line. A run that writes a screenshot is measuring something,
		// and a measurement that silently inherits whatever the owner last
		// saved in the project is not reproducible -- on a ray-capable machine
		// a saved `RayTracing: true` selects a different GI form entirely and
		// the run measures rays while its author believes it measured voxels.
		// So `--screenshot` requires this to be stated, either way; see
		// EngineConfig::Init.
		bool         HasRenderDefaults = false;

		// --msaa=N. Zero leaves the scene's count alone.
		int          MsaaOverride = 0;

		// Whether opaque batches are ordered nearest-first before drawing.
		//
		// On by default: early-z only skips shading a pixel once something
		// nearer has written depth, and on a scene of 200 full-screen slabs
		// this is 0.32 ms against 33.9 ms -- a factor of 105. Off exists for
		// the same reason --import-cache=off does: the win depends entirely
		// on how much a scene overlaps itself, so it has to stay measurable
		// rather than remembered. On a spread-out 1500-mesh scene the same
		// switch is worth a few percent.
		bool         DepthSortOpaque = true;

		// Whether the lit pass reads material textures through the bindless
		// heap (ENGINE-NOTES 7al) on a device that has one.
		//
		// On by default and silently absent on OpenGL, which has no heap. Off
		// exists for one reason: on Vulkan both paths run, so rendering the
		// same scene with this on and off and comparing the pixels is the
		// check that the whole feature is right -- the two must be identical.
		// The same reason --depth-sort=off exists: a switch that keeps a
		// change measurable rather than remembered.
		bool         Bindless = true;

		// Whether cooked assets may be read from and written to the project's
		// import cache (ENGINE-NOTES 7l).
		//
		// On by default, because the cache is the difference between a boot
		// that decodes 198 MB of PNG and one that does not. Off exists to make
		// the comparison *repeatable*: cooking is lossy by design, so the
		// acceptance test for it is a bounded pixel diff of the same scene
		// rendered both ways, and without a switch that diff means deleting a
		// folder and hoping nothing else moved. The same reason --benchmark
		// and --frame-time exist.
		bool         UseImportCache = true;

		// Whether to open an audio device. Off means the engine still tracks
		// every sound it would have played and simply plays none of them, which
		// is the same path a machine with no output device takes -- so turning
		// this off is how that path gets exercised rather than assumed.
		bool         EnableAudio = true;

		// Startup window size. Worth having as a flag rather than a constant:
		// panel layout only misbehaves at sizes you have to reproduce to see.
		uint32_t     WindowWidth = 1600;
		uint32_t     WindowHeight = 900;
		// Off unless asked, in every configuration. The layers cost ~1.5 ms of
		// CPU per frame -- measured; it more than doubled an editor frame and
		// made Vulkan read as slower than OpenGL when it is faster -- and a
		// cost like that should be a choice, not a default. `validation = on`
		// in ragev.ini or --validation=on turns them on for GPU debugging.
		bool         EnableValidation = false;
		// --validation=gpu: the layers instrument every shader to check what
		// it actually reads. Slow, and the only thing that reports an
		// out-of-range index into a bindless array -- ordinary validation
		// cannot know which element a shader will touch (ENGINE-NOTES 7al).
		// Turns synchronization validation off for the run; the two do not
		// share a process well.
		bool         ValidationGpuAssisted = false;

		// Parses ragev.ini (if present) then the command line. Call once, before
		// anything creates a window.
		static void Init(int argc, char** argv);

		static const EngineConfig& Get();

		// The one setting here that changes while the engine is running.
		//
		// Everything else is fixed at startup by design -- the backend decides
		// how the window is created, so it cannot move. Anti-aliasing can, and
		// the editor's dropdown has to take effect on the next *frame* rather
		// than the next launch, or picking a filter would do nothing visible
		// until a restart.
		static void SetAntiAliasingOverride(AntiAliasing aa);

		static const char* BackendName(RHI::Backend backend);

		// Writes `rhi=` into ragev.ini beside the executable, preserving every
		// other line.
		//
		// So that the editor's backend picker survives a plain restart and not
		// only the relaunch it offers. The backend is a restart-time choice by
		// design -- the window itself is created differently per backend -- so
		// the only way to make it changeable from the UI is to record the
		// choice and act on it next time.
		static bool SaveBackendPreference(RHI::Backend backend);

		// Writes `vsync=` the same way, so the editor's checkbox survives a
		// plain restart rather than only the session it was clicked in.
		static bool SaveVSyncPreference(bool enabled);

		// And `aa=`, for the same reason and with one difference worth
		// stating: anti-aliasing is *also* a project property, so writing it
		// here makes it an override that applies to every project this
		// installation opens.
		//
		// That is the intent rather than an accident. Which filter to run is a
		// judgement about this machine and this monitor -- the same judgement
		// vsync and the backend are -- and having to re-pick it every launch is
		// not how a viewing preference should behave. A packaged game still
		// uses what its project stores, because ragev.ini is the developer's
		// file and does not ship.
		static bool SaveAntiAliasingPreference(AntiAliasing aa);

		// And `width=`/`height=`, so the editor comes back the size it was
		// left at.
		//
		// The same two keys `--width`/`--height` set, which is the point: the
		// size is restored by the machinery that already reads them rather
		// than by a second path that would have to be kept in step. Ignored
		// when either is zero -- a minimised window is not a size anybody
		// asked to come back to.
		static bool SaveWindowSize(uint32_t width, uint32_t height);

	private:
		// Rewrites one `key=value` line in ragev.ini, preserving every other.
		static bool SaveSetting(const std::string& key, const std::string& value);

		static bool ApplyKeyValue(EngineConfig& config, std::string key, std::string value);
		static void LoadFile(EngineConfig& config, const std::filesystem::path& path);
		static void LoadCommandLine(EngineConfig& config, int argc, char** argv);
	};
}
