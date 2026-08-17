#include <rvpch.h>
#include "TerrainBrush.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	// --- rectangles ------------------------------------------------------------

	TerrainRect TerrainRect::Union(const TerrainRect& other) const
	{
		if (Empty()) return other;
		if (other.Empty()) return *this;
		TerrainRect out;
		out.X0 = Math::Min(X0, other.X0);
		out.Z0 = Math::Min(Z0, other.Z0);
		out.X1 = Math::Max(X1, other.X1);
		out.Z1 = Math::Max(Z1, other.Z1);
		return out;
	}

	TerrainRect TerrainRect::Grown(uint32_t samples, uint32_t resolution) const
	{
		if (Empty() || resolution == 0)
			return *this;
		TerrainRect out;
		out.X0 = X0 > samples ? X0 - samples : 0;
		out.Z0 = Z0 > samples ? Z0 - samples : 0;
		out.X1 = Math::Min(X1 + samples, resolution - 1);
		out.Z1 = Math::Min(Z1 + samples, resolution - 1);
		return out;
	}

	// --- the copies --------------------------------------------------------------

	void CopyHeightsOut(const TerrainData& data, const TerrainRect& rect, std::vector<uint16_t>& out)
	{
		out.resize((size_t)rect.Width() * rect.Height());
		if (rect.Empty())
			return;
		for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
		{
			const uint16_t* row = &data.Heights[(size_t)z * data.Resolution + rect.X0];
			std::copy(row, row + rect.Width(), out.begin() + (size_t)(z - rect.Z0) * rect.Width());
		}
	}

	void CopyHeightsIn(TerrainData& data, const TerrainRect& rect, const std::vector<uint16_t>& in)
	{
		if (rect.Empty() || in.size() < (size_t)rect.Width() * rect.Height())
			return;
		for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
		{
			const uint16_t* row = &in[(size_t)(z - rect.Z0) * rect.Width()];
			std::copy(row, row + rect.Width(), data.Heights.begin() + (size_t)z * data.Resolution + rect.X0);
		}
	}

	void CopyWeightsOut(const TerrainData& data, const TerrainRect& rect, std::vector<uint8_t>& out)
	{
		const size_t stride = (size_t)rect.Width() * TerrainData::kLayers;
		out.assign(stride * rect.Height(), 0);
		if (rect.Empty() || !data.HasWeights())
			return;
		for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
		{
			const uint8_t* row = &data.Weights[((size_t)z * data.Resolution + rect.X0) * TerrainData::kLayers];
			std::copy(row, row + stride, out.begin() + (size_t)(z - rect.Z0) * stride);
		}
	}

	void CopyWeightsIn(TerrainData& data, const TerrainRect& rect, const std::vector<uint8_t>& in)
	{
		const size_t stride = (size_t)rect.Width() * TerrainData::kLayers;
		if (rect.Empty() || in.size() < stride * rect.Height())
			return;
		// A grid that has never been painted gets its weights the moment
		// something writes some: all zero, which draws as layer 0 (7aq).
		if (!data.HasWeights())
			data.Weights.assign((size_t)data.Resolution * data.Resolution * TerrainData::kLayers, 0);
		for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
		{
			const uint8_t* row = &in[(size_t)(z - rect.Z0) * stride];
			std::copy(row, row + stride,
					  data.Weights.begin() + ((size_t)z * data.Resolution + rect.X0) * TerrainData::kLayers);
		}
	}

	// --- the recorder --------------------------------------------------------------

	void TerrainStrokeRecorder::Begin(const TerrainData& data, bool heights)
	{
		m_Active = true;
		m_Heights = heights;
		m_Resolution = data.Resolution;
		m_Rect = TerrainRect{};
		m_BeforeHeights.clear();
		m_BeforeWeights.clear();
	}

	void TerrainStrokeRecorder::Cover(const TerrainData& data, const TerrainRect& rect)
	{
		if (!m_Active || rect.Empty())
			return;

		const TerrainRect grown = m_Rect.Union(rect);
		if (!m_Rect.Empty() && grown.X0 == m_Rect.X0 && grown.Z0 == m_Rect.Z0 &&
			grown.X1 == m_Rect.X1 && grown.Z1 == m_Rect.Z1)
			return;

		// The new rectangle from the data -- every sample it covers is still
		// unwritten by this step -- then the part already recorded copied back
		// over it, because those samples *have* been written by earlier steps
		// and the data no longer holds their before.
		if (m_Heights)
		{
			std::vector<uint16_t> fresh;
			CopyHeightsOut(data, grown, fresh);
			for (uint32_t z = m_Rect.Z0; !m_Rect.Empty() && z <= m_Rect.Z1; ++z)
			{
				for (uint32_t x = m_Rect.X0; x <= m_Rect.X1; ++x)
				{
					fresh[(size_t)(z - grown.Z0) * grown.Width() + (x - grown.X0)] =
						m_BeforeHeights[(size_t)(z - m_Rect.Z0) * m_Rect.Width() + (x - m_Rect.X0)];
				}
			}
			m_BeforeHeights.swap(fresh);
		}
		else
		{
			std::vector<uint8_t> fresh;
			CopyWeightsOut(data, grown, fresh);
			const size_t k = TerrainData::kLayers;
			for (uint32_t z = m_Rect.Z0; !m_Rect.Empty() && z <= m_Rect.Z1; ++z)
			{
				for (uint32_t x = m_Rect.X0; x <= m_Rect.X1; ++x)
				{
					const size_t to = ((size_t)(z - grown.Z0) * grown.Width() + (x - grown.X0)) * k;
					const size_t from = ((size_t)(z - m_Rect.Z0) * m_Rect.Width() + (x - m_Rect.X0)) * k;
					for (size_t i = 0; i < k; ++i)
						fresh[to + i] = m_BeforeWeights[from + i];
				}
			}
			m_BeforeWeights.swap(fresh);
		}
		m_Rect = grown;
	}

	// --- the brush -------------------------------------------------------------------

	const char* TerrainBrush::ModeName(TerrainBrush::Op mode)
	{
		switch (mode)
		{
		case Op::Raise:   return "Raise";
		case Op::Smooth:  return "Smooth";
		case Op::Flatten: return "Flatten";
		case Op::Paint:   return "Paint";
		}
		return "?";
	}

	float TerrainBrush::Weight(float distance) const
	{
		if (Radius <= 0.0f || distance < 0.0f)
			return 0.0f;
		const float t = distance / Radius;
		if (t >= 1.0f)
			return 0.0f;
		const float hard = Math::Clamp(Hardness, 0.0f, 1.0f);
		if (t <= hard)
			return 1.0f;
		// 1 - smoothstep over the fall-off band. Hardness 1 never reaches
		// here: everything inside the rim is at full weight.
		const float u = (t - hard) / Math::Max(1.0f - hard, 1e-6f);
		return 1.0f - u * u * (3.0f - 2.0f * u);
	}

	TerrainRect TerrainBrush::Footprint(const TerrainData& data, float sizeMetres,
										float localX, float localZ) const
	{
		TerrainRect rect;
		if (!data.IsValid() || Radius <= 0.0f || sizeMetres <= 0.0f)
			return rect;

		const uint32_t resolution = data.Resolution;
		const float cell = sizeMetres / (float)data.QuadCount();
		const float cx = (localX + 0.5f * sizeMetres) / cell;
		const float cz = (localZ + 0.5f * sizeMetres) / cell;
		const float r = Radius / cell;

		// The circle's bounding box, clamped to the grid; a circle entirely
		// off the grid is no rectangle at all.
		const float last = (float)(resolution - 1);
		if (cx + r < 0.0f || cz + r < 0.0f || cx - r > last || cz - r > last)
			return rect;
		rect.X0 = (uint32_t)Math::Max(Math::Floor(cx - r), 0.0f);
		rect.Z0 = (uint32_t)Math::Max(Math::Floor(cz - r), 0.0f);
		rect.X1 = (uint32_t)Math::Min(Math::Ceil(cx + r), last);
		rect.Z1 = (uint32_t)Math::Min(Math::Ceil(cz + r), last);
		return rect;
	}

	TerrainRect TerrainBrush::Apply(TerrainData& data, float sizeMetres, float heightMetres,
									float localX, float localZ, float flattenTarget, float dt) const
	{
		(void)heightMetres;
		if (dt <= 0.0f)
			return TerrainRect{};
		const TerrainRect rect = Footprint(data, sizeMetres, localX, localZ);
		if (rect.Empty())
			return rect;

		const uint32_t resolution = data.Resolution;
		const float cell = sizeMetres / (float)data.QuadCount();
		const float cx = (localX + 0.5f * sizeMetres) / cell;
		const float cz = (localZ + 0.5f * sizeMetres) / cell;

		const float strength = Math::Clamp(Strength, 0.0f, 1.0f);
		// The blend modes close a fraction of their gap per step of a
		// sixtieth, scaled to the frame's dt and never past 1.
		const float blend = Math::Min(strength * kBlendPerStep * dt / kStepSeconds, 1.0f);

		auto weightAt = [&](uint32_t x, uint32_t z)
		{
			const float dx = (float)x - cx;
			const float dz = (float)z - cz;
			return Weight(Math::Sqrt(dx * dx + dz * dz) * cell);
		};

		switch (Mode)
		{
		case Op::Raise:
		{
			// Units of the sixteen-bit height, from a fraction of the full
			// height per second.
			const float delta = (Invert ? -1.0f : 1.0f) * strength * kRaisePerSecond * dt * 65535.0f;
			for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
			{
				for (uint32_t x = rect.X0; x <= rect.X1; ++x)
				{
					const float w = weightAt(x, z);
					if (w <= 0.0f)
						continue;
					uint16_t& h = data.Heights[(size_t)z * resolution + x];
					h = (uint16_t)Math::Clamp(Math::Round((float)h + w * delta), 0.0f, 65535.0f);
				}
			}
			break;
		}
		case Op::Smooth:
		{
			// The mean of the 3x3 around each sample, read from the grid as it
			// was before this step wrote any of it: a blur that reads its own
			// output drifts toward whichever corner it started in.
			const TerrainRect source = rect.Grown(1, resolution);
			std::vector<uint16_t> before;
			CopyHeightsOut(data, source, before);
			auto at = [&](int x, int z)
			{
				x = Math::Clamp(x, (int)source.X0, (int)source.X1);
				z = Math::Clamp(z, (int)source.Z0, (int)source.Z1);
				return (float)before[(size_t)(z - (int)source.Z0) * source.Width() + (x - (int)source.X0)];
			};
			for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
			{
				for (uint32_t x = rect.X0; x <= rect.X1; ++x)
				{
					const float w = weightAt(x, z);
					if (w <= 0.0f)
						continue;
					float sum = 0.0f;
					for (int dz = -1; dz <= 1; ++dz)
						for (int dx = -1; dx <= 1; ++dx)
							sum += at((int)x + dx, (int)z + dz);
					const float mean = sum / 9.0f;
					uint16_t& h = data.Heights[(size_t)z * resolution + x];
					h = (uint16_t)Math::Clamp(Math::Round((float)h + (mean - (float)h) * blend * w), 0.0f, 65535.0f);
				}
			}
			break;
		}
		case Op::Flatten:
		{
			const float target = Math::Clamp(flattenTarget, 0.0f, 1.0f) * 65535.0f;
			for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
			{
				for (uint32_t x = rect.X0; x <= rect.X1; ++x)
				{
					const float w = weightAt(x, z);
					if (w <= 0.0f)
						continue;
					uint16_t& h = data.Heights[(size_t)z * resolution + x];
					h = (uint16_t)Math::Clamp(Math::Round((float)h + (target - (float)h) * blend * w), 0.0f, 65535.0f);
				}
			}
			break;
		}
		case Op::Paint:
		{
			if (!data.HasWeights())
				data.Weights.assign((size_t)resolution * resolution * TerrainData::kLayers, 0);
			const uint32_t layer = (uint32_t)Math::Clamp(Layer, 0, (int)TerrainData::kLayers - 1);
			for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
			{
				for (uint32_t x = rect.X0; x <= rect.X1; ++x)
				{
					const float w = weightAt(x, z);
					if (w <= 0.0f)
						continue;
					const float a = blend * w;
					uint8_t* texel = &data.Weights[((size_t)z * resolution + x) * TerrainData::kLayers];

					// An unpainted texel is layer 0 by the shader's zero-sum
					// rule; painting into zeros would hand the faintest touch
					// the whole texel. Materialise the rule first.
					float weights[TerrainData::kLayers];
					float sum = 0.0f;
					for (uint32_t i = 0; i < TerrainData::kLayers; ++i)
					{
						weights[i] = (float)texel[i];
						sum += weights[i];
					}
					if (sum <= 0.0f)
						weights[0] = 255.0f;

					// Replace: the layer moves toward full, the others scale
					// down by the same amount, so the sum stays where it was.
					// Erase: only the layer scales down.
					for (uint32_t i = 0; i < TerrainData::kLayers; ++i)
					{
						if (i == layer)
							weights[i] = Invert ? weights[i] * (1.0f - a) : weights[i] * (1.0f - a) + 255.0f * a;
						else if (!Invert)
							weights[i] *= (1.0f - a);
					}
					// Eight bits round to nearest, and a step whose change is
					// under half a unit would stall a held paint short of full
					// and a held erase short of empty -- at 4, where 4 * 7/8
					// rounds back to 4 -- and 4 is not 0 to the shader's
					// zero-sum rule, so an erased layer would still draw. So a
					// step that means to move and would round to nothing moves
					// one unit toward where it meant to go: an airbrush held on
					// one spot saturates, at its soft edge one unit a frame,
					// which is what every painting tool means by holding it.
					for (uint32_t i = 0; i < TerrainData::kLayers; ++i)
					{
						const float before = (float)texel[i];
						float rounded = Math::Clamp(Math::Round(weights[i]), 0.0f, 255.0f);
						if (rounded == before && weights[i] != before)
							rounded = Math::Clamp(before + (weights[i] > before ? 1.0f : -1.0f), 0.0f, 255.0f);
						texel[i] = (uint8_t)rounded;
					}
				}
			}
			break;
		}
		}

		return rect;
	}
}
