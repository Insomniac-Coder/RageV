# -*- coding: utf-8 -*-
"""RT-6.8: the colour box is built from this surface only.

**The exact complement of RT-6.** The resolve keeps two defences against a bad
history. RT-6 taught the first one -- "is the texel I reprojected to the same
surface?" -- to read the G-buffer. The second, the 3x3 colour box the history is
clipped into, was never asked the same question and takes all nine taps whatever
surface they sit on.

**So the box is widest exactly where it needs to be tightest.** At a silhouette
-- dark car against a bright wall -- the nine taps span two surfaces and the
range they describe is enormous, so the clip passes almost any history through.
On a flat wall, where the history was right anyway, the box is tight. Backwards
on both counts, and worst precisely where disocclusion happens.

**The fallback is most of the design.** On a one-pixel member -- a cable, a
railing picket -- few or none of the eight neighbours are the same surface, and
a box built from one sample is a point: the history is then clipped to this
frame and the pixel flickers. That is the trade this must not make. Under motion
the temporal-sigma floor cannot save it either, because `temporalSigma` is
multiplied by `stillness` and is exactly zero for anything moving.

So the box is tightened **only where there is enough same-surface evidence to
build one**, and falls back to the full neighbourhood otherwise. Three of eight
is the bar: enough to describe a range, and low enough that an ordinary
silhouette (typically four or five same-surface neighbours) still gets the
tighter box. Thin geometry keeps exactly today's behaviour rather than trading a
ghost for a flicker.

**It may reopen RT-6.2.** That measured flat on the reasoning "on a detailed
surface the 3x3 box is already wide, so the clamp only bites where there is
nothing to lose". Part of that width at an edge is a *foreign surface* rather
than detail, so the material-aware clamp deserves re-running once the box only
contains this surface.

`--taa-box-geometry=off` is the reference arm.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# ------------------------------------------------------------------ shader
S = 'RageVEditor/assets/shaders/taa_resolve.rvshader'
s = read(S)
if has(s, 'kLeastSameNeighbours'):
    print('shader already done')
else:
    s = rep(s,
        "\t// RT-6.2: one when the material lane is bound.\n"
        "\tfloat Material;\n"
        "\tfloat Pad2;\n",
        "\t// RT-6.2: one when the material lane is bound.\n"
        "\tfloat Material;\n"
        "\t// RT-6.8: one when the colour box may be built from this surface's\n"
        "\t// taps alone. Zero is the reference arm (--taa-box-geometry=off) and\n"
        "\t// is exactly the behaviour before RT-6.8.\n"
        "\tfloat BoxGeometry;\n")

    s = rep(s,
        "const float kNormalTolerance = 0.9;\n",
        "const float kNormalTolerance = 0.9;\n"
        "\n"
        "// **RT-6.8: how many of the eight neighbours must be this surface before\n"
        "// the box is built from them alone.**\n"
        "//\n"
        "// The box exists to catch a history that has gone wrong, and it can only\n"
        "// do that if it describes *this* surface -- at a silhouette the nine taps\n"
        "// span two, so the range is enormous and the clip passes anything. But a\n"
        "// box built from one or two samples is nearly a point, and clipping the\n"
        "// history into a point is the same as discarding it: the pixel shows the\n"
        "// raw sample every frame and flickers. On a one-pixel cable or picket\n"
        "// that is exactly what few matching neighbours means.\n"
        "//\n"
        "// Nor can the temporal floor cover it there: `temporalSigma` is multiplied\n"
        "// by `stillness` and is zero for anything moving, which is when this\n"
        "// matters most.\n"
        "//\n"
        "// Three, then: enough to describe a range, and low enough that an ordinary\n"
        "// silhouette -- four or five of its neighbours are usually the same\n"
        "// surface -- still gets the tighter box. Below it the full neighbourhood\n"
        "// stands, so thin geometry keeps today's behaviour rather than trading a\n"
        "// ghost for a flicker.\n"
        "const int kLeastSameNeighbours = 3;\n")

    s = rep(s,
        "\tvec3 centreYCoCg = ToYCoCg(current.rgb);\n"
        "\tvec3 lowest = centreYCoCg;\n"
        "\tvec3 highest = centreYCoCg;\n"
        "\tfloat alphaLow = current.a;\n"
        "\tfloat alphaHigh = current.a;\n",
        "\tvec3 centreYCoCg = ToYCoCg(current.rgb);\n"
        "\tvec3 lowest = centreYCoCg;\n"
        "\tvec3 highest = centreYCoCg;\n"
        "\tfloat alphaLow = current.a;\n"
        "\tfloat alphaHigh = current.a;\n"
        "\n"
        "\t// **RT-6.8: the same box, from this surface's taps only.** Gathered\n"
        "\t// beside the full one rather than instead of it, because whether it is\n"
        "\t// usable is not known until the taps have been counted.\n"
        "\tconst bool boxGeometry = u_Params.Geometry > 0.5 && u_Params.BoxGeometry > 0.5;\n"
        "\tconst ivec2 guideSize = textureSize(u_GuideCurrent, 0);\n"
        "\tconst ivec2 centreTexel = clamp(ivec2(uv * vec2(guideSize)),\n"
        "\t\t\t\t\t\t\t\t\tivec2(0), guideSize - 1);\n"
        "\tconst vec4 centreGuide = boxGeometry ? texelFetch(u_GuideCurrent, centreTexel, 0)\n"
        "\t\t\t\t\t\t\t\t\t\t : vec4(0.0);\n"
        "\tvec3 lowestSame = centreYCoCg;\n"
        "\tvec3 highestSame = centreYCoCg;\n"
        "\tfloat alphaLowSame = current.a;\n"
        "\tfloat alphaHighSame = current.a;\n"
        "\tint sameSurface = 0;\n")

    s = rep(s,
        "\t\t\talphaLow = min(alphaLow, tap.a);\n"
        "\t\t\talphaHigh = max(alphaHigh, tap.a);\n",
        "\t\t\talphaLow = min(alphaLow, tap.a);\n"
        "\t\t\talphaHigh = max(alphaHigh, tap.a);\n"
        "\t\t\t// RT-6.8: and again, for the taps that are this surface. The same\n"
        "\t\t\t// test the history uses, both sides taken from *this* frame's lane:\n"
        "\t\t\t// the question here is not \"was this the same surface\" but \"is this\n"
        "\t\t\t// neighbour the same surface\". `false` because the refusal reason\n"
        "\t\t\t// belongs to the centre's history test, not to a box tap.\n"
        "\t\t\tif (boxGeometry)\n"
        "\t\t\t{\n"
        "\t\t\t\tconst ivec2 at = clamp(centreTexel + ivec2(x, y),\n"
        "\t\t\t\t\t\t\t\t\t   ivec2(0), guideSize - 1);\n"
        "\t\t\t\tif (Matches(centreGuide, texelFetch(u_GuideCurrent, at, 0), false))\n"
        "\t\t\t\t{\n"
        "\t\t\t\t\tlowestSame = min(lowestSame, neighbour);\n"
        "\t\t\t\t\thighestSame = max(highestSame, neighbour);\n"
        "\t\t\t\t\talphaLowSame = min(alphaLowSame, tap.a);\n"
        "\t\t\t\t\talphaHighSame = max(alphaHighSame, tap.a);\n"
        "\t\t\t\t\t++sameSurface;\n"
        "\t\t\t\t}\n"
        "\t\t\t}\n")

    s = rep(s,
        "\tcentreYCoCg = ToYCoCg(filtered / max(filterWeight, 1.0e-6));\n",
        "\tcentreYCoCg = ToYCoCg(filtered / max(filterWeight, 1.0e-6));\n"
        "\n"
        "\t// **RT-6.8: take the narrower box only where it was built from enough\n"
        "\t// of this surface to mean anything.** Below the bar the full\n"
        "\t// neighbourhood stands and the pixel behaves exactly as it did.\n"
        "\t//\n"
        "\t// The filter above is deliberately *not* gated: it is RT-6.4's, its\n"
        "\t// width was set on the owner's eye, and narrowing it at silhouettes is\n"
        "\t// a separate change with its own picture to judge.\n"
        "\tif (boxGeometry && sameSurface >= kLeastSameNeighbours)\n"
        "\t{\n"
        "\t\tlowest = lowestSame;\n"
        "\t\thighest = highestSame;\n"
        "\t\talphaLow = alphaLowSame;\n"
        "\t\talphaHigh = alphaHighSame;\n"
        "\t}\n")
    write(S, s)
    print('taa_resolve.rvshader: the box knows its surface')

# ------------------------------------------------------------- PostProcess
H = 'RageV/src/RageV/Renderer/PostProcess.h'
s = read(H)
if not has(s, 'bool boxGeometry'):
    s = rep(s,
        "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& material = nullptr);\n",
        "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& material = nullptr,\n"
        "\t\t\t\t\t\t\t\t\t // RT-6.8: whether the colour box may be built from\n"
        "\t\t\t\t\t\t\t\t\t // this surface's taps alone. False is the arm.\n"
        "\t\t\t\t\t\t\t\t\t bool boxGeometry = true);\n")
    write(H, s)
    print('PostProcess.h: the flag')

C = 'RageV/src/RageV/Renderer/PostProcess.cpp'
s = read(C)
if not has(s, 'bool boxGeometry'):
    s = rep(s,
        "\t\t\t\t\t\t\t\t\t  const Ref<RHITexture>& material)\n",
        "\t\t\t\t\t\t\t\t\t  const Ref<RHITexture>& material, bool boxGeometry)\n")
    s = rep(s,
        "\t\t\t// RT-6.2: whether the material lane is bound.\n"
        "\t\t\tfloat Material = 0.0f;\n"
        "\t\t\tfloat Pad2 = 0.0f;\n",
        "\t\t\t// RT-6.2: whether the material lane is bound.\n"
        "\t\t\tfloat Material = 0.0f;\n"
        "\t\t\t// RT-6.8: whether the box may be built from this surface alone.\n"
        "\t\t\tfloat BoxGeometry = 0.0f;\n")
    s = rep(s,
        "\t\tfull.Material = material ? 1.0f : 0.0f;\n",
        "\t\tfull.Material = material ? 1.0f : 0.0f;\n"
        "\t\tfull.BoxGeometry = boxGeometry ? 1.0f : 0.0f;\n")
    write(C, s)
    print('PostProcess.cpp: the flag is pushed')

# ------------------------------------------------------------ EngineConfig
EH = 'RageV/src/RageV/Core/EngineConfig.h'
s = read(EH)
if not has(s, 'TaaBoxGeometry'):
    s = rep(s, "\t\tbool  TaaGeometry = true;\n",
            "\t\tbool  TaaGeometry = true;\n"
            "\t\t// **--taa-box-geometry=on|off** (RT-6.8). Whether the temporal\n"
            "\t\t// resolve's 3x3 colour box is built only from taps that are the\n"
            "\t\t// same surface as the centre pixel. Off is the reference arm.\n"
            "\t\tbool  TaaBoxGeometry = true;\n")
    write(EH, s)
    print('EngineConfig.h: TaaBoxGeometry')

EC = 'RageV/src/RageV/Core/EngineConfig.cpp'
s = read(EC)
if not has(s, '"taa-box-geometry"'):
    s = rep(s, "\t\t\treturn ParseBool(value, config.TaaGeometry);\n",
            "\t\t\treturn ParseBool(value, config.TaaGeometry);\n"
            "\n"
            "\t\tif (key == \"taa-box-geometry\" || key == \"taaboxgeometry\")\n"
            "\t\t\treturn ParseBool(value, config.TaaBoxGeometry);\n")
    write(EC, s)
    print('EngineConfig.cpp: --taa-box-geometry')

# ---------------------------------------------------------- the call site
F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if not has(s, 'boxGeometry = config.TaaBoxGeometry'):
    s = rep(s,
        "\t\t\t\t\t\t\tcontext.Color(sceneHDR, normalIndex));\n"
        "\t\t\t\t\t});\n",
        "\t\t\t\t\t\t\tcontext.Color(sceneHDR, normalIndex),\n"
        "\t\t\t\t\t\t\t// RT-6.8: and whether the box may be built from this\n"
        "\t\t\t\t\t\t\t// surface's taps alone.\n"
        "\t\t\t\t\t\t\tboxGeometry);\n"
        "\t\t\t\t\t});\n")
    s = rep(s,
        "\t\t\t\t\t hasHistory, jitter, taaGuideCurrent, taaGuidePrevious, taaGuideHasHistory,\n",
        "\t\t\t\t\t hasHistory, jitter, taaGuideCurrent, taaGuidePrevious, taaGuideHasHistory,\n"
        "\t\t\t\t\t boxGeometry = config.TaaBoxGeometry,\n")
    write(F, s)
    print('FrameGraphBuilder.cpp: the flag reaches the pass')
