import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io


def edit(p, a, b):
    t = io.open(p, encoding='utf-8', newline='').read()
    eol = '\r\n' if t.count('\r\n') > t.count('\n') // 2 else '\n'
    a, b = a.replace('\n', eol), b.replace('\n', eol)
    assert t.count(a) == 1, (p, t.count(a), a[:80])
    io.open(p, 'w', encoding='utf-8', newline='').write(t.replace(a, b))


R = (_RAGEV + '/')
T = R + 'RageVEditor/assets/shaders/reflection_trace.rvshader'
S = R + 'RageVEditor/assets/shaders/reflection_resolve.rvshader'

# --- the trace: the aimed light in a lane of its own ---------------------------------------
edit(T, """layout(location = 3) out vec4 o_HitTravel;
#endif
""", """layout(location = 3) out vec4 o_HitTravel;
// **RT-24 (2026-09-24): the light the aimed samples brought, kept apart from what the lobe's
// rays carried.** The resolve shares a texel's lobe rays with its neighbours by re-aiming each
// neighbour's hit point from here; an aimed sample arrived along a different direction (toward a
// lamp), so re-aimed as though it had come along the lobe ray it spread each lamp's light in a
// halo around its reflection. The resolve now shares the lobe's part alone and adds each texel's
// own aimed light back after it. rgb the mean over the texel's rays, tinted like o_Reflection.
layout(location = 4) out vec4 o_Aimed;
#endif
""")
edit(T, """	o_HitTravel = vec4(0.0);

	const ivec2 texel = ivec2(gl_FragCoord.xy);""", """	o_HitTravel = vec4(0.0);
	o_Aimed = vec4(0.0);

	const ivec2 texel = ivec2(gl_FragCoord.xy);""")
edit(T, """	float emissiveSum = hit.Missed ? 0.0 : dot(hit.Emissive, vec3(0.2126, 0.7152, 0.0722));
""", """	float emissiveSum = hit.Missed ? 0.0 : dot(hit.Emissive, vec3(0.2126, 0.7152, 0.0722));
	// RT-24: the aimed samples' light over the texel's rays, summed here and written apart.
	vec3 aimedSum = vec3(0.0);
""")
edit(T, """		const vec3 aimed = AimedAtEmitters(P, N, V, roughness, emitters, neeSeed);
		radiance += aimed;
		// The clamp downstream reads this lane to tell a ray that found a lamp
		// from a ray that got lucky (2026-09-21). With the aimed sample most of
		// a lamp's light no longer arrives as a hit, so the lane now carries
		// both halves -- what the ray kept and what the aim brought.
		emissiveSum = dot(kept + aimed, vec3(0.2126, 0.7152, 0.0722));""", """		const vec3 aimed = AimedAtEmitters(P, N, V, roughness, emitters, neeSeed);
		radiance += aimed;
		aimedSum = aimed;
		// The clamp downstream reads this lane to tell a ray that found a lamp
		// from a ray that got lucky (2026-09-21) -- in the taps it shares, which
		// since RT-24 carry the lobe's part alone: so the lane is what the ray
		// kept of a struck lamp's glow, and the aim's light rides o_Aimed.
		emissiveSum = dot(kept, vec3(0.2126, 0.7152, 0.0722));""")
edit(T, """			arrived += aimedMore;
			glow = dot(keptMore + aimedMore, vec3(0.2126, 0.7152, 0.0722));""", """			arrived += aimedMore;
			aimedSum += aimedMore;
			glow = dot(keptMore, vec3(0.2126, 0.7152, 0.0722));""")
edit(T, """	o_Reflection = vec4(min(radiance, vec3(64.0)) * tint, travelledSum / float(rays));""",
     """	const vec3 aimedMean = aimedSum / float(rays);
	o_Reflection = vec4(min(max(radiance - aimedMean, vec3(0.0)), vec3(64.0)) * tint,
						travelledSum / float(rays));
	o_Aimed = vec4(min(aimedMean, vec3(64.0)) * tint, 0.0);""")

# --- the resolve: share the lobe's part, add this texel's own aimed light back ----------------
edit(S, """layout(set = 3, binding = 4) uniform sampler2D u_Travel;
""", """layout(set = 3, binding = 4) uniform sampler2D u_Travel;
// **RT-24 (2026-09-24): the trace's fifth, the light its aimed samples brought** -- kept out of
// the taps shared below (an aimed sample did not arrive along the lobe ray a tap is re-aimed by)
// and added back to this texel's own result. Black where no trace wrote one.
layout(set = 3, binding = 5) uniform sampler2D u_Aimed;
""")
edit(S, """	const vec4 fresh = texelFetch(u_Fresh, texel, 0);
	o_Resolved = fresh;""", """	const vec4 fresh = texelFetch(u_Fresh, texel, 0);
	const vec3 ownAimed = fresh.a >= 0.0 ? texelFetch(u_Aimed, texel, 0).rgb : vec3(0.0);
	o_Resolved = vec4(fresh.rgb + ownAimed, fresh.a);""")
edit(S, """		o_Resolved = vec4(sum / weightSum, distanceSum / weightSum);""",
     """		o_Resolved = vec4(sum / weightSum + ownAimed, distanceSum / weightSum);""")

# --- the pipeline: the trace writes a fifth attachment ------------------------------------------
edit(R + 'RageV/src/RageV/Renderer/Renderer3D.cpp', """			const int extras = pass == 0 ? 3 : pass == 1 ? 1""",
     """			// **RT-24: and the trace a fifth** -- the light its aimed samples brought, apart
			// from the lobe's, so the resolve shares only what a re-aimed ray carried.
			const int extras = pass == 0 ? 4 : pass == 1 ? 1""")

# --- the resolve binds it -------------------------------------------------------------------------
edit(R + 'RageV/src/RageV/Renderer/Renderer3D.h', """									   const RHI::Ref<RHI::RHITexture>& travel = nullptr);
		// **The reconstruction contract""", """									   const RHI::Ref<RHI::RHITexture>& travel = nullptr,
									   // RT-24: the trace's fifth lane, its aimed samples' light,
									   // added back after the sharing. Null reads as none.
									   const RHI::Ref<RHI::RHITexture>& aimed = nullptr);
		// **The reconstruction contract""")
edit(R + 'RageV/src/RageV/Renderer/Renderer3D.cpp', """										bool glassLayer,
										const RHI::Ref<RHITexture>& travel)
	{""", """										bool glassLayer,
										const RHI::Ref<RHITexture>& travel,
										const RHI::Ref<RHITexture>& aimed)
	{""")
edit(R + 'RageV/src/RageV/Renderer/Renderer3D.cpp', """		if (inputs->HasBinding(4))
			inputs->SetTexture(4, travel ? travel : TextureLoader::TransparentBlack(*s_Data->Device),
							   s_Data->PointSampler);
		inputs->Commit();

		ReflectionPushConstants push;
		push.InverseViewProjection = Math::Inverse(s_Data->Scene.ViewProjection);
		push.History.w = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;""", """		if (inputs->HasBinding(4))
			inputs->SetTexture(4, travel ? travel : TextureLoader::TransparentBlack(*s_Data->Device),
							   s_Data->PointSampler);
		// RT-24: the aimed samples' light, added back after the sharing. Black: none.
		if (inputs->HasBinding(5))
			inputs->SetTexture(5, aimed ? aimed : TextureLoader::TransparentBlack(*s_Data->Device),
							   s_Data->PointSampler);
		inputs->Commit();

		ReflectionPushConstants push;
		push.InverseViewProjection = Math::Inverse(s_Data->Scene.ViewProjection);
		push.History.w = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;""")

# --- the frame: both trace targets grow a fifth lane, and both resolves read it --------------------
F = R + 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
edit(F, """			traceDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT, Format::R16G16_SFLOAT,
									  Format::R16G16B16A16_SFLOAT };""", """			// RT-24: and the aimed samples' light, apart from the lobe's (reflection_trace).
			traceDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT, Format::R16G16_SFLOAT,
									  Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT };""")
edit(F, """			glassRayDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT, Format::R16G16_SFLOAT,
										 Format::R16G16B16A16_SFLOAT };""", """			// RT-24: and the aimed samples' light, as the floor's trace (reflection_trace).
			glassRayDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT, Format::R16G16_SFLOAT,
										 Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT };""")
edit(F, """					Renderer3D::ResolveReflections(context.Color(traced),
												   context.Color(traced, 1),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex), false,
												   context.Color(traced, 3));""", """					Renderer3D::ResolveReflections(context.Color(traced),
												   context.Color(traced, 1),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex), false,
												   context.Color(traced, 3),
												   context.Color(traced, 4));""")
edit(F, """					Renderer3D::ResolveReflections(context.Color(glassRays),
												   context.Color(glassRays, 1),
												   context.Depth(glassLayer),
												   context.Color(glassLayer, 1), true,
												   context.Color(glassRays, 3));""", """					Renderer3D::ResolveReflections(context.Color(glassRays),
												   context.Color(glassRays, 1),
												   context.Depth(glassLayer),
												   context.Color(glassLayer, 1), true,
												   context.Color(glassRays, 3),
												   context.Color(glassRays, 4));""")
print('ok')
