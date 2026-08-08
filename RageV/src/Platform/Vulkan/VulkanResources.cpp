#include <rvpch.h>
#include "VulkanResources.h"
#include "VulkanImGui.h"
#include "VulkanDevice.h"

#include <backends/imgui_impl_vulkan.h>

namespace RageV::Vk
{
	namespace
	{
		VkImageAspectFlags AspectFor(RHI::Format format)
		{
			if (!RHI::IsDepthFormat(format))
				return VK_IMAGE_ASPECT_COLOR_BIT;

			VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (RHI::IsStencilFormat(format))
				aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
			return aspect;
		}

		VkImageViewType ViewTypeFor(RHI::TextureType type)
		{
			switch (type)
			{
				case RHI::TextureType::Texture2D:        return VK_IMAGE_VIEW_TYPE_2D;
				case RHI::TextureType::Texture2DArray:   return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
				case RHI::TextureType::TextureCube:      return VK_IMAGE_VIEW_TYPE_CUBE;
				case RHI::TextureType::TextureCubeArray: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			}
			return VK_IMAGE_VIEW_TYPE_2D;
		}

		VkBorderColor ToVkBorderColor(RHI::BorderColor color)
		{
			switch (color)
			{
				case RHI::BorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
				case RHI::BorderColor::OpaqueBlack:      return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
				case RHI::BorderColor::OpaqueWhite:      return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			}
			return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		}

		// Access/stage pair implied by a layout. Enough for the transitions this
		// backend performs; a general barrier system would take these as input.
		void AccessForLayout(VkImageLayout layout, VkPipelineStageFlags2& stage, VkAccessFlags2& access)
		{
			switch (layout)
			{
				case VK_IMAGE_LAYOUT_UNDEFINED:
					stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
					access = 0;
					break;
				case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
					stage = VK_PIPELINE_STAGE_2_COPY_BIT;
					access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
					break;
				case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
					stage = VK_PIPELINE_STAGE_2_COPY_BIT;
					access = VK_ACCESS_2_TRANSFER_READ_BIT;
					break;
				case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
					stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
					access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
					break;
				case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
					stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
					access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
					break;
				case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
					stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
					access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
					break;
				case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
					stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
					access = 0;
					break;
				default:
					stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
					access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
					break;
			}
		}
	}

	// -------------------------------------------------------------------------
	// Buffer
	// -------------------------------------------------------------------------
	VulkanBuffer::VulkanBuffer(VulkanDevice& device, const RHI::BufferDesc& desc)
		: RHI::RHIBuffer(desc), m_Device(device), m_Deletion(device.GetDeletionQueue())
	{
		VkBufferUsageFlags usage = ToVkBufferUsage(desc.Usage);
		if (desc.Memory == RHI::MemoryDomain::DeviceLocal)
			usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;   // needs to receive staging copies

		VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = desc.Size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		if (desc.Memory == RHI::MemoryDomain::HostVisible)
		{
			// Persistently mapped and write-combined: this is the path the
			// per-frame vertex stream takes, so it must never require a copy.
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
							  VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}

		VmaAllocationInfo allocationInfo{};
		VK_CHECK(vmaCreateBuffer(m_Device.GetAllocator(), &bufferInfo, &allocInfo,
								 &m_Buffer, &m_Allocation, &allocationInfo));

		if (desc.Memory == RHI::MemoryDomain::HostVisible)
			m_Mapped = allocationInfo.pMappedData;

		if (!desc.DebugName.empty())
			m_Device.SetDebugName((uint64_t)m_Buffer, VK_OBJECT_TYPE_BUFFER, desc.DebugName.c_str());
	}

	VulkanBuffer::~VulkanBuffer()
	{
		// The GPU may still be reading this from an in-flight frame.
		VkBuffer buffer = m_Buffer;
		VmaAllocation allocation = m_Allocation;
		VmaAllocator allocator = m_Device.GetAllocator();
		m_Deletion->Push([allocator, buffer, allocation]()
		{
			vmaDestroyBuffer(allocator, buffer, allocation);
		});
	}

	void VulkanBuffer::Upload(const void* data, uint64_t size, uint64_t offset)
	{
		if (size == 0)
			return;

		RV_CORE_ASSERT(offset + size <= m_Desc.Size, "Buffer upload out of range");

		if (m_Mapped)
		{
			memcpy((uint8_t*)m_Mapped + offset, data, (size_t)size);
			return;
		}

		// Device-local: stage through a temporary host-visible buffer.
		VkBufferCreateInfo stagingInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		stagingInfo.size = size;
		stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		VmaAllocationCreateInfo stagingAlloc{};
		stagingAlloc.usage = VMA_MEMORY_USAGE_AUTO;
		stagingAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
							 VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation stagingAllocation = VK_NULL_HANDLE;
		VmaAllocationInfo stagingInfoOut{};
		VK_CHECK(vmaCreateBuffer(m_Device.GetAllocator(), &stagingInfo, &stagingAlloc,
								 &staging, &stagingAllocation, &stagingInfoOut));

		memcpy(stagingInfoOut.pMappedData, data, (size_t)size);

		m_Device.ImmediateSubmit([&](VkCommandBuffer cmd)
		{
			VkBufferCopy region{};
			region.dstOffset = offset;
			region.size = size;
			vkCmdCopyBuffer(cmd, staging, m_Buffer, 1, &region);
		});

		vmaDestroyBuffer(m_Device.GetAllocator(), staging, stagingAllocation);
	}

	// -------------------------------------------------------------------------
	// Sampler
	// -------------------------------------------------------------------------
	VulkanSampler::VulkanSampler(VulkanDevice& device, const RHI::SamplerDesc& desc)
		: RHI::RHISampler(desc), m_Device(device), m_Deletion(device.GetDeletionQueue())
	{
		const auto& caps = device.GetCaps();

		VkSamplerCreateInfo createInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		createInfo.magFilter = ToVkFilter(desc.MagFilter);
		createInfo.minFilter = ToVkFilter(desc.MinFilter);
		createInfo.mipmapMode = ToVkMipmapMode(desc.Mipmap);
		createInfo.addressModeU = ToVkAddressMode(desc.WrapU);
		createInfo.addressModeV = ToVkAddressMode(desc.WrapV);
		createInfo.addressModeW = ToVkAddressMode(desc.WrapW);
		createInfo.minLod = desc.MinLod;
		createInfo.maxLod = desc.MaxLod;
		createInfo.borderColor = ToVkBorderColor(desc.Border);
		createInfo.compareEnable = desc.CompareEnable ? VK_TRUE : VK_FALSE;
		createInfo.compareOp = ToVkCompareOp(desc.Compare);

		if (caps.SupportsAnisotropy && desc.MaxAnisotropy > 1.0f)
		{
			createInfo.anisotropyEnable = VK_TRUE;
			createInfo.maxAnisotropy = std::min(desc.MaxAnisotropy, caps.MaxAnisotropy);
		}

		VK_CHECK(vkCreateSampler(m_Device.GetDevice(), &createInfo, nullptr, &m_Sampler));
	}

	VulkanSampler::~VulkanSampler()
	{
		VkDevice device = m_Device.GetDevice();
		VkSampler sampler = m_Sampler;
		m_Deletion->Push([device, sampler]() { vkDestroySampler(device, sampler, nullptr); });
	}

	// -------------------------------------------------------------------------
	// Texture
	// -------------------------------------------------------------------------
	VulkanTexture::VulkanTexture(VulkanDevice& device, const RHI::TextureDesc& desc)
		: RHI::RHITexture(desc), m_Device(device), m_Deletion(device.GetDeletionQueue())
	{
		m_Aspect = AspectFor(desc.Format);
		CreateImage();
	}

	VulkanTexture::VulkanTexture(VulkanDevice& device, const RHI::TextureDesc& desc,
								 VkImage image, VkImageView view, bool owned)
		: RHI::RHITexture(desc), m_Device(device), m_Deletion(device.GetDeletionQueue())
		, m_Image(image), m_View(view), m_Owned(owned)
	{
		m_Aspect = AspectFor(desc.Format);
	}

	void VulkanTexture::CreateImage()
	{
		if (m_Desc.MipLevels == 0)
		{
			m_Desc.MipLevels = 1 + (uint32_t)std::floor(std::log2(std::max(m_Desc.Width, m_Desc.Height)));
			// Generating the chain needs both ends of a blit.
			m_Desc.Usage = m_Desc.Usage | RHI::TextureUsage::TransferSrc | RHI::TextureUsage::TransferDst;
		}

		const bool isCube = m_Desc.Type == RHI::TextureType::TextureCube ||
							m_Desc.Type == RHI::TextureType::TextureCubeArray;
		const uint32_t layers = isCube ? std::max(6u, m_Desc.Layers) : m_Desc.Layers;

		VkImageUsageFlags usage = ToVkImageUsage(m_Desc.Usage);
		usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;   // every texture can be uploaded to

		VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = ToVkFormat(m_Desc.Format);
		imageInfo.extent = { m_Desc.Width, m_Desc.Height, 1 };
		imageInfo.mipLevels = m_Desc.MipLevels;
		imageInfo.arrayLayers = layers;
		imageInfo.samples = (VkSampleCountFlagBits)m_Desc.Samples;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = usage;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (isCube)
			imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

		VK_CHECK(vmaCreateImage(m_Device.GetAllocator(), &imageInfo, &allocInfo,
								&m_Image, &m_Allocation, nullptr));

		VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.image = m_Image;
		viewInfo.viewType = ViewTypeFor(m_Desc.Type);
		viewInfo.format = imageInfo.format;
		viewInfo.subresourceRange.aspectMask = m_Aspect;
		viewInfo.subresourceRange.levelCount = m_Desc.MipLevels;
		viewInfo.subresourceRange.layerCount = layers;

		VK_CHECK(vkCreateImageView(m_Device.GetDevice(), &viewInfo, nullptr, &m_View));

		if (!m_Desc.DebugName.empty())
			m_Device.SetDebugName((uint64_t)m_Image, VK_OBJECT_TYPE_IMAGE, m_Desc.DebugName.c_str());

		// Attachments start in their attachment layout so the first render pass
		// does not have to special-case an undefined image.
		if (RHI::HasFlag(m_Desc.Usage, RHI::TextureUsage::Sampled))
		{
			m_Device.ImmediateSubmit([&](VkCommandBuffer cmd)
			{
				TransitionTo(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			});
		}
	}

	VulkanTexture::~VulkanTexture()
	{
		VkDevice device = m_Device.GetDevice();
		VmaAllocator allocator = m_Device.GetAllocator();
		VkImage image = m_Image;
		VkImageView view = m_View;
		VmaAllocation allocation = m_Allocation;
		const bool owned = m_Owned;
		VkDescriptorSet imguiSet = m_ImGuiDescriptor;
		VkSampler imguiSampler = m_ImGuiSampler;

		m_Deletion->Push([=]()
		{
			// Guarded: this runs on the deletion queue, and the queue's final
			// flush happens when the device is destroyed -- after the ImGui
			// backend has shut down and taken its descriptor pool with it. The
			// set is already gone at that point; asking to free it again is a
			// crash.
			if (imguiSet && IsImGuiVulkanReady())
				ImGui_ImplVulkan_RemoveTexture(imguiSet);
			if (imguiSampler) vkDestroySampler(device, imguiSampler, nullptr);
			if (owned)
			{
				if (view)  vkDestroyImageView(device, view, nullptr);
				if (image) vmaDestroyImage(allocator, image, allocation);
			}
		});
	}

	void VulkanTexture::TransitionTo(VkCommandBuffer cmd, VkImageLayout newLayout)
	{
		if (m_Layout == newLayout)
			return;

		VkPipelineStageFlags2 srcStage, dstStage;
		VkAccessFlags2 srcAccess, dstAccess;
		AccessForLayout(m_Layout, srcStage, srcAccess);
		AccessForLayout(newLayout, dstStage, dstAccess);

		const bool isCube = m_Desc.Type == RHI::TextureType::TextureCube ||
							m_Desc.Type == RHI::TextureType::TextureCubeArray;

		VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		barrier.srcStageMask = srcStage;
		barrier.srcAccessMask = srcAccess;
		barrier.dstStageMask = dstStage;
		barrier.dstAccessMask = dstAccess;
		barrier.oldLayout = m_Layout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = m_Image;
		barrier.subresourceRange.aspectMask = m_Aspect;
		barrier.subresourceRange.levelCount = m_Desc.MipLevels;
		barrier.subresourceRange.layerCount = isCube ? std::max(6u, m_Desc.Layers) : m_Desc.Layers;

		VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers = &barrier;

		vkCmdPipelineBarrier2(cmd, &dependency);
		m_Layout = newLayout;
	}

	void VulkanTexture::Upload(const void* data, uint64_t size)
	{
		if (!data || size == 0)
			return;

		VkBufferCreateInfo stagingInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		stagingInfo.size = size;
		stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		VmaAllocationCreateInfo stagingAlloc{};
		stagingAlloc.usage = VMA_MEMORY_USAGE_AUTO;
		stagingAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
							 VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation stagingAllocation = VK_NULL_HANDLE;
		VmaAllocationInfo stagingOut{};
		VK_CHECK(vmaCreateBuffer(m_Device.GetAllocator(), &stagingInfo, &stagingAlloc,
								 &staging, &stagingAllocation, &stagingOut));

		memcpy(stagingOut.pMappedData, data, (size_t)size);

		m_Device.ImmediateSubmit([&](VkCommandBuffer cmd)
		{
			TransitionTo(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			VkBufferImageCopy region{};
			region.imageSubresource.aspectMask = m_Aspect;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = { m_Desc.Width, m_Desc.Height, 1 };

			vkCmdCopyBufferToImage(cmd, staging, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			TransitionTo(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});

		vmaDestroyBuffer(m_Device.GetAllocator(), staging, stagingAllocation);

		if (m_Desc.MipLevels > 1)
			GenerateMips();
	}

	void VulkanTexture::GenerateMips()
	{
		if (m_Desc.MipLevels <= 1)
			return;

		VkFormatProperties properties;
		vkGetPhysicalDeviceFormatProperties(m_Device.GetPhysicalDevice(), ToVkFormat(m_Desc.Format), &properties);
		if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
		{
			RV_CORE_WARN("Linear blitting unsupported for this format; skipping mip generation");
			return;
		}

		m_Device.ImmediateSubmit([&](VkCommandBuffer cmd)
		{
			int32_t width = (int32_t)m_Desc.Width;
			int32_t height = (int32_t)m_Desc.Height;

			auto barrierFor = [&](uint32_t level, VkImageLayout oldLayout, VkImageLayout newLayout,
								  VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess)
			{
				VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
				barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
				barrier.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
				barrier.srcAccessMask = srcAccess;
				barrier.dstAccessMask = dstAccess;
				barrier.oldLayout = oldLayout;
				barrier.newLayout = newLayout;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = m_Image;
				barrier.subresourceRange.aspectMask = m_Aspect;
				barrier.subresourceRange.baseMipLevel = level;
				barrier.subresourceRange.levelCount = 1;
				barrier.subresourceRange.layerCount = m_Desc.Layers;

				VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
				dependency.imageMemoryBarrierCount = 1;
				dependency.pImageMemoryBarriers = &barrier;
				vkCmdPipelineBarrier2(cmd, &dependency);
			};

			// The whole image is currently SHADER_READ_ONLY; walk it down level
			// by level, blitting each mip from the one above.
			for (uint32_t level = 1; level < m_Desc.MipLevels; level++)
			{
				barrierFor(level - 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
				barrierFor(level, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

				VkImageBlit blit{};
				blit.srcSubresource.aspectMask = m_Aspect;
				blit.srcSubresource.mipLevel = level - 1;
				blit.srcSubresource.layerCount = m_Desc.Layers;
				blit.srcOffsets[1] = { width, height, 1 };

				width  = std::max(1, width / 2);
				height = std::max(1, height / 2);

				blit.dstSubresource.aspectMask = m_Aspect;
				blit.dstSubresource.mipLevel = level;
				blit.dstSubresource.layerCount = m_Desc.Layers;
				blit.dstOffsets[1] = { width, height, 1 };

				vkCmdBlitImage(cmd,
							   m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
							   m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							   1, &blit, VK_FILTER_LINEAR);

				barrierFor(level - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						   VK_ACCESS_2_TRANSFER_READ_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
				barrierFor(level, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						   VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
			}
		});

		m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	uint64_t VulkanTexture::GetImGuiHandle()
	{
		if (m_ImGuiDescriptor)
			return (uint64_t)m_ImGuiDescriptor;

		VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxLod = 1.0f;
		VK_CHECK(vkCreateSampler(m_Device.GetDevice(), &samplerInfo, nullptr, &m_ImGuiSampler));

		m_ImGuiDescriptor = ImGui_ImplVulkan_AddTexture(m_ImGuiSampler, m_View,
														VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		return (uint64_t)m_ImGuiDescriptor;
	}

	// -------------------------------------------------------------------------
	// Render target
	// -------------------------------------------------------------------------
	VulkanRenderTarget::VulkanRenderTarget(VulkanDevice& device, const RHI::RenderTargetDesc& desc)
		: RHI::RHIRenderTarget(desc), m_Device(device)
	{
		Build();
	}

	VulkanRenderTarget::~VulkanRenderTarget() = default;

	void VulkanRenderTarget::Build()
	{
		m_Color.clear();
		m_Depth.reset();

		for (size_t i = 0; i < m_Desc.ColorAttachments.size(); i++)
		{
			RHI::TextureDesc textureDesc;
			textureDesc.Width = m_Desc.Width;
			textureDesc.Height = m_Desc.Height;
			textureDesc.Format = m_Desc.ColorAttachments[i].Format;
			textureDesc.Layers = m_Desc.Layers;
			textureDesc.Type = m_Desc.Layers > 1 ? RHI::TextureType::Texture2DArray : RHI::TextureType::Texture2D;
			textureDesc.Samples = m_Desc.Samples;
			// Sampled so the editor can display the target through ImGui.
			textureDesc.Usage = RHI::TextureUsage::ColorAttachment | RHI::TextureUsage::Sampled |
								RHI::TextureUsage::TransferSrc;
			textureDesc.DebugName = m_Desc.DebugName + ".color" + std::to_string(i);

			m_Color.push_back(std::make_shared<VulkanTexture>(m_Device, textureDesc));
		}

		if (m_Desc.HasDepth)
		{
			RHI::TextureDesc depthDesc;
			depthDesc.Width = m_Desc.Width;
			depthDesc.Height = m_Desc.Height;
			depthDesc.Format = m_Desc.DepthAttachment.Format == RHI::Format::Undefined
							 ? RHI::Format::D32_SFLOAT
							 : m_Desc.DepthAttachment.Format;
			depthDesc.Layers = m_Desc.Layers;
			depthDesc.Type = m_Desc.Layers > 1 ? RHI::TextureType::Texture2DArray : RHI::TextureType::Texture2D;
			depthDesc.Samples = m_Desc.Samples;
			depthDesc.Usage = RHI::TextureUsage::DepthAttachment;
			// Shadow maps are sampled after being rendered.
			if (m_Desc.DepthSampled)
				depthDesc.Usage = depthDesc.Usage | RHI::TextureUsage::Sampled;
			depthDesc.DebugName = m_Desc.DebugName + ".depth";

			m_Depth = std::make_shared<VulkanTexture>(m_Device, depthDesc);
		}
	}

	void VulkanRenderTarget::Resize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return;
		if (width == m_Desc.Width && height == m_Desc.Height)
			return;

		m_Desc.Width = width;
		m_Desc.Height = height;
		// The old textures defer their own destruction, so in-flight frames
		// still reading them stay valid.
		Build();
	}

	RHI::Ref<RHI::RHITexture> VulkanRenderTarget::GetColorTexture(uint32_t index) const
	{
		if (index >= m_Color.size())
			return nullptr;
		return m_Color[index];
	}
}
