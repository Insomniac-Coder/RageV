#pragma once

// The terrain brush (ENGINE-NOTES 7ar, 7as): a kernel under the cursor --
// a disc with a hardness, or a greyscale mask rotated to an angle, times a
// pattern laid over the ground -- and an operation: raise, lower, smooth,
// flatten, paint a layer, terrace, ramp, set a height, erode. One step at a
// time on a TerrainData. A function of the data and nothing else -- no
// device, no scene, no editor -- so the suite can assert its arithmetic and
// the editor's tool is only the hand that holds it.
//
// Rates are relative to the terrain's height and to time, never to frames:
// a full-strength, full-weight raise climbs a quarter of `height` per
// second; a smooth, a flatten, a paint or any of the blends closes at most
// an eighth of its gap per sixtieth of a second; erosion is droplets per
// second. What a check can ask for is what the user gets whatever the
// frame rate.

#include "TerrainData.h"

#include <cstdint>
#include <memory>
#include <vector>

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

	// A square greyscale image the brush reads (7as): a shape laid over the
	// brush's square, or a pattern tiled over the ground. Values in [0, 1],
	// row-major from the top left. The editor decodes them from the PNGs in
	// its assets/brushes folder; the suite builds them by hand.
	struct BrushMask
	{
		uint32_t Size = 0;
		std::vector<float> Values;

		bool Valid() const { return Size >= 2 && Values.size() == (size_t)Size * Size; }
		// Bilinear at (u, v), (0, 0) the top-left corner and (1, 1) the bottom
		// right. Outside [0, 1]^2 the answer is 0 -- or, `wrap`, the image
		// tiles. Invalid masks read 0.
		float Sample(float u, float v, bool wrap) const;
		// From an image file's bytes -- anything stb_image reads; the red
		// channel is the value, so a greyscale PNG and an RGB one agree. False
		// (and `out` untouched) when the bytes are not an image or the image
		// is not square.
		static bool Decode(const std::vector<uint8_t>& bytes, BrushMask& out);
	};

	struct TerrainBrush
	{
		enum class Op : int { Raise = 0, Smooth, Flatten, Paint, Terrace, Ramp, SetHeight, Erode };
		static constexpr int kModeCount = 8;
		static const char* ModeName(Op mode);

		// The kernel's shape (7as): the disc with its hardness, or a mask
		// rotated to an angle. And the pattern over the ground that multiplies
		// it: none, the brush's own noise, or a mask tiled at a scale.
		enum class Shape : int { Disc = 0, Mask };
		enum class Pattern : int { None = 0, Noise, Tiled };

		Op    Mode = Op::Raise;
		// Metres from the centre to the rim (or to the mask's edge).
		float Radius = 8.0f;
		// 0..1. The rate multiplier: see the rates above.
		float Strength = 0.5f;
		// 0..1. The fraction of the radius at full weight before the fall-off
		// begins: 0 is a soft cone from the centre, 1 a hard disc. The disc's;
		// a mask is its own fall-off and ignores it.
		float Hardness = 0.5f;
		// The layer Paint moves weight toward, 0..3.
		int   Layer = 1;
		// Shift: Raise lowers, Paint erases; Set Height reads it as "sample the
		// height under the cursor" (the tool's gesture, not the step's). The
		// rest ignore it.
		bool  Invert = false;

		Shape ShapeKind = Shape::Disc;
		// The mask under Shape::Mask; null reads as no weight anywhere.
		std::shared_ptr<const BrushMask> ShapeMask;
		// Radians the mask is turned, counter-clockwise seen from above (+x
		// toward +z is a quarter turn); with FollowStroke the direction of the
		// last movement is added, so a streak lies along the drag.
		float Angle = 0.0f;
		bool  FollowStroke = false;

		Pattern PatternKind = Pattern::None;
		std::shared_ptr<const BrushMask> PatternMask;
		// Metres per repeat of the noise or the tiled mask.
		float PatternScale = 16.0f;

		// Terrace: how many levels across the terrain's Height.
		int   TerraceSteps = 8;
		// Set Height: the height aimed at, in metres.
		float TargetHeight = 0.0f;

		// The rates, as fractions per second at strength 1 and weight 1.
		static constexpr float kRaisePerSecond = 0.25f;     // of `height`
		static constexpr float kBlendPerStep = 0.125f;      // of the gap, per 1/60 s
		static constexpr float kStepSeconds = 1.0f / 60.0f;
		static constexpr float kDropletsPerSecond = 1500.0f;

		// The stroke's context a step reads (7as): where the stroke began and
		// the height there in [0, 1] (Flatten's target, Ramp's start), the
		// direction of the last movement in radians (what FollowStroke adds),
		// and a seed the caller advances every step (what Erode's droplets
		// and the noise pattern draw from, so a scripted stroke is the same
		// stroke twice).
		struct Stroke
		{
			float StartX = 0.0f, StartZ = 0.0f;
			float StartHeight = 0.0f;
			float Direction = 0.0f;
			uint32_t Seed = 0;
		};

		// The disc's radial rule: 1 inside hardness * radius, then a
		// smoothstep down to 0 at the rim, 0 beyond. What Shape::Disc reads,
		// what the ramp reads across its line, and what the ring shows.
		float Weight(float distance) const;
		// The shape's weight at an offset from the centre in metres, the mask
		// turned by Angle (plus `direction` when FollowStroke). The disc
		// answers by distance; a mask by its bilinear value inside the turned
		// square and 0 outside.
		float Weight(float dx, float dz, float direction) const;
		// The pattern's value at a terrain-local point in metres: 1 for None,
		// the noise or the tiled mask in [0, 1] otherwise.
		float PatternAt(float localX, float localZ, uint32_t seed) const;
		// The brush's own value noise, three octaves, in [0, 1]: a pure hash of
		// the position and the seed, the same on every machine.
		static float Noise(float x, float z, uint32_t seed);

		// The inclusive sample rectangle a step at (localX, localZ) would
		// write to: the kernel's bounding box (the disc's; a mask's is that
		// grown by root two, whatever the angle) clamped to the grid, empty
		// when it misses. What a stroke records *before* the step. The Stroke
		// form is the one Ramp needs -- its box is the segment's from the
		// start to here, grown by the radius; every other mode ignores it.
		TerrainRect Footprint(const TerrainData& data, float sizeMetres,
							  float localX, float localZ) const;
		TerrainRect Footprint(const TerrainData& data, float sizeMetres,
							  float localX, float localZ, const Stroke& stroke) const;

		// One step of a stroke on `data`, a terrain `sizeMetres` a side and
		// `heightMetres` at a full sample, centred at terrain-local (localX,
		// localZ) metres, for `dt` seconds. Returns the inclusive sample
		// rectangle the step wrote to (empty when the kernel missed the grid).
		// The 7ar form takes Flatten's target alone and builds a Stroke that
		// began here with it.
		TerrainRect Apply(TerrainData& data, float sizeMetres, float heightMetres,
						  float localX, float localZ, float flattenTarget, float dt) const;
		TerrainRect Apply(TerrainData& data, float sizeMetres, float heightMetres,
						  float localX, float localZ, const Stroke& stroke, float dt) const;

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
