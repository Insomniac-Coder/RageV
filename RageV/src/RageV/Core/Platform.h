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

		// Out of line for the same reason Application::Get and Log's accessors
		// are: a static data member read inline does not survive the crossing
		// into another module.
		static PlatformType& GetPlatformType();
	private:
		static PlatformType m_Platform;
	};

}