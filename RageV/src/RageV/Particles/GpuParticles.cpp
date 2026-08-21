#include <rvpch.h>
#include "GpuParticles.h"
#include "ParticleSystem.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "RageV/Asset/AssetManager.h"
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

			// The spawn box, as three half-axis vectors already turned into
			// the space this shader integrates in -- the answer, not the
			// ingredients, exactly as the ramps below are.
			//
			// Three vectors rather than a size and a matrix because that is
			// what makes the two paths provably the same: both ask
			// Particles::SpawnBoxAxes, so "does the box follow rotation but
			// not scale" is decided once, in C++, where it can be tested.
			// A point emitter's are zero and the shader adds them anyway.
			Vec4 BoxAxisX;
			Vec4 BoxAxisY;
			Vec4 BoxAxisZ;

			// The ramps, already resolved.
			//
			// The shader is handed the *answer*, not the ingredients: which of
			// the curve and the start/end pair decides each channel is worked
			// out once by Particles::Evaluate on the CPU, and only the result
			// crosses. That rule therefore has exactly one implementation, and
			// the shader has no copy of it to drift from -- which matters more
			// here than the 2 KB, because a drift between the CPU and GPU paths
			// reads as a simulation bug rather than a sampling one.
			//
			// Same 64 samples the CPU path reads, so switching an emitter
			// between them cannot change its appearance.
			Vec4 ColorRamp[Curve::Baked::kSize];
			// x only. Three floats per element are wasted and std140 would pad
			// a float array to the same size anyway, so packing would buy
			// nothing but an indexing bug.
			Vec4 SizeRamp[Curve::Baked::kSize];
		};
		static_assert(sizeof(EmitterParams) == 256 + 2 * 16 * Curve::Baked::kSize,
					  "Must match particle_sim.rvshader");

		// One emitter's residency. Kept while the emitter exists, whether or
		// not it is currently simulating on the GPU.
		// The most particles the sort can handle in one workgroup. Past this an
		// emitter draws unsorted, exactly as every GPU emitter did before --
		// see particle_sort.rvshader for why the limit is where it is.
		constexpr uint32_t kMaxSortable = 2048;

		// std140. The `Count` tail is padded out by the compiler; stating the
		// padding would only add a field nothing reads.
		struct SortParams
		{
			Vec4 CameraPosition;
			Vec4 CameraForward;
			uint32_t Count = 0;
			uint32_t Padding[3] = { 0, 0, 0 };
		};

		struct Resident
		{
			Ref<RHIBuffer> State;
			Ref<RHIBuffer> Instances;
			Ref<RHIBuffer> Params;
			Ref<RHIResourceSet> Set;

			// The sort's output, and its own set. A second instance buffer
			// rather than an index buffer the draw indirects through: it costs
			// 48 bytes per particle instead of 4, and buys a draw path that
			// does not change at all -- no extra binding in the shared vertex
			// stage, and nothing in the renderer able to tell a sorted emitter
			// from an unsorted one.
			Ref<RHIBuffer> Sorted;
			Ref<RHIBuffer> SortParams;
			Ref<RHIResourceSet> SortSet;

			// Whether *this frame's* dispatch sorted it. Read by GetInstances
			// to decide which buffer the draw is handed, and cleared every
			// frame -- an emitter switched from Alpha to Additive must stop
			// being handed a buffer nothing is filling.
			bool SortedThisFrame = false;

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

			// Null when the sort shader did not compile, or when the device
			// cannot run a 1024-thread workgroup. Everything else still works
			// in that case; alpha emitters simply draw in pool order, which is
			// what they did before this existed.
			Ref<RHIShader> SortShader;
			Ref<RHIComputePipeline> SortPipeline;

			std::unordered_map<UUID, Resident> Residents;
			bool Ready = false;

			// Said once per process, not once per frame.
			bool WarnedTooManyToSort = false;
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

			// Only for pools the sort can actually handle. A 16k emitter would
			// otherwise carry a 768 KB buffer nothing ever writes.
			if (s_Data->SortPipeline && capacity <= kMaxSortable)
			{
				BufferDesc sortedDesc = instanceDesc;
				sortedDesc.DebugName = "GpuParticles.sorted";
				resident.Sorted = s_Data->Device->CreateBuffer(sortedDesc);

				BufferDesc sortParamsDesc;
				sortParamsDesc.Size = sizeof(SortParams);
				sortParamsDesc.Usage = BufferUsage::Uniform;
				sortParamsDesc.Memory = MemoryDomain::HostVisible;
				sortParamsDesc.DebugName = "GpuParticles.sortparams";
				resident.SortParams = s_Data->Device->CreateBuffer(sortParamsDesc);

				// Bound and committed per frame, not here -- the same rule the
				// simulation's set follows, and for the same reason it
				// documents below.
				resident.SortSet = s_Data->Device->CreateResourceSet(s_Data->SortPipeline, 0);
			}
			else
			{
				resident.Sorted = nullptr;
				resident.SortParams = nullptr;
				resident.SortSet = nullptr;
			}

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

		// --- the sort, which is allowed to fail without taking the rest down --
		//
		// A device that cannot run a 1024-thread workgroup is unusual and not
		// impossible, and the answer to it is the behaviour that shipped for
		// the whole of 6.7b: alpha emitters draw in pool order. Refusing to
		// simulate at all because they cannot be *sorted* would be trading a
		// working feature for a missing one.
		const uint32_t maxGroup = device.GetCaps().MaxComputeWorkGroupSize;
		if (maxGroup < 1024)
		{
			RV_CORE_WARN("This device's compute workgroups top out at {0} threads; GPU "
						 "alpha emitters will draw unsorted", maxGroup);
			return;
		}

		auto sortCompiled = ShaderCompiler::CompileFromFile("assets/shaders/particle_sort.rvshader");
		if (!sortCompiled)
		{
			RV_CORE_ERROR("GpuParticles: failed to compile assets/shaders/particle_sort.rvshader; "
						  "GPU alpha emitters will draw unsorted");
			return;
		}

		s_Data->SortShader = device.CreateShader(*sortCompiled);
		if (!s_Data->SortShader)
			return;

		ComputePipelineDesc sortDesc;
		sortDesc.Name = "GpuParticles.sort";
		sortDesc.Shader = s_Data->SortShader;
		s_Data->SortPipeline = device.CreateComputePipeline(sortDesc);

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
		auto view = scene.GetRegistry().GetView<ParticleEmitterComponent>();
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

		// The sorted copy when this frame produced one. **The renderer cannot
		// tell**, which is the point of sorting into a second instance buffer
		// rather than into an index buffer the draw would indirect through:
		// there is no branch on the draw side, no extra binding in the shared
		// vertex stage, and no way for the two paths to drift apart.
		if (found->second.SortedThisFrame && found->second.Sorted)
			return found->second.Sorted;

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

		auto view = scene.GetRegistry().GetView<ParticleEmitterComponent, TransformComponent>();
		for (auto handle : view)
		{
			auto [emitter, transform] =
				view.Get<ParticleEmitterComponent, TransformComponent>(handle);

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
				Vec4(axis, Math::Cos(Math::Radians(Math::Clamp(emitter.Spread, 0.0f, 180.0f))));
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
			// z carries the motion, so a stationary emitter neither launches
			// its particles nor pulls on them afterwards. The CPU path reads
			// the same field off the component; this is the same decision
			// crossing the bus rather than a second one.
			params.SpaceScale = Vec4(local ? 1.0f : 0.0f, scale,
									 emitter.Motion == ParticleMotion::Stationary ? 1.0f : 0.0f,
									 0.0f);

			// The one function both paths ask, so the box the editor draws,
			// the box the CPU spawns in and the box the compute shader spawns
			// in are the same box by construction.
			Vec3 boxAxes[3];
			Particles::SpawnBoxAxes(emitter, transform.World, boxAxes);
			params.BoxAxisX = Vec4(boxAxes[0], 0.0f);
			params.BoxAxisY = Vec4(boxAxes[1], 0.0f);
			params.BoxAxisZ = Vec4(boxAxes[2], 0.0f);

			// Resolve the ramps here, once per emitter per frame, using the
			// same function the CPU renderer calls per particle. 64 samples of
			// a handful of flops each, against a pool of up to sixteen
			// thousand -- the cost is noise, and it buys the shader having no
			// opinion about which of a curve and a pair wins.
			const Curve::Baked* sizeCurve = RageV::Assets::Manager::GetBakedCurve(emitter.SizeCurve);
			const Curve::Baked* colorCurve = RageV::Assets::Manager::GetBakedCurve(emitter.ColorGradient);
			const Curve::Baked* alphaCurve = RageV::Assets::Manager::GetBakedCurve(emitter.AlphaCurve);

			for (uint32_t i = 0; i < Curve::Baked::kSize; i++)
			{
				// Inclusive of both ends, matching Curve::Bake -- i/(kSize-1),
				// never i/kSize, which would stop short of the last key and
				// clip the end off every ramp.
				const float t = (float)i / (float)(Curve::Baked::kSize - 1);
				const Appearance appearance =
					Evaluate(emitter, t, sizeCurve, colorCurve, alphaCurve);

				params.ColorRamp[i] = appearance.Color;
				params.SizeRamp[i] = Vec4(appearance.Size, 0.0f, 0.0f, 0.0f);
			}

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

			// Only alpha needs an order: additive blending is commutative, so
			// sorting it would be a dispatch and a 48-byte-per-particle copy
			// that changes no pixel.
			//
			// Recomputed every frame rather than remembered, so flipping the
			// blend mode in the inspector takes effect at once -- and so an
			// emitter that stops being sorted stops being handed a buffer
			// nothing is filling.
			resident.SortedThisFrame =
				emitter.Blend == ParticleBlend::Alpha && resident.SortSet != nullptr;

			if (resident.SortedThisFrame)
			{
				resident.SortSet->SetStorageBuffer(0, resident.Instances, 0,
												   (uint64_t)resident.Capacity * sizeof(InstanceData));
				resident.SortSet->SetStorageBuffer(1, resident.Sorted, 0,
												   (uint64_t)resident.Capacity * sizeof(InstanceData));
				resident.SortSet->SetUniformBuffer(2, resident.SortParams, 0, sizeof(SortParams));
				resident.SortSet->Commit();
			}
			else if (emitter.Blend == ParticleBlend::Alpha && s_Data->SortPipeline
					 && !s_Data->WarnedTooManyToSort)
			{
				// The pool is bigger than one workgroup can sort. Said once,
				// naming the way out, because at this scale weighted blending
				// is the better answer anyway.
				s_Data->WarnedTooManyToSort = true;
				RV_CORE_WARN("A GPU alpha emitter has {0} particles, past the {1} this "
							 "engine sorts in one pass; it will draw in pool order. Lower "
							 "MaxParticles, or use weighted blending, which does not need "
							 "an order at all.", resident.Capacity, kMaxSortable);
			}

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
		// dispatches wrote rather than the previous contents. Also what makes
		// Instances readable by the sort below: BufferSync names a *use*, and
		// ShaderRead covers a compute read and a vertex read alike.
		for (Resident* resident : ready)
		{
			cmd.BufferBarrier(resident->Instances, BufferSync::ComputeWrite,
							  BufferSync::ShaderRead);
		}

		// --- sort the alpha emitters, far to near ------------------------------
		//
		// The view this sorts against is the scene's **primary camera**, and
		// that is a real limitation worth stating rather than discovering.
		//
		// Sorting depends on where you are looking from, and this function runs
		// once per frame while the editor has two views. Sorting per view would
		// need a dispatch and a buffer per view; sorting once means the editor's
		// scene view sees an order computed for the game camera. A shipped game
		// has one view and is exact, which is what this feature is for -- and
		// the scene view was drawing in *pool* order until now, so it is not
		// worse off.
		//
		// With no camera in the scene there is nothing to sort against, and the
		// emitters keep their pool order.
		Entity camera = scene.GetPrimaryCameraEntity();
		if (!s_Data->SortPipeline || !camera || !camera.HasComponent<TransformComponent>())
			return;

		const Mat4& cameraWorld = camera.GetComponent<TransformComponent>().World;
		const Vec3 eye = Vec3(cameraWorld[3]);

		// The camera looks down its own -Z, the same convention the renderer
		// uses when it sorts emitters against each other. Taking +Z here would
		// sort every emitter exactly backwards, which looks like alpha being
		// broken rather than like a sign.
		const Vec3 forward = Math::Normalize(-Vec3(cameraWorld[2]));

		std::vector<Resident*> sorting;
		for (Resident* resident : ready)
		{
			if (resident->SortedThisFrame && resident->SortSet)
				sorting.push_back(resident);
		}

		if (sorting.empty())
			return;

		// The sim wrote Instances; the sort reads it. Bracketed for the same
		// reason the block above is -- a barrier per dispatch serialises them.
		for (Resident* resident : sorting)
		{
			cmd.BufferBarrier(resident->Sorted, BufferSync::ShaderRead,
							  BufferSync::ComputeWrite);
		}

		cmd.BindComputePipeline(s_Data->SortPipeline);

		for (Resident* resident : sorting)
		{
			SortParams params;
			params.CameraPosition = Vec4(eye, 0.0f);
			params.CameraForward = Vec4(forward, 0.0f);
			params.Count = resident->Capacity;
			resident->SortParams->Upload(&params, sizeof(params));

			cmd.BindResourceSet(0, resident->SortSet);

			// One group. The whole ladder runs in shared memory inside it --
			// see particle_sort.rvshader for why that is the shape.
			cmd.Dispatch(1);
		}

		for (Resident* resident : sorting)
		{
			cmd.BufferBarrier(resident->Sorted, BufferSync::ComputeWrite,
							  BufferSync::ShaderRead);
		}
	}
}
