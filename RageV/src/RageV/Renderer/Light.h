#pragma once
#include "RageV/Math/Math.h"
#include <vector>

namespace RageV
{
	typedef Vec3 color;

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

	// One emissive surface, as a rectangle a traced bounce can aim at.
	//
	// **This is what turns the bounce from a lottery into an integral.** A
	// cosine-sampled hemisphere finds a ceiling luminaire by accident: most
	// rays hit a wall at three percent albedo and return almost nothing, a few
	// hit the panel and return several hundred times that, and the average of
	// four such samples is a number that jumps from pixel to pixel. That is
	// speckle -- variance, not a bug -- and it falls only as 1/sqrt(N), which
	// is why raising the ray count barely moves it.
	//
	// Sampling the emitter directly removes it: every sample sees the panel,
	// so the estimate stops depending on whether a ray happened to find one.
	//
	// A rectangle because a luminaire is one. TangentU and TangentV are
	// half-extents, so it spans centre +/- U +/- V, its area is 4|U x V| and
	// its normal falls out of their cross product. A non-planar emissive mesh
	// is approximated by the flattest rectangle of its own bounds.
	//
	// Here rather than in Renderer3D.h because it is a light, and because the
	// scene builds these and has no business including the renderer.
	struct AreaEmitter
	{
		Vec3 Centre{ 0.0f };
		Vec3 TangentU{ 1.0f, 0.0f, 0.0f };
		Vec3 TangentV{ 0.0f, 0.0f, 1.0f };
		Vec3 Radiance{ 0.0f };

		// Which object this rectangle stands for, as an opaque id the scene
		// chooses and the renderer only compares. **The traced bounce needs
		// to know, at a hit, whether the surface it landed on is one this
		// list answers for**: emissive found by a shadow ray *and* by a
		// hemisphere ray is counted twice, and emissive found by neither is
		// lost. Both used to happen -- see kMaxAreaEmitters in Renderer3D.h.
		//
		// Not a pointer, deliberately. The renderer compares it against the
		// same id on a RayCaster and must not be tempted to dereference
		// something the scene owns and may have rebuilt.
		uint64_t Owner{ 0 };
	};

	// What the renderers actually consume. A struct rather than the tuple this
	// used to be: PBR needs six fields per light and positional arguments stop
	// being readable well before that.
	struct LightRenderData
	{
		Vec3 Position{ 0.0f };
		Vec3 Direction{ 0.0f, -1.0f, 0.0f };
		Vec3 Color{ 1.0f };
		float Intensity = 1.0f;
		float Range = 10.0f;
		float InnerCone = 20.0f;   // degrees
		float OuterCone = 30.0f;
		Light::LightType Type = Light::LightType::Directional;
		bool CastShadows = false;
	};

	using LightList = std::vector<LightRenderData>;
}
