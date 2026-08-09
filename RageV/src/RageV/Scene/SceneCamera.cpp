#include <rvpch.h>
#include "SceneCamera.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	void SceneCamera::SetViewport(float width, float height)
	{
		if (height <= 0.0f)
			return;

		AspectRatio = width / height;
		Recalculate();
	}

	void SceneCamera::SetAspectRatio(float aspect)
	{
		if (aspect <= 0.0f || AspectRatio == aspect)
			return;

		AspectRatio = aspect;
		Recalculate();
	}

	void SceneCamera::Recalculate()
	{
		// Before the viewport has reported a size there is no meaningful
		// aspect. Dividing by it produced a projection full of infinities.
		const float aspect = AspectRatio > 0.0f ? AspectRatio : 16.0f / 9.0f;

		if (Projection == ProjectionType::Orthographic)
		{
			const float left = -0.5f * aspect * OrthographicSize;
			const float right = 0.5f * aspect * OrthographicSize;
			const float bottom = -0.5f * OrthographicSize;
			const float top = 0.5f * OrthographicSize;
			m_Projection = Math::Orthographic(left, right, bottom, top, OrthographicNear, OrthographicFar);
		}
		else
		{
			m_Projection = Math::Perspective(Math::Radians(PerspectiveFOV), aspect,
											PerspectiveNear, PerspectiveFar);
		}
	}
}
