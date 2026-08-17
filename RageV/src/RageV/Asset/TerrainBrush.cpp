#include <rvpch.h>
#include "TerrainBrush.h"
#include "RageV/Math/Math.h"

#include "stb_image.h"

namespace RageV
{
	// --- the mask (7as) --------------------------------------------------------------

	float BrushMask::Sample(float u, float v, bool wrap) const
	{
		if (!Valid())
			return 0.0f;
		if (wrap)
		{
			u -= Math::Floor(u);
			v -= Math::Floor(v);
		}
		else if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
			return 0.0f;

		// Texel centres at (i + 0.5) / Size; the edges clamp when not wrapping,
		// and wrap when they do, so a tiled mask has no seam.
		const float fx = u * (float)Size - 0.5f;
		const float fz = v * (float)Size - 0.5f;
		const int x0 = (int)Math::Floor(fx);
		const int z0 = (int)Math::Floor(fz);
		const float tx = fx - (float)x0;
		const float tz = fz - (float)z0;
		auto at = [&](int x, int z)
		{
			const int n = (int)Size;
			if (wrap)
			{
				x = ((x % n) + n) % n;
				z = ((z % n) + n) % n;
			}
			else
			{
				x = Math::Clamp(x, 0, n - 1);
				z = Math::Clamp(z, 0, n - 1);
			}
			return Values[(size_t)z * Size + (size_t)x];
		};
		const float top = at(x0, z0) * (1.0f - tx) + at(x0 + 1, z0) * tx;
		const float bottom = at(x0, z0 + 1) * (1.0f - tx) + at(x0 + 1, z0 + 1) * tx;
		return top * (1.0f - tz) + bottom * tz;
	}

	bool BrushMask::Decode(const std::vector<uint8_t>& bytes, BrushMask& out)
	{
		if (bytes.empty())
			return false;
		int width = 0, height = 0, ignored = 0;
		stbi_uc* pixels = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &width, &height, &ignored, 4);
		if (!pixels)
			return false;
		if (width != height || width < 2)
		{
			stbi_image_free(pixels);
			return false;
		}
		out.Size = (uint32_t)width;
		out.Values.resize((size_t)width * height);
		for (size_t i = 0; i < out.Values.size(); ++i)
			out.Values[i] = (float)pixels[i * 4] / 255.0f;
		stbi_image_free(pixels);
		return true;
	}

	// --- the noise (7as) --------------------------------------------------------------

	namespace
	{
		// A hash of an integer lattice point and a seed to [0, 1): the same
		// on every machine, which <random> does not promise across libraries.
		float LatticeValue(int x, int z, uint32_t seed)
		{
			uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)z * 0xd8163841u ^ (seed + 0x9e3779b9u) * 0xcb1ab31fu;
			h ^= h >> 15; h *= 0x2c1b3c6du;
			h ^= h >> 12; h *= 0x297a2d39u;
			h ^= h >> 15;
			return (float)(h & 0x00ffffffu) / 16777216.0f;
		}

		float ValueNoise(float x, float z, uint32_t seed)
		{
			const int x0 = (int)Math::Floor(x);
			const int z0 = (int)Math::Floor(z);
			float tx = x - (float)x0;
			float tz = z - (float)z0;
			tx = tx * tx * (3.0f - 2.0f * tx);
			tz = tz * tz * (3.0f - 2.0f * tz);
			const float a = LatticeValue(x0, z0, seed);
			const float b = LatticeValue(x0 + 1, z0, seed);
			const float c = LatticeValue(x0, z0 + 1, seed);
			const float d = LatticeValue(x0 + 1, z0 + 1, seed);
			return (a * (1.0f - tx) + b * tx) * (1.0f - tz) + (c * (1.0f - tx) + d * tx) * tz;
		}

		// xorshift32: the droplets' die, seeded per step so a stroke replays.
		struct Die
		{
			uint32_t State;
			explicit Die(uint32_t seed) : State(seed ? seed : 0x1234567u) {}
			uint32_t Next()
			{
				State ^= State << 13;
				State ^= State >> 17;
				State ^= State << 5;
				return State;
			}
			float Unit() { return (float)(Next() & 0x00ffffffu) / 16777216.0f; }
		};
	}

	float TerrainBrush::Noise(float x, float z, uint32_t seed)
	{
		// Three octaves, each half the amplitude and twice the frequency of the
		// last, normalised back to [0, 1].
		float sum = 0.0f, amplitude = 1.0f, total = 0.0f, frequency = 1.0f;
		for (int octave = 0; octave < 3; ++octave)
		{
			sum += ValueNoise(x * frequency, z * frequency, seed + (uint32_t)octave * 131u) * amplitude;
			total += amplitude;
			amplitude *= 0.5f;
			frequency *= 2.0f;
		}
		return Math::Clamp(sum / total, 0.0f, 1.0f);
	}

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
		case Op::Raise:     return "Raise";
		case Op::Smooth:    return "Smooth";
		case Op::Flatten:   return "Flatten";
		case Op::Paint:     return "Paint";
		case Op::Terrace:   return "Terrace";
		case Op::Ramp:      return "Ramp";
		case Op::SetHeight: return "Set Height";
		case Op::Erode:     return "Erode";
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

	float TerrainBrush::Weight(float dx, float dz, float direction) const
	{
		if (Radius <= 0.0f)
			return 0.0f;
		if (ShapeKind == Shape::Disc)
			return Weight(Math::Sqrt(dx * dx + dz * dz));
		if (!ShapeMask || !ShapeMask->Valid())
			return 0.0f;

		// The offset into the mask's frame: turn it back by the mask's angle
		// (and the stroke's direction when following), then the square
		// [-Radius, Radius]^2 onto [0, 1]^2, top-left first.
		const float angle = Angle + (FollowStroke ? direction : 0.0f);
		const float c = Math::Cos(-angle);
		const float sn = Math::Sin(-angle);
		const float rx = dx * c - dz * sn;
		const float rz = dx * sn + dz * c;
		const float u = (rx / Radius + 1.0f) * 0.5f;
		const float v = (rz / Radius + 1.0f) * 0.5f;
		return ShapeMask->Sample(u, v, false);
	}

	float TerrainBrush::PatternAt(float localX, float localZ, uint32_t seed) const
	{
		const float scale = Math::Max(PatternScale, 1e-3f);
		switch (PatternKind)
		{
		case Pattern::None:
			return 1.0f;
		case Pattern::Noise:
			return Noise(localX / scale, localZ / scale, seed);
		case Pattern::Tiled:
			return PatternMask && PatternMask->Valid()
				? PatternMask->Sample(localX / scale, localZ / scale, true) : 1.0f;
		}
		return 1.0f;
	}

	namespace
	{
		// The inclusive sample box of the metre-space box [x0, x1] x [z0, z1]
		// on a grid of `resolution` at `cell` metres, empty when it misses.
		TerrainRect BoxToRect(float x0, float z0, float x1, float z1, float sizeMetres,
							  float cell, uint32_t resolution)
		{
			TerrainRect rect;
			const float last = (float)(resolution - 1);
			const float cx0 = (x0 + 0.5f * sizeMetres) / cell;
			const float cz0 = (z0 + 0.5f * sizeMetres) / cell;
			const float cx1 = (x1 + 0.5f * sizeMetres) / cell;
			const float cz1 = (z1 + 0.5f * sizeMetres) / cell;
			if (cx1 < 0.0f || cz1 < 0.0f || cx0 > last || cz0 > last)
				return rect;
			rect.X0 = (uint32_t)Math::Max(Math::Floor(cx0), 0.0f);
			rect.Z0 = (uint32_t)Math::Max(Math::Floor(cz0), 0.0f);
			rect.X1 = (uint32_t)Math::Min(Math::Ceil(cx1), last);
			rect.Z1 = (uint32_t)Math::Min(Math::Ceil(cz1), last);
			return rect;
		}
	}

	TerrainRect TerrainBrush::Footprint(const TerrainData& data, float sizeMetres,
										float localX, float localZ) const
	{
		Stroke stroke;
		stroke.StartX = localX;
		stroke.StartZ = localZ;
		return Footprint(data, sizeMetres, localX, localZ, stroke);
	}

	TerrainRect TerrainBrush::Footprint(const TerrainData& data, float sizeMetres,
										float localX, float localZ, const Stroke& stroke) const
	{
		if (!data.IsValid() || Radius <= 0.0f || sizeMetres <= 0.0f)
			return TerrainRect{};

		const float cell = sizeMetres / (float)data.QuadCount();
		// The kernel's reach: the disc's radius, or the turned square's,
		// which is root two of it whatever the angle.
		const float reach = ShapeKind == Shape::Mask ? Radius * 1.41421356f : Radius;
		if (Mode == Op::Ramp)
		{
			// The segment from the start to here, grown by the reach.
			return BoxToRect(Math::Min(stroke.StartX, localX) - reach, Math::Min(stroke.StartZ, localZ) - reach,
							 Math::Max(stroke.StartX, localX) + reach, Math::Max(stroke.StartZ, localZ) + reach,
							 sizeMetres, cell, data.Resolution);
		}
		return BoxToRect(localX - reach, localZ - reach, localX + reach, localZ + reach,
						 sizeMetres, cell, data.Resolution);
	}

	TerrainRect TerrainBrush::Apply(TerrainData& data, float sizeMetres, float heightMetres,
									float localX, float localZ, float flattenTarget, float dt) const
	{
		Stroke stroke;
		stroke.StartX = localX;
		stroke.StartZ = localZ;
		stroke.StartHeight = flattenTarget;
		return Apply(data, sizeMetres, heightMetres, localX, localZ, stroke, dt);
	}

	TerrainRect TerrainBrush::Apply(TerrainData& data, float sizeMetres, float heightMetres,
									float localX, float localZ, const Stroke& stroke, float dt) const
	{
		if (dt <= 0.0f)
			return TerrainRect{};
		const TerrainRect rect = Footprint(data, sizeMetres, localX, localZ, stroke);
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

		// The kernel at a sample: the shape at the offset from the centre,
		// times the pattern at the sample's place on the ground (7as).
		auto localOf = [&](uint32_t x, uint32_t z)
		{
			return std::pair<float, float>((float)x * cell - 0.5f * sizeMetres,
										   (float)z * cell - 0.5f * sizeMetres);
		};
		auto weightAt = [&](uint32_t x, uint32_t z)
		{
			const float dx = ((float)x - cx) * cell;
			const float dz = ((float)z - cz) * cell;
			const float w = Weight(dx, dz, stroke.Direction);
			if (w <= 0.0f || PatternKind == Pattern::None)
				return w;
			const auto [px, pz] = localOf(x, z);
			return w * PatternAt(px, pz, stroke.Seed);
		};
		// A blend of a sample toward a target height, in sixteen-bit units.
		auto blendToward = [&](uint16_t& h, float target, float w)
		{
			h = (uint16_t)Math::Clamp(Math::Round((float)h + (target - (float)h) * blend * w), 0.0f, 65535.0f);
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
			const float target = Math::Clamp(stroke.StartHeight, 0.0f, 1.0f) * 65535.0f;
			for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
			{
				for (uint32_t x = rect.X0; x <= rect.X1; ++x)
				{
					const float w = weightAt(x, z);
					if (w <= 0.0f)
						continue;
					blendToward(data.Heights[(size_t)z * resolution + x], target, w);
				}
			}
			break;
		}
		case Op::Terrace:
		{
			// Toward the nearest of TerraceSteps levels across the height.
			const float step = 65535.0f / (float)Math::Max(TerraceSteps, 1);
			for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
			{
				for (uint32_t x = rect.X0; x <= rect.X1; ++x)
				{
					const float w = weightAt(x, z);
					if (w <= 0.0f)
						continue;
					uint16_t& h = data.Heights[(size_t)z * resolution + x];
					const float target = Math::Round((float)h / step) * step;
					blendToward(h, target, w);
				}
			}
			break;
		}
		case Op::SetHeight:
		{
			const float target = heightMetres > 0.0f
				? Math::Clamp(TargetHeight / heightMetres, 0.0f, 1.0f) * 65535.0f : 0.0f;
			for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
			{
				for (uint32_t x = rect.X0; x <= rect.X1; ++x)
				{
					const float w = weightAt(x, z);
					if (w <= 0.0f)
						continue;
					blendToward(data.Heights[(size_t)z * resolution + x], target, w);
				}
			}
			break;
		}
		case Op::Ramp:
		{
			// The segment from where the stroke began (at the height it had
			// then) to here (at the height here now), 2 * Radius wide: the
			// target lerps the two heights along it, the weight is the disc's
			// radial rule across it -- distance to the segment, round-capped --
			// times the pattern.
			const float ax = stroke.StartX, az = stroke.StartZ;
			const float bx = localX, bz = localZ;
			const float ah = Math::Clamp(stroke.StartHeight, 0.0f, 1.0f) * 65535.0f;
			const float bh = data.Sample(cx, cz) * 65535.0f;
			const float segX = bx - ax, segZ = bz - az;
			const float segLength2 = segX * segX + segZ * segZ;
			for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
			{
				for (uint32_t x = rect.X0; x <= rect.X1; ++x)
				{
					const auto [px, pz] = localOf(x, z);
					float t = 0.0f;
					if (segLength2 > 1e-8f)
						t = Math::Clamp(((px - ax) * segX + (pz - az) * segZ) / segLength2, 0.0f, 1.0f);
					const float nx = ax + segX * t, nz = az + segZ * t;
					const float d = Math::Sqrt((px - nx) * (px - nx) + (pz - nz) * (pz - nz));
					float w = Weight(d);
					if (w <= 0.0f)
						continue;
					if (PatternKind != Pattern::None)
						w *= PatternAt(px, pz, stroke.Seed);
					if (w <= 0.0f)
						continue;
					const float target = ah + (bh - ah) * t;
					blendToward(data.Heights[(size_t)z * resolution + x], target, w);
				}
			}
			break;
		}
		case Op::Erode:
		{
			// Hydraulic erosion by droplets (Beyer's algorithm, the one every
			// terrain tool ships): each lands where the kernel is thick, rolls
			// downhill with a little inertia, carries sediment while it has
			// the speed and the water, drops it when it slows or climbs, and
			// dies at the footprint's edge or after its lifetime. In a float
			// copy of the footprint, rounded back at the end of the step;
			// every take and every drop is scaled by the kernel at its spot,
			// so the brush's edge is soft. Seeded, so a stroke replays.
			constexpr int   kLifetime = 30;
			constexpr float kInertia = 0.05f;
			constexpr float kCapacity = 4.0f;
			constexpr float kMinSlope = 0.01f;
			constexpr float kErodeSpeed = 0.3f;
			constexpr float kDepositSpeed = 0.3f;
			constexpr float kEvaporate = 0.01f;
			constexpr float kGravity = 4.0f;
			constexpr int   kErodeRadius = 2;

			const uint32_t width = rect.Width(), height = rect.Height();
			if (width < 3 || height < 3)
				break;
			std::vector<float> h((size_t)width * height);
			for (uint32_t z = 0; z < height; ++z)
				for (uint32_t x = 0; x < width; ++x)
					h[(size_t)z * width + x] = (float)data.Heights[(size_t)(rect.Z0 + z) * resolution + rect.X0 + x] / 65535.0f;
			std::vector<float> weights((size_t)width * height);
			float maxWeight = 0.0f;
			for (uint32_t z = 0; z < height; ++z)
				for (uint32_t x = 0; x < width; ++x)
					maxWeight = Math::Max(maxWeight, weights[(size_t)z * width + x] = weightAt(rect.X0 + x, rect.Z0 + z));
			if (maxWeight <= 0.0f)
				break;

			// Height and gradient by bilinear interpolation inside the copy.
			auto sample = [&](float px, float pz, float& gx, float& gz)
			{
				const int x0 = Math::Clamp((int)Math::Floor(px), 0, (int)width - 2);
				const int z0 = Math::Clamp((int)Math::Floor(pz), 0, (int)height - 2);
				const float fx = Math::Clamp(px - (float)x0, 0.0f, 1.0f);
				const float fz = Math::Clamp(pz - (float)z0, 0.0f, 1.0f);
				const float h00 = h[(size_t)z0 * width + x0], h10 = h[(size_t)z0 * width + x0 + 1];
				const float h01 = h[(size_t)(z0 + 1) * width + x0], h11 = h[(size_t)(z0 + 1) * width + x0 + 1];
				gx = (h10 - h00) * (1.0f - fz) + (h11 - h01) * fz;
				gz = (h01 - h00) * (1.0f - fx) + (h11 - h10) * fx;
				return h00 * (1.0f - fx) * (1.0f - fz) + h10 * fx * (1.0f - fz) + h01 * (1.0f - fx) * fz + h11 * fx * fz;
			};
			auto weightAtPoint = [&](float px, float pz)
			{
				const int x = Math::Clamp((int)Math::Round(px), 0, (int)width - 1);
				const int z = Math::Clamp((int)Math::Round(pz), 0, (int)height - 1);
				return weights[(size_t)z * width + x];
			};

			Die die(stroke.Seed * 2654435761u + 0x51ed270bu);
			const int droplets = Math::Max((int)Math::Round(kDropletsPerSecond * strength * dt), 1);
			for (int i = 0; i < droplets; ++i)
			{
				// Where it lands: rejection-sampled by the kernel.
				float px = 0.0f, pz = 0.0f;
				bool landed = false;
				for (int attempt = 0; attempt < 8 && !landed; ++attempt)
				{
					px = die.Unit() * (float)(width - 1);
					pz = die.Unit() * (float)(height - 1);
					landed = die.Unit() * maxWeight <= weightAtPoint(px, pz);
				}
				if (!landed)
					continue;

				float dirX = 0.0f, dirZ = 0.0f, speed = 1.0f, water = 1.0f, sediment = 0.0f;
				for (int life = 0; life < kLifetime; ++life)
				{
					float gx, gz;
					const float oldHeight = sample(px, pz, gx, gz);
					dirX = dirX * kInertia - gx * (1.0f - kInertia);
					dirZ = dirZ * kInertia - gz * (1.0f - kInertia);
					const float length = Math::Sqrt(dirX * dirX + dirZ * dirZ);
					if (length < 1e-6f)
					{
						const float a = die.Unit() * 2.0f * Math::Pi;
						dirX = Math::Cos(a); dirZ = Math::Sin(a);
					}
					else
					{
						dirX /= length; dirZ /= length;
					}
					const float oldX = px, oldZ = pz;
					px += dirX; pz += dirZ;
					// Out of the footprint: the droplet is done. The recorder
					// covers the footprint and nothing else.
					if (px < 0.0f || pz < 0.0f || px > (float)(width - 1) || pz > (float)(height - 1))
						break;

					float ngx, ngz;
					const float newHeight = sample(px, pz, ngx, ngz);
					const float deltaHeight = newHeight - oldHeight;
					const float capacity = Math::Max(-deltaHeight, kMinSlope) * speed * water * kCapacity;
					const float w = weightAtPoint(oldX, oldZ);

					if (sediment > capacity || deltaHeight > 0.0f)
					{
						// Drop: fill the hole it would climb, or the excess.
						const float amount = (deltaHeight > 0.0f ? Math::Min(deltaHeight, sediment)
																 : (sediment - capacity) * kDepositSpeed) * w;
						sediment -= amount;
						const int x0 = Math::Clamp((int)Math::Floor(oldX), 0, (int)width - 2);
						const int z0 = Math::Clamp((int)Math::Floor(oldZ), 0, (int)height - 2);
						const float fx = oldX - (float)x0, fz = oldZ - (float)z0;
						h[(size_t)z0 * width + x0] += amount * (1.0f - fx) * (1.0f - fz);
						h[(size_t)z0 * width + x0 + 1] += amount * fx * (1.0f - fz);
						h[(size_t)(z0 + 1) * width + x0] += amount * (1.0f - fx) * fz;
						h[(size_t)(z0 + 1) * width + x0 + 1] += amount * fx * fz;
					}
					else
					{
						// Take: at most what would level the step, spread over
						// a small disc weighted by nearness.
						const float amount = Math::Min((capacity - sediment) * kErodeSpeed, -deltaHeight) * w;
						float total = 0.0f;
						const int cxI = (int)Math::Round(oldX), czI = (int)Math::Round(oldZ);
						for (int dz = -kErodeRadius; dz <= kErodeRadius; ++dz)
							for (int dx = -kErodeRadius; dx <= kErodeRadius; ++dx)
							{
								const int x = cxI + dx, z = czI + dz;
								if (x < 0 || z < 0 || x >= (int)width || z >= (int)height) continue;
								const float d = Math::Sqrt((float)(dx * dx + dz * dz));
								if (d > (float)kErodeRadius) continue;
								total += (float)kErodeRadius - d;
							}
						if (total > 0.0f)
						{
							float taken = 0.0f;
							for (int dz = -kErodeRadius; dz <= kErodeRadius; ++dz)
								for (int dx = -kErodeRadius; dx <= kErodeRadius; ++dx)
								{
									const int x = cxI + dx, z = czI + dz;
									if (x < 0 || z < 0 || x >= (int)width || z >= (int)height) continue;
									const float d = Math::Sqrt((float)(dx * dx + dz * dz));
									if (d > (float)kErodeRadius) continue;
									float& cellHeight = h[(size_t)z * width + x];
									const float share = amount * ((float)kErodeRadius - d) / total;
									const float take = Math::Min(share, cellHeight);
									cellHeight -= take;
									taken += take;
								}
							sediment += taken;
						}
					}

					speed = Math::Sqrt(Math::Max(speed * speed + deltaHeight * kGravity, 0.0f));
					water *= (1.0f - kEvaporate);
				}
			}

			for (uint32_t z = 0; z < height; ++z)
				for (uint32_t x = 0; x < width; ++x)
					data.Heights[(size_t)(rect.Z0 + z) * resolution + rect.X0 + x] =
						(uint16_t)Math::Clamp(Math::Round(h[(size_t)z * width + x] * 65535.0f), 0.0f, 65535.0f);
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
