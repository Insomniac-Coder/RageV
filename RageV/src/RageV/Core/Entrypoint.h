#pragma once

#include "RageV/Core/EngineConfig.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"

#ifdef RV_PLATFORM_WINDOWS

#ifdef RV_DEBUG
	#include <crtdbg.h>
	#include <cstdlib>
	#include <windows.h>
#endif

extern RageV::Application* RageV::CreateApplication();

namespace RageV::Detail
{
	// Make a failed assertion end the process instead of parking it on a modal
	// dialog.
	//
	// The dialog is the right behaviour under a debugger, where "Retry" is how
	// you get a stack. Run without one -- from a terminal, from a script, or by
	// double-clicking -- and it is a trap: the process never exits, its window
	// never closes, and every subsystem it owns stays live. The audio device is
	// the one you notice, because a stalled mixer keeps repeating a fragment of
	// whatever it was playing, and several abandoned instances at once sound
	// like the machine has developed a fault.
	//
	// So: dialog when a debugger is attached, message on stderr and a non-zero
	// exit when not. The information is the same either way; only the thing
	// that happens afterwards differs.
	inline void ConfigureCrashBehaviour()
	{
#ifdef RV_DEBUG
		if (IsDebuggerPresent())
			return;

		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

		for (int report : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
		{
			_CrtSetReportMode(report, _CRTDBG_MODE_FILE);
			_CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
		}
#endif
	}
}

int main(int argc, char** argv)
{
	RageV::Detail::ConfigureCrashBehaviour();

	RageV::Log::Init();

	// Must run before anything creates a window: the window is created
	// differently depending on the graphics backend.
	RageV::EngineConfig::Init(argc, argv);

	RageV::Application* app = RageV::CreateApplication();
	app->Run();
	delete app;

	// A measurement whose shaders did not all compile is not a measurement
	// (ENGINE-NOTES 7be, hole 1). Shaders compile at runtime, so the build
	// that produced this exe proves nothing about them; 8.13's lit shader
	// never compiled once, every GI fixture rendered pure black, and this
	// returned 0 for all of it. A screenshot or a benchmark is a number
	// somebody is going to believe, so it fails loudly instead.
	//
	// Only for those two. An interactive session is where a person edits a
	// shader, sees the error and fixes it, and exiting non-zero because of a
	// mistake made and corrected an hour earlier would be noise.
	const RageV::EngineConfig& config = RageV::EngineConfig::Get();
	const bool measuring = !config.ScreenshotPath.empty() || config.BenchmarkFrames > 0;
	const uint32_t failures = RageV::RHI::ShaderCompiler::FailureCount();
	if (measuring && failures > 0)
	{
		RV_CORE_ERROR("{0} shader(s) failed to compile this run, the first being "
					  "'{1}'. Whatever was captured was rendered without them, so "
					  "it is not a measurement -- exiting 3 rather than reporting "
					  "it (ENGINE-NOTES 7be).",
					  failures, RageV::RHI::ShaderCompiler::FirstFailure());
		return 3;
	}

	return 0;
}

#else
#error RageV only supports Windows!
#endif
