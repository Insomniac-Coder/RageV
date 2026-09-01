// TPDF dither (WR-2, Playdead's INSIDE recipe) and the integer hash it
// shares with tonemap.rvshader's film grain.
//
// An integer hash, not `fract(sin(x) * 43758.5453)`. The sine version is not
// *reproducible*: the precision of `sin` is implementation-defined and the
// answer lives in the low bits the two backends are least likely to agree
// on. Integer multiplies and shifts are exact and wrap identically
// everywhere, which is what makes a dither pattern something a Vulkan and
// an OpenGL screenshot can be compared on at all.
//
// lowbias32 (Wellons) -- moved here from tonemap.rvshader (WR-2) so sky and
// fog can seed dither the same reproducible way rather than each growing
// their own copy to drift from it.
uint HashU(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

// One uniform sample in [0, 1). 24 bits so the conversion to float is
// exact rather than rounded -- the same property tonemap.rvshader's
// Lattice() relies on for grain.
float Rand01(uint seed)
{
	return float(HashU(seed) >> 8) * (1.0 / 16777216.0);
}

// Triangular dither, added identically to every channel: two independent
// uniform draws so the sum has zero mean and no bias, rather than the
// visible steps a single uniform draw leaves at its own quantisation
// boundaries. ~1 LSB peak-to-peak at an 8-bit write.
//
// Seeded by pixel position, the frame number and a per-pass salt. The frame
// number is what makes this animate instead of sitting still -- static
// noise reads as dirt on the lens, animated noise reads as film grain, the
// same requirement tonemap.rvshader's grain has and the same reason: "the
// frame number, not a clock: reproduce, so a screenshot of frame 30 is the
// same picture every time." The salt keeps sky, fog and tonemap's dither
// from drawing the identical pattern on the identical pixel and stacking
// coherently into 3 LSB instead of 1 -- each pass passes its own constant.
float TpdfDither(vec2 fragCoord, uint frame, uint pass)
{
	uint seed = HashU(uint(fragCoord.x) ^ HashU(uint(fragCoord.y) ^ HashU(frame ^ pass)));
	float r1 = Rand01(seed);
	float r2 = Rand01(seed ^ 0x9e3779b9u);
	return (r1 + r2 - 1.0) * (1.0 / 255.0);
}
