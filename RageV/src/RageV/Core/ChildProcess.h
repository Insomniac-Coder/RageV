#pragma once

// A child process whose whole tree can be killed mid-run.
//
// This replaced _popen in the build pipeline for two reasons, both of them
// cancellation:
//
//   - _popen gives back a FILE*, not a process handle. There is nothing to
//     terminate, so a build started through it runs to the end no matter what
//     the person who started it wants.
//   - Even with a handle, killing cmake would not kill the MSBuild and cl.exe
//     processes it spawned. The child is created inside a Job Object with
//     KILL_ON_JOB_CLOSE, so cancelling -- or the editor crashing -- takes the
//     whole tree down, not just the root of it.
//
// It also removed a trap rather than inheriting one: _popen ran everything
// through `cmd /c`, whose quote-stripping rules once cost a session (the whole
// command line needed one extra pair of quotes -- see HANDOFF section 10).
// CreateProcess parses arguments itself, so a quoted program path is just a
// quoted program path.
//
// Run() blocks. Callers who want the editor interactive put it on a worker
// thread and hand it the cancel flag; that is the editor's job, not this
// class's.

#include <atomic>
#include <functional>
#include <string>

namespace RageV
{
	class ChildProcess
	{
	public:
		// Called with each chunk of output as it arrives, from the thread
		// running Run(). This is what a live build console is made of; a
		// caller that only wants the final text ignores it and reads
		// Result::Output.
		using OutputSink = std::function<void(const char*, size_t)>;

		struct Result
		{
			// False when the executable could not be started at all -- which is
			// how a missing tool reads, since nothing searches PATH in advance.
			bool Launched = false;

			// True when the cancel flag stopped it. ExitCode is meaningless
			// then; the tree was terminated, not finished.
			bool Cancelled = false;

			int ExitCode = -1;

			// stdout and stderr merged, in arrival order. Build tools split
			// diagnostics across both, and a log in two halves reorders itself
			// unhelpfully.
			std::string Output;
		};

		// Runs `commandLine` to completion or cancellation, capturing
		// everything it prints. `cancel` may be null for a run nothing will
		// ever want to stop -- a version probe, a test.
		static Result Run(const std::string& commandLine,
						  const std::atomic<bool>* cancel = nullptr,
						  const OutputSink& sink = {});
	};
}
