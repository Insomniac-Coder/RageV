#include <rvpch.h>
#include "LightGrid.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	void LightGrid::DepthRangeOf(const Mat4& projection, float& nearPlane, float& farPlane)
	{
		// For a zero-to-one **reverse-Z** perspective projection:
		//   P[2][2] = near / (far - near)
		//   P[3][2] = (far * near) / (far - near)
		// so their ratio is *far* and P[2][2] + 1 yields *near* -- exactly the
		// two the conventional form produces, exchanged. Which is the whole
		// character of the flip: the algebra is the same and the labels swap.
		const float m22 = projection[2][2];
		const float m32 = projection[3][2];

		// An orthographic projection has P[3][3] == 1 and the algebra above
		// does not apply. Nothing clusters an orthographic view today; giving
		// it a plausible range keeps it from producing a division by zero if
		// something ever does.
		if (Math::Abs(projection[3][3] - 1.0f) < 1e-6f || Math::Abs(m22) < 1e-9f)
		{
			nearPlane = 0.1f;
			farPlane = 1000.0f;
			return;
		}

		farPlane = m32 / m22;
		const float denominator = m22 + 1.0f;
		nearPlane = Math::Abs(denominator) < 1e-9f ? farPlane * 0.001f : m32 / denominator;

		nearPlane = Math::Max(nearPlane, 0.0001f);
		farPlane = Math::Max(farPlane, nearPlane * 1.001f);
	}

	float LightGrid::SliceScale(float nearPlane, float farPlane)
	{
		// Slice boundaries are a geometric series from near to far, so
		//     slice = kSlices * log(z / near) / log(far / near)
		// which factors into log(z) * scale + bias with these two.
		const float ratio = Math::Max(farPlane / Math::Max(nearPlane, 0.0001f), 1.0001f);
		return (float)kSlices / Math::Log(ratio);
	}

	float LightGrid::SliceBias(float nearPlane, float farPlane)
	{
		return -SliceScale(nearPlane, farPlane) * Math::Log(Math::Max(nearPlane, 0.0001f));
	}

	uint32_t LightGrid::SliceForDepth(float viewDepth, float nearPlane, float farPlane)
	{
		// Anything at or in front of the near plane belongs to the first slice.
		// log of a non-positive depth is not a number, and a fragment exactly on
		// the near plane is a real case rather than a degenerate one.
		if (viewDepth <= nearPlane)
			return 0;

		const float slice = Math::Log(viewDepth) * SliceScale(nearPlane, farPlane) +
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

		// The cells a sphere touches, conservatively: the bounding box of the
		// sphere in view space decides the slices and its screen-space box
		// the tiles. Used twice -- for the light's range, which bins it, and
		// (WR-16 S2) for a hybrid lamp's half-bake sphere, which marks the
		// cells where a static surface still has to walk it.
		const auto forEachCellOf = [&](const Vec3& centre, float radius, auto&& visit)
		{
			// Straddling the near plane: some corners project and some do
			// not, and the ones that do not are the ones wrapping around
			// behind the eye. Their projection is meaningless, so the tile
			// range has to be the whole screen or the light disappears from
			// the edges as the camera moves into it. And the y axis is not
			// flipped: the shader derives its tile from the same normalised
			// coordinate, so both agree whatever the convention.
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

			if (maxDepth <= nearPlane || minDepth >= farPlane)
				return;

			const bool straddles = !anyInFront || minDepth <= nearPlane;
			if (straddles)
			{
				minScreen = Vec2(-1.0f);
				maxScreen = Vec2(1.0f);
			}

			minScreen = Math::Max(minScreen, Vec2(-1.0f));
			maxScreen = Math::Min(maxScreen, Vec2(1.0f));
			if (minScreen.x > maxScreen.x || minScreen.y > maxScreen.y)
				return;

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
				for (uint32_t y = y0; y <= y1; y++)
					for (uint32_t x = x0; x <= x1; x++)
						visit((z * kTilesY + y) * kTilesX + x);
		};

		// **The full list, and beside it the live sublist (WR-16 S2).** A
		// realtime or half-baked lamp is live wherever it reaches; a fully
		// baked one is live nowhere for a static surface deep inside the
		// field, except where a moving object stands inside its range and
		// the subtractive ray is traced on screen; a hybrid one is live within
		// its radius plus the blend band BakedShare uses (a tenth of the
		// radius, a metre at least), which is the sphere binned here in place
		// of its range, and beyond it like a fully baked one. The lamps are
		// walked in ascending index order, so each cell's live sublist comes
		// out in the same order as its full list; a hybrid lamp with a moving
		// object can push itself twice in a row, which the back() test folds.
		// A cell test alone (a count of zero) was not enough under the deck,
		// where the roadway's lamps and the underside share a view-space cell
		// and every hit still iterated the whole list.
		if (m_Live.size() != kCellCount)
			m_Live.resize(kCellCount);
		for (auto& live : m_Live)
			live.clear();

		const auto pushLive = [&](uint32_t cell, uint32_t i)
		{
			auto& live = m_Live[cell];
			if (live.empty() || live.back() != i)
				live.push_back(i);
		};

		for (uint32_t i = firstPositional; i < (uint32_t)lights.size(); i++)
		{
			const LightRenderData& light = lights[i];
			const float range = Math::Max(light.Range, 0.0001f);
			const bool fullyBaked = light.Mobility == LightMobility::FullBake;
			const bool hybrid = light.Mobility == LightMobility::HybridFullBake;

			// The range sphere: where the lamp is in the cell at all.
			forEachCellOf(light.Position, range,
						  [&](uint32_t cell) { m_Buckets[cell].push_back(i); });

			if (hybrid)
			{
				const float radius = Math::Max(light.HybridRadius, 0.0f);
				const float band = Math::Max(radius * 0.1f, 1.0f);
				forEachCellOf(light.Position, radius + band,
							  [&](uint32_t cell) { pushLive(cell, i); });
			}
			if ((fullyBaked || hybrid) && !light.MovingInRange)
				continue;
			forEachCellOf(light.Position, range, [&](uint32_t cell) { pushLive(cell, i); });
		}

		// Flatten. One contiguous index list and two ranges per cell -- the
		// full list, then its live sublist -- because the shader wants to
		// walk them and a vector of vectors is not a buffer.
		for (uint32_t cell = 0; cell < kCellCount; cell++)
		{
			const auto& bucket = m_Buckets[cell];
			const auto& live = m_Live[cell];

			m_Cells[cell].Offset = (uint32_t)m_Indices.size();
			m_Cells[cell].Count = (uint32_t)bucket.size();
			m_MaxCellLoad = Math::Max(m_MaxCellLoad, (uint32_t)bucket.size());
			m_Indices.insert(m_Indices.end(), bucket.begin(), bucket.end());

			m_Cells[cell].LiveOffset = (uint32_t)m_Indices.size();
			m_Cells[cell].LiveCount = (uint32_t)live.size();
			m_Indices.insert(m_Indices.end(), live.begin(), live.end());
		}
	}
}
