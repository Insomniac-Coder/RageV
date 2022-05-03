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