"""v15: the trace draws its one ray from a low-discrepancy sequence: Halton
(2,3) progressive over the accumulator's 64-frame memory, rotated per texel
by the R2 lattice (a Cranley-Patterson rotation), so a texel's samples
stratify the lobe over time and its neighbours cover other parts of it."""
p = 'C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_trace.rvshader'
s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
def rep(old, new):
    global s; o = old.replace('\n', nl); assert s.count(o) == 1, old[:60]; s = s.replace(o, new.replace('\n', nl))
rep("""vec3 SurfaceNormal(vec2 e)
{
	return OctDecode(e);
}
""",
"""vec3 SurfaceNormal(vec2 e)
{
	return OctDecode(e);
}

float Halton(uint index, uint base)
{
	float f = 1.0, r = 0.0;
	for (int i = 0; i < 12 && index > 0u; ++i)
	{
		f /= float(base);
		r += f * float(index % base);
		index /= base;
	}
	return r;
}

// The ray's direction from a low-discrepancy sequence rather than a hash
// (2026-09-06, after the roughness-aware design note): Halton (2, 3),
// progressive over the accumulator's 64-frame memory so a parked texel's
// draws stratify its lobe, rotated per texel by the R2 lattice so its
// neighbours cover other parts of the lobe than it does -- which is what
// the resolve's disc then gathers. GGX by its distribution of half
// vectors, as GlossyReflection does; below the horizon, the mirror.
vec3 GlossyReflectionLD(vec3 N, vec3 V, float roughness, ivec2 texel, uint frame)
{
	const vec3 mirror = reflect(-V, N);
	const float alpha = roughness * roughness;
	if (alpha < 1.0e-4)
		return mirror;
	const uint index = (frame & 63u) + 1u;
	const vec2 rotation = fract(vec2(texel) * vec2(0.7548776662, 0.5698402910));
	const float u1 = fract(Halton(index, 2u) + rotation.x);
	const float u2 = fract(Halton(index, 3u) + rotation.y);
	const float cosTheta = sqrt((1.0 - u1) / (1.0 + (alpha * alpha - 1.0) * u1));
	const float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
	const float phi = 6.2831853 * u2;
	const vec3 axis = abs(N.x) < 0.7 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
	const vec3 t = normalize(cross(axis, N));
	const vec3 b = cross(N, t);
	const vec3 H = normalize(t * (sinTheta * cos(phi)) + b * (sinTheta * sin(phi)) + N * cosTheta);
	const vec3 L = reflect(-V, H);
	return dot(L, N) > 0.02 ? L : mirror;
}
"""),
rep("""	const vec3 direction = GlossyReflection(N, V, roughness, 0, 1);
""",
"""	const vec3 direction = GlossyReflectionLD(N, V, roughness, texel, uint(u_Scene.GlobalIllumination.y));
""")
open(p, 'wb').write(s.encode('utf-8')); print('v15 patched')
