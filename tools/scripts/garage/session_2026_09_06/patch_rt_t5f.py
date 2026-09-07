"""RT-first T5, the contract's second payload on the C++ side and the
direct light on the contract:
- Renderer3D: the pair pipelines (RV_SIGNAL_DIFFUSE + RV_SIGNAL_PAIR) as
  passes 6 and 7 of the signal loops; AccumulateSignal/BlurSignal take the
  twin; DirectSignal() -- the direct light's tuning.
- TemporalHistory: a fourth attachment.
- FrameGraphBuilder: addSignal hoisted above the G-buffer pass and given the
  pair; the direct light's history, accumulate and blur after DirectTrace; the
  lit pass reads the blurred pair; the debug views direct-light /
  direct-refusal.
"""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')
exec(open('tools/scripts/garage/session_2026_09_06/t4_prelude.py', encoding='utf-8').read())

# ---------------------------------------------------------------- Renderer3D.h
p = 'RageV/src/RageV/Renderer/Renderer3D.h'; s, nl = load(p)
if 'DirectSignal()' not in s:
    s = rep(s, nl, """		static SignalParams ReflectionSignal();
""", """		static SignalParams ReflectionSignal();
		// RT-first T5: the direct light's tuning -- a diffuse-kind signal in
		// slot 1 whose two payloads (diffuse before the albedo, specular
		// complete) share one history.
		static SignalParams DirectSignal();
""")
    s, n = re.subn(r"int stride\);", "int stride,\n\t\t\t\t\t\t\t   const RHI::Ref<RHI::RHITexture>& accumulated2 = nullptr);", s); assert n == 1
    s, n = re.subn(r"(velocity,\s*\n\s*)CameraMotion& motion, bool hasHistory\);",
                   r"\1CameraMotion& motion, bool hasHistory,\n\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& fresh2 = nullptr,\n\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& previous2 = nullptr);", s); assert n == 1
    save(p, s)
else:
    print('header already patched')

# ---------------------------------------------------------------- Renderer3D.cpp
p = 'RageV/src/RageV/Renderer/Renderer3D.cpp'; s, nl = load(p)
s = rep(s, nl, """			Ref<RHIShader>   SignalBlurDiffuseShader;
			Ref<RHIPipeline> SignalBlurDiffusePipeline;
""", """			Ref<RHIShader>   SignalBlurDiffuseShader;
			Ref<RHIPipeline> SignalBlurDiffusePipeline;
			// The diffuse kind with the second payload (RV_SIGNAL_PAIR, RT-first
			// T5): the direct light's diffuse and specular halves in one history.
			Ref<RHIShader>   SignalAccumulateDiffusePairShader;
			Ref<RHIPipeline> SignalAccumulateDiffusePairPipeline;
			Ref<RHIShader>   SignalBlurDiffusePairShader;
			Ref<RHIPipeline> SignalBlurDiffusePairPipeline;
""")
s = rep(s, nl, """			s_Data->SignalBlurDiffuseShader = nullptr;
""", """			s_Data->SignalBlurDiffuseShader = nullptr;
			s_Data->SignalAccumulateDiffusePairShader = nullptr;
			s_Data->SignalBlurDiffusePairShader = nullptr;
""")
s = rep(s, nl, """			if (s_Data->RayReflectionsOn)
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
					if (auto shader = ShaderCompiler::CompileFromFile(file, pass >= 4 ? diffuseDefines : traceDefines))
""", """			// The signal contract's shaders load for shadows as well as for
			// reflections (RT-first T5: the direct light rides the pair kind).
			if (s_Data->RayReflectionsOn || s_Data->RayShadowsOn)
			{
				std::vector<std::string> diffuseDefines = traceDefines;
				diffuseDefines.push_back("RV_SIGNAL_DIFFUSE");
				std::vector<std::string> pairDefines = diffuseDefines;
				pairDefines.push_back("RV_SIGNAL_PAIR");
				for (int pass = 0; pass < 8; ++pass)
				{
					const char* file = pass == 0 ? "assets/shaders/reflection_trace.rvshader"
									 : pass == 1 ? "assets/shaders/reflection_resolve.rvshader"
									 : (pass == 2 || pass == 4 || pass == 6) ? "assets/shaders/reflection_accumulate.rvshader"
												 : "assets/shaders/reflection_blur.rvshader";
					Ref<RHIShader>& target = pass == 0 ? s_Data->ReflectionTraceShader
										   : pass == 1 ? s_Data->ReflectionResolveShader
										   : pass == 2 ? s_Data->ReflectionAccumulateShader
										   : pass == 3 ? s_Data->ReflectionBlurShader
										   : pass == 4 ? s_Data->SignalAccumulateDiffuseShader
										   : pass == 5 ? s_Data->SignalBlurDiffuseShader
										   : pass == 6 ? s_Data->SignalAccumulateDiffusePairShader
													   : s_Data->SignalBlurDiffusePairShader;
					if (auto shader = ShaderCompiler::CompileFromFile(file, pass >= 6 ? pairDefines
																		  : pass >= 4 ? diffuseDefines : traceDefines))
""")
s = rep(s, nl, """		for (int pass = 0; pass < 6; ++pass)
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
												  : s_Data->SignalBlurDiffusePipeline;
""", """		for (int pass = 0; pass < 8; ++pass)
		{
			const Ref<RHIShader>& shader = pass == 0 ? s_Data->ReflectionTraceShader
										 : pass == 1 ? s_Data->ReflectionResolveShader
										 : pass == 2 ? s_Data->ReflectionAccumulateShader
										 : pass == 3 ? s_Data->ReflectionBlurShader
										 : pass == 4 ? s_Data->SignalAccumulateDiffuseShader
										 : pass == 5 ? s_Data->SignalBlurDiffuseShader
										 : pass == 6 ? s_Data->SignalAccumulateDiffusePairShader
													  : s_Data->SignalBlurDiffusePairShader;
			Ref<RHIPipeline>& target = pass == 0 ? s_Data->ReflectionTracePipeline
									 : pass == 1 ? s_Data->ReflectionResolvePipeline
									 : pass == 2 ? s_Data->ReflectionAccumulatePipeline
									 : pass == 3 ? s_Data->ReflectionBlurPipeline
									 : pass == 4 ? s_Data->SignalAccumulateDiffusePipeline
									 : pass == 5 ? s_Data->SignalBlurDiffusePipeline
									 : pass == 6 ? s_Data->SignalAccumulateDiffusePairPipeline
												  : s_Data->SignalBlurDiffusePairPipeline;
""")
s = rep(s, nl, """							: pass == 4 ? "Renderer3D.signal.accumulate.diffuse"
										: "Renderer3D.signal.blur.diffuse";
""", """							: pass == 4 ? "Renderer3D.signal.accumulate.diffuse"
							: pass == 5 ? "Renderer3D.signal.blur.diffuse"
							: pass == 6 ? "Renderer3D.signal.accumulate.pair"
										: "Renderer3D.signal.blur.pair";
""")
s = rep(s, nl, """			const int extras = pass == 0 ? 1 : (pass == 2 || pass == 4) ? 2 : 0;
""", """			// The accumulators write the picture, the surface and the extra;
			// the pair kind a fourth (the twin) and its blur a second.
			const int extras = pass == 0 ? 1 : (pass == 2 || pass == 4) ? 2 : pass == 6 ? 3 : pass == 7 ? 1 : 0;
""")
# DirectSignal()
s = rep(s, nl, """		return signal;   // the defaults are the reflections' tuning
	}
""", """		return signal;   // the defaults are the reflections' tuning
	}

	Renderer3D::SignalParams Renderer3D::DirectSignal()
	{
		SignalParams signal;
		signal.Type = SignalParams::Kind::Diffuse;
		signal.Slot = 1;
		// The direct light has no mirror end: the young-history blur is bounded
		// in texels (MaxRadius) and the rough-surface floor applies everywhere.
		return signal;
	}
""")
# AccumulateSignal: the twin
s = rep(s, nl, """									  CameraMotion& motion, bool hasHistory)
	{
		const bool diffuse = signal.Type == SignalParams::Kind::Diffuse;
		const Ref<RHIPipeline>& pipeline = diffuse ? s_Data->SignalAccumulateDiffusePipeline
												   : s_Data->ReflectionAccumulatePipeline;
""", """									  CameraMotion& motion, bool hasHistory,
									  const RHI::Ref<RHITexture>& fresh2,
									  const RHI::Ref<RHITexture>& previous2)
	{
		const bool diffuse = signal.Type == SignalParams::Kind::Diffuse;
		const Ref<RHIPipeline>& pipeline = fresh2 ? s_Data->SignalAccumulateDiffusePairPipeline
										 : diffuse ? s_Data->SignalAccumulateDiffusePipeline
												   : s_Data->ReflectionAccumulatePipeline;
""")
s = rep(s, nl, """		inputs->SetTexture(6, velocity ? velocity : TextureLoader::TransparentBlack(*s_Data->Device),
						   s_Data->PointSampler);
		inputs->Commit();
""", """		inputs->SetTexture(6, velocity ? velocity : TextureLoader::TransparentBlack(*s_Data->Device),
						   s_Data->PointSampler);
		if (fresh2)
		{
			inputs->SetTexture(7, fresh2, s_Data->PointSampler);
			inputs->SetTexture(8, previous2 && hasHistory ? previous2 : fresh2, historySampler);
		}
		inputs->Commit();
""")
# BlurSignal: the twin
s = rep(s, nl, """								int stride)
	{
		const bool diffuse = signal.Type == SignalParams::Kind::Diffuse;
		const Ref<RHIPipeline>& pipeline = diffuse ? s_Data->SignalBlurDiffusePipeline
												   : s_Data->ReflectionBlurPipeline;
""", """								int stride,
								const RHI::Ref<RHITexture>& accumulated2)
	{
		const bool diffuse = signal.Type == SignalParams::Kind::Diffuse;
		const Ref<RHIPipeline>& pipeline = accumulated2 ? s_Data->SignalBlurDiffusePairPipeline
										 : diffuse ? s_Data->SignalBlurDiffusePipeline
												   : s_Data->ReflectionBlurPipeline;
""")
s = rep(s, nl, """		inputs->SetTexture(3, imageDistance, s_Data->PointSampler);
		inputs->Commit();
""", """		inputs->SetTexture(3, imageDistance, s_Data->PointSampler);
		if (accumulated2)
			inputs->SetTexture(4, accumulated2, s_Data->PointSampler);
		inputs->Commit();
""")
save(p, s)

# ---------------------------------------------------------------- TemporalHistory
p = 'RageV/src/RageV/Renderer/TemporalHistory.h'; s, nl = load(p)
s = rep(s, nl, """					 RHI::Format thirdFormat = RHI::Format::Undefined);""",
       """					 RHI::Format thirdFormat = RHI::Format::Undefined,
					 RHI::Format fourthFormat = RHI::Format::Undefined);""")
s = rep(s, nl, """		RHI::Format m_ThirdFormat = RHI::Format::Undefined;
""", """		RHI::Format m_ThirdFormat = RHI::Format::Undefined;
		RHI::Format m_FourthFormat = RHI::Format::Undefined;   // the pair kind's twin (RT-first T5)
"""); save(p, s)
p = 'RageV/src/RageV/Renderer/TemporalHistory.cpp'; s, nl = load(p)
s = rep(s, nl, """								  Format thirdFormat)
	{""", """								  Format thirdFormat, Format fourthFormat)
	{""")
s = rep(s, nl, """			&& m_ThirdFormat == thirdFormat)
			return;""", """			&& m_ThirdFormat == thirdFormat && m_FourthFormat == fourthFormat)
			return;""")
s = rep(s, nl, """		if (thirdFormat != Format::Undefined)
			desc.ColorAttachments.push_back({ thirdFormat });
""", """		if (thirdFormat != Format::Undefined)
			desc.ColorAttachments.push_back({ thirdFormat });
		if (fourthFormat != Format::Undefined)
			desc.ColorAttachments.push_back({ fourthFormat });
""")
s = rep(s, nl, """		m_ThirdFormat = thirdFormat;
""", """		m_ThirdFormat = thirdFormat;
		m_FourthFormat = fourthFormat;
"""); save(p, s)

# ---------------------------------------------------------------- FrameGraphBuilder.cpp
p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'; s, nl = load(p)
# 1. hoist addSignal + SignalPassNames above the G-buffer pass, one tab out
start = "\t\t\tstruct SignalPassNames { const char* Accumulate; const char* Blur[3]; };".replace('\n', nl)
endmark = ("\t\t\t\treturn blurred;" + nl + "\t\t\t};" + nl)
a = s.find(start); assert a >= 0
b = s.find(endmark, a); assert b >= 0
b += len(endmark)
block = s[a:b]
s = s[:a] + s[b:]
dedented = block.replace(nl + '\t\t\t', nl + '\t\t')
dedented = dedented[1:] if dedented.startswith('\t') else dedented   # the first line loses a tab too
# the pair: fresh's second attachment, the history's fourth, the blur's second
dedented = rep(dedented, nl, """CameraMotion* motion, RGTargetDesc blurDesc) -> RGResource""",
               """CameraMotion* motion, RGTargetDesc blurDesc, bool pair) -> RGResource""")
dedented = rep(dedented, nl, """				[params, fresh, sceneHDR, normalIndex, velocityIndex, current, previous, hasHistory, motion]
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
			blurDesc.ExtraColors.clear();""",
"""				[params, fresh, sceneHDR, normalIndex, velocityIndex, current, previous, hasHistory, motion, pair]
				(RGPassContext& context)
				{
					Renderer3D::AccumulateSignal(params,
						context.Color(fresh), context.Depth(sceneHDR),
						context.Color(sceneHDR, normalIndex),
						hasHistory ? context.Color(previous) : nullptr,
						hasHistory ? context.Color(previous, 1) : nullptr,
						hasHistory ? context.Color(previous, 2) : nullptr,
						context.Color(sceneHDR, velocityIndex),
						*motion, hasHistory,
						pair ? context.Color(fresh, 1) : nullptr,
						pair && hasHistory ? context.Color(previous, 3) : nullptr);
				});
			blurDesc.ExtraColors.clear();
			if (pair)
				blurDesc.ExtraColors.push_back(blurDesc.Color);""")
dedented = rep(dedented, nl, """					[params, input, current, sceneHDR, normalIndex, stride](RGPassContext& context)
					{
						Renderer3D::BlurSignal(params, context.Color(input),
											   context.Depth(sceneHDR),
											   context.Color(sceneHDR, normalIndex),
											   context.Color(current, 1),
											   stride);
					});""",
"""					[params, input, current, sceneHDR, normalIndex, stride, pair](RGPassContext& context)
					{
						Renderer3D::BlurSignal(params, context.Color(input),
											   context.Depth(sceneHDR),
											   context.Color(sceneHDR, normalIndex),
											   context.Color(current, 1),
											   stride,
											   pair ? context.Color(input, pair && input == current ? 3 : 1) : nullptr);
					});""")
anchor = "\t\tconst bool gbufferPass = Renderer3D::GBufferPassAvailable();".replace('\n', nl)
assert s.count(anchor) == 1
s = s.replace(anchor, "\t\t// The reconstruction contract as passes (RT-first T4), for any signal:" + nl
              + "\t\t// the accumulate and the three a-trous blurs. Above the G-buffer pass" + nl
              + "\t\t// because the direct light (T5) runs between it and the lit pass." + nl
              + dedented + anchor)
# the reflection call passes no pair
s = rep(s, nl, """															reflectionHistory, &desc.Reflections->Motion(),
															reflectionBlurDesc);""",
       """															reflectionHistory, &desc.Reflections->Motion(),
															reflectionBlurDesc, false);""")
# 2. the direct light on the contract
s = rep(s, nl, """		RGResource directTraced = kRGInvalid;
		if (directSignal)
		{""", """		RGResource directTraced = kRGInvalid;
		RGResource directLit = kRGInvalid;      // what the lit pass adds: the blurred pair
		RGResource currentDirect = kRGInvalid;  // the accumulated pair, for the debug views
		if (directSignal)
		{""")
s = rep(s, nl, """												 Format::R16G16B16A16_SFLOAT,
												 directView, directRays);
				});
		}
""", """												 Format::R16G16B16A16_SFLOAT,
												 directView, directRays);
				});
			// The contract: the pair accumulated over the frames behind it
			// (surface reprojection, the tests, the bound, the motion-capped
			// memory) and blurred while young. Four attachments: the diffuse,
			// the surface, the extra, the specular twin.
			directLit = directTraced;
			TemporalHistory& direct = *desc.DirectLight;
			direct.Prepare(Renderer::GetDevice(),
						   desc.Width * (uint32_t)supersample, desc.Height * (uint32_t)supersample,
						   Format::R16G16B16A16_SFLOAT, "DirectLight",
						   Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT,
						   Format::R16G16B16A16_SFLOAT);
			if (direct.Current() && direct.Previous())
			{
				const RGResource previousDirect = graph.Import(direct.Previous(), "DirectPrevious");
				currentDirect = graph.Import(direct.Current(), "DirectCurrent");
				const bool directHistory = direct.HasHistory();
				RGTargetDesc directBlurDesc = directDesc;
				directBlurDesc.Name = "DirectBlurred";
				static const SignalPassNames kDirectPasses =
					{ "DirectAccumulate", { "DirectBlur", "DirectBlur2", "DirectBlur4" } };
				directLit = addSignal(kDirectPasses, Renderer3D::DirectSignal(),
									  directTraced, currentDirect, previousDirect, directHistory,
									  &direct.Motion(), directBlurDesc, true);
				direct.Advance();
			}
		}
		else if (desc.DirectLight)
		{
			desc.DirectLight->Invalidate();
		}
""")
s = rep(s, nl, """				if (directTraced != kRGInvalid)
					builder.Sample(directTraced);
			},
			gbufferPass ? std::function<void(RGPassContext&)>(
							  [drawLit = desc.DrawSceneLit, jitter, directTraced,""",
       """				if (directLit != kRGInvalid)
					builder.Sample(directLit);
			},
			gbufferPass ? std::function<void(RGPassContext&)>(
							  [drawLit = desc.DrawSceneLit, jitter, directLit,""")
s = rep(s, nl, """								  Renderer3D::SetDirectLight(
									  directTraced != kRGInvalid ? context.Color(directTraced, 0) : nullptr,
									  directTraced != kRGInvalid ? context.Color(directTraced, 1) : nullptr);""",
       """								  Renderer3D::SetDirectLight(
									  directLit != kRGInvalid ? context.Color(directLit, 0) : nullptr,
									  directLit != kRGInvalid ? context.Color(directLit, 1) : nullptr);""")
# 3. the debug views
s = rep(s, nl, """							  : view == EngineConfig::DebugViewMode::ReflectionPicture ? 4.0f
									: 1.0f;""", """							  : view == EngineConfig::DebugViewMode::ReflectionPicture ? 4.0f
							  // The direct light's accumulated diffuse over four; its
							  // refusal reason on the same ramp as the reflections'.
							  : view == EngineConfig::DebugViewMode::DirectLight ? 4.0f
							  : view == EngineConfig::DebugViewMode::DirectRefusal ? 6.0f
									: 1.0f;""")
s = rep(s, nl, """										 : reflectionView
											   ? (tracedReflections ? currentReflections : kRGInvalid)
											   : kRGInvalid;""", """										 : reflectionView
											   ? (tracedReflections ? currentReflections : kRGInvalid)
										 : (view == EngineConfig::DebugViewMode::DirectLight
											|| view == EngineConfig::DebugViewMode::DirectRefusal)
											   ? currentDirect
											   : kRGInvalid;""")
s = rep(s, nl, """										 : view == EngineConfig::DebugViewMode::ReflectionChoice ? 2u
										 : 0u;""", """										 : view == EngineConfig::DebugViewMode::ReflectionChoice ? 2u
										 : view == EngineConfig::DebugViewMode::DirectRefusal ? 2u
										 : 0u;""")
save(p, s)

# ---------------------------------------------------------------- EngineConfig: the views
p = 'RageV/src/RageV/Core/EngineConfig.h'; s, nl = load(p)
s = rep(s, nl, """								   ReflectionChoice, ReflectionPicture };""",
       """								   ReflectionChoice, ReflectionPicture,
								   DirectLight, DirectRefusal };"""); save(p, s)
p = 'RageV/src/RageV/Core/EngineConfig.cpp'; s, nl = load(p)
s = rep(s, nl, """			else if (lowered == "reflection-picture" || lowered == "reflectionpicture")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionPicture;
""", """			else if (lowered == "reflection-picture" || lowered == "reflectionpicture")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionPicture;
			// RT-first T5: the direct light's accumulated diffuse (before the
			// albedo, over four), and the reason its history was refused.
			else if (lowered == "direct-light" || lowered == "directlight")
				config.DebugView = EngineConfig::DebugViewMode::DirectLight;
			else if (lowered == "direct-refusal" || lowered == "directrefusal")
				config.DebugView = EngineConfig::DebugViewMode::DirectRefusal;
"""); save(p, s)
p = 'RageVEditor/assets/shaders/debug_view.rvshader'; s, nl = load(p)
s = rep(s, nl, """	else if (mode == 8)""", """	else if (mode == 8 || mode == 10)   // reflection-picture, direct-light: the picture over the scale""")
save(p, s)
print('T5f patched')
