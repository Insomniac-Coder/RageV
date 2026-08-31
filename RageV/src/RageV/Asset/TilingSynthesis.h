#pragma once

// Histogram-preserving texture synthesis (Heitz & Neyret 2018), run once into
// a larger tile rather than per pixel per frame.
//
// **What this buys and what it does not.** Stochastic tiling in the shader is
// genuinely non-periodic: three hex-tile neighbours at randomised offsets,
// blended, so no square metre of a surface repeats another. It costs three
// texture fetches per map, forever. Baking that cannot preserve the property --
// a baked texture is finite, so it tiles, and all you have is a bigger tile.
//
// A bigger tile is worth having. Synthesising 4K from a 1K source pushes the
// repeat four times further away at no runtime cost at all, and on a pier
// wearing a 1 m concrete that is the difference between a visible lattice and
// a surface.
//
// **The blend has to happen in Gaussian space or it destroys the texture.**
// Averaging independent draws of a distribution shrinks its variance, so
// overlapping copies blended naively come out flat and soft -- the contrast is
// gone and the result reads as mud whatever the source was. So: transform each
// channel onto a unit Gaussian by sorting its histogram, blend there with
// weights normalised to preserve variance (`sum(w G) / sqrt(sum(w^2))`), then
// map back through the inverse. The output then carries the source's histogram
// exactly -- same mean, same contrast, same tails. Measured on Concrete025:
// source 181.83/27.92, synthesised 181.50/27.93.

#include <cstdint>
#include <vector>

namespace RageV::Assets
{
	struct SynthesisRequest
	{
		// Source pixels, 8 bit, `Channels` interleaved.
		const uint8_t* Pixels = nullptr;
		int Width = 0;
		int Height = 0;
		int Channels = 0;

		// How many times larger the output is, per axis.
		int Scale = 4;

		// Lattice cells across the *output*.
		//
		// **The offsets wrap at this, and that is what makes the result
		// tileable.** Heitz and Neyret synthesise over an unbounded surface and
		// never meet an edge; this output is itself going to be tiled, so its
		// left column has to draw the same samples as its right. Hashing the
		// lattice vertex modulo `Cells` makes the offset field periodic over
		// exactly one output.
		int Cells = 8;

		// **The same seed for every map of one material.** The offsets it
		// drives are shared across channels and across maps by design: the
		// colour, the normal and the packed surface map have to be synthesised
		// from the *same* places in their sources, or they describe different
		// surfaces. Only the histogram transform is per channel.
		uint32_t Seed = 7;
	};

	// **A square lattice split on its diagonal, not the paper's hexagonal one.**
	//
	// The hex lattice is more isotropic -- its skew stretches v by 2/sqrt(3) so
	// the triangles come out equilateral. It cannot be used here: under that
	// skew a vertex index runs to `Cells * 1.1547` along v, so wrapping it at
	// `Cells` wraps somewhere that is not the texture's edge. Measured, that
	// left a vertical seam of 30.7 against an interior neighbour difference of
	// 4.2 -- seven times worse, and plainly visible as a line.
	//
	// A square lattice keeps integer vertices, so the wrap is exact on both
	// axes. Measured after the change: seams of 3.2 and 4.1 against an interior
	// of 4.9 -- that is, the edges match *better* than adjacent pixels do, which
	// is what seamless means. Slightly less isotropic blending is a much smaller
	// price than a seam down every pier.
	//
	// Returns Width*Scale x Height*Scale x Channels, or empty if the request
	// makes no sense.
	std::vector<uint8_t> SynthesiseTiling(const SynthesisRequest& request);
}
