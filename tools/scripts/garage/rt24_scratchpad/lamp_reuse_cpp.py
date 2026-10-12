"""Step 4: the C++ -- the trace's sixth target, the resolve's inputs, and --reflection-lamp-pool."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io

R = (_RAGEV + '/RageV/src/RageV/')


def edit(path, a, b):
    t = io.open(R + path, encoding='utf-8', newline='').read()
    crlf = t.count('\r\n') > t.count('\n') // 2
    t = t.replace('\r\n', '\n')
    assert t.count(a) == 1, (path, t.count(a), a[:80])
    t = t.replace(a, b)
    io.open(R + path, 'w', encoding='utf-8', newline='').write(t.replace('\n', '\r\n') if crlf else t)


# -- the setting ---------------------------------------------------------------------------------
edit('Core/EngineConfig.h', """		int ReflectionMovingRays = -1;
""", """		int ReflectionMovingRays = -1;
		// **--reflection-lamp-pool=<neighbours> (RT-24, 2026-09-25):** how many neighbours' lamp
		// points each texel pools, ReSTIR's way (reflection_resolve.rvshader, "the lamp points,
		// pooled"): each point weighed from the texel's own position, one kept, one shadow ray.
		// Zero keeps each texel's own aimed sample alone (fix 1 of 2026-09-24). At most eight.
		int ReflectionLampPool = 4;
""")
edit('Core/EngineConfig.cpp', """		// RT-15b: the rays a texel casts where its surface moves on its own.
		if (key == "reflection-moving-rays" || key == "reflectionmovingrays")
""", """		// RT-24: how many neighbours' lamp points a reflection texel pools.
		if (key == "reflection-lamp-pool" || key == "reflectionlamppool")
		{
			try { config.ReflectionLampPool = std::stoi(value); }
			catch (...) { RV_CORE_WARN("reflection-lamp-pool expects a count; got '{0}'", value); return false; }
			return true;
		}
		// RT-15b: the rays a texel casts where its surface moves on its own.
		if (key == "reflection-moving-rays" || key == "reflectionmovingrays")
""")

# -- the trace's pipeline: a sixth attachment, 32 bits a lane -------------------------------------
edit('Renderer/Renderer3D.cpp', """			// **RT-24: and the trace a fifth** -- the light its aimed samples brought, apart
			// from the lobe's, so the resolve shares only what a re-aimed ray carried.
			const int extras = pass == 0 ? 4 : pass == 1 ? 1""", """			// **RT-24: and the trace a fifth** -- the light its aimed samples brought, apart
			// from the lobe's, so the resolve shares only what a re-aimed ray carried.
			// **RT-24 (2026-09-25): and a sixth** -- the draw behind its first aimed sample, for
			// the resolve to pool ReSTIR's way; 32 bits a lane, because the resolve divides by it.
			const int extras = pass == 0 ? 5 : pass == 1 ? 1""")
edit('Renderer/Renderer3D.cpp', """				reflection.ColorFormats.push_back(pass == 0 && extra == 1 ? Format::R16G16_SFLOAT
""", """				reflection.ColorFormats.push_back(pass == 0 && extra == 1 ? Format::R16G16_SFLOAT
																		 : pass == 0 && extra == 4 ? Format::R32G32B32A32_SFLOAT
""")

# -- the resolve: its inputs ----------------------------------------------------------------------
edit('Renderer/Renderer3D.h', """									   // RT-24: the trace's fifth lane, its aimed samples' light,
									   // added back after the sharing. Null reads as none.
									   const RHI::Ref<RHI::RHITexture>& aimed = nullptr);""",
     """									   // RT-24: the trace's fifth lane, its aimed samples' light,
									   // added back after the sharing. Null reads as none.
									   const RHI::Ref<RHI::RHITexture>& aimed = nullptr,
									   // RT-24 (2026-09-25): the trace's sixth, the draw behind each
									   // texel's aimed sample, and the albedo lane for the tint -- what
									   // the lamp pool needs. Null for either: no pooling.
									   const RHI::Ref<RHI::RHITexture>& lampSample = nullptr,
									   const RHI::Ref<RHI::RHITexture>& albedo = nullptr);""")
edit('Renderer/Renderer3D.cpp', """										const RHI::Ref<RHITexture>& travel,
										const RHI::Ref<RHITexture>& aimed)
	{""", """										const RHI::Ref<RHITexture>& travel,
										const RHI::Ref<RHITexture>& aimed,
										const RHI::Ref<RHITexture>& lampSample,
										const RHI::Ref<RHITexture>& albedo)
	{""")
edit('Renderer/Renderer3D.cpp', """		// RT-24: the aimed samples' light, added back after the sharing. Black: none.
		if (inputs->HasBinding(5))
			inputs->SetTexture(5, aimed ? aimed : TextureLoader::TransparentBlack(*s_Data->Device),
							   s_Data->PointSampler);
		inputs->Commit();
""", """		// RT-24: the aimed samples' light, added back after the sharing. Black: none.
		if (inputs->HasBinding(5))
			inputs->SetTexture(5, aimed ? aimed : TextureLoader::TransparentBlack(*s_Data->Device),
							   s_Data->PointSampler);
		// **RT-24 (2026-09-25): the lamp pool's inputs** -- each texel's draw, the albedo for
		// its tint, and the emitter rows the draws index, bound as the trace binds them. Black
		// draws read as "nothing drawn", which pools nothing.
		const bool lampPool = lampSample && albedo && EngineConfig::Get().ReflectionLampPool > 0;
		if (inputs->HasBinding(6))
			inputs->SetTexture(6, lampPool ? lampSample : TextureLoader::TransparentBlack(*s_Data->Device),
							   s_Data->PointSampler);
		if (inputs->HasBinding(7))
			inputs->SetTexture(7, albedo ? albedo : surface, s_Data->PointSampler);
		bool rowsBound = false;
		if (inputs->HasBinding(8) && inputs->HasBinding(9))
		{
			uint32_t emitterRows = 0;
			uint32_t emitterCdf = 0;
			if (UploadEmitters(slot.ReflectionEmitters, slot.ReflectionEmitterCapacity,
							   slot.ReflectionEmitterCdf, slot.ReflectionEmitterCdfCapacity,
							   emitterRows, emitterCdf))
			{
				inputs->SetStorageBuffer(8, slot.ReflectionEmitters, 0,
										 (uint64_t)emitterRows * sizeof(Renderer3DData::GpuEmitter));
				inputs->SetStorageBuffer(9, slot.ReflectionEmitterCdf, 0,
										 (uint64_t)emitterCdf * sizeof(float));
				rowsBound = true;
			}
		}
		inputs->Commit();
""")
edit('Renderer/Renderer3D.cpp', """		push.Probe.x = ReflectionSignal().FireflySigmas;

		cmd->BindPipeline(s_Data->ReflectionResolvePipeline);""", """		push.Probe.x = ReflectionSignal().FireflySigmas;
		// RT-24 (2026-09-25): y the trace's sampler (visible normals or not), so a lamp point is
		// weighed by the lobe it was drawn against; w the lamp pool's size, none without its inputs.
		push.Probe.y = EngineConfig::Get().ReflectionVndf ? 1.0f : 0.0f;
		push.Probe.w = lampPool && rowsBound && !s_Data->Emitters.empty() && EngineConfig::Get().ReflectionNee
					 ? (float)Math::Clamp(EngineConfig::Get().ReflectionLampPool, 0, 8) : 0.0f;

		cmd->BindPipeline(s_Data->ReflectionResolvePipeline);""")

# -- the frame: a sixth trace target, and the resolves read it --------------------------------------
F = 'Renderer/FrameGraphBuilder.cpp'
edit(F, """			traceDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT, Format::R16G16_SFLOAT,
									  Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT };""",
     """			// RT-24 (2026-09-25): and the draw behind each aimed sample, for the lamp pool.
			traceDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT, Format::R16G16_SFLOAT,
									  Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT,
									  Format::R32G32B32A32_SFLOAT };""")
edit(F, """			glassRayDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT, Format::R16G16_SFLOAT,
										 Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT };""",
     """			// RT-24 (2026-09-25): and the draw behind each aimed sample, as the floor's.
			glassRayDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT, Format::R16G16_SFLOAT,
										 Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT,
										 Format::R32G32B32A32_SFLOAT };""")
edit(F, """				[traced, sceneHDR, normalIndex](RGPassContext& context)
				{
					Renderer3D::ResolveReflections(context.Color(traced),
												   context.Color(traced, 1),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex), false,
												   context.Color(traced, 3),
												   context.Color(traced, 4));""",
     """				[traced, sceneHDR, normalIndex, albedoIndex](RGPassContext& context)
				{
					Renderer3D::ResolveReflections(context.Color(traced),
												   context.Color(traced, 1),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex), false,
												   context.Color(traced, 3),
												   context.Color(traced, 4),
												   // RT-24: the draws, and the albedo for the tint.
												   context.Color(traced, 5),
												   context.Color(sceneHDR, albedoIndex));""")
edit(F, """					Renderer3D::ResolveReflections(context.Color(glassRays),
												   context.Color(glassRays, 1),
												   context.Depth(glassLayer),
												   context.Color(glassLayer, 1), true,
												   context.Color(glassRays, 3),
												   context.Color(glassRays, 4));""",
     """					Renderer3D::ResolveReflections(context.Color(glassRays),
												   context.Color(glassRays, 1),
												   context.Depth(glassLayer),
												   context.Color(glassLayer, 1), true,
												   context.Color(glassRays, 3),
												   context.Color(glassRays, 4),
												   // RT-24: the draws, and the layer's albedo for the tint.
												   context.Color(glassRays, 5),
												   context.Color(glassLayer, 2));""")
print('ok')
