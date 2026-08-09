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
//   --validation=on|off     Vulkan validation layers (no effect without the SDK)
//   --frames-in-flight=N
//   --fixed-hz=N            simulation steps per second (default 60)
//   --width=N --height=N    window size
//   --audio=on|off          open an output device at all
//   --project=<path>        the .rvproject to open, or a folder containing one
//   --screenshot=<file>     write a PNG of one frame and exit
//   --screenshot-frame=N    which frame to capture (default 30, to let the
//                           scene settle and any first-frame allocation pass)
//   --benchmark=N           run N frames, print a frame-time summary, exit
//   --scene=<path>          open this scene instead of the project's start scene
//   --ui-scale=N|auto       editor UI scale; auto follows the monitor

#include "RageV/Renderer/RHI/RHITypes.h"
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

		// Write a PNG of one frame and exit.
		//
		// The only way to check what the engine actually put on screen without
		// a person looking at it, which matters because the interesting
		// failures are not crashes: a blank window, an image cleared after it
		// was drawn, a scene rendered through the wrong camera.
		std::string ScreenshotPath;
		uint32_t    ScreenshotFrame = 30;

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

		// The project to open. Empty falls back to RV_DEFAULT_PROJECT, which
		// CMake bakes in for builds run out of the build tree, and then to
		// nothing -- which is a valid state the editor shows rather than
		// pretending it has a project.
		std::string ProjectPath;

		// Whether to open an audio device. Off means the engine still tracks
		// every sound it would have played and simply plays none of them, which
		// is the same path a machine with no output device takes -- so turning
		// this off is how that path gets exercised rather than assumed.
		bool         EnableAudio = true;

		// Startup window size. Worth having as a flag rather than a constant:
		// panel layout only misbehaves at sizes you have to reproduce to see.
		uint32_t     WindowWidth = 1600;
		uint32_t     WindowHeight = 900;
#ifdef RV_DEBUG
		bool         EnableValidation = true;
#else
		bool         EnableValidation = false;
#endif

		// Parses ragev.ini (if present) then the command line. Call once, before
		// anything creates a window.
		static void Init(int argc, char** argv);

		static const EngineConfig& Get();

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

	private:
		// Rewrites one `key=value` line in ragev.ini, preserving every other.
		static bool SaveSetting(const std::string& key, const std::string& value);

		static bool ApplyKeyValue(EngineConfig& config, std::string key, std::string value);
		static void LoadFile(EngineConfig& config, const std::filesystem::path& path);
		static void LoadCommandLine(EngineConfig& config, int argc, char** argv);
	};
}
