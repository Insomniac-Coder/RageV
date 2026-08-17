#pragma once

// The heights of a terrain (ENGINE-NOTES 7ap): a square grid of 16-bit
// samples, 2^n + 1 a side, spanning [0, 1] of whatever height the component
// that uses it says. This is the CPU side and nothing else -- the chunk
// meshes are built *from* it by Renderer/Terrain, the collider by the
// physics world, and both read it through Sample so they cannot disagree
// with each other about where the surface is.
//
// Its own asset rather than a texture, for three reasons that each decide
// it alone: a height needs sixteen bits and the cook policy would BC4 a
// `_height` map (rightly -- that suffix means a parallax map); the mesh, the
// collider and HeightAt need the samples on the CPU, and a texture is a GPU
// object; and stage 3 (sculpting) writes heights back, so the file has to be
// one this engine owns the format of.

#include <cstdint>
#include <vector>

namespace RageV
{
	struct TerrainData
	{
		// 2^n + 1, so a grid of 2^n quads cuts into 64-quad chunks exactly and
		// every level of detail samples every 2^k-th height and still lands on
		// the last one. 33 is a 32-quad toy; 4097 is 16 million samples, and
		// the geometry a terrain that size builds is stated in 7ap.
		static constexpr uint32_t kMinResolution = 33;
		static constexpr uint32_t kMaxResolution = 4097;

		// Layers a painted terrain has, which is the four channels of one RGBA8
		// weight per sample (ENGINE-NOTES 7aq). The file's layer count is 0 or
		// exactly this.
		static constexpr uint32_t kLayers = 4;

		// Samples per side.
		uint32_t Resolution = 0;
		// Resolution * Resolution, row-major: the sample at (x, z) is
		// Heights[z * Resolution + x]. 0 is the base, 65535 the full height.
		std::vector<uint16_t> Heights;
		// Empty, or Resolution * Resolution * kLayers bytes: the four layer
		// weights of each sample, interleaved, on the same row-major grid as
		// the heights. Their sum need not be 255 -- the shader normalises --
		// and a sample whose weights are all zero is layer 0. Empty means
		// unpainted, which draws exactly as all-zero does (7aq).
		std::vector<uint8_t> Weights;

		static bool IsValidResolution(uint32_t resolution);
		// A valid grid of heights, and weights that are either absent or the
		// grid's size.
		bool IsValid() const;
		bool HasWeights() const { return !Weights.empty(); }

		// Quads per side: Resolution - 1.
		uint32_t QuadCount() const { return Resolution > 0 ? Resolution - 1 : 0; }

		uint16_t At(uint32_t x, uint32_t z) const { return Heights[(size_t)z * Resolution + x]; }
		// The weight of `layer` at a sample; 0 everywhere on an unpainted
		// terrain, layer 0 included -- the "all zero means layer 0" rule is the
		// shader's, applied after normalisation, and this reads the bytes.
		uint8_t WeightAt(uint32_t x, uint32_t z, uint32_t layer) const
		{
			return Weights.empty() ? 0
				: Weights[((size_t)z * Resolution + x) * kLayers + layer];
		}

		// The surface height in [0, 1] at fractional sample coordinates,
		// clamped to the grid. Interpolated over the *triangles* -- the quad
		// split along the (x, z) to (x + 1, z + 1) diagonal, which is the split
		// the chunk meshes are built with and the split Jolt's height field
		// uses -- rather than bilinearly, so this, the drawn surface and the
		// collider are the same surface to the triangle. A ball at rest sits
		// exactly here.
		float Sample(float x, float z) const;

		// The lowest and highest sample; both zero on an empty grid.
		void MinMax(uint16_t& low, uint16_t& high) const;

		// A flat grid at one height, for tests and for a terrain that has no
		// asset yet.
		static TerrainData Flat(uint32_t resolution, uint16_t height);
	};
}
