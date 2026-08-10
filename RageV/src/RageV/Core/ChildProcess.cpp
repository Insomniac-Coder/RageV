#include <rvpch.h>
#include "ChildProcess.h"

#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace RageV
{
	ChildProcess::Result ChildProcess::Run(const std::string& commandLine,
										   const std::atomic<bool>* cancel,
										   const OutputSink& sink)
	{
		Result result;

		// One pipe for both streams: the child writes stdout and stderr to the
		// same end, which is what merges them in arrival order.
		SECURITY_ATTRIBUTES inheritable{};
		inheritable.nLength = sizeof(inheritable);
		inheritable.bInheritHandle = TRUE;

		HANDLE readEnd = nullptr;
		HANDLE writeEnd = nullptr;
		if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0))
			return result;

		// The read end stays ours. If the child inherited it, the pipe would
		// never signal EOF -- the child itself would be holding it open.
		SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOA startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdOutput = writeEnd;
		startup.hStdError = writeEnd;
		startup.hStdInput = nullptr;

		// The job is what makes cancellation mean something: terminating it
		// takes cmake, MSBuild and every cl.exe they spawned, not just the
		// root. KILL_ON_JOB_CLOSE also ties the tree to this handle, so an
		// editor that crashes mid-build does not leave a compiler farm behind.
		HANDLE job = CreateJobObjectA(nullptr, nullptr);
		if (job)
		{
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
			limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(job, JobObjectExtendedLimitInformation,
									&limits, sizeof(limits));
		}

		// CreateProcessA may write into the buffer, so it gets a mutable copy.
		std::string mutableCommand = commandLine;

		PROCESS_INFORMATION process{};
		const BOOL created = CreateProcessA(
			nullptr, mutableCommand.data(), nullptr, nullptr,
			/*inherit handles*/ TRUE,
			// Suspended so the process can be put in the job before it runs --
			// otherwise it could spawn children in the gap, outside the job.
			CREATE_SUSPENDED | CREATE_NO_WINDOW,
			nullptr, nullptr, &startup, &process);

		if (!created)
		{
			CloseHandle(readEnd);
			CloseHandle(writeEnd);
			if (job)
				CloseHandle(job);
			return result;
		}

		if (job)
			AssignProcessToJobObject(job, process.hProcess);
		ResumeThread(process.hThread);
		CloseHandle(process.hThread);

		// Our copy of the write end has to go, or EOF never arrives: the pipe
		// only closes when the last writer does, and we would be one.
		CloseHandle(writeEnd);

		result.Launched = true;

		// Peek-then-read rather than a blocking ReadFile, so the cancel flag
		// is honoured during a silent stretch -- a long link step can print
		// nothing for tens of seconds, and a cancel that waits for output is
		// a cancel that did not work.
		char chunk[512];
		bool childExited = false;
		while (!childExited)
		{
			if (cancel && cancel->load() && !result.Cancelled)
			{
				result.Cancelled = true;
				if (job)
					TerminateJobObject(job, 1);
				else
					TerminateProcess(process.hProcess, 1);
				// Fall through to drain what was already written.
			}

			DWORD available = 0;
			if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr))
				break;   // write end closed and buffer empty: done

			if (available == 0)
			{
				// Nothing to read. Wait briefly on the process instead of
				// spinning; when it exits, drain whatever is left and stop.
				childExited = WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0;
				if (!childExited)
					continue;
			}

			while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr)
				   && available > 0)
			{
				DWORD read = 0;
				const DWORD want = std::min<DWORD>(available, sizeof(chunk));
				if (!ReadFile(readEnd, chunk, want, &read, nullptr) || read == 0)
					break;
				result.Output.append(chunk, read);
				if (sink)
					sink(chunk, read);
			}
		}

		WaitForSingleObject(process.hProcess, INFINITE);

		DWORD exitCode = 1;
		GetExitCodeProcess(process.hProcess, &exitCode);
		result.ExitCode = (int)exitCode;

		CloseHandle(process.hProcess);
		CloseHandle(readEnd);
		if (job)
			CloseHandle(job);

		return result;
	}
}
