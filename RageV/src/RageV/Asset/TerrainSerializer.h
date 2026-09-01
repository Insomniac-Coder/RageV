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

		// **A heightmap image as a second input, cooked to the same samples.**
		//
		// An addition to `.rvterrain`, not a replacement: that format is still
		// what the engine owns, what sculpting writes, and what a painted
		// terrain's weights live in. This is the *import* side, and it is the
		// shape Unity's "Import Raw" and Unreal's landscape import both take --
		// an image is a source you bring heights in from, never the live
		// representation, because heights have to be sixteen bits, on the CPU,
		// and writable.
		//
		// Reads 16-bit greyscale by preference and 8-bit if that is all there
		// is, saying so, because 8 bits is 256 steps and over a few hundred
		// metres of relief that terraces visibly.
		//
		// **The image is resampled to the nearest legal 2^n + 1.** A terrain
		// grid must be one -- see TerrainData -- and a heightmap is almost
		// always a power of two, so a 1024 image becomes 1025 samples. Bilinear
		// across the image's whole extent, so the edges land on the edges.
		//
		// Weights are left empty: an image carries heights and nothing about
		// which material goes where, so an imported terrain is layer 0 until
		// something paints it.
		static bool LoadImage(TerrainData& out, const std::filesystem::path& path);

		// Whether a path is one LoadImage can be asked for -- i.e. anything
		// that is not the engine's own format. Extension-based, so the decision
		// is made once and both the loader and the "can this be sculpted"
		// question read the same answer.
		static bool IsHeightmapImage(const std::filesystem::path& path);
	};
}
