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
#include <array>
#include <cmath>
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
		Ref<RHITexture> s_Red;
		Ref<RHITexture> s_Magenta;
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
			value = Math::Clamp(value, -65504.0f, 65504.0f);

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

	namespace
	{
		// sRGB to linear, the exact curve, through a table: the raw path
		// averages every texel of an image that may be sixteen megapixels,
		// and two hundred and fifty-six answers cover every input it can
		// have.
		const std::array<float, 256>& SrgbTable()
		{
			static const std::array<float, 256> table = []
			{
				std::array<float, 256> t{};
				for (int i = 0; i < 256; i++)
				{
					const float v = i / 255.0f;
					t[i] = v <= 0.04045f ? v / 12.92f
										 : std::pow((v + 0.055f) / 1.055f, 2.4f);
				}
				return t;
			}();
			return table;
		}

		// One BC1 colour block, all sixteen texels: two RGB565 endpoints, a
		// two-bit index per texel, and two interpolation modes chosen by
		// which endpoint is numerically larger.
		void DecodeBc1Block(const uint8_t* block, Vec3 out[16])
		{
			auto expand = [](uint16_t c)
			{
				// 5 and 6 bits replicated into 8, which is what a decoder
				// does: 0x1F must come back 255 and not 248.
				const uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
				return Vec3((float)((r << 3) | (r >> 2)),
							(float)((g << 2) | (g >> 4)),
							(float)((b << 3) | (b >> 2)));
			};

			const uint16_t c0 = (uint16_t)(block[0] | (block[1] << 8));
			const uint16_t c1 = (uint16_t)(block[2] | (block[3] << 8));
			const Vec3 a = expand(c0), b = expand(c1);

			Vec3 palette[4] = { a, b, Vec3(0.0f), Vec3(0.0f) };
			if (c0 > c1)
			{
				palette[2] = (a * 2.0f + b) / 3.0f;
				palette[3] = (a + b * 2.0f) / 3.0f;
			}
			else
			{
				palette[2] = (a + b) * 0.5f;
				palette[3] = Vec3(0.0f);   // the mode's transparent black
			}

			const uint32_t indices = (uint32_t)block[4] | ((uint32_t)block[5] << 8)
								   | ((uint32_t)block[6] << 16) | ((uint32_t)block[7] << 24);
			for (int i = 0; i < 16; i++)
				out[i] = palette[(indices >> (i * 2)) & 0x3u];
		}

		// The mean of a cooked chain.
		//
		// **Not the 1x1 mip, and that was a defect.** The obvious reading is
		// that the smallest level *is* the average and costs one texel to
		// read -- but the cooker halves with a box filter that takes
		// `max(w/2,1)` and taps `min(x*2+1, w-1)`, so an odd level drops its
		// tail row and column entirely. A 768-wide map reduces cleanly to 3
		// and then throws away five texels of nine. Measured on this
		// project's own emitter fixture that is 2.27x wrong, and a variant
		// with the lit cells one row over reads exactly zero -- which would
		// hand the emitter list a black rectangle for a surface that is
		// visibly glowing, and stage 0's flag would then have the hemisphere
		// term subtract light nothing ever added.
		//
		// So: the deepest level reachable by *even* halvings, every texel of
		// it, linearised individually. Every reduction on the way there
		// divided exactly, so that level's average is the image's. It is a
		// handful of blocks -- 3x3 for the 768 case, 25x25 for a 100 -- and
		// the arithmetic is honest at any size.
		bool MeanOfCooked(const IO::CookedTexture& cooked, bool srgb, Vec3& out)
		{
			if (cooked.Mips.empty() || cooked.Width == 0 || cooked.Height == 0)
				return false;

			// BC4 and BC5 are data maps -- roughness, normals -- and nothing
			// asks those for a mean. Note the cooker picks them by *file
			// name*, so an emissive map called `..._ao.png` lands here; the
			// caller warns, because silently keeping the unfolded radiance is
			// the failure this whole function exists to prevent.
			if (cooked.Format != IO::CookedPixelFormat::RGBA8
				&& cooked.Format != IO::CookedPixelFormat::BC1
				&& cooked.Format != IO::CookedPixelFormat::BC3)
			{
				return false;
			}

			uint32_t width = cooked.Width, height = cooked.Height, level = 0;
			if (cooked.Format != IO::CookedPixelFormat::RGBA8)
			{
				// RGBA8 is only chosen for a texture smaller than one block,
				// and its chain is filtered in *encoded* space rather than
				// linear -- so for that format only mip 0 is a sound answer.
				while (level + 1 < cooked.Mips.size()
					   && width > 1 && height > 1
					   && (width % 2) == 0 && (height % 2) == 0)
				{
					width /= 2;
					height /= 2;
					level++;
				}
			}

			const std::vector<uint8_t>& mip = cooked.Mips[level];
			const std::array<float, 256>& table = SrgbTable();
			auto channel = [&](float encoded)
			{
				const int i = (int)Math::Clamp(encoded + 0.5f, 0.0f, 255.0f);
				return srgb ? table[(size_t)i] : encoded / 255.0f;
			};

			double r = 0.0, g = 0.0, b = 0.0;
			uint64_t counted = 0;

			if (cooked.Format == IO::CookedPixelFormat::RGBA8)
			{
				if (mip.size() < (size_t)width * height * 4)
					return false;
				for (uint64_t i = 0; i < (uint64_t)width * height; i++)
				{
					const uint8_t* p = mip.data() + i * 4;
					r += channel((float)p[0]);
					g += channel((float)p[1]);
					b += channel((float)p[2]);
					counted++;
				}
			}
			else
			{
				const uint32_t blocksX = (width + 3) / 4;
				const uint32_t blocksY = (height + 3) / 4;
				const size_t stride = cooked.Format == IO::CookedPixelFormat::BC3 ? 16 : 8;
				// BC3 puts eight bytes of alpha in front of the colour block.
				const size_t colour = cooked.Format == IO::CookedPixelFormat::BC3 ? 8 : 0;
				if (mip.size() < (size_t)blocksX * blocksY * stride)
					return false;

				Vec3 texels[16];
				for (uint32_t by = 0; by < blocksY; by++)
				{
					for (uint32_t bx = 0; bx < blocksX; bx++)
					{
						const size_t offset = ((size_t)by * blocksX + bx) * stride + colour;
						DecodeBc1Block(mip.data() + offset, texels);

						// Only the texels the image actually has: the last
						// block of an odd-sized level is padding the encoder
						// invented, and averaging it in would drag the answer
						// toward whatever it invented.
						for (uint32_t ty = 0; ty < 4; ty++)
						{
							for (uint32_t tx = 0; tx < 4; tx++)
							{
								if (bx * 4 + tx >= width || by * 4 + ty >= height)
									continue;
								const Vec3& t = texels[ty * 4 + tx];
								r += channel(t.x);
								g += channel(t.y);
								b += channel(t.z);
								counted++;
							}
						}
					}
				}
			}

			if (counted == 0)
				return false;

			out = Vec3((float)(r / (double)counted), (float)(g / (double)counted),
					   (float)(b / (double)counted));
			return true;
		}

		// And of a raw image, which has no chain to read: every texel, in
		// linear space. **Not sampled on a stride**, which is the tempting
		// optimisation and the wrong one here -- a map whose bright part is
		// four cells of a hundred and forty-four is exactly what this exists
		// to measure, and a stride can step over all four and report black.
		// Only asked of colour maps -- see the call site: a normal or a
		// roughness map is never an emitter's radiance, and scanning every
		// texel of one is a cold-load cost for an answer nobody reads.
		Vec3 MeanOfPixels(const uint8_t* rgba, uint32_t width, uint32_t height, bool srgb)
		{
			const std::array<float, 256>& table = SrgbTable();
			double r = 0.0, g = 0.0, b = 0.0;
			const size_t count = (size_t)width * height;
			for (size_t i = 0; i < count; i++)
			{
				const uint8_t* p = rgba + i * 4;
				if (srgb)
				{
					r += table[p[0]]; g += table[p[1]]; b += table[p[2]];
				}
				else
				{
					r += p[0] / 255.0; g += p[1] / 255.0; b += p[2] / 255.0;
				}
			}
			if (count == 0)
				return Vec3(1.0f);
			return Vec3((float)(r / count), (float)(g / count), (float)(b / count));
		}

		// Keyed on the texture rather than the path: two names for one file
		// are one texture here, and a caller holding the texture is what
		// asks. Weak by construction -- a raw pointer used only as an
		// identity, never dereferenced.
		std::unordered_map<const RHITexture*, Vec3> s_Means;
	}

	Vec3 TextureLoader::MeanColor(const Ref<RHITexture>& texture)
	{
		if (!texture)
			return Vec3(1.0f);
		const auto it = s_Means.find(texture.get());
		return it == s_Means.end() ? Vec3(1.0f) : it->second;
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

			if (Vec3 mean(1.0f); MeanOfCooked(cooked, srgb, mean))
			{
				s_Means[texture.get()] = mean;
			}
			else if (srgb)
			{
				// Loud, because the failure is invisible: a colour map with
				// no mean keeps white, and an emissive one then radiates the
				// unfolded scalar -- up to the whole phantom this exists to
				// remove. The usual cause is the cooker choosing BC4 or BC5
				// from the file's *name* (`..._ao`, `..._roughness`) for a
				// map that is really a colour.
				RV_CORE_WARN("No average could be read from '{0}', so a material using it "
							 "as an emissive map will light the scene from its full "
							 "scalar rather than from what the map actually emits.", path);
			}

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

		// Colour maps only, for the reason MeanOfPixels gives.
		if (srgb)
		{
			s_Means[texture.get()] =
				MeanOfPixels(pixels, (uint32_t)width, (uint32_t)height, srgb);
		}

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

	Ref<RHITexture> TextureLoader::Red(RHIDevice& device)
	{
		if (!s_Red)
			s_Red = MakeSolid(device, 0x000000ff, "default.red");   // R=255, G=B=A=0
		return s_Red;
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

	Ref<RHITexture> TextureLoader::Magenta(RHIDevice& device)
	{
		// ABGR in memory: R=255, G=0, B=255, A=255.
		if (!s_Magenta)
			s_Magenta = MakeSolid(device, 0xffff00ff, "default.magenta");
		return s_Magenta;
	}

	void TextureLoader::ClearCache()
	{
		// Must run before the device is destroyed; these hold GPU images.
		//
		// **s_Means goes with them, not after them.** It is keyed on a raw
		// RHITexture pointer used purely as an identity, and this call is
		// what destroys the objects those pointers name -- so a map that
		// outlives it holds addresses the allocator may hand to something
		// else, and a later texture landing on one would inherit an average
		// belonging to an image nobody has any more. That is the same
		// dangling-identity rule the environment filter and the probe arrays
		// were joined to this clear for; the call site in AssetManager spells
		// it out. It also stops the map growing once per texture ever loaded.
		s_Means.clear();
		s_Cache.clear();
		s_CubeCache.clear();
		s_IrradianceCache.clear();
		s_White.reset();
		s_Black.reset();
		s_TransparentBlack.reset();
		s_FlatNormal.reset();
		s_Red.reset();
		s_Magenta.reset();
		s_BlackCube.reset();
		s_BlackCubeArray.reset();
	}
}
