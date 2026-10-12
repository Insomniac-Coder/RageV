"""Step 2: the trace keeps its first aimed draw (o_LampSample) and its score total, for the resolve."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io

T = (_RAGEV + '/RageVEditor/assets/shaders/reflection_trace.rvshader')
t = io.open(T, encoding='utf-8', newline='').read().replace('\r\n', '\n')


def edit(a, b):
    global t
    assert t.count(a) == 1, (t.count(a), a[:80])
    t = t.replace(a, b)


edit("""layout(location = 4) out vec4 o_Aimed;
#endif
""", """layout(location = 4) out vec4 o_Aimed;
// **RT-24 (2026-09-25): and the draw behind the texel's first aimed sample, for the resolve to pool.**
// The resolve pools its neighbours' lamp points the way ReSTIR does (Bitterli et al. 2020; NVIDIA's
// RTXDI): not the light a neighbour's point sent the neighbour, which was worked out for the
// neighbour's angle to the lamp and spread as a halo when re-aimed, but the point itself, which the
// texel then weighs from its own position. What that needs is the draw and the densities it was
// drawn at: xy the draw (the aiming table's uv where the row has one, the rectangle's (su, sv) where
// it radiates evenly), z the density the draw had per square metre of the fitting, w the texel's
// score total -- every row's EmitterScore summed, so a row's chance here is its score over this.
// Thirty-two bits a lane: the resolve divides by these, and a rounded density is a biased one.
// o_Aimed's w carries the row (plus 16 where the point was stepped out to the fitting's back face),
// or -1 where no row was drawn.
layout(location = 5) out vec4 o_LampSample;
#endif
""")

# AimedAtEmitters reports the draw.
edit("""vec3 AimedAtEmitters(vec3 P, vec3 N, vec3 V, float roughness, int emitters, inout uint seed)
{""", """// `drawn` and `totalScore`: the draw itself and every row's score summed, for o_LampSample (see its
// declaration): xy the draw, z its density per square metre of the fitting, w the row (plus 16 on
// the back face) or -1 where nothing was drawn.
vec3 AimedAtEmitters(vec3 P, vec3 N, vec3 V, float roughness, int emitters, inout uint seed,
					 out vec4 drawn, out float totalScore)
{
	drawn = vec4(0.0, 0.0, 0.0, -1.0);
	totalScore = 0.0;""")
edit("""	if (total <= 0.0)
		return vec3(0.0);

	const float draw = GiRandom(seed) * total;""", """	if (total <= 0.0)
		return vec3(0.0);
	totalScore = total;

	const float draw = GiRandom(seed) * total;""")
edit("""	const float side = dot(P - flat_, lightNormal) >= 0.0 ? 1.0 : -1.0;
	const vec3 point = flat_ + lightNormal * (light.UvToSurface1.w * side);
""", """	const float side = dot(P - flat_, lightNormal) >= 0.0 ? 1.0 : -1.0;
	const vec3 point = flat_ + lightNormal * (light.UvToSurface1.w * side);
	// The draw, before any test of this texel's own: a point this texel cannot see is still a
	// point its sampler drew, and a neighbour may see it (RT-24, o_LampSample).
	if (light.CentreArea.w > 0.0)
		drawn = vec4(grid > 0 ? aimUv : vec2(su, sv), density / light.CentreArea.w,
					 float(pick) + (side < 0.0 ? 16.0 : 0.0));
""")

# The trace keeps the first ray's draw.
edit("""	o_Aimed = vec4(0.0);

	const ivec2 texel = ivec2(gl_FragCoord.xy);""", """	o_Aimed = vec4(0.0, 0.0, 0.0, -1.0);
	o_LampSample = vec4(0.0);

	const ivec2 texel = ivec2(gl_FragCoord.xy);""")
edit("""	// RT-24: the aimed samples' light over the texel's rays, summed here and written apart.
	vec3 aimedSum = vec3(0.0);
""", """	// RT-24: the aimed samples' light over the texel's rays, summed here and written apart.
	vec3 aimedSum = vec3(0.0);
	// RT-24 (2026-09-25): and the first ray's draw, for the resolve to pool (o_LampSample).
	vec4 lampDrawn = vec4(0.0, 0.0, 0.0, -1.0);
	float lampTotal = 0.0;
""")
edit("""		const vec3 aimed = AimedAtEmitters(P, N, V, roughness, emitters, neeSeed);""",
     """		const vec3 aimed = AimedAtEmitters(P, N, V, roughness, emitters, neeSeed, lampDrawn, lampTotal);""")
edit("""			const vec3 aimedMore = AimedAtEmitters(P, N, V, roughness, emitters, neeSeed);""",
     """			vec4 drawnMore;
			float totalMore;
			const vec3 aimedMore = AimedAtEmitters(P, N, V, roughness, emitters, neeSeed, drawnMore, totalMore);""")
edit("""	o_Aimed = vec4(min(aimedMean, vec3(64.0)) * tint, 0.0);""",
     """	o_Aimed = vec4(min(aimedMean, vec3(64.0)) * tint, lampDrawn.w);
	o_LampSample = vec4(lampDrawn.xyz, lampTotal);""")

io.open(T, 'w', encoding='utf-8', newline='').write(t.replace('\n', '\r\n'))
print('ok')
