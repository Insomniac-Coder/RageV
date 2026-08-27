#pragma once

#include "RageV/Core/Core.h"

namespace RageV
{
	// **A stack when the process dies, instead of an exit code.**
	//
	// There is no debugger on every machine this runs on, and a crash under a
	// check script or a benchmark is a number in a terminal -- 3221225477, or
	// 139 through a shell -- which says nothing at all about where it happened.
	// The ray-instance defect fixed on 2026-08-27 sat unexplained for a session
	// because of exactly that: the last log line named a subsystem that turned
	// out to be innocent, and finding the real frame needed a debugger the
	// machine did not have.
	//
	// So the process prints its own stack on the way out: the exception, what
	// it touched, and the frames, with file and line wherever symbols are
	// available.
	//
	// **It stands aside for a real debugger.** With one attached the handler
	// returns without printing, because a live debugger is strictly better than
	// this and breaking at the fault is the whole reason to have attached.
	//
	// Install once, early -- before any window, device or scene exists, so a
	// failure during startup is covered too.
	void RV_API InstallCrashHandler();
}
