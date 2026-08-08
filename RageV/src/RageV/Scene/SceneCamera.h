#pragma once
#include "RageV/Renderer/Camera.h"

namespace RageV
{
	// Plain data plus a cached projection.
	//
	// The fields were private behind setters that only assigned and called
	// Recalculate -- which is exactly what ComponentDesc::OnChanged does, and
	// two of those setters (FOV and projection type) forgot to call it, so
	// changing either left the projection stale until something else moved.
	class SceneCamera : public Camera
	{
	public:
		enum class ProjectionType
		{
			Perspective = 0,
			Orthographic = 1
		};

		SceneCamera() { Recalculate(); }
		virtual ~SceneCamera() {}

		ProjectionType Projection = ProjectionType::Orthographic;

		float OrthographicSize = 10.0f;
		float OrthographicNear = -1.0f;
		float OrthographicFar = 1.0f;

		float PerspectiveFOV = 60.0f;      // degrees
		float PerspectiveNear = 0.01f;
		float PerspectiveFar = 1000.0f;

		// Driven by the viewport rather than the inspector, so it is neither a
		// described field nor serialized.
		float AspectRatio = 0.0f;

		void SetViewport(float width, float height);
		// Only recalculates when the value actually changes: this is called
		// every frame by the render passes.
		void SetAspectRatio(float aspect);

		// Public because the registry calls it after writing any field.
		void Recalculate();
	};
}
