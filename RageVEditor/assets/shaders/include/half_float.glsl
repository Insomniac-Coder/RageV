// **A running average kept in a half float, without losing light to the write.**
//
// Every temporal filter here keeps its average in a half-float target and
// moves it a small share of the way toward each new estimate: a sixty-fourth
// in the signal contract, a fiftieth in the temporal resolve. That move is
// usually smaller than one step of the half's grid -- a thousandth of the
// value -- so the stored result is almost never representable and the
// conversion has to round it.
//
// **On this engine's GPU that conversion rounds toward zero** (measured
// 2026-09-13, RT-5: the stored value equalled the exact update rounded down in
// 99.9% of 1.3 million texel-frames, and rounded to nearest in 50.2%). So each
// frame takes half a step on average, and an average that forgets at 1/n
// settles about n/2 steps low: the direct light's diffuse sat 1.7% under its
// own estimate, the wet floor's final colour 3.4% under what it should be.
// The loss only happens where the input moves -- a still input is never
// rounded, because a converged average stops changing -- which is why it
// showed up as "sampled lights read darker than every light", why every
// clamp ablation said nothing, and why it scales with each filter's memory.
//
// **The value is rounded onto the half grid here, stochastically**: up with
// the chance of how far past the step below it sits. The stored value is then
// exactly representable, the hardware has nothing left to round on any
// backend or vendor, and the mean of many frames is the mean of the values.
// Adding half a step instead would be right only on hardware that truncates
// and a half step too bright everywhere else; a 32-bit history would double
// the memory of every one of them to fix a rounding.
//
// Guarded for octahedral.glsl's reason: a shader may reach this more than once.

#ifndef RV_HALF_FLOAT_GLSL
#define RV_HALF_FLOAT_GLSL

#include "dither.glsl"

// The draw that decides each rounding: independent per pixel, per frame and
// per value written, so two values on one pixel do not round together and a
// pixel does not round the same way two frames running.
float HalfRoundingDraw(uvec2 pixel, uint frame, uint value)
{
	return Rand01(HashU(pixel.x ^ HashU(pixel.y ^ HashU(frame ^ (value * 0x9E3779B9u)))));
}

// `v` on the half-float grid, rounded up with probability equal to its
// distance past the step below. Zero, anything past the largest half and
// anything that is not a number come back untouched: the first is already on
// the grid and the others were lost before they got here.
float StoreAsHalf(float v, float u)
{
	const float magnitude = abs(v);
	if (!(magnitude > 0.0 && magnitude < 65504.0))
		return v;
	int exponent;
	frexp(magnitude, exponent);
	// The half's spacing at this magnitude: 2^(exponent - 11) across the
	// normal range, and the subnormals' 2^-24 below it.
	const float spacing = ldexp(1.0, max(exponent - 11, -24));
	const float below = floor(magnitude / spacing) * spacing;
	const float rounded = below + ((magnitude - below) > u * spacing ? spacing : 0.0);
	return v < 0.0 ? -rounded : rounded;
}

vec3 StoreAsHalf(vec3 v, float u)
{
	return vec3(StoreAsHalf(v.x, u), StoreAsHalf(v.y, u), StoreAsHalf(v.z, u));
}

// **The same grid, to the nearest step and with no draw -- for an average
// that something thresholds rather than something that is looked at.**
//
// The ray allocator's averaged demand decides whole ray counts across a dead
// band and a coverage floor, and a tile whose demand sits at one of those
// edges is exactly where the stochastic rounding's zero-mean wobble becomes a
// crossing: with it the sixty-second still test on the bridge went from 2
// restless tiles to 7 and from 0.0082 changes per tile per second to 0.0203,
// over its 0.01 bar (2026-09-13). Nearest rounding keeps the stored value
// representable and the truncation's one-way drift gone, and adds nothing
// that moves. What it gives up is the average's exactness -- a converged
// value may sit a few steps either side of its input -- which a picture would
// show and a threshold does not.
float StoreAsHalfNearest(float v)
{
	const float magnitude = abs(v);
	if (!(magnitude > 0.0 && magnitude < 65504.0))
		return v;
	int exponent;
	frexp(magnitude, exponent);
	const float spacing = ldexp(1.0, max(exponent - 11, -24));
	const float rounded = floor(magnitude / spacing + 0.5) * spacing;
	return v < 0.0 ? -rounded : rounded;
}

#endif
