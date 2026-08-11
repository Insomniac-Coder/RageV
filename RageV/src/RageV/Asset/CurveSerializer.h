#pragma once

#include "Curve.h"

#include <filesystem>

namespace RageV::Assets
{
	// Reads and writes `.rcurve`, the file behind AssetType::Curve.
	//
	// YAML like everything else authored here, and for the same reason: a curve
	// is a handful of numbers somebody may want to diff, review or fix by hand
	// when the editor has mangled one. A binary blob would be smaller and would
	// cost exactly the thing that makes a text scene format worth having.
	class CurveSerializer
	{
	public:
		static bool Save(const Curve& curve, const std::filesystem::path& path);

		// False leaves `out` untouched, so a caller that pre-filled a default
		// keeps it rather than being handed an empty curve it has to detect.
		static bool Load(Curve& out, const std::filesystem::path& path);
	};
}
