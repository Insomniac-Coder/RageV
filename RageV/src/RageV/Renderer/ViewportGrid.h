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

		// One world unit, subdivided ten to a decade. Two spacings rather than
		// one because a single one either vanishes when zoomed out or becomes a
		// solid sheet when zoomed in -- there is no spacing that survives four
		// orders of magnitude of camera distance, which is the range an editor
		// camera actually covers.
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

		static void SetTargetFormats(RHI::Format color, RHI::Format depth);

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

		// The depth of the plane y = 0 at a pixel, in the [0, 1] both backends
		// use -- the same solve grid.rvshader does, so the test suite can check
		// it against points whose depth is already known.
		//
		// False when the plane is not visible at that pixel: behind the camera,
		// past the far plane, or exactly edge-on. `depth` is untouched then.
		static bool PlaneDepthAt(const Mat4& inverseViewProjection,
								 float ndcX, float ndcY, float& depth);
	};
}
