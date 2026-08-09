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
		};

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
	};
}
