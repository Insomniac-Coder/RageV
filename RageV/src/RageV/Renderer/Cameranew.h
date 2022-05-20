#pragma once
#include "glm/glm.hpp"

namespace RageV
{

	class Cameranew
	{
	public:
		Cameranew() = default;
		Cameranew(const glm::mat4& projection): m_Projection(projection) {}
		virtual ~Cameranew() {}
		const glm::mat4& GetProjection() const { return m_Projection; }
	protected:
		glm::mat4 m_Projection{1.0f};
	};

}