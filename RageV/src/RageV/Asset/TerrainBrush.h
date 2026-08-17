#pragma once

// The terrain brush (ENGINE-NOTES 7ar): a circle under the cursor with a
// size, a strength and a hardness that raises, lowers, smooths and flattens
// the heights of a TerrainData, and paints its four layers, one step at a
// time. A function of the data and nothing else -- no device, no scene, no
// editor -- so the suite can assert its arithmetic and the editor's tool is
// only the hand that holds it.
//
// Rates are relative to the terrain's height and to time, never to frames:
// a full-strength, full-weight raise climbs a quarter of `height` per
// second; a smooth, a flatten or a paint closes at most an eighth of its
// gap per sixtieth of a second. What a check can ask for is what the user
// gets whatever the frame rate.

#include "TerrainData.h"

#include <cstdint>

namespace RageV
{
	// An inclusive rectangle of samples: X0..X1 by Z0..Z1. Empty when X1 < X0.
	struct TerrainRect
	{
		uint32_t X0 = 1, Z0 = 1, X1 = 0, Z1 = 0;

		bool Empty() const { return X1 < X0 || Z1 < Z0; }
		uint32_t Width() const { return Empty() ? 0 : X1 - X0 + 1; }
		uint32_t Height() const { return Empty() ? 0 : Z1 - Z0 + 1; }
		bool Contains(uint32_t x, uint32_t z) const
		{
			return !Empty() && x >= X0 && x <= X1 && z >= Z0 && z <= Z1;
		}
		// The smallest rectangle holding both. Either may be empty.
		TerrainRect Union(const TerrainRect& other) const;
		// Grown by `samples` each way, clamped to a grid of `resolution`.
		TerrainRect Grown(uint32_t samples, uint32_t resolution) const;
	};

	struct TerrainBrush
	{
		enum class Op : int { Raise = 0, Smooth, Flatten, Paint };
		static constexpr int kModeCount = 4;
		static const char* ModeName(Op mode);

		Op    Mode = Op::Raise;
		// Metres from the centre to the rim.
		float Radius = 8.0f;
		// 0..1. The rate multiplier: see the rates above.
		float Strength = 0.5f;
		// 0..1. The fraction of the radius at full weight before the fall-off
		// begins: 0 is a soft cone from the centre, 1 a hard disc.
		float Hardness = 0.5f;
		// The layer Paint moves weight toward, 0..3.
		int   Layer = 1;
		// Shift: Raise lowers, Paint erases. Smooth and Flatten ignore it.
		bool  Invert = false;

		// The rates, as fractions per second at strength 1 and weight 1.
		static constexpr float kRaisePerSecond = 0.25f;     // of `height`
		static constexpr float kBlendPerStep = 0.125f;      // of the gap, per 1/60 s
		static constexpr float kStepSeconds = 1.0f / 60.0f;

		// The kernel weight at `distance` from the centre for a brush of this
		// radius and hardness: 1 inside hardness * radius, then a smoothstep
		// down to 0 at the rim, 0 beyond.
		float Weight(float distance) const;

		// The inclusive sample rectangle a step at (localX, localZ) would
		// write to: the circle's bounding box clamped to the grid, empty when
		// the circle misses it. What a stroke records *before* the step.
		TerrainRect Footprint(const TerrainData& data, float sizeMetres,
							  float localX, float localZ) const;

		// One step of a stroke on `data`, a terrain `sizeMetres` a side and
		// `heightMetres` at a full sample, centred at terrain-local (localX,
		// localZ) metres, for `dt` seconds. `flattenTarget` is the height in
		// [0, 1] Flatten aims at -- the caller samples it where the button went
		// down. Returns the inclusive sample rectangle the step wrote to
		// (empty when the circle missed the grid).
		TerrainRect Apply(TerrainData& data, float sizeMetres, float heightMetres,
						  float localX, float localZ, float flattenTarget, float dt) const;

		// Which of the two arrays this mode edits.
		bool EditsHeights() const { return Mode != Op::Paint; }
		bool EditsWeights() const { return Mode == Op::Paint; }
	};

	// Records the samples a stroke is about to change so one command can undo
	// the whole drag (7ar). Cover(rect) grows the recorded rectangle to hold
	// `rect` and copies every newly covered sample from `data` -- call it
	// *before* the step that writes them. Heights or weights by the brush's
	// mode, never both.
	class TerrainStrokeRecorder
	{
	public:
		void Begin(const TerrainData& data, bool heights);
		void Cover(const TerrainData& data, const TerrainRect& rect);

		bool Active() const { return m_Active; }
		bool RecordsHeights() const { return m_Heights; }
		const TerrainRect& Rect() const { return m_Rect; }
		// Row-major over Rect(): Width() * Height() heights, or that many
		// times TerrainData::kLayers weight bytes.
		const std::vector<uint16_t>& BeforeHeights() const { return m_BeforeHeights; }
		const std::vector<uint8_t>& BeforeWeights() const { return m_BeforeWeights; }
		void End() { m_Active = false; }

	private:
		bool m_Active = false;
		bool m_Heights = true;
		uint32_t m_Resolution = 0;
		TerrainRect m_Rect;
		std::vector<uint16_t> m_BeforeHeights;
		std::vector<uint8_t> m_BeforeWeights;
	};

	// The rectangle's samples copied out of / into a grid, row-major. Shared by
	// the recorder and the stroke command.
	void CopyHeightsOut(const TerrainData& data, const TerrainRect& rect, std::vector<uint16_t>& out);
	void CopyHeightsIn(TerrainData& data, const TerrainRect& rect, const std::vector<uint16_t>& in);
	void CopyWeightsOut(const TerrainData& data, const TerrainRect& rect, std::vector<uint8_t>& out);
	void CopyWeightsIn(TerrainData& data, const TerrainRect& rect, const std::vector<uint8_t>& in);
}
