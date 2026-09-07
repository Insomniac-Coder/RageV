# -*- coding: utf-8 -*-
"""RT-6.5's fourth check: the reflection accumulator tests the object id.

**What it does.** Before reusing last frame's reflection for a pixel, the
accumulator asks whether the surface under it is the same one. It checked
position, facing, shininess, and (RT-6.5's first half) metal-or-not. This adds
the fourth: **is it the same object?**, comparing the id the G-buffer already
writes for every surface.

**Why the earlier argument against it was wrong.** The reasoning was that two
patches agreeing on position, facing and shininess reflect the same image, so an
id test would refuse a correct history. That ignores the *tolerance*: the plane
test accepts anything within `0.05 + 0.01 * eyeDistance` metres -- **25 cm at
twenty metres**. Two different flat objects 20 cm apart, same facing, same
material, pass every existing check and reflect **different** images, because
they are at different depths. The id catches exactly that and nothing else does.

**So the penalty is shaped by how marginal the plane test was.** A different
object that is genuinely coplanar keeps its history -- there the old argument
holds, and the two really do reflect the same thing. A different object out near
the tolerance edge loses most of its memory, because that is the case the plane
test was never tight enough to catch. `smoothstep` between the two, multiplied
into RT-6.4's match confidence like every other term, so a candidate marginal on
two counts is worth less than one marginal on either.

**The cost, priced by RT-14 before it was spent:** a fifth attachment on the
reflection history, 16 bytes a pixel on a budget already at 128, and traffic to
a pass that is 96% pixel-bound. Measured in the record.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# ------------------------------------------------------------------ shader
S = 'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
s = read(S)
if has(s, 'u_HistoryIdent'):
    print('shader already done')
else:
    s = rep(s,
        "layout(set = 3, binding = 6) uniform sampler2D u_Velocity;\n",
        "layout(set = 3, binding = 6) uniform sampler2D u_Velocity;\n"
        "// **RT-6.5: who this surface is, and who it was.** The G-buffer's id\n"
        "// lane (signed object id in r), and the copy this pass kept last frame.\n"
        "// The fourth history test: position, facing and shininess can all agree\n"
        "// across two different objects, because the plane test's tolerance is\n"
        "// 0.05 + 0.01 * eyeDistance -- a quarter of a metre at twenty.\n"
        "layout(set = 3, binding = 9) uniform sampler2D u_SurfaceId;\n"
        "layout(set = 3, binding = 10) uniform sampler2D u_HistoryIdent;\n")

    s = rep(s,
        "layout(location = 3) out vec4 o_Motion;\n",
        "layout(location = 3) out vec4 o_Motion;\n"
        "// RT-6.5: the object id under this texel, for the next frame's test.\n"
        "layout(location = 4) out vec4 o_Ident;\n")

    s = rep(s,
        "float g_Metallic = 0.0;\n",
        "float g_Metallic = 0.0;\n"
        "// RT-6.5: this pixel's object id, for the same reason -- HistoryAt is\n"
        "// called from two places and this is the same value at both.\n"
        "float g_ObjectId = 0.0;\n")

    s = rep(s,
        "const float kMaterialAgree = 0.15;\n",
        "const float kMaterialAgree = 0.15;\n"
        "\n"
        "// **RT-6.5: and how much a history from a *different object* is worth.**\n"
        "//\n"
        "// Not a flat penalty, because whether the id matters depends on how\n"
        "// marginal the plane test was. Two different objects that are genuinely\n"
        "// coplanar, facing the same way and of the same material reflect the\n"
        "// same image -- refusing there would throw away a correct history. Two\n"
        "// different objects out at the edge of the plane tolerance are up to a\n"
        "// quarter of a metre apart at twenty metres and reflect *different*\n"
        "// images, and that is the case no other test here can see.\n"
        "//\n"
        "// So the penalty follows the plane residual: none when the candidate is\n"
        "// well inside the tolerance, most of it at the edge.\n"
        "const float kIdAgree = 0.25;\n")

    s = rep(s,
        "\t\t\tconst float material = UnpackMetal(c.extra.r) == (g_Metallic >= 0.5)\n"
        "\t\t\t\t\t\t\t\t ? 1.0 : kMaterialAgree;\n",
        "\t\t\tconst float material = UnpackMetal(c.extra.r) == (g_Metallic >= 0.5)\n"
        "\t\t\t\t\t\t\t\t ? 1.0 : kMaterialAgree;\n"
        "\t\t\t// **RT-6.5's fourth check: is it the same object?** Weighted by how\n"
        "\t\t\t// far out the plane test was -- see kIdAgree. A coplanar neighbour\n"
        "\t\t\t// of another object is reflecting what this one reflects and keeps\n"
        "\t\t\t// its history; one near the tolerance's edge is at a different\n"
        "\t\t\t// depth and does not.\n"
        "\t\t\tconst float wasId = texelFetch(u_HistoryIdent, pastTexel, 0).r;\n"
        "\t\t\tconst float idPenalty =\n"
        "\t\t\t\tabs(wasId - g_ObjectId) < 0.5\n"
        "\t\t\t\t\t? 1.0\n"
        "\t\t\t\t\t: mix(1.0, kIdAgree,\n"
        "\t\t\t\t\t\t  smoothstep(0.2, 1.0, offPlane / max(planeTolerance, 1.0e-5)));\n")

    s = rep(s,
        "\t\t\t\t\t\t\t  * material;\n",
        "\t\t\t\t\t\t\t  * material * idPenalty;\n")

    s = rep(s,
        "\tg_Metallic = clamp(surface.a, 0.0, 1.0);\n",
        "\tg_Metallic = clamp(surface.a, 0.0, 1.0);\n"
        "\t// RT-6.5: and who it is. The id lane's r, as the G-buffer wrote it --\n"
        "\t// a whole number, negated for a static surface, so it is compared and\n"
        "\t// never interpolated.\n"
        "\tg_ObjectId = texelFetch(u_SurfaceId, texel, 0).r;\n")

    s = rep(s,
        "\to_Motion = vec4(g_HaveMotion ? g_ImageMotion : vec2(0.0),\n"
        "\t\t\t\t\tOctEncode(reflect(sight, N)));\n",
        "\to_Motion = vec4(g_HaveMotion ? g_ImageMotion : vec2(0.0),\n"
        "\t\t\t\t\tOctEncode(reflect(sight, N)));\n"
        "\t// RT-6.5: who this texel was, for the next frame's fourth check.\n"
        "\to_Ident = vec4(g_ObjectId, 0.0, 0.0, 0.0);\n")
    write(S, s)
    print('reflection_accumulate.rvshader: the fourth check')

# ------------------------------------------------------- a fifth attachment
T = 'RageV/src/RageV/Renderer/TemporalHistory.h'
s = read(T)
if not has(s, 'fifthFormat'):
    s = rep(s,
        "\t\t\t\t\t RHI::Format fourthFormat = RHI::Format::Undefined);\n",
        "\t\t\t\t\t RHI::Format fourthFormat = RHI::Format::Undefined,\n"
        "\t\t\t\t\t // RT-6.5: and a fifth, for the reflection accumulator's\n"
        "\t\t\t\t\t // object id -- the one history test position, facing and\n"
        "\t\t\t\t\t // material cannot stand in for.\n"
        "\t\t\t\t\t RHI::Format fifthFormat = RHI::Format::Undefined);\n")
    s = rep(s, "\t\tRHI::Format m_FourthFormat = RHI::Format::Undefined;\n",
            "\t\tRHI::Format m_FourthFormat = RHI::Format::Undefined;\n"
            "\t\tRHI::Format m_FifthFormat = RHI::Format::Undefined;\n")
    write(T, s)
    print('TemporalHistory.h: a fifth format')

C = 'RageV/src/RageV/Renderer/TemporalHistory.cpp'
s = read(C)
if not has(s, 'fifthFormat'):
    s = rep(s,
        "\t\t\t\t\t\t\t\t  Format thirdFormat, Format fourthFormat)\n",
        "\t\t\t\t\t\t\t\t  Format thirdFormat, Format fourthFormat,\n"
        "\t\t\t\t\t\t\t\t  Format fifthFormat)\n")
    s = rep(s,
        "\t\t\t&& m_ThirdFormat == thirdFormat && m_FourthFormat == fourthFormat)\n",
        "\t\t\t&& m_ThirdFormat == thirdFormat && m_FourthFormat == fourthFormat\n"
        "\t\t\t&& m_FifthFormat == fifthFormat)\n")
    s = rep(s,
        "\t\tif (fourthFormat != Format::Undefined)\n"
        "\t\t\tdesc.ColorAttachments.push_back({ fourthFormat });\n",
        "\t\tif (fourthFormat != Format::Undefined)\n"
        "\t\t\tdesc.ColorAttachments.push_back({ fourthFormat });\n"
        "\t\tif (fifthFormat != Format::Undefined)\n"
        "\t\t\tdesc.ColorAttachments.push_back({ fifthFormat });\n")
    write(C, s)
    print('TemporalHistory.cpp: the fifth attachment')
