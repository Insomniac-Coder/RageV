#pragma once
#include "glm/glm.hpp"
#include <vector>

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

		LightType GetLightType() const { return m_Type; }
		void SetLightType(const LightType& type) { m_Type = type; }

		color& GetLightColor() { return m_Color; }
		const color& GetLightColor() const { return m_Color; }

		// PBR needs a magnitude as well as a hue: a colour alone cannot express
		// the difference between a candle and a floodlight.
		float GetIntensity() const { return m_Intensity; }
		void  SetIntensity(float value) { m_Intensity = value; }

		// Distance at which a positional light reaches zero. Inverse-square
		// falloff never actually reaches zero, so it is windowed against this.
		float GetRange() const { return m_Range; }
		void  SetRange(float value) { m_Range = value; }

		// Spot cone half-angles, in degrees.
		float GetInnerCone() const { return m_InnerCone; }
		void  SetInnerCone(float degrees) { m_InnerCone = degrees; }
		float GetOuterCone() const { return m_OuterCone; }
		void  SetOuterCone(float degrees) { m_OuterCone = degrees; }

	private:
		LightType m_Type = LightType::Directional;
		color m_Color{ 1.0f };
		float m_Intensity = 1.0f;
		float m_Range = 10.0f;
		float m_InnerCone = 20.0f;
		float m_OuterCone = 30.0f;
	};

	// What the renderers actually consume. A struct rather than the tuple this
	// used to be: PBR needs six fields per light and positional arguments stop
	// being readable well before that.
	struct LightRenderData
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Direction{ 0.0f, -1.0f, 0.0f };
		glm::vec3 Color{ 1.0f };
		float Intensity = 1.0f;
		float Range = 10.0f;
		float InnerCone = 20.0f;   // degrees
		float OuterCone = 30.0f;
		Light::LightType Type = Light::LightType::Directional;
	};

	using LightList = std::vector<LightRenderData>;
}
