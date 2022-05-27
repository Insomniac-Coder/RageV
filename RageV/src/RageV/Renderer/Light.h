#pragma once

namespace RageV
{

	class Light {

	public:
		enum class LightType
		{
			Directional = 0,
			Point = 1,
			Spot =  2
		};

		const LightType& GetLightType() const { return m_Type; }
		void SetLightType(const LightType& type) { m_Type = type; }

	private:
		LightType m_Type = LightType::Directional;

	};

}