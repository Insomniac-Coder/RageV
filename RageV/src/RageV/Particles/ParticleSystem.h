#pragma once
#include "RageV/Math/Math.h"

namespace RageV
{
	class Scene;
	struct ParticleEmitterComponent;
}

namespace RageV::Particles
{
	// The CPU simulation: ages, integrates and emits for every emitter in the
	// scene that is not simulating on the GPU.
	//
	// Runs per *frame*, not per fixed step. Particles are appearance, not
	// gameplay -- nothing collides with one, nothing scores one -- and
	// appearance should move as smoothly as the display can show it. This is
	// the opposite call from scripts and physics, and it is deliberate.
	class System
	{
	public:
		static void Update(Scene& scene, float deltaSeconds);

		// Emission for one emitter, spawning `count` at its world transform.
		// What ParticleEmitterComponent::Burst consumes, and what a C++ script
		// with component access can call directly.
		static void Emit(ParticleEmitterComponent& emitter, const Mat4& world, int count);

		// Live particles across every emitter in the scene. For the tests and
		// the stats panel; a walk, not a cache.
		static uint32_t Count(Scene& scene);

		// Whether any emitter asks for weighted blending.
		//
		// Asked of the *scene*, before the frame is described, because the
		// frame graph has to decide whether to allocate the transparency
		// attachments before anything has drawn. Asking the renderer what it
		// collected would answer for the previous frame.
		static bool HasWeightedEmitters(Scene& scene);
	};
}
