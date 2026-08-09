#pragma once
#include "RageV/Math/Math.h"

namespace RageV
{
	// Base for anything the renderer can draw from. It holds the projection and
	// nothing else -- the view comes from a transform the caller supplies. That
	// split is what lets a camera attached to an entity and a free-flying editor
	// camera share one interface without either knowing about the other.
	class Camera
	{
	public:
		Camera() = default;
		Camera(const Mat4& projection) : m_Projection(projection) {}
		virtual ~Camera() = default;

		const Mat4& GetProjection() const { return m_Projection; }

	protected:
		Mat4 m_Projection{ 1.0f };
	};
}
