#include <rvpch.h>
#include "Curve.h"

#include <algorithm>

namespace RageV
{
	uint32_t Curve::Clamp(uint32_t channels)
	{
		if (channels < 1)
			return 1;
		return channels > kMaxChannels ? kMaxChannels : channels;
	}

	size_t Curve::AddKey(float time, const Vec4& value)
	{
		Key key;
		key.Time = time;
		for (uint32_t channel = 0; channel < kMaxChannels; channel++)
			key.Value[channel] = value[(int)channel];

		// Upper bound rather than lower: two keys authored at the same time is
		// legal and means a hard step, and the one added later should sit after
		// the one already there. Sorting the other way would make adding a step
		// silently reorder the pair somebody just made.
		const auto position = std::upper_bound(m_Keys.begin(), m_Keys.end(), time,
			[](float t, const Key& existing) { return t < existing.Time; });

		const size_t index = (size_t)(position - m_Keys.begin());
		m_Keys.insert(position, key);
		return index;
	}

	void Curve::RemoveKey(size_t index)
	{
		if (index < m_Keys.size())
			m_Keys.erase(m_Keys.begin() + (ptrdiff_t)index);
	}

	size_t Curve::MoveKey(size_t index, float time, const Vec4& value)
	{
		if (index >= m_Keys.size())
			return index;

		// Removing and re-adding keeps one definition of "in time order"
		// instead of a second, subtly different one here.
		m_Keys.erase(m_Keys.begin() + (ptrdiff_t)index);
		return AddKey(time, value);
	}

	Vec4 Curve::Evaluate(float time) const
	{
		if (m_Keys.empty())
			return Fallback();

		// Clamped at both ends. A particle's normalised age reaches exactly 1.0
		// on its final frame, so the last key has to be reachable rather than
		// being a limit the curve approaches.
		if (time <= m_Keys.front().Time)
		{
			const Key& first = m_Keys.front();
			return { first.Value[0], first.Value[1], first.Value[2], first.Value[3] };
		}

		const Key& last = m_Keys.back();
		if (time >= last.Time)
			return { last.Value[0], last.Value[1], last.Value[2], last.Value[3] };

		// The first key strictly after `time`; the span is that one and the one
		// before it. The two clamps above guarantee both exist.
		const auto upper = std::upper_bound(m_Keys.begin(), m_Keys.end(), time,
			[](float t, const Key& key) { return t < key.Time; });

		const Key& b = *upper;
		const Key& a = *(upper - 1);

		const float span = b.Time - a.Time;

		// Coincident keys are a step, not a division by zero.
		const float t = span > 0.0f ? (time - a.Time) / span : 1.0f;

		Vec4 result;
		for (uint32_t channel = 0; channel < kMaxChannels; channel++)
			result[(int)channel] = a.Value[channel] + (b.Value[channel] - a.Value[channel]) * t;

		return result;
	}

	Curve Curve::Linear(const Vec4& from, const Vec4& to, uint32_t channels)
	{
		Curve curve(channels);
		curve.AddKey(0.0f, from);
		curve.AddKey(1.0f, to);
		return curve;
	}

	Curve Curve::Constant(const Vec4& value, uint32_t channels)
	{
		Curve curve(channels);
		curve.AddKey(0.0f, value);
		return curve;
	}

	void Curve::Bake(Vec4* out, uint32_t count) const
	{
		if (!out || count == 0)
			return;

		// Inclusive of both ends: with count texels the last one is time 1.0,
		// so a curve's final value survives baking. Dividing by count rather
		// than count - 1 would stop just short of it and quietly clip the end
		// of every ramp.
		const float step = count > 1 ? 1.0f / (float)(count - 1) : 0.0f;

		for (uint32_t i = 0; i < count; i++)
			out[i] = Evaluate((float)i * step);
	}

	bool Curve::operator==(const Curve& other) const
	{
		if (m_Channels != other.m_Channels || m_Keys.size() != other.m_Keys.size())
			return false;

		for (size_t i = 0; i < m_Keys.size(); i++)
		{
			if (m_Keys[i].Time != other.m_Keys[i].Time)
				return false;

			for (uint32_t channel = 0; channel < kMaxChannels; channel++)
			{
				if (m_Keys[i].Value[channel] != other.m_Keys[i].Value[channel])
					return false;
			}
		}

		return true;
	}
}
