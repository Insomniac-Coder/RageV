"""RT-first T5, the contract's second payload (RV_SIGNAL_PAIR): the
accumulator and its blur carry two pictures through one set of surface
tests, one refusal reason and one blur radius -- the direct light's diffuse
and specular halves. Compiled out for the reflection instance."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')
exec(open('tools/scripts/garage/session_2026_09_06/t4_prelude.py', encoding='utf-8').read())

# ------------------------------------------------------------ accumulate
p = 'RageVEditor/assets/shaders/reflection_accumulate.rvshader'; s, nl = load(p)
assert 'RV_SIGNAL_PAIR' not in s
s = rep(s, nl, """layout(set = 3, binding = 6) uniform sampler2D u_Velocity;
""", """layout(set = 3, binding = 6) uniform sampler2D u_Velocity;
#ifdef RV_SIGNAL_PAIR
// The second payload (RT-first T5): a signal of two pictures -- the direct
// light's diffuse and specular halves -- sharing every test above. Its fresh
// frame and its own history; the same texel, the same frames, the same bound
// width from its own neighbourhood.
layout(set = 3, binding = 7) uniform sampler2D u_Fresh2;
layout(set = 3, binding = 8) uniform sampler2D u_History2;
#endif
""")
s = rep(s, nl, """layout(location = 2) out vec4 o_Extra;
""", """layout(location = 2) out vec4 o_Extra;
#ifdef RV_SIGNAL_PAIR
layout(location = 3) out vec4 o_Accumulated2;
#endif
""")
# the neighbourhood over any picture
s = rep(s, nl, """void Neighbourhood(ivec2 texel, ivec2 size, out vec3 mean, out vec3 sd)
{
	vec3 sum = vec3(0.0), sum2 = vec3(0.0);
	float n = 0.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			const ivec2 at = clamp(texel + ivec2(x, y), ivec2(0), size - 1);
			const vec4 f = texelFetch(u_Fresh, at, 0);
""", """void Neighbourhood(ivec2 texel, ivec2 size, out vec3 mean, out vec3 sd)
{
	vec3 sum = vec3(0.0), sum2 = vec3(0.0);
	float n = 0.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			const ivec2 at = clamp(texel + ivec2(x, y), ivec2(0), size - 1);
			const vec4 f = texelFetch(u_Fresh, at, 0);
""")
# Candidate remembers where it read, so the second payload reads the same place
s = rep(s, nl, """	vec2 pastUv;
	vec2 thenNdc;
};""", """	vec2 pastUv;
	vec2 thenNdc;
	ivec2 pastTexel;  // the texel the candidate came from (RV_SIGNAL_PAIR reads its twin there)
	bool  bilinear;   // whether the picture was sampled at pastUv rather than fetched
};""")
s = rep(s, nl, """		c.past = k == 0 ? texture(u_History, c.pastUv) : texelFetch(u_History, pastTexel, 0);
		if (c.past.a > 0.0)
			return true;""", """		c.past = k == 0 ? texture(u_History, c.pastUv) : texelFetch(u_History, pastTexel, 0);
		c.pastTexel = pastTexel;
		c.bilinear = k == 0;
		if (c.past.a > 0.0)
			return true;""")
# main: the second fresh picture, its kept value, the invalid branch
s = rep(s, nl, """	const vec4 fresh = texelFetch(u_Fresh, texel, 0);
""", """	const vec4 fresh = texelFetch(u_Fresh, texel, 0);
#ifdef RV_SIGNAL_PAIR
	const vec4 fresh2 = texelFetch(u_Fresh2, texel, 0);
	vec3 kept2 = fresh2.rgb;
#endif
""")
s = rep(s, nl, """	vec3 P, N;
	if (fresh.a < 0.0 || !SurfaceAt(texel, size, P, N))
	{
		o_Accumulated = vec4(0.0);
		o_Surface = vec4(0.0, 0.0, 0.0, -1.0);
		o_Extra = vec4(0.0);
		return;
	}""", """	vec3 P, N;
	if (fresh.a < 0.0 || !SurfaceAt(texel, size, P, N))
	{
		o_Accumulated = vec4(0.0);
		o_Surface = vec4(0.0, 0.0, 0.0, -1.0);
		o_Extra = vec4(0.0);
#ifdef RV_SIGNAL_PAIR
		o_Accumulated2 = vec4(0.0);
#endif
		return;
	}""")
s = rep(s, nl, """			frames = min(c.past.a + 1.0, memory);
			kept = mix(held, fresh.rgb, 1.0 / frames);
""", """			frames = min(c.past.a + 1.0, memory);
			kept = mix(held, fresh.rgb, 1.0 / frames);
#ifdef RV_SIGNAL_PAIR
			// The twin: read where the candidate was read, bounded by its
			// own neighbourhood at the same width, blended by the same
			// frames. A specular half is sharper than its diffuse twin, so
			// the bound is its own; the memory is shared.
			{
				vec3 mean2, sd2;
				Neighbourhood2(texel, size, mean2, sd2);
				const vec4 past2 = c.bilinear ? texture(u_History2, c.pastUv)
											  : texelFetch(u_History2, c.pastTexel, 0);
				const vec3 halfWidth2 = max(sd2 * width, vec3(1.0e-4));
				const vec3 held2 = clamp(past2.rgb, mean2 - halfWidth2, mean2 + halfWidth2);
				kept2 = mix(held2, fresh2.rgb, 1.0 / frames);
			}
#endif
""")
s = rep(s, nl, """	o_Accumulated = vec4(kept, frames);
""", """	o_Accumulated = vec4(kept, frames);
#ifdef RV_SIGNAL_PAIR
	o_Accumulated2 = vec4(kept2, frames);
#endif
""")
# Neighbourhood2: the same walk over u_Fresh2, placed after Neighbourhood
s = rep(s, nl, """	n = max(n, 1.0);
	mean = sum / n;
	sd = sqrt(max(sum2 / n - mean * mean, vec3(0.0)));
}
""", """	n = max(n, 1.0);
	mean = sum / n;
	sd = sqrt(max(sum2 / n - mean * mean, vec3(0.0)));
}
#ifdef RV_SIGNAL_PAIR
void Neighbourhood2(ivec2 texel, ivec2 size, out vec3 mean, out vec3 sd)
{
	vec3 sum = vec3(0.0), sum2 = vec3(0.0);
	float n = 0.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			const ivec2 at = clamp(texel + ivec2(x, y), ivec2(0), size - 1);
			const vec4 f = texelFetch(u_Fresh2, at, 0);
			if (f.a < 0.0)
				continue;
			sum += f.rgb;
			sum2 += f.rgb * f.rgb;
			n += 1.0;
		}
	}
	n = max(n, 1.0);
	mean = sum / n;
	sd = sqrt(max(sum2 / n - mean * mean, vec3(0.0)));
}
#endif
""")
save(p, s)

# ------------------------------------------------------------ blur
p = 'RageVEditor/assets/shaders/reflection_blur.rvshader'; s, nl = load(p)
assert 'RV_SIGNAL_PAIR' not in s
s = rep(s, nl, """layout(set = 3, binding = 3) uniform sampler2D u_ImageDistance;
""", """layout(set = 3, binding = 3) uniform sampler2D u_ImageDistance;
#ifdef RV_SIGNAL_PAIR
layout(set = 3, binding = 4) uniform sampler2D u_Accumulated2;   // the twin, same weights
#endif
""")
s = rep(s, nl, """layout(location = 0) out vec4 o_Blurred;
""", """layout(location = 0) out vec4 o_Blurred;
#ifdef RV_SIGNAL_PAIR
layout(location = 1) out vec4 o_Blurred2;
#endif
""")
s = rep(s, nl, """	const vec4 centre = texelFetch(u_Accumulated, texel, 0);
	o_Blurred = centre;
""", """	const vec4 centre = texelFetch(u_Accumulated, texel, 0);
	o_Blurred = centre;
#ifdef RV_SIGNAL_PAIR
	const vec4 centre2 = texelFetch(u_Accumulated2, texel, 0);
	o_Blurred2 = centre2;
	vec3 sum2 = vec3(0.0);
#endif
""")
s = rep(s, nl, """			sum += f.rgb * w;
			weightSum += w;
""", """			sum += f.rgb * w;
#ifdef RV_SIGNAL_PAIR
			sum2 += texelFetch(u_Accumulated2, at, 0).rgb * w;
#endif
			weightSum += w;
""")
s = rep(s, nl, """	if (weightSum > 0.0)
		o_Blurred = vec4(sum / weightSum, centre.a);
""", """	if (weightSum > 0.0)
	{
		o_Blurred = vec4(sum / weightSum, centre.a);
#ifdef RV_SIGNAL_PAIR
		o_Blurred2 = vec4(sum2 / weightSum, centre2.a);
#endif
	}
""")
save(p, s)
print('T5e patched (shaders)')
