"""RT-2b: ambient occlusion as a signal from the G-buffer (docs/RT-SERIES.md RT-2).
- The AO (RTAO under rays, SSAO in raster) is computed before the lit pass
  from the G-buffer's depth and normal, upsampled depth-aware to full
  resolution (the apply shader against a white scene, the intensity folded
  in), denoised on the reconstruction contract (slot 2), and read by the lit
  shader at set 0 binding 28 -- applied to the ambient, the stored indirect
  and the environment's specular only, never to the direct light.
- --ao-signal=on|off keeps the old post-apply chain as the A/B;
  --debug-view=ao shows the accumulated signal.
"""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')
exec(open('tools/scripts/garage/session_2026_09_06/t4_prelude.py', encoding='utf-8').read())

# ---------------------------------------------------------------- the lit shader
p = 'RageVEditor/assets/shaders/include/pbr_fragment.glsl'; s, nl = load(p)
assert 'RV_SCREEN_OCCLUSION_INPUT' not in s
s = rep(s, nl, """#define RV_DIRECT_SIGNAL_INPUT
layout(set = 0, binding = 26) uniform sampler2D u_DirectDiffuse;
layout(set = 0, binding = 27) uniform sampler2D u_DirectSpecular;
#endif
""", """#define RV_DIRECT_SIGNAL_INPUT
layout(set = 0, binding = 26) uniform sampler2D u_DirectDiffuse;
layout(set = 0, binding = 27) uniform sampler2D u_DirectSpecular;
#endif
// **RT-2: the screen-space occlusion as a signal.** RTAO under rays, SSAO in
// raster, computed from the G-buffer before this pass and settled on the
// reconstruction contract; the intensity is folded in already. Read by texel
// and applied below to the ambient, the stored indirect and the environment's
// specular -- never to the direct light, which has its own shadow rays. The
// same lit variants as the direct pair declare it, rays or not.
#if !defined(RV_TRACE_ONLY) && !defined(RV_GBUFFER) && !defined(RV_TRANSPARENT) \\
	&& !defined(RV_WATER) && !defined(RV_IRRADIANCE_FILL)
#define RV_SCREEN_OCCLUSION_INPUT
layout(set = 0, binding = 28) uniform sampler2D u_ScreenOcclusion;
#endif
""")
s = rep(s, nl, """	vec3 ambient = kD * albedo *
		   ((ambientLight + skyDiffuse) * skyVisible + irradiance) * occlusion;
""", """	// RT-2: the screen-space occlusion signal, where the frame graph ran it
	// (RayRates.w bit 23); one otherwise, and the post-pass applies instead.
#ifdef RV_SCREEN_OCCLUSION_INPUT
	const float screenOcclusion = (int(u_Scene.RayRates.w + 0.5) & 8388608) != 0
								? clamp(texelFetch(u_ScreenOcclusion, ivec2(gl_FragCoord.xy), 0).r, 0.0, 1.0)
								: 1.0;
#else
	const float screenOcclusion = 1.0;
#endif
	vec3 ambient = kD * albedo *
		   ((ambientLight + skyDiffuse) * skyVisible + irradiance) * occlusion * screenOcclusion;
""")
s = rep(s, nl, """	o_Indirect = vec4(kD * albedo * indirectTerm * occlusion, 1.0);""",
       """	o_Indirect = vec4(kD * albedo * indirectTerm * occlusion * screenOcclusion, 1.0);""")
s = rep(s, nl, """	ambient += prefiltered * (F0 * envBRDF.x + envBRDF.y) * occlusion;""",
       """	ambient += prefiltered * (F0 * envBRDF.x + envBRDF.y) * occlusion * screenOcclusion;""")
s = rep(s, nl, """	vec3 reflected = prefiltered * (F0 * envBRDF.x + envBRDF.y) * occlusion;""",
       """	vec3 reflected = prefiltered * (F0 * envBRDF.x + envBRDF.y) * occlusion * screenOcclusion;""")
save(p, s)

p = 'RageVEditor/assets/shaders/debug_view.rvshader'; s, nl = load(p)
s = rep(s, nl, """	else if (mode == 8 || mode == 10)   // reflection-picture, direct-light: the picture over the scale""",
       """	else if (mode == 8 || mode == 10 || mode == 12)   // reflection-picture, direct-light, ao: the picture over the scale""")
save(p, s)

# ---------------------------------------------------------------- EngineConfig
p = 'RageV/src/RageV/Core/EngineConfig.h'; s, nl = load(p)
s = rep(s, nl, """		bool  DirectSignal = true;
""", """		bool  DirectSignal = true;
		// --ao-signal=on|off (RT-2): whether the ambient occlusion is computed
		// from the G-buffer before the lit pass, settled on the contract and
		// applied to the ambient terms in the lit shader (on, the default), or
		// multiplied over the shaded frame afterwards as it was (off, the A/B).
		bool  AoSignal = true;
""")
s = rep(s, nl, """								   DirectLight, DirectRefusal };""",
       """								   DirectLight, DirectRefusal, Occlusion };"""); save(p, s)
p = 'RageV/src/RageV/Core/EngineConfig.cpp'; s, nl = load(p)
s = rep(s, nl, """		if (key == "direct-signal" || key == "directsignal")
			return ParseBool(value, config.DirectSignal);
""", """		if (key == "direct-signal" || key == "directsignal")
			return ParseBool(value, config.DirectSignal);

		if (key == "ao-signal" || key == "aosignal")
			return ParseBool(value, config.AoSignal);
""")
s = rep(s, nl, """			else if (lowered == "direct-refusal" || lowered == "directrefusal")
				config.DebugView = EngineConfig::DebugViewMode::DirectRefusal;
""", """			else if (lowered == "direct-refusal" || lowered == "directrefusal")
				config.DebugView = EngineConfig::DebugViewMode::DirectRefusal;
			// RT-2: the accumulated occlusion signal, as the lit shader reads it.
			else if (lowered == "ao" || lowered == "occlusion")
				config.DebugView = EngineConfig::DebugViewMode::Occlusion;
"""); save(p, s)

# ---------------------------------------------------------------- Renderer3D
p = 'RageV/src/RageV/Renderer/Renderer3D.h'; s, nl = load(p)
s = rep(s, nl, """		static void SetDirectSignal(bool requested);
""", """		static void SetDirectSignal(bool requested);
		// RT-2: whether the occlusion signal runs this frame (RayRates.w bit 23),
		// and the texture the lit draw reads it from.
		static void SetAoSignal(bool requested);
		static void SetScreenOcclusion(const RHI::Ref<RHI::RHITexture>& occlusion);
""")
s = rep(s, nl, """		static SignalParams DirectSignal();
""", """		static SignalParams DirectSignal();
		// RT-2: the occlusion signal's tuning -- a diffuse-kind scalar in slot 2,
		// low-frequency enough to keep a young blur.
		static SignalParams AoSignal();
"""); save(p, s)

p = 'RageV/src/RageV/Renderer/Renderer3D.cpp'; s, nl = load(p)
s = rep(s, nl, """			bool             DirectSignalRequested = false;
""", """			bool             DirectSignalRequested = false;
			bool             AoSignalRequested = false;      // RT-2
""")
s = rep(s, nl, """			Ref<RHITexture>  DirectDiffuse;
			Ref<RHITexture>  DirectSpecular;
""", """			Ref<RHITexture>  DirectDiffuse;
			Ref<RHITexture>  DirectSpecular;
			Ref<RHITexture>  ScreenOcclusion;   // RT-2: the occlusion signal the lit draw reads
""")
s = rep(s, nl, """						// bit 22: the DirectTrace pass runs this frame (RT-first T5).
						+ (s_Data->DirectSignalRequested ? 4194304 : 0)));""",
       """						// bit 22: the DirectTrace pass runs this frame (RT-first T5);
						// bit 23: the occlusion signal does (RT-2).
						+ (s_Data->DirectSignalRequested ? 4194304 : 0)
						+ (s_Data->AoSignalRequested ? 8388608 : 0)));""")
s = rep(s, nl, """			sceneSet->SetTexture(26, TextureLoader::TransparentBlack(*s_Data->Device), s_Data->PointSampler);
			sceneSet->SetTexture(27, TextureLoader::TransparentBlack(*s_Data->Device), s_Data->PointSampler);
		}
""", """			sceneSet->SetTexture(26, TextureLoader::TransparentBlack(*s_Data->Device), s_Data->PointSampler);
			sceneSet->SetTexture(27, TextureLoader::TransparentBlack(*s_Data->Device), s_Data->PointSampler);
		}
		// RT-2: the occlusion signal's binding on the same lit-kind sets, rays
		// or not (SSAO rides it in raster); white until the lit draw fills it.
		if (sceneSet == slot.Set || sceneSet == slot.SkinnedSet || sceneSet == slot.LayeredSet
			|| sceneSet == slot.MaskedSet || sceneSet == slot.GpuSet || sceneSet == slot.MaskedGpuSet)
		{
			sceneSet->SetTexture(28, TextureLoader::White(*s_Data->Device), s_Data->PointSampler);
		}
""")
s = rep(s, nl, """		if (s_Data->RayShadowsOn && s_Data->DirectDiffuse && s_Data->DirectSpecular)
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
""", """		const bool directPair = s_Data->RayShadowsOn && s_Data->DirectDiffuse && s_Data->DirectSpecular;
		if (directPair || s_Data->ScreenOcclusion)
		{
			const Ref<RHIResourceSet> litSets[] = { slot.Set, slot.SkinnedSet, slot.LayeredSet,
													slot.MaskedSet, slot.GpuSet, slot.MaskedGpuSet };
			for (const Ref<RHIResourceSet>& litSet : litSets)
			{
				if (!litSet)
					continue;
				if (directPair)
				{
					litSet->SetTexture(26, s_Data->DirectDiffuse, s_Data->PointSampler);
					litSet->SetTexture(27, s_Data->DirectSpecular, s_Data->PointSampler);
				}
				if (s_Data->ScreenOcclusion)
					litSet->SetTexture(28, s_Data->ScreenOcclusion, s_Data->PointSampler);   // RT-2
				litSet->Commit();
			}
		}
""")
s = rep(s, nl, """	void Renderer3D::SetDirectLight(const RHI::Ref<RHITexture>& diffuse,
									const RHI::Ref<RHITexture>& specular)
	{
		if (!s_Data)
			return;
		s_Data->DirectDiffuse = diffuse;
		s_Data->DirectSpecular = specular;
	}
""", """	void Renderer3D::SetDirectLight(const RHI::Ref<RHITexture>& diffuse,
									const RHI::Ref<RHITexture>& specular)
	{
		if (!s_Data)
			return;
		s_Data->DirectDiffuse = diffuse;
		s_Data->DirectSpecular = specular;
	}

	void Renderer3D::SetAoSignal(bool requested)
	{
		if (s_Data)
			s_Data->AoSignalRequested = requested;
	}

	void Renderer3D::SetScreenOcclusion(const RHI::Ref<RHITexture>& occlusion)
	{
		if (s_Data)
			s_Data->ScreenOcclusion = occlusion;
	}

	Renderer3D::SignalParams Renderer3D::AoSignal()
	{
		SignalParams signal;
		signal.Type = SignalParams::Kind::Diffuse;
		signal.Slot = 2;
		// Occlusion is low-frequency: the young-history blur stays, bounded at
		// six texels, and the moving memory floor is the direct light's.
		signal.YoungRadius = 6.0f;
		signal.MaxRadius = 6.0f;
		return signal;
	}
""")
save(p, s)

# ---------------------------------------------------------------- FrameGraphBuilder
p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'; s, nl = load(p)
if '#include "TextureLoader.h"' not in s:
    s = rep(s, nl, """#include "PostProcess.h"
""", """#include "PostProcess.h"
#include "TextureLoader.h"
""")
# 1. the view reconstruction, hoisted above the G-buffer pass (the AO before the lit pass needs it)
lines = s.split(nl)
a = next(i for i, l in enumerate(lines) if l == '\t\tPostProcess::ViewReconstruction reconstruction;')
b = next(i for i in range(a, len(lines)) if lines[i] == '\t\treconstruction.JitterY = jitter.y;')
block = lines[a:b + 1]
del lines[a:b + 1]
g = next(i for i, l in enumerate(lines) if l == '\t\tconst bool gbufferPass = Renderer3D::GBufferPassAvailable();')
lines[g:g] = ['\t\t// The depth-to-view reconstruction every screen-space pass takes; above',
              '\t\t// the G-buffer pass since RT-2, because the occlusion signal runs there.'] + block
s = nl.join(lines)
# 2. the budget map's previous/current imported early, so the pre-lit AO can read last frame's allocation
s = rep(s, nl, """		RGResource rayBudgetMap = kRGInvalid;
		bool hasRayBudget = false;
		uint32_t budgetTilesX = 0;
		uint32_t budgetTilesY = 0;
""", """		RGResource rayBudgetMap = kRGInvalid;
		bool hasRayBudget = false;
""")
s = rep(s, nl, """			TemporalHistory& budget = *desc.RayBudget;
			budget.Prepare(Renderer::GetDevice(), budgetTilesX, budgetTilesY,
				   Format::R16G16B16A16_SFLOAT, "RayBudget");

			if (budget.Current() && budget.Previous())
			{
				const bool hasHistory = budget.HasHistory();
				const RGResource previous =
					graph.Import(budget.Previous(), "RayBudgetPrevious");
				const RGResource current =
					graph.Import(budget.Current(), "RayBudgetCurrent");
""", """			TemporalHistory& budget = *desc.RayBudget;

			if (budget.Current() && budget.Previous())
			{
				const bool hasHistory = budgetHasHistory;
				// Imported above the G-buffer pass (RT-2), where the occlusion
				// signal reads last frame's allocation.
				const RGResource previous = budgetPrevious;
				const RGResource current = budgetCurrent;
""")
s = rep(s, nl, """			budgetTilesX = Math::Max((desc.Width + kTileSize - 1) / kTileSize, 1u);
			budgetTilesY = Math::Max((desc.Height + kTileSize - 1) / kTileSize, 1u);
""", """""")
s = rep(s, nl, """			constexpr uint32_t kTileSize = 16;
""", """""")
s = rep(s, nl, """		const bool gbufferPass = Renderer3D::GBufferPassAvailable();
""", """		// The ray budget's tile map, prepared and imported here so the passes
		// before the lit pass (RT-2's occlusion) can read last frame's allocation;
		// the allocator's own passes run after TAA as before.
		uint32_t budgetTilesX = 0;
		uint32_t budgetTilesY = 0;
		RGResource budgetPrevious = kRGInvalid;
		RGResource budgetCurrent = kRGInvalid;
		bool budgetHasHistory = false;
		if (desc.RayBudget && PostProcess::IsReady())
		{
			// Sixteen: one number per 256 pixels. Small enough that a wave never
			// straddles two allocations, large enough that the map is a few
			// thousand texels rather than a few million.
			constexpr uint32_t kTileSize = 16;
			budgetTilesX = Math::Max((desc.Width + kTileSize - 1) / kTileSize, 1u);
			budgetTilesY = Math::Max((desc.Height + kTileSize - 1) / kTileSize, 1u);
			TemporalHistory& budget = *desc.RayBudget;
			budget.Prepare(Renderer::GetDevice(), budgetTilesX, budgetTilesY,
						   Format::R16G16B16A16_SFLOAT, "RayBudget");
			if (budget.Current() && budget.Previous())
			{
				budgetHasHistory = budget.HasHistory();
				budgetPrevious = graph.Import(budget.Previous(), "RayBudgetPrevious");
				budgetCurrent = graph.Import(budget.Current(), "RayBudgetCurrent");
			}
		}
		const bool gbufferPass = Renderer3D::GBufferPassAvailable();
""")
# 3. the decision, told to the scene block with the direct signal's
s = rep(s, nl, """		const int directRays = config.HasRaysPerPixelOverride ? config.RaysPerPixel
															   : rtPreset.RaysPerPixel;
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
""", """		const int directRays = config.HasRaysPerPixelOverride ? config.RaysPerPixel
															   : rtPreset.RaysPerPixel;
		// RT-2: the ambient occlusion as a signal -- computed from the G-buffer
		// before the lit pass, settled on the contract, applied to the ambient
		// terms in the lit shader. RTAO under rays, SSAO in raster; needs the
		// split, a place to keep the history, and not --ao-signal=off.
		const AoDetail aoSignalLevel = rayOcclusion ? rayAo : desc.Post.AmbientOcclusion;
		const bool aoSignal = gbufferPass && aoSignalLevel != AoDetail::Off && PostProcess::IsReady()
						   && config.AoSignal && desc.Occlusion != nullptr;
		auto drawGBuffer = [drawScene, directSignal, aoSignal](RGPassContext& context)
		{
			Renderer3D::SetGBufferPassActive(true);
			// Told before BeginScene fills the scene block, whose RayRates.w
			// bits 22 and 23 are the lit shader's switches.
			Renderer3D::SetDirectSignal(directSignal);
			Renderer3D::SetAoSignal(aoSignal);
			drawScene(context);
			Renderer3D::SetDirectSignal(false);
			Renderer3D::SetAoSignal(false);
			Renderer3D::SetGBufferPassActive(false);
		};
""")
# 4. the signal's passes, after the direct light's block
s = rep(s, nl, """		else if (desc.DirectLight)
		{
			desc.DirectLight->Invalidate();
		}
""", """		else if (desc.DirectLight)
		{
			desc.DirectLight->Invalidate();
		}
		RGResource occlusionLit = kRGInvalid;       // what the lit pass reads
		RGResource currentOcclusion = kRGInvalid;   // the accumulated signal, for the debug view
		if (aoSignal)
		{
			// The compute at its own resolution (RTAO's half, SSAO's rung), from
			// the G-buffer's depth and normal, exactly as the post chain did it.
			const uint32_t aoTaps = aoSignalLevel == AoDetail::Full    ? 8u
								  : aoSignalLevel == AoDetail::Quarter ? 2u
																	   : 4u;
			const uint32_t aoDivisor = rayOcclusion                       ? 2u
									 : aoSignalLevel == AoDetail::Full     ? 2u
									 : aoSignalLevel == AoDetail::Quarter  ? 4u
																		   : 2u;
			const uint32_t aoWidth = Math::Max(desc.Width / aoDivisor, 1u);
			const uint32_t aoHeight = Math::Max(desc.Height / aoDivisor, 1u);
			RGTargetDesc aoRawDesc;
			aoRawDesc.Name = "OcclusionRaw";
			aoRawDesc.Color = Format::R16G16B16A16_SFLOAT;
			aoRawDesc.Depth = Format::Undefined;
			aoRawDesc.Scale = 1.0f / (float)aoDivisor;
			const RGResource aoRaw = graph.CreateTarget(aoRawDesc);
			const float aoRadius = desc.Post.AoRadius;
			const float aoIntensity = desc.Post.AoIntensity;
			const float aoFrame = (float)(Renderer::GetFrameCount() % 64u);
			const bool aoBudget = rayOcclusion && budgetPrevious != kRGInvalid && budgetHasHistory;
			graph.AddPass("OcclusionCompute",
				[&](RGPassBuilder& builder)
				{
					builder.Write(aoRaw);
					builder.Sample(sceneHDR);
					if (aoBudget)
						builder.Sample(budgetPrevious);
					builder.DisableDepth();
				},
				[sceneHDR, normalIndex, aoWidth, aoHeight, reconstruction, aoRadius,
				 rayOcclusion, aoTaps, aoFrame, aoBudget, budgetPrevious](RGPassContext& context)
				{
					if (rayOcclusion)
					{
						RayGpuScope rayTime(context.Cmd);
						PostProcess::RtaoCompute(context.Cmd, context.Depth(sceneHDR),
												 context.Color(sceneHDR, normalIndex),
												 RayShadows::GetStructure(),
												 aoBudget ? context.Color(budgetPrevious) : nullptr,
												 aoWidth, aoHeight, reconstruction,
												 aoRadius, aoTaps, Format::R16G16B16A16_SFLOAT,
												 aoFrame);
					}
					else
					{
						PostProcess::SsaoCompute(context.Cmd, context.Depth(sceneHDR),
												 context.Color(sceneHDR, normalIndex),
												 aoWidth, aoHeight, reconstruction,
												 aoRadius, Format::R16G16B16A16_SFLOAT);
					}
				});
			// Full resolution, depth-aware: the apply shader against a white scene
			// is exactly the upsample it did before multiplying, with the
			// intensity folded in, so the contract runs at the G-buffer's size
			// and the lit shader reads by texel.
			RGTargetDesc aoDesc;
			aoDesc.Name = "OcclusionFresh";
			aoDesc.Color = Format::R16G16B16A16_SFLOAT;
			aoDesc.Depth = Format::Undefined;
			aoDesc.Scale = (float)supersample;
			const RGResource aoFresh = graph.CreateTarget(aoDesc);
			graph.AddPass("OcclusionUpsample",
				[&](RGPassBuilder& builder)
				{
					builder.Write(aoFresh);
					builder.Sample(aoRaw);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[aoRaw, sceneHDR, aoWidth, aoHeight, aoIntensity,
				 nearZ = desc.NearClip, farZ = desc.FarClip](RGPassContext& context)
				{
					PostProcess::SsaoApply(context.Cmd, TextureLoader::White(Renderer::GetDevice()),
										   context.Color(aoRaw), context.Depth(sceneHDR),
										   aoWidth, aoHeight, nearZ, farZ, aoIntensity,
										   Format::R16G16B16A16_SFLOAT);
				});
			occlusionLit = aoFresh;
			TemporalHistory& occlusion = *desc.Occlusion;
			occlusion.Prepare(Renderer::GetDevice(),
							  desc.Width * (uint32_t)supersample, desc.Height * (uint32_t)supersample,
							  Format::R16G16B16A16_SFLOAT, "OcclusionSignal",
							  Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT);
			if (occlusion.Current() && occlusion.Previous())
			{
				const RGResource previousOcclusion = graph.Import(occlusion.Previous(), "OcclusionPrevious");
				currentOcclusion = graph.Import(occlusion.Current(), "OcclusionCurrent");
				RGTargetDesc aoBlurDesc = aoDesc;
				aoBlurDesc.Name = "OcclusionBlurred";
				static const SignalPassNames kOcclusionPasses =
					{ "OcclusionAccumulate", { "OcclusionBlur", "OcclusionBlur2", "OcclusionBlur4" } };
				occlusionLit = addSignal(kOcclusionPasses, Renderer3D::AoSignal(),
										 aoFresh, currentOcclusion, previousOcclusion, occlusion.HasHistory(),
										 &occlusion.Motion(), aoBlurDesc, false);
				occlusion.Advance();
			}
		}
""")
s = rep(s, nl, """				if (directLit != kRGInvalid)
					builder.Sample(directLit);
			},""", """				if (directLit != kRGInvalid)
					builder.Sample(directLit);
				if (occlusionLit != kRGInvalid)
					builder.Sample(occlusionLit);
			},""")
s = rep(s, nl, """							  [drawLit = desc.DrawSceneLit, jitter, directLit,""",
       """							  [drawLit = desc.DrawSceneLit, jitter, directLit, occlusionLit,""")
s = rep(s, nl, """								  Renderer3D::SetDirectLight(
									  directLit != kRGInvalid ? context.Color(directLit, 0) : nullptr,
									  directLit != kRGInvalid ? context.Color(directLit, 1) : nullptr);""",
       """								  Renderer3D::SetDirectLight(
									  directLit != kRGInvalid ? context.Color(directLit, 0) : nullptr,
									  directLit != kRGInvalid ? context.Color(directLit, 1) : nullptr);
								  Renderer3D::SetScreenOcclusion(
									  occlusionLit != kRGInvalid ? context.Color(occlusionLit) : nullptr);""")
s = rep(s, nl, """								  Renderer3D::SetDirectLight(nullptr, nullptr);""",
       """								  Renderer3D::SetDirectLight(nullptr, nullptr);
								  Renderer3D::SetScreenOcclusion(nullptr);""")
# 5. the old chain stays as the A/B, off under the signal
s = rep(s, nl, """		const AoDetail aoLevel = rayOcclusion ? rayAo : desc.Post.AmbientOcclusion;
		if (aoLevel != AoDetail::Off && PostProcess::IsReady())
		{""", """		const AoDetail aoLevel = rayOcclusion ? rayAo : desc.Post.AmbientOcclusion;
		// Under the occlusion signal (RT-2) the lit shader has applied it already;
		// this post chain is the old path, kept as the A/B (--ao-signal=off).
		if (aoLevel != AoDetail::Off && PostProcess::IsReady() && !aoSignal)
		{""")
# 6. the debug view
s = rep(s, nl, """							  : view == EngineConfig::DebugViewMode::DirectRefusal ? 6.0f
									: 1.0f;""", """							  : view == EngineConfig::DebugViewMode::DirectRefusal ? 6.0f
							  : view == EngineConfig::DebugViewMode::Occlusion ? 1.0f
									: 1.0f;""")
s = rep(s, nl, """										 : (view == EngineConfig::DebugViewMode::DirectLight
											|| view == EngineConfig::DebugViewMode::DirectRefusal)
											   ? currentDirect
											   : kRGInvalid;""", """										 : (view == EngineConfig::DebugViewMode::DirectLight
											|| view == EngineConfig::DebugViewMode::DirectRefusal)
											   ? currentDirect
										 : view == EngineConfig::DebugViewMode::Occlusion
											   ? currentOcclusion
											   : kRGInvalid;""")
save(p, s)
print('RT-2b patched')
