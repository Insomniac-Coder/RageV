#include <rvpch.h>
#include "Log.h"
#include "spdlog/sinks/stdout_color_sinks.h"

std::shared_ptr<spdlog::logger> RageV::Log::m_ClientLogger;
std::shared_ptr<spdlog::logger> RageV::Log::m_CoreLogger;

// Defined here rather than in the header: see the declaration for why an
// accessor that crosses a DLL boundary cannot be inline.
std::shared_ptr<spdlog::logger>& RageV::Log::GetClientLogger() { return m_ClientLogger; }
std::shared_ptr<spdlog::logger>& RageV::Log::GetCoreLogger() { return m_CoreLogger; }

void RageV::Log::Init()
{
	spdlog::set_pattern("%^[%T] %n: %v%$");

	m_CoreLogger = spdlog::stdout_color_mt("RageV");
	// Dist is what a shipped game runs. Warnings and errors still reach the
	// log there, because a player who can send you a line saying what went
	// wrong is worth vastly more than the handful of microseconds saved by a
	// silent build -- and a game that fails with no output at all is a bug
	// report nobody can act on.
#ifdef RV_DIST
	m_CoreLogger->set_level(spdlog::level::warn);
#else
	m_CoreLogger->set_level(spdlog::level::trace);
#endif

	m_ClientLogger = spdlog::stdout_color_mt("APP");
#ifdef RV_DIST
	m_ClientLogger->set_level(spdlog::level::warn);
#else
	m_ClientLogger->set_level(spdlog::level::trace);
#endif
}