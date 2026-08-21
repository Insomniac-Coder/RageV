#include <rvpch.h>
#include "GpuCull.h"

#include "RageV/Renderer/RHI/ShaderCompiler.h"

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

			// The frame's table, shared by every view. Host-visible: the CPU
			// writes them once a frame and the GPU only reads them.
			Ref<RHIBuffer> Objects;
			uint32_t       ObjectCapacity = 0;
			Ref<RHIBuffer> Template;
			uint32_t       TemplateCapacity = 0;

			uint32_t ObjectCount = 0;
			uint32_t SlotCount = 0;
			bool     HaveObjects = false;
			// Who built the table this frame; null when nobody has.
			const void* Owner = nullptr;

			// One ring per frame in flight: the previous frame's views may
			// still be reading theirs.
			std::vector<std::vector<ViewSlot>> Slots;
			uint32_t Cursor = 0;
			uint32_t ViewsThisFrame = 0;
		};

		std::unique_ptr<GpuCullData> s_Data;

		// Separate from s_Data so that turning it off does not throw away the
		// pipelines, and turning it back on does not have to rebuild them.
		bool s_Enabled = true;

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

		s_Data->Slots.assign(device.GetFramesInFlight(), {});
		RV_CORE_INFO("GPU culling available: depth views cull in compute");
	}

	void GpuCull::Shutdown()
	{
		s_Data.reset();
	}

	void GpuCull::SetEnabled(bool enabled)
	{
		s_Enabled = enabled;
	}

	bool GpuCull::IsAvailable()
	{
		return s_Enabled && s_Data && s_Data->CullPipeline && s_Data->ResetPipeline;
	}

	bool GpuCull::HasObjects(const void* owner)
	{
		return s_Data && s_Data->HaveObjects && s_Data->Owner == owner;
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
		s_Data->ViewsThisFrame = 0;
		// Forgotten rather than kept: a frame that never calls SetObjects --
		// a probe capture, a frame before the first RenderShadows -- must not
		// cull against the last frame's table, which is where everything has
		// moved since.
		s_Data->HaveObjects = false;
		s_Data->Owner = nullptr;
		s_Data->ObjectCount = 0;
		s_Data->SlotCount = 0;
	}

	bool GpuCull::SetObjects(const void* owner, const std::vector<Object>& objects,
							 const std::vector<SlotCommand>& slots)
	{
		if (!IsAvailable())
			return false;

		if (s_Data->HaveObjects && s_Data->Owner == owner)
			return true;

		s_Data->HaveObjects = false;
		s_Data->Owner = nullptr;

		if (objects.empty() || slots.empty())
			return false;

		const uint32_t objectCount = (uint32_t)objects.size();
		const uint32_t slotCount = (uint32_t)slots.size();

		if (!EnsureBuffer(s_Data->Objects, s_Data->ObjectCapacity, objectCount,
						  sizeof(Object), BufferUsage::Storage, MemoryDomain::HostVisible,
						  "GpuCull.objects"))
		{
			return false;
		}

		if (!EnsureBuffer(s_Data->Template, s_Data->TemplateCapacity, slotCount,
						  sizeof(SlotCommand), BufferUsage::Storage, MemoryDomain::HostVisible,
						  "GpuCull.template"))
		{
			return false;
		}

		s_Data->Objects->Upload(objects.data(), (uint64_t)objectCount * sizeof(Object));
		s_Data->Template->Upload(slots.data(), (uint64_t)slotCount * sizeof(SlotCommand));

		s_Data->ObjectCount = objectCount;
		s_Data->SlotCount = slotCount;
		s_Data->HaveObjects = true;
		s_Data->Owner = owner;
		return true;
	}

	GpuCull::View GpuCull::Cull(RHICommandList& cmd, const Mat4& viewProjection)
	{
		View view;

		if (!IsAvailable() || !s_Data->HaveObjects)
			return view;

		const uint32_t frame = s_Data->Device->GetFrameIndex();
		auto& slots = s_Data->Slots[frame];
		while (s_Data->Cursor >= slots.size())
			slots.push_back(ViewSlot{});

		ViewSlot& slot = slots[s_Data->Cursor++];

		// The commands are read by the backend as indirect arguments *and*
		// written by the cull pass, so they carry both usages.
		if (!EnsureBuffer(slot.Commands, slot.CommandCapacity, s_Data->SlotCount,
						  sizeof(SlotCommand), BufferUsage::Storage | BufferUsage::Indirect,
						  MemoryDomain::DeviceLocal, "GpuCull.commands"))
		{
			return view;
		}

		// Every slot's range is as long as the number of objects that named
		// it, so the ranges together are exactly the object count -- however
		// the culling goes. Sized to the whole scene rather than to what
		// survives, which is the price of not sorting.
		if (!EnsureBuffer(slot.Instances, slot.InstanceCapacity, s_Data->ObjectCount,
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
		slot.ResetSet->SetStorageBuffer(0, s_Data->Template);
		slot.ResetSet->SetStorageBuffer(1, slot.Commands);
		slot.ResetSet->Commit();

		slot.CullSet->SetStorageBuffer(0, s_Data->Objects);
		slot.CullSet->SetStorageBuffer(1, slot.Commands);
		slot.CullSet->SetStorageBuffer(2, slot.Instances);
		slot.CullSet->Commit();

		ResetParams reset;
		reset.SlotCount = s_Data->SlotCount;

		cmd.BindComputePipeline(s_Data->ResetPipeline);
		cmd.BindResourceSet(0, slot.ResetSet);
		cmd.PushConstants(ShaderStage::Compute, 0, sizeof(reset), &reset);
		cmd.Dispatch(s_Data->ResetPipeline->GroupsFor(s_Data->SlotCount));

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
		params.ObjectCount = s_Data->ObjectCount;
		params.SlotCount = s_Data->SlotCount;

		cmd.BindComputePipeline(s_Data->CullPipeline);
		cmd.BindResourceSet(0, slot.CullSet);
		cmd.PushConstants(ShaderStage::Compute, 0, sizeof(params), &params);
		cmd.Dispatch(s_Data->CullPipeline->GroupsFor(s_Data->ObjectCount));

		cmd.BufferBarrier(slot.Commands, BufferSync::ComputeWrite, BufferSync::IndirectRead);
		cmd.BufferBarrier(slot.Instances, BufferSync::ComputeWrite, BufferSync::ShaderRead);

		view.Commands = slot.Commands;
		view.Instances = slot.Instances;
		view.SlotCount = s_Data->SlotCount;
		s_Data->ViewsThisFrame++;
		return view;
	}
}
