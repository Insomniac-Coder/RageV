#pragma once

#include "VulkanCommon.h"
#include "RageV/Renderer/RHI/RHIResources.h"
#include <vk_mem_alloc.h>

namespace RageV::Vk
{
	class VulkanDevice;

	class VulkanBuffer final : public RHI::RHIBuffer
	{
	public:
		VulkanBuffer(VulkanDevice& device, const RHI::BufferDesc& desc);
		~VulkanBuffer() override;

		void  Upload(const void* data, uint64_t size, uint64_t offset = 0) override;
		void* GetMappedPointer() override { return m_Mapped; }

		VkBuffer GetHandle() const { return m_Buffer; }

	private:
		VulkanDevice& m_Device;
		std::shared_ptr<DeletionQueue> m_Deletion;
		VkBuffer      m_Buffer     = VK_NULL_HANDLE;
		VmaAllocation m_Allocation = VK_NULL_HANDLE;
		void*         m_Mapped     = nullptr;
	};

	class VulkanSampler final : public RHI::RHISampler
	{
	public:
		VulkanSampler(VulkanDevice& device, const RHI::SamplerDesc& desc);
		~VulkanSampler() override;

		VkSampler GetHandle() const { return m_Sampler; }

	private:
		VulkanDevice& m_Device;
		std::shared_ptr<DeletionQueue> m_Deletion;
		VkSampler     m_Sampler = VK_NULL_HANDLE;
	};

	class VulkanTexture final : public RHI::RHITexture
	{
	public:
		VulkanTexture(VulkanDevice& device, const RHI::TextureDesc& desc);
		// Wraps an image this class does not own -- used for render-target
		// attachments whose storage belongs to the target.
		VulkanTexture(VulkanDevice& device, const RHI::TextureDesc& desc,
					  VkImage image, VkImageView view, bool owned);
		~VulkanTexture() override;

		void Upload(const void* data, uint64_t size) override;
		void GenerateMips() override;
		uint64_t GetImGuiHandle() override;

		VkImage       GetImage()  const { return m_Image; }
		VkImageView   GetView()   const { return m_View; }
		VkImageLayout GetLayout() const { return m_Layout; }
		void SetLayout(VkImageLayout layout) { m_Layout = layout; }

		// Records a layout transition covering every mip and layer.
		void TransitionTo(VkCommandBuffer cmd, VkImageLayout newLayout);

	private:
		void CreateImage();

		VulkanDevice& m_Device;
		std::shared_ptr<DeletionQueue> m_Deletion;
		VkImage       m_Image      = VK_NULL_HANDLE;
		VkImageView   m_View       = VK_NULL_HANDLE;
		VmaAllocation m_Allocation = VK_NULL_HANDLE;
		VkImageLayout m_Layout     = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageAspectFlags m_Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		bool m_Owned = true;

		// Lazily created descriptor set for ImGui::Image.
		VkDescriptorSet m_ImGuiDescriptor = VK_NULL_HANDLE;
		VkSampler       m_ImGuiSampler    = VK_NULL_HANDLE;
	};

	class VulkanRenderTarget final : public RHI::RHIRenderTarget
	{
	public:
		VulkanRenderTarget(VulkanDevice& device, const RHI::RenderTargetDesc& desc);
		~VulkanRenderTarget() override;

		void Resize(uint32_t width, uint32_t height) override;
		RHI::Ref<RHI::RHITexture> GetColorTexture(uint32_t index = 0) const override;
		RHI::Ref<RHI::RHITexture> GetDepthTexture() const override { return m_Depth; }

		const std::vector<RHI::Ref<VulkanTexture>>& GetColorTextures() const { return m_Color; }
		VulkanTexture* GetDepth() const { return m_Depth.get(); }

	private:
		void Build();

		VulkanDevice& m_Device;
		std::vector<RHI::Ref<VulkanTexture>> m_Color;
		RHI::Ref<VulkanTexture> m_Depth;
	};
}
