#pragma once

#ifdef RV_PLATFORM_WINDOWS
	#ifdef RV_BUILD_DLL
		#define RV_API __declspec(dllexport)
	#else
		#define RV_API __declspec(dllimport)
	#endif 
#else
	#error RageV only supports Windows!
#endif

#define BIT(x) (1 << x)

#ifdef RV_DEBUG
	#define RV_ENABLE_ASSERTS
#endif

#ifdef RV_ENABLE_ASSERTS
	#define RV_ASSERT(x, ...) { if(!(x)) { RV_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
	#define RV_CORE_ASSERT(x, ...) { if(!(x)) { RV_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debug_break(); } }
#else
	#define RV_ASSERT(x, ...)
	#define RV_CORE_ASSERT(x, ...)
#endif

#define RV_BIND_EVENT_FN(x) std::bind(&x, this, std::placeholders::_1)