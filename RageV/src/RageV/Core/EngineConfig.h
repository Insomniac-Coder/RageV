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
//   --bake=on|off           solve the scene's irradiance fields and write them
//                           beside the scene, for later runs to load instead of
//                           solving (default off)
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
//   --pass-timings=on       GPU time every render-graph pass (implied by --benchmark)
//   --gpu-cull=on|off       cull the depth views in compute (8.3) or on the
//                           CPU; on where the device allows it. Off draws the
//                           same picture the old way, for comparison.
//   --gpu-lit=on|off        the same for the lit pass. UNFINISHED, off by
//                           default: correct and much faster on static-only
//                           scenes, wrong and flickering on mixed ones.
//   --render-defaults=on|off  ignore the project's Render Settings and use
//                           the struct's defaults, so a check depends only on
//                           its own command line (ENGINE-NOTES 7ba). The
//                           overrides above still apply on top.
//   --ssaa=N                how many times larger SSAA draws each axis
//   --msaa=N                coverage samples per pixel for MSAA: 2, 4 or 8
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
//   --ray-budget=<ms>       hold this GPU frame time by scaling ray counts;
//                           0 (default) spends the quality rung's full count
//   --shadow-rays=<shape>,<start>,<end>[,<floor>][,lit]  override the project's
//                           shadow-ray falloff (WR-17): off, or hard | linear
//                           | smooth | log | share with the distances in metres
//   --light-cutoff=<m>      override the project's light cutoff (WR-17): no
//                           positional light reaches past this many metres
//   --rt-optimisation=off|quality|balanced|performance  override the project's
//                           RT optimisation preset (WR-17) for one run
//   --ray-rate=1|2          the water's mirror and refraction rays per pixel
//                           (WR-18): 1 every pixel, 2 one lane per 2x2 quad
//   --refraction-floor=<t>  the transmittance under which the water's
//                           refraction ray stops looking (WR-18); 0 = 300 m
//   --hit-lights=on|off     measurement only: off skips the light walk at
//                           every traced hit, so the ray's cost and the
//                           walk's can be told apart (WR-10)
//   --casting-lights=N      measurement only: under rays, only the first N
//                           positional lights keep a shadow ray; the rest
//                           light without one (WR-16 S0's light-count sweep)
//   --light-sampling=K[,target]  WR-16 S4: a live surface with more lamps
//                           reaching it than K scores them cheaply, keeps K
//                           by weighted reservoir sampling, and shades and
//                           traces only those -- the unbiased estimate S1
//                           measured. target is 'term' (the default: the
//                           unshadowed term's luminance, the water's own lobe
//                           with its Fresnel and masking) or 'irradiance'
//                           (S1's cheap target, kept as the arm it lost as).
//   --shade-lights=N        measurement only (WR-16 S4's sizing): a fragment
//                           or a traced hit shades at most N positional
//                           lamps and skips the rest where the eighty-byte
//                           read begins -- the cheap sixteen-byte walk stays,
//                           as a sampler's would. The picture is wrong on
//                           purpose; the frame time bounds what choosing a
//                           few lamps per pixel can win.
//   --shadow-budget=K[,full] measurement only (WR-16 S1): each pixel traces
//                           K shadow rays in all, to K lamps chosen by
//                           importance from its cluster list, and takes the
//                           lamps' light through the unbiased sampling
//                           weights; 1, 2, 4 or 8. Replaces the thinning.
//                           ",full" weighs candidates by their whole
//                           unshadowed term instead of the cheap irradiance.
//   --debug-view=rays|lights|confidence|importance|importance-gi
//                           replace the picture with a heat map: rays cast
//                           per pixel, lights walked per pixel, the temporal
//                           resolve's history validity, or the ray budget's
//                           per-tile allocation (WR-16 S0). `importance` is
//                           the AO lane and `importance-gi` the bounce's --
//                           the two are allocated separately and can be
//                           restless separately, so a judge that reads only
//                           the first cannot see half of what it grades.
//                           Vulkan only.
//   --debug-view-mix=<0..1> how much of the scene shows through a debug
//                           view. 0.2 by default; 0 makes the map exact,
//                           which is what a test that counts changed
//                           pixels needs.
//   --tile-dead-band=<rays> WR-16 S3, measurement: the allocator's three
//   --tile-dwell=<frames>   stillness levers, overriding RenderSettings for
//   --tile-smooth=<w>       one run. How far a tile's continuous demand must
//                           move before its whole number follows; how long it
//                           must then sit still; and how much of this frame's
//                           demand it believes against the average it holds.
//                           `--tile-dead-band=0 --tile-dwell=0 --tile-smooth=1`
//                           is the undamped allocator the still test measures
//                           against. Dwell is capped at 31 frames, both lanes'
//                           counters sharing one half-float lane.

#include "RageV/Renderer/RHI/RHITypes.h"
#include "RageV/Renderer/RenderSettings.h"
#include "RageV/Math/Math.h"
#include <string>
#include <filesystem>

namespace RageV
{
	struct EngineConfig
	{
		// **Vulkan, not OpenGL.** Every feature worth measuring on this engine
		// -- ray tracing, the traced bounce, occlusion, the runtime cache --
		// exists only on the Vulkan path, so an unqualified run defaulting to
		// OpenGL is a run with the subject of the work switched off. It also
		// silently wasted a lot of time: a build launched without `--rhi` came
		// up on the backend that cannot do any of it.
		RHI::Backend Backend        = RHI::Backend::Vulkan;

		// Every backend this build may use, in the order to try them.
		//
		// **A packaged game names this and nothing else does.** `Backend` above
		// is the one to start with -- what `--api=` and `rhi =` set -- and this
		// is what happens when its driver is not there. `RHIDevice::Create` has
		// no fallback of its own: a Vulkan device that will not create returns
		// null and the application stops, which for a player is a game that
		// does not open and says nothing useful.
		//
		// Empty means "only Backend", which is what the editor and every
		// command line get: a developer who asked for Vulkan wants to know that
		// Vulkan failed, not to be quietly moved to OpenGL and left measuring
		// the wrong renderer.
		std::vector<RHI::Backend> Backends;
		// **Off.** A developer build exists to be measured, and vsync pins
		// every frame to the display's refresh -- which hides a regression
		// until it costs a whole frame interval, and makes a win invisible.
		// A packaged game still ships with it on (ProjectPackager), because a
		// player wants a whole picture rather than a number.
		bool         VSync          = false;
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
		// The runtime honours it too: RuntimeLayer poses the scene's primary
		// camera from it, so a benchmark can name its shot (HANDOFF's
		// night-scene protocol). The camera's own projection is kept.
		// **A GPU frame time to hold, in milliseconds, by spending fewer
		// rays.** Zero -- the default -- means the quality rungs get their
		// full ray counts and a costly view simply takes longer.
		//
		// It exists because ray counts are otherwise fixed per rung while
		// their *cost* is not: an ambient-occlusion ray that hits geometry
		// costs far more than one that escapes, so the same eight rays a
		// pixel measured 3.30 ms with the showroom's car close and 2.17 ms
		// with it far. Making the count the variable is what turns that into
		// a flat frame instead of a visible dip.
		float RayBudgetMs = 0.0f;

		// --shadow-rays=<shape>,<start>,<end>[,<share>]: the project's shadow-ray
		// falloff (RenderSettings::ShadowRayFade, WR-17), overridden for one run
		// so the falloff matrix renders one scene every way without editing the
		// project between runs. The shape is RenderSettings::ShadowRayFalloff's
		// order as an integer.
		bool  HasShadowRayOverride = false;
		int   ShadowRayShapeOverride = 0;
		float ShadowRayStartOverride = 0.0f;
		float ShadowRayEndOverride = 0.0f;
		float ShadowRayShareOverride = 0.0f;
		// --light-cutoff=<m>: the preset's cutoff, overridden for one run.
		bool  HasLightCutoffOverride = false;
		float LightCutoffOverride = 0.0f;
		// --rt-optimisation=<level>: RenderSettings::RtOptimisation for one run,
		// in RayOptimisation's order, so each preset can be benchmarked without
		// editing the project. The two overrides above still win over it.
		bool  HasRayOptimisationOverride = false;
		int   RayOptimisationOverride = 0;
		// --ray-rate=1|2 and --refraction-floor=<t>: WR-18's two preset columns,
		// overridden for one run so each can be measured on its own.
		bool  HasRayRateOverride = false;
		float RayRateOverride = 1.0f;
		bool  HasRefractionFloorOverride = false;
		float RefractionFloorOverride = 0.0f;
		// --hit-lights=off: a measurement, the traced hit's light walk skipped.
		bool  HitLights = true;
		// --casting-lights=N: a measurement (WR-16 S0). Under rays only the
		// first N positional lights keep their shadow ray. Negative -- the
		// default -- leaves every light as authored.
		int   CastingLights = -1;
		// --shadow-budget=K: a measurement (WR-16 S1, the fixed-budget
		// pre-check). Under rays each pixel traces K shadow rays in all, to K
		// lamps chosen by weighted reservoir sampling on a cheap importance
		// (unshadowed irradiance), and takes the lamps' light through the
		// importance-sampling weights -- unbiased, and as noisy as K allows.
		// Zero, the default, leaves the per-light rays and the thinning as
		// they are. Carried to the shader in RayRates.w, K in the low four
		// bits and the target above them: `--shadow-budget=K,full` weighs the
		// candidates by their whole unshadowed term instead of the cheap
		// irradiance -- the S4 design question the water's glitter forces.
		int   ShadowBudget = 0;
		bool  ShadowBudgetFullTarget = false;

		// --tile-dead-band=<rays> and --tile-dwell=<frames>: WR-16 S3's two
		// stillness levers, overridden for one run so the sixty-second still
		// test can sweep them without editing the project. Both zero is the
		// undamped allocator -- every tile's count re-derived from scratch
		// each frame -- which is the baseline the test grades against.
		bool  HasTileDeadBandOverride = false;
		float TileDeadBandOverride = 0.0f;
		bool  HasTileDwellOverride = false;
		float TileDwellOverride = 0.0f;
		// --tile-smooth=<w>: the third lever, and the one that works. The two
		// above ration how often a tile may change its count; this removes
		// what was making it want to. `w` is how much of this frame's demand
		// a tile believes, the rest coming from the average it held; 1 is the
		// undamped allocator and an eighth is one jitter cycle's memory.
		bool  HasTileSmoothOverride = false;
		float TileSmoothOverride = 1.0f;

		// --shade-lights=N: a measurement (WR-16 S4's sizing, 2026-09-04).
		// A fragment or a traced hit walks every lamp's sixteen-byte cull
		// record as it does today -- as S4's sampler would, to score
		// candidates -- but only the first N lamps past that record are read
		// in full and shaded at all; the rest cost nothing, ray included.
		//
		// **Why this and not --casting-lights.** That flag removes the shadow
		// ray and leaves the shading, which is the half the calibration found
		// bigger: 77 to 125 lamps evaluated per fragment. S2 made the static
		// pixels cheap by leaving fully baked lamps to the field, but the
		// water is live by the standing rule and still shades every lamp that
		// reaches it. This flag bounds what S4's "shade only the survivors"
		// can win, which nothing shipped can show. The picture is wrong on
		// purpose (the lamps past N are simply absent).
		//
		// Negative -- the default -- shades every lamp. Carried to the shader
		// in RayRates.w's bits 8 and up, as N + 1 so that zero means off.
		int   ShadeLights = -1;

		// --light-sampling=K[,target]: WR-16 S4's sampler. K reservoirs per
		// pixel over the cell's lamps, scored by the target, and only the K
		// survivors shaded and traced. Zero -- the default -- leaves every
		// lamp shaded as it is today. The target: 0 the cheap irradiance S1
		// measured unusable on the water, 1 the same with the specular lobe's
		// magnitude added, which is what the water's glitter needs. Carried
		// in RayRates.w's bits 16-19 and 20-21.
		// Set only when the flag was given, so `--light-sampling=0` can turn
		// the sampler OFF for one run against a project that carries it on --
		// which is the A/B the measurement scripts need.
		bool  HasLightSamplingOverride = false;
		int   LightSampling = 0;
		int   LightSamplingTarget = 1;
		// --water-lamp-pass=on|off (WR-16 S4b): whether the sea's lamps are
		// chosen and shaded in their own two passes, or by the sampler inside
		// the water shader. The same estimate either way -- the passes add the
		// neighbours' choices to it, which a fragment cannot reach -- so the
		// two arms are what says what that reuse is worth. On by default
		// wherever --light-sampling asks for lamps at all.
		bool  WaterLampPass = true;
		// --water-lamp-reuse=on|off: whether the lamp passes keep the choice
		// across frames and read the neighbours', or start from this pixel's
		// own sweep every frame. Off is the same estimate the sampler inside
		// the water shader makes, which is what makes the pair an A/B of the
		// reuse alone.
		//
		// **Off by default since 2026-09-04, measured.** Both halves lose on
		// all three cameras: the sea retilts every patch every frame, so a
		// choice kept from the last frame -- or borrowed from a pixel twelve
		// across -- was made for a surface that has since turned, and on water
		// the score is nearly all specular, which turns with it. Pier, error
		// against everything traced under TAA: 1.32% with neither, 8.55% with
		// the history, 4.51% with the neighbours, 6.45% with both; and the
		// flicker rises with it, 8.3% of pixels blinking to 11.9%. It costs
		// 0.4 to 2.1 ms to make the picture worse.
		bool  WaterLampReuse = false;
		// --water-lamp-history=on|off and --water-lamp-neighbours=on|off: the
		// two halves of the reuse, switched apart. Keeping a choice across
		// frames and borrowing one from a neighbour are different mechanisms
		// with different ways of being wrong, and the single switch above
		// could only say that the pair cost the picture, never which of them.
		// --water-lamp-reuse stays the master: off means neither runs.
		bool  WaterLampHistory = true;
		bool  WaterLampNeighbours = true;
		// --water-lamp-tuning=taps[,cap]: the two numbers the reuse was built
		// with by assertion -- how many neighbours a pixel reads (a ring of
		// three) and the most confidence a history may carry (twenty frames).
		// Both are carried to the shader so a matrix can move them without a
		// rebuild per arm; neither has ever been measured.
		int   WaterLampTaps = 3;
		int   WaterLampCap = 20;

		// --water-lamp-accumulate=on|off and --water-lamp-memory=scatter,glint
		// (WR-16 S4c): whether the sea's lamp light is averaged with the frames
		// behind it, and how many frames each half may carry. Two numbers
		// rather than one because the halves go stale at different rates -- the
		// light entering the water is broad and slow and holds for many frames,
		// while the light glinting off it is the specular, which is exactly
		// what a turning wave changes; one memory long enough for the first
		// would smear the second into a haze.
		bool  WaterLampAccumulate = true;
		// **Four and two, chosen on the picture** (2026-09-04). Sixteen and
		// four take the flicker furthest -- 2.01% of pixels blinking against
		// the shipped preset's 3.84 -- but they cost the sea a third of its
		// fine detail, 2.72 against the traced reference's 4.29, and the water
		// stops reading as water. These keep 3.39 of that 4.29 and still bring
		// the blink to 4.18, near the preset's own. The flicker was never the
		// only axis; it was the only one that had a number until the blur got
		// one too.
		int   WaterLampMemoryScatter = 4;
		int   WaterLampMemoryGlint = 2;
		// --water-lamp-clamp=N: how many standard deviations of the pixel's
		// own neighbourhood a history may sit from its mean before it is
		// pulled back. Their outright range is the obvious bound and is wrong
		// here -- four lamps out of a hundred make a pixel legitimately
		// brighter than all eight neighbours often enough that the range
		// clips the average back into the noise every frame.
		float WaterLampClamp = 4.0f;

		// --water-ablate=<names>: a measurement. Each name zeroes one piece
		// of the sea's shading so its cost can be taken by subtraction --
		// `reflection`, `refraction`, `lamps`, comma separated. The picture
		// is wrong on purpose and the time is honest, the same contract
		// --shade-lights carries. It exists because the water draw's cost
		// was otherwise a single unexplained lump: every other number in
		// WR-16 came from turning something off, and nothing could turn
		// these off.
		int   WaterAblate = 0;

		// **How many pixels one lamp choice covers.** Two: the choice is made
		// once per 2x2 block and all four pixels shade the lamps it names,
		// each with its own normal and its own shadow rays. Measured on
		// Headland at 1440p -- the choose pass 4.06 ms per pixel against 1.20
		// per block, 3.8 ms off the frame, shade unchanged, and 0.18% of
		// pixels differing by more than six levels with a signed mean of
		// zero. Sweeping the cell's ~146 candidates is what that pass costs;
		// sweeping a quarter as often is the only thing that divides it.
		//
		// Not a setting: it is how the sea's lamps are chosen, not a trade a
		// project makes. It falls back to one only under the reuse, whose
		// history and neighbour paths address the choice buffer and the
		// surface buffer with a single texel -- two different pixels once the
		// two differ in size. The reuse is off and measured to lose; the
		// fallback keeps that arm runnable for anyone re-testing it.
		static int ChooseBlock(bool reuse) { return reuse ? 1 : 2; }

		// --water-reflection=full|half|quarter (WR-16 S5): the sea's mirror ray traced in a pass
		// of its own at 1/n the picture's width and height -- 2 for half, 4
		// for a quarter -- instead of once per 2x2 quad inside the water
		// shader. The quad share quarters the rays and saves only a fifth of
		// the time, because the three lanes that do not trace wait rather
		// than working; a smaller pass has nothing idle. Zero, the default,
		// leaves the quad share alone.
		int   WaterReflectionScale = 0;

		// --world-grid=on|off (WR-16 S4): whether a ray's hit that lands
		// outside the camera's frustum reads the world-space lamp grid or
		// falls back to walking every light in the scene, which is what it did
		// before the grid existed. A measurement switch, not a quality one --
		// the two answers are identical, because a cell holds every light
		// whose range reaches it -- so the only thing it can move is time, and
		// this laptop's GPU drifts too much between sessions to compare a
		// number taken before a rebuild with one taken after.
		bool  WorldLightGrid = true;
		// --lamp-probe=x,y: one water pixel's arithmetic, written to a buffer
		// by the shading pass and printed by the CPU. A picture cannot show
		// the sum of the scores a pixel swept or the score of the lamp it
		// kept, and those two are what the estimate turns on. Negative -- the
		// default -- writes nothing.
		int   LampProbeX = -1;
		int   LampProbeY = -1;

		// **--debug-view=<what>** (WR-16 S0): the frame replaced by a heat map
		// of one number per pixel, read from the counts the lit shaders write
		// under RV_DEBUG_VIEW (rays, lights), from the temporal resolve's
		// second attachment (confidence), or from the ray budget's tile map
		// (importance -- the allocation the importance became, which is the
		// map the consumers read). The owner's multi-light document asks for
		// these views before any budgeting is built, so a hot pixel is seen
		// rather than inferred from a mean.
		// **--debug-view-mix=<0..1>**: how much of the tone-mapped frame the
		// heat map is drawn over. The default fifth is what makes a map
		// readable by a person -- and it is also what made the sixty-second
		// still test count the moving sea as changed allocations, since a
		// fifth of an animated frame is 51 levels a channel. Zero for any
		// test that reads the picture as data.
		float DebugViewMix = 0.2f;

		enum class DebugViewMode { None, Rays, Lights, Confidence, Importance,
								   GiImportance };
		DebugViewMode DebugView = DebugViewMode::None;

		// **--gi-source=baked|realtime.** Which form of indirect light to use,
		// stated rather than inferred. Without it the only lever is whether a
		// matching bake happens to be on disk, so the only way to see the
		// realtime path is to move somebody's data out of the way -- and a test
		// that edits the project it is measuring is a test that can lose it.
		// **--slow-frames=<ms>: name the pass that caused a hitch, as it
		// happens.** A benchmark reports means over a run, which is exactly the
		// wrong statistic for a drop somebody sees while moving: the frames
		// that hurt are a handful out of six hundred, and averaging buries
		// them. This prints the pass breakdown of any frame over the threshold,
		// so an interactive session produces evidence instead of an impression.
		float SlowFrameMs = 0.0f;


		bool  HasGiSourceOverride = false;
		bool  GiSourceBaked = true;

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
		// Time every render-graph pass, on both processors. Implied by
		// --benchmark; off otherwise, because seventy timestamps a frame is
		// not free.
		//
		// Three states, not two. `--benchmark` used to force this on with an
		// OR, so there was no way to measure a frame the profiler was not
		// inside -- which makes "how much does the profiler cost" a question
		// the engine could not answer about itself. An explicit
		// --pass-timings=off now wins over the implication.
		bool         PassTimings = false;
		bool         HasPassTimingsOverride = false;
		// Whether the depth views decide what they can see in a compute pass
		// (roadmap 8.3) or by walking the scene on the CPU. On by default
		// where the device allows it; off is the same picture drawn the old
		// way, which is what makes the two directly comparable -- and an
		// escape hatch if a driver ever disagrees about indirect draws.
		bool         GpuCull = true;
		// Whether the static casters' shadow depth draws run as meshlets
		// through a mesh-shading pipeline (roadmap 8.3's second half,
		// VK_EXT_mesh_shader, Vulkan only). Off by default: the classic
		// vertex path draws the identical image, this is the first stage of
		// the meshlet work rather than the last, and a device without the
		// extension ignores the flag with a log line. When on, the shadow
		// views come off the GPU-cull path for static casters -- per-meshlet
		// frustum culling in the mesh stage is what replaces it there.
		bool         Meshlets = false;
		// The same for the *lit* pass (roadmap 8.3). **Unfinished**: it draws a
		// pure-static scene pixel for pixel identically to the CPU path and
		// takes sixty thousand objects from 55 to 73 FPS, but a scene that
		// mixes GPU-drawn static meshes with CPU-drawn skinned ones renders
		// visibly differently and flickers. On, so the defect is in front of
		// whoever is working on it; --gpu-lit=off is the way back.
		bool         GpuLit = true;
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

		// Whether Build Game opens the finished build in the file manager.
		// Written by the dialog's checkbox; see SaveRevealAfterBuildPreference.
		bool RevealAfterBuild = false;

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

		// **Solve the scene's lighting and write it to disk.**
		//
		// Off by default, because a bake overwrites authored content: the file
		// it writes is the answer every later run will trust without checking
		// the scene again beyond its stamp. So it happens when somebody asks
		// for it, and never as a side effect of running the game.
		bool         BakeLighting = false;

		// **And do it again even where a file already matches** (`--bake=force`).
		//
		// The two are different requests. `on` produces what a scene is
		// missing, so it loads a bake whose stamp still fits and writes
		// nothing -- which is what makes it cheap to leave on. `force` is the
		// command-line half of the inspector's Bake button: the stamp covers
		// the box, the grid and the lights, and *not* the geometry or the
		// materials, so after a wall moves the only thing that knows the stored
		// answer is stale is the person who moved it.
		bool         ForceLightingBake = false;

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

		// And `reveal-after-build=`, for the Build Game dialog's checkbox.
		//
		// A preference about how this person likes to work rather than
		// anything about the project, which is exactly what ragev.ini is for:
		// somebody who wants the folder every time should tick it once, not
		// once per launch.
		static bool SaveRevealAfterBuildPreference(bool enabled);

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
