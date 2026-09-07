"""RT-first T5, shader side (docs/RT-FIRST.md section 2d):
- pbr_fragment.glsl: the two direct-light textures at set 0 bindings 26/27 in
  the lit variants only; the uniform switch (RayRates.w bit 22); the loop
  keeps only the subtractive case under the switch and adds the textures
  once; the G-buffer's albedo lane carries the specular scalar and the
  surface id's sign carries Static; ClusterCellFor(worldPos) for a pass with
  no varyings.
- docs: the owner's directive (everything RT from the G-buffer), and the two
  new items (the water on the G-buffer; TAA and the accumulators on it).
"""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def load(p):
    s = open(p, 'rb').read().decode('utf-8'); return s, ('\r\n' if '\r\n' in s else '\n')
def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)
def rep(s, nl, old, new, count=1):
    o = old.replace('\n', nl); n = new.replace('\n', nl)
    assert s.count(o) == count, (s.count(o), old[:70]); return s.replace(o, n)

p = 'RageVEditor/assets/shaders/include/pbr_fragment.glsl'; s, nl = load(p)

# 1. the inputs, lit variants only
s = rep(s, nl, """layout(set = 0, binding = 16) uniform sampler2D u_Indirect;
""", """layout(set = 0, binding = 16) uniform sampler2D u_Indirect;

// **RT-first T5: the direct light as a signal** (docs/RT-FIRST.md 2d). Under
// ray tracing the DirectTrace pass chooses, shades and traces the lights for
// every opaque pixel from the G-buffer and the accumulator settles it; the lit
// shader adds these two pictures where its loop used to trace a ray to every
// light. Diffuse is before the albedo and the 1/pi (the denoiser must never
// blur texture), specular complete. Only the lit variants read them, and only
// they declare them: a set layout comes from reflection, and a binding a
// pipeline does not reference cannot be written on its set.
#if defined(RV_RAY_SHADOWS) && !defined(RV_TRACE_ONLY) && !defined(RV_GBUFFER) \\
	&& !defined(RV_TRANSPARENT) && !defined(RV_WATER) && !defined(RV_IRRADIANCE_FILL)
#define RV_DIRECT_SIGNAL_INPUT
layout(set = 0, binding = 26) uniform sampler2D u_DirectDiffuse;
layout(set = 0, binding = 27) uniform sampler2D u_DirectSpecular;
#endif
""")

# 2. the cluster cell for a pass with no varyings (the loop's ClusterIndexFor
#    reads v_ClipPos, which a fullscreen pass has not got)
s = rep(s, nl, """	return (z * uint(tileCount.y) + tile.y) * uint(tileCount.x) + tile.x;
}
#endif
""", """	return (z * uint(tileCount.y) + tile.y) * uint(tileCount.x) + tile.x;
}
#endif

// The same cell from a world position alone -- projected through the scene's
// view-projection instead of read from the fragment's clip position -- for
// the passes that have no varyings (RT-first T5's DirectTrace; the water's
// lamp passes carry their own copy in water_lamps.glsl).
uint ClusterCellFor(vec3 worldPos)
{
	const vec2 tileCount = u_Scene.ClusterGrid.xy;
	const vec4 clip = u_Scene.ViewProjection * vec4(worldPos, 1.0);
	const vec2 ndc = clip.xy / max(abs(clip.w), 1.0e-6) * sign(clip.w);
	const uvec2 tile = uvec2(clamp(ndc * 0.5 + 0.5, vec2(0.0), vec2(0.9999)) * tileCount);
	const float viewDepth = dot(worldPos - u_Scene.CameraPosition.xyz,
								u_Scene.CameraForward.xyz);
	float slice = 0.0;
	if (viewDepth > u_Scene.ClusterDepth.x)
		slice = log(viewDepth) * u_Scene.ClusterDepth.z + u_Scene.ClusterDepth.w;
	const uint z = uint(clamp(slice, 0.0, u_Scene.ClusterGrid.z - 1.0));
	return (z * uint(tileCount.y) + tile.y) * uint(tileCount.x) + tile.x;
}
""")

# 3. the G-buffer lanes: specular scalar beside the albedo (metallic already
#    rides o_Surface.a), Static in the id's sign
s = rep(s, nl, """#ifdef RV_GBUFFER
	o_Albedo = vec4(albedo, metallic);
	o_SurfaceId = v_ObjectId;
	return;
#endif""", """#ifdef RV_GBUFFER
	// The albedo and the specular scalar (metallic already rides o_Surface.a);
	// the object's id, negative for a Static surface (RT-first T5) -- what the
	// DirectTrace pass needs of the material and the static/moving split.
	o_Albedo = vec4(albedo, clamp(surface.Specular, 0.0, 1.0));
	o_SurfaceId = v_Instance.y > 0.5 ? -v_ObjectId : v_ObjectId;
	return;
#endif""")

# 4. the switch, read once
s = rep(s, nl, """	int directionalCount = int(u_Scene.ClusterGrid.w);
""", """	int directionalCount = int(u_Scene.ClusterGrid.w);
	// RT-first T5: whether the DirectTrace pass ran this frame (RayRates.w
	// bit 22, set by the frame graph). A uniform rather than a define, so a
	// preset change needs no recompile.
#ifdef RV_DIRECT_SIGNAL_INPUT
	const bool directSignal = (int(u_Scene.RayRates.w + 0.5) & 4194304) != 0;
#else
	const bool directSignal = false;
#endif
""")

# 5. the WR-17 pre-loop and the in-loop sampler are the loop's; off under the signal
s = rep(s, nl, """	float directIrradiance = 0.0;
	if (mod(u_Scene.ShadowRayFade.x, 16.0) > 4.5)""", """	float directIrradiance = 0.0;
	if (!directSignal && mod(u_Scene.ShadowRayFade.x, 16.0) > 4.5)""")
s = rep(s, nl, """	if (sampleCount > 0 && shadowBudget == 0 && shadeLimit < 0 && fieldWeight <= 0.0""",
       """	if (!directSignal && sampleCount > 0 && shadowBudget == 0 && shadeLimit < 0 && fieldWeight <= 0.0""")

# 6. under the signal the loop keeps only the subtractive case
s = rep(s, nl, """		if (survivor)
			liveShare *= sampleScale[entry - directionalCount];
""", """		if (survivor)
			liveShare *= sampleScale[entry - directionalCount];
#ifdef RV_DIRECT_SIGNAL_INPUT
		// RT-first T5: with the direct-light signal on, every light's live
		// share was chosen, shaded and traced in the DirectTrace pass and
		// arrives in two textures after this loop. What stays here is the
		// subtractive case alone -- a static surface under a fully baked lamp
		// with a moving object in its range: its moving-only ray and the clamp
		// against the field's stored direct light, which only this shader
		// reads. The live share is zeroed so nothing below adds it twice and
		// no ordinary ray is traced.
		if (directSignal)
		{
			if (!subtractive)
				continue;
			liveShare = 0.0;
		}
#endif
""")

# 7. the textures added once, where the loop's light ends
s = rep(s, nl, """	// it is somewhere.
	vec3 ambientLight = u_Scene.Ambient.rgb * u_Scene.Ambient.a;
""", """	// it is somewhere.
#ifdef RV_DIRECT_SIGNAL_INPUT
	// RT-first T5: the direct light the DirectTrace pass and its accumulator
	// settled for this pixel -- the diffuse before the albedo and the 1/pi,
	// the specular complete. By texel: a picture at this pixel's own place,
	// never filtered across an edge.
	if (directSignal)
	{
		const ivec2 directTexel = ivec2(gl_FragCoord.xy);
		Lo += texelFetch(u_DirectDiffuse, directTexel, 0).rgb * albedo / PI
			+ texelFetch(u_DirectSpecular, directTexel, 0).rgb;
	}
#endif
	vec3 ambientLight = u_Scene.Ambient.rgb * u_Scene.Ambient.a;
""")
save(p, s)

# ---- docs: the owner's directive and the two new items
p = 'docs/RT-FIRST.md'; s, nl = load(p)
s = rep(s, nl, """5. **Raster and RT kept on a par** for every feature both can have; raster may lag where it cannot.""",
"""5. **Raster and RT kept on a par** for every feature both can have; raster may lag where it cannot.
7. **Everything RT reads the G-buffer** (owner, 2026-09-06 evening: "pretty much anything that has been implemented for RT, if possible implement it using G buffers, use it to your advantage"): every ray-traced signal -- shadows/direct light, AO, GI, reflections, and the water's rays -- is traced in its own pass from the G-buffer's depth, normal, roughness, albedo and id, never inside the lit shader, and the temporal systems (TAA, every accumulator) validate their histories by the G-buffer's depth, normal and id rather than by colour alone. T5 is the first instance; T12 and T13 below carry the water and TAA.""")
s = rep(s, nl, """| T11 | Next-event estimation at GI and reflection hits with the resampled light | 3c | medium | |""",
"""| T11 | Next-event estimation at GI and reflection hits with the resampled light | 3c | medium | |
| T12 | **The water on the G-buffer** (owner, 2026-09-06 evening): the water's surface prepass becomes G-buffer lanes (or a second G-buffer layer for the sea), so the direct-light signal (T5), reflections, refraction and TAA cover the sea like any surface; the water's own choose/shade/accumulate passes (S4) fold into the shared contract; R7 (the water writes its motion) lands here | 2f | large | |
| T13 | **TAA and the accumulators on the G-buffer** (owner, 2026-09-06 evening: "TAA gets better in the presence of G buffers, even accumulation"): history rejection by depth, normal and surface id (disocclusion and silhouettes told by geometry, not by the colour clamp alone), one shared reprojection for TAA and every signal, the still-feedback rule read from the G-buffer's velocity instead of a project constant | 2g | medium | |""")
save(p, s)
print('T5a patched')
