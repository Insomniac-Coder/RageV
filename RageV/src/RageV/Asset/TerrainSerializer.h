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
	//   uint32   layer count        0 in version 1; stage 2's weight map
	//   uint32   reserved[4]        0
	//   uint16   heights[res * res] row-major, little-endian
	//
	// Thirty-two bytes of header, then the samples. Version 1 has no layers
	// and no material list; the header carries the count so a version-2 file
	// can grow them without turning the magic over, and a version-1 reader
	// meeting a count it does not understand refuses cleanly rather than
	// reading weights as heights.
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
