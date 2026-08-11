#pragma once

#include "Font.h"

#include <filesystem>

namespace RageV::Assets
{
	// Reads `.rvfont`, the file behind AssetType::Font.
	//
	// **Read only, deliberately.** Every other asset serializer here has a
	// Save, because every other asset is authored: somebody drags a curve
	// point or edits a material. A font is *baked* -- the only thing that can
	// legitimately produce one is `tools/rvfont`, from a `.ttf` and a charset.
	// A Save here would offer a way to write a metrics table that no longer
	// describes the atlas beside it, which is a corruption nothing would
	// detect until the text came out wrong.
	class FontSerializer
	{
	public:
		// False leaves `out` untouched.
		static bool Load(Font& out, const std::filesystem::path& path);
	};
}
