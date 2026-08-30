// The vertex stage of a body of water: a flat grid displaced into waves.
//
// No #version here: an include is spliced into a file that already has one.
// Expects include/scene_vertex.glsl, include/water_params.glsl and
// include/water_waves.glsl first -- the sum itself lives in water_waves.glsl,
// shared with the foam accumulation pass, and this file is what feeds it the
// dials off the push-constant block.
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
//
// zw = where this vertex sits in the body's own rectangle, 0..1 across width
// and length -- the coordinate the foam accumulation buffer is addressed by.
// Computed here because only the vertex has the undisplaced position: taken
// off the world position in the fragment, the foam would slosh sideways with
// every crest that displaced it.
layout(location = 10) out vec4 v_Water;

// The two colours, forwarded rather than looked up again -- flat, because they
// are constant across the body. **The foam dial rides in .w** rather than
// taking a varying of its own: the fragment stage deliberately declares no
// push-constant block (see water_params.glsl), so anything it needs has to
// arrive this way, and a vec3 costs a full four-lane slot regardless.
layout(location = 11) flat out vec4 v_WaterShallow;
// rgb = the deep colour; w = the wave direction in radians, which the
// anisotropic highlight needs -- the glitter track on wind-driven water is a
// streak along the wind, not a disc, and the fragment cannot stretch the lobe
// without knowing which way the wind blows.
layout(location = 12) flat out vec4 v_WaterDeep;
// The dials the fragment reads that no earlier varying had room for:
// x = the body's clock (the detail normals scroll by it), y = the grid
// spacing, z = the gradient depth in metres, w = flags and the NDC sign --
// see water_params.glsl for the packing.
layout(location = 13) flat out vec4 v_WaterMisc;

// The dials off the push-constant block, into the shared sum.
void WaterSurface(vec2 position, float time,
				  out vec3 offset, out vec3 tangent, out vec3 binormal)
{
	WaterSurfaceEval(position, time,
					 RV_WATER_HEIGHT, RV_WATER_LENGTH, RV_WATER_CHOPPINESS,
					 RV_WATER_SPEED, RV_WATER_DIRECTION, RV_WATER_SPACING,
					 offset, tangent, binormal);
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

	// **The two history taps that used to sit here moved into the foam
	// accumulation buffer.** Asking the surface twice more at earlier times
	// bought the "left behind" cue for two more full evaluations of the sum on
	// every vertex, every frame -- and it still could only fake a 1.4-second
	// memory. The buffer keeps a real one: injected where the surface folds,
	// decayed exponentially, advected downwind, fresh ageing into residual.
	// The fragment reads it at the coordinate below; this stage's only foam
	// job now is the *instantaneous* Jacobian above, which drives the wet
	// crest -- a property of the wave itself, not of the foam it left.

	// Against the authored height rather than any one wave's amplitude:
	// measured against the largest wave the ratio saturates at every crest and
	// the gradient collapses into two flat colours with a hard line between.
	v_Water.y = clamp(offset.y / max(RV_WATER_HEIGHT * 0.5, 1e-4) * 0.5 + 0.5,
					  0.0, 1.0);

	// The undisplaced grid position, as a 0..1 coordinate over the body's
	// rectangle. BuildGeometry centres the grid on the origin, so half the
	// size is the offset back to the corner.
	v_Water.zw = a_Position.xz / max(RV_WATER_SIZE, vec2(1e-3)) + 0.5;

	v_WaterShallow = vec4(RV_WATER_SHALLOW, RV_WATER_FOAM);
	v_WaterDeep    = vec4(RV_WATER_DEEP, RV_WATER_DIRECTION);
	v_WaterMisc    = vec4(RV_WATER_TIME, RV_WATER_SPACING,
						  RV_WATER_GRADIENT, RV_WATER_FLAGS);

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
