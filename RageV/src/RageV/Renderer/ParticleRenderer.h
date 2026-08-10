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

		static void SetTargetFormats(RHI::Format color, RHI::Format depth);

		// Resets the per-frame batch pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		static void BeginScene(const Camera& camera, const Mat4& cameraTransform);
		// Sorts and flushes everything submitted since BeginScene.
		static void EndScene();

		// Everything drawable about one emitter: its live pool, converted to
		// world space, faded and sized by age. `world` carries the emitter's
		// transform for local-space pools.
		static void DrawEmitter(const ParticleEmitterComponent& emitter, const Mat4& world);

		// Instances actually submitted since BeginScene, after the pool cap.
		// For the tests and the stats panel.
		static uint32_t GetParticleCount();

	private:
		static void EnsurePipelines();
		static void Flush();
	};
}
