#pragma once
#include "RageV/Renderer/Light.h"
#include "RageV/Renderer/Camera.h"
#include "RageV/Math/Math.h"
#include <cstdint>
#include <vector>

namespace RageV
{
	// The view frustum cut into cells, with the lights that reach each one.
	//
	// A forward renderer's cost is fragments times lights, and almost every one
	// of those pairs is a light the fragment is nowhere near. Clustering makes
	// the second factor local: a fragment finds its cell and reads only the
	// lights actually reaching it, so a scene with two hundred lights costs a
	// fragment about what a scene with four does, as long as no four of them
	// overlap at that point.
	//
	// Built on the CPU. The GPU-driven version is a compute pass, and is worth
	// having when the build shows up in a measurement -- ENGINE-NOTES §5 says
	// GPU-driven rendering is out of scope, and a few hundred lights against a
	// few thousand cells is microseconds.
	//
	// Directional lights are deliberately *not* binned. They have no position
	// and reach everything, so they would land in every cell and make the light
	// list the size of the grid. They live at the front of the light buffer
	// instead and every fragment reads them unconditionally.
	class LightGrid
	{
	public:
		// Cells across the screen and through depth.
		//
		// 16x9 matches the usual aspect closely enough that cells stay roughly
		// square, which is what keeps a cell's bounding volume tight. 24 depth
		// slices is the figure the DOOM 2016 presentation used and is a good
		// trade: more slices means tighter cells and a longer index list.
		static constexpr uint32_t kTilesX = 16;
		static constexpr uint32_t kTilesY = 9;
		static constexpr uint32_t kSlices = 24;
		static constexpr uint32_t kCellCount = kTilesX * kTilesY * kSlices;

		// What a cell holds: where its lights start in the index list, and how
		// many. Mirrors the same struct in pbr.rvshader.
		struct Cell
		{
			uint32_t Offset = 0;
			uint32_t Count = 0;
			// **WR-16 S2: a second, shorter list for a static surface deep
			// inside the irradiance field.** A fully baked lamp is in the
			// field and a hybrid one is in it beyond its radius; both cost a
			// pixel an 80-byte read to learn they can be skipped. So beside
			// the cell's full list -- unchanged, in its original order, for
			// every other pixel -- the grid writes the *live* sublist: every
			// realtime and half-baked lamp binned into the cell, every hybrid
			// lamp whose half-bake sphere reaches it, and every fully baked or
			// hybrid lamp with a moving object inside its range (whose
			// subtractive shadow ray a static pixel traces on screen; a traced
			// hit drops those through the cull record). In the same ascending
			// order as the full list, so a static pixel's lamps are processed
			// in the order they always were and the picture stays
			// bit-identical -- a reordered single list would have changed
			// which thinned lamp was traced last, which is what a skipped
			// lamp borrows its visibility from. Mirrors LightCell in
			// pbr_fragment.glsl.
			uint32_t LiveOffset = 0;
			uint32_t LiveCount = 0;
		};
		static_assert(sizeof(Cell) == 16, "LightCell in pbr_fragment.glsl is four words");

		// The near and far planes back out of a projection matrix.
		//
		// Camera holds a projection and nothing else -- deliberately, so an
		// entity camera and the editor's free camera can share one interface --
		// so there is no near clip to ask for. Recovering it from the matrix is
		// also the more honest source: it is the projection the frame is
		// actually drawn with, not a field somebody may have changed since.
		//
		// The engine's projections put clip-space depth in [0, 1] rather than
		// [-1, 1] -- Vulkan's convention, which the OpenGL backend is configured
		// to match so one projection serves both. That is what makes these two
		// expressions the right ones; the [-1, 1] forms differ.
		static void DepthRangeOf(const Mat4& projection, float& nearPlane, float& farPlane);

		// Depth slices are exponential, not linear.
		//
		// A linear split spends most of its slices on distance nobody has
		// geometry in, and gives the near field -- where lights are dense and
		// small on screen -- one enormous cell. The standard fix is to make
		// slice boundaries a geometric series between near and far, so
		// `slice = log(z) * scale + bias` inverts it in the shader with two
		// numbers and no loop.
		static float SliceScale(float nearPlane, float farPlane);
		static float SliceBias(float nearPlane, float farPlane);

		// Which slice a view-space depth falls in. Shared with the shader by
		// construction rather than by comment: the test suite checks the two
		// agree at the boundaries.
		static uint32_t SliceForDepth(float viewDepth, float nearPlane, float farPlane);

		// Bins `lights` against the camera. Positional lights only; the caller
		// has already put the directional ones at the front of the buffer and
		// tells us how many to skip.
		//
		// `lightIndices` indexes the *light buffer*, so the offset by
		// `firstPositional` is folded in here and the shader needs no
		// arithmetic to undo it.
		void Build(const Camera& camera, const Mat4& cameraTransform,
				   const LightList& lights, uint32_t firstPositional);

		const std::vector<Cell>& Cells() const { return m_Cells; }
		const std::vector<uint32_t>& Indices() const { return m_Indices; }

		// The largest number of lights any one cell ended up with. Worth
		// reporting: it is the number that actually decides a fragment's cost,
		// and a scene where it equals the light count is a scene clustering did
		// nothing for.
		uint32_t MaxCellLoad() const { return m_MaxCellLoad; }

	private:
		std::vector<Cell> m_Cells;
		std::vector<uint32_t> m_Indices;
		uint32_t m_MaxCellLoad = 0;

		// Reused between frames so a steady scene stops allocating.
		std::vector<std::vector<uint32_t>> m_Buckets;
		// The live sublist per cell (WR-16 S2); reused for the same reason.
		std::vector<std::vector<uint32_t>> m_Live;
	};

	// **The same binning, in the world rather than on the screen** (WR-16 S4,
	// decided after S2 measured what it is for).
	//
	// The grid above is cut through the camera's frustum, which is exactly
	// right for a fragment and useless for a ray's hit: a hit is wherever the
	// ray landed, and the ones that matter here are mostly *not* on screen --
	// the seabed under the water, which lies below the frame's bottom edge,
	// and the bridge reflected in the sea, which lies behind the camera. S2
	// measured about half of Pier's refraction hits in the first case and
	// roughly a third of its mirror hits in the second, and every one of them
	// walked all 190 positional lights because it had no cell to read.
	//
	// A guard band of extra rows on the screen grid was considered and dropped:
	// it reaches the hits below the frame and never the ones behind the camera.
	//
	// **What the resolution buys, measured from this scene's lamps before any
	// of it was written**: the load plateaus almost at once, because a lamp
	// authored at 600 m genuinely reaches that far and no grid can cut what
	// the range does not. 16x4x16 gives at most 93 lights in a cell and 46.8 on
	// average; 8x4x128, sixteen times the cells, gives 85 and 39.5. So the grid
	// is coarse on purpose -- the win is 190 down to about 40, and the last
	// fifth of it is not worth the cells.
	class WorldLightGrid
	{
	public:
		// Shaped to the lights rather than square: this scene's lamps run
		// along a bridge, 2,700 m in z and 86 m in x, so cells along the span
		// separate them and cells across it do not.
		static constexpr uint32_t kCellsX = 8;
		static constexpr uint32_t kCellsY = 4;
		static constexpr uint32_t kCellsZ = 32;
		static constexpr uint32_t kCellCount = kCellsX * kCellsY * kCellsZ;

		// The same sixteen bytes the screen grid uses, so one shader struct
		// and one walk serve both.
		using Cell = LightGrid::Cell;

		// Bins `lights` by where they reach in the world. Positional lights
		// only, as above, and the indices are into the light buffer.
		void Build(const LightList& lights, uint32_t firstPositional);

		const std::vector<Cell>& Cells() const { return m_Cells; }
		const std::vector<uint32_t>& Indices() const { return m_Indices; }

		// Where the grid starts and how a world position becomes a cell. The
		// shader gets both; there is no camera in this arithmetic at all.
		const Vec3& Origin() const { return m_Origin; }
		const Vec3& InverseCellSize() const { return m_InverseCellSize; }

		uint32_t MaxCellLoad() const { return m_MaxCellLoad; }
		bool IsBuilt() const { return !m_Indices.empty(); }

	private:
		std::vector<Cell> m_Cells;
		std::vector<uint32_t> m_Indices;
		Vec3 m_Origin{ 0.0f };
		Vec3 m_InverseCellSize{ 0.0f };
		uint32_t m_MaxCellLoad = 0;

		std::vector<std::vector<uint32_t>> m_Buckets;
		std::vector<std::vector<uint32_t>> m_Live;
	};
}
