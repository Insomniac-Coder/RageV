#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/Camera.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	// How the ground plane is drawn. Nothing here is serialized with a scene:
	// the grid is a property of somebody's viewport, not of the world, and two
	// people looking at the same scene should be free to disagree about it.
	struct ViewportGridSettings
	{
		// Deliberately a mid grey rather than a colour. What sits behind the
		// grid is the scene -- sky, ground, whatever is being built -- so a
		// tinted grid competes with the thing it is there to measure. The axes
		// are the exception, and they are the only exception.
		Vec3 LineColor{ 0.62f, 0.63f, 0.70f };

		// Of a major line. A minor one is a fraction of this; see grid.rvshader.
		float Opacity = 0.34f;

		// Filled in by the editor from the theme's axis colours, so the grid and
		// the transform widget name the axes the same way. The defaults are what
		// the runtime would get if it ever drew one, which it does not.
		Vec3 AxisXColor{ 0.79f, 0.25f, 0.28f };
		Vec3 AxisZColor{ 0.26f, 0.44f, 0.84f };

		// The *finest* spacing the grid will ever draw, and how many of those
		// make the next one up. Coarser decades are chosen per pixel from the
		// screen-space footprint, so the grid stays usable from arm's length to
		// tens of thousands of units without either aliasing or vanishing.
		//
		// A floor rather than a fixed size, because below about a metre the
		// lines stop being a reference and start being texture -- and one unit
		// is the scale scenes in this engine are authored at.
		float Spacing = 1.0f;
		float MajorEvery = 10.0f;
	};

	// The editor's infinite ground grid.
	//
	// A fullscreen pass rather than geometry: a grid made of lines has an
	// extent, and the edge of it is visible from anywhere the camera can get
	// to. See grid.rvshader for the solve, which is a plane in clip space
	// rather than a ray in world space.
	//
	// Kept beside Skybox rather than inside DebugRenderer, which is the other
	// plausible home: DebugRenderer batches line segments, and this draws none.
	class ViewportGrid
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		static void SetTargetFormats(RHI::Format color, RHI::Format depth,
									 uint32_t samples = 1);

		static bool IsReady();

		// Call after the sky and before anything blended.
		//
		// After the sky, and this order is not interchangeable: the sky is
		// drawn at the far plane with the depth test on, so a grid that had
		// already written nearer depth into those pixels would punch holes in
		// it. Before the blended passes for the ordinary reason -- the grid is
		// opaque-ish scenery to them.
		static void Draw(const Camera& camera, const Mat4& cameraTransform,
						 const ViewportGridSettings& settings);

		// The matrix the shader is handed. Exposed for the same reason the
		// sky's direction matrix is: a wrong one produces a picture that is
		// plausible and slightly wrong, and a still image will not say which.
		static Mat4 BuildInverseViewProjection(const Mat4& projection,
											   const Mat4& cameraTransform);

		// The depth the grid writes at a pixel, in the [0, 1] both backends use
		// -- the same solve grid.rvshader does, so the test suite can check it
		// against points whose depth is already known.
		//
		// **Past the far plane it answers 1, not false.** A far clip is where
		// the scene stops, not where the floor stops; clipping the grid there
		// drew a hard edge across the viewport well short of the horizon. Every
		// such point is further than anything that could occlude it, so one
		// value serves them all.
		//
		// False when the plane genuinely is not there: behind the viewer, on the
		// far side of the horizon, or exactly edge-on. `depth` is untouched then.
		static bool PlaneDepthAt(const Mat4& inverseViewProjection,
								 float ndcX, float ndcY, float& depth);
	};
}
