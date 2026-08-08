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

	class VulkanPipeline final : public RHI::RHIPipeline
	{
	public:
		VulkanPipeline(VulkanDevice& device, const RHI::GraphicsPipelineDesc& desc);
		~VulkanPipeline() override;

		VkPipeline       GetHandle() const { return m_Pipeline; }
		VkPipelineLayout GetLayout() const { return m_Layout; }

		VkDescriptorSetLayout GetSetLayout(uint32_t set) const;

	private:
		void CreateLayouts();
		void CreatePipeline();

		VulkanDevice&    m_Device;
		std::shared_ptr<DeletionQueue> m_Deletion;
		VkPipeline       m_Pipeline = VK_NULL_HANDLE;
		VkPipelineLayout m_Layout   = VK_NULL_HANDLE;
		// Indexed by set number; gaps are VK_NULL_HANDLE.
		std::vector<VkDescriptorSetLayout> m_SetLayouts;
	};

	// One descriptor set per frame in flight. Commit() writes only into the
	// current frame's set, so an update never races the GPU reading the set a
	// previous frame bound.
	class VulkanResourceSet final : public RHI::RHIResourceSet
	{
	public:
		VulkanResourceSet(VulkanDevice& device, const RHI::Ref<VulkanPipeline>& pipeline, uint32_t set);
		~VulkanResourceSet() override;

		void SetUniformBuffer(uint32_t binding, const RHI::Ref<RHI::RHIBuffer>& buffer,
							  uint64_t offset = 0, uint64_t range = 0) override;
		void SetTexture(uint32_t binding, const RHI::Ref<RHI::RHITexture>& texture,
						const RHI::Ref<RHI::RHISampler>& sampler, uint32_t arrayIndex = 0) override;
		void Commit() override;

		VkDescriptorSet GetHandle() const;

	private:
		struct BufferWrite
		{
			uint32_t Binding;
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
		RHI::Ref<VulkanPipeline> m_Pipeline;
		std::vector<VkDescriptorSet> m_Sets;   // one per frame in flight

		std::vector<BufferWrite> m_PendingBuffers;
		std::vector<ImageWrite>  m_PendingImages;
		// A set is only valid once written; track which frames still need the
		// full write set so newly created sets are populated on first use.
		std::vector<bool> m_Dirty;
		std::vector<BufferWrite> m_LastBuffers;
		std::vector<ImageWrite>  m_LastImages;
	};
}
