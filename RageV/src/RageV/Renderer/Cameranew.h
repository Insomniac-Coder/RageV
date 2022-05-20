#pragma once
#include "glm/glm.hpp"

namespace RageV
{

	class Cameranew
	{
	public:
		Cameranew(const glm::mat4& projection): m_Projection(projection) {}
		glm::mat4& GetProjection() { return m_Projection; }
	private:
		glm::mat4 m_Projection;
	};

}