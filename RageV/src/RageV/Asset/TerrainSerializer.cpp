#include <rvpch.h>
#include "TerrainSerializer.h"
#include "RageV/IO/VFS.h"
#include "RageV/Core/Log.h"

#include <cstring>
#include <fstream>

namespace RageV::Assets
{
	namespace
	{
		constexpr char kMagic[4] = { 'R', 'V', 'T', 'R' };
		constexpr size_t kHeaderBytes = 4 + 4 + 4 + 4 + 16;

		uint32_t ReadU32(const uint8_t* at)
		{
			uint32_t value = 0;
			std::memcpy(&value, at, sizeof(value));
			return value;
		}

		void WriteU32(std::ofstream& file, uint32_t value)
		{
			file.write(reinterpret_cast<const char*>(&value), sizeof(value));
		}
	}

	bool TerrainSerializer::Load(TerrainData& out, const std::filesystem::path& path)
	{
		std::vector<uint8_t> bytes;
		if (!IO::VFS::ReadBytes(path, bytes))
			return false;

		if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), kMagic, 4) != 0)
		{
			RV_CORE_ERROR("Terrain {0} is not an .rvterrain (bad magic or too short)", path.string());
			return false;
		}

		const uint32_t version = ReadU32(bytes.data() + 4);
		const uint32_t resolution = ReadU32(bytes.data() + 8);
		const uint32_t layers = ReadU32(bytes.data() + 12);

		if (version != kVersion)
		{
			RV_CORE_ERROR("Terrain {0} is version {1}; this build reads version {2}",
						  path.string(), version, kVersion);
			return false;
		}
		if (!TerrainData::IsValidResolution(resolution))
		{
			RV_CORE_ERROR("Terrain {0} has resolution {1}; it must be 2^n + 1 between {2} and {3}",
						  path.string(), resolution, TerrainData::kMinResolution,
						  TerrainData::kMaxResolution);
			return false;
		}
		// Zero layers is a stage-1 file: heights and nothing after them. Four
		// is a painted one: an RGBA8 weight per sample follows the heights
		// (ENGINE-NOTES 7aq). Anything else is a file from some other build;
		// refusing is better than reading its bytes as weights.
		if (layers != 0 && layers != TerrainData::kLayers)
		{
			RV_CORE_ERROR("Terrain {0} carries {1} layer(s); this build reads 0 or {2}",
						  path.string(), layers, TerrainData::kLayers);
			return false;
		}

		const size_t sampleCount = (size_t)resolution * resolution;
		const size_t heightBytes = sampleCount * sizeof(uint16_t);
		const size_t weightBytes = layers == 0 ? 0 : sampleCount * TerrainData::kLayers;
		const size_t expected = kHeaderBytes + heightBytes + weightBytes;
		if (bytes.size() < expected)
		{
			RV_CORE_ERROR("Terrain {0} is truncated: {1} bytes, {2} expected",
						  path.string(), bytes.size(), expected);
			return false;
		}

		// Built into a local and moved out at the end, so a file that turns
		// out to be short leaves the caller's data alone.
		TerrainData data;
		data.Resolution = resolution;
		data.Heights.resize(sampleCount);
		std::memcpy(data.Heights.data(), bytes.data() + kHeaderBytes, heightBytes);
		if (weightBytes > 0)
		{
			data.Weights.resize(weightBytes);
			std::memcpy(data.Weights.data(), bytes.data() + kHeaderBytes + heightBytes, weightBytes);
		}

		out = std::move(data);
		return true;
	}

	bool TerrainSerializer::Save(const TerrainData& data, const std::filesystem::path& path)
	{
		if (!data.IsValid())
		{
			RV_CORE_ERROR("Terrain {0} not written: the data is not a valid grid", path.string());
			return false;
		}

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			RV_CORE_ERROR("Terrain {0} could not be opened for writing", path.string());
			return false;
		}

		file.write(kMagic, 4);
		WriteU32(file, kVersion);
		WriteU32(file, data.Resolution);
		WriteU32(file, data.HasWeights() ? TerrainData::kLayers : 0u);
		for (int i = 0; i < 4; ++i)
			WriteU32(file, 0);   // reserved
		file.write(reinterpret_cast<const char*>(data.Heights.data()),
				   (std::streamsize)(data.Heights.size() * sizeof(uint16_t)));
		if (data.HasWeights())
		{
			file.write(reinterpret_cast<const char*>(data.Weights.data()),
					   (std::streamsize)data.Weights.size());
		}

		return file.good();
	}
}
