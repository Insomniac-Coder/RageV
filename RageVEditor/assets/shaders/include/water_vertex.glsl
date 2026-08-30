// The vertex stage of a body of water: a flat grid displaced into waves.
//
// No #version here: an include is spliced into a file that already has one.
// Expects include/scene_vertex.glsl and include/water_params.glsl first.
//
// **Gerstner, not a sine.** A sine wave is symmetric -- its crests are as round
// as its troughs -- and a sea is not: real waves have sharp crests and long flat
// troughs, because the water at a crest is moving forward as well as up. A
// Gerstner wave says exactly that by moving the vertex *horizontally* towards
// the crest as well as vertically, which is what choppiness scales. Take the
// horizontal term away and it collapses into the rolling hills that give away
// every naive water shader.
//
// **Why the first version read as corrugated iron, and it was not the wave
// count.** It summed four waves, which is what GPU Gems 1 uses -- but GPU Gems
// pairs those four with fifteen more in texture space, so four alone was never
// the whole answer. Three faults stacked:
//
//   * **The band was 1.7 octaves wide.** Four waves of nearly the same size at
//     comparable amplitudes is the textbook recipe for a beat lattice. A real
//     sea shows six-plus octaves, three or four of them in the range geometry
//     can carry. Adding waves without widening the band fixes nothing.
//
//   * **The directional fan was 246 degrees.** Real wind seas are forward
//     peaked -- about +/-35 at the spectral peak. Spread four waves over 246
//     and some pair lands near ninety degrees apart, and two near-orthogonal
//     waves of similar wavelength *are* an egg carton. That is the analytic
//     form of the artefact, not a coincidence.
//
//   * **Every phase was zero.** So every crest passed through the world origin:
//     a radial star, and a lattice locked to it. One line, and the largest
//     single improvement here.
//
// Fixed by letting a spectrum choose the waves instead of hand-picking four.
// Wavelengths are stratified within a log-spaced band, which gives even
// coverage *and* incommensurate ratios for free -- near-rational ratios like
// the old 0.68 (close to 2/3) are worse than either extreme, because they beat
// slowly and the eye locks straight onto a drifting lattice.

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

// x = the Jacobian of the horizontal displacement -- how much this vertex's
// neighbourhood is being stretched or piled up. **Handed over raw rather than
// as a foam value**, because the fragment stage needs to read three states off
// it and not one: below about 0.4 the surface has folded and is breaking, which
// is foam; between 0.4 and 1.0 it is stretched but intact, which is a *wet
// crest about to break* -- and that band is the brightest thing on a real sea
// under a low sun. Collapsing it to foam in the vertex stage threw that away.
//
// y = where this vertex sits between trough and crest, 0 to 1. A crest is
// thinner water than a trough, so more light comes back through it -- which is
// why the tops of swells go green over a dark sea.
layout(location = 10) out vec2 v_Water;

// The two colours, forwarded rather than looked up again -- flat, because they
// are constant across the body. **The foam dial rides in .w** rather than
// taking a varying of its own: the fragment stage deliberately declares no
// push-constant block (see water_params.glsl), so anything it needs has to
// arrive this way, and a vec3 costs a full four-lane slot regardless.
layout(location = 11) flat out vec4 v_WaterShallow;
layout(location = 12) flat out vec3 v_WaterDeep;

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
// this rewrite exists to remove. Twelve is a little under 3.6 octaves, which is
// what a 6 m grid can carry under a 26 m dominant wave.
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

// The whole sum, at a position and a time.
//
// **Domain warping, and it is nearly free.** The short waves are evaluated at
// the position the long ones have already displaced, so the chop *rides* the
// swell instead of sitting in a world-fixed grid underneath it. This is what
// cascaded FFT oceans get implicitly and why they look organic; without it the
// small waves march through the big ones as though the two were unrelated.
void WaterSurface(vec2 position, float time,
				  out vec3 offset, out vec3 tangent, out vec3 binormal)
{
	offset   = vec3(0.0);
	tangent  = vec3(1.0, 0.0, 0.0);
	binormal = vec3(0.0, 0.0, 1.0);

	if (RV_WATER_HEIGHT <= 0.0)
		return;

	const float longest = max(RV_WATER_LENGTH, 0.1);

	// **The short end is the grid's to decide, not the dial's.** A wave shorter
	// than about four quads cannot be drawn -- it aliases, and aliased short
	// waves are the ridged noise this rewrite exists to remove. So the band
	// stops at four times the spacing however wide RV_WATER_BAND asks for, and
	// somebody who wants finer chop gets it by making the grid finer rather
	// than by being quietly given noise.
	const float shortest = max(longest / RV_WATER_BAND,
							   max(RV_WATER_SPACING, 0.01) * 4.0);

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
	const float scale = (RV_WATER_HEIGHT / 2.828427)
					  / max(sqrt(sumSq) * longest, 1e-6);

	// The choppiness budget, shared out so the total cannot fold the surface
	// however the dial is set.
	const float steepShare = RV_WATER_CHOPPINESS / float(RV_WATER_WAVE_COUNT);

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
		const float heading = RV_WATER_DIRECTION + spread;
		const vec2 dir = vec2(cos(heading), sin(heading));

		const float amplitude = scale * wavelength;

		// Deep-water dispersion: the long swell outruns the chop, which is most
		// of what keeps the sum from ever repeating in time.
		const float speed = RV_WATER_SPEED * sqrt(max(9.81 / max(k, 1e-5), 1e-5)) * 0.32;
		const float phase = WaterHash(fi * 7.9) * 6.2831853;

		// The longest third is the swell; everything shorter rides on the
		// displacement the swell has already produced.
		const vec2 samplePoint = (i < swellCount) ? position : warped;

		GerstnerWave(dir, amplitude, k, steepShare, speed, phase,
					 samplePoint, time, offset, tangent, binormal);

		if (i == swellCount - 1)
			warped = position + offset.xz;

		wavelength *= ratio;
	}
}

void main()
{
	InstanceData instance = FetchInstance();

	vec3 offset, tangent, binormal;
	WaterSurface(a_Position.xz, RV_WATER_TIME, offset, tangent, binormal);

	const vec3 local = a_Position + offset;

	// **Cross in this order.** binormal x tangent, not the reverse: the grid is
	// wound counter-clockwise seen from above and the normal has to come out
	// pointing at the sky. The other order gives a sea lit from underneath,
	// which reads as a hole rather than as a mistake.
	const vec3 normal = normalize(cross(binormal, tangent));

	vec4 world = instance.Model * vec4(local, 1.0);

	v_WorldPos = world.xyz;
	v_Normal   = mat3(instance.NormalMatrix) * normal;
	v_TexCoord = a_TexCoord;

	// Taken from the same derivatives the normal came from, so what the fragment
	// reads off it sits exactly on the crests the geometry actually has rather
	// than on the ones a texture guesses at.
	// **No extra 1 here, and that was a real bug.** `tangent` and `binormal`
	// are seeded with the identity basis and the wave loop adds to them, so
	// they already carry the "+1" the textbook Jacobian writes explicitly.
	// Adding it a second time put J near 4 instead of near 1 -- above every
	// threshold, at every point, always. The sea could not produce a whitecap
	// and nothing said so, because a foam term that is uniformly zero looks
	// exactly like a calm day.
	v_Water.x = tangent.x * binormal.z - tangent.z * binormal.x;

	// **Foam is left behind, not carried.** Taken only at the current instant,
	// the stretching term is *attached* to the crest -- it appears and vanishes
	// with the wave, and that is the single thing that makes shader foam read as
	// shader foam. Real whitewater is dropped by a breaking crest and drifts
	// afterwards, outliving the wave that made it.
	//
	// So the surface is asked twice more, at earlier times and at the positions
	// the water has drifted from since, and the strongest stretching of the
	// three wins. What that leaves is a trail behind each crest instead of a
	// band on it.
	//
	// **This approximates an accumulation buffer, and it is worth saying so.**
	// The full answer keeps foam in a persistent world-anchored texture,
	// injects into it, decays it exponentially and advects it by the surface
	// flow -- which also lets old foam fade to a different colour than fresh.
	// That needs a compute pass and a ping-pong target. This buys the
	// "left behind" cue, which is most of the look, for two more taps.
	{
		// Roughly the surface drift: a fraction of the wave speed, along the
		// wind. Foam rides the water, not the wave form.
		const vec2 drift = vec2(cos(RV_WATER_DIRECTION), sin(RV_WATER_DIRECTION))
						 * RV_WATER_SPEED * 0.35;

		vec3 o2, t2, b2;
		WaterSurface(a_Position.xz - drift * 0.6, RV_WATER_TIME - 0.6, o2, t2, b2);

		vec3 o3, t3, b3;
		WaterSurface(a_Position.xz - drift * 1.4, RV_WATER_TIME - 1.4, o3, t3, b3);

		// A lower Jacobian is more stretched, so the trail takes the minimum --
		// and the older taps are biased upward so the trail fades out rather
		// than ending on a hard edge.
		const float older = min((t2.x * b2.z - t2.z * b2.x) + 0.05,
								(t3.x * b3.z - t3.z * b3.x) + 0.12);
		v_Water.x = min(v_Water.x, older);
	}

	// Against the authored height rather than any one wave's amplitude:
	// measured against the largest wave the ratio saturates at every crest and
	// the gradient collapses into two flat colours with a hard line between.
	v_Water.y = clamp(offset.y / max(RV_WATER_HEIGHT * 0.5, 1e-4) * 0.5 + 0.5,
					  0.0, 1.0);

	v_WaterShallow = vec4(RV_WATER_SHALLOW, RV_WATER_FOAM);
	v_WaterDeep    = RV_WATER_DEEP;

	v_BaseColor     = instance.BaseColor;
	v_EmissiveColor = instance.EmissiveColor;
	v_Surface       = instance.Surface;
	v_Probe         = instance.Indices.y;
	v_MaterialIndex = instance.Indices.z;

	gl_Position = u_Scene.ViewProjection * world;
	v_ClipPos = gl_Position;

	// **The previous frame's wave, not the previous frame's flat grid.** The
	// motion vector drives the temporal filter and the motion blur; giving it
	// the undisplaced position would tell both that a visibly moving surface is
	// standing still, and the reprojection would smear every crest.
	vec3 previousOffset, previousTangent, previousBinormal;
	WaterSurface(a_Position.xz, RV_WATER_PREV_TIME,
				 previousOffset, previousTangent, previousBinormal);

	v_PrevClipPos = u_Scene.PreviousViewProjection * instance.PreviousModel *
					vec4(a_Position + previousOffset, 1.0);
}
