#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/Environment.h"
#include "glm/glm.hpp"

namespace RageV
{
	// The fullscreen passes that run after the scene: bloom, tone mapping and
	// anti-aliasing.
	//
	// Each entry point records one fullscreen triangle into a pass the caller
	// has already begun -- the render graph owns the targets and the ordering,
	// and this owns the shaders and the arithmetic. Keeping those apart is what
	// lets the chain be rearranged without touching any of the maths.
	class PostProcess
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// Resets the per-frame descriptor pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		// Bloom stage 1: threshold and halve. Source is the linear HDR scene.
		static void Prefilter(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
							  uint32_t width, uint32_t height, RHI::Format outputFormat,
							  float threshold, float knee, float clamp);

		// Bloom stage 2: halve again. Source is the level above.
		static void Downsample(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
							   uint32_t sourceWidth, uint32_t sourceHeight,
							   RHI::Format outputFormat);

		// Bloom stage 3: blur the smaller level up onto the larger one.
		// Additive, so the level being drawn into keeps what it already had.
		static void Upsample(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
							 uint32_t sourceWidth, uint32_t sourceHeight,
							 RHI::Format outputFormat, float radius);

		// Exposure, bloom, ACES and the transfer function, in one pass. This is
		// where linear HDR becomes an image.
		static void Tonemap(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& scene,
							const RHI::Ref<RHI::RHITexture>& bloom, RHI::Format outputFormat,
							float exposure, float bloomIntensity);

		// Anti-aliasing, on the tone-mapped image.
		static void FXAA(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
						 uint32_t width, uint32_t height, RHI::Format outputFormat,
						 float contrastThreshold, float relativeThreshold);

		// A straight copy, for when anti-aliasing is off but the chain still
		// has to land in the target the caller wanted.
		static void Blit(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
						 RHI::Format outputFormat);

		static bool IsReady();

	private:
		// Which shader. Pipelines are cached per (shader, output format),
		// because the chain writes into an HDR format and then an LDR one, and
		// a pipeline bakes the format it renders to.
		enum class Shader { Prefilter, Downsample, Upsample, Tonemap, FXAA, Blit, Count };

		static void Dispatch(RHI::RHICommandList& cmd, Shader shader, RHI::Format outputFormat,
							 const RHI::Ref<RHI::RHITexture>& first,
							 const RHI::Ref<RHI::RHITexture>& second,
							 const void* params, uint32_t paramSize);
	};
}
