"""RT-first T5, the rest of the C++ side after patch_rt_t5b.py stopped at
the frame graph (its drawGBuffer anchor lacked the comment between the two
lines): FrameGraphBuilder.cpp and the layers. Do not re-run 5b."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')
exec(open('tools/scripts/garage/session_2026_09_06/t4_prelude.py', encoding='utf-8').read())

p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'; s, nl = load(p)
assert 'directSignal' not in s, 'already patched'
s = rep(s, nl, """		auto drawGBuffer = [drawScene](RGPassContext& context)
		{
			Renderer3D::SetGBufferPassActive(true);
			drawScene(context);
			Renderer3D::SetGBufferPassActive(false);
		};
""", """		// RT-first T5: the direct light as a signal -- the DirectTrace pass
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
print('T5c patched')
