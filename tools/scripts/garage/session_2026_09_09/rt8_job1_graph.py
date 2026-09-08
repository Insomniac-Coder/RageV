# -*- coding: utf-8 -*-
"""RT-8 job 1, the graph half: one pass instead of two, for the sea's light.

`WaterChooseLamps` and `WaterShadeLamps` are the sea's private copy of what
DirectTrace does for every other surface. Under `--water-direct` they are
replaced by one `DirectWaterTrace`, whose pair of pictures goes into the same
contract job 3 put the private pair into -- so the switch changes who *makes*
the light and nothing about what happens to it afterwards.

The gate around them is left alone deliberately: the sea's direct light runs
exactly where its lamp passes would have, so `--water-lamp-pass=off` and the
lamp-count gate still mean what they meant.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'DirectWaterTrace' in s:
    sys.exit('already patched')


def once(old, new, what):
    global s
    if s.count(old) != 1:
        sys.exit('%s matched %d' % (what, s.count(old)))
    s = s.replace(old, new, 1)


once('''				if (choices.Current() && choices.Previous())
				{
					const RGResource pastChoices =
						graph.Import(choices.Previous(), "WaterChoicesPrevious");
					const RGResource newChoices =
						graph.Import(choices.Current(), "WaterChoicesCurrent");

					graph.AddPass("WaterChooseLamps",''',
'''				// **RT-8 job 1: who makes the sea's light.**
				//
				// The two passes below are the sea's own copy of what
				// DirectTrace has done for every other surface since RT-first
				// T5: score the lamps that reach a point, keep K of them by
				// reservoir sampling, shade them, trace their shadow rays. The
				// one thing that is genuinely the sea's is the lobe --
				// anisotropic Beckmann about the wind rather than GGX -- and
				// that is a branch inside DirectTerm, not a pass.
				//
				// So under --water-direct the two become one, and the pair it
				// writes goes into the same contract the private pair went
				// into. Everything downstream is untouched.
				const bool seaDirect = EngineConfig::Get().WaterDirect
									&& Renderer3D::CanTraceDirectWater();
				RGTargetDesc lampDesc;
				lampDesc.Name = seaDirect ? "WaterDirectLight" : "WaterLampLight";
				lampDesc.Color = Format::R16G16B16A16_SFLOAT;
				lampDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT };
				lampDesc.Depth = Format::Undefined;
				lampDesc.Scale = (float)supersample;
				if (seaDirect)
				{
					waterLamps = graph.CreateTarget(lampDesc);
					Renderer3D::GiTraceView seaView;
					seaView.NearClip = desc.NearClip;
					seaView.FarClip = desc.FarClip;
					seaView.InvProjection0 = desc.InvProjection0;
					seaView.InvProjection1 = desc.InvProjection1;
					seaView.View = desc.View;
					const RGResource seaLight = waterLamps;
					graph.AddPass("DirectWaterTrace",
						[&](RGPassBuilder& builder)
						{
							builder.Write(seaLight);
							builder.Sample(waterSurface);
							builder.DisableDepth();
						},
						[waterSurface, seaView, rtLamps](RGPassContext& context)
						{
							// The sea's layer in the four slots the G-buffer
							// uses: position where a depth would be, the normal
							// with the RMS slope and the wind angle, the albedo
							// with the specular dial.
							Renderer3D::TraceDirectWater(*context.Cmd,
														 context.Color(waterSurface, 2),
														 context.Color(waterSurface, 0),
														 context.Color(waterSurface, 1),
														 Format::R16G16B16A16_SFLOAT,
														 seaView, rtLamps);
						});
				}
				if (!seaDirect && choices.Current() && choices.Previous())
				{
					const RGResource pastChoices =
						graph.Import(choices.Previous(), "WaterChoicesPrevious");
					const RGResource newChoices =
						graph.Import(choices.Current(), "WaterChoicesCurrent");

					graph.AddPass("WaterChooseLamps",''',
     'sea direct arm')

# The shade pass's own target declaration goes, since the block above makes it.
once('''					RGTargetDesc lampDesc;
					lampDesc.Name = "WaterLampLight";
					lampDesc.Color = Format::R16G16B16A16_SFLOAT;
					lampDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT };
					lampDesc.Depth = Format::Undefined;
					lampDesc.Scale = (float)supersample;
					waterLamps = graph.CreateTarget(lampDesc);''',
'''					// Declared above, so both arms write the same shape.
					waterLamps = graph.CreateTarget(lampDesc);''',
     'shade target')

# ...and the averaging runs for whichever arm produced the light, so it moves
# out from under the choose/shade branch and closes it first.
once('''					// The swap. The camera that drew the choices is recorded
					// by the pass itself, when it has used the one before it:
					// a history of choices is per chain, and the editor draws
					// two chains from two cameras in one frame.
					choices.Advance();
				}''',
'''					// The swap. The camera that drew the choices is recorded
					// by the pass itself, when it has used the one before it:
					// a history of choices is per chain, and the editor draws
					// two chains from two cameras in one frame.
					choices.Advance();
				}''',
     'choices advance (unchanged)')

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('DirectWaterTrace pass added')
