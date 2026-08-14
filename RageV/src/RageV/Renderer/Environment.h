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
	};
}
