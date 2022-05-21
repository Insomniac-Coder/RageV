#include <rvpch.h>
#include "SceneCamera.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

namespace RageV
{



	void SceneCamera::SetOrthgraphicSize(float size)
	{
		m_Size = size;
		Recalculate();
	}

	void SceneCamera::SetViewport(float width, float height)
	{
		m_AspectRatio = width / height;
		Recalculate();
	}

	void SceneCamera::Recalculate()
	{
		if (m_ProjectionType == ProjectionType::Orthographic)
		{
			float left = -0.5f * m_AspectRatio * m_Size;
			float right = 0.5f * m_AspectRatio * m_Size;
			float bottom = -0.5f * m_Size;
			float top = 0.5f * m_Size;
			m_Projection = glm::ortho(left, right, bottom, top, m_OrthoNear, m_OrthoFar);
		}
		if (m_ProjectionType == ProjectionType::Perspective)
		{
			m_Projection = glm::perspective(glm::radians(m_PerspectiveFOV), m_AspectRatio, m_PerspectiveNear, m_PerspectiveFar);
		}
	}


}