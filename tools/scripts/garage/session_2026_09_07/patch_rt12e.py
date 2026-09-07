# -*- coding: utf-8 -*-
"""RT-12, the piece deferred yesterday: §11's reflection direction, and its
frame-to-frame difference.

**Why it was deferred, and why that reason turned out to be wrong.** The first
reading was that a world-space reflection direction needs an inverse
view-projection and a camera position in a push-constant block already sitting
inside the 128 bytes Vulkan guarantees, so it wanted a uniform buffer. Two
observations remove the whole problem:

1. **`o_Motion` has two spare channels.** RT-6.1 gave the reflection history a
   fourth `R16G16B16A16_SFLOAT` attachment and writes `vec4(motion.xy, 0, 0)`.
   The z and w are allocated, paid for, and zero.
2. **A unit vector fits in exactly two channels.** `OctEncode` is already in
   `include/octahedral.glsl` and this shader already includes it.

So the accumulator -- which computes `reflect(sight, N)` anyway for RT-6.3's
direction test -- writes the direction itself, and the debug pass needs no
matrix, no camera, no uniform buffer and not one byte of new bandwidth.

**And the difference falls out for free.** The previous frame's copy of that
same lane is `previousReflections`, which the frame graph already imports for
the accumulator. `1 - dot(R_now, R_prev)` is the specification's
`reflectionDifference` exactly, computed from two stored vectors rather than
reconstructed from two cameras.

The scale is 0.1, which saturates at about twenty-five degrees of swing. A
mirror's whole tolerance is cos(1.8 degrees) -- a difference of 0.0005 -- so
this is the view that wants `--debug-view-log`.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# ------------------------------------------------- the accumulator writes it
A = 'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
s = read(A)
if not has(s, 'RT-12: and the reflection direction'):
    s = rep(s,
        "\to_Motion = vec4(g_HaveMotion ? g_ImageMotion : vec2(0.0), 0.0, 0.0);\n",
        "\t// **RT-12: and the reflection direction itself, in the two channels\n"
        "\t// this attachment has always had spare.** RT-6.1 sized it RGBA16F for\n"
        "\t// a two-channel motion and left zw at zero; a unit vector is exactly\n"
        "\t// two channels octahedrally, and this pass computes the direction\n"
        "\t// anyway for RT-6.3's test. So the specification's §11 view costs no\n"
        "\t// bandwidth, no matrix and no uniform buffer -- and next frame this\n"
        "\t// same lane, read from the previous history, is what makes\n"
        "\t// `1 - dot(R_now, R_prev)` a difference of two stored vectors rather\n"
        "\t// than a reconstruction from two cameras.\n"
        "\to_Motion = vec4(g_HaveMotion ? g_ImageMotion : vec2(0.0),\n"
        "\t\t\t\t\tOctEncode(reflect(sight, N)));\n")
    write(A, s)
    print('reflection_accumulate.rvshader: the direction is stored')
else:
    print('accumulator already done')

# ------------------------------------------------------------- the debug pass
D = 'RageVEditor/assets/shaders/debug_view.rvshader'
s = read(D)
if not has(s, 'u_AuxPrev'):
    s = rep(s,
        "layout(set = 0, binding = 1) uniform sampler2D u_Aux;\n",
        "layout(set = 0, binding = 1) uniform sampler2D u_Aux;\n"
        "// **RT-12: the same lane, last frame.** Only the direction-difference\n"
        "// view reads it; every other view leaves it bound to black. Binding 2\n"
        "// is Dispatch's velocity slot, which this pass has never used.\n"
        "layout(set = 0, binding = 2) uniform sampler2D u_AuxPrev;\n")
    s = rep(s,
        "\t\tif (display == 4)\n"
        "\t\t{\n"
        "\t\t\tconst vec3 n = DecodeOct(aux.rg);\n"
        "\t\t\to_Color = vec4(known ? 0.5 + 0.5 * n : vec3(0.0), 1.0);\n"
        "\t\t\treturn;\n"
        "\t\t}\n",
        "\t\tif (display == 4)\n"
        "\t\t{\n"
        "\t\t\tconst vec3 n = DecodeOct(aux.rg);\n"
        "\t\t\to_Color = vec4(known ? 0.5 + 0.5 * n : vec3(0.0), 1.0);\n"
        "\t\t\treturn;\n"
        "\t\t}\n"
        "\t\t// **RT-12: the reflection direction** (specification §11), stored\n"
        "\t\t// octahedrally in zw by the accumulator. About grey, so the three\n"
        "\t\t// axes are readable and a flat surface reads as one colour.\n"
        "\t\tif (display == 5)\n"
        "\t\t{\n"
        "\t\t\tconst vec3 r = DecodeOct(aux.zw);\n"
        "\t\t\to_Color = vec4(known ? 0.5 + 0.5 * r : vec3(0.0), 1.0);\n"
        "\t\t\treturn;\n"
        "\t\t}\n")
    s = rep(s,
        "\t\t// Two colours: the ramp's top where the history was kept",
        "\t\t// **RT-12: how far the reflection direction swung since last\n"
        "\t\t// frame** -- `1 - dot(R_now, R_prev)`, the specification's\n"
        "\t\t// `reflectionDifference`, from two stored vectors. This is the\n"
        "\t\t// quantity RT-6.3 shortens the memory by, and the one no surface\n"
        "\t\t// test can see: on a mirror the camera orbits, every other test\n"
        "\t\t// passes and this is the only thing that moved.\n"
        "\t\tif (display == 6)\n"
        "\t\t\tvalue = 1.0 - dot(DecodeOct(aux.zw),\n"
        "\t\t\t\t\t\t\t  DecodeOct(texture(u_AuxPrev, uv).zw));\n"
        "\n"
        "\t\t// Two colours: the ramp's top where the history was kept")
    write(D, s)
    print('debug_view.rvshader: the direction and its difference')
else:
    print('debug shader already done')

# --------------------------------------------------------------- the names
H = 'RageV/src/RageV/Core/EngineConfig.h'
s = read(H)
if not has(s, 'ReflectionDirection'):
    s = rep(s, "ReflectionNormal, ReflectionMotion };\n",
            "ReflectionNormal, ReflectionMotion,\n"
            + "\t" * 7 + "   // **§11.** The reflection direction as RGB, and how far it\n"
            + "\t" * 7 + "   // swung since last frame -- the one quantity that changes\n"
            + "\t" * 7 + "   // on a mirror an orbiting camera looks at while every\n"
            + "\t" * 7 + "   // surface test says nothing has changed at all.\n"
            + "\t" * 7 + "   ReflectionDirection, ReflectionDirectionDelta };\n")
    write(H, s)
    print('EngineConfig.h: the two view names')

C = 'RageV/src/RageV/Core/EngineConfig.cpp'
s = read(C)
if not has(s, '"reflection-direction"'):
    s = rep(s,
        "\t\t\telse if (lowered == \"reflection-motion\" || lowered == \"reflectionmotion\")\n"
        "\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::ReflectionMotion;\n",
        "\t\t\telse if (lowered == \"reflection-motion\" || lowered == \"reflectionmotion\")\n"
        "\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::ReflectionMotion;\n"
        "\t\t\t// The reflection direction (specification §11), stored\n"
        "\t\t\t// octahedrally in the motion lane's two spare channels, and its\n"
        "\t\t\t// frame-to-frame difference against the previous history. The\n"
        "\t\t\t// difference wants --debug-view-log: a mirror's whole tolerance\n"
        "\t\t\t// is cos(1.8 degrees), which is 0.0005 on a 0.1 ramp.\n"
        "\t\t\telse if (lowered == \"reflection-direction\" || lowered == \"reflectiondirection\")\n"
        "\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::ReflectionDirection;\n"
        "\t\t\telse if (lowered == \"reflection-direction-delta\"\n"
        "\t\t\t\t\t || lowered == \"reflectiondirectiondelta\"\n"
        "\t\t\t\t\t || lowered == \"reflection-direction-difference\")\n"
        "\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::ReflectionDirectionDelta;\n")
    write(C, s)
    print('EngineConfig.cpp: the two names parse')

# ------------------------------------------------------------ the graph rows
F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if not has(s, 'ReflectionDirection'):
    s = rep(s,
        "\t\t\t\tRGResource  Aux = kRGInvalid;\n",
        "\t\t\t\tRGResource  Aux = kRGInvalid;\n"
        "\t\t\t\t// RT-12 §11: the same attachment one frame back, for the\n"
        "\t\t\t\t// direction difference. Every other view leaves it invalid.\n"
        "\t\t\t\tRGResource  AuxPrev = kRGInvalid;\n")
    s = rep(s,
        "\t\t\tcase EngineConfig::DebugViewMode::ReflectionMotion:\n",
        "\t\t\t// **§11.** The direction the accumulator stores octahedrally in\n"
        "\t\t\t// the motion lane's spare channels, and the swing since last\n"
        "\t\t\t// frame against the previous history. Both need the reflection\n"
        "\t\t\t// pass; the difference needs a previous frame as well, so it\n"
        "\t\t\t// reports \"no source\" rather than differencing against black.\n"
        "\t\t\tcase EngineConfig::DebugViewMode::ReflectionDirection:\n"
        "\t\t\t\tspec.Aux = reflectionAux; spec.Attachment = 3; spec.Display = 5;\n"
        "\t\t\t\tspec.Name = \"reflection-direction\"; spec.Missing = kMissingReflection;\n"
        "\t\t\t\tbreak;\n"
        "\t\t\tcase EngineConfig::DebugViewMode::ReflectionDirectionDelta:\n"
        "\t\t\t\tspec.Aux = (tracedReflections && previousReflections != kRGInvalid)\n"
        "\t\t\t\t\t\t ? currentReflections : kRGInvalid;\n"
        "\t\t\t\tspec.AuxPrev = previousReflections; spec.Attachment = 3;\n"
        "\t\t\t\tspec.Display = 6; spec.Scale = 0.1f;\n"
        "\t\t\t\tspec.Name = \"reflection-direction-delta\";\n"
        "\t\t\t\tspec.Missing = kMissingReflection;\n"
        "\t\t\t\tbreak;\n"
        "\t\t\tcase EngineConfig::DebugViewMode::ReflectionMotion:\n")
    s = rep(s,
        "\t\t\tconst RGResource auxResource = spec.Aux;\n",
        "\t\t\tconst RGResource auxResource = spec.Aux;\n"
        "\t\t\tconst RGResource auxPrevResource = spec.AuxPrev;\n")
    s = rep(s,
        "\t\t\t\t\tif (auxResource != kRGInvalid)\n"
        "\t\t\t\t\t\tbuilder.Sample(auxResource);\n",
        "\t\t\t\t\tif (auxResource != kRGInvalid)\n"
        "\t\t\t\t\t\tbuilder.Sample(auxResource);\n"
        "\t\t\t\t\tif (auxPrevResource != kRGInvalid)\n"
        "\t\t\t\t\t\tbuilder.Sample(auxPrevResource);\n")
    s = rep(s,
        "\t\t\t\t[tonemapped, auxResource, auxAttachment, counts, mode, scale, format,\n",
        "\t\t\t\t[tonemapped, auxResource, auxPrevResource, auxAttachment, counts,\n"
        "\t\t\t\t mode, scale, format,\n")
    s = rep(s,
        "\t\t\t\t\t\t\t\t\t\t   auxResource != kRGInvalid\n"
        "\t\t\t\t\t\t\t\t\t\t\t   ? context.Color(auxResource, auxAttachment) : nullptr,\n",
        "\t\t\t\t\t\t\t\t\t\t   auxResource != kRGInvalid\n"
        "\t\t\t\t\t\t\t\t\t\t\t   ? context.Color(auxResource, auxAttachment) : nullptr,\n"
        "\t\t\t\t\t\t\t\t\t\t   auxPrevResource != kRGInvalid\n"
        "\t\t\t\t\t\t\t\t\t\t\t   ? context.Color(auxPrevResource, auxAttachment) : nullptr,\n")
    write(F, s)
    print('FrameGraphBuilder.cpp: the two rows and the previous lane')

# ---------------------------------------------------------------- the plumbing
PH = 'RageV/src/RageV/Renderer/PostProcess.h'
s = read(PH)
if not has(s, 'auxPrevious'):
    s = rep(s,
        "\t\t\t\t\t\t\t  const RHI::Ref<RHI::RHITexture>& aux,\n",
        "\t\t\t\t\t\t\t  const RHI::Ref<RHI::RHITexture>& aux,\n"
        "\t\t\t\t\t\t\t  // RT-12 §11: the same lane one frame back, for the\n"
        "\t\t\t\t\t\t\t  // direction difference. Null for every other view.\n"
        "\t\t\t\t\t\t\t  const RHI::Ref<RHI::RHITexture>& auxPrevious,\n")
    write(PH, s)
    print('PostProcess.h: DebugView takes the previous lane')

PC = 'RageV/src/RageV/Renderer/PostProcess.cpp'
s = read(PC)
if not has(s, 'auxPrevious'):
    s = rep(s,
        "\t\t\t\t\t\t\t\tconst Ref<RHITexture>& aux, const Ref<RHIBuffer>& counts,\n",
        "\t\t\t\t\t\t\t\tconst Ref<RHITexture>& aux,\n"
        "\t\t\t\t\t\t\t\tconst Ref<RHITexture>& auxPrevious,\n"
        "\t\t\t\t\t\t\t\tconst Ref<RHIBuffer>& counts,\n")
    s = rep(s,
        "\t\t\t\t nullptr, Sampling::Point, nullptr, Sampling::Point,\n"
        "\t\t\t\t nullptr, nullptr, Format::Undefined, Format::Undefined, counts);\n",
        "\t\t\t\t // RT-12 §11: binding 2, Dispatch's velocity slot, which this\n"
        "\t\t\t\t // pass has never used. Point: a direction is a measurement.\n"
        "\t\t\t\t auxPrevious ? auxPrevious : s_Data->Black, Sampling::Point,\n"
        "\t\t\t\t nullptr, Sampling::Point,\n"
        "\t\t\t\t nullptr, nullptr, Format::Undefined, Format::Undefined, counts);\n")
    write(PC, s)
    print('PostProcess.cpp: the previous lane is bound')
