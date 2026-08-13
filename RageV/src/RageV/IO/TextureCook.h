#pragma once
#include "RageV/Renderer/RHI/RHITypes.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace RageV::IO
{
	// What a cooked texture holds. A subset of RHI::Format on purpose: the
	// container stores *data*, and whether that data is viewed as sRGB stays
	// a load-time decision, exactly as it is for a PNG -- so there are no
	// sRGB variants here, only layouts.
	enum class CookedPixelFormat : uint32_t
	{
		RGBA8 = 0,
		BC1,
		BC3,
		BC4,
		BC5,
	};

	struct CookedTexture
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		CookedPixelFormat Format = CookedPixelFormat::RGBA8;
		// Mip 0 first, then down to 1x1. Never empty after a successful cook
		// or read -- a cooked texture's chain is the whole point of it.
		std::vector<std::vector<uint8_t>> Mips;
	};

	// Turns decoded RGBA8 pixels into the form a shipped game uploads
	// without decoding: a full mip chain, block-compressed. See ENGINE-NOTES
	// 7i for the design and for what the encode choice keys on.
	class TextureCook
	{
	public:
		// The encode, chosen from the file's name and its pixels:
		//   *_normal*                          -> BC5, mips renormalized
		//   *_roughness/_metallic/_ao/
		//    _occlusion/_height/_displacement  -> BC4, from the red channel
		//   alpha that varies                  -> BC3
		//   everything else                    -> BC1
		// Name conventions, not guesses: they are how every pipeline in this
		// repository already names its maps, and content alone cannot answer
		// -- a grey smoke sprite is not a roughness map, and cooking it BC4
		// would sample back red.
		//
		// Textures smaller than a block cook as RGBA8; compressing a 2x2
		// into a padded 4x4 block stores more bytes to say less.
		static CookedTexture Cook(const uint8_t* rgba, uint32_t width, uint32_t height,
								  std::string_view name);

		// The on-disk form: "RVTX", version, dimensions, format, mip count,
		// then each mip as size + bytes.
		static std::vector<uint8_t> Serialize(const CookedTexture& texture);
		static bool Deserialize(CookedTexture& out, const uint8_t* bytes, size_t size);

		// Whether these bytes are a cooked texture -- the sniff loaders do
		// before handing anything to stb.
		static bool IsCooked(const uint8_t* bytes, size_t size);

		// The RHI format this data uploads as, given the colour space the
		// slot asked for. BC4/BC5 have no sRGB variant and are never asked
		// for one -- data maps load linear.
		static RHI::Format PixelFormat(CookedPixelFormat format, bool srgb);
	};
}
