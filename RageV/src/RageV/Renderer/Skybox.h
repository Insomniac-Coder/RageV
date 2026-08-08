#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/Camera.h"
#include "RageV/Renderer/Environment.h"
#include "glm/glm.hpp"

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

		static void SetTargetFormats(RHI::Format color, RHI::Format depth);

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
		static void Draw(const Camera& camera, const glm::mat4& cameraTransform,
						 const SceneEnvironment& environment,
						 const RHI::Ref<RHI::RHITexture>& cubemap);

		// The matrix the shader uses: clip space to a world direction, camera
		// translation removed and the sky's rotation folded in. Exposed so the
		// test suite can check the reconstruction against known directions
		// without a GPU -- getting this wrong tilts or mirrors the whole sky,
		// and it is not obvious from a still image which of the two happened.
		static glm::mat4 BuildDirectionMatrix(const glm::mat4& projection,
											  const glm::mat4& cameraTransform,
											  float rotation);
	};
}
