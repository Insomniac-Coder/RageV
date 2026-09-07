"""Prelude for the second half of the T4 patch: the same helpers, tolerant
of the mixed line endings Renderer3D.cpp carries (some lines CRLF, some
LF): each match is tried with both."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def load(p):
    s = open(p, 'rb').read().decode('utf-8'); return s, ('\r\n' if '\r\n' in s else '\n')
def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)
def _variants(text, nl):
    seen = []
    for cand in (text.replace('\n', nl), text.replace('\n', '\n'), text.replace('\n', '\r\n')):
        if cand not in seen:
            seen.append(cand)
    return seen
def rep(s, nl, old, new, count=1):
    for o in _variants(old, nl):
        if s.count(o) == count:
            n = new.replace('\n', '\r\n' if '\r\n' in o or (('\n' not in o) and nl == '\r\n') else '\n')
            return s.replace(o, n)
    raise AssertionError((s.count(old.replace('\n', nl)), old[:70]))
def span(s, nl, start, end_line, new):
    a = -1
    for st in _variants(start, nl):
        a = s.find(st)
        if a >= 0:
            break
    assert a >= 0, start[:60]
    b = -1
    for en in _variants(end_line, nl):
        b = s.find(en, a)
        if b >= 0:
            break
    assert b >= 0, end_line[:60]
    b = s.find('\n', b) + 1
    return s[:a] + new.replace('\n', nl) + s[b:]

"""The C++ half of T4 (shaders and the header are applied)."""
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
s = rep(s, nl, """				Ref<RHIResourceSet> ReflectionAccumulateInputs;""", """				// One per signal slot (SignalParams::Slot): the accumulate and
				// blur inputs of reflections, shadows, occlusion, irradiance.
				Ref<RHIResourceSet> SignalAccumulateInputs[4];""")
s = rep(s, nl, """				Ref<RHIResourceSet> ReflectionBlurInputs;""", """				Ref<RHIResourceSet> SignalBlurInputs[4];""")
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
