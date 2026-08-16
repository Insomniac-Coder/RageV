#pragma once

#include "VulkanCommon.h"
#include "RageV/Renderer/RHI/RHIPipeline.h"
#include "RageV/Renderer/RHI/RHIResourceSet.h"
#include "VulkanResources.h"

namespace RageV::Vk
{
	class VulkanDevice;

	class VulkanShader final : public RHI::RHIShader
	{
	public:
		VulkanShader(VulkanDevice& device, const RHI::CompiledShader& compiled);
		~VulkanShader() override;

		struct StageModule
		{
			VkShaderModule        Module = VK_NULL_HANDLE;
			VkShaderStageFlagBits Bits   = VK_SHADER_STAGE_VERTEX_BIT;
			std::string           EntryPoint;
		};

		const std::vector<StageModule>& GetStages() const { return m_Stages; }

	private:
		VulkanDevice&            m_Device;
		std::shared_ptr<DeletionQueue> m_Deletion;
		std::vector<StageModule> m_Stages;
	};

	// The half of a pipeline that has nothing to do with which kind it is.
	//
	// Descriptor set layouts and the pipeline layout come from reflection and
	// only from reflection, so graphics and compute build them by the same
	// code. What differs is one call -- vkCreateGraphicsPipelines against
	// vkCreateComputePipelines -- and the bind point that follows from it.
	//
	// This exists so the command list and the resource set can hold one type.
	// Neither of them cares what kind of pipeline it has: one needs a layout
	// to bind against, the other a set layout to allocate from.
	class VulkanPipelineCommon
	{
	public:
		virtual ~VulkanPipelineCommon();

		VkPipeline          GetHandle() const { return m_Pipeline; }
		VkPipelineLayout    GetLayout() const { return m_Layout; }
		VkPipelineBindPoint GetBindPoint() const { return m_BindPoint; }

		VkDescriptorSetLayout GetSetLayout(uint32_t set) const;

		// Push-constant ranges live here so the command list can widen a
		// stage mask to what the shader declared without knowing the kind.
		const RHI::ShaderReflection& GetCommonReflection() const { return *m_Reflection; }

		// For diagnostics: the same name the VkPipeline carries in the
		// validation layer, so an engine-side message and a layer message
		// about one pipeline read as one story.
		const std::string& GetDebugName() const { return m_DebugName; }

	protected:
		VulkanPipelineCommon(VulkanDevice& device, VkPipelineBindPoint bindPoint,
							 const RHI::ShaderReflection& reflection);

		void CreateLayouts();

		VulkanDevice&    m_Device;
		std::shared_ptr<DeletionQueue> m_Deletion;
		const RHI::ShaderReflection*   m_Reflection = nullptr;
		VkPipelineBindPoint m_BindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		VkPipeline       m_Pipeline = VK_NULL_HANDLE;
		VkPipelineLayout m_Layout   = VK_NULL_HANDLE;
		std::string      m_DebugName;
		// Indexed by set number; gaps are VK_NULL_HANDLE.
		std::vector<VkDescriptorSetLayout> m_SetLayouts;
		// Parallel to m_SetLayouts: true where the layout is the device's
		// shared bindless one, which this pipeline uses but does not own and
		// must not destroy (ENGINE-NOTES 7al).
		std::vector<bool> m_BorrowedLayouts;
	};

	// What the command list needs from any Vulkan resource set: the
	// descriptor set to bind right now. Two kinds exist -- the per-frame set
	// below and the bindless heap -- and BindResourceSet must not care which.
	class VulkanSetBase : public RHI::RHIResourceSet
	{
	public:
		virtual VkDescriptorSet GetHandle() const = 0;

	protected:
		explicit VulkanSetBase(uint32_t set) : RHI::RHIResourceSet(set) {}
	};

	class VulkanPipeline final : public RHI::RHIPipeline, public VulkanPipelineCommon
	{
	public:
		VulkanPipeline(VulkanDevice& device, const RHI::GraphicsPipelineDesc& desc);

	private:
		void CreatePipeline();
	};

	class VulkanComputePipeline final : public RHI::RHIComputePipeline, public VulkanPipelineCommon
	{
	public:
		VulkanComputePipeline(VulkanDevice& device, const RHI::ComputePipelineDesc& desc);
	};

	// One descriptor set per frame in flight. Commit() writes only into the
	// current frame's set, so an update never races the GPU reading the set a
	// previous frame bound.
	class VulkanResourceSet final : public VulkanSetBase
	{
	public:
		// Takes the pipeline as the *common* half plus an owning reference:
		// a set is allocated from a descriptor set layout and must not outlive
		// it, but nothing here cares whether that layout came from a graphics
		// pipeline or a compute one.
		VulkanResourceSet(VulkanDevice& device, VulkanPipelineCommon* pipeline,
						  std::shared_ptr<void> owner, uint32_t set);
		~VulkanResourceSet() override;

		void SetUniformBuffer(uint32_t binding, const RHI::Ref<RHI::RHIBuffer>& buffer,
							  uint64_t offset = 0, uint64_t range = 0) override;
		void SetStorageBuffer(uint32_t binding, const RHI::Ref<RHI::RHIBuffer>& buffer,
							  uint64_t offset = 0, uint64_t range = 0) override;
		void SetTexture(uint32_t binding, const RHI::Ref<RHI::RHITexture>& texture,
						const RHI::Ref<RHI::RHISampler>& sampler, uint32_t arrayIndex = 0) override;
		void Commit() override;

		VkDescriptorSet GetHandle() const override;

	private:
		struct BufferWrite
		{
			uint32_t Binding;
			// Carried per write rather than assumed: uniform and storage
			// buffers share this path and the descriptor type has to match the
			// layout the shader declared, or the write is rejected.
			VkDescriptorType Type;
			VkDescriptorBufferInfo Info;
		};
		struct ImageWrite
		{
			uint32_t Binding;
			uint32_t ArrayIndex;
			VkDescriptorImageInfo Info;
		};

		VulkanDevice&           m_Device;
		std::shared_ptr<DeletionQueue> m_Deletion;
		VulkanPipelineCommon*   m_Pipeline = nullptr;
		// Keeps whichever pipeline object m_Pipeline points into alive, which
		// is what keeps the set layout these sets were allocated from alive.
		std::shared_ptr<void>   m_Owner;
		std::vector<VkDescriptorSet> m_Sets;   // one per frame in flight
		// The block these came from. The device allocates from a chain, so the
		// owning pool is not necessarily the one it would hand out today, and
		// vkFreeDescriptorSets must be given the one that owns them.
		VkDescriptorPool m_Pool = VK_NULL_HANDLE;

		std::vector<BufferWrite> m_PendingBuffers;
		std::vector<ImageWrite>  m_PendingImages;
		// A set is only valid once written; track which frames still need the
		// full write set so newly created sets are populated on first use.
		std::vector<bool> m_Dirty;
		std::vector<BufferWrite> m_LastBuffers;
		std::vector<ImageWrite>  m_LastImages;
	};

	// The bindless texture heap (ENGINE-NOTES 7al): one descriptor set, not
	// one per frame, allocated from the device's shared bindless layout with
	// exactly `capacity` combined image samplers.
	//
	// Commit writes land immediately, while the set may be bound to a command
	// buffer the GPU has not finished -- that is what UPDATE_AFTER_BIND
	// permits, and it is why this is not a VulkanResourceSet: the per-frame
	// copies, the dirty tracking and the rewrite-after-bind tripwire are all
	// machinery for sets that *cannot* be written while bound. The permission
	// has one condition, and the caller owns it: a slot a pending frame reads
	// must not be rewritten until that frame has completed.
	class VulkanBindlessSet final : public VulkanSetBase
	{
	public:
		VulkanBindlessSet(VulkanDevice& device, uint32_t capacity);
		~VulkanBindlessSet() override;

		// Buffers have no meaning on a texture heap; both log and drop.
		void SetUniformBuffer(uint32_t binding, const RHI::Ref<RHI::RHIBuffer>& buffer,
							  uint64_t offset = 0, uint64_t range = 0) override;
		void SetStorageBuffer(uint32_t binding, const RHI::Ref<RHI::RHIBuffer>& buffer,
							  uint64_t offset = 0, uint64_t range = 0) override;
		// `binding` must be 0 and `arrayIndex` the slot.
		void SetTexture(uint32_t binding, const RHI::Ref<RHI::RHITexture>& texture,
						const RHI::Ref<RHI::RHISampler>& sampler, uint32_t arrayIndex = 0) override;
		void Commit() override;

		VkDescriptorSet GetHandle() const override { return m_Set; }
		uint32_t GetCapacity() const { return m_Capacity; }

	private:
		VulkanDevice&                  m_Device;
		std::shared_ptr<DeletionQueue> m_Deletion;
		uint32_t         m_Capacity = 0;
		// Its own pool: sized for the heap and flagged UPDATE_AFTER_BIND,
		// neither of which the renderer's pool chain is.
		VkDescriptorPool m_Pool = VK_NULL_HANDLE;
		VkDescriptorSet  m_Set  = VK_NULL_HANDLE;

		std::vector<VkDescriptorImageInfo> m_PendingInfos;
		std::vector<uint32_t>              m_PendingSlots;
	};
}
