#include <rvpch.h>
#include "Log.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>

#ifdef RV_PLATFORM_WINDOWS
	#include <io.h>
#endif

namespace RageV {

	namespace {

		// **spdlog's own colours, deliberately.** Anybody who has run this
		// engine has read green for info and bold red for an error for as long
		// as it has existed, and a logger swap is not a reason to make them
		// learn a new palette. These are the exact escapes its ansicolor sink
		// used: white, green, yellow bold, red bold.
		constexpr const char* kColour[] = {
			"\033[37m",         // trace
			"\033[32m",         // info
			"\033[33m\033[1m",  // warn
			"\033[31m\033[1m",  // error
		};
		constexpr const char* kReset = "\033[m";

		// The names the pattern used to interpolate as %n.
		constexpr const char* kName[] = { "RageV", "APP" };

		std::atomic<LogLevel> s_Level[2] = { LogLevel::Trace, LogLevel::Trace };

		// Guards the console and the sink, and nothing else: the stamp and the
		// buffers below are per thread precisely so they do not need it.
		std::mutex s_Write;
		Log::SinkFn s_Sink = nullptr;
		void* s_SinkUser = nullptr;

		// Set once in Init and only read afterwards, so it needs no guard.
		bool s_Colour = false;

		// Two buffers per thread. Scratch holds the formatted message, Line
		// holds the whole thing including the stamp and the escapes -- one
		// fwrite per line rather than five, which is one lock acquisition
		// inside the CRT rather than five and, more to the point, a line that
		// cannot interleave with another thread's halfway through.
		std::string& LineBuffer()
		{
			thread_local std::string line;
			return line;
		}

		// The clock, formatted once a second rather than once a line.
		//
		// Worth caching because it is otherwise the most expensive thing in a
		// log call that logs a short string: localtime is a call into the CRT
		// with a lock behind it, and the answer is the same for every line
		// written in the same second.
		std::string_view Stamp()
		{
			thread_local char text[16] = "[00:00:00] ";
			thread_local std::time_t second = 0;

			const std::time_t now = std::time(nullptr);
			if (now != second)
			{
				second = now;

				std::tm local{};
			#ifdef RV_PLATFORM_WINDOWS
				localtime_s(&local, &now);
			#else
				localtime_r(&now, &local);
			#endif
				std::snprintf(text, sizeof(text), "[%02d:%02d:%02d] ",
							  local.tm_hour, local.tm_min, local.tm_sec);
			}
			return { text, 11 };
		}
	}

	std::string& Log::Scratch()
	{
		thread_local std::string scratch;
		return scratch;
	}

	void Log::SetLevel(Channel channel, LogLevel level)
	{
		s_Level[(size_t)channel].store(level, std::memory_order_relaxed);
	}

	LogLevel Log::GetLevel(Channel channel)
	{
		return s_Level[(size_t)channel].load(std::memory_order_relaxed);
	}

	bool Log::Enabled(Channel channel, LogLevel level)
	{
		// Relaxed on purpose. The level is set at startup and, rarely, from a
		// menu; a line either side of that change is not worth an acquire on
		// every log call in the engine.
		return level >= s_Level[(size_t)channel].load(std::memory_order_relaxed);
	}

	void Log::SetSink(SinkFn sink, void* user)
	{
		std::lock_guard<std::mutex> lock(s_Write);
		s_Sink = sink;
		s_SinkUser = user;
	}

	void Log::Emit(Channel channel, LogLevel level, std::string_view message)
	{
		// Off is not a level anything writes at, and indexing the colour table
		// with it would read past the end.
		if (level >= LogLevel::Off)
			return;

		std::string& line = LineBuffer();
		line.clear();

		if (s_Colour)
			line += kColour[(size_t)level];

		line += Stamp();
		line += kName[(size_t)channel];
		line += ": ";
		line += message;

		if (s_Colour)
			line += kReset;
		line += '\n';

		std::lock_guard<std::mutex> lock(s_Write);
		std::fwrite(line.data(), 1, line.size(), stdout);

		// Flushed every line, as the logger this replaced did.
		//
		// It is not free, and it is not negotiable: the lines that matter most
		// are the ones written immediately before a crash, and those are
		// exactly the ones a buffer loses. stdout is block-buffered the moment
		// it is redirected, which is how every check script in tools/ reads
		// it, so without this a fatal error would reach the terminal and not
		// the file.
		std::fflush(stdout);

		if (s_Sink)
			s_Sink(channel, level, message, s_SinkUser);
	}

	void Log::Init()
	{
		// **Only when stdout is a terminal.** Escapes written into a pipe are
		// not colour, they are corruption: every check script in tools/ greps
		// this output, and rvdoc and the packagers parse it.
	#ifdef RV_PLATFORM_WINDOWS
		s_Colour = _isatty(_fileno(stdout)) != 0;

		// Windows consoles interpret ANSI only after being asked to, and only
		// since Windows 10. If the mode cannot be set the escapes would print
		// as literal junk, so the answer to failure is no colour rather than a
		// best effort.
		if (s_Colour)
		{
			const HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
			DWORD mode = 0;
			if (console == INVALID_HANDLE_VALUE || !GetConsoleMode(console, &mode)
				|| !SetConsoleMode(console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
			{
				s_Colour = false;
			}
		}
	#else
		s_Colour = isatty(fileno(stdout)) != 0;
	#endif

		// Dist is what a shipped game runs. Warnings and errors still reach the
		// log there, because a player who can send you a line saying what went
		// wrong is worth vastly more than the handful of microseconds saved by
		// a silent build -- and a game that fails with no output at all is a
		// bug report nobody can act on.
	#ifdef RV_DIST
		const LogLevel level = LogLevel::Warn;
	#else
		const LogLevel level = LogLevel::Trace;
	#endif
		SetLevel(Channel::Core, level);
		SetLevel(Channel::Client, level);
	}

}
