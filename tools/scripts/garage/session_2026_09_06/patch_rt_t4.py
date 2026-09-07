"""RT-first T4: the reconstruction contract as code. The reflection
accumulator and its young-history blur become the engine's signal
denoiser: one accumulate shader and one blur shader compiled in two kinds
(specular = the reflections as they are; diffuse = surface reprojection
only, a fixed radius bound), driven by a SignalParams block instead of
constants, with per-signal resource sets, a frame-graph helper that adds
the accumulate + three blur passes for any signal, and a refusal-reason
channel for the debug view. Reflections are the first client and must
stay pixel-identical."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def load(p):
    s = open(p, 'rb').read().decode('utf-8'); return s, ('\r\n' if '\r\n' in s else '\n')
def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)
def rep(s, nl, old, new, count=1):
    o = old.replace('\n', nl); n = new.replace('\n', nl)
    assert s.count(o) == count, (s.count(o), old[:70]); return s.replace(o, n)
def span(s, nl, start, end_line, new):
    """Replace from `start` through the line containing `end_line` (inclusive)."""
    a = s.index(start.replace('\n', nl)); b = s.index(end_line.replace('\n', nl), a); b = s.index(nl, b) + len(nl)
    return s[:a] + new.replace('\n', nl) + s[b:]

# ======================================================== accumulate shader
p = 'RageVEditor/assets/shaders/reflection_accumulate.rvshader'; s, nl = load(p)
s = rep(s, nl, """	vec4 Probe;
} u_Reflection;""", """	vec4 Probe;
	// The signal's tuning (RT-first T4, Renderer3D::SignalParams): x the
	// smear cap in texels of travel, y the moving memory floor, z the
	// silhouette memory floor, w spare.
	vec4 Tuning;
	// The blur's (read by reflection_blur.rvshader; carried here so the two
	// share one push block).
	vec4 Blur;
} u_Reflection;

// Why the texel's own history was refused this frame, for the
// `reflection-refusal` debug view: 0 kept, 1 off screen, 2 none there,
// 3 normal, 4 plane, 5 roughness. Written into o_Extra.a above `choice`.
int g_Refusal = 0;""")
s = rep(s, nl, "const float kSmearTexels = 6.0;", "#define kSmearTexels (u_Reflection.Tuning.x)")
s = rep(s, nl, "const float kMovingMemory = 8.0;", "#define kMovingMemory (u_Reflection.Tuning.y)")
s = rep(s, nl, "const float kSilhouetteMemory = 4.0;", "#define kSilhouetteMemory (u_Reflection.Tuning.z)")
# refusal reasons in HistoryAt
s = rep(s, nl, """	if (any(lessThan(thenUv, vec2(0.0))) || any(greaterThan(thenUv, vec2(1.0))))
		return false;
	const float thenRow = u_Reflection.History.w > 0.5 ? 1.0 - thenUv.y : thenUv.y;""",
"""	if (any(lessThan(thenUv, vec2(0.0))) || any(greaterThan(thenUv, vec2(1.0))))
	{
		g_Refusal = 1;
		return false;
	}
	const float thenRow = u_Reflection.History.w > 0.5 ? 1.0 - thenUv.y : thenUv.y;""")
s = rep(s, nl, """		c.reflector = texelFetch(u_HistorySurface, pastTexel, 0);
		if (c.reflector.a < 0.0)
			continue;
		c.extra = texelFetch(u_HistoryExtra, pastTexel, 0);""",
"""		c.reflector = texelFetch(u_HistorySurface, pastTexel, 0);
		if (c.reflector.a < 0.0)
		{
			if (k == 0) g_Refusal = 2;
			continue;
		}
		c.extra = texelFetch(u_HistoryExtra, pastTexel, 0);""")
s = rep(s, nl, """			const vec3 wasN = OctDecode(c.reflector.rg);
			if (dot(wasN, N) < 0.8)
				continue;
			if (abs(dot(wasN, P) - c.reflector.b) > 0.05 + 0.01 * eyeDistance)
				continue;
			if (abs(c.extra.r - roughness) > 0.5)
				continue;""",
"""			const vec3 wasN = OctDecode(c.reflector.rg);
			if (dot(wasN, N) < 0.8)
			{
				if (k == 0) g_Refusal = 3;
				continue;
			}
			if (abs(dot(wasN, P) - c.reflector.b) > 0.05 + 0.01 * eyeDistance)
			{
				if (k == 0) g_Refusal = 4;
				continue;
			}
			if (abs(c.extra.r - roughness) > 0.5)
			{
				if (k == 0) g_Refusal = 5;
				continue;
			}""")
# the diffuse kind: no virtual image
s = span(s, nl, "	const float curvature = Curvature(texel, size, P, N);", "	float image = travelled * clamp(dominant, 0.0, 1.0) / (1.0 + 2.0 * travelled * curvature);",
"""#ifdef RV_SIGNAL_DIFFUSE
	// A diffuse signal (shadows, occlusion, irradiance) moves with its
	// surface: no virtual image, the history is read where the surface was.
	float image = 0.0;
#else
	const float curvature = Curvature(texel, size, P, N);
	const float NoV = clamp(dot(N, -sight), 0.0, 1.0);
	const float lobe = 0.298475 * log(39.4115 - 39.0029 * roughness);
	const float dominant = pow(1.0 - NoV, 10.8649) * (1.0 - lobe) + lobe;
	float image = travelled * clamp(dominant, 0.0, 1.0) / (1.0 + 2.0 * travelled * curvature);
#endif
""")
s = span(s, nl, "		if (haveSurface)\n			image = mix(atSurface.reflector.a, image, 1.0 / min(atSurface.past.a + 1.0, 8.0));", "\n		}",
"""#ifdef RV_SIGNAL_DIFFUSE
		Candidate c = atSurface;
		bool have = haveSurface;
		choice = have ? 0.5 : 0.0;
#else
		if (haveSurface)
			image = mix(atSurface.reflector.a, image, 1.0 / min(atSurface.past.a + 1.0, 8.0));
		Candidate c;
		bool have = HistoryAt(P + sight * image, size, P, N, roughness, eyeDistance, silhouette, c);
		choice = have ? 1.0 : 0.0;
		if (!have && haveSurface)
		{
			c = atSurface;
			have = true;
			choice = 0.5;
		}
#endif
""")
s = rep(s, nl, """			const float gloss = smoothstep(0.0, 0.3, roughness);""",
"""#ifdef RV_SIGNAL_DIFFUSE
			const float gloss = 1.0;   // a diffuse signal has no mirror end
#else
			const float gloss = smoothstep(0.0, 0.3, roughness);
#endif""")
s = rep(s, nl, """	o_Extra = vec4(roughness, momMean, momMeanSq, choice);""",
"""	// choice in the fraction (0 none, 0.25 the surface's history, 0.5 the
	// image's), the refusal reason in the integer part.
	o_Extra = vec4(roughness, momMean, momMeanSq, 0.5 * choice + float(g_Refusal));""")
save(p, s)

# ============================================================= blur shader
p = 'RageVEditor/assets/shaders/reflection_blur.rvshader'; s, nl = load(p)
s = rep(s, nl, """	vec4 Probe;
} u_Reflection;""", """	vec4 Probe;
	vec4 Tuning;
	// The young-history blur's tuning (RT-first T4, Renderer3D::SignalParams):
	// x the radius at one frame of history, y the frames at which it has
	// faded to nothing, z how far past the lobe's footprint it may reach
	// while young (specular), w the radius bound in texels (diffuse).
	vec4 Blur;
} u_Reflection;""")
s = rep(s, nl, "const float kYoungRadius = 12.0;", "#define kYoungRadius (u_Reflection.Blur.x)")
s = rep(s, nl, "const float kBlurFrames = 32.0;", "#define kBlurFrames (u_Reflection.Blur.y)")
s = rep(s, nl, "const float kYoungOverreach = 1.5;", "#define kYoungOverreach (u_Reflection.Blur.z)")
s = rep(s, nl, """	const vec2 reach = min(vec2(radius), footprint / texelSize * (1.0 + kYoungOverreach * young * young));""",
"""#ifdef RV_SIGNAL_DIFFUSE
	// A diffuse signal has no lobe to bound it: a plain radius does.
	const vec2 reach = min(vec2(radius), vec2(u_Reflection.Blur.w));
#else
	const vec2 reach = min(vec2(radius), footprint / texelSize * (1.0 + kYoungOverreach * young * young));
#endif""")
save(p, s)

# ============================================================ Renderer3D.h
p = 'RageV/src/RageV/Renderer/Renderer3D.h'; s, nl = load(p)
s = rep(s, nl, """		static void AccumulateReflections(const RHI::Ref<RHI::RHITexture>& fresh,
										  const RHI::Ref<RHI::RHITexture>& depth,
										  const RHI::Ref<RHI::RHITexture>& surface,
										  const RHI::Ref<RHI::RHITexture>& previous,
										  const RHI::Ref<RHI::RHITexture>& previousSurface,
										  const RHI::Ref<RHI::RHITexture>& previousExtra,
										  const RHI::Ref<RHI::RHITexture>& velocity,
										  CameraMotion& motion, bool hasHistory);""",
"""		// **The reconstruction contract (RT-first T4, docs/RT-FIRST.md).** One
		// temporal accumulator and one young-history blur for every stochastic
		// signal, in two kinds: Specular reprojects by the surface and by the
		// virtual image and bounds its blur by the lobe; Diffuse reprojects by
		// the surface alone and bounds its blur by a radius. Each signal owns
		// a TemporalHistory of three attachments (picture + frames, reflector
		// normal/plane/image distance, roughness + moments + choice/refusal)
		// and a slot for its resource sets. The numbers are what the
		// reflections were tuned to over 2026-09-05/06; other signals start
		// from ReflectionSignal() and change what they measure.
		struct SignalParams
		{
			enum class Kind { Specular, Diffuse };
			Kind Kind = Kind::Specular;
			int Slot = 0;                  // 0 reflections, 1 shadows, 2 occlusion, 3 irradiance
			float Memory = 64.0f;          // frames kept standing still
			float Fewest = 4.0f;           // the floor for a rough surface (a mirror keeps 1)
			float Slack = 1.0f;            // texels of travel per frame that halve the memory
			float BoundWidth = 3.0f;       // the history bound, in neighbourhood spreads, on a rough surface
			float SmearTexels = 6.0f;      // the average may span this much travel
			float MovingMemory = 8.0f;     // and never fewer frames than this while moving
			float SilhouetteMemory = 4.0f; // the floor at a silhouette whose other side moves
			float YoungRadius = 12.0f;     // the blur at one frame of history, texels
			float BlurFrames = 32.0f;      // gone by this many frames
			float YoungOverreach = 1.5f;   // Specular: how far past the lobe while young
			float MaxRadius = 8.0f;        // Diffuse: the radius bound, texels
		};
		static SignalParams ReflectionSignal();

		static void AccumulateSignal(const SignalParams& signal,
									 const RHI::Ref<RHI::RHITexture>& fresh,
									 const RHI::Ref<RHI::RHITexture>& depth,
									 const RHI::Ref<RHI::RHITexture>& surface,
									 const RHI::Ref<RHI::RHITexture>& previous,
									 const RHI::Ref<RHI::RHITexture>& previousSurface,
									 const RHI::Ref<RHI::RHITexture>& previousExtra,
									 const RHI::Ref<RHI::RHITexture>& velocity,
									 CameraMotion& motion, bool hasHistory);""")
s = rep(s, nl, """		static void BlurReflections(const RHI::Ref<RHI::RHITexture>& accumulated,
									const RHI::Ref<RHI::RHITexture>& depth,
									const RHI::Ref<RHI::RHITexture>& surface,
									const RHI::Ref<RHI::RHITexture>& imageDistance,
									int stride);""",
"""		static void BlurSignal(const SignalParams& signal,
							   const RHI::Ref<RHI::RHITexture>& accumulated,
							   const RHI::Ref<RHI::RHITexture>& depth,
							   const RHI::Ref<RHI::RHITexture>& surface,
							   const RHI::Ref<RHI::RHITexture>& imageDistance,
							   int stride);""")
save(p, s)

# ============================================================ Renderer3D.cpp
p = 'RageV/src/RageV/Renderer/Renderer3D.cpp'; s, nl = load(p)
s = rep(s, nl, """			Ref<RHIShader>   ReflectionBlurShader;
			Ref<RHIPipeline> ReflectionBlurPipeline;
""", """			Ref<RHIShader>   ReflectionBlurShader;
			Ref<RHIPipeline> ReflectionBlurPipeline;
			// The diffuse kind of the same two (RT-first T4): compiled with
			// RV_SIGNAL_DIFFUSE, for shadows, occlusion and irradiance.
			Ref<RHIShader>   SignalAccumulateDiffuseShader;
			Ref<RHIPipeline> SignalAccumulateDiffusePipeline;
			Ref<RHIShader>   SignalBlurDiffuseShader;
			Ref<RHIPipeline> SignalBlurDiffusePipeline;
""")
s = rep(s, nl, """				Ref<RHIResourceSet> ReflectionAccumulateInputs;
""", """				// One per signal slot (SignalParams::Slot): the accumulate and
				// blur inputs of reflections, shadows, occlusion, irradiance.
				Ref<RHIResourceSet> SignalAccumulateInputs[4];
""")
s = rep(s, nl, """				Ref<RHIResourceSet> ReflectionBlurInputs;
""", """				Ref<RHIResourceSet> SignalBlurInputs[4];
""")
# the shader loop: two more passes
s = rep(s, nl, """			s_Data->ReflectionBlurShader = nullptr;
			if (s_Data->RayReflectionsOn)
			{
				for (int pass = 0; pass < 4; ++pass)
				{
					const char* file = pass == 0 ? "assets/shaders/reflection_trace.rvshader"
									 : pass == 1 ? "assets/shaders/reflection_resolve.rvshader"
									 : pass == 2 ? "assets/shaders/reflection_accumulate.rvshader"
												 : "assets/shaders/reflection_blur.rvshader";
					Ref<RHIShader>& target = pass == 0 ? s_Data->ReflectionTraceShader
										   : pass == 1 ? s_Data->ReflectionResolveShader
										   : pass == 2 ? s_Data->ReflectionAccumulateShader
													   : s_Data->ReflectionBlurShader;
					if (auto shader = ShaderCompiler::CompileFromFile(file, traceDefines))""",
"""			s_Data->ReflectionBlurShader = nullptr;
			s_Data->SignalAccumulateDiffuseShader = nullptr;
			s_Data->SignalBlurDiffuseShader = nullptr;
			if (s_Data->RayReflectionsOn)
			{
				// Passes 4 and 5 are the diffuse kind of the accumulate and the
				// blur (RT-first T4): the same files with RV_SIGNAL_DIFFUSE.
				std::vector<std::string> diffuseDefines = traceDefines;
				diffuseDefines.push_back("RV_SIGNAL_DIFFUSE");
				for (int pass = 0; pass < 6; ++pass)
				{
					const char* file = pass == 0 ? "assets/shaders/reflection_trace.rvshader"
									 : pass == 1 ? "assets/shaders/reflection_resolve.rvshader"
									 : (pass == 2 || pass == 4) ? "assets/shaders/reflection_accumulate.rvshader"
												 : "assets/shaders/reflection_blur.rvshader";
					Ref<RHIShader>& target = pass == 0 ? s_Data->ReflectionTraceShader
										   : pass == 1 ? s_Data->ReflectionResolveShader
										   : pass == 2 ? s_Data->ReflectionAccumulateShader
										   : pass == 3 ? s_Data->ReflectionBlurShader
										   : pass == 4 ? s_Data->SignalAccumulateDiffuseShader
													   : s_Data->SignalBlurDiffuseShader;
					if (auto shader = ShaderCompiler::CompileFromFile(file, pass >= 4 ? diffuseDefines : traceDefines))""")
# the pipeline loop: six
s = rep(s, nl, """		for (int pass = 0; pass < 4; ++pass)
		{
			const Ref<RHIShader>& shader = pass == 0 ? s_Data->ReflectionTraceShader
										 : pass == 1 ? s_Data->ReflectionResolveShader
										 : pass == 2 ? s_Data->ReflectionAccumulateShader
													  : s_Data->ReflectionBlurShader;
			Ref<RHIPipeline>& target = pass == 0 ? s_Data->ReflectionTracePipeline
									 : pass == 1 ? s_Data->ReflectionResolvePipeline
									 : pass == 2 ? s_Data->ReflectionAccumulatePipeline
												  : s_Data->ReflectionBlurPipeline;""",
"""		for (int pass = 0; pass < 6; ++pass)
		{
			const Ref<RHIShader>& shader = pass == 0 ? s_Data->ReflectionTraceShader
										 : pass == 1 ? s_Data->ReflectionResolveShader
										 : pass == 2 ? s_Data->ReflectionAccumulateShader
										 : pass == 3 ? s_Data->ReflectionBlurShader
										 : pass == 4 ? s_Data->SignalAccumulateDiffuseShader
													  : s_Data->SignalBlurDiffuseShader;
			Ref<RHIPipeline>& target = pass == 0 ? s_Data->ReflectionTracePipeline
									 : pass == 1 ? s_Data->ReflectionResolvePipeline
									 : pass == 2 ? s_Data->ReflectionAccumulatePipeline
									 : pass == 3 ? s_Data->ReflectionBlurPipeline
									 : pass == 4 ? s_Data->SignalAccumulateDiffusePipeline
												  : s_Data->SignalBlurDiffusePipeline;""")
s = rep(s, nl, """			reflection.Name = pass == 0 ? "Renderer3D.reflection.trace"
							: pass == 1 ? "Renderer3D.reflection.resolve"
							: pass == 2 ? "Renderer3D.reflection.accumulate"
										: "Renderer3D.reflection.blur";""",
"""			reflection.Name = pass == 0 ? "Renderer3D.reflection.trace"
							: pass == 1 ? "Renderer3D.reflection.resolve"
							: pass == 2 ? "Renderer3D.reflection.accumulate"
							: pass == 3 ? "Renderer3D.reflection.blur"
							: pass == 4 ? "Renderer3D.signal.accumulate.diffuse"
										: "Renderer3D.signal.blur.diffuse";""")
s = rep(s, nl, """			const int extras = pass == 0 ? 1 : pass == 2 ? 2 : 0;""",
"""			const int extras = pass == 0 ? 1 : (pass == 2 || pass == 4) ? 2 : 0;""")
# the push block for the two passes
s = rep(s, nl, """		struct ReflectionPushConstants
		{
			Mat4 InverseViewProjection{ 1.0f };
			Mat4 PreviousViewProjection{ 1.0f };
			Vec4 History{ 0.0f, 24.0f, 6.0f, 0.0f };
			Vec4 Probe{ 3.0f, 2.0f, 0.0f, 0.0f };
		};
""", """		struct ReflectionPushConstants
		{
			Mat4 InverseViewProjection{ 1.0f };
			Mat4 PreviousViewProjection{ 1.0f };
			Vec4 History{ 0.0f, 24.0f, 6.0f, 0.0f };
			Vec4 Probe{ 3.0f, 2.0f, 0.0f, 0.0f };
		};
		// The accumulate and blur passes' block (RT-first T4): the same four,
		// then the signal's tuning and its blur's, from SignalParams.
		struct SignalPushConstants
		{
			Mat4 InverseViewProjection{ 1.0f };
			Mat4 PreviousViewProjection{ 1.0f };
			Vec4 History{ 0.0f, 64.0f, 4.0f, 0.0f };
			Vec4 Probe{ 3.0f, 1.0f, 0.0f, 0.0f };
			Vec4 Tuning{ 6.0f, 8.0f, 4.0f, 0.0f };
			Vec4 Blur{ 12.0f, 32.0f, 1.5f, 8.0f };
		};
""")
# the two bodies
accumulate_new = """	Renderer3D::SignalParams Renderer3D::ReflectionSignal()
	{
		SignalParams signal;
		signal.Kind = SignalParams::Kind::Specular;
		signal.Slot = 0;
		return signal;   // the defaults are the reflections' tuning
	}

	void Renderer3D::AccumulateSignal(const SignalParams& signal,
									  const RHI::Ref<RHITexture>& fresh,
									  const RHI::Ref<RHITexture>& depth,
									  const RHI::Ref<RHITexture>& surface,
									  const RHI::Ref<RHITexture>& previous,
									  const RHI::Ref<RHITexture>& previousSurface,
									  const RHI::Ref<RHITexture>& previousExtra,
									  const RHI::Ref<RHITexture>& velocity,
									  CameraMotion& motion, bool hasHistory)
	{
		const bool diffuse = signal.Kind == SignalParams::Kind::Diffuse;
		const Ref<RHIPipeline>& pipeline = diffuse ? s_Data->SignalAccumulateDiffusePipeline
												   : s_Data->ReflectionAccumulatePipeline;
		if (!s_Data || !pipeline || !s_Data->ActiveScene)
			return;
		RHICommandList* cmd = Renderer::GetCommandList();
		Renderer3DData::SceneSlot& slot = *s_Data->ActiveScene;
		if (!cmd || !slot.LampSet || !fresh || !depth || !surface)
			return;
		const int index = Math::Clamp(signal.Slot, 0, 3);
		Ref<RHIResourceSet>& inputs = slot.SignalAccumulateInputs[index];
		if (!inputs)
			inputs = s_Data->Device->CreateResourceSet(pipeline, 3);
		if (!inputs)
			return;
		const Ref<RHISampler>& historySampler =
			s_Data->WaterClampSampler ? s_Data->WaterClampSampler : s_Data->PointSampler;
		inputs->SetTexture(0, fresh, s_Data->PointSampler);
		inputs->SetTexture(1, depth, s_Data->PointSampler);
		inputs->SetTexture(2, previous && hasHistory ? previous : fresh, historySampler);
		inputs->SetTexture(3, surface, s_Data->PointSampler);
		inputs->SetTexture(4, previousSurface && hasHistory ? previousSurface : surface,
						   s_Data->PointSampler);
		inputs->SetTexture(5, previousExtra && hasHistory ? previousExtra : surface,
						   s_Data->PointSampler);
		// The scene's velocity, for the silhouette rule; black where the
		// target has none, which reads as still.
		inputs->SetTexture(6, velocity ? velocity : TextureLoader::TransparentBlack(*s_Data->Device),
						   s_Data->PointSampler);
		inputs->Commit();

		SignalPushConstants push;
		push.InverseViewProjection = Math::Inverse(s_Data->Scene.ViewProjection);
		push.PreviousViewProjection = motion.ViewProjection;
		const EngineConfig& config = EngineConfig::Get();
		const bool readHistory = !(config.HasReflectionHistoryOverride
								   && !config.ReflectionHistoryOverride);
		push.History.x = previous && previousSurface && previousExtra && hasHistory
					   && readHistory ? 1.0f : 0.0f;
		push.History.y = signal.Memory;
		push.History.z = signal.Fewest;
		push.History.w = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
		push.Probe.x = signal.BoundWidth;
		push.Probe.y = signal.Slack;
		push.Tuning = { signal.SmearTexels, signal.MovingMemory, signal.SilhouetteMemory, 0.0f };
		push.Blur = { signal.YoungRadius, signal.BlurFrames, signal.YoungOverreach, signal.MaxRadius };
		motion.ViewProjection = s_Data->Scene.ViewProjection;

		cmd->BindPipeline(pipeline);
		cmd->BindResourceSet(0, slot.LampSet);
		if (s_Data->Heap)
			cmd->BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
		cmd->BindResourceSet(3, inputs);
		cmd->PushConstants(ShaderStage::Fragment, 0, sizeof(push), &push);
		cmd->Draw(3);
	}
"""
blur_new = """	void Renderer3D::BlurSignal(const SignalParams& signal,
								const RHI::Ref<RHITexture>& accumulated,
								const RHI::Ref<RHITexture>& depth,
								const RHI::Ref<RHITexture>& surface,
								const RHI::Ref<RHITexture>& imageDistance,
								int stride)
	{
		const bool diffuse = signal.Kind == SignalParams::Kind::Diffuse;
		const Ref<RHIPipeline>& pipeline = diffuse ? s_Data->SignalBlurDiffusePipeline
												   : s_Data->ReflectionBlurPipeline;
		if (!s_Data || !pipeline || !s_Data->ActiveScene)
			return;
		RHICommandList* cmd = Renderer::GetCommandList();
		Renderer3DData::SceneSlot& slot = *s_Data->ActiveScene;
		if (!cmd || !slot.LampSet || !accumulated || !depth || !surface || !imageDistance)
			return;
		const int index = Math::Clamp(signal.Slot, 0, 3);
		Ref<RHIResourceSet>& inputs = slot.SignalBlurInputs[index];
		if (!inputs)
			inputs = s_Data->Device->CreateResourceSet(pipeline, 3);
		if (!inputs)
			return;
		inputs->SetTexture(0, accumulated, s_Data->PointSampler);
		inputs->SetTexture(1, depth, s_Data->PointSampler);
		inputs->SetTexture(2, surface, s_Data->PointSampler);
		inputs->SetTexture(3, imageDistance, s_Data->PointSampler);
		inputs->Commit();

		SignalPushConstants push;
		push.InverseViewProjection = Math::Inverse(s_Data->Scene.ViewProjection);
		push.History.y = signal.BlurFrames;
		push.History.w = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
		push.Probe.z = (float)Math::Max(stride, 1);
		push.Tuning = { signal.SmearTexels, signal.MovingMemory, signal.SilhouetteMemory, 0.0f };
		push.Blur = { signal.YoungRadius, signal.BlurFrames, signal.YoungOverreach, signal.MaxRadius };

		cmd->BindPipeline(pipeline);
		cmd->BindResourceSet(0, slot.LampSet);
		if (s_Data->Heap)
			cmd->BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
		cmd->BindResourceSet(3, inputs);
		cmd->PushConstants(ShaderStage::Fragment, 0, sizeof(push), &push);
		cmd->Draw(3);
	}
"""
for start, new in (("\tvoid Renderer3D::BlurReflections(", blur_new), ("\tvoid Renderer3D::AccumulateReflections(", accumulate_new)):
    a = s.index(start.replace('\n', nl)); b = s.index(nl + '\t}' + nl, a) + len(nl + '\t}' + nl)
    s = s[:a] + new.replace('\n', nl) + s[b:]
save(p, s)

# ========================================================= frame graph
p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'; s, nl = load(p)
a = s.index("\t\t\tconst bool reflectionHistory = desc.Reflections && desc.Reflections->HasHistory() && previousReflections != kRGInvalid;".replace('\n', nl))
end = ("\t\t\t\t\tblurInput = output;" + nl + "\t\t\t\t\tblurred = output;" + nl + "\t\t\t\t}" + nl)
b = s.index(end, a) + len(end)
new = """			// **The reconstruction contract (RT-first T4).** The accumulate and
			// the three young-history blurs are one helper for any signal:
			// reflections here; shadows, occlusion and irradiance to follow.
			struct SignalPassNames { const char* Accumulate; const char* Blur[3]; };
			auto addSignal = [&](const SignalPassNames& names, const Renderer3D::SignalParams& params,
								 RGResource fresh, RGResource current, RGResource previous, bool hasHistory,
								 CameraMotion* motion, RGTargetDesc blurDesc) -> RGResource
			{
				graph.AddPass(names.Accumulate,
					[&](RGPassBuilder& builder)
					{
						builder.Write(current);
						builder.Sample(fresh);
						builder.Sample(sceneHDR);
						if (hasHistory)
							builder.Sample(previous);
						builder.DisableDepth();
					},
					[params, fresh, sceneHDR, normalIndex, velocityIndex, current, previous, hasHistory, motion]
					(RGPassContext& context)
					{
						Renderer3D::AccumulateSignal(params,
							context.Color(fresh), context.Depth(sceneHDR),
							context.Color(sceneHDR, normalIndex),
							hasHistory ? context.Color(previous) : nullptr,
							hasHistory ? context.Color(previous, 1) : nullptr,
							hasHistory ? context.Color(previous, 2) : nullptr,
							context.Color(sceneHDR, velocityIndex),
							*motion, hasHistory);
					});
				// Three blur passes at strides 1, 2, 4: each reads the previous
				// pass's output; the first reads the history itself, which is
				// never written here.
				blurDesc.ExtraColors.clear();
				RGResource blurred = kRGInvalid;
				RGResource blurInput = current;
				const std::string base = blurDesc.Name;
				for (int pass = 0; pass < 3; ++pass)
				{
					blurDesc.Name = base + (pass == 0 ? "" : pass == 1 ? "2" : "4");
					const RGResource output = graph.CreateTarget(blurDesc);
					const RGResource input = blurInput;
					const int stride = 1 << pass;
					graph.AddPass(names.Blur[pass],
						[&](RGPassBuilder& builder)
						{
							builder.Write(output);
							builder.Sample(input);
							if (input != current)
								builder.Sample(current);
							builder.Sample(sceneHDR);
							builder.DisableDepth();
						},
						[params, input, current, sceneHDR, normalIndex, stride](RGPassContext& context)
						{
							Renderer3D::BlurSignal(params, context.Color(input),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex),
												   context.Color(current, 1),
												   stride);
						});
					blurInput = output;
					blurred = output;
				}
				return blurred;
			};
			const bool reflectionHistory = desc.Reflections && desc.Reflections->HasHistory() && previousReflections != kRGInvalid;
			RGTargetDesc reflectionBlurDesc = traceDesc;
			reflectionBlurDesc.Name = "ReflectionBlurred";
			static const SignalPassNames kReflectionPasses = { "ReflectionAccumulate", { "ReflectionBlur", "ReflectionBlur2", "ReflectionBlur4" } };
			const RGResource blurredReflections = addSignal(kReflectionPasses, Renderer3D::ReflectionSignal(),
															resolved, currentReflections, previousReflections,
															reflectionHistory, &desc.Reflections->Motion(),
															reflectionBlurDesc);
			{
				const RGResource blurred = blurredReflections;
"""
s = s[:a] + new.replace('\n', nl) + s[b:]
s = rep(s, nl, """							  : view == EngineConfig::DebugViewMode::Reflection ? 64.0f""",
"""							  : view == EngineConfig::DebugViewMode::Reflection ? Renderer3D::ReflectionSignal().Memory
							  : view == EngineConfig::DebugViewMode::ReflectionChoice ? 6.0f""")
save(p, s)

p = 'RageV/src/RageV/Core/EngineConfig.cpp'; s, nl = load(p)
s = rep(s, nl, """			else if (lowered == "reflection-choice" || lowered == "reflectionchoice")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionChoice;""",
"""			// The integer part is why the texel's own history was refused (0
			// kept, 1 off screen, 2 none, 3 normal, 4 plane, 5 roughness), the
			// fraction which candidate served (0 none, .25 surface, .5 image);
			// the ramp runs 0..6 (RT-first T4).
			else if (lowered == "reflection-choice" || lowered == "reflectionchoice"
					 || lowered == "reflection-refusal" || lowered == "reflectionrefusal")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionChoice;""")
save(p, s)
print('T4 patched')
