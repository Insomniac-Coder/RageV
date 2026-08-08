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

	private:
		static bool ApplyKeyValue(EngineConfig& config, std::string key, std::string value);
		static void LoadFile(EngineConfig& config, const std::filesystem::path& path);
		static void LoadCommandLine(EngineConfig& config, int argc, char** argv);
	};
}
