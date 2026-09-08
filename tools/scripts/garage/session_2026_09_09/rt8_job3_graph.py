# -*- coding: utf-8 -*-
"""RT-8 job 3, the graph half: the sea's light goes through the contract.

Two changes.

  * `SignalGuidance` gains lane indices and a position flag, so a signal can
    point at three lanes of a target that already exists instead of needing a
    target laid out for it. The sea's surface pass writes all three already:
    its normal at 0, its position at 2, its motion at 3.
  * The water's private accumulate becomes one arm of a switch. The other is
    `addSignal`, exactly as the direct light takes it.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'WaterContract' in s:
    sys.exit('already patched')


def once(old, new, what):
    global s
    if s.count(old) != 1:
        sys.exit('%s matched %d' % (what, s.count(old)))
    s = s.replace(old, new, 1)


# ---- the guidance carries lanes ----------------------------------------
once('''			RGResource Depth = kRGInvalid;      // kRGInvalid means the G-buffer's own
			RGResource Surface = kRGInvalid;
			RGResource Velocity = kRGInvalid;
			uint32_t   Divisor = 1;
		};''',
'''			RGResource Depth = kRGInvalid;      // kRGInvalid means the G-buffer's own
			RGResource Surface = kRGInvalid;
			RGResource Velocity = kRGInvalid;
			uint32_t   Divisor = 1;
			// **RT-8: which lane of each, because a layer that already exists
			// is cheaper than one laid out to suit.** The downsampled guidance
			// puts its three in lanes 0, 1 and 2, which is what these default
			// to; the sea's surface pass already writes its normal at 0, its
			// position at 2 and its motion at 3, so it needs no pass of its own.
			uint32_t   DepthLane = 0;
			uint32_t   NormalLane = 1;
			uint32_t   VelocityLane = 2;
			// **And what the depth lane means.** True where it is the layer's
			// own position and mask rather than clip depth -- see
			// SignalParams::PositionLane, which this sets so the two cannot
			// disagree about one binding.
			bool       PositionLane = false;
		};''',
     'SignalGuidance lanes')

once('''			const uint32_t guideNormalLane = guide.Surface != kRGInvalid ? 1u : normalIndex;
			const uint32_t guideVelocityLane = guide.Velocity != kRGInvalid ? 2u : velocityIndex;
			const bool ownGuide = guide.Depth != kRGInvalid;''',
'''			const uint32_t guideDepthLane = guide.Depth != kRGInvalid ? guide.DepthLane : 0u;
			const uint32_t guideNormalLane =
				guide.Surface != kRGInvalid ? guide.NormalLane : normalIndex;
			const uint32_t guideVelocityLane =
				guide.Velocity != kRGInvalid ? guide.VelocityLane : velocityIndex;
			const bool ownGuide = guide.Depth != kRGInvalid;
			// RT-8: the guidance says what its depth lane is, and the signal
			// carries it to both shaders. Set here rather than trusted to the
			// caller, so a signal cannot describe one binding two ways.
			params.PositionLane = params.PositionLane || guide.PositionLane;''',
     'guide lanes resolved')

once('''				[params, fresh, sceneHDR, current, previous, hasHistory, motion, pair,
				 surfaceIdIndex,
				 specular = params.Type != Renderer3D::SignalParams::Kind::Diffuse,
				 guideDepth, guideSurface, guideVelocity, guideNormalLane, guideVelocityLane, ownGuide]''',
'''				[params, fresh, sceneHDR, current, previous, hasHistory, motion, pair,
				 surfaceIdIndex,
				 specular = params.Type != Renderer3D::SignalParams::Kind::Diffuse,
				 guideDepth, guideSurface, guideVelocity, guideDepthLane, guideNormalLane,
				 guideVelocityLane, ownGuide]''',
     'accumulate capture')

once('''						ownGuide ? context.Color(guideDepth) : context.Depth(sceneHDR),''',
'''						ownGuide ? context.Color(guideDepth, guideDepthLane)
								 : context.Depth(sceneHDR),''',
     'accumulate depth lane')

once('''					[params, input, current, sceneHDR, stride, pair,
					 guideDepth, guideSurface, guideNormalLane, ownGuide](RGPassContext& context)
					{
						Renderer3D::BlurSignal(params, context.Color(input),
											   ownGuide ? context.Color(guideDepth)
													: context.Depth(sceneHDR),''',
'''					[params, input, current, sceneHDR, stride, pair,
					 guideDepth, guideSurface, guideDepthLane, guideNormalLane,
					 ownGuide](RGPassContext& context)
					{
						Renderer3D::BlurSignal(params, context.Color(input),
											   ownGuide ? context.Color(guideDepth, guideDepthLane)
													: context.Depth(sceneHDR),''',
     'blur depth lane')

# ---- the sea's light on the contract -----------------------------------
once('''					if (desc.WaterLampLight && EngineConfig::Get().WaterLampAccumulate)
					{
						TemporalHistory& light = *desc.WaterLampLight;''',
'''					// **RT-8 job 3: the same averaging, on the contract.** The
					// sea's surface pass already writes the three lanes the
					// contract validates against -- its normal, its position
					// and its motion -- so nothing is downsampled or copied to
					// get here; the guidance points straight at them. The
					// private pass below is the other arm of --water-contract,
					// kept so the two can be compared rather than argued over.
					if (desc.WaterLampLight && EngineConfig::Get().WaterLampAccumulate
						&& EngineConfig::Get().WaterContract)
					{
						TemporalHistory& light = *desc.WaterLampLight;
						// The pair's four: the scattered half, the surface, the
						// extra, the glinting twin. The same shape the direct
						// light's history has, because it is the same signal.
						light.Prepare(Renderer::GetDevice(),
									  desc.Width * (uint32_t)supersample,
									  desc.Height * (uint32_t)supersample,
									  Format::R16G16B16A16_SFLOAT, "WaterLampSignal",
									  Format::R16G16B16A16_SFLOAT,
									  Format::R16G16B16A16_SFLOAT,
									  Format::R16G16B16A16_SFLOAT);
						if (light.Current() && light.Previous())
						{
							const RGResource pastLight =
								graph.Import(light.Previous(), "WaterLightPrevious");
							const RGResource newLight =
								graph.Import(light.Current(), "WaterLightCurrent");
							SignalGuidance seaGuide;
							seaGuide.Depth = waterSurface;
							seaGuide.Surface = waterSurface;
							seaGuide.Velocity = waterSurface;
							seaGuide.DepthLane = 2;      // world position, w the mask
							seaGuide.NormalLane = 0;     // octahedral normal, roughness
							seaGuide.VelocityLane = 3;   // the wave's own screen motion
							seaGuide.PositionLane = true;
							RGTargetDesc seaBlurDesc = lampDesc;
							seaBlurDesc.Name = "WaterLampBlurred";
							static const SignalPassNames kWaterPasses =
								{ "WaterLampAccumulate",
								  { "WaterLampBlur", "WaterLampBlur2", "WaterLampBlur4" } };
							waterLamps = addSignal(kWaterPasses,
												   Renderer3D::WaterLampSignal(),
												   waterLamps, newLight, pastLight,
												   light.HasHistory(), &light.Motion(),
												   seaBlurDesc, true, seaGuide);
							light.Advance();
						}
					}
					else if (desc.WaterLampLight && EngineConfig::Get().WaterLampAccumulate)
					{
						TemporalHistory& light = *desc.WaterLampLight;''',
     'contract arm')

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('FrameGraphBuilder patched')
