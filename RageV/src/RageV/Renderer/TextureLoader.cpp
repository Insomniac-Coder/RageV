#include <rvpch.h>
#include "TextureLoader.h"
// Upwards, from the renderer into the asset layer, which is the one place
// that direction is worth taking: every texture in the engine arrives through
// Load2D, so asking the cache here covers all of them, where asking in
// AssetManager would cover only the ones routed through handles.
#include "RageV/Asset/ImportCache.h"
#include "RageV/IO/TextureCook.h"
#include "RageV/IO/VFS.h"
#include "stb_image.h"
#include <filesystem>

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// Every image comes in through the VFS, so a texture in a pak and a
		// texture on disk are the same call. stb decodes from memory either
		// way; the bytes just stop coming from fopen.
		//
		// A miss is logged here, because the callers cite stbi_failure_reason
		// -- which describes the last *decode* failure, and a file that never
		// arrived was never decoded.
		float* LoadPixelsF(const std::string& path, int* width, int* height, int channels)
		{
			std::vector<uint8_t> bytes;
			if (!VFS::ReadBytes(path, bytes))
			{
				RV_CORE_ERROR("Texture '{0}' does not exist, loose or in a pak", path);
				return nullptr;
			}

			int unused = 0;
			return stbi_loadf_from_memory(bytes.data(), (int)bytes.size(),
										  width, height, &unused, channels);
		}
		std::unordered_map<std::string, Ref<RHITexture>> s_Cache;
		std::unordered_map<std::string, Ref<RHITexture>> s_CubeCache;
		std::unordered_map<std::string, Ref<RHITexture>> s_IrradianceCache;
		Ref<RHITexture> s_White;
		Ref<RHITexture> s_Black;
		Ref<RHITexture> s_TransparentBlack;
		Ref<RHITexture> s_FlatNormal;
		Ref<RHITexture> s_BlackCube;
		Ref<RHITexture> s_BlackCubeArray;

		// The suffixes a six-file skybox set uses, in the layer order the APIs
		// index faces by.
		constexpr const char* kFaceSuffixes[CubeFaces::kFaceCount] =
		{
			"_px", "_nx", "_py", "_ny", "_pz", "_nz"
		};

		// The integral of a whole hemisphere has no detail worth resolving, so
		// this is as small as it is useful to be. 16 a side is six 1 KB faces.
		constexpr uint32_t kIrradianceSize = 16;

		uint16_t FloatToHalf(float value)
		{
			// Clamped rather than left to overflow to infinity. An infinity in
			// the scene target survives the bloom chain -- every filter tap that
			// touches it produces another one -- and turns a whole mip white.
			value = std::clamp(value, -65504.0f, 65504.0f);

			uint32_t bits = 0;
			memcpy(&bits, &value, sizeof(bits));

			const uint32_t sign = (bits >> 16) & 0x8000u;
			const int exponent = (int)((bits >> 23) & 0xffu) - 127 + 15;
			const uint32_t mantissa = bits & 0x7fffffu;

			// Below the half range. Flushed to zero rather than encoded as a
			// denormal: these are radiance values, and 6e-8 is not a colour.
			if (exponent <= 0)
				return (uint16_t)sign;

			// Only reachable for NaN, the clamp above having handled overflow.
			if (exponent >= 31)
				return (uint16_t)(sign | 0x7bffu);

			return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
		}

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

		// The import cache first, which answers with cooked bytes for exactly
		// this source when it has cooked them before (7l). A miss is silent
		// and falls through to the source read below, so a project with no
		// cache -- or no write permission for one -- simply loads the slow way.
		//
		// **Only when a mip chain was asked for.** A caller passing false
		// wants the image as authored: the font atlas is a distance field,
		// and cooking one both block-compresses and mips it, which are the
		// two things GetFontAtlas exists to avoid. The cache refuses atlases
		// itself, and this refuses anything else that wants no chain, because
		// a rule that silent deserves both halves.
		std::vector<uint8_t> bytes;
		const bool cooked = generateMips && Assets::ImportCache::Fetch(path, bytes);

		if (!cooked && !VFS::ReadBytes(path, bytes))
		{
			RV_CORE_ERROR("Texture '{0}' does not exist, loose or in a pak", path);
			return nullptr;
		}

		// A cooked texture, if that is what the cache or the pak holds under
		// this name: same path, same handle, different bytes. The chain is
		// uploaded level by level and nothing is decoded or generated --
		// which is the whole point of cooking it.
		if (IO::TextureCook::IsCooked(bytes.data(), bytes.size()))
		{
			// Cooked bytes where the caller wanted none can only have come
			// from a pak, since the cache was not consulted at all above.
			// Uploading one level is the most that can be salvaged: the block
			// compression happened at cook time and cannot be undone here.
			// Loud, because soft text with no other symptom is exactly the
			// bug this is.
			if (!generateMips)
			{
				RV_CORE_WARN("'{0}' is cooked, but was asked for without mips -- it is "
							 "almost certainly a font atlas that the packager should "
							 "have shipped raw. Only the top level is uploaded; the "
							 "block compression cannot be undone at load.", path);
			}

			IO::CookedTexture cooked;
			if (!IO::TextureCook::Deserialize(cooked, bytes.data(), bytes.size()))
			{
				RV_CORE_ERROR("Cooked texture '{0}' will not parse", path);
				return nullptr;
			}

			const uint32_t levels =
				generateMips ? (uint32_t)cooked.Mips.size() : 1u;

			TextureDesc desc;
			desc.Width = cooked.Width;
			desc.Height = cooked.Height;
			desc.Format = IO::TextureCook::PixelFormat(cooked.Format, srgb);
			desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
			desc.MipLevels = levels;
			desc.DebugName = path;

			auto texture = device.CreateTexture(desc);
			for (uint32_t mip = 0; mip < levels; mip++)
				texture->UploadMip(cooked.Mips[mip].data(), cooked.Mips[mip].size(), mip, 0);

			s_Cache[key] = texture;
			RV_CORE_INFO("Loaded cooked texture {0} ({1}x{2}, {3} mips, {4})", path,
						 cooked.Width, cooked.Height, levels,
						 srgb ? "sRGB" : "linear");
			return texture;
		}

		int width = 0, height = 0, ignored = 0;
		// Forced to 4 channels: 3-channel uploads need row alignment handling
		// that is not worth the memory saved.
		stbi_uc* pixels = stbi_load_from_memory(bytes.data(), (int)bytes.size(),
												&width, &height, &ignored, 4);
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

	Ref<RHITexture> TextureLoader::CreateCube(RHIDevice& device, const CubeFaces& faces,
											  const std::string& debugName)
	{
		if (!faces.Valid())
		{
			RV_CORE_ERROR("CreateCube: the faces given are not a complete cube");
			return nullptr;
		}

		TextureDesc desc;
		desc.Width = faces.Size;
		desc.Height = faces.Size;
		desc.Layers = CubeFaces::kFaceCount;
		desc.Type = TextureType::TextureCube;
		desc.Format = Format::R16G16B16A16_SFLOAT;
		desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst | TextureUsage::TransferSrc;
		desc.MipLevels = 0;   // the full chain: roughness will want it in 3.4
		desc.DebugName = debugName;

		auto texture = device.CreateTexture(desc);
		if (!texture)
			return nullptr;

		std::vector<uint16_t> halves((size_t)faces.FaceFloats());

		for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
		{
			const float* source = faces.Face(face);
			for (size_t i = 0; i < halves.size(); i++)
				halves[i] = FloatToHalf(source[i]);

			texture->UploadLayer(halves.data(), halves.size() * sizeof(uint16_t), face);
		}

		// Once, after the last face. A chain generated per face would blit the
		// faces that are not there yet.
		texture->GenerateMips();
		return texture;
	}

	Ref<RHITexture> TextureLoader::LoadCube(RHIDevice& device, const std::string& path,
										   uint32_t faceSize)
	{
		if (path.empty() || faceSize == 0)
			return nullptr;

		const std::string key = path + "|cube" + std::to_string(faceSize);
		if (const auto it = s_CubeCache.find(key); it != s_CubeCache.end())
			return it->second;

		CubeFaces faces;

		// Six files, if the name says so. Recognising the set from one member
		// means the editor needs no separate six-slot widget: point at any face
		// and the rest come with it.
		const std::filesystem::path source(path);
		const std::string stem = source.stem().string();

		int suffixIndex = -1;
		for (int i = 0; i < (int)CubeFaces::kFaceCount; i++)
		{
			const std::string suffix = kFaceSuffixes[i];
			if (stem.size() > suffix.size() &&
				stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
			{
				suffixIndex = i;
				break;
			}
		}

		if (suffixIndex >= 0)
		{
			const std::string base = stem.substr(0, stem.size() - 3);
			const std::string extension = source.extension().string();

			for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
			{
				const std::filesystem::path facePath =
					source.parent_path() / (base + kFaceSuffixes[face] + extension);

				int width = 0, height = 0;
				float* pixels = LoadPixelsF(facePath.string(), &width, &height, 4);
				if (!pixels)
				{
					RV_CORE_ERROR("Cube face '{0}' will not load: {1}",
								  facePath.string(), stbi_failure_reason());
					return nullptr;
				}

				// The first face decides the size; the rest have to agree, or
				// the faces are not from the same set.
				if (face == 0)
				{
					faces.Size = (uint32_t)width;
					faces.Pixels.resize((size_t)CubeFaces::kFaceCount * width * height * 4);
				}

				if ((uint32_t)width != faces.Size || (uint32_t)height != faces.Size)
				{
					RV_CORE_ERROR("Cube face '{0}' is {1}x{2}; the set is {3} square",
								  facePath.string(), width, height, faces.Size);
					stbi_image_free(pixels);
					return nullptr;
				}

				memcpy(faces.Face(face), pixels, (size_t)faces.FaceBytes());
				stbi_image_free(pixels);
			}
		}
		else
		{
			int width = 0, height = 0;
			// Float, whatever the file was. stb un-gammas an LDR image on the
			// way through, so both paths end up linear -- which is what the
			// scene target holds and what the maths downstream assumes.
			float* pixels = LoadPixelsF(path, &width, &height, 4);
			if (!pixels)
			{
				RV_CORE_ERROR("Failed to load environment map '{0}': {1}", path,
							  stbi_failure_reason());
				return nullptr;
			}

			faces = EquirectangularToCube(pixels, (uint32_t)width, (uint32_t)height, faceSize);
			stbi_image_free(pixels);
		}

		auto texture = CreateCube(device, faces, path);
		if (!texture)
			return nullptr;

		// Convolved now, while the faces are in hand. Doing it later would mean
		// keeping 25 MB of float faces resident against the chance that
		// something asks.
		const CubeFaces irradiance = IrradianceFromCube(faces, kIrradianceSize);
		auto irradianceTexture = CreateCube(device, irradiance, path + " (irradiance)");

		// Both or neither. A cube without its irradiance is a state nothing
		// downstream can handle honestly: the sky would draw this map while the
		// diffuse ambient came from the gradient's colours, which describe a
		// different sky the scene may never have set. Failing the load makes
		// that state unrepresentable rather than merely unlikely.
		if (!irradianceTexture)
		{
			RV_CORE_ERROR("Environment map '{0}' loaded but its irradiance could not be "
						  "created; treating the map as unloaded", path);
			return nullptr;
		}

		s_IrradianceCache[key] = irradianceTexture;
		s_CubeCache[key] = texture;
		RV_CORE_INFO("Loaded environment map {0} ({1} per face, {2})", path, faces.Size,
					 suffixIndex >= 0 ? "six files" : "equirectangular");
		return texture;
	}

	Ref<RHITexture> TextureLoader::LoadIrradiance(RHIDevice& device, const std::string& path,
												  uint32_t faceSize)
	{
		if (path.empty() || faceSize == 0)
			return nullptr;

		const std::string key = path + "|cube" + std::to_string(faceSize);
		if (const auto it = s_IrradianceCache.find(key); it != s_IrradianceCache.end())
			return it->second;

		// Not loaded yet, or loaded before this existed. LoadCube fills both.
		if (!LoadCube(device, path, faceSize))
			return nullptr;

		const auto it = s_IrradianceCache.find(key);
		return it != s_IrradianceCache.end() ? it->second : nullptr;
	}

	Ref<RHITexture> TextureLoader::BlackCube(RHIDevice& device)
	{
		if (s_BlackCube)
			return s_BlackCube;

		CubeFaces faces;
		faces.Size = 1;
		faces.Pixels.assign((size_t)CubeFaces::kFaceCount * 4, 0.0f);
		for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
			faces.Face(face)[3] = 1.0f;

		s_BlackCube = CreateCube(device, faces, "default.blackcube");
		return s_BlackCube;
	}

	Ref<RHITexture> TextureLoader::BlackCubeArray(RHIDevice& device)
	{
		if (s_BlackCubeArray)
			return s_BlackCubeArray;

		// One cube's worth, because a binding only has to be *filled* -- a
		// shader reading slice 3 of a one-slice array is undefined, but nothing
		// reads this at all: it exists for the frames before the probe arrays
		// are allocated, when every instance's probe index is still zero.
		TextureDesc desc;
		desc.Width = 1;
		desc.Height = 1;
		desc.Layers = CubeFaces::kFaceCount;
		desc.Type = TextureType::TextureCubeArray;
		desc.Format = Format::R16G16B16A16_SFLOAT;
		desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
		desc.MipLevels = 1;
		desc.DebugName = "default.blackcubearray";

		auto texture = device.CreateTexture(desc);
		if (!texture)
			return nullptr;

		const uint16_t texel[4] = { 0, 0, 0, FloatToHalf(1.0f) };
		for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
			texture->UploadLayer(texel, sizeof(texel), face);

		s_BlackCubeArray = texture;
		return s_BlackCubeArray;
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

	Ref<RHITexture> TextureLoader::TransparentBlack(RHIDevice& device)
	{
		if (!s_TransparentBlack)
			s_TransparentBlack = MakeSolid(device, 0x00000000, "default.transparent");
		return s_TransparentBlack;
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
		s_CubeCache.clear();
		s_IrradianceCache.clear();
		s_White.reset();
		s_Black.reset();
		s_TransparentBlack.reset();
		s_FlatNormal.reset();
		s_BlackCube.reset();
		s_BlackCubeArray.reset();
	}
}
