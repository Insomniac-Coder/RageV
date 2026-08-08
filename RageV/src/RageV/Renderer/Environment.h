#pragma once
#include "glm/glm.hpp"

namespace RageV
{
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
