"""WR-16 R12: short memory under motion, wide blur while the history is
young. (1) The accumulator's memory is capped so the average never spans
more than kSmearTexels of the picture's travel: memory <= 4 / moved, floor
6 frames. (2) The blur after the accumulator gets a radius from the
history length -- 16 texels at one frame, nothing at 32 -- bounded by the
lobe's footprint per axis and the gloss window, reached with an a-trous
ladder of three passes (strides 1, 2, 4). The history itself stays sharp."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def patch(p, pairs):
    s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
    for old, new in pairs:
        n = new.replace('\n', nl)
        if isinstance(old, tuple):
            a = s.index(old[0].replace('\n', nl)); b = s.index(old[1].replace('\n', nl), a); b = s.index(nl, b) + len(nl)
            s = s[:a] + n + s[b:]; continue
        o = old.replace('\n', nl)
        assert s.count(o) == 1, (p, s.count(o), old[:60]); s = s.replace(o, n)
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)

# ---- (1) the accumulator's cap -------------------------------------------
patch('RageVEditor/assets/shaders/reflection_accumulate.rvshader', [(
"""const float kTemporalSigma = 2.0;
""",
"""const float kTemporalSigma = 2.0;
// **The smear cap (WR-16 R12, 2026-09-06).** A running average of a moving
// picture is motion blur: with the memory only halved per texel of travel
// a frame, a floor texel sliding one texel a frame still averaged 32
// positions -- the smear the owner saw at every camera move. The memory
// may never span more than kSmearTexels of travel (memory <= kSmearTexels
// / moved), down to kMovingMemory frames; the blur after the accumulator
// carries the noise of a short history. Parked, `moved` reads exactly zero
// (measured through the picture view, 2026-09-06), so the cap never bites
// on a still camera; a slow hand dolly moves ~0.3 texel a frame and gets
// ~13 frames, a fast one 1.5 texels and gets the floor.
const float kSmearTexels = 4.0;
const float kMovingMemory = 6.0;
"""),
("""			const float memory = max(max(u_Reflection.History.y, 1.0) / (1.0 + moved / slack),
									 fewest);
""",
"""			float memory = max(u_Reflection.History.y, 1.0) / (1.0 + moved / slack);
			memory = min(memory, max(kSmearTexels / max(moved, 1.0e-3), kMovingMemory));
			memory = max(memory, fewest);
""")])

# ---- (2) the blur: radius from the history length, footprint-bounded, a-trous
p = 'RageVEditor/assets/shaders/reflection_blur.rvshader'
s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
head = s[:s.index('const int kMaxRadius = 3;')]
old_binding = 'layout(set = 3, binding = 2) uniform sampler2D u_Surface;' + nl
assert head.count(old_binding) == 1
head = head.replace(old_binding, old_binding + """// The reflection history's second attachment: its .a is the image
// distance the accumulator placed the picture at (-1 none), the hit
// distance that bounds the blur below.
layout(set = 3, binding = 3) uniform sampler2D u_ImageDistance;
""".replace('\n', nl))
body = """// **WR-16 R12 (2026-09-06): the blur is the short history's partner.** Its
// radius comes from how few frames stand behind the texel -- kYoungRadius
// texels at one frame, nothing at kBlurFrames -- so a picture that just
// started (after a move, or while moving under the smear cap) is soft but
// stable instead of grainy, and sharpens as the frames arrive. The radius
// is bounded per axis by the lobe's footprint on the surface (a chrome
// pole is never blurred past its own lobe; a foreshortened floor blurs
// less across than along) and by the gloss window; three passes at
// strides 1, 2, 4 reach it with seven taps each. The accumulator's own
// history is never blurred: this reads it and writes the composite's
// input, so there is no feedback and nothing here can smear over time.
const int kMaxRadius = 3;
const float kYoungRadius = 16.0;
const float kBlurFrames = 32.0;
const float kLobeWidth = 2.8;
const float kPassShare = 0.57735;   // three passes share the variance

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

void main()
{
	const ivec2 texel = ivec2(gl_FragCoord.xy);
	const ivec2 size = textureSize(u_Accumulated, 0);
	const vec4 centre = texelFetch(u_Accumulated, texel, 0);
	o_Blurred = centre;
	if (centre.a <= 0.0)
		return;

	vec3 P, N;
	float roughness;
	if (!SurfaceAt(texel, size, P, N, roughness))
		return;

	const float young = 1.0 - clamp((centre.a - 1.0) / (kBlurFrames - 1.0), 0.0, 1.0);
	const float gloss = smoothstep(0.02, 0.35, roughness);
	float radius = kYoungRadius * young * young * gloss;
	if (radius < 0.5)
		return;

	// The lobe's footprint per axis, in texels, as the resolve measures it.
	const float eyeDistance = length(P - u_Scene.CameraPosition.xyz);
	const float imageDistance = max(texelFetch(u_ImageDistance, texel, 0).a, 0.0);
	const float footprint = imageDistance * kLobeWidth * roughness * roughness;
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
	const vec2 reach = min(vec2(radius), footprint / texelSize);
	// Radius is two sigmas; this pass carries its share at its stride, and a
	// seven-tap window supports a sigma of about one and a half.
	const int stride = max(int(u_Reflection.Probe.z + 0.5), 1);
	const vec2 sigma = min(0.5 * reach * kPassShare / float(stride), vec2(1.5));
	if (max(sigma.x, sigma.y) < 0.15)
		return;
	const vec2 invTwoSigmaSq = 0.5 / max(sigma * sigma, vec2(0.0001));
	const float planeTolerance = 0.02 + 0.01 * eyeDistance;

	vec3 sum = vec3(0.0);
	float weightSum = 0.0;
	for (int y = -kMaxRadius; y <= kMaxRadius; ++y)
	{
		for (int x = -kMaxRadius; x <= kMaxRadius; ++x)
		{
			const ivec2 at = clamp(texel + ivec2(x, y) * stride, ivec2(0), size - 1);
			const vec4 f = texelFetch(u_Accumulated, at, 0);
			if (f.a <= 0.0)
				continue;
			vec3 Pn, Nn;
			float rn;
			if (!SurfaceAt(at, size, Pn, Nn, rn))
				continue;
			float w = exp(-(float(x * x) * invTwoSigmaSq.x + float(y * y) * invTwoSigmaSq.y));
			w *= pow(max(dot(N, Nn), 0.0), 8.0);
			w *= exp(-abs(dot(N, Pn - P)) / planeTolerance);
			w *= abs(rn - roughness) < 0.2 ? 1.0 : 0.0;
			sum += f.rgb * w;
			weightSum += w;
		}
	}
	if (weightSum > 0.0)
		o_Blurred = vec4(sum / weightSum, centre.a);
}
""".replace('\n', nl)
open(p, 'wb').write((head + body).encode('utf-8')); print('ok', p)

# ---- C++: three passes, the image-distance binding, the stride ------------
patch('RageV/src/RageV/Renderer/Renderer3D.h', [(
"""		static void BlurReflections(const RHI::Ref<RHI::RHITexture>& accumulated,
									const RHI::Ref<RHI::RHITexture>& depth,
									const RHI::Ref<RHI::RHITexture>& surface);""",
"""		static void BlurReflections(const RHI::Ref<RHI::RHITexture>& accumulated,
									const RHI::Ref<RHI::RHITexture>& depth,
									const RHI::Ref<RHI::RHITexture>& surface,
									const RHI::Ref<RHI::RHITexture>& imageDistance,
									int stride);""")])

patch('RageV/src/RageV/Renderer/Renderer3D.cpp', [(
"""	void Renderer3D::BlurReflections(const RHI::Ref<RHITexture>& accumulated,
									 const RHI::Ref<RHITexture>& depth,
									 const RHI::Ref<RHITexture>& surface)
	{""",
"""	void Renderer3D::BlurReflections(const RHI::Ref<RHITexture>& accumulated,
									 const RHI::Ref<RHITexture>& depth,
									 const RHI::Ref<RHITexture>& surface,
									 const RHI::Ref<RHITexture>& imageDistance,
									 int stride)
	{"""),
("""		if (!cmd || !slot.LampSet || !accumulated || !depth || !surface)
			return;

		if (!slot.ReflectionBlurInputs)""",
"""		if (!cmd || !slot.LampSet || !accumulated || !depth || !surface || !imageDistance)
			return;

		if (!slot.ReflectionBlurInputs)"""),
("""		slot.ReflectionBlurInputs->SetTexture(2, surface, s_Data->PointSampler);
""",
"""		slot.ReflectionBlurInputs->SetTexture(2, surface, s_Data->PointSampler);
		slot.ReflectionBlurInputs->SetTexture(3, imageDistance, s_Data->PointSampler);
"""),
("""		push.History.y = 16.0f;
		push.History.w = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
""",
"""		// The radius law lives in the shader (kYoungRadius, kBlurFrames); the
		// stride is the a-trous ladder's step, 1, 2, 4 over three passes.
		push.History.y = 32.0f;
		push.History.w = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
		push.Probe.z = (float)Math::Max(stride, 1);
""")])

patch('RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [(
"""				RGTargetDesc blurDesc = traceDesc;
				blurDesc.Name = "ReflectionBlurred";
				const RGResource blurred = graph.CreateTarget(blurDesc);
				graph.AddPass("ReflectionBlur",
					[&](RGPassBuilder& builder)
					{
						builder.Write(blurred);
						builder.Sample(currentReflections);
						builder.Sample(sceneHDR);
						builder.DisableDepth();
					},
					[currentReflections, sceneHDR, normalIndex](RGPassContext& context)
					{
						Renderer3D::BlurReflections(context.Color(currentReflections),
													context.Depth(sceneHDR),
													context.Color(sceneHDR, normalIndex));
					});
""",
"""				// Three passes at strides 1, 2, 4 (WR-16 R12): the young-history
				// blur reaches sixteen texels for three times seven taps. Each
				// reads the previous pass's output; the first reads the history
				// itself, which is never written here.
				RGTargetDesc blurDesc = traceDesc;
				blurDesc.ExtraColors.clear();
				RGResource blurred = kRGInvalid;
				RGResource blurInput = currentReflections;
				const char* const kBlurPassNames[3] = { "ReflectionBlur", "ReflectionBlur2", "ReflectionBlur4" };
				for (int pass = 0; pass < 3; ++pass)
				{
					blurDesc.Name = pass == 0 ? "ReflectionBlurred" : pass == 1 ? "ReflectionBlurred2" : "ReflectionBlurred4";
					const RGResource output = graph.CreateTarget(blurDesc);
					const RGResource input = blurInput;
					const int stride = 1 << pass;
					graph.AddPass(kBlurPassNames[pass],
						[&](RGPassBuilder& builder)
						{
							builder.Write(output);
							builder.Sample(input);
							if (input != currentReflections)
								builder.Sample(currentReflections);
							builder.Sample(sceneHDR);
							builder.DisableDepth();
						},
						[input, currentReflections, sceneHDR, normalIndex, stride](RGPassContext& context)
						{
							Renderer3D::BlurReflections(context.Color(input),
														context.Depth(sceneHDR),
														context.Color(sceneHDR, normalIndex),
														context.Color(currentReflections, 1),
														stride);
						});
					blurInput = output;
					blurred = output;
				}
""")])
print('R12 patched')
