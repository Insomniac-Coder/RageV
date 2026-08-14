#pragma once

#include "RageV/Renderer/PostSettings.h"

#include <filesystem>

namespace RageV::Assets
{
	// Reads and writes `.rvpostprofile`, the file behind AssetType::PostProfile.
	//
	// YAML like everything else authored here, and driven entirely by
	// `PostSettingsRegistry` -- there is no field list in this file, which is
	// the whole point. A setting added to `PostSettings` and its registry is on
	// disk with nothing written here, and a setting added to the struct alone
	// is missing from every consumer at once rather than from this one
	// silently. ENGINE-NOTES 7s.
	class PostProfileSerializer
	{
	public:
		static bool Save(const PostSettings& settings, const std::filesystem::path& path);

		// False leaves `out` untouched, so a caller that pre-filled the
		// defaults keeps them rather than being handed a zeroed profile -- and
		// a profile of zeroes is not a neutral grade, it is a black screen.
		static bool Load(PostSettings& out, const std::filesystem::path& path);
	};
}
