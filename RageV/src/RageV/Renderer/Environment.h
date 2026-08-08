#pragma once
#include "RageV/Core/UUID.h"
#include "glm/glm.hpp"

namespace RageV
{
	// How the image is anti-aliased.
	//
	// Only two of these are real today. The other two are named because the
	// choice is worth being explicit about and because what each *needs* is
	// the useful part:
	//
	//   SMAA needs two precomputed lookup textures -- an area table and a
	//   search table -- vendored into the repository, and three passes rather
	//   than one. Sharper than FXAA for the same idea.
	//
	//   TAA needs motion vectors, which means every mesh carrying its previous
	//   world transform and the renderer writing a velocity target, plus a
	//   jittered projection and a history buffer. That is a renderer feature
	//   with its own prerequisites, not a post pass -- and the same motion
	//   vectors would then also buy motion blur and temporal upscaling.
	enum class AntiAliasing : uint32_t
	{
		None = 0,
		FXAA = 1,
	};

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
	struct SceneEnvironment
	{
		glm::vec3 AmbientColor{ 0.42f, 0.47f, 0.58f };   // cool, sky-ish
		float AmbientIntensity = 0.12f;

		// --- sky ---------------------------------------------------------------
		SkyType Sky = SkyType::Gradient;

		// Read bottom to top: the colour at the horizon, straight up, and
		// straight down. Linear, like everything else that reaches the scene
		// target.
		glm::vec3 SkyHorizon{ 0.52f, 0.60f, 0.72f };
		glm::vec3 SkyZenith { 0.18f, 0.31f, 0.62f };
		glm::vec3 SkyGround { 0.16f, 0.15f, 0.14f };

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

		// --- post processing --------------------------------------------------
		// Applied before the tone curve, which is what makes it an exposure
		// control rather than a brightness one: it slides the scene along the
		// response curve instead of scaling the result of it.
		float Exposure = 1.0f;

		bool BloomEnabled = true;
		// Brightness at which a pixel starts to bleed. Above 1 only genuinely
		// over-bright things glow, which is usually what is wanted.
		float BloomThreshold = 1.0f;
		// Width of the ramp around the threshold. Zero is a hard cut, which
		// pops as something crosses it and reads as flickering.
		float BloomKnee = 0.5f;
		float BloomIntensity = 0.06f;

		AntiAliasing AA = AntiAliasing::FXAA;
	};
}
