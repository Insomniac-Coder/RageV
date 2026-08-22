#include <rvpch.h>
#include "GpuCull.h"

#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "RageV/Renderer/Renderer3D.h"

namespace RageV
{
	using namespace RHI;

	namespace
	{
		// What the cull shader is told about the view it is culling for. The
		// six frustum planes are not here on purpose: a mat4 and six vec4s is
		// a hundred and sixty bytes and Vulkan guarantees a hundred and
		// twenty-eight of push constants, so the shader derives the planes
		// from the matrix instead.
		struct CullParams
		{
			Mat4     ViewProjection{ 1.0f };
			uint32_t ObjectCount = 0;
			uint32_t SlotCount = 0;
		};
		static_assert(sizeof(CullParams) == 72, "Must match Params in cull_casters.rvshader");

		// The lit variant's own block, because it carries two words the depth
		// cull has no use for -- and because CullParams is asserted against
		// cull_casters.rvshader, so growing it would move a layout that shader
		// reflects.
		struct LitCullParams
		{
			Mat4     ViewProjection{ 1.0f };
			uint32_t ObjectCount = 0;
			uint32_t SlotCount = 0;
			// Where this table's rows begin in the lit pass's instance buffer,
			// which the opaque and blended tables share. See CullLit.
			uint32_t InstanceBase = 0;
			uint32_t Pad0 = 0;
		};
		static_assert(sizeof(LitCullParams) == 80, "Must match Params in cull_lit.rvshader");

		struct ResetParams
		{
			uint32_t SlotCount = 0;
		};

		// One view's private pair of buffers, and the sets that bind them.
		//
		// Both live in device memory and neither is ever written by the CPU.
		// The commands especially: the cull pass increments one of their words
		// once per surviving object, and an atomic on host-visible memory
		// crosses the bus every time.
		struct ViewSlot
		{
			Ref<RHIBuffer>      Commands;
			uint32_t            CommandCapacity = 0;
			Ref<RHIBuffer>      Instances;
			uint32_t            InstanceCapacity = 0;
			Ref<RHIResourceSet> ResetSet;
			Ref<RHIResourceSet> CullSet;
		};

		struct GpuCullData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader>          ResetShader;
			Ref<RHIComputePipeline> ResetPipeline;
			Ref<RHIShader>          CullShader;
			Ref<RHIComputePipeline> CullPipeline;
			// The camera's variant: writes indices rather than matrices.
			Ref<RHIShader>          LitShader;
			Ref<RHIComputePipeline> LitPipeline;

			// One frame's table, shared by every view that culls it.
			// Host-visible: the CPU writes it once a frame and the GPU only
			// reads it.
			//
			// **A struct rather than loose fields**, because there are two of
			// them now -- opaque and blended -- and the alternative was a
			// second set of six members whose names differ by a prefix. Every
			// place that used to name `s_Data->Objects` names a table's, and
			// which table is an argument.
			struct Table
			{
				Ref<RHIBuffer> Objects;
				uint32_t       ObjectCapacity = 0;
				Ref<RHIBuffer> Template;
				uint32_t       TemplateCapacity = 0;

				uint32_t ObjectCount = 0;
				uint32_t SlotCount = 0;
				bool     HaveObjects = false;
				// Who built it this frame; null when nobody has.
				const void* Owner = nullptr;
			};

			Table Tables[(uint32_t)GpuCull::Pass::Count];

			// One ring per frame in flight: the previous frame's views may
			// still be reading theirs.
			std::vector<std::vector<ViewSlot>> Slots;
			uint32_t Cursor = 0;
			uint32_t ViewsThisFrame = 0;

			// The camera's own ring. Separate from the depth views' because
			// its survivor buffer holds four bytes an object rather than
			// sixty-four, and sizing one ring for the larger would waste the
			// difference on every depth view a frame opens.
			std::vector<std::vector<ViewSlot>> LitSlots;
			uint32_t LitCursor = 0;

			// The most objects one table may hold on *this* device, worked out
			// from its limits at startup rather than chosen. See ComputeCeiling.
			uint32_t MaxObjects = 0;
			bool CeilingReported = false;
		};

		std::unique_ptr<GpuCullData> s_Data;

		// How many objects this device can actually cull in one pass.
		//
		// **Nothing here is a number somebody picked.** Two device limits bound
		// it and the smaller wins:
		//
		//   - one storage buffer's addressable size, divided by whichever of
		//     the two tables has the larger element -- an Object is 96 bytes
		//     and an instance matrix is 64, so the objects decide it. A buffer
		//     past this limit can allocate and then be readable only up to it,
		//     which is silently wrong rather than a failure;
		//   - the largest group count one dispatch may ask for, times the
		//     shader's group size, since the cull is one invocation per object.
		//
		// On anything current this lands in the millions -- Vulkan guarantees
		// at least 128 MB of storage buffer and 65535 groups, so the floor is
		// about 1.4 million objects, and real devices are far above it. The
		// point is not that the ceiling is low; it is that it belongs to the
		// device and is said out loud when a scene reaches it, instead of the
		// tail of a big scene quietly not being drawn.
		uint32_t ComputeCeiling(const RHIDevice& device, uint32_t groupSize)
		{
			const DeviceCaps& caps = device.GetCaps();

			uint32_t ceiling = ~0u;

			if (caps.MaxStorageBufferBytes > 0)
			{
				const uint64_t byElement = caps.MaxStorageBufferBytes / sizeof(GpuCull::Object);
				ceiling = (uint32_t)Math::Min<uint64_t>(ceiling, byElement);
			}

			if (caps.MaxComputeWorkGroupCount > 0 && groupSize > 0)
			{
				const uint64_t byDispatch =
					(uint64_t)caps.MaxComputeWorkGroupCount * (uint64_t)groupSize;
				ceiling = (uint32_t)Math::Min<uint64_t>(ceiling, byDispatch);
			}

			// A device that reported neither limit told us nothing, and
			// guessing a ceiling would be exactly the invented number this
			// function exists to avoid. Unlimited, and the allocation failing
			// is then the honest answer.
			return ceiling;
		}

		// Separate from s_Data so that turning it off does not throw away the
		// pipelines, and turning it back on does not have to rebuild them.
		bool s_Enabled = true;
		bool s_LitEnabled = true;

		// Grows `buffer` to hold at least `count` elements of `stride`, in
		// powers of two so a scene that creeps upwards does not reallocate
		// every frame. A new buffer rather than a resize: the old one may
		// still be bound to a command buffer, and the deletion queue is what
		// makes releasing it safe.
		bool EnsureBuffer(Ref<RHIBuffer>& buffer, uint32_t& capacity, uint32_t count,
						  uint32_t stride, BufferUsage usage, MemoryDomain memory,
						  const char* name)
		{
			if (buffer && capacity >= count)
				return true;

			uint32_t target = capacity > 0 ? capacity : 64;
			while (target < count)
				target *= 2;

			BufferDesc desc;
			desc.Size = (uint64_t)target * stride;
			desc.Usage = usage;
			desc.Memory = memory;
			desc.DebugName = name;

			Ref<RHIBuffer> grown = s_Data->Device->CreateBuffer(desc);
			if (!grown)
				return false;

			buffer = grown;
			capacity = target;
			return true;
		}
	}

	void GpuCull::Init(RHIDevice& device)
	{
		Shutdown();

		if (!device.GetCaps().SupportsCompute)
		{
			RV_CORE_INFO("GPU culling: this device has no compute, so the depth passes walk "
						 "their casters on the CPU");
			return;
		}

		s_Data = std::make_unique<GpuCullData>();
		s_Data->Device = &device;

		ShaderCompiler::Init();

		if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/cull_reset.rvshader"))
		{
			s_Data->ResetShader = device.CreateShader(*compiled);
			if (s_Data->ResetShader)
			{
				ComputePipelineDesc desc;
				desc.Name = "GpuCull.reset";
				desc.Shader = s_Data->ResetShader;
				s_Data->ResetPipeline = device.CreateComputePipeline(desc);
			}
		}

		if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/cull_casters.rvshader"))
		{
			s_Data->CullShader = device.CreateShader(*compiled);
			if (s_Data->CullShader)
			{
				ComputePipelineDesc desc;
				desc.Name = "GpuCull.casters";
				desc.Shader = s_Data->CullShader;
				s_Data->CullPipeline = device.CreateComputePipeline(desc);
			}
		}

		if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/cull_lit.rvshader"))
		{
			s_Data->LitShader = device.CreateShader(*compiled);
			if (s_Data->LitShader)
			{
				ComputePipelineDesc desc;
				desc.Name = "GpuCull.lit";
				desc.Shader = s_Data->LitShader;
				s_Data->LitPipeline = device.CreateComputePipeline(desc);
			}
		}

		if (!s_Data->ResetPipeline || !s_Data->CullPipeline)
		{
			// Said once, and loudly enough to be found: the frame still draws
			// exactly what it drew before, but a scene that used to be limited
			// by its depth passes still is.
			RV_CORE_WARN("GPU culling: the cull passes did not build, so the depth passes walk "
						 "their casters on the CPU");
			s_Data.reset();
			return;
		}

		if (!s_Data->LitPipeline)
		{
			// The depth half still runs; only the camera falls back. Said
			// separately from the warning above, because they fail
			// independently and a reader needs to know which happened.
			RV_CORE_WARN("GPU culling: the camera's cull pass did not build, so the lit pass "
						 "walks and sorts its draws on the CPU");
		}

		s_Data->Slots.assign(device.GetFramesInFlight(), {});
		s_Data->LitSlots.assign(device.GetFramesInFlight(), {});
		s_Data->MaxObjects = ComputeCeiling(device, s_Data->CullPipeline->GetWorkGroupSizeX());

		RV_CORE_INFO("GPU culling available: depth views cull in compute, up to {0} objects "
					 "a scene on this device", s_Data->MaxObjects);
	}

	void GpuCull::Shutdown()
	{
		s_Data.reset();
	}

	void GpuCull::SetEnabled(bool enabled)
	{
		s_Enabled = enabled;
	}

	void GpuCull::SetLitEnabled(bool enabled)
	{
		s_LitEnabled = enabled;
	}

	bool GpuCull::IsAvailable()
	{
		return s_Enabled && s_Data && s_Data->CullPipeline && s_Data->ResetPipeline;
	}

	bool GpuCull::HasObjects(const void* owner, Pass pass)
	{
		if (!s_Data || pass >= Pass::Count)
			return false;

		const GpuCullData::Table& table = s_Data->Tables[(uint32_t)pass];
		return table.HaveObjects && table.Owner == owner;
	}

	uint32_t GpuCull::GetViewCount()
	{
		return s_Data ? s_Data->ViewsThisFrame : 0;
	}

	void GpuCull::BeginFrame()
	{
		if (!s_Data)
			return;

		s_Data->Cursor = 0;
		s_Data->LitCursor = 0;
		s_Data->ViewsThisFrame = 0;
		// Forgotten rather than kept: a frame that never calls SetObjects --
		// a probe capture, a frame before the first RenderShadows -- must not
		// cull against the last frame's table, which is where everything has
		// moved since.
		for (GpuCullData::Table& table : s_Data->Tables)
		{
			table.HaveObjects = false;
			table.Owner = nullptr;
			table.ObjectCount = 0;
			table.SlotCount = 0;
		}
	}

	bool GpuCull::SetObjects(const void* owner, const std::vector<Object>& objects,
							 const std::vector<SlotCommand>& slots, Pass pass)
	{
		if (!IsAvailable() || pass >= Pass::Count)
			return false;

		GpuCullData::Table& table = s_Data->Tables[(uint32_t)pass];

		table.HaveObjects = false;
		table.Owner = nullptr;

		if (objects.empty() || slots.empty())
			return false;

		const uint32_t objectCount = (uint32_t)objects.size();
		const uint32_t slotCount = (uint32_t)slots.size();

		// Refused rather than truncated. Culling the first million objects and
		// silently dropping the rest would be a scene missing its tail with
		// nothing to say why; taking the CPU walk is slower and complete.
		//
		// **The number of draws is not what is bounded here** -- that is one
		// per distinct mesh, and its buffer grows to fit like every other. What
		// is bounded is how many objects one dispatch can reach.
		if (objectCount > s_Data->MaxObjects)
		{
			if (!s_Data->CeilingReported)
			{
				RV_CORE_WARN("GPU culling: {0} objects is past this device's limit of {1}, so "
							 "the depth passes are walking their casters on the CPU instead. "
							 "The limit is the smaller of one storage buffer's addressable "
							 "size and one dispatch's group count.",
							 objectCount, s_Data->MaxObjects);
				s_Data->CeilingReported = true;
			}
			return false;
		}

		if (!EnsureBuffer(table.Objects, table.ObjectCapacity, objectCount,
						  sizeof(Object), BufferUsage::Storage, MemoryDomain::HostVisible,
						  "GpuCull.objects"))
		{
			return false;
		}

		if (!EnsureBuffer(table.Template, table.TemplateCapacity, slotCount,
						  sizeof(SlotCommand), BufferUsage::Storage, MemoryDomain::HostVisible,
						  "GpuCull.template"))
		{
			return false;
		}

		table.Objects->Upload(objects.data(), (uint64_t)objectCount * sizeof(Object));
		table.Template->Upload(slots.data(), (uint64_t)slotCount * sizeof(SlotCommand));

		table.ObjectCount = objectCount;
		table.SlotCount = slotCount;
		table.HaveObjects = true;
		table.Owner = owner;
		return true;
	}

	GpuCull::View GpuCull::CullLit(RHICommandList& cmd, const Mat4& viewProjection, Pass pass,
								   uint32_t instanceBase)
	{
		View view;

		if (!s_Data || pass >= Pass::Count)
			return view;

		GpuCullData::Table& table = s_Data->Tables[(uint32_t)pass];

		// **The lit path needs bindless materials, and this is not a
		// preference.**
		//
		// It draws one indirect call per mesh slot, so every instance in that
		// call is shaded by whatever set is bound -- which is only correct
		// when the material is *per instance*, and it is per instance only
		// under bindless, where the instance carries a record index and the
		// shader looks its textures up.
		//
		// On the bound path a material is a descriptor set that has to be
		// bound before its draw, and one draw cannot bind several. The result
		// is every static mesh shaded by the default material: colours survive
		// (they ride in the instance data) and textures do not. It showed on
		// OpenGL, which never has bindless, with a scene of white boxes and a
		// correctly textured fox -- the fox being skinned, and skinned meshes
		// taking the CPU path.
		//
		// The depth views are unaffected either way: a depth pass has no
		// material at all.
		if (!s_LitEnabled || !IsAvailable() || !table.HaveObjects || !s_Data->LitPipeline
			|| !Renderer3D::IsBindless())
		{
			return view;
		}

		const uint32_t frame = s_Data->Device->GetFrameIndex();
		auto& slots = s_Data->LitSlots[frame];
		while (s_Data->LitCursor >= slots.size())
			slots.push_back(ViewSlot{});

		ViewSlot& slot = slots[s_Data->LitCursor++];

		if (!EnsureBuffer(slot.Commands, slot.CommandCapacity, table.SlotCount,
						  sizeof(SlotCommand), BufferUsage::Storage | BufferUsage::Indirect,
						  MemoryDomain::DeviceLocal, "GpuCull.litCommands"))
		{
			return view;
		}

		// Four bytes an object, not sixty-four: this holds indices into the
		// lit pass's instance table rather than finished matrices. The slot
		// ranges together are exactly the object count, whatever survives.
		if (!EnsureBuffer(slot.Instances, slot.InstanceCapacity, table.ObjectCount,
						  sizeof(uint32_t), BufferUsage::Storage,
						  MemoryDomain::DeviceLocal, "GpuCull.litVisible"))
		{
			return view;
		}

		if (!slot.ResetSet)
			slot.ResetSet = s_Data->Device->CreateResourceSet(s_Data->ResetPipeline, 0);
		if (!slot.CullSet)
			slot.CullSet = s_Data->Device->CreateResourceSet(s_Data->LitPipeline, 0);
		if (!slot.ResetSet || !slot.CullSet)
			return view;

		slot.ResetSet->SetStorageBuffer(0, table.Template);
		slot.ResetSet->SetStorageBuffer(1, slot.Commands);
		slot.ResetSet->Commit();

		slot.CullSet->SetStorageBuffer(0, table.Objects);
		slot.CullSet->SetStorageBuffer(1, slot.Commands);
		slot.CullSet->SetStorageBuffer(2, slot.Instances);
		slot.CullSet->Commit();

		ResetParams reset;
		reset.SlotCount = table.SlotCount;

		cmd.BindComputePipeline(s_Data->ResetPipeline);
		cmd.BindResourceSet(0, slot.ResetSet);
		cmd.PushConstants(ShaderStage::Compute, 0, sizeof(reset), &reset);
		cmd.Dispatch(s_Data->ResetPipeline->GroupsFor(table.SlotCount));

		// Two, for the reason Cull gives: the pass reads each slot's base and
		// increments each slot's count, and BufferBarrier names one use a side.
		cmd.BufferBarrier(slot.Commands, BufferSync::ComputeWrite, BufferSync::ComputeWrite);
		cmd.BufferBarrier(slot.Commands, BufferSync::ComputeWrite, BufferSync::ComputeRead);

		LitCullParams params;
		params.ViewProjection = viewProjection;
		params.InstanceBase = instanceBase;
		params.ObjectCount = table.ObjectCount;
		params.SlotCount = table.SlotCount;

		cmd.BindComputePipeline(s_Data->LitPipeline);
		cmd.BindResourceSet(0, slot.CullSet);
		cmd.PushConstants(ShaderStage::Compute, 0, sizeof(params), &params);
		cmd.Dispatch(s_Data->LitPipeline->GroupsFor(table.ObjectCount));

		cmd.BufferBarrier(slot.Commands, BufferSync::ComputeWrite, BufferSync::IndirectRead);
		cmd.BufferBarrier(slot.Instances, BufferSync::ComputeWrite, BufferSync::ShaderRead);

		view.Commands = slot.Commands;
		view.Instances = slot.Instances;
		view.SlotCount = table.SlotCount;
		s_Data->ViewsThisFrame++;
		return view;
	}

	GpuCull::View GpuCull::Cull(RHICommandList& cmd, const Mat4& viewProjection)
	{
		View view;

		GpuCullData::Table& table = s_Data->Tables[(uint32_t)Pass::Opaque];

		// The depth passes only ever cull the opaque table. Blended geometry
		// casts no shadow here -- glass that shadows like a wall is worse than
		// glass that does not shadow at all, which is the same call every
		// renderer makes.
		if (!IsAvailable() || !table.HaveObjects)
			return view;

		const uint32_t frame = s_Data->Device->GetFrameIndex();
		auto& slots = s_Data->Slots[frame];
		while (s_Data->Cursor >= slots.size())
			slots.push_back(ViewSlot{});

		ViewSlot& slot = slots[s_Data->Cursor++];

		// The commands are read by the backend as indirect arguments *and*
		// written by the cull pass, so they carry both usages.
		if (!EnsureBuffer(slot.Commands, slot.CommandCapacity, table.SlotCount,
						  sizeof(SlotCommand), BufferUsage::Storage | BufferUsage::Indirect,
						  MemoryDomain::DeviceLocal, "GpuCull.commands"))
		{
			return view;
		}

		// Every slot's range is as long as the number of objects that named
		// it, so the ranges together are exactly the object count -- however
		// the culling goes. Sized to the whole scene rather than to what
		// survives, which is the price of not sorting.
		if (!EnsureBuffer(slot.Instances, slot.InstanceCapacity, table.ObjectCount,
						  sizeof(Mat4), BufferUsage::Storage, MemoryDomain::DeviceLocal,
						  "GpuCull.instances"))
		{
			return view;
		}

		if (!slot.ResetSet)
			slot.ResetSet = s_Data->Device->CreateResourceSet(s_Data->ResetPipeline, 0);
		if (!slot.CullSet)
			slot.CullSet = s_Data->Device->CreateResourceSet(s_Data->CullPipeline, 0);
		if (!slot.ResetSet || !slot.CullSet)
			return view;

		// Rebound every view rather than once: the shared buffers are
		// recreated when the scene outgrows them, and a set still pointing at
		// the old one reads a scene that has been freed.
		slot.ResetSet->SetStorageBuffer(0, table.Template);
		slot.ResetSet->SetStorageBuffer(1, slot.Commands);
		slot.ResetSet->Commit();

		slot.CullSet->SetStorageBuffer(0, table.Objects);
		slot.CullSet->SetStorageBuffer(1, slot.Commands);
		slot.CullSet->SetStorageBuffer(2, slot.Instances);
		slot.CullSet->Commit();

		ResetParams reset;
		reset.SlotCount = table.SlotCount;

		cmd.BindComputePipeline(s_Data->ResetPipeline);
		cmd.BindResourceSet(0, slot.ResetSet);
		cmd.PushConstants(ShaderStage::Compute, 0, sizeof(reset), &reset);
		cmd.Dispatch(s_Data->ResetPipeline->GroupsFor(table.SlotCount));

		// **Two barriers, because the cull does two things to this buffer.**
		// It reads each slot's instance base, which is the read-after-write;
		// and it increments each slot's count, which is a write after the
		// reset's write. BufferBarrier names one use on each side, so a single
		// call cannot express both, and the one that looks sufficient -- the
		// read -- is the one that is not.
		cmd.BufferBarrier(slot.Commands, BufferSync::ComputeWrite, BufferSync::ComputeWrite);
		cmd.BufferBarrier(slot.Commands, BufferSync::ComputeWrite, BufferSync::ComputeRead);

		CullParams params;
		params.ViewProjection = viewProjection;
		params.ObjectCount = table.ObjectCount;
		params.SlotCount = table.SlotCount;

		cmd.BindComputePipeline(s_Data->CullPipeline);
		cmd.BindResourceSet(0, slot.CullSet);
		cmd.PushConstants(ShaderStage::Compute, 0, sizeof(params), &params);
		cmd.Dispatch(s_Data->CullPipeline->GroupsFor(table.ObjectCount));

		cmd.BufferBarrier(slot.Commands, BufferSync::ComputeWrite, BufferSync::IndirectRead);
		cmd.BufferBarrier(slot.Instances, BufferSync::ComputeWrite, BufferSync::ShaderRead);

		view.Commands = slot.Commands;
		view.Instances = slot.Instances;
		view.SlotCount = table.SlotCount;
		s_Data->ViewsThisFrame++;
		return view;
	}
}
