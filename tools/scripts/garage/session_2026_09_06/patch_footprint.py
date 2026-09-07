import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def patch(p, pairs):
    s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
    for old, new in pairs:
        o = old.replace('\n', nl); n = new.replace('\n', nl)
        assert s.count(o) == 1, (p, s.count(o), old[:70]); s = s.replace(o, n)
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)

# The resolve's kernel follows the lobe's footprint instead of roughness alone.
patch('RageVEditor/assets/shaders/reflection_resolve.rvshader', [(
"""const int kMaxRadius = 3;
""",
"""const int kMaxRadius = 3;
// The lobe's half-width as a tangent, per unit of GGX alpha (roughness
// squared). GGX keeps three quarters of its energy inside tan = 2.8 alpha
// and half inside about 1.5; the kernel is meant to gather the lobe's own
// blur and no more, so it sits at the half-energy width. Measured against
// the unfiltered estimator's converged picture (2026-09-06): see HANDOFF.
const float kLobeWidth = 1.5;
// Three passes at strides 1, 2, 4 (the a-trous ladder) share the footprint:
// each carries footprint / sqrt(3) so their variances add to the whole.
const float kPassShare = 0.57735;
"""),
("""	const float sigma = float(kMaxRadius) * 0.6 * smoothstep(0.05, 0.45, roughness);
	if (sigma < 0.15)
		return;
	const int radius = kMaxRadius;
	const int stride = max(int(u_Reflection.Probe.z + 0.5), 1);

	const float eyeDistance = length(P - u_Scene.CameraPosition.xyz);
	const float planeTolerance = 0.02 + 0.01 * eyeDistance;
""",
"""	const int radius = kMaxRadius;
	const int stride = max(int(u_Reflection.Probe.z + 0.5), 1);
	const float eyeDistance = length(P - u_Scene.CameraPosition.xyz);
	const float planeTolerance = 0.02 + 0.01 * eyeDistance;

	// The footprint. A lobe of half-width tan = kLobeWidth * alpha reaches a
	// hit at distance H over a disc of radius H * tan; every surface texel
	// inside that disc sees the same hit inside its own lobe, so their rays
	// are one estimate and may be averaged. The disc is metres on the
	// surface; a texel's size in metres differs by axis (a floor is
	// foreshortened), so the kernel is an ellipse: measured from the
	// neighbours' positions, the smaller of each side so an edge's far
	// neighbour cannot inflate it. Mirrors get no radius and stay sharp; a
	// near hit (the car in the floor) a small one; the tubes on the
	// ceiling, seen in the floor, the wide one their reflection really has.
	const float alpha = roughness * roughness;
	const float hitDistance = max(fresh.a, 0.0);
	const float footprint = hitDistance * kLobeWidth * alpha;
	vec2 texelSize = vec2(eyeDistance * 0.0015);
	{
		vec3 Pn, Nn;
		float rn;
		float best = 1e9;
		if (SurfaceAt(clamp(texel + ivec2(1, 0), ivec2(0), size - 1), size, Pn, Nn, rn)) best = min(best, length(Pn - P));
		if (SurfaceAt(clamp(texel - ivec2(1, 0), ivec2(0), size - 1), size, Pn, Nn, rn)) best = min(best, length(Pn - P));
		if (best < 1e8) texelSize.x = max(best, eyeDistance * 0.0002);
		best = 1e9;
		if (SurfaceAt(clamp(texel + ivec2(0, 1), ivec2(0), size - 1), size, Pn, Nn, rn)) best = min(best, length(Pn - P));
		if (SurfaceAt(clamp(texel - ivec2(0, 1), ivec2(0), size - 1), size, Pn, Nn, rn)) best = min(best, length(Pn - P));
		if (best < 1e8) texelSize.y = max(best, eyeDistance * 0.0002);
	}
	// Radius is two sigmas; this pass carries its share at its stride, and a
	// seven-tap window supports a sigma of about one and a half.
	const vec2 sigma = min(0.5 * footprint / texelSize * kPassShare / float(stride), vec2(1.5));
	if (max(sigma.x, sigma.y) < 0.15)
		return;
	const vec2 invTwoSigmaSq = 0.5 / max(sigma * sigma, vec2(0.0001));
	// Taps whose ray hit somewhere else -- the car's reflected edge against
	// the ceiling behind it -- are a different picture, not this one's blur.
	const float hitTolerance = max(0.3 * hitDistance, 0.05);
"""),
("""			float w = exp(-float(x * x + y * y) / (2.0 * sigma * sigma));
			w *= pow(max(dot(N, Nn), 0.0), 8.0);
			w *= exp(-abs(dot(N, Pn - P)) / planeTolerance);
			w *= abs(rn - roughness) < 0.2 ? 1.0 : 0.0;
""",
"""			float w = exp(-(float(x * x) * invTwoSigmaSq.x + float(y * y) * invTwoSigmaSq.y));
			w *= pow(max(dot(N, Nn), 0.0), 8.0);
			w *= exp(-abs(dot(N, Pn - P)) / planeTolerance);
			w *= abs(rn - roughness) < 0.2 ? 1.0 : 0.0;
			w *= exp(-abs(f.a - hitDistance) / hitTolerance);
""")])

# A third dilated pass at stride 4, and the accumulator reads it.
patch('RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [(
"""					Renderer3D::ResolveReflections(context.Color(resolved),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex), 2);
				});
""",
"""					Renderer3D::ResolveReflections(context.Color(resolved),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex), 2);
				});
			// And a third at stride 4: 45 texels of reach for three times seven
			// taps, which is what the lobe's footprint on the floor needs
			// (the tubes' reflection there is thirty texels wide at 1600x900).
			RGTargetDesc resolve3Desc = traceDesc;
			resolve3Desc.Name = "ReflectionResolve3";
			const RGResource resolved3 = graph.CreateTarget(resolve3Desc);
			graph.AddPass("ReflectionResolve3",
				[&](RGPassBuilder& builder)
				{
					builder.Write(resolved3);
					builder.Sample(resolved2);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[resolved2, sceneHDR, normalIndex](RGPassContext& context)
				{
					Renderer3D::ResolveReflections(context.Color(resolved2),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex), 4);
				});
"""),
("""					builder.Write(currentReflections);
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
						context.Color(resolved2), context.Depth(sceneHDR),""",
"""					builder.Write(currentReflections);
					builder.Sample(resolved3);
					builder.Sample(sceneHDR);
					if (reflectionHistory)
						builder.Sample(previousReflections);
					builder.DisableDepth();
				},
				[resolved3, sceneHDR, normalIndex, previousReflections, reflectionHistory,
				 motion = &desc.Reflections->Motion()](RGPassContext& context)
				{
					Renderer3D::AccumulateReflections(
						context.Color(resolved3), context.Depth(sceneHDR),""")])
