"""RT-first T5, C++ side (docs/RT-FIRST.md section 2d):
- EngineConfig: --direct-signal=on|off.
- Renderer3D: the DirectTrace shader/pipeline/inputs, TraceDirectLight,
  SetDirectSignal (the RayRates.w bit 22 the lit shader reads), SetDirectLight
  (the two textures re-committed on the six lit-kind sets in DrawLit -- legal
  because none of them is bound before the Scene pass in split mode).
- FrameGraphBuilder: the DirectTrace pass between "GBuffer" and "Scene", the
  Scene pass sampling it and handing it to the lit draw; FrameDesc.DirectLight.
- The layers: the history slot (unused until the pair contract lands).
"""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')
exec(open('tools/scripts/garage/session_2026_09_06/t4_prelude.py', encoding='utf-8').read())

# ---------------------------------------------------------------- EngineConfig
p = 'RageV/src/RageV/Core/EngineConfig.h'; s, nl = load(p)
s = rep(s, nl, """		bool  WaterLampPass = true;
""", """		bool  WaterLampPass = true;
		// --direct-signal=on|off (RT-first T5): whether the direct light of
		// every opaque pixel is chosen, shaded and traced in the DirectTrace
		// pass from the G-buffer (on, the default) or by the lit shader's own
		// loop, a ray to every light (off -- the reference arm and the A/B).
		bool  DirectSignal = true;
"""); save(p, s)
p = 'RageV/src/RageV/Core/EngineConfig.cpp'; s, nl = load(p)
s = rep(s, nl, """		if (key == "water-lamp-pass" || key == "waterlamppass")
			return ParseBool(value, config.WaterLampPass);
""", """		if (key == "water-lamp-pass" || key == "waterlamppass")
			return ParseBool(value, config.WaterLampPass);

		if (key == "direct-signal" || key == "directsignal")
			return ParseBool(value, config.DirectSignal);
"""); save(p, s)

# ---------------------------------------------------------------- Renderer3D.h
p = 'RageV/src/RageV/Renderer/Renderer3D.h'; s, nl = load(p)
s = rep(s, nl, """		static bool IsGBufferPassActive();
""", """		static bool IsGBufferPassActive();
		// RT-first T5: whether the DirectTrace pass runs this frame, told to
		// the scene block (RayRates.w bit 22) before BeginScene fills it.
		static void SetDirectSignal(bool requested);
""")
s = rep(s, nl, """		static void SetWaterLamps(const RHI::Ref<RHI::RHITexture>& diffuse,
								  const RHI::Ref<RHI::RHITexture>& specular);
""", """		static void SetWaterLamps(const RHI::Ref<RHI::RHITexture>& diffuse,
								  const RHI::Ref<RHI::RHITexture>& specular);
		// RT-first T5: the direct light for the lit draw -- the DirectTrace
		// pass's pair (or its accumulated form), added by the lit shader where
		// its loop used to trace. Set by the "Scene" pass before the lit draw
		// and cleared after; committed onto the lit sets in DrawLit.
		static void SetDirectLight(const RHI::Ref<RHI::RHITexture>& diffuse,
								   const RHI::Ref<RHI::RHITexture>& specular);
""")
s = rep(s, nl, """		static bool CanTraceGlobalIllumination();
""", """		static bool CanTraceGlobalIllumination();
		// RT-first T5: the direct light of every opaque pixel, chosen (K per
		// pixel; zero shades every light), shaded and traced from the G-buffer
		// into two pictures (diffuse before the albedo, specular complete).
		static void TraceDirectLight(RHI::RHICommandList& cmd,
									 const RHI::Ref<RHI::RHITexture>& depth,
									 const RHI::Ref<RHI::RHITexture>& surface,
									 const RHI::Ref<RHI::RHITexture>& albedo,
									 const RHI::Ref<RHI::RHITexture>& surfaceId,
									 RHI::Format targetColor,
									 const GiTraceView& view, int rays);
		static bool CanTraceDirectLight();
"""); save(p, s)

# ---------------------------------------------------------------- Renderer3D.cpp
p = 'RageV/src/RageV/Renderer/Renderer3D.cpp'; s, nl = load(p)
# members
s = rep(s, nl, """			Ref<RHIShader>   GiShader;
			Ref<RHIPipeline> GiPipeline;
""", """			Ref<RHIShader>   GiShader;
			Ref<RHIPipeline> GiPipeline;
			// RT-first T5: the direct-light trace pass, and whether the frame
			// graph runs it this frame (the lit shader's switch).
			Ref<RHIShader>   DirectShader;
			Ref<RHIPipeline> DirectPipeline;
			bool             DirectSignalRequested = false;
""")
s = rep(s, nl, """			Ref<RHITexture>  WaterLampSpecular;
""", """			Ref<RHITexture>  WaterLampSpecular;
			// RT-first T5: the direct light the lit draw adds (SetDirectLight).
			Ref<RHITexture>  DirectDiffuse;
			Ref<RHITexture>  DirectSpecular;
""")
s = rep(s, nl, """				Ref<RHIResourceSet> GiInputs;
""", """				Ref<RHIResourceSet> GiInputs;
				Ref<RHIResourceSet> DirectInputs;   // RT-first T5: the G-buffer, set 3
""")
# the shader: loaded beside the GI trace, dropped with it
s = rep(s, nl, """		s_Data->GiShader = nullptr;
""", """		s_Data->GiShader = nullptr;
		s_Data->DirectShader = nullptr;
		s_Data->DirectPipeline = nullptr;
""")
s = rep(s, nl, """			if (auto gi = ShaderCompiler::CompileFromFile("assets/shaders/rtgi_trace.rvshader",
														 traceDefines))
""", """			// RT-first T5: the direct light from the G-buffer, under rays only.
			if (s_Data->RayShadowsOn)
			{
				if (auto direct = ShaderCompiler::CompileFromFile("assets/shaders/direct_trace.rvshader",
																 traceDefines))
				{
					s_Data->DirectShader = s_Data->Device->CreateShader(*direct);
				}
				else
				{
					RV_CORE_ERROR("Renderer3D: assets/shaders/direct_trace.rvshader did not compile; "
								  "the lit shader keeps tracing a shadow ray to every light");
				}
			}
			if (auto gi = ShaderCompiler::CompileFromFile("assets/shaders/rtgi_trace.rvshader",
														 traceDefines))
""")
# the lit sets carry the two bindings (black until the lit draw fills them)
s = rep(s, nl, """		if (s_Data->RayShadowsOn)
			sceneSet->SetAccelerationStructure(RayShadows::kBinding, RayShadows::GetStructure());
""", """		// RT-first T5: the direct-light pair, on the lit-kind sets alone -- the
		// only pipelines whose fragment references the bindings (a binding a
		// pipeline does not reference is not in its layout). Black here; the
		// frame's pictures are committed in DrawLit, after the pass has run.
		if (s_Data->RayShadowsOn
			&& (sceneSet == slot.Set || sceneSet == slot.SkinnedSet || sceneSet == slot.LayeredSet
				|| sceneSet == slot.MaskedSet || sceneSet == slot.GpuSet || sceneSet == slot.MaskedGpuSet))
		{
			sceneSet->SetTexture(26, TextureLoader::TransparentBlack(*s_Data->Device), s_Data->PointSampler);
			sceneSet->SetTexture(27, TextureLoader::TransparentBlack(*s_Data->Device), s_Data->PointSampler);
		}
		if (s_Data->RayShadowsOn)
			sceneSet->SetAccelerationStructure(RayShadows::kBinding, RayShadows::GetStructure());
""")
# the switch in the scene block
s = rep(s, nl, """						+ 1048576 * config.LightSamplingTarget));""",
"""						+ 1048576 * config.LightSamplingTarget
						// bit 22: the DirectTrace pass runs this frame (RT-first T5).
						+ (s_Data->DirectSignalRequested ? 4194304 : 0)));""")
# DrawLit: the frame's pictures onto the lit sets, then the draw
s = rep(s, nl, """		const bool haveIndirect = s_Data->IndirectView.IsValid() && !s_Data->IndirectSlots.empty();
		DrawLitBody(cmd, slot, (uint32_t)s_Data->Pending.size(), haveIndirect);
""", """		const bool haveIndirect = s_Data->IndirectView.IsValid() && !s_Data->IndirectSlots.empty();
		// RT-first T5: this frame's direct light onto the lit-kind sets. Legal
		// here and nowhere earlier: in split mode the G-buffer pass binds only
		// the G-buffer and prepass sets, so none of these has been bound yet,
		// and a set rewritten after a bind is what the RHI refuses.
		if (s_Data->RayShadowsOn && s_Data->DirectDiffuse && s_Data->DirectSpecular)
		{
			const Ref<RHIResourceSet> litSets[] = { slot.Set, slot.SkinnedSet, slot.LayeredSet,
													slot.MaskedSet, slot.GpuSet, slot.MaskedGpuSet };
			for (const Ref<RHIResourceSet>& litSet : litSets)
			{
				if (!litSet)
					continue;
				litSet->SetTexture(26, s_Data->DirectDiffuse, s_Data->PointSampler);
				litSet->SetTexture(27, s_Data->DirectSpecular, s_Data->PointSampler);
				litSet->Commit();
			}
		}
		DrawLitBody(cmd, slot, (uint32_t)s_Data->Pending.size(), haveIndirect);
""")
# the setters and the pass, after SetWaterLamps
s = rep(s, nl, """		s_Data->WaterLampDiffuse = diffuse;
		s_Data->WaterLampSpecular = specular;
	}
""", """		s_Data->WaterLampDiffuse = diffuse;
		s_Data->WaterLampSpecular = specular;
	}

	void Renderer3D::SetDirectSignal(bool requested)
	{
		if (s_Data)
			s_Data->DirectSignalRequested = requested;
	}

	void Renderer3D::SetDirectLight(const RHI::Ref<RHITexture>& diffuse,
									const RHI::Ref<RHITexture>& specular)
	{
		if (!s_Data)
			return;
		s_Data->DirectDiffuse = diffuse;
		s_Data->DirectSpecular = specular;
	}

	bool Renderer3D::CanTraceDirectLight()
	{
		return s_Data && s_Data->DirectShader != nullptr;
	}

	// RT-first T5 (docs/RT-FIRST.md 2d): the shape of TraceGlobalIllumination
	// -- set 0 is the scene's trace-only set, set 3 the G-buffer, the view in
	// the push block -- with two colour targets.
	void Renderer3D::TraceDirectLight(RHICommandList& cmd,
									  const Ref<RHITexture>& depth,
									  const Ref<RHITexture>& surface,
									  const Ref<RHITexture>& albedo,
									  const Ref<RHITexture>& surfaceId,
									  Format targetColor,
									  const GiTraceView& view, int rays)
	{
		if (!s_Data || !s_Data->DirectShader || !depth || !surface || !albedo || !surfaceId)
			return;
		if (!s_Data->ActiveScene)
			return;
		if (!s_Data->DirectPipeline)
		{
			GraphicsPipelineDesc direct;
			direct.Name = "Renderer3D.direct.trace";
			direct.Shader = s_Data->DirectShader;
			direct.Topology = PrimitiveTopology::TriangleList;
			direct.Rasterizer.Cull = CullMode::None;
			direct.Blend = BlendPreset::Opaque;
			direct.DepthStencil.DepthTestEnable = false;
			direct.DepthStencil.DepthWriteEnable = false;
			direct.ColorFormats = { targetColor, targetColor };
			direct.BlendPerAttachment = { BlendPreset::Opaque, BlendPreset::Opaque };
			direct.DepthFormat = Format::Undefined;
			s_Data->DirectPipeline = s_Data->Device->CreatePipeline(direct);
			for (auto& frame : s_Data->SceneSlots)
				for (auto& slot : frame)
					slot.DirectInputs = nullptr;
		}
		if (!s_Data->DirectPipeline)
			return;
		Renderer3DData::SceneSlot& slot = *s_Data->ActiveScene;
		if (!slot.LampSet)
			return;
		if (!slot.DirectInputs)
			slot.DirectInputs = s_Data->Device->CreateResourceSet(s_Data->DirectPipeline, 3);
		if (!slot.DirectInputs)
			return;
		slot.DirectInputs->SetTexture(0, depth, s_Data->PointSampler);
		slot.DirectInputs->SetTexture(1, surface, s_Data->PointSampler);
		slot.DirectInputs->SetTexture(2, albedo, s_Data->PointSampler);
		slot.DirectInputs->SetTexture(3, surfaceId, s_Data->PointSampler);
		slot.DirectInputs->Commit();

		struct DirectParams
		{
			float NearClip, FarClip, InvP0, InvP1;
			float FlipY, Rays, Frame, Animated;
			Vec4  CameraRow0, CameraRow1, CameraRow2, CameraPosition;
		} params{};
		params.NearClip = view.NearClip;
		params.FarClip = view.FarClip;
		params.InvP0 = view.InvProjection0;
		params.InvP1 = view.InvProjection1;
		params.FlipY = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
		params.Rays = (float)Math::Clamp(rays, 0, 8);
		// The same frame index and the same "is there a filter" test the lit
		// shader's own draws use, so a sample walks exactly as it did there.
		params.Frame = s_Data->Scene.GlobalIllumination.y;
		const Vec4& jitter = s_Data->Scene.Jitter;
		params.Animated = (jitter.x != 0.0f || jitter.y != 0.0f || jitter.z != 0.0f || jitter.w != 0.0f)
						? 1.0f : 0.0f;
		const Mat4 camera = Math::Inverse(view.View);
		params.CameraRow0 = Vec4(camera[0][0], camera[1][0], camera[2][0], 0.0f);
		params.CameraRow1 = Vec4(camera[0][1], camera[1][1], camera[2][1], 0.0f);
		params.CameraRow2 = Vec4(camera[0][2], camera[1][2], camera[2][2], 0.0f);
		params.CameraPosition = Vec4(camera[3][0], camera[3][1], camera[3][2], 0.0f);

		cmd.BindPipeline(s_Data->DirectPipeline);
		cmd.BindResourceSet(0, slot.LampSet);
		if (s_Data->Heap)
			cmd.BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
		cmd.BindResourceSet(3, slot.DirectInputs);
		cmd.PushConstants(ShaderStage::Fragment, 0, sizeof(params), &params);
		cmd.Draw(3);
	}
""")
save(p, s)

# ---------------------------------------------------------------- FrameGraphBuilder.h
p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.h'; s, nl = load(p)
s = rep(s, nl, """		TemporalHistory* Reflections = nullptr;
""", """		TemporalHistory* Reflections = nullptr;

		// RT-first T5: where the direct light keeps its accumulated pair. Null
		// means the DirectTrace pass cannot run for this caller and the lit
		// shader traces its own rays, as a probe capture and scenetest want.
		TemporalHistory* DirectLight = nullptr;
"""); save(p, s)

# ---------------------------------------------------------------- FrameGraphBuilder.cpp
p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'; s, nl = load(p)
s = rep(s, nl, """		const bool gbufferPass = Renderer3D::GBufferPassAvailable();
		auto drawGBuffer = [drawScene](RGPassContext& context)
		{
			Renderer3D::SetGBufferPassActive(true);
			drawScene(context);
			Renderer3D::SetGBufferPassActive(false);
		};
""", """		const bool gbufferPass = Renderer3D::GBufferPassAvailable();
		// RT-first T5: the direct light as a signal -- the DirectTrace pass
		// between the G-buffer and the lit pass, K lights per pixel (the
		// preset's count, or --light-sampling), the lit shader adding its two
		// pictures. Needs the split, rays, a place to keep the history, the
		// shader, and not --direct-signal=off (the reference arm).
		const bool directSignal = gbufferPass && ResolveRayTracing(desc.Render)
							   && config.DirectSignal && desc.DirectLight != nullptr
							   && Renderer3D::CanTraceDirectLight();
		const int directRays = config.HasLightSamplingOverride ? config.LightSampling
															   : rtPreset.Lamps;
		auto drawGBuffer = [drawScene, directSignal](RGPassContext& context)
		{
			Renderer3D::SetGBufferPassActive(true);
			// Told before BeginScene fills the scene block, whose RayRates.w
			// bit 22 is the lit shader's switch.
			Renderer3D::SetDirectSignal(directSignal);
			drawScene(context);
			Renderer3D::SetDirectSignal(false);
			Renderer3D::SetGBufferPassActive(false);
		};
""")
s = rep(s, nl, """				drawGBuffer);
		}
""", """				drawGBuffer);
		}
		RGResource directTraced = kRGInvalid;
		if (directSignal)
		{
			RGTargetDesc directDesc;
			directDesc.Name = "DirectTrace";
			directDesc.Color = Format::R16G16B16A16_SFLOAT;
			directDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT };
			directDesc.Depth = Format::Undefined;
			directDesc.Scale = (float)supersample;
			directTraced = graph.CreateTarget(directDesc);
			Renderer3D::GiTraceView directView;
			directView.NearClip = desc.NearClip;
			directView.FarClip = desc.FarClip;
			directView.InvProjection0 = desc.InvProjection0;
			directView.InvProjection1 = desc.InvProjection1;
			directView.View = desc.View;
			graph.AddPass("DirectTrace",
				[&](RGPassBuilder& builder)
				{
					builder.Write(directTraced);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[sceneHDR, normalIndex, albedoIndex, surfaceIdIndex, directView, directRays]
				(RGPassContext& context)
				{
					Renderer3D::TraceDirectLight(context.Cmd,
												 context.Depth(sceneHDR),
												 context.Color(sceneHDR, normalIndex),
												 context.Color(sceneHDR, albedoIndex),
												 context.Color(sceneHDR, surfaceIdIndex),
												 Format::R16G16B16A16_SFLOAT,
												 directView, directRays);
				});
		}
""")
s = rep(s, nl, """				if (previousIndirect != kRGInvalid)
					builder.Sample(previousIndirect);
			},
			gbufferPass ? std::function<void(RGPassContext&)>(
							  [drawLit = desc.DrawSceneLit, jitter,
							   motion = desc.History ? &desc.History->Motion() : nullptr](RGPassContext& context)
""", """				if (previousIndirect != kRGInvalid)
					builder.Sample(previousIndirect);
				if (directTraced != kRGInvalid)
					builder.Sample(directTraced);
			},
			gbufferPass ? std::function<void(RGPassContext&)>(
							  [drawLit = desc.DrawSceneLit, jitter, directTraced,
							   motion = desc.History ? &desc.History->Motion() : nullptr](RGPassContext& context)
""")
s = rep(s, nl, """								  if (drawLit)
									  drawLit(context);
								  else
									  Renderer3D::DrawLit();
""", """								  // RT-first T5: the direct light for the lit draw.
								  Renderer3D::SetDirectLight(
									  directTraced != kRGInvalid ? context.Color(directTraced, 0) : nullptr,
									  directTraced != kRGInvalid ? context.Color(directTraced, 1) : nullptr);
								  if (drawLit)
									  drawLit(context);
								  else
									  Renderer3D::DrawLit();
								  Renderer3D::SetDirectLight(nullptr, nullptr);
""")
save(p, s)

# ---------------------------------------------------------------- the layers
p = 'RageVRuntime/src/RuntimeLayer.h'; s, nl = load(p)
s = rep(s, nl, """	RageV::TemporalHistory m_Reflections;
""", """	RageV::TemporalHistory m_Reflections;
	RageV::TemporalHistory m_DirectLight;   // RT-first T5
"""); save(p, s)
p = 'RageVRuntime/src/RuntimeLayer.cpp'; s, nl = load(p)
s = rep(s, nl, """	frame.Reflections = &m_Reflections;
""", """	frame.Reflections = &m_Reflections;
	frame.DirectLight = &m_DirectLight;
"""); save(p, s)
p = 'RageVEditor/src/EditorLayer.h'; s, nl = load(p)
s = rep(s, nl, """	RageV::TemporalHistory m_SceneReflections;
	RageV::TemporalHistory m_GameReflections;
""", """	RageV::TemporalHistory m_SceneReflections;
	RageV::TemporalHistory m_GameReflections;
	RageV::TemporalHistory m_SceneDirectLight;   // RT-first T5
	RageV::TemporalHistory m_GameDirectLight;
"""); save(p, s)
p = 'RageVEditor/src/EditorLayer.cpp'; s, nl = load(p)
s = rep(s, nl, """	scene.Reflections = &m_SceneReflections;
""", """	scene.Reflections = &m_SceneReflections;
	scene.DirectLight = &m_SceneDirectLight;
""")
s = rep(s, nl, """		game.Reflections = &m_GameReflections;
""", """		game.Reflections = &m_GameReflections;
		game.DirectLight = &m_GameDirectLight;
"""); save(p, s)
print('T5b patched')
