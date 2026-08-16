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
				case RHI::TextureType::Texture3D:        return VK_IMAGE_VIEW_TYPE_3D;
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
				// ALL_TRANSFER rather than COPY: mip generation blits, and the
				// blit stage is not part of the copy stage.
				case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
					stage = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
					access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
					break;
				case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
					stage = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
					access = VK_ACCESS_2_TRANSFER_READ_BIT;
					break;
				// Both shader stages, not only the fragment one. A target this
				// engine transitions to shader-read is usually sampled by a
				// fullscreen pass -- and since the render graph learned compute
				// passes it may instead be read by a dispatch, which this
				// barrier would then not cover, in either direction. A slightly
				// wider dependency costs nothing measurable; the missing half
				// is the kind of hazard that is a wrong answer on one driver
				// and correct on another. ENGINE-NOTES 7y.
				case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
					stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
						  | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
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

		// A usage from an extension the device did not enable is a creation
		// error, so on a device that cannot trace the input bit is dropped
		// here rather than by every caller asking the caps first. The buffer
		// then simply cannot be traced, which is what the caps said.
		if (!m_Device.RayQuerySupported())
		{
			usage &= ~(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
					   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
		}
		m_HasAddress = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;

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

	VkDeviceAddress VulkanBuffer::GetDeviceAddress() const
	{
		if (!m_HasAddress || m_Buffer == VK_NULL_HANDLE)
			return 0;
		VkBufferDeviceAddressInfo info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		info.buffer = m_Buffer;
		return vkGetBufferDeviceAddress(m_Device.GetDevice(), &info);
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
			createInfo.maxAnisotropy = Math::Min(desc.MaxAnisotropy, caps.MaxAnisotropy);
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
			m_Desc.MipLevels = 1 + (uint32_t)Math::Floor(Math::Log2((float)Math::Max(m_Desc.Width, m_Desc.Height)));
			// Generating the chain needs both ends of a blit.
			m_Desc.Usage = m_Desc.Usage | RHI::TextureUsage::TransferSrc | RHI::TextureUsage::TransferDst;
		}

		const bool isCube = m_Desc.Type == RHI::TextureType::TextureCube ||
							m_Desc.Type == RHI::TextureType::TextureCubeArray;
		const uint32_t layers = RHI::EffectiveLayers(m_Desc);

		VkImageUsageFlags usage = ToVkImageUsage(m_Desc.Usage);
		usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;   // every texture can be uploaded to

		// A volume's third extent is `extent.depth`, which every other texture
		// type here leaves at 1 -- and its arrayLayers stays 1, because Vulkan
		// has no 3D array. That is the shape of the distinction: depth is
		// filtered, layers are not. ENGINE-NOTES 7t.
		const bool isVolume = m_Desc.Type == RHI::TextureType::Texture3D;

		VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.imageType = isVolume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
		imageInfo.format = ToVkFormat(m_Desc.Format);
		imageInfo.extent = { m_Desc.Width, m_Desc.Height, isVolume ? m_Desc.Depth : 1 };
		imageInfo.mipLevels = m_Desc.MipLevels;
		imageInfo.arrayLayers = isVolume ? 1 : layers;
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

	void VulkanTexture::TransitionTo(VkCommandBuffer cmd, VkImageLayout newLayout,
									 VkPipelineStageFlags2 extraStages,
									 VkAccessFlags2 extraAccess)
	{
		if (m_Layout == newLayout)
			return;

		VkPipelineStageFlags2 srcStage, dstStage;
		VkAccessFlags2 srcAccess, dstAccess;
		AccessForLayout(m_Layout, srcStage, srcAccess);
		AccessForLayout(newLayout, dstStage, dstAccess);

		// Both halves: the caller's extra access is something that both
		// happened before this barrier and can happen after it -- a resolve
		// target is written once per pass, pass after pass.
		srcStage |= extraStages;
		srcAccess |= extraAccess;
		dstStage |= extraStages;
		dstAccess |= extraAccess;

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
		barrier.subresourceRange.layerCount = EffectiveLayers();

		VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers = &barrier;

		vkCmdPipelineBarrier2(cmd, &dependency);
		m_Layout = newLayout;
	}

	uint32_t VulkanTexture::EffectiveLayers() const
	{
		return RHI::EffectiveLayers(m_Desc);
	}

	void VulkanTexture::Upload(const void* data, uint64_t size)
	{
		if (!data || size == 0)
			return;

		StageInto(data, size, 0, 0);

		if (m_Desc.MipLevels > 1)
			GenerateMips();
	}

	void VulkanTexture::UploadLayer(const void* data, uint64_t size, uint32_t layer)
	{
		if (!data || size == 0)
			return;

		if (layer >= EffectiveLayers())
		{
			RV_CORE_WARN("Texture '{0}' has {1} layers; asked to upload layer {2}",
						 m_Desc.DebugName, EffectiveLayers(), layer);
			return;
		}

		// No mip generation here. The caller does it once the last layer is in,
		// because a blit chain reads every layer of the level above.
		StageInto(data, size, 0, layer);
	}

	void VulkanTexture::UploadMip(const void* data, uint64_t size, uint32_t mip,
								  uint32_t layer)
	{
		if (!data || size == 0)
			return;

		if (mip >= m_Desc.MipLevels || layer >= EffectiveLayers())
		{
			RV_CORE_WARN("Texture '{0}' has {1} mips and {2} layers; asked for "
						 "mip {3} of layer {4}", m_Desc.DebugName,
						 m_Desc.MipLevels, EffectiveLayers(), mip, layer);
			return;
		}

		const uint32_t width = Math::Max(m_Desc.Width >> mip, 1u);
		const uint32_t height = Math::Max(m_Desc.Height >> mip, 1u);
		const uint64_t expected = TextureDataSize(m_Desc.Format, width, height);

		// Exact, not "at least": a short buffer reads out of bounds, and a
		// long one means the caller's mip math disagrees with this one --
		// which will not stay harmless.
		if (size != expected)
		{
			RV_CORE_WARN("Texture '{0}' mip {1} is {2}x{3} and takes {4} bytes; "
						 "given {5}", m_Desc.DebugName, mip, width, height,
						 expected, size);
			return;
		}

		StageInto(data, size, mip, layer);
	}

	void VulkanTexture::StageInto(const void* data, uint64_t size, uint32_t mip, uint32_t layer)
	{
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

			// A volume is uploaded whole -- one copy covering every slice --
			// because its data is one contiguous block and it has no layers to
			// walk. `layer` is not a slice index and must stay 0 here; the
			// z extent is what carries the depth.
			const bool isVolume = m_Desc.Type == RHI::TextureType::Texture3D;

			VkBufferImageCopy region{};
			region.imageSubresource.aspectMask = m_Aspect;
			region.imageSubresource.mipLevel = mip;
			region.imageSubresource.baseArrayLayer = isVolume ? 0 : layer;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = { Math::Max(m_Desc.Width >> mip, 1u),
								   Math::Max(m_Desc.Height >> mip, 1u),
								   isVolume ? Math::Max(m_Desc.Depth >> mip, 1u) : 1 };

			vkCmdCopyBufferToImage(cmd, staging, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			TransitionTo(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});

		vmaDestroyBuffer(m_Device.GetAllocator(), staging, stagingAllocation);
	}

	void VulkanTexture::GenerateMips()
	{
		// Its own submission, which waits. Correct only when mip 0 was written
		// by something that has already run -- an upload from the CPU, say.
		// When this frame wrote mip 0, use RecordGenerateMips instead.
		m_Device.ImmediateSubmit([&](VkCommandBuffer cmd) { RecordGenerateMips(cmd); });
	}

	void VulkanTexture::RecordGenerateMips(VkCommandBuffer cmd)
	{
		if (m_Desc.MipLevels <= 1)
			return;

		// A compressed image cannot be blitted -- its chain arrives via
		// UploadMip or not at all. Loud, because a silent skip here renders
		// as a texture that shimmers at distance and nothing says why.
		if (IsCompressedFormat(m_Desc.Format))
		{
			RV_CORE_WARN("Texture '{0}' is block-compressed; its mips must be "
						 "uploaded, not generated", m_Desc.DebugName);
			return;
		}

		VkFormatProperties properties;
		vkGetPhysicalDeviceFormatProperties(m_Device.GetPhysicalDevice(), ToVkFormat(m_Desc.Format), &properties);
		if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
		{
			RV_CORE_WARN("Linear blitting unsupported for this format; skipping mip generation");
			return;
		}

		const uint32_t layers = EffectiveLayers();

		{
			int32_t width = (int32_t)m_Desc.Width;
			int32_t height = (int32_t)m_Desc.Height;

			// Stages come from the layouts rather than from the caller. The
			// version that named the blit stage for both sides of every barrier
			// was wrong in one direction each time -- these levels arrive
			// readable by the fragment shader and leave readable by it again,
			// and neither of those is a transfer. It went unseen because
			// nothing in the project had a mip chain until environment maps did.
			auto barrierFor = [&](uint32_t level, VkImageLayout oldLayout, VkImageLayout newLayout)
			{
				VkPipelineStageFlags2 srcStage, dstStage;
				VkAccessFlags2 srcAccess, dstAccess;
				AccessForLayout(oldLayout, srcStage, srcAccess);
				AccessForLayout(newLayout, dstStage, dstAccess);

				VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
				barrier.srcStageMask = srcStage;
				barrier.dstStageMask = dstStage;
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
				barrier.subresourceRange.layerCount = layers;

				VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
				dependency.imageMemoryBarrierCount = 1;
				dependency.pImageMemoryBarriers = &barrier;
				vkCmdPipelineBarrier2(cmd, &dependency);
			};

			// The whole image is currently SHADER_READ_ONLY; walk it down level
			// by level, blitting each mip from the one above.
			for (uint32_t level = 1; level < m_Desc.MipLevels; level++)
			{
				barrierFor(level - 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
				barrierFor(level, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

				VkImageBlit blit{};
				blit.srcSubresource.aspectMask = m_Aspect;
				blit.srcSubresource.mipLevel = level - 1;
				blit.srcSubresource.layerCount = layers;
				blit.srcOffsets[1] = { width, height, 1 };

				width  = Math::Max(1, width / 2);
				height = Math::Max(1, height / 2);

				blit.dstSubresource.aspectMask = m_Aspect;
				blit.dstSubresource.mipLevel = level;
				blit.dstSubresource.layerCount = layers;
				blit.dstOffsets[1] = { width, height, 1 };

				vkCmdBlitImage(cmd,
							   m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
							   m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							   1, &blit, VK_FILTER_LINEAR);

				barrierFor(level - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				barrierFor(level, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}
		}

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
		m_Resolve.clear();
		m_Depth.reset();
		m_DepthResolve.reset();

		// A multisampled image cannot be read by an ordinary sampler2D, so a
		// multisampled target carries a single-sampled twin per colour
		// attachment and the hardware resolves into it when the pass ends.
		// GetColorTexture hands that twin out, which is what keeps every
		// consumer of this target -- bloom, tone mapping, the transparency
		// composite, ImGui -- from needing to know MSAA exists.
		const bool multisampled = m_Desc.Samples > 1;

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

			if (multisampled)
			{
				RHI::TextureDesc resolveDesc = textureDesc;
				resolveDesc.Samples = 1;
				resolveDesc.DebugName = m_Desc.DebugName + ".resolve" + std::to_string(i);
				m_Resolve.push_back(std::make_shared<VulkanTexture>(m_Device, resolveDesc));
			}
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
			// TransferSrc like the colour attachments, and for the same reason:
			// something eventually wants to copy what was rendered. A point
			// light's shadow is six of these blitted into a depth cube.
			depthDesc.Usage = RHI::TextureUsage::DepthAttachment | RHI::TextureUsage::TransferSrc;
			// Shadow maps are sampled after being rendered -- but a
			// *multisampled* depth image cannot be, so there the flag belongs
			// on the twin below rather than here. Leaving it off the attachment
			// is what makes the mistake loud: bind this image to a sampler2D
			// and the layers say so on the first draw.
			if (m_Desc.DepthSampled && !multisampled)
				depthDesc.Usage = depthDesc.Usage | RHI::TextureUsage::Sampled;
			depthDesc.DebugName = m_Desc.DebugName + ".depth";

			m_Depth = std::make_shared<VulkanTexture>(m_Device, depthDesc);

			// Depth gets a single-sampled twin on the same terms the colours
			// do, and for the same reason -- everything downstream of the scene
			// pass that reconstructs a position reads depth through an ordinary
			// sampler. Only when something actually samples it: an MSAA shadow
			// map does not exist, and a twin nobody reads is a full-size image
			// per frame chain. ENGINE-NOTES 7ai.
			if (multisampled && m_Desc.DepthSampled)
			{
				RHI::TextureDesc resolveDesc = depthDesc;
				resolveDesc.Samples = 1;
				resolveDesc.Usage = resolveDesc.Usage | RHI::TextureUsage::Sampled;
				resolveDesc.DebugName = m_Desc.DebugName + ".depth.resolve";
				m_DepthResolve = std::make_shared<VulkanTexture>(m_Device, resolveDesc);
			}
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
		// The resolve, when there is one: a caller asking for "the colour" of
		// this target wants something it can sample.
		if (index < m_Resolve.size())
			return m_Resolve[index];

		if (index >= m_Color.size())
			return nullptr;
		return m_Color[index];
	}

	// -------------------------------------------------------------------------
	// Acceleration structures (ENGINE-NOTES 7am)
	// -------------------------------------------------------------------------
	namespace
	{
		VkTransformMatrixKHR ToVkTransform(const float m[16])
		{
			// Column-major 4x4 in, row-major 3x4 out: the first three rows,
			// each row's four values being that row across the columns.
			VkTransformMatrixKHR out{};
			for (int row = 0; row < 3; row++)
				for (int col = 0; col < 4; col++)
					out.matrix[row][col] = m[col * 4 + row];
			return out;
		}
	}

	VulkanAccelerationStructure::Backing VulkanAccelerationStructure::CreateBacking(
		VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, const char* name)
	{
		Backing backing;

		VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = size;
		bufferInfo.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		if (hostVisible)
		{
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
							  VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}

		VmaAllocationInfo allocationInfo{};
		VK_CHECK(vmaCreateBuffer(m_Device.GetAllocator(), &bufferInfo, &allocInfo,
								 &backing.Buffer, &backing.Allocation, &allocationInfo));
		if (hostVisible)
			backing.Mapped = allocationInfo.pMappedData;

		VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		addressInfo.buffer = backing.Buffer;
		backing.Address = vkGetBufferDeviceAddress(m_Device.GetDevice(), &addressInfo);

		if (name)
			m_Device.SetDebugName((uint64_t)backing.Buffer, VK_OBJECT_TYPE_BUFFER, name);
		return backing;
	}

	void VulkanAccelerationStructure::Destroy(Backing& backing)
	{
		if (backing.Buffer == VK_NULL_HANDLE)
			return;
		VmaAllocator allocator = m_Device.GetAllocator();
		VkBuffer buffer = backing.Buffer;
		VmaAllocation allocation = backing.Allocation;
		m_Deletion->Push([allocator, buffer, allocation]()
		{
			vmaDestroyBuffer(allocator, buffer, allocation);
		});
		backing = Backing{};
	}

	VulkanAccelerationStructure::VulkanAccelerationStructure(VulkanDevice& device,
															 const RHI::AccelerationGeometryDesc& geometry)
		: RHI::RHIAccelerationStructure(false, 0, geometry.Dynamic), m_Device(device), m_Deletion(device.GetDeletionQueue())
	{
		auto vertices = std::static_pointer_cast<VulkanBuffer>(geometry.Vertices);
		auto indices = std::static_pointer_cast<VulkanBuffer>(geometry.Indices);
		if (!vertices || !indices || vertices->GetDeviceAddress() == 0 || indices->GetDeviceAddress() == 0)
		{
			RV_CORE_ERROR("[Vulkan] bottom-level acceleration structure '{0}': the vertex and index "
						  "buffers must exist and carry BufferUsage::AccelerationStructureInput",
						  geometry.DebugName);
			return;
		}

		VkAccelerationStructureGeometryTrianglesDataKHR triangles{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
		triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		triangles.vertexData.deviceAddress = vertices->GetDeviceAddress() + geometry.VertexOffset;
		triangles.vertexStride = geometry.VertexStride;
		triangles.maxVertex = geometry.VertexCount > 0 ? geometry.VertexCount - 1 : 0;
		triangles.indexType = geometry.Type == RHI::IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
		triangles.indexData.deviceAddress = indices->GetDeviceAddress() + geometry.IndexOffset;

		VkAccelerationStructureGeometryKHR geometryInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		geometryInfo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		geometryInfo.geometry.triangles = triangles;
		geometryInfo.flags = geometry.Opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;

		const uint32_t primitiveCount = geometry.IndexCount / 3;

		// A dynamic structure (7an) is created for update -- the flag has to
		// be there at creation, and it costs a larger structure -- and built
		// for a fast build rather than a fast trace, because it will be
		// refit every frame and traced by shadow rays that stop at the first
		// hit. A static one is the opposite on both counts.
		VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		build.flags = geometry.Dynamic
					? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR
					: VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		build.geometryCount = 1;
		build.pGeometries = &geometryInfo;

		VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		vkGetAccelerationStructureBuildSizesKHR(m_Device.GetDevice(),
												VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
												&build, &primitiveCount, &sizes);

		m_Storage = CreateBacking(sizes.accelerationStructureSize,
								  VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false,
								  geometry.DebugName.empty() ? "blas" : geometry.DebugName.c_str());

		VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		createInfo.buffer = m_Storage.Buffer;
		createInfo.size = sizes.accelerationStructureSize;
		createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		VK_CHECK(vkCreateAccelerationStructureKHR(m_Device.GetDevice(), &createInfo, nullptr, &m_Structure));
		if (!geometry.DebugName.empty())
			m_Device.SetDebugName((uint64_t)m_Structure, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, geometry.DebugName.c_str());

		VkAccelerationStructureDeviceAddressInfoKHR addressInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
		addressInfo.accelerationStructure = m_Structure;
		m_Address = vkGetAccelerationStructureDeviceAddressKHR(m_Device.GetDevice(), &addressInfo);

		if (geometry.Dynamic)
		{
			// Not built now: the vertex buffer is whatever the caller has not
			// yet written into it. Kept instead: the geometry, so
			// BuildBottomLevel re-records it without being told again, and
			// scratch large enough for either a build or an update, so a
			// per-frame refit allocates nothing.
			m_Geometry = geometryInfo;
			m_PrimitiveCount = primitiveCount;
			m_Scratch = CreateBacking(Math::Max(sizes.buildScratchSize, sizes.updateScratchSize),
									  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "blas.scratch");
			return;
		}

		Backing scratch = CreateBacking(sizes.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false,
										"blas.scratch");

		build.dstAccelerationStructure = m_Structure;
		build.scratchData.deviceAddress = scratch.Address;

		VkAccelerationStructureBuildRangeInfoKHR range{};
		range.primitiveCount = primitiveCount;
		const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { &range };

		// Once, now, and wait: static geometry, the same lifetime as the
		// vertex buffer, and nothing downstream can proceed without it.
		m_Device.ImmediateSubmit([&](VkCommandBuffer cmd)
		{
			vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, ranges);
		});

		Destroy(scratch);
		m_Built = true;
	}

	void VulkanAccelerationStructure::BuildBottomLevel(VkCommandBuffer cmd)
	{
		if (m_TopLevel || !m_Dynamic || m_Structure == VK_NULL_HANDLE)
		{
			RV_CORE_ERROR("[Vulkan] BuildBottomLevel called on something that is not a dynamic "
						  "bottom-level acceleration structure");
			return;
		}

		VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		build.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |
					  VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
		// The first time there is nothing to update from; after that the
		// structure is refit in place -- source and destination the same --
		// which is legal exactly because the topology has not changed.
		build.mode = m_Built ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
							 : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		build.srcAccelerationStructure = m_Built ? m_Structure : VK_NULL_HANDLE;
		build.dstAccelerationStructure = m_Structure;
		build.geometryCount = 1;
		build.pGeometries = &m_Geometry;
		build.scratchData.deviceAddress = m_Scratch.Address;

		VkAccelerationStructureBuildRangeInfoKHR range{};
		range.primitiveCount = m_PrimitiveCount;
		const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { &range };

		vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, ranges);
		m_Built = true;

		// What reads a bottom-level structure is a top-level build that
		// places it, and the two are recorded back to back in a frame; a
		// shader never traces into one directly. So the barrier is build to
		// build. The top-level build's own barrier then covers the shaders.
		VkMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
		barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;

		VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependency.memoryBarrierCount = 1;
		dependency.pMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(cmd, &dependency);
	}

	VulkanAccelerationStructure::VulkanAccelerationStructure(VulkanDevice& device, uint32_t maxInstances)
		: RHI::RHIAccelerationStructure(true, maxInstances), m_Device(device), m_Deletion(device.GetDeletionQueue())
	{
		// Sized for the most instances a build may carry; the per-frame build
		// then reuses all three buffers and allocates nothing.
		m_Instances = CreateBacking((VkDeviceSize)Math::Max(maxInstances, 1u) * sizeof(VkAccelerationStructureInstanceKHR),
									VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
									true, "tlas.instances");

		VkAccelerationStructureGeometryInstancesDataKHR instances{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
		instances.arrayOfPointers = VK_FALSE;
		instances.data.deviceAddress = m_Instances.Address;

		VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		geometry.geometry.instances = instances;

		VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		build.geometryCount = 1;
		build.pGeometries = &geometry;

		VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		vkGetAccelerationStructureBuildSizesKHR(m_Device.GetDevice(),
												VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
												&build, &maxInstances, &sizes);

		m_Storage = CreateBacking(sizes.accelerationStructureSize,
								  VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false, "tlas");
		m_Scratch = CreateBacking(Math::Max(sizes.buildScratchSize, sizes.updateScratchSize),
								  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "tlas.scratch");

		VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		createInfo.buffer = m_Storage.Buffer;
		createInfo.size = sizes.accelerationStructureSize;
		createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		VK_CHECK(vkCreateAccelerationStructureKHR(m_Device.GetDevice(), &createInfo, nullptr, &m_Structure));
		m_Device.SetDebugName((uint64_t)m_Structure, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, "tlas");

		VkAccelerationStructureDeviceAddressInfoKHR addressInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
		addressInfo.accelerationStructure = m_Structure;
		m_Address = vkGetAccelerationStructureDeviceAddressKHR(m_Device.GetDevice(), &addressInfo);
	}

	VulkanAccelerationStructure::~VulkanAccelerationStructure()
	{
		VkDevice device = m_Device.GetDevice();
		VkAccelerationStructureKHR structure = m_Structure;
		if (structure != VK_NULL_HANDLE)
		{
			m_Deletion->Push([device, structure]()
			{
				vkDestroyAccelerationStructureKHR(device, structure, nullptr);
			});
		}
		Destroy(m_Storage);
		Destroy(m_Scratch);
		Destroy(m_Instances);
	}

	void VulkanAccelerationStructure::Build(VkCommandBuffer cmd, const RHI::AccelerationInstance* instances,
											uint32_t count)
	{
		if (!m_TopLevel || m_Structure == VK_NULL_HANDLE)
		{
			RV_CORE_ERROR("[Vulkan] Build called on something that is not a top-level acceleration structure");
			return;
		}
		if (count > m_MaxInstances)
		{
			RV_CORE_WARN("[Vulkan] top-level build asked for {0} instances, sized for {1}; the rest are dropped",
						 count, m_MaxInstances);
			count = m_MaxInstances;
		}

		// Pack. The BLAS address is the one field only this side can fill,
		// which is why the RHI took a reference and not a struct.
		auto* packed = static_cast<VkAccelerationStructureInstanceKHR*>(m_Instances.Mapped);
		uint32_t written = 0;
		for (uint32_t i = 0; i < count; i++)
		{
			auto blas = std::static_pointer_cast<VulkanAccelerationStructure>(instances[i].Blas);
			if (!blas || blas->GetDeviceAddress() == 0)
				continue;

			VkAccelerationStructureInstanceKHR& out = packed[written++];
			out.transform = ToVkTransform(instances[i].Transform);
			out.instanceCustomIndex = instances[i].CustomIndex & 0x00FFFFFFu;
			out.mask = instances[i].Mask;
			out.instanceShaderBindingTableRecordOffset = 0;
			out.flags = 0;
			out.accelerationStructureReference = blas->GetDeviceAddress();
		}

		VkAccelerationStructureGeometryInstancesDataKHR instanceData{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
		instanceData.arrayOfPointers = VK_FALSE;
		instanceData.data.deviceAddress = m_Instances.Address;

		VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		geometry.geometry.instances = instanceData;

		VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		build.dstAccelerationStructure = m_Structure;
		build.geometryCount = 1;
		build.pGeometries = &geometry;
		build.scratchData.deviceAddress = m_Scratch.Address;

		VkAccelerationStructureBuildRangeInfoKHR range{};
		range.primitiveCount = written;
		const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { &range };

		// The host wrote the instances a moment ago; the build reads them by
		// address. Host writes made before a submit are visible to it, and
		// this build is in the frame's command buffer, so no barrier is owed
		// for that. The one owed is *after*: the structure is written by the
		// build stage and read by whichever shader stage traces into it.
		vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, ranges);

		VkMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
		barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
							   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
							   VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;

		VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependency.memoryBarrierCount = 1;
		dependency.pMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(cmd, &dependency);
	}
}
