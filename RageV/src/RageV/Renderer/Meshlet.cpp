#include <rvpch.h>
#include "Meshlet.h"

namespace RageV
{
	MeshletData BuildMeshlets(const float* positions, uint32_t positionStrideFloats,
							  const uint32_t* indices, uint32_t indexCount)
	{
		MeshletData data;
		if (!positions || !indices || indexCount < 3)
			return data;

		const uint32_t triangleCount = indexCount / 3;

		// Global index -> slot in the open meshlet, kMaxVertices wide. A flat
		// array over the whole vertex range would be simpler to read and is
		// what a first draft used; a 490k-vertex car makes that a 2 MB clear
		// per meshlet, which is the entire build time. 64 entries searched
		// linearly is smaller than one cache line's worth of misses.
		uint32_t globalOf[MeshletData::kMaxVertices];

		Meshlet open;
		uint32_t used = 0;

		auto close = [&]()
		{
			if (open.TriangleCount == 0)
				return;

			// The sphere: centre at the mean of the meshlet's vertices, radius
			// to the farthest. Not minimal -- a minimal sphere buys a few
			// percent of cull rate for Welzl's algorithm at import -- but it
			// *contains*, which is the property culling is allowed to rely on.
			Vec3 centre(0.0f);
			for (uint32_t v = 0; v < open.VertexCount; v++)
			{
				const float* p = positions + (size_t)data.Vertices[open.VertexOffset + v]
											 * positionStrideFloats;
				centre += Vec3(p[0], p[1], p[2]);
			}
			centre /= (float)open.VertexCount;

			float radius2 = 0.0f;
			for (uint32_t v = 0; v < open.VertexCount; v++)
			{
				const float* p = positions + (size_t)data.Vertices[open.VertexOffset + v]
											 * positionStrideFloats;
				const Vec3 d = Vec3(p[0], p[1], p[2]) - centre;
				radius2 = Math::Max(radius2, Math::Dot(d, d));
			}

			open.Sphere = Vec4(centre, Math::Sqrt(radius2));
			data.Meshlets.push_back(open);

			open = Meshlet{};
			open.VertexOffset = (uint32_t)data.Vertices.size();
			open.TriangleOffset = (uint32_t)data.Triangles.size();
			used = 0;
		};

		open.VertexOffset = 0;
		open.TriangleOffset = 0;

		for (uint32_t t = 0; t < triangleCount; t++)
		{
			const uint32_t* tri = indices + (size_t)t * 3;

			// How many of this triangle's vertices are new to the open
			// meshlet, and which slot each lands in.
			uint32_t slots[3];
			uint32_t fresh = 0;
			for (int c = 0; c < 3; c++)
			{
				slots[c] = UINT32_MAX;
				for (uint32_t v = 0; v < used; v++)
				{
					if (globalOf[v] == tri[c])
					{
						slots[c] = v;
						break;
					}
				}
				if (slots[c] == UINT32_MAX)
					fresh++;
			}

			if (used + fresh > MeshletData::kMaxVertices ||
				open.TriangleCount + 1 > MeshletData::kMaxTriangles)
			{
				close();
				// Every slot is fresh in a new meshlet.
				slots[0] = slots[1] = slots[2] = UINT32_MAX;
			}

			for (int c = 0; c < 3; c++)
			{
				if (slots[c] != UINT32_MAX)
					continue;

				// A degenerate triangle can name one vertex twice: the second
				// occurrence has to find the first rather than take two slots.
				bool found = false;
				for (uint32_t v = 0; v < used && !found; v++)
				{
					if (globalOf[v] == tri[c])
					{
						slots[c] = v;
						found = true;
					}
				}
				if (found)
					continue;

				globalOf[used] = tri[c];
				data.Vertices.push_back(tri[c]);
				slots[c] = used;
				used++;
			}

			data.Triangles.push_back(slots[0] | (slots[1] << 8) | (slots[2] << 16));
			open.VertexCount = used;
			open.TriangleCount++;
		}

		close();
		return data;
	}
}
