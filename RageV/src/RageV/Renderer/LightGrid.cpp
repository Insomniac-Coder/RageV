#include <rvpch.h>
#include "LightGrid.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	void LightGrid::DepthRangeOf(const Mat4& projection, float& nearPlane, float& farPlane)
	{
		// For a zero-to-one perspective projection:
		//   P[2][2] = far / (near - far)
		//   P[3][2] = (far * near) / (near - far)
		// so near is their ratio and far falls out of P[2][2] + 1.
		const float m22 = projection[2][2];
		const float m32 = projection[3][2];

		// An orthographic projection has P[3][3] == 1 and the algebra above
		// does not apply. Nothing clusters an orthographic view today; giving
		// it a plausible range keeps it from producing a division by zero if
		// something ever does.
		if (std::fabs(projection[3][3] - 1.0f) < 1e-6f || std::fabs(m22) < 1e-9f)
		{
			nearPlane = 0.1f;
			farPlane = 1000.0f;
			return;
		}

		nearPlane = m32 / m22;
		const float denominator = m22 + 1.0f;
		farPlane = std::fabs(denominator) < 1e-9f ? nearPlane * 1000.0f : m32 / denominator;

		nearPlane = Math::Max(nearPlane, 0.0001f);
		farPlane = Math::Max(farPlane, nearPlane * 1.001f);
	}

	float LightGrid::SliceScale(float nearPlane, float farPlane)
	{
		// Slice boundaries are a geometric series from near to far, so
		//     slice = kSlices * log(z / near) / log(far / near)
		// which factors into log(z) * scale + bias with these two.
		const float ratio = Math::Max(farPlane / Math::Max(nearPlane, 0.0001f), 1.0001f);
		return (float)kSlices / std::log(ratio);
	}

	float LightGrid::SliceBias(float nearPlane, float farPlane)
	{
		return -SliceScale(nearPlane, farPlane) * std::log(Math::Max(nearPlane, 0.0001f));
	}

	uint32_t LightGrid::SliceForDepth(float viewDepth, float nearPlane, float farPlane)
	{
		// Anything at or in front of the near plane belongs to the first slice.
		// log of a non-positive depth is not a number, and a fragment exactly on
		// the near plane is a real case rather than a degenerate one.
		if (viewDepth <= nearPlane)
			return 0;

		const float slice = std::log(viewDepth) * SliceScale(nearPlane, farPlane) +
							SliceBias(nearPlane, farPlane);

		return (uint32_t)Math::Clamp((int)slice, 0, (int)kSlices - 1);
	}

	void LightGrid::Build(const Camera& camera, const Mat4& cameraTransform,
						  const LightList& lights, uint32_t firstPositional)
	{
		m_Cells.assign(kCellCount, Cell{});
		m_Indices.clear();
		m_MaxCellLoad = 0;

		if (m_Buckets.size() != kCellCount)
			m_Buckets.resize(kCellCount);
		for (auto& bucket : m_Buckets)
			bucket.clear();

		const Mat4 view = Math::Inverse(cameraTransform);
		const Mat4 projection = camera.GetProjection();
		const Mat4 viewProjection = projection * view;

		float nearPlane = 0.1f;
		float farPlane = 1000.0f;
		DepthRangeOf(projection, nearPlane, farPlane);

		for (uint32_t i = firstPositional; i < (uint32_t)lights.size(); i++)
		{
			const LightRenderData& light = lights[i];
			const float radius = Math::Max(light.Range, 0.0001f);

			// The sphere's extent in view space decides the slice range, and
			// its screen-space box decides the tiles.
			//
			// Both are computed from the eight corners of the sphere's bounding
			// box rather than analytically. Conservative in the safe direction:
			// a light kept in a cell it does not reach costs a wasted iteration,
			// and a light dropped from a cell it does reach is a dark patch that
			// moves with the camera.
			const Vec3 centre = light.Position;

			float minDepth = std::numeric_limits<float>::max();
			float maxDepth = std::numeric_limits<float>::lowest();
			Vec2 minScreen(std::numeric_limits<float>::max());
			Vec2 maxScreen(std::numeric_limits<float>::lowest());
			bool anyInFront = false;

			for (int corner = 0; corner < 8; corner++)
			{
				const Vec3 offset{
					(corner & 1) ? radius : -radius,
					(corner & 2) ? radius : -radius,
					(corner & 4) ? radius : -radius,
				};

				const Vec3 viewPos = Vec3(view * Vec4(centre + offset, 1.0f));
				// View space looks down -Z, so depth in front of the camera is
				// positive -z.
				const float depth = -viewPos.z;

				minDepth = Math::Min(minDepth, depth);
				maxDepth = Math::Max(maxDepth, depth);

				if (depth <= nearPlane)
					continue;

				anyInFront = true;

				const Vec4 clip = viewProjection * Vec4(centre + offset, 1.0f);
				const Vec2 ndc = Vec2(clip) / clip.w;

				minScreen = Math::Min(minScreen, ndc);
				maxScreen = Math::Max(maxScreen, ndc);
			}

			// Entirely behind the camera, or entirely beyond the far plane.
			if (maxDepth <= nearPlane || minDepth >= farPlane)
				continue;

			// Straddling the near plane: some corners projected and some did
			// not, and the ones that did not are the ones wrapping around
			// behind the eye. Their projection is meaningless, so the tile
			// range has to be the whole screen or the light disappears from the
			// edges as the camera moves into it.
			const bool straddles = !anyInFront || minDepth <= nearPlane;
			if (straddles)
			{
				minScreen = Vec2(-1.0f);
				maxScreen = Vec2(1.0f);
			}

			minScreen = Math::Max(minScreen, Vec2(-1.0f));
			maxScreen = Math::Min(maxScreen, Vec2(1.0f));
			if (minScreen.x > maxScreen.x || minScreen.y > maxScreen.y)
				continue;

			// NDC to tiles. The y axis is not flipped here: the shader derives
			// its tile from the same normalised coordinate, so both agree
			// whatever the convention, and a flip would have to happen in both
			// or neither.
			const auto tileOf = [](float ndc, uint32_t count)
			{
				const float unit = (ndc * 0.5f + 0.5f) * (float)count;
				return (uint32_t)Math::Clamp((int)unit, 0, (int)count - 1);
			};

			const uint32_t x0 = tileOf(minScreen.x, kTilesX);
			const uint32_t x1 = tileOf(maxScreen.x, kTilesX);
			const uint32_t y0 = tileOf(minScreen.y, kTilesY);
			const uint32_t y1 = tileOf(maxScreen.y, kTilesY);

			const uint32_t z0 = SliceForDepth(Math::Max(minDepth, nearPlane), nearPlane, farPlane);
			const uint32_t z1 = SliceForDepth(Math::Min(maxDepth, farPlane), nearPlane, farPlane);

			for (uint32_t z = z0; z <= z1; z++)
			{
				for (uint32_t y = y0; y <= y1; y++)
				{
					for (uint32_t x = x0; x <= x1; x++)
					{
						const uint32_t cell = (z * kTilesY + y) * kTilesX + x;
						m_Buckets[cell].push_back(i);
					}
				}
			}
		}

		// Flatten. One contiguous index list and a range per cell, because the
		// shader wants to walk it and a vector of vectors is not a buffer.
		for (uint32_t cell = 0; cell < kCellCount; cell++)
		{
			const auto& bucket = m_Buckets[cell];

			m_Cells[cell].Offset = (uint32_t)m_Indices.size();
			m_Cells[cell].Count = (uint32_t)bucket.size();
			m_MaxCellLoad = Math::Max(m_MaxCellLoad, (uint32_t)bucket.size());

			m_Indices.insert(m_Indices.end(), bucket.begin(), bucket.end());
		}
	}
}
