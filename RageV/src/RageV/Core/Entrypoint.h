#pragma once

#include "RageV/Core/EngineConfig.h"

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

	return 0;
}

#else
#error RageV only supports Windows!
#endif
