#pragma once
#include "RageV/Math/Math.h"
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/RHI/RHIResources.h"

#include <vector>

namespace RageV
{
	// **A box of stored indirect light, sampled by position.**
	//
	// The engine can already say what indirect light reaches a *surface it is
	// looking at* -- that is what the traced bounce and the screen-space gather
	// do -- and it can say what reaches an *object* by picking the nearest
	// reflection probe. What it has never been able to answer is the plain
	// question underneath both: what is the irradiance at an arbitrary point in
	// space? A moving object needs exactly that, and so does every ray the
	// traced bounce terminates, which is why this is worth more than it first
	// looks.
	//
	// The answer is stored on a regular grid and interpolated between cells, so
	// it is continuous: a character walking across a room reads a value that
	// slides rather than one that switches when it crosses a probe's boundary.
	//
	// **Spherical harmonics, first order.** Four coefficients per colour
	// channel: one constant term and three that lean the answer toward an axis,
	// which is enough for "the sky side of an object is lit by sky and the
	// ground side by ground" and is where the cost-to-quality curve bends. L2
	// would be nine coefficients for a refinement nothing here is asking for.
	//
	// **Laid out channel-major, in one volume texture with five tiles.** Three
	// hold (L0, L1x, L1y, L1z) of light for one colour channel each, stacked
	// along z: channel c owns slices [c*Depth, (c+1)*Depth).
	//
	// **And thirty-two hold the shadow half of the same bake.** They are an
	// octahedral map of sixty-four directions per cell, two to a texel: how far
	// geometry is that way and the mean of its square, which is what a
	// Chebyshev test needs to answer "is something between this cell and the
	// surface asking" softly rather than as a hard edge. Light without that is
	// light that leaks through walls, so the two are solved together, from the
	// same rays, with no way to ask for one and not the other. A lookup is three fetches rather
	// than four, and -- the reason it is a texture at all rather than a buffer
	// -- the hardware does the trilinear blend between the eight surrounding
	// cells for free. Doing that by hand would be eight loads and a lerp tree.
	//
	// **One texture rather than three, because a sampler is a scarce thing.**
	// OpenGL gives a fragment shader thirty-two, and the layered terrain
	// variant was already using thirty; three volumes took it to thirty-three
	// and the shader stopped compiling, which is a black frame wherever terrain
	// is drawn. Tiles cost nothing: the sample coordinate is mapped to texel
	// centres, so the filter never reaches past the end of a tile into the
	// next channel, and the picture is identical to what three textures gave.
	class IrradianceVolume
	{
	public:
		// One cell's worth of light: four SH coefficients for each of the three
		// channels, in the order the shader reads them.
		struct Cell
		{
			Vec4 R{ 0.0f };
			Vec4 G{ 0.0f };
			Vec4 B{ 0.0f };
		};

		// Null if the device cannot make the textures, which is the only way
		// this fails; a caller that gets null carries on without a volume and
		// the shading falls back to what it did before.
		static RHI::Ref<IrradianceVolume> Create(RHI::RHIDevice& device,
												 uint32_t x, uint32_t y, uint32_t z);

		// Cells in x-major order: index = (z * Height + y) * Width + x, which is
		// the order a 3D texture upload expects and the order Fill writes.
		void Upload(const std::vector<Cell>& cells);

		// **The texture's own bytes, straight in.** What a bake reads back is
		// exactly what the texture holds, tile order and all, so putting one
		// back is a copy with nothing in between -- no repacking, no chance for
		// the two ends to disagree about layout. The size is checked against
		// what this volume actually is, because a payload of the wrong shape is
		// a stale bake rather than a smaller picture.
		bool UploadRaw(const std::vector<uint8_t>& bytes);

		// Tiles of the texture: three of light, thirty-two of distance. The readers
		// divide the texture's depth by this to find a field's own depth, so
		// changing it changes both ends at once.
		static constexpr uint32_t kTiles = 7;

		uint32_t Width() const { return m_Width; }
		uint32_t Height() const { return m_Height; }
		uint32_t Depth() const { return m_Depth; }
		size_t CellCount() const { return (size_t)m_Width * m_Height * m_Depth; }

		// The one texture, all three channels. Slices [c*Depth, (c+1)*Depth)
		// are channel c; readers find the tile from the texture's own depth,
		// which is three times a field's, so nothing has to be told.
		const RHI::Ref<RHI::RHITexture>& Texture() const { return m_Texture; }

		// Linear, clamped. Linear because the whole point is the blend between
		// cells; clamped because a sample just outside the box should read its
		// edge rather than wrap to the far side of the room.
		const RHI::Ref<RHI::RHISampler>& Sampler() const { return m_Sampler; }

	private:
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Depth = 0;

		RHI::Ref<RHI::RHITexture> m_Texture;
		RHI::Ref<RHI::RHISampler> m_Sampler;

		// Scratch for the channel split and the half conversion, kept so an
		// upload of a settled volume allocates nothing.
		std::vector<uint16_t> m_Scratch;
	};
}
