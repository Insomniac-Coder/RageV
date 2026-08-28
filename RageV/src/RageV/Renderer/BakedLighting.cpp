#include <rvpch.h>
#include "BakedLighting.h"

#include "RageV/Core/Log.h"
#include "RageV/IO/VFS.h"
#include "RageV/Project/Project.h"

#include <fstream>

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// 'R' 'V' 'B' 'K'. Four bytes that say what a file is before anything
		// reads a size out of it, because the alternative is trusting a
		// filename and allocating whatever the first field happens to say.
		constexpr uint32_t kMagic = 0x4B425652u;

		// **One.** Bumped when the payload's meaning changes -- a different
		// tile layout, a different coefficient convention -- and a loader
		// refuses a version it does not know rather than reading old bytes
		// under new rules. That refusal costs a re-bake; the alternative costs
		// a lighting bug nobody can find.
		// **Two.** Bumped 2026-08-27, when volumes became independent: the
		// stamp gained a Layout hash, and a version-1 field describes one
		// composed box rather than an atlas of them. Refusing costs a re-bake
		// and is the whole reason the field is here -- reading old bytes under
		// new rules is the lighting bug nobody can find.
		// **Three.** Bumped 2026-08-28 for sky occlusion. The field's shape did
		// not change -- seven tiles before and after -- but two meanings moved:
		// the light tiles' alpha carries sky visibility now instead of the
		// aliveness flag, and aliveness lives in tile 6's .y, which was spare
		// and therefore zero in every older file. A version-2 bake would load
		// without complaint and read every cell as buried, so the careful read
		// paths would quietly drop their field light. Same size, same stamp,
		// wrong bytes: exactly the case this refusal exists for.
		constexpr uint32_t kVersion = 3;

		struct Header
		{
			uint32_t Magic = kMagic;
			uint32_t Version = kVersion;
			uint32_t Kind = 0;
			uint32_t Format = 0;
			BakedLighting::Stamp Stamp;
			uint64_t PayloadBytes = 0;
		};

		// --- BC6H, as a file format -------------------------------------------
		//
		// A probe cube is 12.5 MB of RGBA16F, one per lighting, and a scene
		// with a few lightings ships tens of megabytes of what is mostly
		// smooth radiance. BC6H stores the same HDR at a byte a texel -- and
		// smooth is its best case, so a *single-partition* encoder (mode 11:
		// two 10-bit endpoints, sixteen 4-bit indices along the segment
		// between them) is all a probe needs. Encoded when a probe is written,
		// decoded when one is read, so the compression lives entirely in this
		// file: the capture pipeline keeps its renderable RGBA16F cube, old
		// uncompressed files still load by their header's word, and the
		// texture the GPU sees is bit-for-bit what the decoder said.
		//
		// The arithmetic follows the D3D functional spec (unsigned path):
		// unquantize a 10-bit endpoint as q*64+32 (0 and 1023 pinned to the
		// range ends), interpolate with the 4-bit weight table, and scale by
		// 31/64 into half-float bit space. Both directions below share those
		// exact steps, which is what a round-trip test leans on.

		constexpr uint16_t kBc6hWeights[16] = { 0, 4, 9, 13, 17, 21, 26, 30,
												34, 38, 43, 47, 51, 55, 60, 64 };

		uint32_t Bc6hUnquantize(uint32_t q)
		{
			if (q == 0)
				return 0;
			if (q == 1023)
				return 0xFFFF;
			return q * 64 + 32;
		}

		uint16_t Bc6hFinish(uint32_t comp)
		{
			return (uint16_t)((comp * 31) >> 6);
		}

		// A half-float radiance sample as the encoder wants it: non-negative
		// and finite. Probes hold neither negative light nor infinities, but a
		// readback must not be trusted to promise that.
		uint16_t Bc6hSanitize(uint16_t half)
		{
			if (half & 0x8000)
				return 0;
			return half > 0x7BFF ? 0x7BFF : half;
		}

		// The value the interpolator must produce for this half, before the
		// final 31/64 -- the space endpoints and indices are chosen in.
		uint32_t Bc6hPreSpace(uint16_t half)
		{
			const uint32_t pre = ((uint32_t)half * 64 + 15) / 31;
			return pre > 0xFFFF ? 0xFFFF : pre;
		}

		void Bc6hWriteBits(uint8_t* block, uint32_t& cursor, uint32_t value,
						   uint32_t count)
		{
			for (uint32_t i = 0; i < count; i++, cursor++)
			{
				if (value & (1u << i))
					block[cursor >> 3] |= (uint8_t)(1u << (cursor & 7));
			}
		}

		uint32_t Bc6hReadBits(const uint8_t* block, uint32_t& cursor, uint32_t count)
		{
			uint32_t value = 0;
			for (uint32_t i = 0; i < count; i++, cursor++)
			{
				if (block[cursor >> 3] & (1u << (cursor & 7)))
					value |= 1u << i;
			}
			return value;
		}

		// One 4x4 block from 16 RGB half triples, texel 0 first.
		void Bc6hEncodeBlock(const uint16_t texels[16][3], uint8_t block[16])
		{
			std::memset(block, 0, 16);

			// Endpoints: the component-wise box of the block, in pre-space.
			// A diagonal fit would buy a little on hard chroma edges; a probe
			// has none worth the code.
			uint32_t lo[3], hi[3];
			uint32_t pre[16][3];
			for (int c = 0; c < 3; c++)
			{
				lo[c] = 0xFFFF;
				hi[c] = 0;
			}
			for (int t = 0; t < 16; t++)
			{
				for (int c = 0; c < 3; c++)
				{
					pre[t][c] = Bc6hPreSpace(texels[t][c]);
					lo[c] = Math::Min(lo[c], pre[t][c]);
					hi[c] = Math::Max(hi[c], pre[t][c]);
				}
			}

			// Quantized endpoints, then the palette ends the *decoder* will
			// use -- indices are chosen against what will be reconstructed,
			// not against what was wished for.
			uint32_t qa[3], qb[3], ua[3], ub[3];
			for (int c = 0; c < 3; c++)
			{
				qa[c] = Math::Min(lo[c] >> 6, 1023u);
				qb[c] = Math::Min(hi[c] >> 6, 1023u);
				ua[c] = Bc6hUnquantize(qa[c]);
				ub[c] = Bc6hUnquantize(qb[c]);
			}

			// Each texel's place along the segment, as a 4-bit index.
			uint32_t indices[16];
			int64_t axis[3] = { (int64_t)ub[0] - ua[0], (int64_t)ub[1] - ua[1],
								(int64_t)ub[2] - ua[2] };
			const int64_t lengthSq = axis[0] * axis[0] + axis[1] * axis[1]
								   + axis[2] * axis[2];
			for (int t = 0; t < 16; t++)
			{
				if (lengthSq == 0)
				{
					indices[t] = 0;
					continue;
				}
				int64_t along = 0;
				for (int c = 0; c < 3; c++)
					along += ((int64_t)pre[t][c] - ua[c]) * axis[c];
				int64_t index = (along * 15 + lengthSq / 2) / lengthSq;
				indices[t] = (uint32_t)Math::Clamp<int64_t>(index, 0, 15);
			}

			// The anchor texel's top bit is not stored, so it must be zero:
			// swap the endpoints and mirror every index when it is not.
			if (indices[0] > 7)
			{
				for (int c = 0; c < 3; c++)
					std::swap(qa[c], qb[c]);
				for (int t = 0; t < 16; t++)
					indices[t] = 15 - indices[t];
			}

			uint32_t cursor = 0;
			Bc6hWriteBits(block, cursor, 0x03, 5);   // mode 11
			Bc6hWriteBits(block, cursor, qa[0], 10);
			Bc6hWriteBits(block, cursor, qa[1], 10);
			Bc6hWriteBits(block, cursor, qa[2], 10);
			Bc6hWriteBits(block, cursor, qb[0], 10);
			Bc6hWriteBits(block, cursor, qb[1], 10);
			Bc6hWriteBits(block, cursor, qb[2], 10);
			Bc6hWriteBits(block, cursor, indices[0], 3);
			for (int t = 1; t < 16; t++)
				Bc6hWriteBits(block, cursor, indices[t], 4);
		}

		void Bc6hDecodeBlock(const uint8_t block[16], uint16_t texels[16][3])
		{
			uint32_t cursor = 0;
			const uint32_t mode = Bc6hReadBits(block, cursor, 5);

			uint32_t qa[3], qb[3];
			if (mode != 0x03)
			{
				// Only what the encoder above writes. Anything else in a file
				// this engine stamped is corruption, and black is the honest
				// answer for a block nothing can interpret.
				for (int t = 0; t < 16; t++)
					texels[t][0] = texels[t][1] = texels[t][2] = 0;
				return;
			}

			for (int c = 0; c < 3; c++)
				qa[c] = Bc6hReadBits(block, cursor, 10);
			for (int c = 0; c < 3; c++)
				qb[c] = Bc6hReadBits(block, cursor, 10);

			uint16_t palette[16][3];
			for (int c = 0; c < 3; c++)
			{
				const uint32_t a = Bc6hUnquantize(qa[c]);
				const uint32_t b = Bc6hUnquantize(qb[c]);
				for (int i = 0; i < 16; i++)
				{
					const uint32_t w = kBc6hWeights[i];
					palette[i][c] = Bc6hFinish((a * (64 - w) + b * w + 32) >> 6);
				}
			}

			for (int t = 0; t < 16; t++)
			{
				const uint32_t index =
					t == 0 ? Bc6hReadBits(block, cursor, 3)
						   : Bc6hReadBits(block, cursor, 4);
				for (int c = 0; c < 3; c++)
					texels[t][c] = palette[index][c];
			}
		}

		// RGBA16F faces in, BC6H blocks out. Faces are `depth` layers of
		// `width` x `height`, exactly as ReadTexture returns a cube.
		std::vector<uint8_t> Bc6hEncode(const std::vector<uint8_t>& raw,
										uint32_t width, uint32_t height,
										uint32_t depth)
		{
			const uint32_t blocksX = (width + 3) / 4;
			const uint32_t blocksY = (height + 3) / 4;
			std::vector<uint8_t> packed((size_t)blocksX * blocksY * depth * 16, 0);
			const uint16_t* halves = reinterpret_cast<const uint16_t*>(raw.data());

			size_t out = 0;
			for (uint32_t layer = 0; layer < depth; layer++)
			{
				const uint16_t* face = halves + (size_t)layer * width * height * 4;
				for (uint32_t by = 0; by < blocksY; by++)
				{
					for (uint32_t bx = 0; bx < blocksX; bx++)
					{
						uint16_t texels[16][3];
						for (uint32_t t = 0; t < 16; t++)
						{
							// Clamped to the edge, so a partial block at an odd
							// size repeats its last row rather than reading air.
							const uint32_t x = Math::Min(bx * 4 + (t & 3), width - 1);
							const uint32_t y = Math::Min(by * 4 + (t >> 2), height - 1);
							const uint16_t* texel = face + ((size_t)y * width + x) * 4;
							for (int c = 0; c < 3; c++)
								texels[t][c] = Bc6hSanitize(texel[c]);
						}
						Bc6hEncodeBlock(texels, packed.data() + out);
						out += 16;
					}
				}
			}
			return packed;
		}

		std::vector<uint8_t> Bc6hDecode(const std::vector<uint8_t>& packed,
										uint32_t width, uint32_t height,
										uint32_t depth)
		{
			const uint32_t blocksX = (width + 3) / 4;
			const uint32_t blocksY = (height + 3) / 4;
			std::vector<uint8_t> raw((size_t)width * height * depth * 4 * sizeof(uint16_t), 0);
			uint16_t* halves = reinterpret_cast<uint16_t*>(raw.data());

			size_t in = 0;
			for (uint32_t layer = 0; layer < depth; layer++)
			{
				uint16_t* face = halves + (size_t)layer * width * height * 4;
				for (uint32_t by = 0; by < blocksY; by++)
				{
					for (uint32_t bx = 0; bx < blocksX; bx++)
					{
						uint16_t texels[16][3];
						Bc6hDecodeBlock(packed.data() + in, texels);
						in += 16;
						for (uint32_t t = 0; t < 16; t++)
						{
							const uint32_t x = bx * 4 + (t & 3);
							const uint32_t y = by * 4 + (t >> 2);
							if (x >= width || y >= height)
								continue;
							uint16_t* texel = face + ((size_t)y * width + x) * 4;
							texel[0] = texels[t][0];
							texel[1] = texels[t][1];
							texel[2] = texels[t][2];
							texel[3] = 0x3C00;   // 1.0; BC6H carries no alpha
						}
					}
				}
			}
			return raw;
		}
	}

	bool BakedLighting::Stamp::Matches(const Stamp& other) const
	{
		// Positions compared with a tolerance and everything else exactly. A
		// transform that round-trips through a text scene file comes back a few
		// ulps out; a grid size does not, and a lighting hash certainly does
		// not.
		const float kEpsilon = 1.0e-4f;
		return Math::Distance(Centre, other.Centre) <= kEpsilon
			&& Math::Distance(Extents, other.Extents) <= kEpsilon
			&& Math::Distance(AxisX, other.AxisX) <= kEpsilon
			&& Math::Distance(AxisY, other.AxisY) <= kEpsilon
			&& Width == other.Width && Height == other.Height
			&& Depth == other.Depth && Tiles == other.Tiles
			&& Lighting == other.Lighting
			// The arrangement inside the atlas, which the dimensions above
			// cannot speak for: same texture, different boxes.
			&& Layout == other.Layout;
	}

	std::filesystem::path BakedLighting::DirectoryFor(const std::string& sceneName)
	{
		// **Relative to the asset root, because that is what the VFS speaks.**
		//
		// A packaged game serves its assets out of a pak, and a path into the
		// filesystem finds nothing there -- which is exactly how this shipped
		// broken: it read bakes with an ifstream, worked in the editor and on a
		// loose project, and could not find a single field in a built game. The
		// scene loader had the answer written beside it already: "a scene in a
		// shipped pak and a scene on disk are the same call".
		//
		// So the key is `baked/<scene>/...`, the same shape as `scenes/x.rage`,
		// and the VFS resolves it against a pak or against loose files without
		// the caller knowing which. Writing takes the absolute path instead --
		// only the editor and the bake tool write, and they write to disk.
		std::filesystem::path scene(sceneName);
		return Project::AssetRoot() / "baked" / scene.stem();
	}

	std::filesystem::path BakedLighting::WritePathFor(const std::filesystem::path& key)
	{
		return Project::AssetRoot() / key;
	}

	bool BakedLighting::Write(RHIDevice& device, const std::filesystem::path& file,
							  Kind kind, const Stamp& stamp,
							  const Ref<RHITexture>& texture)
	{
		if (!texture)
		{
			RV_CORE_ERROR("Bake: nothing to write for {0}", file.string());
			return false;
		}

		std::vector<uint8_t> payload;
		if (!device.ReadTexture(texture, payload) || payload.empty())
		{
			RV_CORE_ERROR("Bake: could not read {0} back from the device",
						  texture->GetDesc().DebugName);
			return false;
		}

		// **A probe cube ships as BC6H** -- see the codec above. The field
		// does not: its alpha lane is the alive flag and one tile is a
		// visibility bitmask, and lossy blocks would corrupt exactly the
		// leak-prevention data. Only taken when the payload is the exact
		// RGBA16F cube expected, so an unexpected capture format falls
		// through and stores raw rather than garbling.
		RHI::Format storedAs = texture->GetDesc().Format;
		if (kind == Kind::ReflectionProbe
			&& storedAs == RHI::Format::R16G16B16A16_SFLOAT
			&& payload.size() == (size_t)stamp.Width * stamp.Height * stamp.Depth
								 * 4 * sizeof(uint16_t))
		{
			const size_t rawBytes = payload.size();
			payload = Bc6hEncode(payload, stamp.Width, stamp.Height, stamp.Depth);
			storedAs = RHI::Format::BC6H_UFLOAT;
			RV_CORE_INFO("Bake: probe cube compressed BC6H, {0} -> {1} KB",
						 (rawBytes + 1023) / 1024, (payload.size() + 1023) / 1024);
		}

		const std::filesystem::path onDisk = file;

		std::error_code code;
		std::filesystem::create_directories(onDisk.parent_path(), code);

		std::ofstream out(onDisk, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			RV_CORE_ERROR("Bake: could not open {0} for writing", file.string());
			return false;
		}

		Header header;
		header.Kind = (uint32_t)kind;
		header.Format = (uint32_t)storedAs;
		header.Stamp = stamp;
		header.PayloadBytes = payload.size();

		out.write(reinterpret_cast<const char*>(&header), sizeof(header));
		out.write(reinterpret_cast<const char*>(payload.data()),
				  (std::streamsize)payload.size());
		if (!out)
		{
			RV_CORE_ERROR("Bake: {0} was not written completely", file.string());
			return false;
		}

		RV_CORE_INFO("Bake: wrote {0} ({1} KB, {2}x{3}x{4})", file.filename().string(),
					 (payload.size() + 1023) / 1024, stamp.Width, stamp.Height,
					 stamp.Depth);
		return true;
	}

	bool BakedLighting::Read(const std::filesystem::path& file, Kind kind,
							 Stamp& stamp, std::vector<uint8_t>& payload)
	{
		// Through the VFS, so a bake inside a shipped pak and one loose on disk
		// are the same call.
		std::vector<uint8_t> bytes;
		if (!VFS::ReadBytes(file, bytes))
			return false;       // no bake for this one, which is not an error

		if (bytes.size() < sizeof(Header))
		{
			RV_CORE_WARN("Bake: {0} is too short to be one", file.string());
			return false;
		}

		Header header;
		std::memcpy(&header, bytes.data(), sizeof(header));
		if (header.Magic != kMagic)
		{
			RV_CORE_WARN("Bake: {0} is not a baked lighting file", file.string());
			return false;
		}

		if (header.Version != kVersion)
		{
			RV_CORE_WARN("Bake: {0} is version {1} and this build reads {2}; it will "
						 "be solved at runtime instead", file.filename().string(),
						 header.Version, kVersion);
			return false;
		}

		if (header.Kind != (uint32_t)kind)
		{
			RV_CORE_WARN("Bake: {0} holds a different kind of lighting than the one "
						 "asked for", file.filename().string());
			return false;
		}

		// Bounded before it is trusted: the size in the header decides an
		// allocation, and a corrupt file must not be able to ask for a
		// terabyte. Sixty-four megabytes is far past any field this engine can
		// build and far short of anything that hurts.
		constexpr uint64_t kSaneLimit = 64ull * 1024 * 1024;
		if (header.PayloadBytes == 0 || header.PayloadBytes > kSaneLimit)
		{
			RV_CORE_WARN("Bake: {0} claims a {1} byte payload, which is not credible",
						 file.filename().string(), header.PayloadBytes);
			return false;
		}

		if (bytes.size() - sizeof(Header) < header.PayloadBytes)
		{
			RV_CORE_WARN("Bake: {0} is shorter than its header says", file.string());
			return false;
		}

		payload.assign(bytes.begin() + sizeof(Header),
					   bytes.begin() + sizeof(Header) + (size_t)header.PayloadBytes);

		// **A compressed probe decodes here**, so every caller keeps meeting
		// the raw RGBA16F cube it always has -- the compression begins and
		// ends at this file boundary. The size is checked against the block
		// arithmetic first: a truncated payload must fail as a bad file, not
		// as a read past the end.
		if (header.Format == (uint32_t)RHI::Format::BC6H_UFLOAT)
		{
			const Stamp& dims = header.Stamp;
			const uint64_t blocks = (uint64_t)((dims.Width + 3) / 4)
								  * ((dims.Height + 3) / 4) * dims.Depth;
			if (kind != Kind::ReflectionProbe || payload.size() != blocks * 16)
			{
				RV_CORE_WARN("Bake: {0} says BC6H but its payload does not fit "
							 "{1}x{2}x{3}", file.filename().string(),
							 dims.Width, dims.Height, dims.Depth);
				return false;
			}
			payload = Bc6hDecode(payload, dims.Width, dims.Height, dims.Depth);
		}

		stamp = header.Stamp;
		return true;
	}
}
