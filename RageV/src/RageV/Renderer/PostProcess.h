#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/Environment.h"
#include "RageV/Math/Math.h"

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

		// SMAA, in the three passes it takes. Also on the tone-mapped image,
		// and for the same reason FXAA is. ENGINE-NOTES 7n.

		// 1: where the discontinuities are, into RG8.
		static void SmaaEdges(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
							  uint32_t width, uint32_t height, RHI::Format outputFormat,
							  float threshold, float localContrast);

		// 2: reconstruct the line each edge belongs to and store its coverage.
		static void SmaaWeights(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& edges,
								uint32_t width, uint32_t height, RHI::Format outputFormat);

		// 3: mix each pixel along the axis its coverage came from.
		static void SmaaBlend(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
							  const RHI::Ref<RHI::RHITexture>& weights,
							  uint32_t width, uint32_t height, RHI::Format outputFormat);

		// SSAA's resolve: average an N-times-larger image down to this one.
		// On the linear HDR scene, before tone mapping -- see the shader.
		static void SsaaResolve(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
								uint32_t sourceWidth, uint32_t sourceHeight,
								RHI::Format outputFormat, int factor);

		// TAA's resolve: blend the jittered frame into the accumulated one.
		//
		// On the linear HDR scene, in the same slot as the SSAA resolve and
		// for the same reason. `history` is last frame's output of this same
		// pass and `velocity` the scene's motion vectors; `hasHistory` is
		// false on the first frame and after a resize, where the correct
		// answer is the current frame whole. ENGINE-NOTES 7r.
		static void TemporalResolve(RHI::RHICommandList& cmd,
									const RHI::Ref<RHI::RHITexture>& current,
									const RHI::Ref<RHI::RHITexture>& history,
									const RHI::Ref<RHI::RHITexture>& velocity,
									uint32_t width, uint32_t height,
									RHI::Format outputFormat,
									float feedback, bool hasHistory);

		// A straight copy, for when anti-aliasing is off but the chain still
		// has to land in the target the caller wanted.
		static void Blit(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
						 RHI::Format outputFormat);

		static bool IsReady();

	private:
		// Which shader. Pipelines are cached per (shader, output format),
		// because the chain writes into an HDR format and then an LDR one, and
		// a pipeline bakes the format it renders to.
		enum class Shader
		{
			Prefilter, Downsample, Upsample, Tonemap, FXAA, Blit,
			SmaaEdges, SmaaWeights, SmaaBlend,
			SsaaResolve,
			TaaResolve,
			Count
		};

		// How a binding wants to read its texture.
		//
		// Bloom and FXAA sample between texels and want the filter; SMAA's
		// first two passes address exact texels and its edge and weight maps
		// are *classifications*, not colours -- a filtered read of a flag
		// halfway between "edge" and "no edge" is a value that means nothing.
		enum class Sampling { Linear, Point };

		// `third` is only for the temporal resolve, which needs the frame, the
		// history and the motion vectors at once. Null bindings are skipped
		// rather than filled: writing a binding the shader never declared is
		// out of range for the layout, and the driver takes it badly.
		static void Dispatch(RHI::RHICommandList& cmd, Shader shader, RHI::Format outputFormat,
							 const RHI::Ref<RHI::RHITexture>& first,
							 const RHI::Ref<RHI::RHITexture>& second,
							 const void* params, uint32_t paramSize,
							 Sampling firstSampling = Sampling::Linear,
							 Sampling secondSampling = Sampling::Linear,
							 const RHI::Ref<RHI::RHITexture>& third = nullptr,
							 Sampling thirdSampling = Sampling::Linear);
	};
}
