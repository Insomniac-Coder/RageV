#include <rvpch.h>
#include "TerrainData.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	bool TerrainData::IsValidResolution(uint32_t resolution)
	{
		if (resolution < kMinResolution || resolution > kMaxResolution)
			return false;
		const uint32_t quads = resolution - 1;
		return (quads & (quads - 1)) == 0;
	}

	bool TerrainData::IsValid() const
	{
		return IsValidResolution(Resolution) &&
			   Heights.size() == (size_t)Resolution * Resolution;
	}

	float TerrainData::Sample(float x, float z) const
	{
		if (!IsValid())
			return 0.0f;

		const float last = (float)(Resolution - 1);
		x = Math::Clamp(x, 0.0f, last);
		z = Math::Clamp(z, 0.0f, last);

		// The quad, and the fraction inside it. The last row and column belong
		// to the quad before them, so a sample exactly on the far edge still
		// has a quad to stand in.
		uint32_t x0 = (uint32_t)x;
		uint32_t z0 = (uint32_t)z;
		if (x0 >= Resolution - 1) x0 = Resolution - 2;
		if (z0 >= Resolution - 1) z0 = Resolution - 2;
		const float fx = x - (float)x0;
		const float fz = z - (float)z0;

		const float h00 = (float)At(x0, z0) / 65535.0f;
		const float h10 = (float)At(x0 + 1, z0) / 65535.0f;
		const float h01 = (float)At(x0, z0 + 1) / 65535.0f;
		const float h11 = (float)At(x0 + 1, z0 + 1) / 65535.0f;

		// The (x0, z0) -> (x0 + 1, z0 + 1) diagonal splits the quad; the same
		// two triangles Jolt's ProjectOntoSurface walks and the chunk builder
		// emits. Below the diagonal (fz >= fx) the triangle is (00, 01, 11);
		// above it (00, 11, 10). Each is a plane, so this is exact.
		if (fz >= fx)
			return h00 + fz * (h01 - h00) + fx * (h11 - h01);
		return h00 + fx * (h10 - h00) + fz * (h11 - h10);
	}

	void TerrainData::MinMax(uint16_t& low, uint16_t& high) const
	{
		low = 0;
		high = 0;
		if (Heights.empty())
			return;
		low = 65535;
		for (uint16_t h : Heights)
		{
			low = Math::Min(low, h);
			high = Math::Max(high, h);
		}
	}

	TerrainData TerrainData::Flat(uint32_t resolution, uint16_t height)
	{
		TerrainData data;
		data.Resolution = resolution;
		data.Heights.assign((size_t)resolution * resolution, height);
		return data;
	}
}
