#pragma once
#include "RageV/Core/UUID.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	// How the image is anti-aliased.
	//
	//   FXAA looks at one pixel's neighbourhood and guesses. One pass, no
	//   prerequisites, and it softens the picture slightly -- which is its
	//   well-known cost and the reason it was first rather than best.
	//
	//   SMAA reconstructs the silhouette instead: it finds the run of pixels
	//   an edge spans, works out which way the real line sloped, and computes
	//   the coverage the rasterizer should have produced. Three passes over
	//   two small intermediates, and sharper for it. ENGINE-NOTES 7n.
	//
	// TAA is named in the roadmap and absent here on purpose: it needs motion
	// vectors, which means every mesh carrying its previous world transform
	// and the renderer writing a velocity target, plus a jittered projection
	// and a history buffer. That is a renderer feature with its own
	// prerequisites, not a post pass -- and the same motion vectors would then
	// also buy motion blur and temporal upscaling.
	//   SSAA renders the whole scene larger and averages it down. It is the
	//   only one of the three that anti-aliases *shading* rather than
	//   geometry: a specular highlight sparkling across a curved surface, or a
	//   texture aliasing into moire, is a signal the frame never sampled
	//   finely enough, and no filter working on the finished image can invent
	//   what was missed. It costs the square of the factor in fill.
	enum class AntiAliasing : uint32_t
	{
		None = 0,
		FXAA = 1,
		SMAA = 2,
		SSAA = 3,
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

		// Ceiling on what a single pixel may contribute to bloom.
		//
		// Without one, anything very bright and very small -- the sun reflected
		// in curved metal is the usual culprit, a few hundred nits across less
		// than a texel of the mip being read -- survives the whole chain as an
		// isolated blob that floats in the air near the surface that produced
		// it. Every engine has this control for the same reason.
		//
		// It bounds the contribution, not the pixel: the scene keeps its real
		// values, and only what bleeds out of them is limited.
		float BloomClamp = 16.0f;

		// --- shadows -----------------------------------------------------------
		bool ShadowsEnabled = true;

		// More cascades means better texel density near the camera and more
		// scene renders. Four is the usual answer and the most this supports.
		int ShadowCascades = 4;

		// Per cascade, square. This is the single biggest lever on both quality
		// and cost: four 2048 maps is 64 MB of depth.
		int ShadowResolution = 2048;

		// How far from the camera shadows are drawn at all. Not the camera's
		// far plane, which is usually a kilometre: past this distance the
		// texels are so large the shadow is worse than none.
		float ShadowDistance = 40.0f;

		// Blend between a logarithmic split, which distributes texels correctly
		// and starves the far cascades, and a uniform one, which does the
		// reverse. 1 is fully logarithmic.
		float ShadowSplitLambda = 0.85f;

		// How far along the surface normal a sample is pushed, in shadow
		// texels. Raising it removes acne and starts detaching shadows from
		// their casters; there is no value that has neither, which is why the
		// shadow pass also writes back faces.
		float ShadowNormalOffset = 0.9f;

		AntiAliasing AA = AntiAliasing::FXAA;

		// How many times larger each axis is drawn when AA is SSAA. Ignored
		// otherwise.
		//
		// **Cost is the square of this.** Two means four times the pixels
		// shaded, four means sixteen -- which is why it is a number the person
		// paying for it chooses rather than a fixed part of the mode.
		//
		// Clamped to something a texture can be: at 4 a 4K output would ask
		// for a 16K target, which is past what a lot of hardware will allocate
		// and all of what it would be sensible to.
		int SupersampleFactor = 2;
	};
}
