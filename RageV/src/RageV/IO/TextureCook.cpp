#include <rvpch.h>
#include "TextureCook.h"
#include "RageV/Core/Log.h"

#include "stb_dxt.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace RageV::IO
{
	namespace
	{
		constexpr char kMagic[4] = { 'R', 'V', 'T', 'X' };
		constexpr uint32_t kVersion = 1;

		// One mip level as float RGBA, which is what the filtering works in.
		struct FloatImage
		{
			uint32_t Width = 0;
			uint32_t Height = 0;
			std::vector<float> Pixels;   // 4 floats per texel

			float* At(uint32_t x, uint32_t y) { return &Pixels[((size_t)y * Width + x) * 4]; }
			const float* At(uint32_t x, uint32_t y) const { return &Pixels[((size_t)y * Width + x) * 4]; }
		};

		float SrgbToLinear(float v)
		{
			return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
		}

		float LinearToSrgb(float v)
		{
			return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
		}

		bool NameSaysNormal(std::string_view name)
		{
			return name.find("_normal") != std::string_view::npos;
		}

		bool NameSaysSingleChannel(std::string_view name)
		{
			// The suffixes every pipeline in this repository uses for its
			// data maps -- the shader reads all of them from .r.
			for (const char* tag : { "_roughness", "_metallic", "_ao",
									 "_occlusion", "_height", "_displacement" })
			{
				if (name.find(tag) != std::string_view::npos)
					return true;
			}
			return false;
		}

		FloatImage ToFloat(const uint8_t* rgba, uint32_t width, uint32_t height,
						   bool srgbFilter)
		{
			FloatImage image;
			image.Width = width;
			image.Height = height;
			image.Pixels.resize((size_t)width * height * 4);

			for (size_t i = 0; i < (size_t)width * height; i++)
			{
				for (int c = 0; c < 3; c++)
				{
					const float v = rgba[i * 4 + c] / 255.0f;
					image.Pixels[i * 4 + c] = srgbFilter ? SrgbToLinear(v) : v;
				}
				// Alpha is coverage and filters linearly whatever the colour
				// channels do.
				image.Pixels[i * 4 + 3] = rgba[i * 4 + 3] / 255.0f;
			}

			return image;
		}

		std::vector<uint8_t> ToBytes(const FloatImage& image, bool srgbFilter)
		{
			std::vector<uint8_t> bytes((size_t)image.Width * image.Height * 4);
			for (size_t i = 0; i < bytes.size() / 4; i++)
			{
				for (int c = 0; c < 3; c++)
				{
					float v = image.Pixels[i * 4 + c];
					if (srgbFilter)
						v = LinearToSrgb(v);
					bytes[i * 4 + c] = (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
				}
				bytes[i * 4 + 3] =
					(uint8_t)std::lround(std::clamp(image.Pixels[i * 4 + 3], 0.0f, 1.0f) * 255.0f);
			}
			return bytes;
		}

		// A 2x2 box filter, clamping at odd edges. Colour filtering happens
		// in whatever space ToFloat put the values in -- linear for sRGB
		// content, raw for data -- which matches what the GPU blit chain this
		// replaces did with the formats those textures were created as.
		FloatImage Downsample(const FloatImage& source)
		{
			FloatImage mip;
			mip.Width = std::max(source.Width / 2, 1u);
			mip.Height = std::max(source.Height / 2, 1u);
			mip.Pixels.resize((size_t)mip.Width * mip.Height * 4);

			for (uint32_t y = 0; y < mip.Height; y++)
			{
				for (uint32_t x = 0; x < mip.Width; x++)
				{
					const uint32_t x0 = std::min(x * 2, source.Width - 1);
					const uint32_t x1 = std::min(x * 2 + 1, source.Width - 1);
					const uint32_t y0 = std::min(y * 2, source.Height - 1);
					const uint32_t y1 = std::min(y * 2 + 1, source.Height - 1);

					float* out = mip.At(x, y);
					for (int c = 0; c < 4; c++)
					{
						out[c] = 0.25f * (source.At(x0, y0)[c] + source.At(x1, y0)[c] +
										  source.At(x0, y1)[c] + source.At(x1, y1)[c]);
					}
				}
			}

			return mip;
		}

		// Averaging unit vectors shortens them, and a shortened normal reads
		// as a duller surface at distance. Reconstruct, renormalize, restore.
		void RenormalizeNormals(FloatImage& image)
		{
			for (size_t i = 0; i < (size_t)image.Width * image.Height; i++)
			{
				float x = image.Pixels[i * 4 + 0] * 2.0f - 1.0f;
				float y = image.Pixels[i * 4 + 1] * 2.0f - 1.0f;
				const float zSquared = 1.0f - x * x - y * y;
				float z = std::sqrt(std::max(zSquared, 0.0f));

				const float length = std::sqrt(x * x + y * y + z * z);
				if (length > 1e-6f)
				{
					x /= length;
					y /= length;
					z /= length;
				}
				else
				{
					x = 0.0f; y = 0.0f; z = 1.0f;
				}

				image.Pixels[i * 4 + 0] = x * 0.5f + 0.5f;
				image.Pixels[i * 4 + 1] = y * 0.5f + 0.5f;
				image.Pixels[i * 4 + 2] = z * 0.5f + 0.5f;
			}
		}

		// One 4x4 block's worth of texels, edge-clamped where the mip is not
		// a multiple of four -- the padding must repeat real pixels, because
		// the encoder fits endpoints to everything it is given.
		void GatherBlock(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height,
						 uint32_t blockX, uint32_t blockY, uint8_t out[64])
		{
			for (uint32_t y = 0; y < 4; y++)
			{
				for (uint32_t x = 0; x < 4; x++)
				{
					const uint32_t sx = std::min(blockX * 4 + x, width - 1);
					const uint32_t sy = std::min(blockY * 4 + y, height - 1);
					std::memcpy(&out[(y * 4 + x) * 4], &rgba[((size_t)sy * width + sx) * 4], 4);
				}
			}
		}

		std::vector<uint8_t> EncodeMip(const std::vector<uint8_t>& rgba,
									   uint32_t width, uint32_t height,
									   CookedPixelFormat format)
		{
			if (format == CookedPixelFormat::RGBA8)
				return rgba;

			const uint32_t blocksX = (width + 3) / 4;
			const uint32_t blocksY = (height + 3) / 4;
			const uint32_t blockBytes =
				(format == CookedPixelFormat::BC1 || format == CookedPixelFormat::BC4) ? 8 : 16;

			std::vector<uint8_t> encoded((size_t)blocksX * blocksY * blockBytes);

			uint8_t block[64];
			for (uint32_t by = 0; by < blocksY; by++)
			{
				for (uint32_t bx = 0; bx < blocksX; bx++)
				{
					GatherBlock(rgba, width, height, bx, by, block);
					uint8_t* out = &encoded[((size_t)by * blocksX + bx) * blockBytes];

					switch (format)
					{
						case CookedPixelFormat::BC1:
							stb_compress_dxt_block(out, block, 0, STB_DXT_HIGHQUAL);
							break;
						case CookedPixelFormat::BC3:
							stb_compress_dxt_block(out, block, 1, STB_DXT_HIGHQUAL);
							break;
						case CookedPixelFormat::BC4:
						{
							uint8_t red[16];
							for (int i = 0; i < 16; i++)
								red[i] = block[i * 4];
							stb_compress_bc4_block(out, red);
							break;
						}
						case CookedPixelFormat::BC5:
						{
							uint8_t redGreen[32];
							for (int i = 0; i < 16; i++)
							{
								redGreen[i * 2 + 0] = block[i * 4 + 0];
								redGreen[i * 2 + 1] = block[i * 4 + 1];
							}
							stb_compress_bc5_block(out, redGreen);
							break;
						}
						case CookedPixelFormat::RGBA8:
							break;
					}
				}
			}

			return encoded;
		}

		template <typename T>
		void Append(std::vector<uint8_t>& out, const T& value)
		{
			const uint8_t* bytes = (const uint8_t*)&value;
			out.insert(out.end(), bytes, bytes + sizeof(T));
		}

		template <typename T>
		bool ReadValue(const uint8_t*& cursor, const uint8_t* end, T& out)
		{
			if ((size_t)(end - cursor) < sizeof(T))
				return false;
			std::memcpy(&out, cursor, sizeof(T));
			cursor += sizeof(T);
			return true;
		}
	}

	CookedTexture TextureCook::Cook(const uint8_t* rgba, uint32_t width, uint32_t height,
									std::string_view name)
	{
		CookedTexture cooked;
		cooked.Width = width;
		cooked.Height = height;

		const bool isNormal = NameSaysNormal(name);
		const bool isSingleChannel = !isNormal && NameSaysSingleChannel(name);

		// Alpha decides BC1 against BC3 -- but only where alpha is a real
		// channel, which normals and data maps do not have.
		bool alphaVaries = false;
		for (size_t i = 0; i < (size_t)width * height && !alphaVaries; i++)
			alphaVaries = rgba[i * 4 + 3] != 255;

		if (width < 4 || height < 4)
			cooked.Format = CookedPixelFormat::RGBA8;
		else if (isNormal)
			cooked.Format = CookedPixelFormat::BC5;
		else if (isSingleChannel)
			cooked.Format = CookedPixelFormat::BC4;
		else if (alphaVaries)
			cooked.Format = CookedPixelFormat::BC3;
		else
			cooked.Format = CookedPixelFormat::BC1;

		// Colour maps filter in linear space, the way the GPU blit chain
		// filtered their sRGB-created textures; data maps filter raw, the
		// way theirs did as UNORM.
		const bool srgbFilter = cooked.Format == CookedPixelFormat::BC1 ||
								cooked.Format == CookedPixelFormat::BC3;

		FloatImage level = ToFloat(rgba, width, height, srgbFilter);

		while (true)
		{
			// Copied only for normal maps, which are the only ones that get
			// modified before encoding -- renormalizing in place would feed a
			// shortened, re-lengthened vector into the *next* downsample and
			// compound the error down the chain.
			//
			// Everything else encodes straight from `level`. The copy is not
			// small: a 4K mip 0 is 4096x4096x4 floats, 256 MB, so this was
			// a quarter of a gigabyte memcpy'd per texture to be read once
			// and thrown away. It matters most because cooking now runs on
			// several threads at a time, where peak memory is the thing that
			// decides how many threads are affordable.
			if (isNormal)
			{
				FloatImage snapshot = level;
				RenormalizeNormals(snapshot);
				cooked.Mips.push_back(
					EncodeMip(ToBytes(snapshot, srgbFilter), level.Width, level.Height,
							  cooked.Format));
			}
			else
			{
				cooked.Mips.push_back(
					EncodeMip(ToBytes(level, srgbFilter), level.Width, level.Height,
							  cooked.Format));
			}

			if (level.Width == 1 && level.Height == 1)
				break;

			level = Downsample(level);
		}

		return cooked;
	}

	std::vector<uint8_t> TextureCook::Serialize(const CookedTexture& texture)
	{
		std::vector<uint8_t> out;
		out.insert(out.end(), kMagic, kMagic + 4);
		Append(out, kVersion);
		Append(out, texture.Width);
		Append(out, texture.Height);
		Append(out, (uint32_t)texture.Format);
		Append(out, (uint32_t)texture.Mips.size());

		for (const std::vector<uint8_t>& mip : texture.Mips)
		{
			Append(out, (uint64_t)mip.size());
			out.insert(out.end(), mip.begin(), mip.end());
		}

		return out;
	}

	bool TextureCook::IsCooked(const uint8_t* bytes, size_t size)
	{
		return bytes && size >= 4 && std::memcmp(bytes, kMagic, 4) == 0;
	}

	bool TextureCook::Deserialize(CookedTexture& out, const uint8_t* bytes, size_t size)
	{
		if (!IsCooked(bytes, size))
			return false;

		const uint8_t* cursor = bytes + 4;
		const uint8_t* end = bytes + size;

		uint32_t version = 0, format = 0, mipCount = 0;
		CookedTexture texture;

		if (!ReadValue(cursor, end, version) || version != kVersion)
		{
			RV_CORE_ERROR("Cooked texture is version {0}; this engine reads {1}",
						  version, kVersion);
			return false;
		}

		if (!ReadValue(cursor, end, texture.Width) ||
			!ReadValue(cursor, end, texture.Height) ||
			!ReadValue(cursor, end, format) ||
			!ReadValue(cursor, end, mipCount) ||
			mipCount == 0)
			return false;

		texture.Format = (CookedPixelFormat)format;
		texture.Mips.reserve(mipCount);

		for (uint32_t i = 0; i < mipCount; i++)
		{
			uint64_t mipSize = 0;
			if (!ReadValue(cursor, end, mipSize) || (size_t)(end - cursor) < mipSize)
				return false;

			texture.Mips.emplace_back(cursor, cursor + mipSize);
			cursor += mipSize;
		}

		out = std::move(texture);
		return true;
	}

	RHI::Format TextureCook::PixelFormat(CookedPixelFormat format, bool srgb)
	{
		switch (format)
		{
			case CookedPixelFormat::RGBA8:
				return srgb ? RHI::Format::R8G8B8A8_SRGB : RHI::Format::R8G8B8A8_UNORM;
			case CookedPixelFormat::BC1:
				return srgb ? RHI::Format::BC1_SRGB : RHI::Format::BC1_UNORM;
			case CookedPixelFormat::BC3:
				return srgb ? RHI::Format::BC3_SRGB : RHI::Format::BC3_UNORM;
			// No sRGB variants exist, and no data map asks for one.
			case CookedPixelFormat::BC4:
				return RHI::Format::BC4_UNORM;
			case CookedPixelFormat::BC5:
				return RHI::Format::BC5_UNORM;
		}
		return RHI::Format::Undefined;
	}
}
