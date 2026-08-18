#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/Camera.h"
#include "RageV/Renderer/Environment.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	// Fills whatever the scene did not cover.
	//
	// Kept apart from Renderer3D because it is not a mesh renderer: there is no
	// geometry, no material and no light loop, and folding it in would mean the
	// scene pipeline growing a second shape for the one draw that has none of
	// those things.
	class Skybox
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		static void SetTargetFormats(RHI::Format color, RHI::Format depth,
									 uint32_t samples = 1,
									 RHI::Format velocity = RHI::Format::Undefined,
									 RHI::Format normal = RHI::Format::Undefined,
									 RHI::Format indirect = RHI::Format::Undefined);

		// Resets the per-frame descriptor pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		static bool IsReady();

		// Call after the opaque geometry and before anything blended.
		//
		// After, because the depth test is the entire optimisation -- a sky
		// drawn first shades every pixel and then gets painted over. Before the
		// blended pass, because a transparent surface has to have something
		// behind it to blend with.
		//
		// `cubemap` may be null; the shader declares the binding whether or not
		// the scene uses it, so a neutral cube is bound in its place.
		//
		// `jitter` is the sub-pixel offset already folded into `camera`, in
		// NDC, and is passed for the same reason Renderer3D::BeginScene takes
		// it: the sky writes motion vectors too, and they have to be free of
		// the camera's dither. Defaulted to none, so a caller that did not
		// jitter its camera does not get the correction either.
		static void Draw(const Camera& camera, const Mat4& cameraTransform,
						 const SceneEnvironment& environment,
						 const RHI::Ref<RHI::RHITexture>& cubemap,
						 const Vec2& jitter = Vec2(0.0f, 0.0f));

		// What the scene's surfaces reflect.
		//
		// Never null once Init has run. A gradient sky becomes a small cube of
		// its own colours rather than nothing, so a metal surface reflects the
		// sky that is actually behind it -- a chrome sphere against a visible
		// blue sky reflecting black is the single most obvious way for
		// reflections to look broken, and it is the default case.
		//
		// `cubemap` is the scene's environment map if it has one, and is
		// returned unchanged when it does.
		static RHI::Ref<RHI::RHITexture> ResolveEnvironment(const SceneEnvironment& environment,
															const RHI::Ref<RHI::RHITexture>& cubemap);

		// The diffuse half: how much light arrives at a surface facing each
		// direction. A gradient sky is convolved the same way an environment
		// map is, so a scene lit by the default sky is still lit from above
		// rather than by one flat colour.
		//
		// Takes `cubemap` as well, and needs it: what the ambient should be
		// when a cubemap sky has no irradiance depends entirely on what Draw
		// is putting on screen. With no cube, Draw falls back to the gradient
		// and the gradient's irradiance is the right answer. With a cube but
		// no irradiance, the gradient's colours describe a sky that is not
		// being drawn -- so this returns black instead, because no ambient is
		// honest and a plausible wrong one is not.
		static RHI::Ref<RHI::RHITexture> ResolveIrradiance(const SceneEnvironment& environment,
														   const RHI::Ref<RHI::RHITexture>& cubemap,
														   const RHI::Ref<RHI::RHITexture>& irradiance);

		// The matrix the shader uses: clip space to a world direction, camera
		// translation removed and the sky's rotation folded in. Exposed so the
		// test suite can check the reconstruction against known directions
		// without a GPU -- getting this wrong tilts or mirrors the whole sky,
		// and it is not obvious from a still image which of the two happened.
		static Mat4 BuildDirectionMatrix(const Mat4& projection,
											  const Mat4& cameraTransform,
											  float rotation);
	};
}
