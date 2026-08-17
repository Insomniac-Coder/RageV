#pragma once

#include "TerrainData.h"

#include <filesystem>

namespace RageV::Assets
{
	// Reads and writes `.rvterrain`, the file behind AssetType::Terrain
	// (ENGINE-NOTES 7ap).
	//
	// Binary, not YAML: a 1025 terrain is a million samples, and a text form
	// of that is tens of megabytes that no one will ever read. The layout:
	//
	//   char[4]  "RVTR"
	//   uint32   version            1
	//   uint32   resolution         2^n + 1, 33..4097
	//   uint32   layer count        0, or 4 when the weights below are present
	//   uint32   reserved[4]        0
	//   uint16   heights[res * res] row-major, little-endian
	//   uint8    weights[res * res * 4]   only when the count is 4: one RGBA
	//                                     weight per sample, interleaved, on
	//                                     the heights' grid (ENGINE-NOTES 7aq)
	//
	// Thirty-two bytes of header, then the samples. The layer count is the
	// word stage 1 reserved and stage 2 uses: 0 is a terrain with one
	// material, 4 a painted one, and a reader meeting any other count refuses
	// cleanly rather than reading bytes it does not understand as weights.
	// The four materials themselves are the component's, not the file's --
	// the same weights under different `.rmat`s are a different look, not a
	// different terrain.
	//
	// Both directions, because a terrain is authored -- by a tool now, by
	// sculpting later -- and Save is what stage 3 writes through.
	class TerrainSerializer
	{
	public:
		static constexpr uint32_t kVersion = 1;

		// Through the VFS, like every reader. False leaves `out` untouched.
		static bool Load(TerrainData& out, const std::filesystem::path& path);
		// To disk. False when the data is not valid or the file cannot be
		// written; nothing is written for invalid data.
		static bool Save(const TerrainData& data, const std::filesystem::path& path);
	};
}
