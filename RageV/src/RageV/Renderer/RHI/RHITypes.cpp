#include <rvpch.h>
#include "RHITypes.h"

namespace RageV::RHI
{
	bool IsDepthFormat(Format format)
	{
		switch (format)
		{
			case Format::D16_UNORM:
			case Format::D32_SFLOAT:
			case Format::D24_UNORM_S8_UINT:
			case Format::D32_SFLOAT_S8_UINT:
				return true;
			default:
				return false;
		}
	}

	bool IsStencilFormat(Format format)
	{
		return format == Format::D24_UNORM_S8_UINT || format == Format::D32_SFLOAT_S8_UINT;
	}

	uint32_t FormatSize(Format format)
	{
		switch (format)
		{
			case Format::R8_UNORM:              return 1;
			case Format::R8G8_UNORM:            return 2;
			case Format::R16_SFLOAT:            return 2;
			case Format::D16_UNORM:             return 2;
			case Format::R16G16_SFLOAT:         return 4;
			case Format::B10G11R11_UFLOAT:      return 4;
			case Format::R9G9B9E5_UFLOAT:       return 4;
			case Format::R8G8B8A8_UNORM:
			case Format::R8G8B8A8_SRGB:
			case Format::B8G8R8A8_UNORM:
			case Format::B8G8R8A8_SRGB:
			case Format::R32_SFLOAT:
			case Format::R32_UINT:
			case Format::R32_SINT:
			case Format::D32_SFLOAT:
			case Format::D24_UNORM_S8_UINT:     return 4;
			case Format::R32G32_UINT:
			case Format::R32G32_SINT:
			case Format::R32G32_SFLOAT:         return 8;
			case Format::D32_SFLOAT_S8_UINT:    return 8;
			case Format::R16G16B16A16_SFLOAT:   return 8;
			case Format::R32G32B32_UINT:
			case Format::R32G32B32_SINT:
			case Format::R32G32B32_SFLOAT:      return 12;
			case Format::R32G32B32A32_UINT:
			case Format::R32G32B32A32_SINT:
			case Format::R32G32B32A32_SFLOAT:   return 16;
			// No per-pixel size exists; TextureDataSize is the question to ask.
			case Format::BC1_UNORM:
			case Format::BC1_SRGB:
			case Format::BC3_UNORM:
			case Format::BC3_SRGB:
			case Format::BC4_UNORM:
			case Format::BC5_UNORM:
			case Format::BC6H_UFLOAT:           return 0;
			case Format::Undefined:             return 0;
		}
		return 0;
	}

	bool IsCompressedFormat(Format format)
	{
		switch (format)
		{
			case Format::BC1_UNORM:
			case Format::BC1_SRGB:
			case Format::BC3_UNORM:
			case Format::BC3_SRGB:
			case Format::BC4_UNORM:
			case Format::BC5_UNORM:
			case Format::BC6H_UFLOAT:
				return true;
			default:
				return false;
		}
	}

	uint64_t TextureDataSize(Format format, uint32_t width, uint32_t height)
	{
		if (!IsCompressedFormat(format))
			return (uint64_t)FormatSize(format) * width * height;

		// Whole 4x4 blocks: a 2x2 mip still occupies one.
		const uint64_t blocksX = (width + 3) / 4;
		const uint64_t blocksY = (height + 3) / 4;

		const bool halfSize = format == Format::BC1_UNORM || format == Format::BC1_SRGB ||
							  format == Format::BC4_UNORM;
		return blocksX * blocksY * (halfSize ? 8 : 16);
	}

	uint32_t EffectiveLayers(const TextureDesc& desc)
	{
		const bool isCube = desc.Type == TextureType::TextureCube ||
							desc.Type == TextureType::TextureCubeArray;
		return isCube ? Math::Max(6u, desc.Layers) : Math::Max(1u, desc.Layers);
	}

	const char* ShaderStageName(ShaderStage stage)
	{
		switch (stage)
		{
			case ShaderStage::Vertex:   return "vertex";
			case ShaderStage::Fragment: return "fragment";
			case ShaderStage::Compute:  return "compute";
			default:                    return "unknown";
		}
	}
}
