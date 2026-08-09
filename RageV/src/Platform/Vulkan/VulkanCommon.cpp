#include <rvpch.h>
#include "VulkanCommon.h"

namespace RageV::Vk
{
	const char* ResultToString(VkResult result)
	{
		switch (result)
		{
			case VK_SUCCESS:                        return "VK_SUCCESS";
			case VK_NOT_READY:                      return "VK_NOT_READY";
			case VK_TIMEOUT:                        return "VK_TIMEOUT";
			case VK_INCOMPLETE:                     return "VK_INCOMPLETE";
			case VK_SUBOPTIMAL_KHR:                 return "VK_SUBOPTIMAL_KHR";
			case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
			case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
			case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
			case VK_ERROR_DEVICE_LOST:              return "VK_ERROR_DEVICE_LOST";
			case VK_ERROR_MEMORY_MAP_FAILED:        return "VK_ERROR_MEMORY_MAP_FAILED";
			case VK_ERROR_LAYER_NOT_PRESENT:        return "VK_ERROR_LAYER_NOT_PRESENT";
			case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
			case VK_ERROR_FEATURE_NOT_PRESENT:      return "VK_ERROR_FEATURE_NOT_PRESENT";
			case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
			case VK_ERROR_TOO_MANY_OBJECTS:         return "VK_ERROR_TOO_MANY_OBJECTS";
			case VK_ERROR_FORMAT_NOT_SUPPORTED:     return "VK_ERROR_FORMAT_NOT_SUPPORTED";
			case VK_ERROR_FRAGMENTED_POOL:          return "VK_ERROR_FRAGMENTED_POOL";
			case VK_ERROR_OUT_OF_POOL_MEMORY:       return "VK_ERROR_OUT_OF_POOL_MEMORY";
			case VK_ERROR_SURFACE_LOST_KHR:         return "VK_ERROR_SURFACE_LOST_KHR";
			case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
			case VK_ERROR_OUT_OF_DATE_KHR:          return "VK_ERROR_OUT_OF_DATE_KHR";
			default:                                return "VK_ERROR_<unknown>";
		}
	}

	namespace Detail
	{
		void ReportFailure(VkResult result, const char* expression, const char* file, int line)
		{
			RV_CORE_ERROR("Vulkan call failed: {0} returned {1} ({2}:{3})",
						  expression, ResultToString(result), file, line);
			RV_CORE_ASSERT(false, "Vulkan call failed");
		}
	}

	VkFormat ToVkFormat(Format format)
	{
		switch (format)
		{
			case Format::R8_UNORM:            return VK_FORMAT_R8_UNORM;
			case Format::R8G8_UNORM:          return VK_FORMAT_R8G8_UNORM;
			case Format::R8G8B8A8_UNORM:      return VK_FORMAT_R8G8B8A8_UNORM;
			case Format::R8G8B8A8_SRGB:       return VK_FORMAT_R8G8B8A8_SRGB;
			case Format::B8G8R8A8_UNORM:      return VK_FORMAT_B8G8R8A8_UNORM;
			case Format::B8G8R8A8_SRGB:       return VK_FORMAT_B8G8R8A8_SRGB;
			case Format::R16_SFLOAT:          return VK_FORMAT_R16_SFLOAT;
			case Format::R16G16_SFLOAT:       return VK_FORMAT_R16G16_SFLOAT;
			case Format::R16G16B16A16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
			case Format::B10G11R11_UFLOAT:    return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
			case Format::R9G9B9E5_UFLOAT:     return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
			case Format::D16_UNORM:           return VK_FORMAT_D16_UNORM;
			case Format::R32_SFLOAT:          return VK_FORMAT_R32_SFLOAT;
			case Format::R32G32_SFLOAT:       return VK_FORMAT_R32G32_SFLOAT;
			case Format::R32G32B32_SFLOAT:    return VK_FORMAT_R32G32B32_SFLOAT;
			case Format::R32G32B32A32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
			case Format::R32_UINT:            return VK_FORMAT_R32_UINT;
			case Format::R32_SINT:            return VK_FORMAT_R32_SINT;
			case Format::R32G32_UINT:         return VK_FORMAT_R32G32_UINT;
			case Format::R32G32B32_UINT:      return VK_FORMAT_R32G32B32_UINT;
			case Format::R32G32B32A32_UINT:   return VK_FORMAT_R32G32B32A32_UINT;
			case Format::R32G32_SINT:         return VK_FORMAT_R32G32_SINT;
			case Format::R32G32B32_SINT:      return VK_FORMAT_R32G32B32_SINT;
			case Format::R32G32B32A32_SINT:   return VK_FORMAT_R32G32B32A32_SINT;
			case Format::D32_SFLOAT:          return VK_FORMAT_D32_SFLOAT;
			case Format::D24_UNORM_S8_UINT:   return VK_FORMAT_D24_UNORM_S8_UINT;
			case Format::D32_SFLOAT_S8_UINT:  return VK_FORMAT_D32_SFLOAT_S8_UINT;
			case Format::Undefined:           return VK_FORMAT_UNDEFINED;
		}
		return VK_FORMAT_UNDEFINED;
	}

	Format FromVkFormat(VkFormat format)
	{
		switch (format)
		{
			case VK_FORMAT_R8_UNORM:            return Format::R8_UNORM;
			case VK_FORMAT_R8G8_UNORM:          return Format::R8G8_UNORM;
			case VK_FORMAT_R8G8B8A8_UNORM:      return Format::R8G8B8A8_UNORM;
			case VK_FORMAT_R8G8B8A8_SRGB:       return Format::R8G8B8A8_SRGB;
			case VK_FORMAT_B8G8R8A8_UNORM:      return Format::B8G8R8A8_UNORM;
			case VK_FORMAT_B8G8R8A8_SRGB:       return Format::B8G8R8A8_SRGB;
			case VK_FORMAT_R16_SFLOAT:          return Format::R16_SFLOAT;
			case VK_FORMAT_R16G16_SFLOAT:       return Format::R16G16_SFLOAT;
			case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::R16G16B16A16_SFLOAT;
			case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return Format::B10G11R11_UFLOAT;
			case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:  return Format::R9G9B9E5_UFLOAT;
			case VK_FORMAT_D16_UNORM:           return Format::D16_UNORM;
			case VK_FORMAT_R32_SFLOAT:          return Format::R32_SFLOAT;
			case VK_FORMAT_R32G32_SFLOAT:       return Format::R32G32_SFLOAT;
			case VK_FORMAT_R32G32B32_SFLOAT:    return Format::R32G32B32_SFLOAT;
			case VK_FORMAT_R32G32B32A32_SFLOAT: return Format::R32G32B32A32_SFLOAT;
			case VK_FORMAT_R32_UINT:            return Format::R32_UINT;
			case VK_FORMAT_R32_SINT:            return Format::R32_SINT;
			case VK_FORMAT_D32_SFLOAT:          return Format::D32_SFLOAT;
			case VK_FORMAT_D24_UNORM_S8_UINT:   return Format::D24_UNORM_S8_UINT;
			case VK_FORMAT_D32_SFLOAT_S8_UINT:  return Format::D32_SFLOAT_S8_UINT;
			default:                            return Format::Undefined;
		}
	}

	VkBufferUsageFlags ToVkBufferUsage(BufferUsage usage)
	{
		VkBufferUsageFlags flags = 0;
		if (HasFlag(usage, BufferUsage::Vertex))      flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		if (HasFlag(usage, BufferUsage::Index))       flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		if (HasFlag(usage, BufferUsage::Uniform))     flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		if (HasFlag(usage, BufferUsage::Storage))     flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (HasFlag(usage, BufferUsage::Indirect))    flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		if (HasFlag(usage, BufferUsage::TransferSrc)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		if (HasFlag(usage, BufferUsage::TransferDst)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		return flags;
	}

	VkImageUsageFlags ToVkImageUsage(TextureUsage usage)
	{
		VkImageUsageFlags flags = 0;
		if (HasFlag(usage, TextureUsage::Sampled))         flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
		if (HasFlag(usage, TextureUsage::ColorAttachment)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if (HasFlag(usage, TextureUsage::DepthAttachment)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if (HasFlag(usage, TextureUsage::Storage))         flags |= VK_IMAGE_USAGE_STORAGE_BIT;
		if (HasFlag(usage, TextureUsage::TransferSrc))     flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if (HasFlag(usage, TextureUsage::TransferDst))     flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		return flags;
	}

	VkShaderStageFlags ToVkShaderStages(ShaderStage stages)
	{
		VkShaderStageFlags flags = 0;
		if (HasFlag(stages, ShaderStage::Vertex))   flags |= VK_SHADER_STAGE_VERTEX_BIT;
		if (HasFlag(stages, ShaderStage::Fragment)) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
		if (HasFlag(stages, ShaderStage::Compute))  flags |= VK_SHADER_STAGE_COMPUTE_BIT;
		return flags;
	}

	VkShaderStageFlagBits ToVkShaderStage(ShaderStage stage)
	{
		switch (stage)
		{
			case ShaderStage::Vertex:   return VK_SHADER_STAGE_VERTEX_BIT;
			case ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
			case ShaderStage::Compute:  return VK_SHADER_STAGE_COMPUTE_BIT;
			default:                    return VK_SHADER_STAGE_VERTEX_BIT;
		}
	}

	VkDescriptorType ToVkDescriptorType(ResourceType type)
	{
		switch (type)
		{
			case ResourceType::UniformBuffer:        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			case ResourceType::StorageBuffer:        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			case ResourceType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			case ResourceType::StorageImage:         return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		}
		return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	}

	VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology)
	{
		switch (topology)
		{
			case PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			case PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case PrimitiveTopology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		}
		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}

	VkPolygonMode ToVkPolygonMode(PolygonMode mode)
	{
		switch (mode)
		{
			case PolygonMode::Fill:  return VK_POLYGON_MODE_FILL;
			case PolygonMode::Line:  return VK_POLYGON_MODE_LINE;
			case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
		}
		return VK_POLYGON_MODE_FILL;
	}

	VkCullModeFlags ToVkCullMode(CullMode mode)
	{
		switch (mode)
		{
			case CullMode::None:  return VK_CULL_MODE_NONE;
			case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
			case CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
		}
		return VK_CULL_MODE_NONE;
	}

	VkFrontFace ToVkFrontFace(FrontFace face)
	{
		return face == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
	}

	VkCompareOp ToVkCompareOp(CompareOp op)
	{
		switch (op)
		{
			case CompareOp::Never:          return VK_COMPARE_OP_NEVER;
			case CompareOp::Less:           return VK_COMPARE_OP_LESS;
			case CompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
			case CompareOp::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
			case CompareOp::Greater:        return VK_COMPARE_OP_GREATER;
			case CompareOp::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
			case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
			case CompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
		}
		return VK_COMPARE_OP_LESS_OR_EQUAL;
	}

	VkFilter ToVkFilter(FilterMode filter)
	{
		return filter == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	}

	VkSamplerMipmapMode ToVkMipmapMode(MipmapMode mode)
	{
		return mode == MipmapMode::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}

	VkSamplerAddressMode ToVkAddressMode(WrapMode wrap)
	{
		switch (wrap)
		{
			case WrapMode::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
			case WrapMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			case WrapMode::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			case WrapMode::ClampToBorder:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		}
		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}

	VkIndexType ToVkIndexType(IndexType type)
	{
		return type == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
	}

	VkAttachmentLoadOp ToVkLoadOp(LoadOp op)
	{
		switch (op)
		{
			case LoadOp::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
			case LoadOp::Clear:    return VK_ATTACHMENT_LOAD_OP_CLEAR;
			case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		}
		return VK_ATTACHMENT_LOAD_OP_CLEAR;
	}

	VkAttachmentStoreOp ToVkStoreOp(StoreOp op)
	{
		return op == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}

	VkImageLayout DepthAttachmentLayout(Format format)
	{
		return IsStencilFormat(format) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
									   : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	}

	VkImageLayout DepthAttachmentLayout(VkFormat format)
	{
		return DepthAttachmentLayout(FromVkFormat(format));
	}

	void ApplyBlendPreset(BlendPreset preset, VkPipelineColorBlendAttachmentState& out)
	{
		out.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
							 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		out.colorBlendOp = VK_BLEND_OP_ADD;
		out.alphaBlendOp = VK_BLEND_OP_ADD;

		switch (preset)
		{
			case BlendPreset::Opaque:
				out.blendEnable = VK_FALSE;
				out.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				out.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
				out.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				out.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				break;

			case BlendPreset::AlphaBlend:
				out.blendEnable = VK_TRUE;
				out.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				out.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				out.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				out.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				break;

			case BlendPreset::Additive:
				out.blendEnable = VK_TRUE;
				out.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				out.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				out.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				out.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				break;

			case BlendPreset::PremultipliedAlpha:
				out.blendEnable = VK_TRUE;
				out.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				out.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				out.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				out.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				break;
		}
	}
}
