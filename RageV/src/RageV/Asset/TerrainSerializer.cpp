#include <rvpch.h>
#include "TerrainSerializer.h"
#include "RageV/IO/VFS.h"
#include "RageV/Core/Log.h"

#include "stb_image.h"

#include <algorithm>
#include <cmath>
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

	bool TerrainSerializer::IsHeightmapImage(const std::filesystem::path& path)
	{
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
					   [](unsigned char c) { return (char)std::tolower(c); });
		// Everything stb reads that can carry a sensible height. PNG and TGA
		// are the ones that carry sixteen bits; the rest are accepted because
		// refusing them would be a worse surprise than the precision warning.
		return ext == ".png" || ext == ".tga" || ext == ".tif" || ext == ".tiff"
			|| ext == ".bmp" || ext == ".jpg" || ext == ".jpeg";
	}

	bool TerrainSerializer::LoadImage(TerrainData& out, const std::filesystem::path& path)
	{
		std::vector<uint8_t> bytes;
		if (!IO::VFS::ReadBytes(path, bytes))
			return false;

		int width = 0, height = 0, channels = 0;
		if (!stbi_info_from_memory(bytes.data(), (int)bytes.size(),
								   &width, &height, &channels))
		{
			RV_CORE_ERROR("Terrain heightmap {0} is not an image stb can read", path.string());
			return false;
		}
		if (width < 2 || height < 2)
		{
			RV_CORE_ERROR("Terrain heightmap {0} is {1}x{2}; it needs at least 2x2",
						  path.string(), width, height);
			return false;
		}

		// **One channel, and heights come out of it.** A heightmap is grey;
		// asking stb for one channel makes a colour one average to luminance
		// rather than being refused, which is what someone who exported RGB
		// meant.
		std::vector<float> source((size_t)width * height);
		const bool sixteen = stbi_is_16_bit_from_memory(bytes.data(), (int)bytes.size()) != 0;
		if (sixteen)
		{
			stbi_us* pixels = stbi_load_16_from_memory(bytes.data(), (int)bytes.size(),
													   &width, &height, &channels, 1);
			if (!pixels)
			{
				RV_CORE_ERROR("Terrain heightmap {0} could not be decoded: {1}",
							  path.string(), stbi_failure_reason());
				return false;
			}
			for (size_t i = 0; i < source.size(); i++)
				source[i] = (float)pixels[i] / 65535.0f;
			stbi_image_free(pixels);
		}
		else
		{
			stbi_uc* pixels = stbi_load_from_memory(bytes.data(), (int)bytes.size(),
													&width, &height, &channels, 1);
			if (!pixels)
			{
				RV_CORE_ERROR("Terrain heightmap {0} could not be decoded: {1}",
							  path.string(), stbi_failure_reason());
				return false;
			}
			for (size_t i = 0; i < source.size(); i++)
				source[i] = (float)pixels[i] / 255.0f;
			stbi_image_free(pixels);

			// Not an error -- it renders -- but 256 steps across a few hundred
			// metres is a step every metre or two, and it reads as contour
			// terracing that no amount of smoothing in the shader removes.
			RV_CORE_WARN("Terrain heightmap {0} is 8-bit: 256 height steps, which terraces "
						 "on tall terrain. Export 16-bit PNG for smooth relief.",
						 path.filename().string());
		}

		// **The nearest legal grid at or above the image.** TerrainData needs
		// 2^n + 1 so that every level of detail lands on the last sample, and
		// a heightmap is nearly always a power of two -- so the usual case is
		// 1024 becoming 1025 and one row of bilinear reach at the far edge.
		const uint32_t longest = (uint32_t)std::max(width, height);
		uint32_t resolution = TerrainData::kMinResolution;
		while (resolution < longest && resolution < TerrainData::kMaxResolution)
			resolution = (resolution - 1) * 2 + 1;
		resolution = std::clamp(resolution, TerrainData::kMinResolution,
								TerrainData::kMaxResolution);

		if (width != height)
		{
			// Stretched to square rather than refused or letterboxed: the
			// component describes a square of ground, so a non-square image has
			// to become one, and saying so is better than a silent aspect
			// change.
			RV_CORE_WARN("Terrain heightmap {0} is {1}x{2}, not square; it is stretched to "
						 "{3}x{3}. The terrain covers a square of ground.",
						 path.filename().string(), width, height, resolution);
		}

		out.Resolution = resolution;
		out.Heights.assign((size_t)resolution * resolution, 0);
		out.Weights.clear();

		// Bilinear, mapping sample 0 to pixel 0 and the last sample to the last
		// pixel, so the terrain's edges are the image's edges rather than half
		// a texel inside them.
		const float xScale = (float)(width - 1) / (float)(resolution - 1);
		const float zScale = (float)(height - 1) / (float)(resolution - 1);
		for (uint32_t z = 0; z < resolution; z++)
		{
			const float sz = (float)z * zScale;
			const uint32_t z0 = (uint32_t)sz;
			const uint32_t z1 = std::min(z0 + 1u, (uint32_t)height - 1u);
			const float fz = sz - (float)z0;

			for (uint32_t x = 0; x < resolution; x++)
			{
				const float sx = (float)x * xScale;
				const uint32_t x0 = (uint32_t)sx;
				const uint32_t x1 = std::min(x0 + 1u, (uint32_t)width - 1u);
				const float fx = sx - (float)x0;

				const float a = source[(size_t)z0 * width + x0];
				const float b = source[(size_t)z0 * width + x1];
				const float c = source[(size_t)z1 * width + x0];
				const float d = source[(size_t)z1 * width + x1];
				const float value = (a * (1.0f - fx) + b * fx) * (1.0f - fz)
								  + (c * (1.0f - fx) + d * fx) * fz;

				out.Heights[(size_t)z * resolution + x] =
					(uint16_t)std::clamp(value * 65535.0f + 0.5f, 0.0f, 65535.0f);
			}
		}

		RV_CORE_INFO("Terrain heightmap {0}: {1}x{2} {3}-bit -> {4} samples a side",
					 path.filename().string(), width, height, sixteen ? 16 : 8, resolution);
		return out.IsValid();
	}
}
