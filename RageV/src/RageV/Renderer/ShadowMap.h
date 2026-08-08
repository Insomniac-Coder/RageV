#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "glm/glm.hpp"
#include <functional>

namespace RageV
{
	// One slice of a cascaded shadow map.
	struct ShadowCascade
	{
		// World space straight to shadow-map lookup coordinates: xy are texture
		// coordinates and z is the depth to compare against, after the divide
		// by w. The texture-space bias is folded in on the CPU, including the
		// vertical flip one backend needs and the other must not have -- see
		// HANDOFF §5, which is where that cost a day.
		glm::mat4 LookupMatrix{ 1.0f };

		// Without the bias, for rendering the cascade.
		glm::mat4 ViewProjection{ 1.0f };

		// How far down the view axis this cascade reaches. Selection compares
		// against these in order.
		float SplitDepth = 0.0f;

		// World-space size of one shadow texel. Normal-offset bias scales with
		// it, because the error being corrected is one texel wide by
		// definition: a fixed offset is too small on the near cascade and
		// detaches shadows on the far one.
		float TexelWorldSize = 0.0f;
	};

	// Cascaded shadow maps for one directional light.
	//
	// Directional only, deliberately. A directional light has no position, so
	// its shadow has to cover whatever the camera can see -- which is the whole
	// reason cascades exist and the only case where the fitting is hard. Spot
	// and point lights have a frustum and a range of their own and are a
	// simpler problem on top of this one.
	class ShadowMap
	{
	public:
		static constexpr uint32_t kMaxCascades = 4;

		static void Init(RHI::RHIDevice& device);
		static void Shutdown();
		static bool IsReady();

		// Fits `count` cascades to the camera's frustum.
		//
		// Pure, and checked by the test suite. Every property that matters here
		// is invisible in a still image and obvious in motion: a cascade that
		// changes size as the camera turns makes shadow edges crawl, and one
		// that is not snapped to its own texel grid makes them shimmer on every
		// sub-texel step. Neither shows up in a screenshot.
		static void ComputeCascades(const glm::mat4& cameraTransform,
									float fovYRadians, float aspect,
									float nearClip, float farClip,
									const glm::vec3& lightDirection,
									uint32_t count, uint32_t resolution,
									float lambda, bool flipLookupY,
									ShadowCascade* out);

		// Draws the scene from one cascade's point of view. Given the matrix to
		// transform world positions by; the caller draws whatever casts.
		using DrawCasters = std::function<void(const glm::mat4& viewProjection)>;

		// Renders every cascade. Must be called outside a render pass -- it
		// opens one per cascade.
		static void Render(RHI::RHICommandList& cmd, const ShadowCascade* cascades,
						   uint32_t count, uint32_t resolution, const DrawCasters& draw);

		// What the last Render produced. The lit pass reads these rather than
		// being handed them, because it is called once per viewport and the
		// cascades belong to the frame.
		static const ShadowCascade* GetCascades();
		static uint32_t GetResolution();

		// Which light in the scene's list the cascades were fitted to. -1 when
		// nothing casts.
		static int GetLightIndex();
		static void SetLightIndex(int index);

		// Nothing has been rendered this frame, so surfaces must not sample.
		static void Invalidate();
		static bool HasCascades();
		static uint32_t GetCascadeCount();

		static RHI::Ref<RHI::RHITexture> GetCascadeTexture(uint32_t index);
		static RHI::Ref<RHI::RHISampler> GetSampler();

		// The 1x1 fully-lit depth bound where a cascade does not exist. A
		// comparison sampler still has to be given something, and "everything
		// passes" is the only harmless answer.
		static RHI::Ref<RHI::RHITexture> GetEmptyTexture();
	};
}
