// The Gerstner sum itself, with every dial an argument.
//
// No #version here: an include is spliced into a file that already has one.
//
// **Split out of water_vertex.glsl for the foam pass, and parameterised for
// the same reason.** The vertex stage reads the dials off its push-constant
// block through the RV_WATER_* macros; the foam accumulation pass is a compute
// shader with a push-constant block of its own and different field names. One
// copy of the sum taking plain arguments is what lets both call it -- two
// copies that have to agree wave for wave is a lattice with two authors, and
// the first time they drift the foam sits beside the crest that made it
// instead of on it.
//
// The reasoning behind the spectrum -- the octave band, the stratification,
// the phases, the fan -- lives at the top of water_vertex.glsl, which is where
// it has always been. This file is the arithmetic.

// How many waves the sum carries.
//
// **Thirty-two, and the number is not arbitrary.** Below about sixteen the eye
// can still pick out the individual constituents; somewhere near thirty to
// sixty it stops being able to, provided they are spectrally distributed. Under
// that threshold, more waves only makes a more complicated lattice.
#define RV_WATER_WAVE_COUNT 32

// How far below the longest wave the shortest sits. **Bounded by the grid, not
// chosen for looks**: a wave shorter than about four grid quads cannot be
// drawn, only aliased, and aliased short waves are exactly the ridged noise
// the spectrum rewrite exists to remove. Twelve is a little under 3.6 octaves,
// which is what a 6 m grid can carry under a 26 m dominant wave.
#define RV_WATER_BAND 12.0

// A cheap hash for the per-wave phase and the direction jitter. Any hash will
// do; what matters is that consecutive waves get unrelated values, so the
// stratification does not turn back into a regular fan.
float WaterHash(float n)
{
	return fract(sin(n * 127.1 + 311.7) * 43758.5453123);
}

// One wave's contribution. `k` is the wavenumber, `dir` the horizontal
// direction it travels, `amplitude` its half-height, `steep` its share of the
// choppiness budget.
void GerstnerWave(vec2 dir, float amplitude, float k, float steep, float speed,
				  float phase, vec2 position, float time,
				  inout vec3 offset, inout vec3 tangent, inout vec3 binormal)
{
	// **The phase offset is what stops every crest meeting at the origin.**
	const float angle = k * dot(dir, position) - speed * k * time + phase;
	const float c = cos(angle);
	const float s = sin(angle);

	// Horizontal towards the crest, vertical up it. `steep / k` is the standard
	// Gerstner amplitude for the horizontal term: it is what keeps the surface
	// from self-intersecting while the steepness terms sum to under one.
	const float qa = steep / max(k, 1e-5);

	offset.xz += dir * qa * c;
	offset.y  += amplitude * s;

	// The two surface derivatives of the above -- where the normal comes from
	// without ever touching a neighbouring vertex. Deriving it from the
	// triangle would give one normal per face and break the specular into flat
	// plates, which is precisely the highlight this is for.
	tangent.x  += -dir.x * dir.x * steep * s;
	tangent.z  += -dir.x * dir.y * steep * s;
	tangent.y  +=  dir.x * amplitude * k * c;

	binormal.x += -dir.x * dir.y * steep * s;
	binormal.z += -dir.y * dir.y * steep * s;
	binormal.y +=  dir.y * amplitude * k * c;
}

// The whole sum, at a position and a time, from the dials.
//
// **Domain warping, and it is nearly free.** The short waves are evaluated at
// the position the long ones have already displaced, so the chop *rides* the
// swell instead of sitting in a world-fixed grid underneath it. This is what
// cascaded FFT oceans get implicitly and why they look organic; without it the
// small waves march through the big ones as though the two were unrelated.
void WaterSurfaceEval(vec2 position, float time,
					  float waveHeight, float waveLength, float choppiness,
					  float waveSpeed, float direction, float spacing,
					  out vec3 offset, out vec3 tangent, out vec3 binormal)
{
	offset   = vec3(0.0);
	tangent  = vec3(1.0, 0.0, 0.0);
	binormal = vec3(0.0, 0.0, 1.0);

	if (waveHeight <= 0.0)
		return;

	const float longest = max(waveLength, 0.1);

	// **The short end is the grid's to decide, not the dial's.** A wave shorter
	// than about four quads cannot be drawn -- it aliases, and aliased short
	// waves are the ridged noise the rewrite exists to remove. So the band
	// stops at four times the spacing however wide RV_WATER_BAND asks for, and
	// somebody who wants finer chop gets it by making the grid finer rather
	// than by being quietly given noise.
	const float shortest = max(longest / RV_WATER_BAND,
							   max(spacing, 0.01) * 4.0);

	// Log spacing, so each wave is the same fraction shorter than the last and
	// the band is covered evenly in octaves rather than in metres.
	const float ratio = pow(shortest / longest, 1.0 / float(RV_WATER_WAVE_COUNT - 1));

	// **Amplitude proportional to wavelength**, which is what a k^-4 equilibrium
	// spectrum sampled on a log-spaced band comes out to: constant steepness
	// across scales. The alternative -- picking a decay by eye -- is how the
	// first version ended up with a band too narrow to matter.
	//
	// Normalised so the surface's variance matches the authored height: the sum
	// of the squared amplitudes is twice the square of the RMS elevation, and
	// the RMS is a quarter of the crest-to-trough figure somebody typed. That
	// is why the divisor is 2*sqrt(2) rather than something tuned.
	const float ratioSq = ratio * ratio;
	const float sumSq = (1.0 - pow(ratioSq, float(RV_WATER_WAVE_COUNT)))
					  / max(1.0 - ratioSq, 1e-6);
	const float scale = (waveHeight / 2.828427)
					  / max(sqrt(sumSq) * longest, 1e-6);

	// The choppiness budget. The fold condition is the *sum* of the shares --
	// worst-case phase alignment collapses the tangent by exactly that sum,
	// whatever its spread across the band -- so the total is pinned to the
	// dial while the shares tilt toward the short end: the chop a viewer
	// stands next to peaks into crests, the swell underneath keeps rolling.
	// Shared out uniformly this read as rounded everywhere, which is the
	// "waves are not sharp" note the tilt exists to answer.
	const float steepBase = choppiness / float(RV_WATER_WAVE_COUNT);

	// **A narrow fan, and narrower for the long waves than the short ones.**
	// That is the physical behaviour: long swell is strongly directional, short
	// chop is nearly isotropic. A single fan applied to every wave is wrong at
	// both ends, and the old one was wrong by a factor of three.
	const float baseSpread = 0.62;   // about +/-35 degrees at the peak

	const int swellCount = RV_WATER_WAVE_COUNT / 3;

	vec2 warped = position;
	float wavelength = longest;

	for (int i = 0; i < RV_WATER_WAVE_COUNT; i++)
	{
		const float fi = float(i);
		const float t = fi / float(RV_WATER_WAVE_COUNT - 1);

		// Stratified: each wave sits in its own slice of the band and is
		// jittered inside it. Even coverage, and no two wavelengths in a small
		// whole-number ratio.
		const float jitter = (WaterHash(fi * 1.7) - 0.5) * 0.8 * (1.0 - ratio);
		const float k = 6.2831853 / max(wavelength * (1.0 + jitter), 1e-3);

		// The fan widens with wave number, and the sign comes from the hash so
		// it fills from the middle out rather than sweeping one way.
		const float spread = baseSpread * (0.35 + 1.15 * t)
						   * (WaterHash(fi * 3.3) * 2.0 - 1.0);
		const float heading = direction + spread;
		const vec2 dir = vec2(cos(heading), sin(heading));

		const float amplitude = scale * wavelength;

		// Deep-water dispersion: the long swell outruns the chop, which is most
		// of what keeps the sum from ever repeating in time.
		const float speed = waveSpeed * sqrt(max(9.81 / max(k, 1e-5), 1e-5)) * 0.32;
		const float phase = WaterHash(fi * 7.9) * 6.2831853;

		// The longest third is the swell; everything shorter rides on the
		// displacement the swell has already produced.
		const vec2 samplePoint = (i < swellCount) ? position : warped;

		// Half a share for the longest wave, one and a half for the
		// shortest; the mean over the band is exactly one share, which is
		// what keeps the fold budget the dial's.
		GerstnerWave(dir, amplitude, k, steepBase * (0.5 + t), speed, phase,
					 samplePoint, time, offset, tangent, binormal);

		if (i == swellCount - 1)
			warped = position + offset.xz;

		wavelength *= ratio;
	}
}
