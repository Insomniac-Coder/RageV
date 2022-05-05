#pragma once
#include "Core.h"
#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"

namespace RageV {
	class RV_API Log
	{
	public:
		static void Init();
		inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return m_ClientLogger; }
		inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return m_CoreLogger; }
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