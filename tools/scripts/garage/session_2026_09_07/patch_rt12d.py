# -*- coding: utf-8 -*-
"""RT-12, stage 4: one place per view, in the frame graph.

The four facts about a debug view -- where its number comes from, which
attachment, which channel, and how to display it -- were spread across four
parallel ternary chains and a hardcoded ordinal test in the shader. Three of
fourteen views had drifted out of agreement (see patch_rt12c's header). They
are now one switch case each, so a view is wrong only if its own case is wrong,
and adding one is a case rather than an edit in five places.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if has(s, 'struct ViewSpec'):
    print('already done')
    raise SystemExit(0)

start = s.index("\t\t\tconst float busiest = (float)Math::Max(Renderer3D::GetMaxCellLoad(), 1u);")
# This file is CRLF, so run to the end of the marker's line rather than
# matching a literal "\n" that is not there.
end = s.index(": 0u;") + len(": 0u;")
while end < len(s) and s[end] in "\r\n":
    end += 1

V = 'EngineConfig::DebugViewMode::'


def case(name, body):
    return "\t\t\tcase %s%s:\n\t\t\t\t%s\n\t\t\t\tbreak;\n" % (V, name, body)


# Display: 0 ramp, 1 picture, 2 two-colour, 3 vector, 4 oct normal
# Channel: 0 r, 1 g, 2 b, 3 a, 4 sigma
signal_rows = []
for name, res, mem in (('Reflection', 'reflectionAux', 'ReflectionSignal'),
                       ('Direct', 'currentDirect', 'DirectSignal'),
                       ('Gi', 'currentGi', 'GiSignal'),
                       ('Ao', 'currentOcclusion', 'AoSignal')):
    signal_rows.append(case(
        name + 'History',
        'spec.Aux = %s; spec.Attachment = 0; spec.Channel = 3;\n'
        '\t\t\t\tspec.Scale = Math::Max(Renderer3D::%s().Memory, 1.0f);\n'
        '\t\t\t\tspec.Name = "%s-history"; spec.Missing = kMissing%s;'
        % (res, mem, name.lower(), name)))
    signal_rows.append(case(
        name + 'Sigma',
        'spec.Aux = %s; spec.Attachment = 2; spec.Channel = 4;\n'
        '\t\t\t\tspec.Scale = %s; spec.Name = "%s-sigma"; spec.Missing = kMissing%s;'
        % (res, '0.25f' if name != 'Ao' else '0.10f', name.lower(), name)))

block = (
"\t\t\t// **RT-12: one place per view.** Where its number comes from, which\n"
"\t\t\t// attachment and channel it is, and how it is displayed -- together,\n"
"\t\t\t// because they were four parallel ternary chains and a hardcoded\n"
"\t\t\t// ordinal in the shader, and three of fourteen views had drifted out\n"
"\t\t\t// of agreement without anything failing.\n"
"\t\t\t//\n"
"\t\t\t// Display: 0 a ramp, 1 the picture, 2 two colours, 3 a vector in rg,\n"
"\t\t\t// 4 an octahedral normal. Channel: 0 r, 1 g, 2 b, 3 a, 4 the standard\n"
"\t\t\t// deviation the moments in g and b describe.\n"
"\t\t\tstruct ViewSpec\n"
"\t\t\t{\n"
"\t\t\t\tfloat       Scale = 1.0f;\n"
"\t\t\t\tRGResource  Aux = kRGInvalid;\n"
"\t\t\t\tuint32_t    Attachment = 0u;\n"
"\t\t\t\tint         Display = 0;\n"
"\t\t\t\tint         Channel = 3;\n"
"\t\t\t\tbool        FromCounts = false;\n"
"\t\t\t\tconst char* Name = \"\";\n"
"\t\t\t\tconst char* Missing = \"\";\n"
"\t\t\t};\n"
"\n"
"\t\t\tconst float busiest = (float)Math::Max(Renderer3D::GetMaxCellLoad(), 1u);\n"
"\t\t\tconst RGResource reflectionAux = tracedReflections ? currentReflections\n"
"\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t   : kRGInvalid;\n"
"\t\t\tstatic constexpr const char* kMissingReflection = \"the traced reflection pass is off\";\n"
"\t\t\tstatic constexpr const char* kMissingDirect = \"the direct-light signal is off (--direct-signal)\";\n"
"\t\t\tstatic constexpr const char* kMissingGi = \"the GI signal is off (--gi-signal, or the scene bakes its GI)\";\n"
"\t\t\tstatic constexpr const char* kMissingAo = \"the occlusion signal is off (--ao-signal)\";\n"
"\t\t\tstatic constexpr const char* kMissingBudget = \"the ray budget's tile allocator is off\";\n"
"\t\t\tstatic constexpr const char* kMissingTaa = \"the temporal resolve runs under TAA only\";\n"
"\n"
"\t\t\tViewSpec spec;\n"
"\t\t\tswitch (view)\n"
"\t\t\t{\n"
+ case('Rays', 'spec.Scale = busiest + 8.0f; spec.FromCounts = true; spec.Name = "rays";')
+ case('Lights', 'spec.Scale = busiest; spec.FromCounts = true; spec.Name = "lights";')
+ "\t\t\t// The resolve's validity as two colours: kept or refused. Zero means\n"
  "\t\t\t// kept, in the accumulator's encoding, which RT-12 gave the resolve\n"
  "\t\t\t// too -- so this is now the same question as taa-refusal asked as a\n"
  "\t\t\t// yes or no.\n"
+ case('Confidence',
       'spec.Aux = temporalCurrent; spec.Attachment = 1; spec.Channel = 3;\n'
       '\t\t\t\tspec.Display = 2; spec.Name = "confidence"; spec.Missing = kMissingTaa;')
+ "\t\t\t// **RT-12: and the same lane as a ramp, which is the new part.** The\n"
  "\t\t\t// clause that refused the reprojected texel, 0..6, plus a half where\n"
  "\t\t\t// the nine-tap search found nothing either.\n"
+ case('TaaRefusal',
       'spec.Aux = temporalCurrent; spec.Attachment = 1; spec.Channel = 3;\n'
       '\t\t\t\tspec.Scale = 7.0f; spec.Name = "taa-refusal"; spec.Missing = kMissingTaa;')
+ case('Importance',
       'spec.Aux = rayBudgetMap; spec.Channel = 0;\n'
       '\t\t\t\tspec.Scale = Math::Min(rtPreset.AoRays * Math::Max(rtPreset.Spread, 1.0f),\n'
       '\t\t\t\t\t\t\t\t\t   kTileRayCeiling);\n'
       '\t\t\t\tspec.Name = "importance"; spec.Missing = kMissingBudget;')
+ case('GiImportance',
       'spec.Aux = rayBudgetMap; spec.Channel = 1;\n'
       '\t\t\t\tspec.Scale = Math::Min(rtPreset.GiRays * Math::Max(rtPreset.Spread, 1.0f),\n'
       '\t\t\t\t\t\t\t\t\t   kTileRayCeiling);\n'
       '\t\t\t\tspec.Name = "importance-gi"; spec.Missing = kMissingBudget;')
+ "\t\t\t// The reflection accumulator's own four, unchanged in meaning.\n"
+ case('Reflection',
       'spec.Aux = reflectionAux; spec.Attachment = 0; spec.Channel = 3;\n'
       '\t\t\t\tspec.Scale = Math::Max(Renderer3D::ReflectionSignal().Memory, 1.0f);\n'
       '\t\t\t\tspec.Name = "reflection"; spec.Missing = kMissingReflection;')
+ case('ReflectionImage',
       'spec.Aux = reflectionAux; spec.Attachment = 1; spec.Channel = 3;\n'
       '\t\t\t\tspec.Scale = 20.0f; spec.Name = "reflection-image";\n'
       '\t\t\t\tspec.Missing = kMissingReflection;')
+ case('ReflectionChoice',
       'spec.Aux = reflectionAux; spec.Attachment = 2; spec.Channel = 3;\n'
       '\t\t\t\tspec.Scale = 6.0f; spec.Name = "reflection-choice";\n'
       '\t\t\t\tspec.Missing = kMissingReflection;')
+ case('ReflectionPicture',
       'spec.Aux = reflectionAux; spec.Attachment = 0; spec.Display = 1;\n'
       '\t\t\t\tspec.Scale = 4.0f; spec.Name = "reflection-picture";\n'
       '\t\t\t\tspec.Missing = kMissingReflection;')
+ "\t\t\t// RT-12: new. The stored reflector normal, and the virtual image's\n"
  "\t\t\t// motion in the velocity lane's units -- RT-6.1 has written the\n"
  "\t\t\t// second since it landed and nothing has ever looked at it.\n"
+ case('ReflectionNormal',
       'spec.Aux = reflectionAux; spec.Attachment = 1; spec.Display = 4;\n'
       '\t\t\t\tspec.Name = "reflection-normal"; spec.Missing = kMissingReflection;')
+ case('ReflectionMotion',
       'spec.Aux = reflectionAux; spec.Attachment = 3; spec.Display = 3;\n'
       '\t\t\t\tspec.Scale = 0.02f; spec.Name = "reflection-motion";\n'
       '\t\t\t\tspec.Missing = kMissingReflection;')
+ "\t\t\t// **The three that were wrong.** direct-light and ao rendered the\n"
  "\t\t\t// frame count instead of the picture, and direct-refusal rendered raw\n"
  "\t\t\t// radiance instead of its refusal ramp.\n"
+ case('DirectLight',
       'spec.Aux = currentDirect; spec.Attachment = 0; spec.Display = 1;\n'
       '\t\t\t\tspec.Scale = 64.0f; spec.Name = "direct-light";\n'
       '\t\t\t\tspec.Missing = kMissingDirect;')
+ case('DirectRefusal',
       'spec.Aux = currentDirect; spec.Attachment = 2; spec.Channel = 3;\n'
       '\t\t\t\tspec.Scale = 6.0f; spec.Name = "direct-refusal";\n'
       '\t\t\t\tspec.Missing = kMissingDirect;')
+ case('Occlusion',
       'spec.Aux = currentOcclusion; spec.Attachment = 0; spec.Display = 1;\n'
       '\t\t\t\tspec.Scale = 1.0f; spec.Name = "ao"; spec.Missing = kMissingAo;')
+ case('AoRefusal',
       'spec.Aux = currentOcclusion; spec.Attachment = 2; spec.Channel = 3;\n'
       '\t\t\t\tspec.Scale = 6.0f; spec.Name = "ao-refusal"; spec.Missing = kMissingAo;')
+ case('GiLight',
       'spec.Aux = currentGi; spec.Attachment = 0; spec.Display = 1;\n'
       '\t\t\t\tspec.Scale = 1.0f; spec.Name = "gi-light"; spec.Missing = kMissingGi;')
+ case('GiRefusal',
       'spec.Aux = currentGi; spec.Attachment = 2; spec.Channel = 3;\n'
       '\t\t\t\tspec.Scale = 6.0f; spec.Name = "gi-refusal"; spec.Missing = kMissingGi;')
+ ''.join(signal_rows)
+ "\t\t\tdefault:\n\t\t\t\tbreak;\n"
"\t\t\t}\n"
"\n"
"\t\t\tconst float scale = spec.Scale;\n"
"\t\t\tconst RGResource auxResource = spec.Aux;\n"
"\t\t\tconst uint32_t auxAttachment = spec.Attachment;\n")

s = s[:start] + block + s[end:]

# The "no source this frame" warning: it tested `mode >= 2` and rebuilt the
# name and reason from another ternary chain. The spec carries both now.
old_warn_start = s.index("\t\t\t// Said once: a view whose source is not running draws a dark map,")
old_warn_end = s.index("\t\t\tif (debugCounts && counts)")
s = s[:old_warn_start] + (
"\t\t\t// Said once: a view whose source is not running draws a dark map,\n"
"\t\t\t// and the log should say why rather than leave it to be guessed.\n"
"\t\t\t// RT-12: the name and the reason come from the view's own row, so a\n"
"\t\t\t// new view cannot be described as whichever one the chain ended on --\n"
"\t\t\t// which is what happened before RT-3 added two of its own.\n"
"\t\t\tstatic EngineConfig::DebugViewMode s_Said = EngineConfig::DebugViewMode::None;\n"
"\t\t\tif (!spec.FromCounts && auxResource == kRGInvalid && s_Said != view)\n"
"\t\t\t{\n"
"\t\t\t\ts_Said = view;\n"
"\t\t\t\tRV_CORE_WARN(\"Debug view: {0} has no source this frame ({1}); the map stays dark\",\n"
"\t\t\t\t\t\t\t spec.Name, spec.Missing);\n"
"\t\t\t}\n"
"\n") + s[old_warn_end:]

# And the call passes the style.
s = rep(s,
    "\t\t\t\t[tonemapped, auxResource, auxAttachment, counts, mode, scale, format,\n"
    "\t\t\t\t frameMix = config.DebugViewMix]\n",
    "\t\t\t\t[tonemapped, auxResource, auxAttachment, counts, mode, scale, format,\n"
    "\t\t\t\t display = spec.Display, channel = spec.Channel,\n"
    "\t\t\t\t fromCounts = spec.FromCounts, logRamp = config.DebugViewLog,\n"
    "\t\t\t\t frameMix = config.DebugViewMix]\n")
s = rep(s,
    "\t\t\t\t\t\t\t\t\t\t   counts, mode, scale, frameMix, format);\n",
    "\t\t\t\t\t\t\t\t\t\t   counts, mode, scale, frameMix,\n"
    "\t\t\t\t\t\t\t\t\t\t   display, channel, fromCounts, logRamp, format);\n")

write(F, s)
print('FrameGraphBuilder.cpp: one switch case per view')
