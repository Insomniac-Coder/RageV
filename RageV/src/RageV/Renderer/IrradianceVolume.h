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

		// **One volume's place in the atlas**, and the box it stands for.
		//
		// Several volumes share one texture because they must: a fragment
		// shader has thirty-two samplers on OpenGL and the layered terrain
		// variant already spends thirty-one of them, so N fields cannot be N
		// bindings. They are packed instead -- every volume keeps its own
		// grid, its own spacing and its own rotation, and gives up only the
		// right to be its own texture.
		//
		// Packed along z *within each tile*: volume v's tile t occupies the
		// slices [t * AtlasDepth + ZOffset, ... + Depth). The tile stride is
		// therefore the atlas depth rather than a field's own, which is the
		// one line every reader of this layout has to get right.
		struct Region
		{
			// The box in the world. Axes as columns, unit length.
			Vec3 Centre{ 0.0f };
			Vec3 Extents{ 1.0f };
			Mat3 Rotation{ 1.0f };

			// Cells this volume has, which need not match its neighbours'.
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t Depth = 0;

			// Where its slices begin inside a tile.
			uint32_t ZOffset = 0;

			// Metres between cells, kept for the stamp and the inspector.
			float Spacing = 0.0f;
		};

		// Null if the device cannot make the textures, which is the only way
		// this fails; a caller that gets null carries on without a volume and
		// the shading falls back to what it did before.
		static RHI::Ref<IrradianceVolume> Create(RHI::RHIDevice& device,
												 uint32_t x, uint32_t y, uint32_t z);

		// **An atlas holding several volumes.** The regions arrive already
		// laid out -- the scene decides who sits where, because it is the
		// scene that knows which boxes exist. Width and height are the widest
		// and tallest any region asked for; the depth is their sum.
		static RHI::Ref<IrradianceVolume> CreateAtlas(RHI::RHIDevice& device,
													  const std::vector<Region>& regions);

		// The regions packed into this texture, in the order they were laid
		// out. Empty for a volume made by Create, which is one region that
		// happens to fill the whole texture.
		const std::vector<Region>& Regions() const { return m_Regions; }

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
		// **Seven: six of light, one of visibility.** Tile 6 carries the
		// neighbour bitmask in .x and the cell's aliveness in .y -- aliveness
		// moved here from the light tiles' alpha so that lane could carry sky
		// visibility instead, where the hardware filter is free to blend it.
		// The readers divide the texture's depth by this to find a field's own
		// depth, so changing it changes both ends at once.
		// **Ten since 2026-09-02: nine spherical-harmonic tiles and the
		// visibility tile.** Six axis faces before -- an ambient cube, which
		// reads a broad bounce faithfully and a single lamp at cos² weights:
		// 1.0 on axis, 0.71 at 45 degrees, measured as a full-baked showroom
		// 30% darker than its live twin. Second-order harmonics hold a lamp's
		// direction within a few percent. A stored bake with seven tiles fails
		// the stamp's tile count and is solved again; the probe cubes are
		// untouched, so no version bump.
		// **Nineteen since later the same day: nine more for the fully baked
		// lights' direct light, kept apart from the bounce.** On screen the
		// field is the bounce's fallback, weighted by one minus the gather's
		// confidence -- right for bounce light, and it discarded a fully
		// baked lamp's direct light along with it (the showroom read 0.68 of
		// its live twin). Direct light now has its own coefficients, read at
		// the pixel unconditionally and at reflection hits, never mixed with
		// the bounce. Tiles 0-8 bounce (+ sky in alpha), 9-17 direct, 18
		// visibility and the alive flag.
		static constexpr uint32_t kTiles = 19;

		uint32_t Width() const { return m_Width; }
		uint32_t Height() const { return m_Height; }
		uint32_t Depth() const { return m_Depth; }
		size_t CellCount() const { return (size_t)m_Width * m_Height * m_Depth; }

		// The one texture, all three channels. Slices [c*Depth, (c+1)*Depth)
		// are channel c; readers find the tile from the texture's own depth,
		// which is three times a field's, so nothing has to be told.
		const RHI::Ref<RHI::RHITexture>& Texture() const { return m_Texture; }

		// **The solve's write target -- the other half of a swap pair.**
		//
		// A solve that reads the texture it is writing is a race within a
		// sweep and a muddle between them: cells written early in a sweep leak
		// into cells solved later, and "one pass, one bounce" stops being
		// true. So the solve writes this one while every reader -- the frame,
		// and the solve's own feedback -- samples Texture(), and FlipSolve()
		// exchanges the two at each sweep boundary. Sweep k then reads exactly
		// what sweep k-1 finished, nothing newer.
		const RHI::Ref<RHI::RHITexture>& SolveTarget() const { return m_Solve; }

		// Called once per completed sweep, after its last rows are recorded:
		// what was just written becomes what everything samples, and the old
		// front becomes the next sweep's target. The bound descriptors catch
		// up on the next frame's set write, which is also when the first
		// reader of the new front runs.
		void FlipSolve() { std::swap(m_Texture, m_Solve); }

		// Linear, clamped. Linear because the whole point is the blend between
		// cells; clamped because a sample just outside the box should read its
		// edge rather than wrap to the far side of the room.
		const RHI::Ref<RHI::RHISampler>& Sampler() const { return m_Sampler; }

	private:
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		// The atlas depth: the sum of the regions' depths, and the stride
		// between one tile and the next.
		uint32_t m_Depth = 0;
		std::vector<Region> m_Regions;

		RHI::Ref<RHI::RHITexture> m_Texture;
		RHI::Ref<RHI::RHITexture> m_Solve;
		RHI::Ref<RHI::RHISampler> m_Sampler;

		// Scratch for the channel split and the half conversion, kept so an
		// upload of a settled volume allocates nothing.
		std::vector<uint16_t> m_Scratch;
	};
}
