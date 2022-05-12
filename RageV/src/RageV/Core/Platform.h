#pragma once
#include <functional>

namespace RageV
{
	enum class PlatformType
	{
		Windows = 0,
		Linux = 1
	};

	class Platform
	{
	public:
		virtual std::function<double()> GetTimeFn() = 0;

		static PlatformType& GetPlatformType() { return m_Platform; }
	private:
		static PlatformType m_Platform;
	};

}