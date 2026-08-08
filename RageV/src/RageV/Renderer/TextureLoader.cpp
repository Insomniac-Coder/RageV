#include <rvpch.h>
#include "TextureLoader.h"
#include "stb_image.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		std::unordered_map<std::string, Ref<RHITexture>> s_Cache;
		Ref<RHITexture> s_White;
		Ref<RHITexture> s_Black;
		Ref<RHITexture> s_FlatNormal;

		Ref<RHITexture> MakeSolid(RHIDevice& device, uint32_t rgba, const char* name)
		{
			TextureDesc desc;
			desc.Width = 1;
			desc.Height = 1;
			desc.Format = Format::R8G8B8A8_UNORM;
			desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
			desc.DebugName = name;

			auto texture = device.CreateTexture(desc);
			texture->Upload(&rgba, sizeof(rgba));
			return texture;
		}
	}

	Ref<RHITexture> TextureLoader::Load2D(RHIDevice& device, const std::string& path,
										  bool srgb, bool generateMips)
	{
		// Keyed on colour space too: the same file used as an albedo map and as
		// a roughness map needs two different textures.
		const std::string key = path + (srgb ? "|srgb" : "|linear");
		if (const auto it = s_Cache.find(key); it != s_Cache.end())
			return it->second;

		int width = 0, height = 0, channels = 0;
		// Forced to 4 channels: 3-channel uploads need row alignment handling
		// that is not worth the memory saved.
		stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
		if (!pixels)
		{
			RV_CORE_ERROR("Failed to load texture '{0}': {1}", path, stbi_failure_reason());
			return nullptr;
		}

		TextureDesc desc;
		desc.Width = (uint32_t)width;
		desc.Height = (uint32_t)height;
		desc.Format = srgb ? Format::R8G8B8A8_SRGB : Format::R8G8B8A8_UNORM;
		desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst | TextureUsage::TransferSrc;
		desc.MipLevels = generateMips ? 0 : 1;   // 0 asks for the full chain
		desc.DebugName = path;

		auto texture = device.CreateTexture(desc);
		texture->Upload(pixels, (uint64_t)width * height * 4);

		stbi_image_free(pixels);

		s_Cache[key] = texture;
		RV_CORE_INFO("Loaded texture {0} ({1}x{2}, {3})", path, width, height, srgb ? "sRGB" : "linear");
		return texture;
	}

	Ref<RHITexture> TextureLoader::White(RHIDevice& device)
	{
		if (!s_White)
			s_White = MakeSolid(device, 0xffffffff, "default.white");
		return s_White;
	}

	Ref<RHITexture> TextureLoader::Black(RHIDevice& device)
	{
		if (!s_Black)
			s_Black = MakeSolid(device, 0xff000000, "default.black");
		return s_Black;
	}

	Ref<RHITexture> TextureLoader::FlatNormal(RHIDevice& device)
	{
		if (!s_FlatNormal)
		{
			// ABGR byte order in memory for R8G8B8A8: R=128, G=128, B=255.
			s_FlatNormal = MakeSolid(device, 0xffff8080, "default.flatnormal");
		}
		return s_FlatNormal;
	}

	void TextureLoader::ClearCache()
	{
		// Must run before the device is destroyed; these hold GPU images.
		s_Cache.clear();
		s_White.reset();
		s_Black.reset();
		s_FlatNormal.reset();
	}
}
