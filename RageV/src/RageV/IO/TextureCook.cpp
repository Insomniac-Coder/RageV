#include <rvpch.h>
#include "TextureCook.h"
#include "RageV/Core/Log.h"

#include "stb_dxt.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

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

		// The transfer functions themselves. Kept as the definition the tables
		// below are built from, and as what a check compares against -- a
		// lookup table whose reference lives only in a comment is a table
		// nobody can prove.
		float SrgbToLinear(float v)
		{
			return v <= 0.04045f ? v / 12.92f : Math::Pow((v + 0.055f) / 1.055f, 2.4f);
		}

		float LinearToSrgb(float v)
		{
			return v <= 0.0031308f ? v * 12.92f : 1.055f * Math::Pow(v, 1.0f / 2.4f) - 0.055f;
		}

		// --- the same transfers, without the transcendentals ------------------
		//
		// These loops used to call `std::pow` per channel for the transfer
		// function and `std::lround` four times per texel -- 67 million libm
		// calls for one 4K map. Everything below is **exactly** equal to what
		// it replaces, not an approximation: the whole project's cooked
		// output hashes identically before and after, which is the only
		// standard worth holding a change to shipped content to.
		//
		// Packaging the sample project: **12.84s to 9.70s**, a quarter of the
		// time, with the pak byte-for-byte unchanged.
		//
		// Two things about how that number was arrived at are worth more than
		// the code, because both cost real time here:
		//
		// **The profile that started this was taken on battery.** Under power
		// saver the CPU is throttled while memory is not, so arithmetic looked
		// like 72% of cooking. On mains it is a much smaller share. Profile in
		// the power state you actually care about, or you will optimise the
		// wrong thing -- as happened here.
		//
		// **The first attempt measured as zero and was nearly abandoned.** It
		// replaced `pow` with a *binary search* over the thresholds, and eight
		// unpredictable branches cost about what the `pow` did, so the gain in
		// `ToFloat` was handed straight back in `ToBytes`. The fix was not
		// more threads or fewer instructions but *removing the branches* --
		// see `SrgbCandidates`.

		// Byte to linear float. Exact by construction: `ToFloat`'s input is
		// always some byte over 255, so 256 entries cover every input there is.
		const std::array<float, 256>& ByteToLinear()
		{
			static const std::array<float, 256> table = []
			{
				std::array<float, 256> t{};
				for (int i = 0; i < 256; i++)
					t[i] = SrgbToLinear(i / 255.0f);
				return t;
			}();
			return table;
		}

		// The same for content that is not colour, where the transfer is the
		// identity and only the divide remains.
		const std::array<float, 256>& ByteToUnit()
		{
			static const std::array<float, 256> table = []
			{
				std::array<float, 256> t{};
				for (int i = 0; i < 256; i++)
					t[i] = i / 255.0f;
				return t;
			}();
			return table;
		}

		// What the old code answered, kept as the definition of correct.
		uint8_t ReferenceSrgbByte(float v)
		{
			return (uint8_t)std::lround(
				Math::Clamp(LinearToSrgb(v), 0.0f, 1.0f) * 255.0f);
		}

		// Going back is the interesting direction, because the input is a
		// continuous float rather than one of 256 values.
		//
		// The trick is that the *output* is a byte: there are only 255 places
		// where it changes, so 255 thresholds describe the function exactly.
		// `t[i]` is the smallest value whose encoded byte exceeds i, and the
		// answer is then the number of thresholds at or below v.
		//
		// **The thresholds are found by asking the reference, not by
		// inverting it.** The obvious construction -- carry the midpoint
		// `(i + 0.5) / 255` back through `SrgbToLinear` -- is right in real
		// arithmetic and wrong in floating point, because `LinearToSrgb` and
		// `SrgbToLinear` are not exact inverses: a value sitting on a
		// rounding boundary lands on the far side of it. That is not
		// theoretical. It was the first thing tried here, and it moved
		// **2 bytes in 190 MB** of cooked output -- small enough to hide
		// under any pixel tolerance, and still a silent change to shipped
		// content.
		//
		// Bisecting the float bit pattern works because positive floats
		// increase monotonically as integers, so this finds the exact
		// crossing rather than one near it. 255 searches of 31 steps, once,
		// on first use.
		//
		// 256 entries rather than 255: the last is an infinity, so the
		// correction below can index it unconditionally instead of testing
		// for the top of the range.
		const std::array<float, 256>& SrgbStepThresholds()
		{
			static const std::array<float, 256> table = []
			{
				constexpr uint32_t kZero = 0x00000000u;   // 0.0f
				constexpr uint32_t kOne  = 0x3F800000u;   // 1.0f

				std::array<float, 256> t{};
				for (int i = 0; i < 255; i++)
				{
					uint32_t low = kZero, high = kOne;
					while (low < high)
					{
						const uint32_t middle = low + (high - low) / 2;
						float value;
						std::memcpy(&value, &middle, sizeof(value));

						if (ReferenceSrgbByte(value) > i)
							high = middle;
						else
							low = middle + 1;
					}

					std::memcpy(&t[i], &low, sizeof(float));
				}

				t[255] = std::numeric_limits<float>::infinity();
				return t;
			}();
			return table;
		}

		// A candidate byte, looked up directly rather than searched for.
		//
		// **The first version of this binary-searched the thresholds, and that
		// is why the whole optimisation measured as nothing.** Eight
		// unpredictable branches per channel cost about what the `std::pow`
		// they replaced did, so the win in `ToFloat` was handed straight back
		// in `ToBytes` and the two cancelled to within noise.
		//
		// Entry k holds the answer for the *lowest* value in its bucket.
		// Because the function only ever increases, that is never above the
		// true answer, and because a bucket is narrower than the closest two
		// thresholds ever come, it is never more than one below -- so one
		// comparison finishes it, with no branch.
		//
		// 4096 buckets is the size that argument requires. The thresholds are
		// tightest in the darks, where sRGB is a straight line and they sit
		// `1/3294.6` apart -- about 3.04e-4 -- and a bucket here is 2.44e-4
		// wide. It is also 4 KB, which stays in L1 while the loop streams a
		// quarter of a gigabyte past it.
		constexpr uint32_t kCandidateBits = 12;
		constexpr uint32_t kCandidateCount = 1u << kCandidateBits;   // 4096

		const std::array<uint8_t, kCandidateCount>& SrgbCandidates()
		{
			static const std::array<uint8_t, kCandidateCount> table = []
			{
				std::array<uint8_t, kCandidateCount> t{};
				for (uint32_t k = 0; k < kCandidateCount; k++)
					t[k] = ReferenceSrgbByte((float)k / (float)(kCandidateCount - 1));
				return t;
			}();
			return table;
		}

		uint8_t LinearToSrgbByte(float v)
		{
			const float clamped = Math::Clamp(v, 0.0f, 1.0f);
			const uint32_t index = (uint32_t)(clamped * (float)(kCandidateCount - 1));

			const uint8_t candidate = SrgbCandidates()[index];
			// Compiles to a compare and an add, not a jump. The infinity in
			// the last slot is what lets 255 take this path too.
			return (uint8_t)(candidate + (clamped >= SrgbStepThresholds()[candidate] ? 1 : 0));
		}

		// `lround` rounds half away from zero; after the clamp the value is
		// never negative, so adding a half and truncating is the same answer
		// for every input -- and is a couple of instructions rather than a
		// call into the CRT that honours the current rounding mode.
		uint8_t UnitToByte(float v)
		{
			return (uint8_t)(Math::Clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
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

			// Chosen once, outside the loop, rather than branching per channel.
			const std::array<float, 256>& colour =
				srgbFilter ? ByteToLinear() : ByteToUnit();
			// Alpha is coverage and filters linearly whatever the colour
			// channels do.
			const std::array<float, 256>& alpha = ByteToUnit();

			for (size_t i = 0; i < (size_t)width * height; i++)
			{
				image.Pixels[i * 4 + 0] = colour[rgba[i * 4 + 0]];
				image.Pixels[i * 4 + 1] = colour[rgba[i * 4 + 1]];
				image.Pixels[i * 4 + 2] = colour[rgba[i * 4 + 2]];
				image.Pixels[i * 4 + 3] = alpha[rgba[i * 4 + 3]];
			}

			return image;
		}

		std::vector<uint8_t> ToBytes(const FloatImage& image, bool srgbFilter)
		{
			std::vector<uint8_t> bytes((size_t)image.Width * image.Height * 4);
			const size_t texels = (size_t)image.Width * image.Height;

			// Two loops rather than one with a branch inside it: the test is
			// the same for every texel in the image, and hoisting it lets each
			// loop be a straight run over memory.
			if (srgbFilter)
			{
				for (size_t i = 0; i < texels; i++)
				{
					bytes[i * 4 + 0] = LinearToSrgbByte(image.Pixels[i * 4 + 0]);
					bytes[i * 4 + 1] = LinearToSrgbByte(image.Pixels[i * 4 + 1]);
					bytes[i * 4 + 2] = LinearToSrgbByte(image.Pixels[i * 4 + 2]);
					bytes[i * 4 + 3] = UnitToByte(image.Pixels[i * 4 + 3]);
				}
			}
			else
			{
				for (size_t i = 0; i < texels; i++)
				{
					bytes[i * 4 + 0] = UnitToByte(image.Pixels[i * 4 + 0]);
					bytes[i * 4 + 1] = UnitToByte(image.Pixels[i * 4 + 1]);
					bytes[i * 4 + 2] = UnitToByte(image.Pixels[i * 4 + 2]);
					bytes[i * 4 + 3] = UnitToByte(image.Pixels[i * 4 + 3]);
				}
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
			mip.Width = Math::Max(source.Width / 2, 1u);
			mip.Height = Math::Max(source.Height / 2, 1u);
			mip.Pixels.resize((size_t)mip.Width * mip.Height * 4);

			for (uint32_t y = 0; y < mip.Height; y++)
			{
				for (uint32_t x = 0; x < mip.Width; x++)
				{
					const uint32_t x0 = Math::Min(x * 2, source.Width - 1);
					const uint32_t x1 = Math::Min(x * 2 + 1, source.Width - 1);
					const uint32_t y0 = Math::Min(y * 2, source.Height - 1);
					const uint32_t y1 = Math::Min(y * 2 + 1, source.Height - 1);

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
				float z = Math::Sqrt(Math::Max(zSquared, 0.0f));

				const float length = Math::Sqrt(x * x + y * y + z * z);
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
					const uint32_t sx = Math::Min(blockX * 4 + x, width - 1);
					const uint32_t sy = Math::Min(blockY * 4 + y, height - 1);
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

	uint8_t TextureCook::EncodeSrgbByte(float linear)
	{
		return LinearToSrgbByte(linear);
	}

	uint8_t TextureCook::EncodeSrgbByteReference(float linear)
	{
		return ReferenceSrgbByte(linear);
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
