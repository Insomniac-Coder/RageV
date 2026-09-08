# -*- coding: utf-8 -*-
"""RT-8 job 2, the graph half: the sea's mirror ray goes through the contract.

A history to keep it in, a guidance at the trace's own resolution, and the
traced picture handed to addSignal instead of straight to the water draw.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)


def patch(p, edits):
    src = io.open(p, encoding='utf-8', newline='').read()
    crlf = CRLF in src
    s = src.replace(CRLF, LF)
    for old, new, what in edits:
        if s.count(old) != 1:
            sys.exit('%s: %s matched %d' % (p, what, s.count(old)))
        s = s.replace(old, new, 1)
    io.open(p, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
    print(p, 'patched')


# --- somewhere to keep it -----------------------------------------------
patch(r'RageV/src/RageV/Renderer/FrameGraphBuilder.h', [
(
"""		TemporalHistory* WaterLampLight = nullptr;""",
"""		TemporalHistory* WaterLampLight = nullptr;

		// **RT-8 job 2: where the sea's traced reflection is kept.** WR-16 S5
		// traces it in a pass of its own at a fraction of the resolution, and
		// until now that picture was this frame's rays and nothing else -- no
		// temporal average anywhere, which is why the sea's mirror is the
		// noisiest thing on the bridge. Null leaves it that way.
		TemporalHistory* WaterReflectionLight = nullptr;""",
    'WaterReflectionLight'),
])

for p, decl, use in (
    (r'RageVRuntime/src/RuntimeLayer.h', '\tRageV::TemporalHistory m_WaterLampLight;', None),
    (r'RageVEditor/src/EditorLayer.h', '\tRageV::TemporalHistory m_SceneWaterLampLight;', None),
):
    pass

patch(r'RageVRuntime/src/RuntimeLayer.h', [
('\tRageV::TemporalHistory m_WaterLampLight;',
 '\tRageV::TemporalHistory m_WaterLampLight;\n'
 '\t// RT-8 job 2: and the sea\'s traced reflection, averaged over its frames.\n'
 '\tRageV::TemporalHistory m_WaterReflectionLight;', 'runtime decl'),
])
patch(r'RageVRuntime/src/RuntimeLayer.cpp', [
('\t\t\tframe.WaterLampLight = &m_WaterLampLight;',
 '\t\t\tframe.WaterLampLight = &m_WaterLampLight;\n'
 '\t\t\tframe.WaterReflectionLight = &m_WaterReflectionLight;', 'runtime use'),
])
patch(r'RageVEditor/src/EditorLayer.h', [
('\tRageV::TemporalHistory m_SceneWaterLampLight;\n\tRageV::TemporalHistory m_GameWaterLampLight;',
 '\tRageV::TemporalHistory m_SceneWaterLampLight;\n'
 '\tRageV::TemporalHistory m_GameWaterLampLight;\n'
 '\t// RT-8 job 2: one per chain, like every other history here -- the editor\n'
 '\t// draws two frames from two cameras and a shared one would drag each\n'
 '\t// behind the other.\n'
 '\tRageV::TemporalHistory m_SceneWaterReflectionLight;\n'
 '\tRageV::TemporalHistory m_GameWaterReflectionLight;', 'editor decl'),
])
patch(r'RageVEditor/src/EditorLayer.cpp', [
('\t\t\tscene.WaterLampLight = &m_SceneWaterLampLight;',
 '\t\t\tscene.WaterLampLight = &m_SceneWaterLampLight;\n'
 '\t\t\tscene.WaterReflectionLight = &m_SceneWaterReflectionLight;', 'editor scene'),
('\t\t\t\tgame.WaterLampLight = &m_GameWaterLampLight;',
 '\t\t\t\tgame.WaterLampLight = &m_GameWaterLampLight;\n'
 '\t\t\t\tgame.WaterReflectionLight = &m_GameWaterReflectionLight;', 'editor game'),
])

# --- the guidance at the trace's resolution, and the signal --------------
patch(r'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [
(
"""					[waterSurface, traceScale](RGPassContext& context)
					{
						Renderer3D::TraceWaterReflection(
							context.Color(waterSurface, 0),
							context.Color(waterSurface, 1),
							context.Color(waterSurface, 2),
							(float)traceScale);
					});
			}""",
"""					[waterSurface, traceScale](RGPassContext& context)
					{
						Renderer3D::TraceWaterReflection(
							context.Color(waterSurface, 0),
							context.Color(waterSurface, 1),
							context.Color(waterSurface, 2),
							(float)traceScale);
					});

				// **RT-8 job 2: and then through the contract, like every other
				// signal.** The trace above is one ray per block and nothing
				// else -- no average over the frames behind it anywhere in the
				// chain, which is why the sea's mirror has always been the
				// noisiest thing on the bridge. The four taps in the water draw
				// are a spatial reconstruction of one frame, not a
				// reconstruction.
				//
				// The guidance is the sea's own layer, selected down to the
				// trace's grid: its position where a depth would be (the sea
				// writes no depth, and the buffer under it is the seabed), its
				// normal, its motion.
				if (desc.WaterReflectionLight && EngineConfig::Get().WaterRayContract
					&& PostProcess::IsReady())
				{
					RGTargetDesc seaGuideDesc;
					seaGuideDesc.Name = "WaterGuidance";
					// Four floats, and full precision: this lane is a world
					// point on a bay a kilometre across, not a clip depth.
					seaGuideDesc.Color = Format::R32G32B32A32_SFLOAT;
					seaGuideDesc.ExtraColors = { kNormalFormat, Format::R16G16_SFLOAT };
					seaGuideDesc.Depth = Format::Undefined;
					seaGuideDesc.Scale = (float)supersample / (float)traceScale;
					const RGResource seaGuideTarget = graph.CreateTarget(seaGuideDesc);
					graph.AddPass("WaterGuidance",
						[&](RGPassBuilder& builder)
						{
							builder.Write(seaGuideTarget);
							builder.Sample(waterSurface);
							builder.DisableDepth();
						},
						[waterSurface, traceScale](RGPassContext& context)
						{
							// **By selection, never by averaging**, which is the
							// whole point of the guide pass: the average of two
							// positions across a wave crest is a point in
							// neither, and the average of two normals faces
							// nowhere. Same shader as the G-buffer's, whose
							// first lane is a whole texel now.
							PostProcess::GuideDownsample(context.Cmd,
														 context.Color(waterSurface, 2),
														 context.Color(waterSurface, 0),
														 context.Color(waterSurface, 3),
														 (uint32_t)traceScale,
														 Format::R32G32B32A32_SFLOAT,
														 kNormalFormat, Format::R16G16_SFLOAT);
						});

					TemporalHistory& mirror = *desc.WaterReflectionLight;
					// The specular kind's five: the picture, the surface, the
					// extra, the image motion, the id lane. The sea has no ids,
					// but the attachment is the pipeline's and must exist.
					mirror.Prepare(Renderer::GetDevice(),
								   (uint32_t)Math::Max(1u, (desc.Width * (uint32_t)supersample)
															   / (uint32_t)traceScale),
								   (uint32_t)Math::Max(1u, (desc.Height * (uint32_t)supersample)
															   / (uint32_t)traceScale),
								   Format::R16G16B16A16_SFLOAT, "WaterReflectionSignal",
								   Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT,
								   Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT);
					if (mirror.Current() && mirror.Previous())
					{
						const RGResource pastMirror =
							graph.Import(mirror.Previous(), "WaterMirrorPrevious");
						const RGResource newMirror =
							graph.Import(mirror.Current(), "WaterMirrorCurrent");
						SignalGuidance seaRayGuide;
						seaRayGuide.Depth = seaGuideTarget;
						seaRayGuide.Surface = seaGuideTarget;
						seaRayGuide.Velocity = seaGuideTarget;
						seaRayGuide.Divisor = (uint32_t)traceScale;
						seaRayGuide.PositionLane = true;
						RGTargetDesc mirrorBlurDesc = traceDesc;
						mirrorBlurDesc.Name = "WaterReflectionBlurred";
						static const SignalPassNames kMirrorPasses =
							{ "WaterMirrorAccumulate",
							  { "WaterMirrorBlur", "WaterMirrorBlur2", "WaterMirrorBlur4" } };
						waterTraced = addSignal(kMirrorPasses,
												Renderer3D::WaterReflectionSignal(),
												traced, newMirror, pastMirror,
												mirror.HasHistory(), &mirror.Motion(),
												mirrorBlurDesc, false, seaRayGuide);
						// **The ray distance the water draw's four taps weigh by
						// lives on the accumulate's surface attachment now**, not
						// in the picture's alpha -- the contract puts the frame
						// count there. Handed over beside the picture.
						waterTracedSurface = newMirror;
						mirror.Advance();
					}
				}
			}
			else if (desc.WaterReflectionLight)
			{
				// No trace this frame: a history left standing would be resumed
				// as truth whenever it comes back, describing another camera.
				desc.WaterReflectionLight->Invalidate();
			}""",
    'mirror signal'),
(
"""			RGResource waterTraced = kRGInvalid;""",
"""			RGResource waterTraced = kRGInvalid;
			// RT-8 job 2: and where its ray distances are, once the contract
			// has taken the picture's alpha for its frame count.
			RGResource waterTracedSurface = kRGInvalid;""",
    'waterTracedSurface'),
])
