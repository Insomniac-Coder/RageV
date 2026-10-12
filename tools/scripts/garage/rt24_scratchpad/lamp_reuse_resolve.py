"""Step 3: the resolve pools the neighbours' lamp points (ReSTIR spatial reuse, pairwise MIS)."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io

S = (_RAGEV + '/RageVEditor/assets/shaders/reflection_resolve.rvshader')
t = io.open(S, encoding='utf-8', newline='').read().replace('\r\n', '\n')


def edit(a, b):
    global t
    assert t.count(a) == 1, (t.count(a), a[:80])
    t = t.replace(a, b)


edit("""layout(set = 3, binding = 5) uniform sampler2D u_Aimed;
""", """layout(set = 3, binding = 5) uniform sampler2D u_Aimed;
// **RT-24 (2026-09-25): and the draw behind each texel's first aimed sample** (reflection_trace's
// o_LampSample: xy the draw, z its density per square metre of the fitting, w the texel's score
// total; u_Aimed's w the row, -1 for none), for the pooling below. The albedo lane, for this texel's
// own reflectance tint, which the pooled light is given as the trace gives its own.
layout(set = 3, binding = 6) uniform sampler2D u_LampSample;
layout(set = 3, binding = 7) uniform sampler2D u_Albedo;
""")

edit("""	vec4 Probe;
} u_Reflection;
""", """	// **RT-24 (2026-09-25): y whether the rays are drawn from the visible normals (the trace's
	// Nee.y), so a lamp point is weighed by the lobe it was drawn against; w how many neighbours'
	// lamp points each texel pools, zero for none (`--reflection-lamp-pool`).**
	vec4 Probe;
} u_Reflection;

bool VisibleNormals() { return u_Reflection.Probe.y > 0.5; }
#define RV_EMITTER_ROWS_BINDING 8
#define RV_EMITTER_CDF_BINDING 9
#include "include/reflection_emitters.glsl"
""")

POOL = r"""
// **RT-24 (2026-09-25): the lamp points, pooled -- the way ReSTIR pools them.**
//
// Each texel's lamp light comes from one point its trace aimed at on one fitting: one sample, grainy
// on its own. Fix 1 (2026-09-24) stopped sharing that light with the neighbours because the sharing
// re-aimed a neighbour's *answer* -- the light its point sent the neighbour, worked out for the
// neighbour's angle to the lamp -- as if it had arrived along the neighbour's lobe ray, which spread
// every lamp's reflection into a halo. What the top engines share instead (ReSTIR, Bitterli et al.
// 2020; NVIDIA's RTXDI, in Cyberpunk 2077's path tracing, Portal RTX, Alan Wake 2) is the *point*:
// a texel takes each neighbour's lamp point and weighs it from its own position -- its own lobe
// toward the point, its own distance, its own chance of having drawn that point -- then keeps one
// of the candidates in proportion to what it is worth here, and fires one shadow ray at that one.
//
// The weights are pairwise MIS over the candidates' own sampling densities (Bitterli 2022; the
// "generalized RIS" of Lin et al. 2022): the candidates were drawn by different texels, each by its
// own chances, and pairwise weights make any mix of them an unbiased estimate of this texel's lamp
// light at the cost of two score evaluations per neighbour, not one per pair. Every density is the
// sampler's own (EmitterScore over the texel's score total, times the draw's density on the
// fitting), so no candidate can be over- or under-counted; a neighbour with nothing like this
// texel's view of a lamp simply earns a weight of nothing. Nothing is tuned: the pool's size is
// the one lever, a global setting.
//
// The integrand is the aimed sample's own -- the lamp's radiance through this texel's lobe, times
// the power-heuristic share the trace gives the aimed strategy against the lobe ray -- so the
// pooled light replaces the texel's own aimed sample exactly, and the lobe rays' struck glow, shared
// by the taps above, stays the other half of the same weighting.

// What the texel at (P, N, V, roughness), with score total `total`, would give the lamp point `y`
// on row `light`: the estimate's integrand in the fitting's area measure (rgb), with the texel's
// own chance of having drawn this row returned beside it.
vec3 LampIntegrand(vec3 P, vec3 N, vec3 V, float roughness, float total, GiEmitter light,
				   vec3 y, vec3 radiance, float density, out vec3 L, out float dist, out float chance)
{
	L = vec3(0.0);
	dist = 0.0;
	chance = 0.0;
	if (total <= 0.0)
		return vec3(0.0);
	chance = EmitterScore(P, N, V, roughness, light) / total;
	vec3 toLight = y - P;
	const float distance2 = max(dot(toLight, toLight), 1e-6);
	dist = sqrt(distance2);
	L = toLight / dist;
	if (dot(N, L) <= 0.0 || chance <= 0.0)
		return vec3(0.0);
	const float cosLight = abs(dot(EmitterNormal(light), L));
	if (cosLight <= 1.0e-4)
		return vec3(0.0);
	// The trace's two densities toward this point, per unit solid angle: the aimed sampler's (the
	// row's chance times the draw's density on the fitting, area to solid angle) and the lobe's.
	const float pLight = chance * density * distance2 / cosLight;
	const float pLobe = LobePdf(N, V, L, roughness);
	const float share = pLight * pLight / max(pLobe * pLobe + pLight * pLight, 1.0e-18);
	return radiance * (pLobe * share * cosLight / distance2);
}

// A stored draw, unpacked: its row, the face it was stepped out to, the point, its light and density.
bool LampDraw(ivec2 at, out GiEmitter light, out vec3 y, out vec3 radiance, out float density,
			  out float side, out float total)
{
	const float code = texelFetch(u_Aimed, at, 0).w;
	const vec4 s = texelFetch(u_LampSample, at, 0);
	total = s.w;
	light = u_Emitters[0];
	y = vec3(0.0);
	radiance = vec3(0.0);
	density = 0.0;
	side = 1.0;
	if (code < -0.5 || total <= 0.0 || s.z <= 0.0)
		return false;
	const int packed = int(code + 0.5);
	light = u_Emitters[packed & 15];
	side = packed >= 16 ? -1.0 : 1.0;
	y = EmitterPointFromDraw(light, s.xy, side);
	radiance = EmitterRadianceFromDraw(light, s.xy);
	density = s.z;
	return true;
}

// The pooled lamp light at `texel` (untinted), from its own draw and up to `pool` neighbours' drawn
// from the same spiral the taps above walk. A negative red lane: nothing to pool, keep the texel's own.
vec3 PooledLamp(ivec2 texel, ivec2 size, vec3 P, vec3 N, vec3 V, float roughness, vec2 reach,
				float rotation, int taps, int pool)
{
	const float total = texelFetch(u_LampSample, texel, 0).w;
	// A texel that draws from no row has an aimed share of nothing: its lamp light is the lobe
	// rays' alone, and so is the pooled one.
	if (total <= 0.0)
		return vec3(0.0);

	// The neighbours: their surfaces (for their chances of having drawn a point) and their draws.
	ivec2 nAt[8];
	vec3 nP[8], nN[8], nV[8];
	float nRough[8], nTotal[8];
	int k = 0;
	const int stride = max(taps / max(pool, 1), 1);
	for (int i = 0; i < taps && k < pool; i += stride)
	{
		const float r = sqrt((float(i) + 0.5) / float(taps));
		const float theta = float(i) * 2.39996323 + rotation;
		const ivec2 at = clamp(texel + ivec2(round(reach * r * vec2(cos(theta), sin(theta)))), ivec2(0), size - 1);
		if (at == texel)
			continue;
		bool seen = false;
		for (int j = 0; j < k; ++j)
			seen = seen || nAt[j] == at;
		if (seen || texelFetch(u_Fresh, at, 0).a < 0.0)
			continue;
		vec3 Pn, Nn;
		float rn;
		if (!SurfaceAt(at, size, Pn, Nn, rn))
			continue;
		const float tn = texelFetch(u_LampSample, at, 0).w;
		if (tn <= 0.0)
			continue;   // it draws from no row: not a sampler, nothing to weigh against
		nAt[k] = at;
		nP[k] = Pn;
		nN[k] = Nn;
		nV[k] = normalize(u_Scene.CameraPosition.xyz - Pn);
		nRough[k] = rn;
		nTotal[k] = tn;
		++k;
	}

	uint seed = GiHash(uint(texel.x) * 1973u ^ uint(texel.y) * 9277u
					   ^ uint(u_Scene.GlobalIllumination.y) * 26699u ^ 0x68E31DA4u);
	float weightSum = 0.0;
	vec3 chosen = vec3(0.0);
	float chosenTarget = 0.0;
	vec3 chosenL = vec3(0.0);
	float chosenDist = 0.0;

	// The texel's own draw, weighed against every neighbour's chance of having drawn it.
	{
		GiEmitter light;
		vec3 y, radiance;
		float density, side, ownTotal;
		if (LampDraw(texel, light, y, radiance, density, side, ownTotal))
		{
			vec3 L;
			float dist, chance;
			const vec3 f = LampIntegrand(P, N, V, roughness, total, light, y, radiance, density, L, dist, chance);
			const float target = dot(f, vec3(0.2126, 0.7152, 0.0722));
			const float drawnAt = chance * density;
			if (target > 0.0 && drawnAt > 0.0)
			{
				float m = 1.0;
				if (k > 0)
				{
					m = 0.0;
					for (int j = 0; j < k; ++j)
					{
						// Neighbour j's chance of this point: none from the fitting's other face.
						const float theirs = EmitterSide(light, nP[j]) == side
										   ? EmitterScore(nP[j], nN[j], nV[j], nRough[j], light) / nTotal[j] : 0.0;
						m += (chance / float(k)) / max(theirs + chance / float(k), 1.0e-30);
					}
					m /= float(k);
				}
				const float w = m * target / drawnAt;
				weightSum += w;
				chosen = f;
				chosenTarget = target;
				chosenL = L;
				chosenDist = dist;
			}
		}
	}
	// Each neighbour's draw, weighed from here.
	for (int j = 0; j < k; ++j)
	{
		GiEmitter light;
		vec3 y, radiance;
		float density, side, theirTotal;
		if (!LampDraw(nAt[j], light, y, radiance, density, side, theirTotal))
			continue;
		// The point was stepped out to the face the neighbour sees; from the other face this
		// texel could not have drawn it, and the fitting's own shell hides it.
		if (EmitterSide(light, P) != side)
			continue;
		vec3 L;
		float dist, chanceHere;
		const vec3 f = LampIntegrand(P, N, V, roughness, total, light, y, radiance, density, L, dist, chanceHere);
		const float target = dot(f, vec3(0.2126, 0.7152, 0.0722));
		const float chanceTheirs = EmitterScore(nP[j], nN[j], nV[j], nRough[j], light) / nTotal[j];
		const float drawnAt = chanceTheirs * density;
		if (target <= 0.0 || drawnAt <= 0.0)
			continue;
		const float m = (chanceTheirs / float(k)) / max(chanceTheirs + chanceHere / float(k), 1.0e-30);
		const float w = m * target / drawnAt;
		weightSum += w;
		if (GiRandom(seed) * weightSum < w)
		{
			chosen = f;
			chosenTarget = target;
			chosenL = L;
			chosenDist = dist;
		}
	}
	if (weightSum <= 0.0 || chosenTarget <= 0.0)
		return vec3(0.0);
	// One shadow ray, for the one candidate kept.
	const float visible = TraceShadowFrom(P, N, chosenL, chosenDist * 0.999);
	return chosen / chosenTarget * weightSum * visible;
}
"""

edit("""void main()
{
	const ivec2 texel = ivec2(gl_FragCoord.xy);""", POOL.lstrip('\n') + """
void main()
{
	const ivec2 texel = ivec2(gl_FragCoord.xy);""")

# main: the pooled light replaces the texel's own aimed light wherever the pool runs.
edit("""	const float planeTolerance = 0.02 + 0.01 * eyeDistance;
	const float rotation = Rotation(texel);

	const int taps = min(clamp(int(u_Scene.Indirect.w + 0.5), 1, 8) * 8, kMaxTaps);
""", """	const float planeTolerance = 0.02 + 0.01 * eyeDistance;
	const float rotation = Rotation(texel);

	const int taps = min(clamp(int(u_Scene.Indirect.w + 0.5), 1, 8) * 8, kMaxTaps);

	// **RT-24 (2026-09-25): the lamp light, pooled** (PooledLamp above) -- where the pool is on and
	// the texel cast the one ray whose draw it kept; a texel that cast several (a mover's) already
	// averages its own draws, and keeps them.
	vec3 lamp = ownAimed;
	const int pool = clamp(int(u_Reflection.Probe.w + 0.5), 0, 8);
	if (pool > 0 && texelFetch(u_Travel, texel, 0).w < 1.5)
	{
		const vec4 albedo = texelFetch(u_Albedo, texel, 0);
		const float metal = clamp(texelFetch(u_Surface, texel, 0).a, 0.0, 1.0);
		// The trace's tint, from the same F0 and split-sum term (reflection_trace, "only the tint's
		// hue, never its magnitude").
		const vec3 F0 = mix(vec3(0.08 * clamp(albedo.a, 0.0, 1.0)), albedo.rgb, metal);
		const vec2 envBRDF = EnvBRDF(max(dot(N, V), 0.0), roughness);
		const vec3 reflectance = F0 * envBRDF.x + envBRDF.y;
		const float tintLuma = dot(reflectance, vec3(0.2126, 0.7152, 0.0722));
		const vec3 tint = tintLuma > 1.0e-5 ? reflectance / tintLuma : vec3(1.0);
		lamp = min(PooledLamp(texel, size, P, N, V, roughness, reach, rotation, taps, pool), vec3(64.0)) * tint;
	}
""")
edit("""	if (weightSum > 0.0)
	{
		o_Resolved = vec4(sum / weightSum + ownAimed, distanceSum / weightSum);""",
     """	o_Resolved = vec4(fresh.rgb + lamp, fresh.a);
	if (weightSum > 0.0)
	{
		o_Resolved = vec4(sum / weightSum + lamp, distanceSum / weightSum);""")

io.open(S, 'w', encoding='utf-8', newline='').write(t.replace('\n', '\r\n'))
print('ok')
