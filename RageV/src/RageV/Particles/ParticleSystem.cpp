#include <rvpch.h>
#include "ParticleSystem.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Math/Math.h"

namespace RageV::Particles
{
	namespace
	{
		// xorshift32: two shifts and an xor per number, no state beyond the
		// seed on the component, and the same sequence on every machine --
		// which is what lets a test assert particle counts exactly.
		uint32_t NextRandom(uint32_t& state)
		{
			state ^= state << 13;
			state ^= state >> 17;
			state ^= state << 5;
			return state;
		}

		float RandomFloat(uint32_t& state)   // [0, 1)
		{
			return (float)(NextRandom(state) >> 8) / 16777216.0f;
		}

		float RandomSigned(uint32_t& state)  // [-1, 1)
		{
			return RandomFloat(state) * 2.0f - 1.0f;
		}

		// A direction within `spreadRadians` of `axis`, uniformly over the
		// cap. Uniform matters: sampling angles directly bunches particles at
		// the cone's centre and reads as a jet inside a jet.
		Vec3 RandomInCone(uint32_t& state, const Vec3& axis, float spreadRadians)
		{
			const float cosSpread = std::cos(Math::Clamp(spreadRadians, 0.0f, Math::Pi));
			const float cosTheta = 1.0f - RandomFloat(state) * (1.0f - cosSpread);
			const float sinTheta = std::sqrt(Math::Max(0.0f, 1.0f - cosTheta * cosTheta));
			const float phi = RandomFloat(state) * Math::TwoPi;

			// An orthonormal frame around the axis. The reference axis just
			// has to not be parallel; Y fails only when the axis is Y.
			const Vec3 reference = std::fabs(axis.y) < 0.99f ? Vec3(0.0f, 1.0f, 0.0f)
															 : Vec3(1.0f, 0.0f, 0.0f);
			const Vec3 tangent = Math::Normalize(Math::Cross(reference, axis));
			const Vec3 bitangent = Math::Cross(axis, tangent);

			return axis * cosTheta +
				   (tangent * std::cos(phi) + bitangent * std::sin(phi)) * sinTheta;
		}
	}

	Appearance Evaluate(const ParticleEmitterComponent& emitter, float t,
						const Curve::Baked* sizeCurve,
						const Curve::Baked* colorGradient,
						const Curve::Baked* alphaCurve)
	{
		Appearance out;

		out.Size = sizeCurve
				 ? sizeCurve->SampleScalar(t)
				 : emitter.SizeStart + (emitter.SizeEnd - emitter.SizeStart) * t;

		out.Color = emitter.ColorStart + (emitter.ColorEnd - emitter.ColorStart) * t;

		// Channel by channel, so a gradient and an alpha curve compose instead
		// of one having to carry the other. The gradient's own fourth channel
		// is ignored on purpose: opacity has its own curve precisely so a
		// gradient can be shared between emitters that fade differently.
		if (colorGradient)
		{
			const Vec4 rgb = colorGradient->Sample(t);
			out.Color = Vec4(rgb.x, rgb.y, rgb.z, out.Color.a);
		}

		if (alphaCurve)
			out.Color.a = alphaCurve->SampleScalar(t);

		return out;
	}

	void System::Emit(ParticleEmitterComponent& emitter, const Mat4& world, int count)
	{
		if (count <= 0)
			return;

		if (emitter.Rng == 0)
			emitter.Rng = 0x9E3779B9u;

		const int capacity = Math::Clamp(emitter.MaxParticles, 1, 1 << 16);
		if ((int)emitter.Pool.size() >= capacity)
			return;

		count = Math::Min(count, capacity - (int)emitter.Pool.size());

		// The cone's axis in the space the particles will live in: local
		// particles keep the authored axis, world particles get it turned by
		// the emitter -- and positioned at it.
		const bool local = emitter.Space == ParticleSpace::Local;
		const Vec3 axisAuthored = Math::Dot(emitter.Direction, emitter.Direction) > 0.0f
								? Math::Normalize(emitter.Direction)
								: Vec3(0.0f, 1.0f, 0.0f);
		const Vec3 axis = local ? axisAuthored
								: Math::Normalize(Vec3(world * Vec4(axisAuthored, 0.0f)));
		const Vec3 origin = local ? Vec3(0.0f) : Vec3(world[3]);

		const float spread = Math::Radians(Math::Clamp(emitter.Spread, 0.0f, 180.0f));

		for (int i = 0; i < count; i++)
		{
			Particle particle;
			particle.Position = origin;
			particle.Velocity = RandomInCone(emitter.Rng, axis, spread)
							  * emitter.Speed
							  * (1.0f + RandomSigned(emitter.Rng) * emitter.SpeedJitter);
			particle.Lifetime = Math::Max(0.05f,
				emitter.Lifetime * (1.0f + RandomSigned(emitter.Rng) * emitter.LifetimeJitter));
			particle.Rotation = RandomFloat(emitter.Rng) * Math::TwoPi;
			particle.Spin = Math::Radians(emitter.Spin) * RandomSigned(emitter.Rng);

			emitter.Pool.push_back(particle);
		}
	}

	void System::Update(Scene& scene, float deltaSeconds)
	{
		// A hitch must not become a wall of particles: a two-second stall at
		// 60/s owes 120 spawns, and paying that debt in one step is a burst
		// nobody authored. Clamping the step forgives the debt instead.
		const float dt = Math::Clamp(deltaSeconds, 0.0f, 0.1f);
		if (dt <= 0.0f)
			return;

		scene.UpdateWorldTransforms();

		auto view = scene.GetRegistry().view<ParticleEmitterComponent, TransformComponent>();
		for (auto handle : view)
		{
			auto [emitter, transform] = view.get<ParticleEmitterComponent, TransformComponent>(handle);

			// The GPU path owns everything per-particle; the component only
			// carries the settings across.
			if (emitter.SimulateOnGpu)
				continue;

			// Age and integrate before emitting, so a particle born this
			// frame is drawn exactly where it was born.
			for (size_t i = 0; i < emitter.Pool.size();)
			{
				Particle& particle = emitter.Pool[i];
				particle.Age += dt;

				if (particle.Age >= particle.Lifetime)
				{
					// Order inside the pool means nothing -- the renderer
					// sorts by depth -- so the O(1) removal is free to use.
					particle = emitter.Pool.back();
					emitter.Pool.pop_back();
					continue;
				}

				particle.Velocity += emitter.Gravity * dt;
				particle.Velocity *= Math::Max(0.0f, 1.0f - emitter.Drag * dt);
				particle.Position += particle.Velocity * dt;
				particle.Rotation += particle.Spin * dt;
				++i;
			}

			// The burst is consumed whether or not emission is on: it is an
			// order, not a rate.
			if (emitter.Burst > 0)
			{
				Emit(emitter, transform.World, emitter.Burst);
				emitter.Burst = 0;
			}

			if (emitter.Emit && emitter.Rate > 0.0f)
			{
				emitter.EmitCarry += emitter.Rate * dt;
				const int whole = (int)emitter.EmitCarry;
				if (whole > 0)
				{
					emitter.EmitCarry -= (float)whole;
					Emit(emitter, transform.World, whole);
				}
			}
		}
	}

	bool System::HasWeightedEmitters(Scene& scene)
	{
		auto view = scene.GetRegistry().view<ParticleEmitterComponent>();
		for (auto handle : view)
		{
			if (view.get<ParticleEmitterComponent>(handle).Blend == ParticleBlend::WeightedBlended)
				return true;
		}
		return false;
	}

	uint32_t System::Count(Scene& scene)
	{
		uint32_t count = 0;
		auto view = scene.GetRegistry().view<ParticleEmitterComponent>();
		for (auto handle : view)
			count += (uint32_t)view.get<ParticleEmitterComponent>(handle).Pool.size();
		return count;
	}
}
