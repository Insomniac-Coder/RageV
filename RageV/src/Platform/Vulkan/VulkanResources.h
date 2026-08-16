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
		void UploadLayer(const void* data, uint64_t size, uint32_t layer) override;
		void UploadMip(const void* data, uint64_t size, uint32_t mip,
					   uint32_t layer) override;
		void GenerateMips() override;
		// The same blit chain, recorded into a caller's command buffer rather
		// than submitted on its own. What GenerateMips() is built from.
		void RecordGenerateMips(VkCommandBuffer cmd);
		uint64_t GetImGuiHandle() override;

		VkImage       GetImage()  const { return m_Image; }
		VkImageView   GetView()   const { return m_View; }
		VkImageLayout GetLayout() const { return m_Layout; }
		void SetLayout(VkImageLayout layout) { m_Layout = layout; }

		// Records a layout transition covering every mip and layer.
		//
		// The stage and access are implied by the two layouts, which is enough
		// for everything the backend does with an image it draws into or
		// samples. `extraStages`/`extraAccess` widen both halves of the
		// dependency for the one case that is not implied by a layout: a
		// multisample resolve target, whose write the spec puts in the colour
		// attachment output stage whatever the attachment's own aspect is.
		void TransitionTo(VkCommandBuffer cmd, VkImageLayout newLayout,
						  VkPipelineStageFlags2 extraStages = 0,
						  VkAccessFlags2 extraAccess = 0);

	private:
		void CreateImage();

		// Copies one mip of one array layer through a staging buffer. Shared
		// by every upload path; they differ only in which level and layer
		// they name.
		void StageInto(const void* data, uint64_t size, uint32_t mip, uint32_t layer);

		// A cube is six layers whether or not the desc said so, which is what
		// CreateImage allocated. Anything walking layers has to agree with it.
		uint32_t EffectiveLayers() const;

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
		// The depth resolve where there is one, for the same reason
		// GetColorTexture hands out the colour resolve: a caller asking a
		// target for "the depth" wants something a sampler2D can read.
		RHI::Ref<RHI::RHITexture> GetDepthTexture() const override
		{
			return m_DepthResolve ? m_DepthResolve : m_Depth;
		}

		const std::vector<RHI::Ref<VulkanTexture>>& GetColorTextures() const { return m_Color; }
		VulkanTexture* GetDepth() const { return m_Depth.get(); }

		// The single-sampled copy the hardware resolves each attachment into,
		// or null when the target is not multisampled. Only the command list
		// wants these: everyone else asks GetColorTexture and is handed the
		// resolve without knowing there was one.
		VulkanTexture* GetResolve(uint32_t index) const
		{
			return index < m_Resolve.size() ? m_Resolve[index].get() : nullptr;
		}

		// And the depth's, which exists only when the depth is both
		// multisampled and sampled by something.
		VulkanTexture* GetDepthResolve() const { return m_DepthResolve.get(); }

	private:
		void Build();

		VulkanDevice& m_Device;
		std::vector<RHI::Ref<VulkanTexture>> m_Color;
		std::vector<RHI::Ref<VulkanTexture>> m_Resolve;
		RHI::Ref<VulkanTexture> m_Depth;
		RHI::Ref<VulkanTexture> m_DepthResolve;
	};
}
