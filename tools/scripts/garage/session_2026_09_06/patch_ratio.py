"""The ratio-estimator resolve (Stachowiak 2015, UE/Frostbite SSSR): the trace
fires one ray per texel and writes its direction and pdf beside the
radiance; the resolve reuses the neighbours' HIT POINTS re-aimed from this
texel, weighted by this texel's own lobe over the neighbour's pdf. No
parallax blur, no over-reach: a band stays as wide as the lobe, a puddle
texel keeps its narrow lobe among rough neighbours. Replaces the three
a-trous footprint passes (patch_footprint.py) with one pass."""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')

def read(p): return open(p, 'rb').read().decode('utf-8')
def write(p, s): open(p, 'wb').write(s.encode('utf-8')); print('ok', p)
def patch(p, pairs):
    s = read(p); nl = '\r\n' if '\r\n' in s else '\n'
    for old, new in pairs:
        n = new.replace('\n', nl)
        if isinstance(old, tuple):   # (start marker, end marker): replace the span through the end marker's line
            a = s.index(old[0].replace('\n', nl)); b = s.index(old[1].replace('\n', nl), a); b = s.index(nl, b) + len(nl)
            s = s[:a] + n + s[b:]; continue
        o = old.replace('\n', nl)
        assert s.count(o) == 1, (p, s.count(o), old[:70]); s = s.replace(o, n)
    write(p, s)

assert 'vec2 OctEncode(' in read('RageVEditor/assets/shaders/include/octahedral.glsl'), 'no OctEncode'

# ---- trace: one ray, and its direction + pdf in a second attachment -------
patch('RageVEditor/assets/shaders/reflection_trace.rvshader', [(
"""layout(location = 0) out vec4 o_Reflection;
""",
"""layout(location = 0) out vec4 o_Reflection;
// The ray's direction (octahedral) and the pdf it was drawn with, for the
// resolve's ratio estimator: a neighbour's hit point re-aimed from another
// texel is a sample of that texel's lobe, weighted by lobe / this pdf.
layout(location = 1) out vec4 o_Hit;
"""),
("""	o_Reflection = vec4(0.0, 0.0, 0.0, -1.0);
""",
"""	o_Reflection = vec4(0.0, 0.0, 0.0, -1.0);
	o_Hit = vec4(0.0);
"""),
(("""	vec3 sum = vec3(0.0);
""", """	o_Reflection = vec4(sum / float(count), travelled / float(count));"""),
"""	// One ray a texel (2026-09-06): the resolve gathers the neighbours' rays
	// re-aimed through this texel's lobe, which is where the lobe's samples
	// come from now -- several rays averaged here would have no single
	// direction to hand it. The count above is kept for the preset column
	// until it is remapped to the resolve's tap count.
	count = 1;
	const vec3 direction = GlossyReflection(N, V, roughness, 0, 1);
	const TracedSurface hit = TraceSurface(P, N, direction, 1.0e4);
	vec3 radiance = hit.Missed
				  ? hit.Sky
				  : ShadeTraced(hit, ProbeIrradiance(hit.Normal, u_Lamps.Trace.x));
	const float travelled = hit.Missed ? 1.0e4 : length(hit.Position - P);
	o_Reflection = vec4(min(radiance, vec3(64.0)), travelled);
	// GGX sampled by its distribution of half vectors: pdf(L) = D(H) NoH / (4 VoH).
	const vec3 H = normalize(direction + V);
	const float pdf = DistributionGGX(N, H, roughness) * max(dot(N, H), 1.0e-4)
					/ (4.0 * max(dot(V, H), 1.0e-4));
	o_Hit = vec4(OctEncode(direction), min(pdf, 6.0e4), 0.0);
""")])

# ---- resolve: the ratio estimator, from kMaxRadius to the end ---------------
p = 'RageVEditor/assets/shaders/reflection_resolve.rvshader'
s = read(p); nl = '\r\n' if '\r\n' in s else '\n'
i = s.index('const int kMaxRadius = 3;')
head = s[:i]
old_binding = 'layout(set = 3, binding = 2) uniform sampler2D u_Surface;' + nl
assert head.count(old_binding) == 1
head = head.replace(old_binding, old_binding +
"""// The trace's second attachment: ray direction (octahedral) and pdf.
layout(set = 3, binding = 3) uniform sampler2D u_Hit;
""".replace('\n', nl))
body = """// The resolve is a ratio estimator over the neighbours' hit points
// (Stachowiak, "Stochastic Screen-Space Reflections", SIGGRAPH 2015; the
// same shape in Frostbite's and Unreal's SSR). A neighbour's ray found a
// point X in the scene; seen from THIS texel, X lies in some direction
// inside or outside this texel's lobe, and its radiance is a sample of the
// lobe's integral with weight lobe(here, towards X) / pdf(the neighbour's
// draw). Averaging neighbours' radiances outright (the footprint kernel
// tried on 2026-09-06) estimates the picture seen from the neighbours'
// points -- a parallax blur on top of the lobe's own, measured at a
// doubled band edge -- and brightens dark floor through the tonemap. The
// re-aim removes both: a band is as wide as the lobe and no wider, and a
// puddle texel with a narrow lobe among rough neighbours keeps only the
// hits that fall inside it.
//
// Taps: a Vogel spiral scaled to the lobe's footprint on the surface,
// per axis in texels (a floor is foreshortened), rotated per texel and
// frame so the pattern's own noise averages away in the accumulator.
const int kTaps = 24;
// The lobe's reach as a tangent per unit of GGX alpha: three quarters of
// the energy sits inside tan = 2.8 alpha. The weights handle the rest.
const float kLobeWidth = 2.8;
const int kMaxReach = 24;
// A neighbour's tail draw (tiny pdf) landing in this texel's core would
// carry an unbounded weight -- the ratio estimator's firefly. Capped
// relative to the centre's own weight, which is 4 VoH / NoH ~ 4.
const float kMaxWeight = 32.0;

bool SurfaceAt(ivec2 texel, ivec2 size, out vec3 P, out vec3 N, out float roughness)
{
	const vec4 surface = texelFetch(u_Surface, texel, 0);
	const float depth = texelFetch(u_Depth, texel, 0).r;
	if (surface.b <= 0.0 || depth >= 1.0)
		return false;
	const vec2 uv = (vec2(texel) + 0.5) / vec2(size);
	const float row = u_Reflection.History.w > 0.5 ? 1.0 - uv.y : uv.y;
	const vec4 clip = u_Reflection.InverseViewProjection
					* vec4(uv.x * 2.0 - 1.0, row * 2.0 - 1.0, depth, 1.0);
	P = clip.xyz / clip.w;
	N = OctDecode(surface.rg);
	roughness = clamp(surface.b, 0.0, 1.0);
	return true;
}

float Rotation(ivec2 texel)
{
	// Interleaved gradient noise, advanced by the frame.
	const vec2 f = vec2(texel) + 5.588238 * float(int(u_Scene.GlobalIllumination.y) & 63);
	return 6.2831853 * fract(52.9829189 * fract(0.06711056 * f.x + 0.00583715 * f.y));
}

void main()
{
	const ivec2 texel = ivec2(gl_FragCoord.xy);
	const ivec2 size = textureSize(u_Fresh, 0);
	const vec4 fresh = texelFetch(u_Fresh, texel, 0);
	o_Resolved = fresh;
	if (fresh.a < 0.0)
		return;

	vec3 P, N;
	float roughness;
	if (!SurfaceAt(texel, size, P, N, roughness))
		return;
	const float alpha = roughness * roughness;
	if (alpha < 1.0e-4)
		return;   // a mirror: its one ray is the whole lobe
	const vec3 V = normalize(u_Scene.CameraPosition.xyz - P);
	const float NoV = max(dot(N, V), 1.0e-3);

	// The footprint in texels per axis: a lobe of half-width tan reaches a
	// hit at distance H over a disc of radius H * tan on the surface; a
	// texel's size in metres is measured from its neighbours, the smaller
	// of each side so an edge's far neighbour cannot inflate it.
	const float eyeDistance = length(P - u_Scene.CameraPosition.xyz);
	const float footprint = max(fresh.a, 0.0) * kLobeWidth * alpha;
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
	const vec2 reach = min(footprint / texelSize, vec2(float(kMaxReach)));
	if (max(reach.x, reach.y) < 0.5)
		return;
	const float planeTolerance = 0.02 + 0.01 * eyeDistance;
	const float rotation = Rotation(texel);

	vec3 sum = vec3(0.0);
	float distanceSum = 0.0;
	float weightSum = 0.0;
	for (int i = -1; i < kTaps; ++i)
	{
		ivec2 at = texel;
		if (i >= 0)
		{
			const float r = sqrt((float(i) + 0.5) / float(kTaps));
			const float theta = float(i) * 2.39996323 + rotation;
			at = clamp(texel + ivec2(round(reach * r * vec2(cos(theta), sin(theta)))), ivec2(0), size - 1);
			if (at == texel)
				continue;
		}
		const vec4 f = texelFetch(u_Fresh, at, 0);
		if (f.a < 0.0)
			continue;
		vec3 Pn, Nn;
		float rn;
		if (!SurfaceAt(at, size, Pn, Nn, rn))
			continue;
		if (dot(N, Nn) < 0.9 || abs(dot(N, Pn - P)) > planeTolerance)
			continue;
		const vec4 h = texelFetch(u_Hit, at, 0);
		const vec3 X = Pn + OctDecode(h.xy) * f.a;
		const vec3 toX = X - P;
		const float distanceHere = max(length(toX), 1.0e-3);
		const vec3 L = toX / distanceHere;
		if (dot(N, L) <= 0.0)
			continue;
		const vec3 H = normalize(L + V);
		// BRDF cos / pdf with the Fresnel and the Smith terms dropped: they
		// are the same for every tap to within the ratio's normalisation.
		// The pdf's own NoH / 4VoH is kept in the neighbour's frame, as the
		// trace wrote it; the solid-angle change of the re-aim (a distance
		// ratio squared) is ignored, as the shipped resolves ignore it.
		const float w = min(DistributionGGX(N, H, roughness) / max(h.z, 1.0e-4), kMaxWeight);
		sum += f.rgb * w;
		distanceSum += distanceHere * w;
		weightSum += w;
	}
	if (weightSum > 0.0)
		o_Resolved = vec4(sum / weightSum, distanceSum / weightSum);
}
""".replace('\n', nl)
write(p, head + body)

# ---- frame graph: a second trace attachment, one resolve pass --------------
patch('RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [(
"""			traceDesc.Name = "ReflectionTrace";
			traceDesc.Color = Format::R16G16B16A16_SFLOAT;
			traceDesc.Depth = Format::Undefined;
""",
"""			traceDesc.Name = "ReflectionTrace";
			traceDesc.Color = Format::R16G16B16A16_SFLOAT;
			// The ray's direction and pdf, for the resolve's ratio estimator.
			traceDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT };
			traceDesc.Depth = Format::Undefined;
"""),
("""			RGTargetDesc resolveDesc = traceDesc;
			resolveDesc.Name = "ReflectionResolve";
""",
"""			RGTargetDesc resolveDesc = traceDesc;
			resolveDesc.Name = "ReflectionResolve";
			resolveDesc.ExtraColors.clear();
"""),
("""				[traced, sceneHDR, normalIndex](RGPassContext& context)
				{
					Renderer3D::ResolveReflections(context.Color(traced),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex), 1);
				});
""",
"""				[traced, sceneHDR, normalIndex](RGPassContext& context)
				{
					Renderer3D::ResolveReflections(context.Color(traced),
												   context.Color(traced, 1),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex));
				});
""")])
# Drop the second and third passes (from their comment to the accumulate pass).
s = read('RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'); nl = '\r\n' if '\r\n' in s else '\n'
a = s.index('\t\t\t// And once more at twice the stride, reading the first\'s output:')
b = s.index('\t\t\tgraph.AddPass("ReflectionAccumulate",')
assert 0 < a < b and 'ReflectionResolve3' in s[a:b]
s = s[:a] + s[b:]
for old, new in (('builder.Sample(resolved3);', 'builder.Sample(resolved);'),
                 ('[resolved3, sceneHDR, normalIndex, previousReflections, reflectionHistory,', '[resolved, sceneHDR, normalIndex, previousReflections, reflectionHistory,'),
                 ('context.Color(resolved3), context.Depth(sceneHDR),', 'context.Color(resolved), context.Depth(sceneHDR),')):
    assert s.count(old) == 1, old; s = s.replace(old, new)
assert 'resolved2' not in s and 'resolved3' not in s
write('RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', s)

# ---- renderer: trace pipeline with two attachments, resolve binds the hit --
patch('RageV/src/RageV/Renderer/Renderer3D.cpp', [(
"""			if (pass == 2)
			{
				for (int extra = 0; extra < 2; ++extra)
				{
					reflection.ColorFormats.push_back(Format::R16G16B16A16_SFLOAT);
					reflection.BlendPerAttachment.push_back(BlendPreset::Opaque);
				}
			}
""",
"""			// The trace writes its ray's direction and pdf beside the radiance;
			// the accumulator its surface and moments beside the picture.
			const int extras = pass == 0 ? 1 : pass == 2 ? 2 : 0;
			for (int extra = 0; extra < extras; ++extra)
			{
				reflection.ColorFormats.push_back(Format::R16G16B16A16_SFLOAT);
				reflection.BlendPerAttachment.push_back(BlendPreset::Opaque);
			}
"""),
("""	void Renderer3D::ResolveReflections(const RHI::Ref<RHITexture>& fresh,
										const RHI::Ref<RHITexture>& depth,
										const RHI::Ref<RHITexture>& surface,
										int stride)
	{""",
"""	void Renderer3D::ResolveReflections(const RHI::Ref<RHITexture>& fresh,
										const RHI::Ref<RHITexture>& hit,
										const RHI::Ref<RHITexture>& depth,
										const RHI::Ref<RHITexture>& surface)
	{"""),
("""		if (!cmd || !slot.LampSet || !fresh || !depth || !surface)
			return;

		if (!slot.ReflectionResolveInputs)""",
"""		if (!cmd || !slot.LampSet || !fresh || !hit || !depth || !surface)
			return;

		if (!slot.ReflectionResolveInputs)"""),
("""		slot.ReflectionResolveInputs->SetTexture(2, surface, s_Data->PointSampler);
		slot.ReflectionResolveInputs->Commit();
""",
"""		slot.ReflectionResolveInputs->SetTexture(2, surface, s_Data->PointSampler);
		slot.ReflectionResolveInputs->SetTexture(3, hit, s_Data->PointSampler);
		slot.ReflectionResolveInputs->Commit();
"""),
("""		push.History.w = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
		push.Probe.z = (float)Math::Max(stride, 1);

		cmd->BindPipeline(s_Data->ReflectionResolvePipeline);""",
"""		push.History.w = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;

		cmd->BindPipeline(s_Data->ReflectionResolvePipeline);""")])
patch('RageV/src/RageV/Renderer/Renderer3D.h', [(
"""		static void ResolveReflections(const RHI::Ref<RHI::RHITexture>& fresh,
									   const RHI::Ref<RHI::RHITexture>& depth,
									   const RHI::Ref<RHI::RHITexture>& surface,
									   int stride);""",
"""		static void ResolveReflections(const RHI::Ref<RHI::RHITexture>& fresh,
									   const RHI::Ref<RHI::RHITexture>& hit,
									   const RHI::Ref<RHI::RHITexture>& depth,
									   const RHI::Ref<RHI::RHITexture>& surface);""")])
print('all patched')
