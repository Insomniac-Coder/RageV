#pragma once
#include "Core.h"
#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"

namespace RageV {
	class RV_API Log
	{
	public:
		static void Init();

		// Out of line, not inline, because the engine is a DLL. An inline
		// accessor reads the static member directly, which puts a *data* symbol
		// in every consumer -- and data does not cross a module boundary the way
		// a call does. A function is exported and called; the member stays where
		// it was defined, so a game module logs through the same two loggers the
		// engine does rather than through its own uninitialised pair.
		static std::shared_ptr<spdlog::logger>& GetClientLogger();
		static std::shared_ptr<spdlog::logger>& GetCoreLogger();
	private:
		static std::shared_ptr<spdlog::logger> m_CoreLogger;
		static std::shared_ptr<spdlog::logger> m_ClientLogger;
	};

}

#define RV_CORE_ERROR(...) RageV::Log::GetCoreLogger()->error(__VA_ARGS__)
#define RV_CORE_WARN(...) RageV::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define RV_CORE_INFO(...) RageV::Log::GetCoreLogger()->info(__VA_ARGS__)
#define RV_CORE_TRACE(...) RageV::Log::GetCoreLogger()->trace(__VA_ARGS__)

#define RV_ERROR(...) RageV::Log::GetClientLogger()->error(__VA_ARGS__)
#define RV_WARN(...) RageV::Log::GetClientLogger()->warn(__VA_ARGS__)
#define RV_INFO(...) RageV::Log::GetClientLogger()->info(__VA_ARGS__)
#define RV_TRACE(...) RageV::Log::GetClientLogger()->trace(__VA_ARGS__)