#pragma once
#include "glm/glm.hpp"

namespace RageV
{
	typedef glm::vec3 color;
	class Light {

	public:
		enum class LightType
		{
			Directional = 0,
			Point = 1,
			Spot = 2
		};

		LightType GetLightType() { return m_Type; }
		void SetLightType(const LightType& type) { m_Type = type; }
		color& GetLightColor() { return m_Color; }
		//void SetLightColor(const color& col) { m_Color = col; }
	private:
		LightType m_Type = LightType::Directional;
		color m_Color{1.0f};
	};

}