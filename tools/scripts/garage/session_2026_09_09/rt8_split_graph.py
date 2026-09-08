# -*- coding: utf-8 -*-
"""RT-8 job 1: two passes where there was one, and the picking runs on a block.

The sea's light is now made the way the sea always made it -- choose once per
block, shade every pixel -- but by the shared pass rather than by two of the
sea's own.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'DirectWaterChoose' in s:
    sys.exit('already patched')

old = s[s.index('\t\t\t\tif (seaDirect)\n'):s.index('\t\t\t\tif (!seaDirect && choices.Current()')]
T = '\t' * 4
new = (
T + "if (seaDirect)\n"
+ T + "{\n"
+ T + "\tRenderer3D::GiTraceView seaView;\n"
+ T + "\tseaView.NearClip = desc.NearClip;\n"
+ T + "\tseaView.FarClip = desc.FarClip;\n"
+ T + "\tseaView.InvProjection0 = desc.InvProjection0;\n"
+ T + "\tseaView.InvProjection1 = desc.InvProjection1;\n"
+ T + "\tseaView.View = desc.View;\n"
+ T + "\t// **RT-8 job 1: choose on the block, shade on the pixel.**\n"
+ T + "\t//\n"
+ T + "\t// Picking which lamps matter is the expensive half and it does not\n"
+ T + "\t// need to run at every pixel; working out the brightness is the cheap\n"
+ T + "\t// half and it does. That is the shape the sea's own two passes have\n"
+ T + "\t// always had, and measuring the fused pass both ways is what showed\n"
+ T + "\t// there was no third option: per pixel it cost about a millisecond\n"
+ T + "\t// too much, and per block it lost 37% of the sea's contrast, because\n"
+ T + "\t// shading at block rate is exactly what flattens a glitter track.\n"
+ T + "\tconst uint32_t seaPickScale = Math::Max(chooseScale, 1u);\n"
+ T + "\tRGResource seaChoice = kRGInvalid;\n"
+ T + "\tif (EngineConfig::Get().WaterDirectSplit)\n"
+ T + "\t{\n"
+ T + "\t\tRGTargetDesc pickDesc;\n"
+ T + "\t\tpickDesc.Name = \"WaterDirectChoice\";\n"
+ T + "\t\t// Whole integers in both: an index is not a thing to interpolate,\n"
+ T + "\t\t// and neither is the reciprocal probability beside it.\n"
+ T + "\t\tpickDesc.Color = Format::R32G32B32A32_UINT;\n"
+ T + "\t\tpickDesc.ExtraColors = { Format::R32G32B32A32_UINT };\n"
+ T + "\t\tpickDesc.Depth = Format::Undefined;\n"
+ T + "\t\tpickDesc.Scale = (float)supersample / (float)seaPickScale;\n"
+ T + "\t\tseaChoice = graph.CreateTarget(pickDesc);\n"
+ T + "\t\tconst RGResource picked = seaChoice;\n"
+ T + "\t\tgraph.AddPass(\"DirectWaterChoose\",\n"
+ T + "\t\t\t[&](RGPassBuilder& builder)\n"
+ T + "\t\t\t{\n"
+ T + "\t\t\t\tbuilder.Write(picked);\n"
+ T + "\t\t\t\tbuilder.Sample(waterSurface);\n"
+ T + "\t\t\t\tbuilder.DisableDepth();\n"
+ T + "\t\t\t},\n"
+ T + "\t\t\t[waterSurface, seaView, rtLamps, seaPickScale](RGPassContext& context)\n"
+ T + "\t\t\t{\n"
+ T + "\t\t\t\tRenderer3D::TraceDirectWater(context.Cmd,\n"
+ T + "\t\t\t\t\tcontext.Color(waterSurface, 2),\n"
+ T + "\t\t\t\t\tcontext.Color(waterSurface, 0),\n"
+ T + "\t\t\t\t\tcontext.Color(waterSurface, 1),\n"
+ T + "\t\t\t\t\tFormat::R32G32B32A32_UINT, seaView, rtLamps,\n"
+ T + "\t\t\t\t\t(int)seaPickScale,\n"
+ T + "\t\t\t\t\tRenderer3D::DirectWaterMode::Choose);\n"
+ T + "\t\t\t});\n"
+ T + "\t}\n"
+ T + "\twaterLamps = graph.CreateTarget(lampDesc);\n"
+ T + "\tconst RGResource seaLight = waterLamps;\n"
+ T + "\tconst RGResource chosen = seaChoice;\n"
+ T + "\tgraph.AddPass(chosen != kRGInvalid ? \"DirectWaterShade\" : \"DirectWaterTrace\",\n"
+ T + "\t\t[&](RGPassBuilder& builder)\n"
+ T + "\t\t{\n"
+ T + "\t\t\tbuilder.Write(seaLight);\n"
+ T + "\t\t\tbuilder.Sample(waterSurface);\n"
+ T + "\t\t\tif (chosen != kRGInvalid)\n"
+ T + "\t\t\t\tbuilder.Sample(chosen);\n"
+ T + "\t\t\tbuilder.DisableDepth();\n"
+ T + "\t\t},\n"
+ T + "\t\t[waterSurface, seaView, rtLamps, seaDirectScale, chosen,\n"
+ T + "\t\t seaPickScale](RGPassContext& context)\n"
+ T + "\t\t{\n"
+ T + "\t\t\t// The sea's layer in the four slots the G-buffer uses: position\n"
+ T + "\t\t\t// where a depth would be, the normal with the RMS slope and the\n"
+ T + "\t\t\t// wind angle, the albedo with the specular dial.\n"
+ T + "\t\t\tRenderer3D::TraceDirectWater(context.Cmd,\n"
+ T + "\t\t\t\tcontext.Color(waterSurface, 2),\n"
+ T + "\t\t\t\tcontext.Color(waterSurface, 0),\n"
+ T + "\t\t\t\tcontext.Color(waterSurface, 1),\n"
+ T + "\t\t\t\tFormat::R16G16B16A16_SFLOAT, seaView, rtLamps,\n"
+ T + "\t\t\t\tchosen != kRGInvalid ? (int)seaPickScale : (int)seaDirectScale,\n"
+ T + "\t\t\t\tchosen != kRGInvalid ? Renderer3D::DirectWaterMode::Shade\n"
+ T + "\t\t\t\t\t\t\t\t\t : Renderer3D::DirectWaterMode::Fused,\n"
+ T + "\t\t\t\tchosen != kRGInvalid ? context.Color(chosen, 0) : nullptr,\n"
+ T + "\t\t\t\tchosen != kRGInvalid ? context.Color(chosen, 1) : nullptr);\n"
+ T + "\t\t});\n"
+ T + "}\n")
s = s.replace(old, new, 1)
io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('choose and shade are two passes now')
