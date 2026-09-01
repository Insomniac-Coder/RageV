#pragma once
#include "RageV/Math/Math.h"
#include "RageV/Renderer/RHI/RHITypes.h"
#include <memory>
#include <vector>

namespace RageV
{
	// An emitter names the map it aims by, and nothing here looks inside one.
	// Declared rather than included for that reason: this header is plain
	// data, and it should stay cheap enough for the component registry and
	// the serializer to include without dragging the RHI in behind it.
	namespace RHI { class RHITexture; class RHISampler; }

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

		// **The physical size of the emitter, in metres of radius. Zero is a
		// point, and the default, and every light authored before this.**
		//
		// What it buys is the specular image: a point source's reflection is a
		// delta -- a wave facet returns it only where the mirror direction is
		// exact, which is a handful of pixels that blink -- while a sphere of
		// real size is integrated over the microfacet lobe and comes back as
		// the continuous streak every night photograph of water is made of.
		// No brightness setting substitutes for extent.
		//
		// Diffuse lighting, cones, range and shadows are untouched: at these
		// radii the diffuse difference is far below what the inverse-square
		// window already discards, so the cost stays in the one term that
		// shows it.
		float SourceRadius = 0.0f;

		// Only one directional light's shadows are rendered per frame -- the
		// first that asks. A second set of cascades is four more scene renders
		// for a light that, in almost every scene, is a fill.
		bool CastShadows = true;

		// **Whether this light is part of the baked lighting** -- the
		// mobility split every baking engine carries (Unity's Baked/Realtime
		// modes, Unreal's Static/Movable). On, the light contributes to the
		// solve and any change to it names a different bake. Off, the light
		// is a realtime light: it renders direct light and shadows exactly
		// as before, but the lighting hash skips it entirely -- so a script
		// can flip it on and off without invalidating a single file -- and
		// the solve does not see it, so its own bounce is simply absent from
		// baked GI, which for a headlight or a flashlight nobody has ever
		// noticed. On by default so every existing scene keeps its hash and
		// its bakes.
		bool IsBaked = true;
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
	// The loader's account of an emissive map's content -- see TextureLoader.
	// Declared rather than included: an emitter passes one along and never
	// looks inside it.
	struct TextureStats;

	// **Where a reflection probe stands, and how far it reaches.**
	//
	// Here rather than in Scene.h because two consumers need it and neither
	// owns it: the scene picks a probe per object with this, and the traced
	// bounce picks one per *hit* with the same rule. Those two answers have to
	// be the same rule, and the surest way to keep them so is one type and one
	// table.
	struct ProbeVolume
	{
		Vec3 Position{ 0.0f };
		float Influence = 0.0f;
		// Index into the irradiance and environment cube arrays. Slot 0 is
		// the sky, which is what a point inside no probe's influence gets.
		uint32_t Slot = 0;
	};

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

		// **Where on the rectangle the light actually is.** Null leaves the
		// rectangle radiating evenly at Radiance, which is right for a panel
		// that glows all over and only average-right for one whose emissive
		// map is four lit cells of a hundred and forty-four. With it, a
		// sampler picks a cell in proportion to what the cell emits and reads
		// the real texel there -- so the shadow rays go to the lit cells and
		// the light arrives from where it is painted.
		RHI::Ref<RHI::RHITexture> EmissiveMap;
		// The material's own sampler, so the heap slot the renderer takes is
		// the same one the material's record already holds rather than a
		// second entry for one image.
		RHI::Ref<RHI::RHISampler> EmissiveSampler;
		std::shared_ptr<const TextureStats> Emission;

		// Texture uv back to the rectangle's own (su, sv) in [-1,1]:
		//     su = UvToSurface[0] * u + UvToSurface[1] * v + UvToSurface[2]
		//     sv = UvToSurface[3] * u + UvToSurface[4] * v + UvToSurface[5]
		// Affine, and only set when it is *exactly* affine -- the scene fills
		// it for the flat primitives whose mapping it knows and leaves the
		// emitter uniform otherwise, because a guess here would put light on
		// the wrong part of a surface with nothing to say it had.
		float UvToSurface[6]{ 0, 0, 0, 0, 0, 0 };
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
		// See Light::SourceRadius. Rides GpuLight.Direction.w to the shader.
		float SourceRadius = 0.0f;
		Light::LightType Type = Light::LightType::Directional;
		bool CastShadows = false;
		// See Light::IsBaked: hashed into the lighting only when set, and the
		// solve shades hits with this light only when set.
		bool IsBaked = true;
	};

	using LightList = std::vector<LightRenderData>;
}
