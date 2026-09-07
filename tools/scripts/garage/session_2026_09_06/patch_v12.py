"""v12: at silhouette texels (a 3x3 neighbour on another surface) the
accumulator keeps the texel's own history across the surface flip that
jitter causes, so the picture there averages over the jitter sequence the
way TAA's colour does; the bound below still clamps it."""
p = 'C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_accumulate.rvshader'
s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
def rep(old, new):
    global s; o = old.replace('\n', nl); assert s.count(o) == 1, old[:60]; s = s.replace(o, new.replace('\n', nl))
rep("""bool HistoryAt(vec3 world, ivec2 size, vec3 P, vec3 N, float roughness,
			   float eyeDistance, out Candidate c)
{""",
"""// Whether a 3x3 neighbour lies on another surface: a silhouette texel,
// whose own surface flips sides with the jitter every frame.
bool AtSilhouette(ivec2 texel, ivec2 size, vec3 P, vec3 N, float eyeDistance)
{
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
		{
			if (x == 0 && y == 0)
				continue;
			vec3 Pn, Nn;
			if (!SurfaceAt(clamp(texel + ivec2(x, y), ivec2(0), size - 1), size, Pn, Nn))
				return true;
			if (dot(N, Nn) < 0.8 || abs(dot(N, Pn - P)) > 0.05 + 0.01 * eyeDistance)
				return true;
		}
	return false;
}

bool HistoryAt(vec3 world, ivec2 size, vec3 P, vec3 N, float roughness,
			   float eyeDistance, bool silhouette, out Candidate c)
{"""),
rep("""		const ivec2 pastTexel = clamp(centre + offsets[k], ivec2(0), size - 1);
		c.reflector = texelFetch(u_HistorySurface, pastTexel, 0);
		if (c.reflector.a < 0.0)
			continue;
		const vec3 wasN = OctDecode(c.reflector.rg);
		if (dot(wasN, N) < 0.8)
			continue;
		if (abs(dot(wasN, P) - c.reflector.b) > 0.05 + 0.01 * eyeDistance)
			continue;
		c.extra = texelFetch(u_HistoryExtra, pastTexel, 0);
		if (abs(c.extra.r - roughness) > 0.5)
			continue;
""",
"""		const ivec2 pastTexel = clamp(centre + offsets[k], ivec2(0), size - 1);
		c.reflector = texelFetch(u_HistorySurface, pastTexel, 0);
		if (c.reflector.a < 0.0)
			continue;
		c.extra = texelFetch(u_HistoryExtra, pastTexel, 0);
		// At a silhouette the texel's own history is kept whichever side it
		// showed last frame: under jitter the side flips every frame, and a
		// history refused there was one frame old and shook (2026-09-06,
		// parked bright edges 7.5 levels a frame against 0.9 with no
		// reflections). Averaged across the flip it is the coverage-weighted
		// picture, which is what TAA's colour is at the same texel. The
		// bound below still holds it to this frame's neighbourhood.
		if (!(silhouette && k == 0))
		{
			const vec3 wasN = OctDecode(c.reflector.rg);
			if (dot(wasN, N) < 0.8)
				continue;
			if (abs(dot(wasN, P) - c.reflector.b) > 0.05 + 0.01 * eyeDistance)
				continue;
			if (abs(c.extra.r - roughness) > 0.5)
				continue;
		}
"""),
rep("""		const bool haveSurface = HistoryAt(P, size, P, N, roughness, eyeDistance, atSurface);""",
"""		const bool silhouette = AtSilhouette(texel, size, P, N, eyeDistance);
		const bool haveSurface = HistoryAt(P, size, P, N, roughness, eyeDistance, silhouette, atSurface);"""),
rep("""		bool have = HistoryAt(P + sight * image, size, P, N, roughness, eyeDistance, c);""",
"""		bool have = HistoryAt(P + sight * image, size, P, N, roughness, eyeDistance, silhouette, c);""")
open(p, 'wb').write(s.encode('utf-8')); print('v12 patched')
