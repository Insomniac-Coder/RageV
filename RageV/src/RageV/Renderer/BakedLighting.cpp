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
		constexpr uint32_t kVersion = 1;

		struct Header
		{
			uint32_t Magic = kMagic;
			uint32_t Version = kVersion;
			uint32_t Kind = 0;
			uint32_t Format = 0;
			BakedLighting::Stamp Stamp;
			uint64_t PayloadBytes = 0;
		};
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
			&& Lighting == other.Lighting;
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
		header.Format = (uint32_t)texture->GetDesc().Format;
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

		stamp = header.Stamp;
		return true;
	}
}
