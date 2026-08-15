#pragma once
#include "RageV/Math/Math.h"
#include "Camera.h"
#include "RageV/Renderer/RHI/RHIDevice.h"

namespace RageV
{
	struct ParticleEmitterComponent;

	// Instanced particle quads, camera-facing or flat, alpha or additive.
	//
	// One draw per emitter: an emitter is one texture, one blend and one
	// facing, and emitter counts are tens where particle counts are
	// thousands -- so per-emitter draws cost nothing that matters and keep
	// the batching trivially correct.
	//
	// Alpha draws are sorted twice: emitters back to front against each
	// other, and particles back to front within each. Additive draws skip
	// both sorts, because addition commutes -- which is also why additive is
	// the cheap one to recommend.
	class ParticleRenderer
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// False when the shader did not compile.
		static bool IsReady();

		static void SetTargetFormats(RHI::Format color, RHI::Format depth,
									 uint32_t samples = 1,
									 RHI::Format velocity = RHI::Format::Undefined,
									 RHI::Format normal = RHI::Format::Undefined);

		// Resets the per-frame batch pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		static void BeginScene(const Camera& camera, const Mat4& cameraTransform);
		// Sorts and flushes everything submitted since BeginScene.
		static void EndScene();

		// Everything drawable about one emitter: its live pool, converted to
		// world space, faded and sized by age. `world` carries the emitter's
		// transform for local-space pools.
		static void DrawEmitter(const ParticleEmitterComponent& emitter, const Mat4& world);

		// The same draw, reading instances a compute pass already wrote.
		//
		// Nothing about the pipeline, the shader or the blending differs --
		// only who filled the buffer. That is the whole point: a GPU emitter
		// and a CPU one reach the same vertex shader, so "the same look" is
		// structural rather than a claim somebody has to keep true.
		//
		// Dead particles arrive with a size of zero rather than being
		// compacted out, so `count` is the emitter's pool size and not its
		// live count.
		static void DrawEmitterGpu(const ParticleEmitterComponent& emitter, const Mat4& world,
								   const RHI::Ref<RHI::RHIBuffer>& instances, uint32_t count);

		// Instances actually submitted since BeginScene, after the pool cap.
		// For the tests and the stats panel.
		static uint32_t GetParticleCount();

		// --- weighted-blended transparency ------------------------------------
		// EndScene draws the alpha and additive emitters and *keeps* the
		// weighted ones, because they belong to a later pass writing different
		// attachments. These two are that pass and its resolve.

		// Whether anything submitted this frame asked for weighted blending.
		// The frame graph asks before paying for two extra targets.
		static bool HasWeighted();

		// Draws what EndScene held back, into the accumulation and revealage
		// attachments the transparent pass bound.
		static void FlushWeighted();

		// Composites those two back over the scene. A fullscreen triangle;
		// the textures come from the graph.
		static void ResolveWeighted(const RHI::Ref<RHI::RHITexture>& accumulate,
									const RHI::Ref<RHI::RHITexture>& revealage);

	private:
		static void EnsurePipelines();
		static void Flush();
	};
}
