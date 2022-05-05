#include <rvpch.h>
#include "Log.h"
#include "spdlog/sinks/stdout_color_sinks.h"

std::shared_ptr<spdlog::logger> RageV::Log::m_ClientLogger;
std::shared_ptr<spdlog::logger> RageV::Log::m_CoreLogger;

void RageV::Log::Init()
{
	spdlog::set_pattern("%^[%T] %n: %v%$");

	m_CoreLogger = spdlog::stdout_color_mt("RageV");
	m_CoreLogger->set_level(spdlog::level::trace);

	m_ClientLogger = spdlog::stdout_color_mt("APP");
	m_ClientLogger->set_level(spdlog::level::trace);
}