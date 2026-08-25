#include <rvpch.h>
#include "IrradianceVolume.h"

#include "RageV/Core/Log.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// IEEE 754 binary32 to binary16.
		//
		// CubeLut.cpp has one of these too, and it stays there: its comment
		// scopes it to LUT entries -- "small, finite and non-negative in every
		// file anyone will load" -- and leans on that to handle the awkward
		// cases by clamping. Half of what arrives here is an SH L1 coefficient,
		// which is routinely negative, so that contract does not cover this and
		// widening it would make one function quietly responsible for two sets
		// of assumptions. Two short converters with honest comments beat one
		// with a footnote.
		uint16_t ToHalf(float value)
		{
			// Clamped to binary16's largest finite value. An irradiance beyond
			// this is not a lighting value, it is a bug upstream, and pinning
			// it is better than emitting an infinity that poisons every cell
			// it is interpolated with.
			value = Math::Clamp(value, -65504.0f, 65504.0f);

			uint32_t bits;
			std::memcpy(&bits, &value, sizeof(bits));

			const uint32_t sign = (bits >> 16) & 0x8000u;
			int exponent = (int)((bits >> 23) & 0xFFu) - 127 + 15;
			uint32_t mantissa = bits & 0x007FFFFFu;

			if (exponent <= 0)
			{
				// Underflows binary16's smallest normal. Flushed to zero:
				// a denormal irradiance is a value no shading can tell from
				// nothing, and the subnormal encoding is where these routines
				// are usually got wrong.
				return (uint16_t)sign;
			}
			if (exponent >= 31)
				return (uint16_t)(sign | 0x7BFFu);   // the clamp above, in bits

			return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
		}
	}

	Ref<IrradianceVolume> IrradianceVolume::Create(RHIDevice& device,
												   uint32_t x, uint32_t y, uint32_t z)
	{
		// One cell a side is legal and useless; zero is neither, and would
		// make a texture the driver refuses.
		x = Math::Max(x, 1u);
		y = Math::Max(y, 1u);
		z = Math::Max(z, 1u);

		Ref<IrradianceVolume> volume = std::make_shared<IrradianceVolume>();
		volume->m_Width = x;
		volume->m_Height = y;
		volume->m_Depth = z;

		static const char* kNames[3] = { "irradiance.r", "irradiance.g", "irradiance.b" };
		for (int i = 0; i < 3; i++)
		{
			TextureDesc desc;
			desc.Type = TextureType::Texture3D;
			desc.Width = x;
			desc.Height = y;
			desc.Depth = z;
			desc.MipLevels = 1;
			// Half floats: the values are irradiance, which is unbounded above
			// and needs more range than eight bits, and four times finer than
			// the difference anyone can see in a diffuse term.
			desc.Format = Format::R16G16B16A16_SFLOAT;
			// Sampled now; Storage as well because the fill that replaces the
			// placeholder one is a compute pass writing straight into these.
			desc.Usage = TextureUsage::Sampled | TextureUsage::Storage;
			desc.DebugName = kNames[i];

			volume->m_Channels[i] = device.CreateTexture(desc);
			if (!volume->m_Channels[i])
			{
				RV_CORE_ERROR("IrradianceVolume: could not create {0}x{1}x{2} volume texture",
							  x, y, z);
				return nullptr;
			}
		}

		SamplerDesc sampler;
		sampler.MinFilter = FilterMode::Linear;
		sampler.MagFilter = FilterMode::Linear;
		// Clamped on all three axes. A sample just outside the box should read
		// the nearest cell it has, not wrap round to the far side of the room
		// -- which is what Repeat would do, and it would do it silently.
		sampler.WrapU = WrapMode::ClampToEdge;
		sampler.WrapV = WrapMode::ClampToEdge;
		sampler.WrapW = WrapMode::ClampToEdge;
		volume->m_Sampler = device.CreateSampler(sampler);

		return volume->m_Sampler ? volume : nullptr;
	}

	void IrradianceVolume::Upload(const std::vector<Cell>& cells)
	{
		if (cells.size() != CellCount())
		{
			RV_CORE_ERROR("IrradianceVolume: uploaded {0} cells for a volume of {1}",
						  cells.size(), CellCount());
			return;
		}

		// Split channel by channel. The cells arrive interleaved because that
		// is how they are computed -- one point's whole answer at a time -- and
		// the textures want them separated, so exactly one of the two has to
		// pay for a repack. Doing it here keeps the fill's loop readable and
		// keeps the scratch buffer's lifetime with the thing it belongs to.
		// Four halves a cell, because the textures are RGBA16F. Half rather
		// than full float deliberately: linear filtering of a 32-bit float
		// image is not something every device offers, and the hardware blend
		// between cells is the entire reason this is a texture.
		m_Scratch.resize(cells.size() * 4);
		for (int channel = 0; channel < 3; channel++)
		{
			for (size_t i = 0; i < cells.size(); i++)
			{
				const Vec4& sh = channel == 0 ? cells[i].R
							   : channel == 1 ? cells[i].G
											  : cells[i].B;
				m_Scratch[i * 4 + 0] = ToHalf(sh.x);
				m_Scratch[i * 4 + 1] = ToHalf(sh.y);
				m_Scratch[i * 4 + 2] = ToHalf(sh.z);
				m_Scratch[i * 4 + 3] = ToHalf(sh.w);
			}

			m_Channels[channel]->Upload(m_Scratch.data(),
										m_Scratch.size() * sizeof(uint16_t));
		}
	}
}
