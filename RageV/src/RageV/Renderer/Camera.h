#pragma once
#include "glm/glm.hpp"

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
		Camera(const glm::mat4& projection) : m_Projection(projection) {}
		virtual ~Camera() = default;

		const glm::mat4& GetProjection() const { return m_Projection; }

	protected:
		glm::mat4 m_Projection{ 1.0f };
	};
}
