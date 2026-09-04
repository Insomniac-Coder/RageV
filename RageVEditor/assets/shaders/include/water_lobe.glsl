// The sea's specular lobe: anisotropic Beckmann, and its masking term.
//
// **Its own file since WR-16 S4b**, because two stages need it now. It was
// written inside pbr_fragment.glsl for the water's fragment shading; the
// pass that chooses and shades the sea's lamps needs exactly the same lobe
// to score a candidate with, and a second copy of a distribution is a second
// thing to keep in step. Nothing here touches a binding or a varying -- they
// are functions of vectors and numbers -- so both stages can include it.

// **Beckmann, not GGX, for the water's analytic lights -- and anisotropic.**
// Cox and Munk photographed the sun's glitter from the air and fitted the sea
// surface's slope distribution: it is a Gaussian, which is exactly Beckmann's
// microfacet density, where GGX's heavy tails smear the glitter into a haze
// over the whole sea instead of a defined track. And they fitted it *twice*,
// because the variances differ along and across the wind -- which is what
// turns the highlight from a disc into the streak a low light actually lays
// on wind-driven water.
//
// **The roughness here is the RMS slope itself, not squared.** The footprint
// block chose its 0.24 ceiling as Cox-Munk's slope at a fresh breeze, so
// feeding it through the perceptual alpha = r*r convention would collapse the
// horizon variance to a hundredth of the measured sea. The two conventions
// meet at the numbers this shader actually runs -- water stays under 0.3 --
// and the environment split-sum keeps the perceptual mapping it was built
// with, so only these two functions read it this way.
//
// The ratio 1.16 : 0.86 is the square root of Cox-Munk's asymptotic variance
// ratio (3.16 : 1.92 per unit wind), applied about the shared mean so the
// total slope energy the footprint block budgeted is preserved.
// The explicit-axis core, split out so a sized light can evaluate the lobe in
// a frame and width of its own (the streak frame below). The wind-frame form
// wraps it with the Cox-Munk axes and is bit-identical to what it replaced.
float WaterBeckmannDX(vec3 H, vec3 N, vec3 T, vec3 B, float ax, float ay)
{

	const float NdotH = dot(N, H);
	// **Strictly above zero is not enough.** In the last millradian before
	// grazing, exp(-slope) underflows to 0.0 while n2*n2 underflows too, and
	// 0/0 is NaN -- one NaN pixel that TAA's history then keeps forever,
	// because min and max of a NaN are NaN and the clamp cannot reject it.
	// Unreachable while the range window kept lamps off grazing water; the
	// moment a light could reach the horizon band (area lamps at 600 m), the
	// sea grew white-cored holes that multiplied frame over frame. A facet
	// this far from the normal carries no visible energy, so zero is the
	// honest answer as well as the safe one.
	if (NdotH <= 1.0e-3)
		return 0.0;

	const float th = dot(T, H);
	const float bh = dot(B, H);
	const float n2 = NdotH * NdotH;

	const float slope = ((th * th) / (ax * ax) + (bh * bh) / (ay * ay)) / n2;
	return exp(-slope) / (PI * ax * ay * n2 * n2);
}

float WaterBeckmannD(vec3 H, vec3 N, vec3 T, vec3 B, float roughness)
{
	return WaterBeckmannDX(H, N, T, B,
						   max(roughness * 1.16, 0.02),
						   max(roughness * 0.86, 0.02));
}

// Walter's rational fit of the Beckmann Smith shadowing term, one direction,
// with the roughness projected into that direction's azimuth so the term
// matches the anisotropic lobe above rather than an isotropic stand-in.
float WaterBeckmannG1X(vec3 v, vec3 N, vec3 T, vec3 B, float ax, float ay)
{
	const float NdotV = dot(N, v);
	if (NdotV <= 0.0)
		return 0.0;

	const float tv = dot(T, v);
	const float bv = dot(B, v);
	const float azimuth2 = max(tv * tv + bv * bv, 1.0e-8);
	const float alpha = sqrt((tv * tv * ax * ax + bv * bv * ay * ay) / azimuth2);

	const float tanTheta = sqrt(max(1.0 - NdotV * NdotV, 0.0)) / max(NdotV, 1.0e-4);
	const float a = 1.0 / max(alpha * tanTheta, 1.0e-4);
	if (a >= 1.6)
		return 1.0;
	return (3.535 * a + 2.181 * a * a) / (1.0 + 2.276 * a + 2.577 * a * a);
}

float WaterBeckmannG1(vec3 v, vec3 N, vec3 T, vec3 B, float roughness)
{
	return WaterBeckmannG1X(v, N, T, B,
							max(roughness * 1.16, 0.02),
							max(roughness * 0.86, 0.02));
}
