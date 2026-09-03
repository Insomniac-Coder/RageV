#include <rvpch.h>
#include "IrradianceVolume.h"

#include "RageV/Core/Log.h"

#include <algorithm>

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

	Ref<IrradianceVolume> IrradianceVolume::CreateAtlas(RHIDevice& device,
														const std::vector<Region>& regions)
	{
		if (regions.empty())
			return nullptr;

		// **Boxes packed into a box, not slices stacked along z** (ENGINE-NOTES
		// 7cx). The atlas is as wide and tall as the greediest region -- one
		// texture, one sampler, and OpenGL's sampler budget is why -- and the
		// regions are placed inside it as 3D boxes: each takes the first
		// anchor, in order of depth then height then width, where it fits
		// without overlapping anything already placed. Stacking every region
		// along z alone padded each one to the widest and tallest region's
		// cross-section: the bridge's 2800 m bay box beside its 44 m deck
		// boxes came out fifteen times the size of its cells, 754 MB a file.
		// Placed beside each other in x and y the same regions are 75 MB.
		//
		// Deterministic -- sorted by volume, then by the order given -- so the
		// same regions always pack the same way and a stored file's stamp
		// (the atlas size) means the same layout. The output keeps the given
		// order: every reader indexes the regions by it.
		uint32_t width = 0, height = 0;
		for (const Region& region : regions)
		{
			width = Math::Max(width, region.Width);
			height = Math::Max(height, region.Height);
		}

		std::vector<size_t> order(regions.size());
		for (size_t i = 0; i < order.size(); i++)
			order[i] = i;
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
		{
			const uint64_t va = (uint64_t)regions[a].Width * regions[a].Height * regions[a].Depth;
			const uint64_t vb = (uint64_t)regions[b].Width * regions[b].Height * regions[b].Depth;
			return va > vb;
		});

		struct Placed { uint32_t X, Y, Z, W, H, D; };
		std::vector<Placed> placed;
		std::vector<Region> laid = regions;
		uint32_t depth = 0;
		for (size_t index : order)
		{
			Region& region = laid[index];
			// The candidate corners: the origin, and the three faces beyond
			// every placed box. Tried nearest the origin first, in z, then y,
			// then x, so a region goes beside its neighbours before it goes
			// behind them. The corner past the deepest box always fits, so
			// the search cannot fail.
			struct Corner { uint32_t X, Y, Z; };
			std::vector<Corner> corners{ { 0, 0, 0 } };
			for (const Placed& b : placed)
			{
				corners.push_back({ b.X + b.W, b.Y, b.Z });
				corners.push_back({ b.X, b.Y + b.H, b.Z });
				corners.push_back({ b.X, b.Y, b.Z + b.D });
			}
			std::sort(corners.begin(), corners.end(), [](const Corner& a, const Corner& b)
			{
				if (a.Z != b.Z) return a.Z < b.Z;
				if (a.Y != b.Y) return a.Y < b.Y;
				return a.X < b.X;
			});

			auto fits = [&](const Corner& c)
			{
				if (c.X + region.Width > width || c.Y + region.Height > height)
					return false;
				for (const Placed& b : placed)
				{
					const bool apart = c.X >= b.X + b.W || c.X + region.Width <= b.X
									|| c.Y >= b.Y + b.H || c.Y + region.Height <= b.Y
									|| c.Z >= b.Z + b.D || c.Z + region.Depth <= b.Z;
					if (!apart)
						return false;
				}
				return true;
			};

			Corner chosen{ 0, 0, depth };   // past everything: the fallback that always fits
			for (const Corner& c : corners)
			{
				if (fits(c))
				{
					chosen = c;
					break;
				}
			}
			region.XOffset = chosen.X;
			region.YOffset = chosen.Y;
			region.ZOffset = chosen.Z;
			placed.push_back({ chosen.X, chosen.Y, chosen.Z, region.Width, region.Height, region.Depth });
			depth = Math::Max(depth, chosen.Z + region.Depth);
		}

		Ref<IrradianceVolume> volume = Create(device, width, height, depth);
		if (volume)
			volume->m_Regions = std::move(laid);
		return volume;
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

		{
			TextureDesc desc;
			desc.Type = TextureType::Texture3D;
			desc.Width = x;
			desc.Height = y;
			// **Thirty-five tiles of the field, stacked**: red, green and blue,
			// then thirty-two of the octahedral map of what stands in the way.
			// One texture is one sampler, and a fragment shader has thirty-two
			// of those on OpenGL -- which the layered terrain variant had
			// already nearly spent before this existed.
			desc.Depth = z * kTiles;
			desc.MipLevels = 1;
			// Half floats: the values are irradiance, which is unbounded above
			// and needs more range than eight bits, and four times finer than
			// the difference anyone can see in a diffuse term.
			desc.Format = Format::R16G16B16A16_SFLOAT;
			// **Sampled and storage**: the solve writes these through
			// imageStore and every shader that reads a field samples them.
			//
			// The pair carries a rule with it, and getting it wrong cost a day.
			// A storage-capable image has its sampled descriptor written
			// against VK_IMAGE_LAYOUT_GENERAL, so anything moving it out of
			// that layout and leaving it there makes a mismatch of every later
			// read -- and a CPU upload used to do exactly that. This volume *is*
			// uploaded: stage one fills it flat so a field is never sampled
			// while it holds whatever the allocator left. So the upload path
			// settles a storage texture back in GENERAL rather than read-only;
			// see VulkanTexture::SettledLayout.
			//
			// Validation said nothing about any of it until a scene actually
			// contained a volume, which is "a check that tests the path nothing
			// ships is not a check" arriving a second time. Any run testing
			// this has to load a scene with a volume in it.
			desc.Usage = TextureUsage::Sampled | TextureUsage::Storage;
			desc.DebugName = "irradiance.field";

			volume->m_Texture = device.CreateTexture(desc);
			if (!volume->m_Texture)
			{
				RV_CORE_ERROR("IrradianceVolume: could not create {0}x{1}x{2} volume texture",
							  x, y, z);
				return nullptr;
			}

			// **And its swap partner, for the solve** (see SolveTarget). Same
			// shape, same rule about layouts -- and zero-filled here for the
			// same reason the front is zero-filled by the scene: its first use
			// is a barrier that believes the texture has settled, and a
			// texture never uploaded has not. Zeros are also the honest
			// content -- "no sweep has written this yet".
			desc.DebugName = "irradiance.field.solve";
			volume->m_Solve = device.CreateTexture(desc);
			if (!volume->m_Solve)
			{
				RV_CORE_ERROR("IrradianceVolume: could not create the solve half of a "
							  "{0}x{1}x{2} volume", x, y, z);
				return nullptr;
			}

			const std::vector<uint8_t> zeros(
				(size_t)x * y * z * kTiles * 4 * sizeof(uint16_t), 0);
			volume->m_Solve->Upload(zeros.data(), zeros.size());
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

	bool IrradianceVolume::UploadRaw(const std::vector<uint8_t>& bytes)
	{
		// Four halves a cell, every tile: the same arithmetic the texture was
		// created with, so a mismatch here means the file describes a different
		// volume than this one.
		const size_t expected = CellCount() * 4 * sizeof(uint16_t) * kTiles;
		if (bytes.size() != expected || !m_Texture)
		{
			RV_CORE_WARN("IrradianceVolume: a baked payload of {0} bytes does not fit "
						 "a {1}x{2}x{3} field, which wants {4}",
						 bytes.size(), m_Width, m_Height, m_Depth, expected);
			return false;
		}

		m_Texture->Upload(bytes.data(), bytes.size());
		return true;
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
		// the texture wants them separated into its three tiles, so exactly one
		// of the two has to pay for a repack. Doing it here keeps the fill's
		// loop readable and keeps the scratch buffer's lifetime with the thing
		// it belongs to.
		//
		// **One upload, because the tiles are already contiguous.** A 3D
		// texture takes its data slice by slice, so channel c's slices are
		// exactly the c-th third of the buffer -- the same three runs the loop
		// below writes, back to back.
		//
		// Four halves a cell, because the texture is RGBA16F. Half rather than
		// full float deliberately: linear filtering of a 32-bit float image is
		// not something every device offers, and the hardware blend between
		// cells is the entire reason this is a texture.
		// Every tile, not only the three the cells describe: the two distance
		// tiles have no CPU-side answer and are left at zero, which reads as
		// "nothing is visible from here" and so contributes no light. That is
		// the right default for a field nothing has solved yet -- the same
		// thing its zeroed light says, said twice.
		m_Scratch.assign(cells.size() * 4 * kTiles, 0);
		for (int channel = 0; channel < 3; channel++)
		{
			const size_t tile = (size_t)channel * cells.size() * 4;
			for (size_t i = 0; i < cells.size(); i++)
			{
				const Vec4& sh = channel == 0 ? cells[i].R
							   : channel == 1 ? cells[i].G
											  : cells[i].B;
				m_Scratch[tile + i * 4 + 0] = ToHalf(sh.x);
				m_Scratch[tile + i * 4 + 1] = ToHalf(sh.y);
				m_Scratch[tile + i * 4 + 2] = ToHalf(sh.z);
				m_Scratch[tile + i * 4 + 3] = ToHalf(sh.w);
			}
		}

		m_Texture->Upload(m_Scratch.data(), m_Scratch.size() * sizeof(uint16_t));
	}
}
