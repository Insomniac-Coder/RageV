# -*- coding: utf-8 -*-
"""RT-8 job 1: the sea's light chosen once per block, as it always was.

The extra millisecond was never the rays -- at four samples the shared pass
casts 1.97 M against the two private passes' 2.38 M and still cost 2.25 ms
against their 1.26. It was the *scoring*: `WaterChooseLamps` runs at half the
width and half the height, one choice per 2x2 block, and the shared pass was
walking the cluster list and scoring every lamp at every pixel -- four times
the work for the same picture.

So the pass runs on the same grid the choice always ran on. The water draw
already reads the pair through a normalised coordinate taken from the target's
own size, so it upsamples it without being told; and the contract filters at
the resolution the signal was made at, which is what SignalGuidance's divisor
is for.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'seaDirectScale' in s:
    sys.exit('already patched')


def once(old, new, what):
    global s
    if s.count(old) != 1:
        sys.exit('%s matched %d' % (what, s.count(old)))
    s = s.replace(old, new, 1)


once('''				RGTargetDesc lampDesc;
				lampDesc.Name = seaDirect ? "WaterDirectLight" : "WaterLampLight";
				lampDesc.Color = Format::R16G16B16A16_SFLOAT;
				lampDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT };
				lampDesc.Depth = Format::Undefined;
				lampDesc.Scale = (float)supersample;''',
'''				// **On the grid the choice has always been made on.** The sea
				// picks its lamps once per `chooseScale` block -- half the
				// width and half the height with the reuse off, which is the
				// measured default -- and shades four pixels from that one
				// choice. The shared pass scoring every pixel was four times
				// the work for the same picture, and that, not the rays, was
				// its extra millisecond.
				const uint32_t seaDirectScale = seaDirect ? chooseScale : 1u;
				RGTargetDesc lampDesc;
				lampDesc.Name = seaDirect ? "WaterDirectLight" : "WaterLampLight";
				lampDesc.Color = Format::R16G16B16A16_SFLOAT;
				lampDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT };
				lampDesc.Depth = Format::Undefined;
				lampDesc.Scale = (float)supersample / (float)seaDirectScale;''',
     'lamp target scale')

# The contract filters at the grid the signal was made on, so its guidance
# comes down to the same one -- by selection, as ever.
once('''							SignalGuidance seaGuide;
							seaGuide.Depth = waterSurface;
							seaGuide.Surface = waterSurface;
							seaGuide.Velocity = waterSurface;
							seaGuide.DepthLane = 2;      // world position, w the mask
							seaGuide.NormalLane = 0;     // octahedral normal, roughness
							seaGuide.VelocityLane = 3;   // the wave's own screen motion
							seaGuide.PositionLane = true;
							seaGuide.UpNormalLane = true;''',
'''							// **RT-8 job 1: and the guidance follows the grid.** At
							// full resolution the sea's own surface lanes are the
							// guidance and nothing is copied. On the block grid they
							// have to come down to it -- by selection, never by
							// averaging, because the average of two positions across
							// a wave crest is a point in neither.
							SignalGuidance seaGuide;
							seaGuide.DepthLane = 2;      // world position, w the mask
							seaGuide.NormalLane = 0;     // the sea's two-component normal
							seaGuide.VelocityLane = 3;   // the wave's own screen motion
							seaGuide.PositionLane = true;
							seaGuide.UpNormalLane = true;
							RGResource seaGuideLanes = waterSurface;
							if (seaDirectScale > 1 && PostProcess::IsReady())
							{
								RGTargetDesc seaLightGuide;
								seaLightGuide.Name = "WaterLightGuidance";
								seaLightGuide.Color = Format::R32G32B32A32_SFLOAT;
								seaLightGuide.ExtraColors = { kNormalFormat,
															  Format::R16G16_SFLOAT };
								seaLightGuide.Depth = Format::Undefined;
								seaLightGuide.Scale = (float)supersample
													/ (float)seaDirectScale;
								seaGuideLanes = graph.CreateTarget(seaLightGuide);
								const RGResource lanes = seaGuideLanes;
								graph.AddPass("WaterLightGuidance",
									[&](RGPassBuilder& builder)
									{
										builder.Write(lanes);
										builder.Sample(waterSurface);
										builder.DisableDepth();
									},
									[waterSurface, seaDirectScale](RGPassContext& context)
									{
										PostProcess::GuideDownsample(
											context.Cmd,
											context.Color(waterSurface, 2),
											context.Color(waterSurface, 0),
											context.Color(waterSurface, 3),
											seaDirectScale,
											Format::R32G32B32A32_SFLOAT,
											kNormalFormat, Format::R16G16_SFLOAT);
									});
								// The downsample lays the three out in 0, 1, 2.
								seaGuide.DepthLane = 0;
								seaGuide.NormalLane = 1;
								seaGuide.VelocityLane = 2;
								seaGuide.Divisor = seaDirectScale;
							}
							seaGuide.Depth = seaGuideLanes;
							seaGuide.Surface = seaGuideLanes;
							seaGuide.Velocity = seaGuideLanes;''',
     'guidance at the block grid')

# The history pair has to be the signal's size, not the screen's.
once('''						light.Prepare(Renderer::GetDevice(),
									  desc.Width * (uint32_t)supersample,
									  desc.Height * (uint32_t)supersample,
									  Format::R16G16B16A16_SFLOAT, "WaterLampSignal",''',
'''						// The signal's own grid, which is the block grid when the
						// shared pass made it: a history of another size is not a
						// history of this picture.
						light.Prepare(Renderer::GetDevice(),
									  Math::Max(1u, desc.Width * (uint32_t)supersample
													  / seaDirectScale),
									  Math::Max(1u, desc.Height * (uint32_t)supersample
													  / seaDirectScale),
									  Format::R16G16B16A16_SFLOAT, "WaterLampSignal",''',
     'history size')

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('the sea is scored on the grid its choice has always been made on')
