#pragma once
// The top-level CMakeLists already defines this for every target on MSVC.
// Defining it again here is what produced the C4005 redefinition warnings.
#ifndef _CRT_SECURE_NO_WARNINGS
	#define _CRT_SECURE_NO_WARNINGS
#endif

#include <iostream>
#include <string>
#include <memory>
#include <utility>
#include <sstream>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "RageV/Core/Log.h"
// The engine's math, everywhere the engine is. Not because every file wants a
// matrix, but because `Math::Max` and `Math::Sqrt` replaced the <cmath> the
// backends and the loaders used to reach for directly, and a vocabulary that
// half the translation units have to include by hand is one half of them will
// keep spelling `std::`.
#include "RageV/Math/Math.h"

#ifdef RV_PLATFORM_WINDOWS
#include <Windows.h>
#endif