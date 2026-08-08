#pragma once

// Image files to RHI textures, plus the 1x1 fallbacks a material binds when a
// map is absent. Every sampler in a descriptor set must be written even when a
// draw will not read it, so "no texture" has to mean "a texture that reads as
// the neutral value" rather than nothing.

#include "RageV/Renderer/RHI/RHIDevice.h"
#include <string>

namespace RageV
{
	class TextureLoader
	{
	public:
		// sRGB for colour maps (albedo, emissive); linear for data maps
		// (normal, metallic-roughness, occlusion). Getting this wrong is the
		// most common cause of PBR that looks subtly washed out or too dark.
		static RHI::Ref<RHI::RHITexture> Load2D(RHI::RHIDevice& device,
												const std::string& path,
												bool srgb = true,
												bool generateMips = true);

		// Cached 1x1 defaults, created on first use.
		static RHI::Ref<RHI::RHITexture> White(RHI::RHIDevice& device);
		static RHI::Ref<RHI::RHITexture> Black(RHI::RHIDevice& device);
		// (0.5, 0.5, 1.0) -- a normal pointing straight out of the surface.
		static RHI::Ref<RHI::RHITexture> FlatNormal(RHI::RHIDevice& device);

		static void ClearCache();
	};
}
