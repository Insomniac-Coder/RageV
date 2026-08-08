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
}
