#pragma once
#include "RageV/Core/UUID.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	// What fills the pixels no geometry covers.
	enum class SkyType : uint32_t
	{
		// The clear colour, and nothing drawn. The right answer for a 2D scene
		// or a UI-only one, where a horizon would be an intrusion.
		Color = 0,

		// Three colours and a horizon. Costs no asset, which is why it is the
		// default: a new scene should look like somewhere rather than like a
		// void, and requiring a downloaded panorama to get there does not fit
		// "ease of use".
		Gradient = 1,

		// An environment map. Linear and float, so the sky is allowed to be
		// brighter than white -- which is what makes bloom read as sunlight
		// rather than as a bright patch.
		Cubemap = 2,
	};

	// Scene-wide lighting that is not attached to any entity.
	//
	// The PBR shader's ambient term used to be the literal constants 0.06 and
	// 0.02, unreachable from the editor. It is still an approximation of
	// image-based lighting -- one colour cannot vary with view angle or
	// roughness the way a real environment does -- but it is a value you can
	// set now, and it is what IBL will fall back to for scenes with no
	// environment map.
	//
	// It lives beside the renderer rather than in Scene so that Renderer3D does
	// not have to include the scene layer to be handed one.
	//
	// **This is what a scene owns, and it is deliberately short.** The struct
	// used to hold anti-aliasing, shadows, exposure and bloom as well; those
	// describe the pipeline and the grade rather than the place, and a project
	// with forty scenes stored each of them forty times. Cost settings are on
	// the project now (`RenderSettings`) and look settings are an asset a
	// camera points at (`PostSettings`, a `.rvpostprofile`). What is left is
	// what genuinely differs from one place to the next. ENGINE-NOTES 7s.
	struct SceneEnvironment
	{
		Vec3 AmbientColor{ 0.42f, 0.47f, 0.58f };   // cool, sky-ish
		float AmbientIntensity = 0.12f;

		// --- sky ---------------------------------------------------------------
		SkyType Sky = SkyType::Gradient;

		// Read bottom to top: the colour at the horizon, straight up, and
		// straight down. Linear, like everything else that reaches the scene
		// target.
		Vec3 SkyHorizon{ 0.52f, 0.60f, 0.72f };
		Vec3 SkyZenith { 0.18f, 0.31f, 0.62f };
		Vec3 SkyGround { 0.16f, 0.15f, 0.14f };

		float SkyIntensity = 1.0f;
		// Radians about the world Y axis. A panorama arrives pointing wherever
		// it was shot, and the scene was not built to match it.
		float SkyRotation = 0.0f;

		// An equirectangular panorama or one face of a six-file set. Only read
		// when Sky is Cubemap.
		//
		// Spelled UUID rather than AssetHandle, which is the same type, because
		// AssetHandle is declared in the asset layer and the asset layer already
		// depends on this one. Whoever resolves it does so by handle.
		UUID SkyTexture = UUID::Invalid();

		// --- shaped sky (WR-1) --------------------------------------------------
		//
		// Only read when Sky is Gradient. `height` below is the fragment's
		// abs(direction.y), so 1 is a plain linear mix and less than 1 crowds
		// the transition toward the horizon -- a real night sky's brightness
		// lives in a thin band low down, not spread evenly to the zenith.
		// **0.45 is not a design choice, it is the value the shader hardcoded
		// before this field existed** (`pow(height, 0.45)` in sky.rvshader and
		// `Skybox::GradientAt`'s mirror of it) -- kept as the default so every
		// scene that has never touched this field renders unchanged.
		float SkyCurve = 0.45f;

		// An additive lobe low in the sky, toward CityGlowBearing -- a city's
		// light pollution. Linear/HDR like the other sky colours; black (the
		// default) adds nothing, so an untouched scene is unaffected.
		Vec3 CityGlowColor{ 0.0f, 0.0f, 0.0f };
		// Degrees, the scene's own compass: 0 is south (+z), 90 is west (+x)
		// -- the same convention `sun_rotation` in make_bridge_scene.py uses,
		// so a scene's sun/moon bearing and its city's bearing read as the
		// same kind of number.
		float CityGlowBearing = 0.0f;
		// How tightly the glow hugs CityGlowBearing (higher = narrower) and how
		// fast it fades with elevation (higher = stays lower in the sky).
		// Research range ~4-8 / ~8-16; defaults sit mid-range and are inert
		// while CityGlowColor is black.
		float CityAzimuthK = 6.0f;
		float CityElevationK = 12.0f;

		// An analytic disc, drawn on the sky and baked into the reflection
		// probe like everything else here. Pre-exposure colour -- set it high
		// enough and it clips into bloom, which is the point. Black (the
		// default) draws nothing, so MoonElevation/MoonBearing/MoonDisc are
		// inert until it is set.
		//
		// **A second source of truth, on purpose, not by oversight.** This
		// struct cannot see the scene's entities (see the header comment
		// above), so it cannot read a moon light's own rotation -- a scene
		// that wants the disc and the illumination to agree sets this to the
		// same elevation/bearing it gave that light. WR-9 names the general
		// version of this trap for lamps and lenses; nothing here prevents it
		// for the moon, only documents it.
		Vec3 MoonColor{ 0.0f, 0.0f, 0.0f };
		float MoonElevation = 0.0f;   // degrees over the horizon
		float MoonBearing = 0.0f;     // degrees, CityGlowBearing's convention
		float MoonDisc = 0.0046f;     // angular radius, radians (the real moon's)
	};
}
