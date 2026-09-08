# -*- coding: utf-8 -*-
"""RT-8 job 3's measurement, in the sea's own accumulate.

Runs the signal contract's geometric history gate against the sea and counts
what it would do. Changes no picture: the water keeps its own average, and the
only new writes are a signature attachment nothing but next frame's counter
reads, plus the counter lanes themselves.
"""
import io, sys

P = r'RageVEditor/assets/shaders/water_accumulate.rvshader'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = '\r\n' in src
s = src.replace('\r\n', '\n')
if 'MeasureContractGate' in s:
    sys.exit('already patched')


def once(old, new, what):
    global s
    if s.count(old) != 1:
        sys.exit('%s matched %d' % (what, s.count(old)))
    s = s.replace(old, new, 1)


# ---- the two new inputs -------------------------------------------------
once('''layout(set = 3, binding = 5) uniform sampler2D u_WaveMotion;''',
'''layout(set = 3, binding = 5) uniform sampler2D u_WaveMotion;
// **RT-8 job 3's measurement, and nothing else reads these.** The sea's
// surface description this frame (octahedral normal in rg), and the
// signature this pass wrote for it last frame. See MeasureContractGate.
layout(set = 3, binding = 6) uniform sampler2D u_SurfaceIn;
layout(set = 3, binding = 7) uniform sampler2D u_HistorySignature;''',
     'bindings')

# ---- the signature attachment ------------------------------------------
once('''layout(location = 0) out vec4 o_Diffuse;
layout(location = 1) out vec4 o_Specular;''',
'''layout(location = 0) out vec4 o_Diffuse;
layout(location = 1) out vec4 o_Specular;
// **RT-8 job 3: what the sea's surface was here**, so next frame can ask the
// contract's gate a question about it -- octahedral normal in rg, the plane
// distance dot(N, P) in b, and one in a where a wave was drawn.
//
// The contract's own attachment marks "nothing here" with a negative alpha;
// this one marks water with a one, because a history target clears to zero
// and a zero must not read as a valid surface. Same test, opposite sentinel.
//
// Full float, unlike the contract's half: the plane distance is a world
// coordinate and this bay is a kilometre across, where a half's step is half
// a metre. What that storage would cost is measured separately -- see the
// half-precision lane -- rather than baked into every other number here.
layout(location = 2) out vec4 o_Signature;''',
     'signature output')

# ---- the gate itself ----------------------------------------------------
once('''void main()
{
	const ivec2 texel = ivec2(gl_FragCoord.xy);''',
'''// **RT-15's "did this actually move", as the sea meets it.**
//
// The contract exempts the plane test where three things hold at once: the
// renderer says the surface moved, the history is the texel's own rather than
// a neighbour the search reached, and the object id matches. The sea is one
// object, so its ids always agree and the exemption comes down to the motion
// -- with the contract's own floor on it, an eighth of a texel, below which
// the velocity lane and the matrix are two ways of computing one answer
// rather than two places. Transcribed from ObjectShift in
// reflection_accumulate.rvshader so the two ask the same question.
bool SeaMoved(vec2 wave, vec3 P, vec2 nowNdc, ivec2 size)
{
	if (dot(wave, wave) <= 0.0)
		return false;
	const vec4 clipThen = u_Lamps.PreviousViewProjection * vec4(P, 1.0);
	if (clipThen.w <= 0.0)
		return false;
	const vec2 byCamera = clipThen.xy / clipThen.w - u_Scene.Jitter.zw;
	const vec2 byMotion = (nowNdc - u_Scene.Jitter.xy) - 2.0 * wave;
	const vec2 texelNdc = 2.0 / vec2(size);
	return length((byMotion - byCamera) / texelNdc) >= 0.125;
}

// **RT-8 job 3: would the signal contract's history gate keep this water
// pixel?**
//
// The item's third piece -- folding this pass into the contract -- was argued
// against on the claim that the contract's geometric gate cannot hold a sea: a
// wave lifting the surface a metre slides the point seen at one pixel by tens
// of metres at a grazing angle, so a test asking "is the surface still the
// distance it was" should refuse the water constantly. **That was never
// measured.** What had been measured was the sea with no averaging at all,
// which is a different claim about a different thing.
//
// So the tests below are the contract's, transcribed from HistoryAt in
// reflection_accumulate.rvshader -- the same tolerances, the same nine-texel
// search, the same reasons in the same order -- run against the sea's surface
// a frame back. **They decide nothing.** The water's own average is untouched
// and every picture is bit-identical; the result goes to the ray counters.
//
// Three answers come back, because the argument needs all three:
//   `kept`       -- the gate as it stands today, RT-15's exemption included.
//   `keptStrict` -- the same gate with that exemption gone, which is what the
//                   argument assumed and what the gate did before RT-15
//                   landed on the same day.
//   `keptHalf`   -- the lenient gate reading a plane distance rounded to the
//                   half float the contract's surface attachment actually is.
void MeasureContractGate(ivec2 texel, ivec2 size, vec3 P, vec3 N, vec2 wave,
						 out bool kept, out bool keptStrict, out bool keptHalf,
						 out bool planeAtCentre, out int refusal)
{
	kept = false; keptStrict = false; keptHalf = false;
	planeAtCentre = false; refusal = 0;

	const vec2 nowUv = (vec2(texel) + 0.5) / vec2(size);
	const float nowRow = u_Lamps.History.w > 0.5 ? 1.0 - nowUv.y : nowUv.y;
	const vec2 nowNdc = vec2(nowUv.x * 2.0 - 1.0, nowRow * 2.0 - 1.0);

	// The same reprojection the pass above does: this place, less the wave.
	const vec2 uv = vec2(nowUv.x, nowRow) - u_Scene.Jitter.xy * 0.5
				  - wave + u_Scene.Jitter.zw * 0.5;
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
	{
		refusal = 1;
		return;
	}

	const float eyeDistance = length(P - u_Scene.CameraPosition.xyz);
	const float planeTolerance = 0.05 + 0.01 * eyeDistance;
	const bool moved = SeaMoved(wave, P, nowNdc, size);

	const float row = u_Lamps.History.w > 0.5 ? 1.0 - uv.y : uv.y;
	const vec2 pastUv = clamp(vec2(uv.x, row), vec2(0.0), vec2(1.0));
	const ivec2 centre = clamp(ivec2(pastUv * vec2(size)), ivec2(0), size - 1);
	const ivec2 offsets[9] = ivec2[9](ivec2(0, 0), ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1),
									  ivec2(1, 1), ivec2(-1, -1), ivec2(1, -1), ivec2(-1, 1));
	for (int k = 0; k < 9; ++k)
	{
		const ivec2 pastTexel = clamp(centre + offsets[k], ivec2(0), size - 1);
		const vec4 was = texelFetch(u_HistorySignature, pastTexel, 0);
		const bool there = was.a > 0.5;
		const vec3 wasN = OctDecode(was.rg);
		const bool facing = there && dot(wasN, N) >= 0.8;
		const float offPlane = abs(dot(wasN, P) - was.b);
		// The plane distance as the contract would have stored it.
		const float wasHalf = unpackHalf2x16(packHalf2x16(vec2(was.b, 0.0))).x;
		const float offHalf = abs(dot(wasN, P) - wasHalf);
		// RT-15's exemption: the texel's own history, on a surface that moved.
		const bool exempt = moved && k == 0;

		if (!kept       && facing && (offPlane <= planeTolerance || exempt)) kept = true;
		if (!keptStrict && facing && offPlane <= planeTolerance)             keptStrict = true;
		if (!keptHalf   && facing && (offHalf  <= planeTolerance || exempt)) keptHalf = true;

		// The reason, recorded at the reprojected texel itself as the contract
		// records it, and reported only where nothing was kept at all.
		if (k == 0)
		{
			refusal = !there ? 2 : !facing ? 3 : (offPlane > planeTolerance ? 4 : 0);
			planeAtCentre = there && facing && offPlane > planeTolerance;
		}
		if (kept && keptStrict && keptHalf)
			break;
	}
}

#if defined(RV_RAY_COUNTERS)
// The lanes, in RayCounters::Lane's order; the two must agree.
const uint RAY_LANE_WATER_PIXELS       = 27u;
const uint RAY_LANE_WATER_KEPT         = 28u;
const uint RAY_LANE_WATER_REFUSED      = 29u;   // and the three after it
const uint RAY_LANE_WATER_KEPT_STRICT  = 33u;
const uint RAY_LANE_WATER_PLANE_STRICT = 34u;
const uint RAY_LANE_WATER_PLANE_HALF   = 35u;
const uint RAY_LANE_WATER_CLAMPED      = 36u;

// **Called once, at the end of main, for every invocation.** Not at the exits:
// a subgroup reduction wants the whole wave, and the pixels that carry no
// water pass `water` false and add nothing rather than leaving the wave early.
void CountWaterGate(bool water, bool decided, bool kept, bool keptStrict,
					bool keptHalf, bool planeAtCentre, int refusal, bool clamped)
{
	const bool real = !gl_HelperInvocation && water && decided;
	const uvec4 lanes = subgroupBallot(!gl_HelperInvocation);
	const bool leader = !gl_HelperInvocation
					 && gl_SubgroupInvocationID == subgroupBallotFindLSB(lanes);
	const uint base = RayCounterSlot() * RAY_COUNTER_STRIDE;
	const uint pixels = subgroupAdd(real ? 1u : 0u);
	const uint held = subgroupAdd((real && kept) ? 1u : 0u);
	const uint strict = subgroupAdd((real && keptStrict) ? 1u : 0u);
	const uint planeStrict = subgroupAdd((real && !keptStrict && planeAtCentre) ? 1u : 0u);
	const uint halfLost = subgroupAdd((real && kept && !keptHalf) ? 1u : 0u);
	const uint pulled = subgroupAdd((real && clamped) ? 1u : 0u);
	if (leader && pixels > 0u)
	{
		atomicAdd(u_RayCounters.Counts[base + RAY_LANE_WATER_PIXELS], pixels);
		if (held > 0u)
			atomicAdd(u_RayCounters.Counts[base + RAY_LANE_WATER_KEPT], held);
		if (strict > 0u)
			atomicAdd(u_RayCounters.Counts[base + RAY_LANE_WATER_KEPT_STRICT], strict);
		if (planeStrict > 0u)
			atomicAdd(u_RayCounters.Counts[base + RAY_LANE_WATER_PLANE_STRICT], planeStrict);
		if (halfLost > 0u)
			atomicAdd(u_RayCounters.Counts[base + RAY_LANE_WATER_PLANE_HALF], halfLost);
		if (pulled > 0u)
			atomicAdd(u_RayCounters.Counts[base + RAY_LANE_WATER_CLAMPED], pulled);
	}
	// One reason per refused pixel, numbered as the contract numbers them:
	// off screen, none there, normal, plane. They sum to the complement of
	// the kept count by construction, which is the check if a line looks odd.
	for (uint r = 1u; r <= 4u; ++r)
	{
		const uint n = subgroupAdd((real && !kept && uint(refusal) == r) ? 1u : 0u);
		if (leader && n > 0u)
			atomicAdd(u_RayCounters.Counts[base + RAY_LANE_WATER_REFUSED + (r - 1u)], n);
	}
}
#else
void CountWaterGate(bool water, bool decided, bool kept, bool keptStrict,
					bool keptHalf, bool planeAtCentre, int refusal, bool clamped) {}
#endif

void main()
{
	const ivec2 texel = ivec2(gl_FragCoord.xy);''',
     'gate functions')

# ---- main, restructured so the count is one uniform call at the end -----
once('''	// No wave here: nothing to average, and nothing downstream reads it. The
	// zero matters all the same -- a stale value left in this attachment would
	// be picked up as a history next frame.
	if (position.w <= 0.5)
	{
		o_Diffuse = vec4(0.0);
		o_Specular = vec4(0.0);
		return;
	}

	vec3 keptD = nowD;''',
'''	// No wave here: nothing to average, and nothing downstream reads it. The
	// zero matters all the same -- a stale value left in this attachment would
	// be picked up as a history next frame.
	//
	// **RT-8 job 3: written rather than returned from.** The gate measurement
	// at the end of this function reduces over the whole wave, and a wave whose
	// dry lanes had already left would be counting a subgroup that is not all
	// there. Every branch below now falls through to one exit. The values
	// written are the ones the early return wrote, to the bit.
	const bool water = position.w > 0.5;

	vec3 keptD = nowD;''',
     'early return')

once('''	if (u_Lamps.History.x > 0.5)
	{
		const vec4 clip = u_Lamps.PreviousViewProjection * vec4(position.xyz, 1.0);''',
'''	// RT-8 job 3: whether the sea's own bound moved this pixel's history, which
	// is the thing the contract's gate would be replacing.
	bool clamped = false;

	if (water && u_Lamps.History.x > 0.5)
	{
		const vec4 clip = u_Lamps.PreviousViewProjection * vec4(position.xyz, 1.0);''',
     'history guard')

once('''				const vec3 heldD = clamp(pastD.rgb, lowD, highD);
				const vec3 heldS = clamp(pastS.rgb, lowS, highS);''',
'''				const vec3 heldD = clamp(pastD.rgb, lowD, highD);
				const vec3 heldS = clamp(pastS.rgb, lowS, highS);
				clamped = any(notEqual(heldD, pastD.rgb)) || any(notEqual(heldS, pastS.rgb));''',
     'clamp count')

once('''	o_Diffuse = vec4(keptD, framesD);
	o_Specular = vec4(keptS, framesS);
}''',
'''	o_Diffuse = water ? vec4(keptD, framesD) : vec4(0.0);
	o_Specular = water ? vec4(keptS, framesS) : vec4(0.0);

	// **RT-8 job 3, and this is the whole of its effect on the frame.** The
	// signature for next frame's question, and the count. Nothing above this
	// line reads either.
	const vec3 N = OctDecode(texelFetch(u_SurfaceIn, texel, 0).rg);
	o_Signature = water ? vec4(OctEncode(N), dot(N, position.xyz), 1.0) : vec4(0.0);

	bool gateKept = false, gateStrict = false, gateHalf = false, gatePlane = false;
	int gateRefusal = 0;
	const bool decided = water && u_Lamps.History.x > 0.5;
	if (decided)
	{
		MeasureContractGate(texel, size, position.xyz, N,
							texelFetch(u_WaveMotion, texel, 0).xy,
							gateKept, gateStrict, gateHalf, gatePlane, gateRefusal);
	}
	CountWaterGate(water, decided, gateKept, gateStrict, gateHalf, gatePlane,
				   gateRefusal, clamped);
}''',
     'exit')

io.open(P, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
print('water_accumulate patched')
