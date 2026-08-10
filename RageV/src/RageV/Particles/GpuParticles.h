#pragma once
#include "RageV/Math/Math.h"
#include "RageV/Core/UUID.h"
#include "RageV/Renderer/RHI/RHIDevice.h"

namespace RageV
{
	class Scene;
	struct ParticleEmitterComponent;
}

namespace RageV::Particles
{
	// The GPU half of the particle system: one compute dispatch per emitter
	// that has SimulateOnGpu set, writing an instance buffer the ordinary
	// particle draw reads.
	//
	// **Per-emitter state is cached by UUID and kept when the flag goes off.**
	// Switching between CPU and GPU is meant to cost nothing a person can
	// perceive, so the buffers an emitter has already paid for are not thrown
	// away the moment it stops using them -- toggling twice would otherwise
	// mean two allocations and two uploads. State is released when the
	// emitter is gone, not when it is idle.
	class Gpu
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// False when the compute pipeline did not build -- a missing shader,
		// or a device without compute. Every emitter falls back to the CPU
		// path in that case, which is why this is worth asking rather than
		// assuming.
		static bool IsReady();

		// Records this frame's simulation for every GPU emitter in the scene.
		//
		// **Once per frame, and outside a render pass.** Vulkan forbids a
		// dispatch inside one, and the editor renders the same scene twice --
		// so this is deliberately not part of the render path, which would
		// advance the simulation once per view.
		static void Simulate(Scene& scene, RHI::RHICommandList& cmd, float deltaSeconds);

		// The instance buffer an emitter's dispatch wrote, and how many
		// instances it holds. Null for an emitter with no GPU state, which is
		// what a CPU emitter has.
		static RHI::Ref<RHI::RHIBuffer> GetInstances(UUID emitter, uint32_t& outCount);

		// Drops state for emitters the scene no longer contains. Cheap, and
		// called when a scene stops rather than per frame: the point is to
		// release a destroyed emitter's buffers, not to punish an idle one.
		static void Collect(Scene& scene);

		// Everything, for a scene change or a device teardown.
		static void Clear();

		// How many emitters currently hold GPU state. For the tests and the
		// stats panel -- and for asserting that a toggle did *not* allocate.
		static uint32_t GetResidentCount();
	};
}
