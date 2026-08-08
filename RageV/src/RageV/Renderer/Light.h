#pragma once
#include "glm/glm.hpp"
#include <vector>

namespace RageV
{
	typedef glm::vec3 color;

	// Plain data. Every accessor this used to have was a bare assignment with
	// no invariant to protect, and private members cannot be described to the
	// component registry -- so the encapsulation bought nothing and blocked the
	// reflection that the serializer, the inspector and later the C# layer all
	// need.
	struct Light {
		enum class LightType
		{
			Directional = 0,
			Point = 1,
			Spot = 2
		};

		LightType Type = LightType::Directional;
		color Color{ 1.0f };

		// PBR needs a magnitude as well as a hue: a colour alone cannot express
		// the difference between a candle and a floodlight.
		float Intensity = 1.0f;

		// Distance at which a positional light reaches zero. Inverse-square
		// falloff never actually reaches zero, so it is windowed against this.
		float Range = 10.0f;

		// Spot cone half-angles, in degrees.
		float InnerCone = 20.0f;
		float OuterCone = 30.0f;

		// Only one directional light's shadows are rendered per frame -- the
		// first that asks. A second set of cascades is four more scene renders
		// for a light that, in almost every scene, is a fill.
		bool CastShadows = true;
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
		bool CastShadows = false;
	};

	using LightList = std::vector<LightRenderData>;
}
