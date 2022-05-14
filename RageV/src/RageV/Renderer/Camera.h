#pragma once
#include "glm/glm.hpp"

namespace RageV
{
	enum class ProjectionType
	{
		Othrographic = 0,
		Projection = 1
	};
	class Camera
	{
	public:
		virtual const glm::mat4& GetViewMatrix() const = 0;
		virtual const glm::mat4& GetProjectionMatrix() const = 0;
		virtual const glm::mat4& GetViewProjectionMatrix() const = 0;
		virtual void SetPosition(const glm::vec3& position) = 0;
	private:
		virtual void  RecalculateViewMatrix() = 0;
		ProjectionType m_ProjectionType;
	};

}