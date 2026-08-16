#include <rvpch.h>
#include "RayShadows.h"
#include "Mesh.h"

namespace RageV
{
	using namespace RHI;

	namespace
	{
		struct RayShadowsData
		{
			RHIDevice* Device = nullptr;
			bool Available = false;

			// [frame in flight]
			std::vector<Ref<RHIAccelerationStructure>> Structures;
			uint32_t Capacity = 0;
			Ref<RHIAccelerationStructure> Empty;

			std::vector<AccelerationInstance> Instances;
			bool Active = false;
			bool BuiltThisFrame = false;
		};

		std::unique_ptr<RayShadowsData> s_Data;

		constexpr uint32_t kInitialCapacity = 256;

		// Grows the per-frame structures to hold at least `count` instances,
		// in powers of two, so a scene that creeps upwards does not reallocate
		// every frame. The old ones are released through the deletion queue.
		void EnsureCapacity(uint32_t count)
		{
			if (s_Data->Capacity >= count && !s_Data->Structures.empty())
				return;

			uint32_t target = s_Data->Capacity > 0 ? s_Data->Capacity : kInitialCapacity;
			while (target < count)
				target *= 2;

			const uint32_t frames = s_Data->Device->GetFramesInFlight();
			s_Data->Structures.assign(frames, nullptr);
			for (uint32_t i = 0; i < frames; i++)
				s_Data->Structures[i] = s_Data->Device->CreateTopLevelAS(target);
			s_Data->Capacity = target;
		}
	}

	void RayShadows::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<RayShadowsData>();
		s_Data->Device = &device;
		s_Data->Available = device.GetCaps().SupportsRayQuery;
		if (!s_Data->Available)
			return;

		EnsureCapacity(kInitialCapacity);

		// The stand-in: built once with nothing in it, so a set that must name
		// a structure on a frame that traced nothing has one to name, and
		// every ray into it misses -- "lit", which is what a frame with no
		// shadow information means everywhere else in this renderer.
		s_Data->Empty = device.CreateTopLevelAS(1);
		if (s_Data->Empty)
		{
			device.ExecuteImmediate([&](RHICommandList& cmd)
			{
				cmd.BuildTopLevelAS(s_Data->Empty, nullptr, 0);
			});
		}
		RV_CORE_INFO("Ray-traced shadows available: {0} instances per frame to start", kInitialCapacity);
	}

	void RayShadows::Shutdown()
	{
		s_Data.reset();
	}

	bool RayShadows::IsAvailable()
	{
		return s_Data && s_Data->Available;
	}

	void RayShadows::BeginFrame()
	{
		if (!s_Data)
			return;
		s_Data->Active = false;
		s_Data->BuiltThisFrame = false;
		s_Data->Instances.clear();
	}

	void RayShadows::ClearInstances()
	{
		if (s_Data)
			s_Data->Instances.clear();
	}

	void RayShadows::AddInstance(const Ref<Mesh>& mesh, const Mat4& world)
	{
		if (!s_Data || !s_Data->Available || !mesh)
			return;

		// A mesh the device could not build a structure for is left out
		// rather than failing the frame; the mesh says so once, at build.
		const Ref<RHIAccelerationStructure>& blas = mesh->GetAccelerationStructure(*s_Data->Device);
		if (!blas)
			return;

		AccelerationInstance instance;
		instance.Blas = blas;
		memcpy(instance.Transform, &world[0][0], sizeof(instance.Transform));
		instance.CustomIndex = (uint32_t)s_Data->Instances.size();
		s_Data->Instances.push_back(std::move(instance));
	}

	void RayShadows::Build(RHICommandList& cmd)
	{
		if (!s_Data || !s_Data->Available)
			return;
		if (s_Data->BuiltThisFrame)
			return;

		const uint32_t count = (uint32_t)s_Data->Instances.size();
		EnsureCapacity(Math::Max(count, 1u));

		const uint32_t frame = s_Data->Device->GetFrameIndex();
		if (frame >= s_Data->Structures.size() || !s_Data->Structures[frame])
			return;

		cmd.BuildTopLevelAS(s_Data->Structures[frame], s_Data->Instances.data(), count);
		s_Data->Active = true;
		s_Data->BuiltThisFrame = true;
	}

	bool RayShadows::IsActive()
	{
		return s_Data && s_Data->Active;
	}

	const Ref<RHIAccelerationStructure>& RayShadows::GetStructure()
	{
		static const Ref<RHIAccelerationStructure> none;
		if (!s_Data || !s_Data->Available)
			return none;
		if (s_Data->Active)
		{
			const uint32_t frame = s_Data->Device->GetFrameIndex();
			if (frame < s_Data->Structures.size() && s_Data->Structures[frame])
				return s_Data->Structures[frame];
		}
		return s_Data->Empty;
	}

	uint32_t RayShadows::GetInstanceCount()
	{
		return s_Data ? (uint32_t)s_Data->Instances.size() : 0u;
	}
}
