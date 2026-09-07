import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def patch(p, pairs):
    s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
    for old, new in pairs:
        o = old.replace('\n', nl); n = new.replace('\n', nl)
        assert s.count(o) == 1, (p, s.count(o), old[:60]); s = s.replace(o, n)
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)

# The resolve learns a stride, for a second, dilated pass.
patch('RageVEditor/assets/shaders/reflection_resolve.rvshader', [(
"""	// w: one where a texture row runs the other way up from a projected
	// coordinate. The rest unused here; the block is the accumulator's.
	vec4 History;
	vec4 Probe;
} u_Reflection;""",
"""	// w: one where a texture row runs the other way up from a projected
	// coordinate. The rest unused here; the block is the accumulator's.
	vec4 History;
	// z: the stride between taps, in texels. One for the first pass; two
	// for the second, which reads the first's output and so reaches twice
	// as far for the same taps -- the dilated (a-trous) second pass every
	// SVGF-class denoiser uses to get a wide kernel cheaply.
	vec4 Probe;
} u_Reflection;"""),
("""	const float sigma = float(kMaxRadius) * 0.6 * smoothstep(0.05, 0.45, roughness);
	if (sigma < 0.15)
		return;
	const int radius = kMaxRadius;
""",
"""	const float sigma = float(kMaxRadius) * 0.6 * smoothstep(0.05, 0.45, roughness);
	if (sigma < 0.15)
		return;
	const int radius = kMaxRadius;
	const int stride = max(int(u_Reflection.Probe.z + 0.5), 1);
"""),
("""			const ivec2 at = clamp(texel + ivec2(x, y), ivec2(0), size - 1);
			const vec4 f = texelFetch(u_Fresh, at, 0);
			if (f.a < 0.0)
				continue;
			vec3 Pn, Nn;
			float rn;
			if (!SurfaceAt(at, size, Pn, Nn, rn))
				continue;""",
"""			const ivec2 at = clamp(texel + ivec2(x, y) * stride, ivec2(0), size - 1);
			const vec4 f = texelFetch(u_Fresh, at, 0);
			if (f.a < 0.0)
				continue;
			vec3 Pn, Nn;
			float rn;
			if (!SurfaceAt(at, size, Pn, Nn, rn))
				continue;""")])

patch('RageV/src/RageV/Renderer/Renderer3D.h', [(
"""		static void ResolveReflections(const RHI::Ref<RHI::RHITexture>& fresh,
									   const RHI::Ref<RHI::RHITexture>& depth,
									   const RHI::Ref<RHI::RHITexture>& surface);""",
"""		static void ResolveReflections(const RHI::Ref<RHI::RHITexture>& fresh,
									   const RHI::Ref<RHI::RHITexture>& depth,
									   const RHI::Ref<RHI::RHITexture>& surface,
									   int stride);""")])

patch('RageV/src/RageV/Renderer/Renderer3D.cpp', [(
"""	void Renderer3D::ResolveReflections(const RHI::Ref<RHITexture>& fresh,
										const RHI::Ref<RHITexture>& depth,
										const RHI::Ref<RHITexture>& surface)
	{""",
"""	void Renderer3D::ResolveReflections(const RHI::Ref<RHITexture>& fresh,
										const RHI::Ref<RHITexture>& depth,
										const RHI::Ref<RHITexture>& surface,
										int stride)
	{"""),
("""		ReflectionPushConstants push;
		push.InverseViewProjection = Math::Inverse(s_Data->Scene.ViewProjection);
		push.History.w = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;

		cmd->BindPipeline(s_Data->ReflectionResolvePipeline);""",
"""		ReflectionPushConstants push;
		push.InverseViewProjection = Math::Inverse(s_Data->Scene.ViewProjection);
		push.History.w = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
		push.Probe.z = (float)Math::Max(stride, 1);

		cmd->BindPipeline(s_Data->ReflectionResolvePipeline);"""),
("""		// Standing still a reflection remembers 24 frames (~100 rays behind a
		// pixel at four a frame); moving a pixel a frame it halves, to a floor
		// of four on a rough surface and one on a mirror, which has no grain
		// to average. Preset columns when they have been measured.
		push.History.y = 24.0f;""",
"""		// Standing still a reflection remembers 64 frames; moving a pixel a
		// frame it halves, to a floor of four on a rough surface and one on a
		// mirror, which has no grain to average. 64 rather than 24 (2026-09-06):
		// the picture is a running average of rare bright tube hits, and a
		// window that short rearranges the blotches visibly as it slides --
		// the floor drifted 8.7 levels over 16 parked frames. The drift per
		// frame falls with the window's length. Preset columns when measured.
		push.History.y = 64.0f;""")])

patch('RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [(
"""				[traced, sceneHDR, normalIndex](RGPassContext& context)
				{
					Renderer3D::ResolveReflections(context.Color(traced),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex));
				});
""",
"""				[traced, sceneHDR, normalIndex](RGPassContext& context)
				{
					Renderer3D::ResolveReflections(context.Color(traced),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex), 1);
				});
			// And once more at twice the stride, reading the first's output:
			// the dilated second pass that reaches thirteen texels for the
			// price of seven, for the rough surfaces whose rare bright hits
			// need the widest average they can get.
			RGTargetDesc resolve2Desc = traceDesc;
			resolve2Desc.Name = "ReflectionResolve2";
			const RGResource resolved2 = graph.CreateTarget(resolve2Desc);
			graph.AddPass("ReflectionResolve2",
				[&](RGPassBuilder& builder)
				{
					builder.Write(resolved2);
					builder.Sample(resolved);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[resolved, sceneHDR, normalIndex](RGPassContext& context)
				{
					Renderer3D::ResolveReflections(context.Color(resolved),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex), 2);
				});
"""),
("""				[&](RGPassBuilder& builder)
				{
					builder.Write(currentReflections);
					builder.Sample(resolved);
					builder.Sample(sceneHDR);
					if (reflectionHistory)
						builder.Sample(previousReflections);
					builder.DisableDepth();
				},
				[resolved, sceneHDR, normalIndex, previousReflections, reflectionHistory,
				 motion = &desc.Reflections->Motion()](RGPassContext& context)
				{
					Renderer3D::AccumulateReflections(
						context.Color(resolved), context.Depth(sceneHDR),""",
"""				[&](RGPassBuilder& builder)
				{
					builder.Write(currentReflections);
					builder.Sample(resolved2);
					builder.Sample(sceneHDR);
					if (reflectionHistory)
						builder.Sample(previousReflections);
					builder.DisableDepth();
				},
				[resolved2, sceneHDR, normalIndex, previousReflections, reflectionHistory,
				 motion = &desc.Reflections->Motion()](RGPassContext& context)
				{
					Renderer3D::AccumulateReflections(
						context.Color(resolved2), context.Depth(sceneHDR),"""),
("""							  : view == EngineConfig::DebugViewMode::Reflection ? 24.0f""",
"""							  : view == EngineConfig::DebugViewMode::Reflection ? 64.0f""")])
