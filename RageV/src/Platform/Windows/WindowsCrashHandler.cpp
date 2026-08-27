// The last-resort crash reporter: a symbolised stack on stderr, then exit.
//
// Everything here runs in a process that has already faulted, which sets the
// rules. The stack is walked from the exception's own CONTEXT rather than from
// here, because "here" is the filter and the frames that matter are the ones
// underneath the fault. Nothing allocates through the engine's allocators, and
// nothing calls back into the engine -- not the logger, whose sinks may be the
// thing that died. Plain fprintf to stderr, which is where ConfigureCrashBehaviour
// already sends assertion output.
//
// DbgHelp itself does allocate and is not thread safe. Both are accepted: this
// runs once, at the end, and the alternative to an imperfect stack is no stack.

#include "rvpch.h"

#include "RageV/Core/CrashHandler.h"

#ifdef RV_PLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
	#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <cstdio>

namespace RageV
{
	namespace
	{
		// Enough for any real stack, and a bound rather than a guess: a filter
		// that walked a corrupt frame chain forever would replace a crash with
		// a hang, which is strictly worse to diagnose.
		constexpr int kMaxFrames = 64;

		const char* ExceptionName(DWORD code)
		{
			switch (code)
			{
				case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
				case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
				case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
				case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "float divide by zero";
				case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
				case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
				case EXCEPTION_IN_PAGE_ERROR:         return "in-page error";
				case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned access";
				case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
				case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "noncontinuable exception";
				default:                              return "exception";
			}
		}

		// **What an access violation actually touched**, which is most of the
		// diagnosis on its own. A write to 0 is a null this function forgot to
		// check; a read at a small offset is a null member access and names the
		// offset; a wild address is a dangling pointer. The three want different
		// searches, and the exit code distinguishes none of them.
		void DescribeAccess(const EXCEPTION_RECORD& record)
		{
			if ((record.ExceptionCode != EXCEPTION_ACCESS_VIOLATION &&
				 record.ExceptionCode != EXCEPTION_IN_PAGE_ERROR) ||
				record.NumberParameters < 2)
			{
				return;
			}

			const ULONG_PTR operation = record.ExceptionInformation[0];
			const ULONG_PTR address = record.ExceptionInformation[1];
			const char* verb = operation == 0 ? "reading"
							 : operation == 1 ? "writing"
							 : operation == 8 ? "executing"
											  : "touching";

			std::fprintf(stderr, "  %s 0x%llX%s\n", verb, (unsigned long long)address,
						 address < 0x10000 ? "  (a null pointer, plus a member offset)" : "");
		}

		// One frame, as well as this build can describe it.
		//
		// **Symbols are not assumed.** Only Debug writes a PDB today, so a
		// Release stack degrades to `module+0x...` rather than printing
		// nothing -- still enough to place the fault in a translation unit and
		// to match against a Debug build of the same commit.
		void PrintFrame(HANDLE process, int index, DWORD64 pc)
		{
			char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
			SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = MAX_SYM_NAME - 1;

			DWORD64 symbolOffset = 0;
			const bool named = SymFromAddr(process, pc, &symbolOffset, symbol) != FALSE;

			IMAGEHLP_LINE64 line{};
			line.SizeOfStruct = sizeof(line);
			DWORD lineOffset = 0;
			const bool located = SymGetLineFromAddr64(process, pc, &lineOffset, &line) != FALSE;

			if (named && located)
			{
				std::fprintf(stderr, "  %2d  %s\n          %s:%lu\n", index, symbol->Name,
							 line.FileName, (unsigned long)line.LineNumber);
				return;
			}
			if (named)
			{
				std::fprintf(stderr, "  %2d  %s + 0x%llX\n", index, symbol->Name,
							 (unsigned long long)symbolOffset);
				return;
			}

			// No symbols: name the module and the offset into it, which is what
			// a map file or a symbolised build of the same commit can resolve.
			const DWORD64 base = SymGetModuleBase64(process, pc);
			char module[MAX_PATH]{};
			if (base != 0 &&
				GetModuleFileNameA(reinterpret_cast<HMODULE>(base), module, MAX_PATH) != 0)
			{
				const char* leaf = std::strrchr(module, '\\');
				std::fprintf(stderr, "  %2d  %s + 0x%llX\n", index, leaf ? leaf + 1 : module,
							 (unsigned long long)(pc - base));
			}
			else
			{
				std::fprintf(stderr, "  %2d  0x%llX\n", index, (unsigned long long)pc);
			}
		}

		LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info)
		{
			// A debugger is better than anything printed here, and it wants the
			// exception rather than a corpse. Hand it over untouched.
			if (IsDebuggerPresent())
				return EXCEPTION_CONTINUE_SEARCH;

			if (!info || !info->ExceptionRecord || !info->ContextRecord)
				return EXCEPTION_EXECUTE_HANDLER;

			const EXCEPTION_RECORD& record = *info->ExceptionRecord;

			std::fprintf(stderr, "\n=== RageV crashed: %s (0x%08lX) at 0x%llX ===\n",
						 ExceptionName(record.ExceptionCode),
						 (unsigned long)record.ExceptionCode,
						 (unsigned long long)(ULONG_PTR)record.ExceptionAddress);
			DescribeAccess(record);

			HANDLE process = GetCurrentProcess();
			SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
			// TRUE: walk the process's own modules, so RageV.dll's symbols are
			// found as well as the executable's -- the frames that matter are
			// almost always in the DLL.
			const bool symbols = SymInitialize(process, nullptr, TRUE) != FALSE;

			// **StackWalk64 writes to the context it is given**, so it gets a
			// copy. The original belongs to the exception, and a filter that
			// consumed it would leave nothing for anything after this.
			CONTEXT context = *info->ContextRecord;
			STACKFRAME64 frame{};
#if defined(_M_X64)
			constexpr DWORD machine = IMAGE_FILE_MACHINE_AMD64;
			frame.AddrPC.Offset = context.Rip;
			frame.AddrFrame.Offset = context.Rbp;
			frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_ARM64)
			constexpr DWORD machine = IMAGE_FILE_MACHINE_ARM64;
			frame.AddrPC.Offset = context.Pc;
			frame.AddrFrame.Offset = context.Fp;
			frame.AddrStack.Offset = context.Sp;
#else
	#error "RageV's crash handler has no stack walk for this architecture"
#endif
			frame.AddrPC.Mode = AddrModeFlat;
			frame.AddrFrame.Mode = AddrModeFlat;
			frame.AddrStack.Mode = AddrModeFlat;

			std::fprintf(stderr, "\n%s\n",
						 symbols ? "stack (innermost first):"
								 : "stack (innermost first; no symbols in this build):");

			for (int i = 0; i < kMaxFrames; ++i)
			{
				if (!StackWalk64(machine, process, GetCurrentThread(), &frame, &context,
								 nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
					break;
				if (frame.AddrPC.Offset == 0)
					break;

				PrintFrame(process, i, frame.AddrPC.Offset);
			}

			std::fprintf(stderr, "\n");
			std::fflush(stderr);

			if (symbols)
				SymCleanup(process);

			// Terminate rather than continue the search: continuing lands on
			// Windows Error Reporting, whose dialog is the trap
			// ConfigureCrashBehaviour exists to avoid -- a process that never
			// exits, holding its window and its audio device open.
			return EXCEPTION_EXECUTE_HANDLER;
		}
	}

	void InstallCrashHandler()
	{
		SetUnhandledExceptionFilter(OnUnhandledException);
	}
}

#else

namespace RageV
{
	void InstallCrashHandler() {}
}

#endif
