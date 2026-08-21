#pragma once
#include "RageV/Math/Math.h"
#include "RageV/Asset/Curve.h"

namespace RageV
{
	class Scene;
	struct ParticleEmitterComponent;
}

namespace RageV::Particles
{
	// What a particle looks like at a given point in its life.
	struct Appearance
	{
		float Size = 0.0f;
		Vec4  Color{ 1.0f, 1.0f, 1.0f, 1.0f };
	};

	// The ramps, resolved: curves where they are set, the two-point pairs
	// where they are not, channel by channel.
	//
	// A free function rather than code inside the renderer's instance loop,
	// because three things have to agree on it -- the CPU path, the compute
	// shader, and the suite. Two of those cannot call a lambda buried in a
	// draw call, and a rule that exists in three places drifts.
	//
	// The baked tables are passed in rather than looked up here: the caller
	// fetches once per emitter, and this runs once per particle.
	Appearance Evaluate(const ParticleEmitterComponent& emitter, float t,
						const Curve::Baked* sizeCurve,
						const Curve::Baked* colorGradient,
						const Curve::Baked* alphaCurve);

	// The spawn box as three half-axis vectors, in the space the emitter's
	// particles are integrated in.
	//
	// A free function for the same reason Evaluate is one: three callers have
	// to agree on it exactly -- the CPU emitter, the parameters the compute
	// shader is handed, and the volume the editor draws -- and a box the
	// overlay disagrees with is worse than no overlay, because it is a
	// measurement of the wrong thing that looks like a measurement of the
	// right one.
	//
	// **World-space emitters get the box turned by the emitter and not scaled
	// by it**, which is BoxSize's contract. A local-space one gets the plain
	// axes: its particles are stored in the emitter's frame and the transform
	// is applied when they are drawn, so folding it in here would apply it
	// twice. Anything drawing this in world space has to account for that --
	// see EditorLayer::DrawEmitterVolumes.
	//
	// All three are zero for a point emitter, so a caller may add them
	// unconditionally.
	void SpawnBoxAxes(const ParticleEmitterComponent& emitter, const Mat4& world,
					  Vec3 outAxes[3]);

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
