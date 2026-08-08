#pragma once

// Shared plumbing for the Vulkan backend: volk entry points, result checking
// and RHI <-> Vulkan enum conversion. volk owns every vkXxx symbol, so
// VK_NO_PROTOTYPES is set project-wide and nothing links vulkan-1 directly.

#include <volk.h>
#include "RageV/Renderer/RHI/RHITypes.h"

namespace RageV::Vk
{
	const char* ResultToString(VkResult result);

	namespace Detail
	{
		void ReportFailure(VkResult result, const char* expression, const char* file, int line);
	}
}

#define VK_CHECK(expr)                                                            \
	do {                                                                          \
		const VkResult rv_result_ = (expr);                                       \
		if (rv_result_ != VK_SUCCESS)                                             \
			::RageV::Vk::Detail::ReportFailure(rv_result_, #expr, __FILE__, __LINE__); \
	} while (false)

namespace RageV::Vk
{
	// Destruction has to be deferred: the GPU may still be reading a resource
	// from an in-flight frame when its last reference is dropped.
	//
	// This is held by shared_ptr from both the device and every resource, so it
	// outlives the device. A resource destroyed after the device then finds
	// DeviceAlive == false and does nothing -- which is correct, because
	// vkDestroyDevice already destroyed every child object. Reaching back into
	// a destroyed VulkanDevice instead is a use-after-free, and an easy mistake
	// to make from application code that keeps a Ref alive a little too long.
	struct DeletionQueue
	{
		bool DeviceAlive = true;
		uint32_t FrameIndex = 0;
		std::vector<std::vector<std::function<void()>>> PerFrame;

		void Push(std::function<void()> deleter)
		{
			if (!DeviceAlive || PerFrame.empty())
				return;
			PerFrame[FrameIndex].push_back(std::move(deleter));
		}

		void Flush(uint32_t frame)
		{
			if (frame >= PerFrame.size())
				return;
			for (auto& deleter : PerFrame[frame])
				deleter();
			PerFrame[frame].clear();
		}

		void FlushAll()
		{
			for (uint32_t i = 0; i < (uint32_t)PerFrame.size(); i++)
				Flush(i);
		}
	};

	using namespace RageV::RHI;

	VkFormat              ToVkFormat(Format format);
	Format                FromVkFormat(VkFormat format);
	VkBufferUsageFlags    ToVkBufferUsage(BufferUsage usage);
	VkImageUsageFlags     ToVkImageUsage(TextureUsage usage);
	VkShaderStageFlags    ToVkShaderStages(ShaderStage stages);
	VkShaderStageFlagBits ToVkShaderStage(ShaderStage stage);
	VkDescriptorType      ToVkDescriptorType(ResourceType type);
	VkPrimitiveTopology   ToVkTopology(PrimitiveTopology topology);
	VkPolygonMode         ToVkPolygonMode(PolygonMode mode);
	VkCullModeFlags       ToVkCullMode(CullMode mode);
	VkFrontFace           ToVkFrontFace(FrontFace face);
	VkCompareOp           ToVkCompareOp(CompareOp op);
	VkFilter              ToVkFilter(FilterMode filter);
	VkSamplerMipmapMode   ToVkMipmapMode(MipmapMode mode);
	VkSamplerAddressMode  ToVkAddressMode(WrapMode wrap);
	VkIndexType           ToVkIndexType(IndexType type);
	VkAttachmentLoadOp    ToVkLoadOp(LoadOp op);
	VkAttachmentStoreOp   ToVkStoreOp(StoreOp op);

	void ApplyBlendPreset(BlendPreset preset, VkPipelineColorBlendAttachmentState& out);

	// A combined depth/stencil image's subresource range includes the stencil
	// aspect, and the spec forbids DEPTH_ATTACHMENT_OPTIMAL for such a range
	// (VUID-VkImageMemoryBarrier2-aspectMask-08703). Pick per format so barriers
	// and vkCmdBeginRendering always agree.
	VkImageLayout DepthAttachmentLayout(Format format);
	VkImageLayout DepthAttachmentLayout(VkFormat format);
}
