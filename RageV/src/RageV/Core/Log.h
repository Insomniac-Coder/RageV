#pragma once
#include "Core.h"

#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace RageV {

	// The engine's log: two channels, four levels, one line at a time.
	//
	// ---------------------------------------------------------------------
	// Why this is not spdlog any more
	// ---------------------------------------------------------------------
	//
	// spdlog is 147 headers and 2.1 MB, it was pulled into *every* translation
	// unit through rvpch.h, and what the engine asked of it was two named
	// loggers, four levels, one pattern and a colour per level. The formatting
	// was the part that made it worth keeping -- and C++20 has that in the
	// standard library now, so the reason expired.
	//
	// The bundled fmt also forced /utf-8 on every target that compiled it,
	// which is documented in Project.cpp because it is not obvious why a
	// logger has an opinion about source encoding.
	//
	// ---------------------------------------------------------------------
	// Why the formatting is wrapped rather than exposed
	// ---------------------------------------------------------------------
	//
	// Call sites say `RV_CORE_WARN("{0} of {1}", a, b)` and nothing more. They
	// do not name std::format, do not include <format>, and do not decide what
	// a line looks like. That is what makes the *next* substitution -- a ring
	// buffer, a file, a panel in the editor -- a change to one file rather than
	// to six hundred and fifty-eight call sites.
	//
	// The wrapper is a template because a format string is checked against its
	// arguments at compile time, and that check is worth having: a `{2}` with
	// two arguments is now a build error rather than a line that says
	// something wrong at runtime.

	enum class LogLevel : uint8_t
	{
		Trace = 0,
		Info,
		Warn,
		Error,

		// Not a level anything writes at: the one every level fails, so a
		// channel can be silenced without a second flag on the way in.
		Off,
	};

	class RV_API Log
	{
	public:
		// The engine's own voice, and the game's. Two, because when something
		// goes wrong the first question is which of them said so.
		enum class Channel : uint8_t
		{
			Core = 0,
			Client = 1,
		};

		static void Init();

		static void SetLevel(Channel channel, LogLevel level);
		static LogLevel GetLevel(Channel channel);

		// Whether a line at this level would survive the filter.
		//
		// Separate from Write, and checked *before* formatting, because the
		// arguments are the expensive part. A Dist build drops trace and info,
		// and the point of dropping them is not paying to build strings that
		// are then thrown away.
		static bool Enabled(Channel channel, LogLevel level);

		// One finished line: stamped, coloured, written.
		//
		// **The only part of this that crosses the DLL boundary.** Formatting
		// is a template, so it lives in the header and every module compiles
		// its own; what they share is this call, and behind it the one mutex,
		// the one console and the one set of levels. The logger this replaced
		// documented the same rule for the same reason -- a call crosses a
		// module boundary, a data symbol does not.
		static void Emit(Channel channel, LogLevel level, std::string_view message);

		// One extra place every line also goes. Null clears it.
		//
		// A function pointer and a void* rather than std::function, because
		// the only caller is a test counting warnings, this sits on the path
		// of every line, and an owning callable would put an allocation and an
		// indirect call where neither buys anything. It is called *after* the
		// console write and under the same lock, so a sink sees lines in the
		// order they were written and never two at once.
		using SinkFn = void (*)(Channel, LogLevel, std::string_view, void*);
		static void SetSink(SinkFn sink, void* user);

		template<typename... Args>
		static void Write(Channel channel, LogLevel level,
						  std::format_string<Args...> fmt, Args&&... args)
		{
			if (!Enabled(channel, level))
				return;

			// A per-thread buffer, cleared rather than freed: capacity
			// survives the clear, so the steady state allocates nothing.
			//
			// Per *thread* rather than one shared buffer under the lock,
			// because formatting is the slow half and holding the write lock
			// across it would serialise every logging thread on the work
			// rather than on the write.
			std::string& text = Scratch();
			text.clear();

			try
			{
				std::format_to(std::back_inserter(text), fmt,
							   std::forward<Args>(args)...);
			}
			catch (...)
			{
				// A logger that throws takes down the thing it was reporting
				// on, which is the one failure it is never allowed to have.
				// Something is better than the message, and both are better
				// than an exception out of a diagnostic.
				text = "<a log message could not be formatted>";
			}

			Emit(channel, level, text);
		}

	private:
		static std::string& Scratch();
	};

}

#define RV_CORE_TRACE(...) ::RageV::Log::Write(::RageV::Log::Channel::Core,   ::RageV::LogLevel::Trace, __VA_ARGS__)
#define RV_CORE_INFO(...)  ::RageV::Log::Write(::RageV::Log::Channel::Core,   ::RageV::LogLevel::Info,  __VA_ARGS__)
#define RV_CORE_WARN(...)  ::RageV::Log::Write(::RageV::Log::Channel::Core,   ::RageV::LogLevel::Warn,  __VA_ARGS__)
#define RV_CORE_ERROR(...) ::RageV::Log::Write(::RageV::Log::Channel::Core,   ::RageV::LogLevel::Error, __VA_ARGS__)

#define RV_TRACE(...)      ::RageV::Log::Write(::RageV::Log::Channel::Client, ::RageV::LogLevel::Trace, __VA_ARGS__)
#define RV_INFO(...)       ::RageV::Log::Write(::RageV::Log::Channel::Client, ::RageV::LogLevel::Info,  __VA_ARGS__)
#define RV_WARN(...)       ::RageV::Log::Write(::RageV::Log::Channel::Client, ::RageV::LogLevel::Warn,  __VA_ARGS__)
#define RV_ERROR(...)      ::RageV::Log::Write(::RageV::Log::Channel::Client, ::RageV::LogLevel::Error, __VA_ARGS__)
