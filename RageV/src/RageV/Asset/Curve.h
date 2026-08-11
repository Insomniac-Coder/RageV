#pragma once

#include "RageV/Math/Types.h"

#include <cstdint>
#include <vector>

namespace RageV
{
	// A keyed ramp over normalised time, which is what every "over life"
	// setting on a particle emitter wants and what a start/end pair cannot
	// express: smoke that swells fast and then holds, a spark that flashes and
	// decays, a puff that fades in *and* out.
	//
	// One type covers both the scalar curves (size, alpha) and the colour
	// gradient, because everything except how many numbers a key carries is
	// identical -- the file, the handle, the importer, the sampling. Channels
	// says which it is: 1 for a curve, 3 for a gradient. Keeping them one type
	// is not tidiness, it is the reason there is one asset path to maintain
	// rather than two that drift.
	class Curve
	{
	public:
		// Four is what a Vec4 holds and what the LUT bakes into an RGBA texel.
		// Nothing needs more, and a fixed ceiling keeps a key trivially
		// copyable and the whole curve a flat allocation.
		static constexpr uint32_t kMaxChannels = 4;

		struct Key
		{
			// Position along the ramp. Not seconds: a particle's life is
			// normalised so the same curve fits a 0.2 s spark and a 6 s plume.
			float Time = 0.0f;
			float Value[kMaxChannels] = { 0.0f, 0.0f, 0.0f, 0.0f };
		};

		Curve() = default;
		explicit Curve(uint32_t channels) : m_Channels(Clamp(channels)) {}

		uint32_t GetChannels() const { return m_Channels; }
		void SetChannels(uint32_t channels) { m_Channels = Clamp(channels); }

		const std::vector<Key>& GetKeys() const { return m_Keys; }
		bool IsEmpty() const { return m_Keys.empty(); }
		size_t GetKeyCount() const { return m_Keys.size(); }

		// Inserts in time order and answers where it landed, so a caller that
		// just added a key by dragging can keep hold of it. Keeping the vector
		// sorted on insert rather than sorting on evaluate means sampling --
		// which happens per particle per frame -- never pays for authoring,
		// which happens when somebody drags a point.
		size_t AddKey(float time, const Vec4& value);
		size_t AddKey(float time, float value) { return AddKey(time, Vec4(value, 0.0f, 0.0f, 0.0f)); }

		void RemoveKey(size_t index);
		void Clear() { m_Keys.clear(); }

		// Moves a key and re-sorts if it crossed a neighbour, answering its new
		// index. This is what a drag calls, and the re-sort is why a drag can
		// pull one point past another without the curve turning inside out.
		size_t MoveKey(size_t index, float time, const Vec4& value);

		// Samples at normalised time, clamped at both ends: before the first
		// key answers the first key, after the last answers the last. Holding
		// rather than extrapolating is deliberate -- a particle's age can reach
		// exactly 1.0, and an extrapolating curve would put a value nobody
		// authored on the last frame of every particle's life.
		Vec4 Evaluate(float time) const;
		float EvaluateScalar(float time) const { return Evaluate(time).x; }

		// What an empty curve answers, and what a null asset handle should be
		// treated as. Named rather than left as a magic zero because "no curve"
		// reaching the renderer must be a visible decision, not an accident.
		static Vec4 Fallback() { return Vec4(0.0f, 0.0f, 0.0f, 0.0f); }

		// The two shapes every ramp used to have, so an emitter that has always
		// interpolated start to end can be expressed as a curve without anybody
		// authoring one.
		static Curve Linear(const Vec4& from, const Vec4& to, uint32_t channels);
		static Curve Constant(const Vec4& value, uint32_t channels);

		// Samples into `out` at `count` evenly spaced points from 0 to 1
		// inclusive, which is exactly what the GPU LUT wants. The CPU
		// simulation reads the same baked table rather than evaluating keys, so
		// the two paths cannot disagree -- and a disagreement between them
		// would read as a blending bug rather than a sampling one.
		void Bake(Vec4* out, uint32_t count) const;

		bool operator==(const Curve& other) const;
		bool operator!=(const Curve& other) const { return !(*this == other); }

		// A curve flattened to a fixed table, which is the form both the CPU
		// and the GPU read.
		//
		// The CPU could evaluate keys directly and be exact, and that is
		// precisely why it does not: the compute shader will sample a texture,
		// so an emitter switched from CPU to GPU would change appearance
		// slightly, and the difference would look like a simulation bug rather
		// than two different samplers. One table, both paths, same answer --
		// the same discipline that already keeps the two instance layouts
		// identical.
		struct Baked
		{
			// 64 texels resolves anything hand-authored. A ramp needs to be
			// visibly kinked at a scale finer than 1/64 of a particle's life
			// before this loses it.
			static constexpr uint32_t kSize = 64;

			Vec4 Samples[kSize] = {};

			// Linear between neighbouring samples, clamped at both ends.
			//
			// Matched to a linear-filtered texture on purpose: a shader
			// sampling this table as a kSize-wide 1D texture must read at
			// `(t * (kSize - 1) + 0.5) / kSize` to land on the same texel
			// centres and get the same answer. That expression is the contract
			// between this function and the compute shader.
			Vec4 Sample(float t) const;
			float SampleScalar(float t) const { return Sample(t).x; }
		};

		Baked BakeTable() const;

	private:
		static uint32_t Clamp(uint32_t channels);

		std::vector<Key> m_Keys;
		uint32_t m_Channels = 1;
	};
}
