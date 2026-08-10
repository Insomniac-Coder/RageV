#include <rvpch.h>
#include "GpuParticles.h"
#include "ParticleSystem.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include <unordered_map>
#include <unordered_set>

namespace RageV::Particles
{
	using namespace RageV::RHI;

	namespace
	{
		// Mirrors the shader's Particle struct, and only exists so the CPU can
		// seed a pool when an emitter switches over.
		struct GpuParticle
		{
			Vec4 PositionAge;
			Vec4 VelocityLifetime;
			Vec4 Spin;
		};
		static_assert(sizeof(GpuParticle) == 48, "Must match particle_sim.rvshader");

		// The instance layout the draw reads. Deliberately the same bytes
		// ParticleRenderer builds on the CPU: one vertex shader serves both.
		struct InstanceData
		{
			Vec4 PositionSize;
			Vec4 Color;
			Vec4 Params;
		};
		static_assert(sizeof(InstanceData) == 48, "Must match particle.rvshader");

		// std140. Every member is a vec4 because std140 rounds everything up to
		// one anyway, so packing four floats into each is free rather than
		// clever.
		struct EmitterParams
		{
			Vec4 GravityDrag;
			Vec4 OriginDelta;
			Vec4 AxisCosSpread;
			Vec4 ColorStart;
			Vec4 ColorEnd;
			Vec4 SpeedLife;
			Vec4 SizeSpin;
			uint32_t PoolSize = 0;
			uint32_t SpawnCursor = 0;
			uint32_t SpawnCount = 0;
			uint32_t Epoch = 0;
			Mat4 Model;
			Vec4 SpaceScale;
		};
		static_assert(sizeof(EmitterParams) == 208, "Must match particle_sim.rvshader");

		// One emitter's residency. Kept while the emitter exists, whether or
		// not it is currently simulating on the GPU.
		struct Resident
		{
			Ref<RHIBuffer> State;
			Ref<RHIBuffer> Instances;
			Ref<RHIBuffer> Params;
			Ref<RHIResourceSet> Set;

			uint32_t Capacity = 0;      // particles the buffers hold
			uint32_t SpawnCursor = 0;
			uint32_t Epoch = 0;
			float EmitCarry = 0.0f;

			// Set when the pool has never been simulated, or when the emitter
			// has just come back from the CPU path -- which is what makes the
			// first GPU frame start from a known state rather than whatever
			// the buffers held last time.
			bool NeedsSeed = true;
		};

		struct GpuData
		{
			RHIDevice* Device = nullptr;
			Ref<RHIShader> Shader;
			Ref<RHIComputePipeline> Pipeline;
			std::unordered_map<UUID, Resident> Residents;
			bool Ready = false;
		};

		std::unique_ptr<GpuData> s_Data;

		uint32_t ClampCapacity(int requested)
		{
			return (uint32_t)Math::Clamp(requested, 1, 16384);
		}

		// Builds an emitter's buffers, or rebuilds them when its pool size
		// changed. Everything else about a toggle avoids this path.
		Resident& Acquire(UUID id, const ParticleEmitterComponent& emitter)
		{
			Resident& resident = s_Data->Residents[id];

			const uint32_t capacity = ClampCapacity(emitter.MaxParticles);
			if (resident.Capacity == capacity && resident.State)
				return resident;

			BufferDesc stateDesc;
			stateDesc.Size = (uint64_t)capacity * sizeof(GpuParticle);
			stateDesc.Usage = BufferUsage::Storage;
			// Device local, though only the seed path ever writes it from the
			// CPU and a mapped buffer would make that a memcpy instead of a
			// staging copy.
			//
			// The trade runs the other way: this buffer is written by compute
			// every frame, and a persistently mapped coherent buffer being
			// written by a shader is a synchronisation point on OpenGL. It
			// cost more than it saved -- the GPU path measured *slower* than
			// the CPU one it replaced until this changed. Seeding happens
			// when somebody flips a checkbox; the simulation happens sixty
			// times a second.
			stateDesc.Memory = MemoryDomain::DeviceLocal;
			stateDesc.DebugName = "GpuParticles.state";
			resident.State = s_Data->Device->CreateBuffer(stateDesc);

			BufferDesc instanceDesc;
			instanceDesc.Size = (uint64_t)capacity * sizeof(InstanceData);
			instanceDesc.Usage = BufferUsage::Storage;
			instanceDesc.Memory = MemoryDomain::DeviceLocal;
			instanceDesc.DebugName = "GpuParticles.instances";
			resident.Instances = s_Data->Device->CreateBuffer(instanceDesc);

			BufferDesc paramsDesc;
			paramsDesc.Size = sizeof(EmitterParams);
			paramsDesc.Usage = BufferUsage::Uniform;
			paramsDesc.Memory = MemoryDomain::HostVisible;
			paramsDesc.DebugName = "GpuParticles.params";
			resident.Params = s_Data->Device->CreateBuffer(paramsDesc);

			resident.Set = s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0);

			resident.Capacity = capacity;
			resident.SpawnCursor = 0;
			resident.NeedsSeed = true;
			return resident;
		}

		// Writes the pool the emitter is carrying on the CPU into the GPU's
		// state buffer, so switching over keeps the particles that are already
		// in the air.
		//
		// The other direction has no equivalent: reading the state back would
		// mean a stall, and a stall is exactly what the switch is supposed not
		// to cost. Going GPU -> CPU drops what was in flight and refills.
		void Seed(Resident& resident, const ParticleEmitterComponent& emitter)
		{
			const uint32_t live = Math::Min((uint32_t)emitter.Pool.size(), resident.Capacity);

			// Built here and uploaded once. A lifetime of zero is what the
			// shader reads as a free slot, so the tail is simply zeroed.
			std::vector<GpuParticle> pool(resident.Capacity);

			for (uint32_t i = 0; i < live; i++)
			{
				const Particle& particle = emitter.Pool[i];
				pool[i].PositionAge = Vec4(particle.Position, particle.Age);
				pool[i].VelocityLifetime = Vec4(particle.Velocity, particle.Lifetime);
				pool[i].Spin = Vec4(particle.Rotation, particle.Spin, 0.0f, 0.0f);
			}

			resident.State->Upload(pool.data(), pool.size() * sizeof(GpuParticle));

			resident.SpawnCursor = live % resident.Capacity;
			resident.NeedsSeed = false;
		}
	}

	void Gpu::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<GpuData>();
		s_Data->Device = &device;

		if (!device.GetCaps().SupportsCompute)
		{
			RV_CORE_WARN("No compute support; particle emitters will simulate on the CPU");
			return;
		}

		ShaderCompiler::Init();
		auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/particle_sim.rvshader");
		if (!compiled)
		{
			RV_CORE_ERROR("GpuParticles: failed to compile assets/shaders/particle_sim.rvshader");
			return;
		}

		s_Data->Shader = device.CreateShader(*compiled);
		if (!s_Data->Shader)
			return;

		ComputePipelineDesc desc;
		desc.Name = "GpuParticles.simulate";
		desc.Shader = s_Data->Shader;

		// Once, at init. An emitter toggling to the GPU must not pay for a
		// pipeline -- that is most of what would make the switch perceptible.
		s_Data->Pipeline = device.CreateComputePipeline(desc);
		s_Data->Ready = s_Data->Pipeline != nullptr;

		if (s_Data->Ready)
			RV_CORE_INFO("GPU particle simulation ready ({0} per group)",
						 s_Data->Pipeline->GetWorkGroupSizeX());
	}

	void Gpu::Shutdown()
	{
		s_Data.reset();
	}

	bool Gpu::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	uint32_t Gpu::GetResidentCount()
	{
		return s_Data ? (uint32_t)s_Data->Residents.size() : 0;
	}

	void Gpu::Clear()
	{
		if (s_Data)
			s_Data->Residents.clear();
	}

	void Gpu::Collect(Scene& scene)
	{
		if (!s_Data)
			return;

		std::unordered_set<UUID> present;
		auto view = scene.GetRegistry().view<ParticleEmitterComponent>();
		for (auto handle : view)
			present.insert(Entity{ handle, &scene }.GetUUID());

		for (auto it = s_Data->Residents.begin(); it != s_Data->Residents.end();)
			it = present.count(it->first) ? std::next(it) : s_Data->Residents.erase(it);
	}

	RHI::Ref<RHIBuffer> Gpu::GetInstances(UUID emitter, uint32_t& outCount)
	{
		outCount = 0;
		if (!s_Data)
			return nullptr;

		const auto found = s_Data->Residents.find(emitter);
		if (found == s_Data->Residents.end() || !found->second.Instances)
			return nullptr;

		outCount = found->second.Capacity;
		return found->second.Instances;
	}

	void Gpu::Simulate(Scene& scene, RHICommandList& cmd, float deltaSeconds)
	{
		if (!IsReady())
			return;

		// The same clamp the CPU simulation applies: a hitch must not be paid
		// back as a wall of particles nobody authored.
		const float dt = Math::Clamp(deltaSeconds, 0.0f, 0.1f);
		if (dt <= 0.0f)
			return;

		scene.UpdateWorldTransforms();

		// Collected first, dispatched together. See the barrier note below.
		std::vector<Resident*> ready;

		auto view = scene.GetRegistry().view<ParticleEmitterComponent, TransformComponent>();
		for (auto handle : view)
		{
			auto [emitter, transform] =
				view.get<ParticleEmitterComponent, TransformComponent>(handle);

			Entity entity{ handle, &scene };

			if (!emitter.SimulateOnGpu)
			{
				// Still on the CPU. Its buffers stay allocated -- that is what
				// makes coming back cost nothing -- but the state in them is
				// going stale while the CPU pool runs, so the next GPU frame
				// re-seeds from that pool rather than resuming a frozen one.
				const auto resident = s_Data->Residents.find(entity.GetUUID());
				if (resident != s_Data->Residents.end())
					resident->second.NeedsSeed = true;
				continue;
			}

			Resident& resident = Acquire(entity.GetUUID(), emitter);
			if (!resident.Set || !resident.State)
				continue;

			// First frame on the GPU, or the first after coming back from the
			// CPU: take over the particles that are already in the air.
			if (resident.NeedsSeed)
				Seed(resident, emitter);

			// How many to spawn this frame, carried the same way the CPU path
			// carries it so a fractional rate does not round to nothing.
			uint32_t spawn = 0;
			if (emitter.Emit && emitter.Rate > 0.0f)
			{
				resident.EmitCarry += emitter.Rate * dt;
				const uint32_t whole = (uint32_t)resident.EmitCarry;
				resident.EmitCarry -= (float)whole;
				spawn = whole;
			}

			if (emitter.Burst > 0)
			{
				spawn += (uint32_t)emitter.Burst;
				emitter.Burst = 0;
			}

			spawn = Math::Min(spawn, resident.Capacity);

			const bool local = emitter.Space == ParticleSpace::Local;
			const Vec3 authoredAxis =
				Math::Dot(emitter.Direction, emitter.Direction) > 0.0f
					? Math::Normalize(emitter.Direction)
					: Vec3(0.0f, 1.0f, 0.0f);

			// A local emitter simulates around its own origin along its own
			// axis; the transform is applied when the instance is written, so
			// the particles ride the emitter rather than being dragged by it.
			const Vec3 axis = local
				? authoredAxis
				: Math::Normalize(Vec3(transform.World * Vec4(authoredAxis, 0.0f)));
			const Vec3 origin = local ? Vec3(0.0f) : Vec3(transform.World[3]);

			// The longest basis axis, which is what a non-uniform scale does
			// to a round thing -- the same answer the CPU renderer takes.
			const float scale = local
				? Math::Max(Math::Max(Math::Length(Vec3(transform.World[0])),
									  Math::Length(Vec3(transform.World[1]))),
							Math::Length(Vec3(transform.World[2])))
				: 1.0f;

			EmitterParams params;
			params.GravityDrag = Vec4(emitter.Gravity, emitter.Drag);
			params.OriginDelta = Vec4(origin, dt);
			params.AxisCosSpread =
				Vec4(axis, std::cos(Math::Radians(Math::Clamp(emitter.Spread, 0.0f, 180.0f))));
			params.ColorStart = emitter.ColorStart;
			params.ColorEnd = emitter.ColorEnd;
			params.SpeedLife = Vec4(emitter.Speed, emitter.SpeedJitter,
									emitter.Lifetime, emitter.LifetimeJitter);
			params.SizeSpin = Vec4(emitter.SizeStart, emitter.SizeEnd, emitter.Spin,
								   emitter.Blend == ParticleBlend::Additive ? 1.0f : 0.0f);
			params.PoolSize = resident.Capacity;
			params.SpawnCursor = resident.SpawnCursor;
			params.SpawnCount = spawn;
			params.Epoch = resident.Epoch;
			params.Model = transform.World;
			params.SpaceScale = Vec4(local ? 1.0f : 0.0f, scale, 0.0f, 0.0f);

			resident.Params->Upload(&params, sizeof(params));

			// Rebound every frame, not once at creation. A resource set holds
			// one descriptor set per frame in flight and Commit writes only
			// the current frame's -- so committing once populates one of them
			// and leaves the rest never written, which validation catches on
			// the second frame and a release build renders as garbage.
			resident.Set->SetStorageBuffer(0, resident.State, 0,
										   (uint64_t)resident.Capacity * sizeof(GpuParticle));
			resident.Set->SetStorageBuffer(1, resident.Instances, 0,
										   (uint64_t)resident.Capacity * sizeof(InstanceData));
			resident.Set->SetUniformBuffer(2, resident.Params, 0, sizeof(EmitterParams));
			resident.Set->Commit();

			resident.SpawnCursor = (resident.SpawnCursor + spawn) % resident.Capacity;
			resident.Epoch++;
			ready.push_back(&resident);
		}

		if (ready.empty())
			return;

		// Barriers bracket the whole batch rather than each dispatch.
		//
		// Interleaved -- barrier, dispatch, barrier, barrier, dispatch --
		// every emitter waits for the one before it, because a barrier orders
		// everything recorded around it and not just the buffer it names. On
		// OpenGL, where the barrier is a global pipeline flush whatever
		// buffer is passed, that turned three emitters into three serialised
		// flushes and made the GPU path slower than the CPU one it replaced.
		//
		// Bracketed, the dispatches are free to overlap, which is most of the
		// point of moving them to the GPU at all.
		for (Resident* resident : ready)
		{
			cmd.BufferBarrier(resident->Instances, BufferSync::ShaderRead,
							  BufferSync::ComputeWrite);
		}

		cmd.BindComputePipeline(s_Data->Pipeline);

		for (Resident* resident : ready)
		{
			cmd.BindResourceSet(0, resident->Set);
			cmd.Dispatch(s_Data->Pipeline->GroupsFor(resident->Capacity));
		}

		// And the other direction, so this frame's draw sees what the
		// dispatches wrote rather than the previous contents.
		for (Resident* resident : ready)
		{
			cmd.BufferBarrier(resident->Instances, BufferSync::ComputeWrite,
							  BufferSync::ShaderRead);
		}
	}
}
