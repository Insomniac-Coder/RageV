#include <rvpch.h>
#include "Platform.h"

namespace RageV
{

	PlatformType Platform::m_Platform = PlatformType::Windows;

	PlatformType& Platform::GetPlatformType() { return m_Platform; }

}