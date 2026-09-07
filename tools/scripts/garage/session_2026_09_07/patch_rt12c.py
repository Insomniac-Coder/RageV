# -*- coding: utf-8 -*-
"""RT-12, stage 3: the debug shader stops knowing view numbers.

**The defect this closes, found by measurement rather than by reading.** The
shader chose how to display a value with `if (mode == 8 || mode == 10 || mode
== 12)`, commented "reflection-picture, direct-light, ao". The modes at those
numbers are reflection-picture, **direct-refusal** and **gi-light**. Captured at
`--debug-view-mix=1.0`, where a ramp view collapses to the plain frame and a
picture view does not, three of fourteen views were shown to render something
other than their name:

    --debug-view=direct-light    showed the frame *count* over 64, near black
    --debug-view=ao              showed the frame count over 1, near white
    --debug-view=direct-refusal  showed raw radiance over 6

The occlusion one had already cost something: RT-2's record files "the AO debug
view reads near-white on a linear ramp" as evidence for RT-12's log ramp. It was
never the ramp. It was the view reading the wrong channel.

**So the fix is not the off-by-one.** Renumbering it would leave the next person
adding a view to make the same mistake, and this item adds eleven. The shader
now receives *what to do* -- which channel, which display -- instead of deriving
it from an ordinal, and the four facts about a view (its source, attachment,
channel and display) are written together in one switch in the frame graph,
where they cannot drift apart.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# ---------------------------------------------------------------- the shader
S = 'RageVEditor/assets/shaders/debug_view.rvshader'
s = read(S)
if has(s, 'u_Params.Display'):
    print('debug_view.rvshader already restructured')
else:
    s = rep(s,
        "\t// 0 rays, 1 lights, 2 confidence, 3 importance (AO), 4 importance (GI).\n"
        "\tfloat Mode;\n",
        "\t// Which lane of the counts buffer, when FromCounts says to read it:\n"
        "\t// 0 rays, 1 lights. Meaningless otherwise.\n"
        "\tfloat Mode;\n")
    s = rep(s,
        "\tfloat FrameMix;\n} u_Params;\n",
        "\tfloat FrameMix;\n"
        "\t// **RT-12: what to do, rather than which view this is.**\n"
        "\t//\n"
        "\t// This pass used to decide how to display a number by testing the\n"
        "\t// view's ordinal, and the test was wrong for three of fourteen views\n"
        "\t// -- silently, because a heat map has no obviously correct picture to\n"
        "\t// be compared against. An ordinal is not a property of a view; the\n"
        "\t// channel and the display are, and the frame graph knows both.\n"
        "\t//\n"
        "\t// Display: 0 ramp, 1 picture (rgb over the scale, straight out),\n"
        "\t//          2 two colours (kept or refused), 3 a vector in rg,\n"
        "\t//          4 an octahedral normal in rg.\n"
        "\t// Channel: 0 r, 1 g, 2 b, 3 a, 4 the standard deviation the second\n"
        "\t//          and first moments in g and b describe.\n"
        "\tfloat Display;\n"
        "\tfloat Channel;\n"
        "\t// Whether the value comes from the counts buffer rather than u_Aux.\n"
        "\tfloat FromCounts;\n"
        "\t// Whether the ramp is logarithmic (--debug-view-log).\n"
        "\tfloat LogRamp;\n"
        "} u_Params;\n")

    s = rep(s,
        "// A dark-to-bright ramp with distinct hues per band",
        "// The G-buffer's octahedral decode, for the normal display. Local, as\n"
        "// it is in every other pass that needs two lines of it.\n"
        "vec3 DecodeOct(vec2 e)\n"
        "{\n"
        "\tvec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));\n"
        "\tconst float t = max(-n.z, 0.0);\n"
        "\tn.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);\n"
        "\treturn normalize(n);\n"
        "}\n"
        "\n"
        "// A dark-to-bright ramp with distinct hues per band")

    # the whole body of main() after the frame fetch
    start = s.index("\tconst int mode = int(u_Params.Mode + 0.5);")
    end = s.index("\to_Color = vec4(frame * mix + heat * (1.0 - mix), 1.0);")
    body = (
"\tconst float scale = max(u_Params.Scale, 1.0e-6);\n"
"\tconst int display = int(u_Params.Display + 0.5);\n"
"\tfloat value = 0.0;\n"
"\tbool known = true;\n"
"\n"
"\tif (u_Params.FromCounts > 0.5)\n"
"\t{\n"
"\t\t// The counts are at the scene target's size, which under SSAA is\n"
"\t\t// not the output's: address by fraction, not by fragment.\n"
"\t\tconst int lane = int(u_Params.Mode + 0.5);\n"
"\t\tconst uvec2 px = uvec2(clamp(uv, vec2(0.0), vec2(0.9999))\n"
"\t\t\t\t\t\t\t   * vec2(u_DebugCounts.Width, u_DebugCounts.Height));\n"
"\t\tconst uint index = px.y * u_DebugCounts.Width + px.x;\n"
"\t\tif (index < u_DebugCounts.Pixels)\n"
"\t\t\tvalue = float(u_DebugCounts.Counts[(lane == 1 ? u_DebugCounts.Pixels : 0u) + index]);\n"
"\t}\n"
"\telse\n"
"\t{\n"
"\t\tknown = u_Params.AuxValid > 0.5;\n"
"\t\tconst vec4 aux = texture(u_Aux, uv);\n"
"\n"
"\t\t// The three displays that write a colour rather than a number, and\n"
"\t\t// none of them takes the frame underneath: a picture, a direction and\n"
"\t\t// a normal are measurements to be read on their own.\n"
"\t\tif (display == 1)\n"
"\t\t{\n"
"\t\t\to_Color = vec4(known ? aux.rgb / scale : vec3(0.0), 1.0);\n"
"\t\t\treturn;\n"
"\t\t}\n"
"\t\tif (display == 3)\n"
"\t\t{\n"
"\t\t\t// A signed pair about grey, so zero motion is flat and the two\n"
"\t\t\t// directions of each axis are distinguishable.\n"
"\t\t\tconst vec2 v = clamp(aux.rg / scale, vec2(-1.0), vec2(1.0));\n"
"\t\t\to_Color = vec4(known ? vec3(0.5 + 0.5 * v.x, 0.5 + 0.5 * v.y, 0.5)\n"
"\t\t\t\t\t\t\t\t : vec3(0.0), 1.0);\n"
"\t\t\treturn;\n"
"\t\t}\n"
"\t\tif (display == 4)\n"
"\t\t{\n"
"\t\t\tconst vec3 n = DecodeOct(aux.rg);\n"
"\t\t\to_Color = vec4(known ? 0.5 + 0.5 * n : vec3(0.0), 1.0);\n"
"\t\t\treturn;\n"
"\t\t}\n"
"\n"
"\t\tconst int channel = int(u_Params.Channel + 0.5);\n"
"\t\tvalue = channel == 0 ? aux.r\n"
"\t\t\t  : channel == 1 ? aux.g\n"
"\t\t\t  : channel == 2 ? aux.b\n"
"\t\t\t  : channel == 3 ? aux.a\n"
"\t\t\t\t// The moments: E[x^2] - E[x]^2, floored at zero because a\n"
"\t\t\t\t// half float's rounding can make a converged pixel's variance\n"
"\t\t\t\t// very slightly negative.\n"
"\t\t\t\t: sqrt(max(aux.b - aux.g * aux.g, 0.0));\n"
"\n"
"\t\t// Two colours: the ramp's top where the history was kept and its red\n"
"\t\t// where it was not. Zero means kept, in the accumulator's encoding and\n"
"\t\t// now in the resolve's too, so the test is against zero rather than\n"
"\t\t// against a validity flag that no longer exists.\n"
"\t\tif (display == 2)\n"
"\t\t\tvalue = value < 0.5 ? scale : scale * (5.0 / 6.0);\n"
"\t}\n"
"\n"
"\tfloat t = known ? value / scale : 0.0;\n"
"\t// **The logarithmic ramp** (--debug-view-log). A frame count, a ray count\n"
"\t// and a radiance all have their interesting range in the bottom decade,\n"
"\t// and a linear ramp spends six of its seven bands above it. Anchored so\n"
"\t// zero stays zero and one stays one, which keeps the two ramps readable\n"
"\t// against each other.\n"
"\tif (u_Params.LogRamp > 0.5)\n"
"\t\tt = log2(1.0 + 31.0 * clamp(t, 0.0, 1.0)) / 5.0;\n"
"\t// A zero stays the frame, dimmed, so \"no rays here\" reads as the scene\n"
"\t// and not as the ramp's black -- unless nothing is mixed in, where a\n"
"\t// zero has to read as black or it is not a measurement.\n"
"\tconst float mix = clamp(u_Params.FrameMix, 0.0, 1.0);\n"
"\tconst vec3 heat = t > 0.0 ? Ramp(t) : vec3(0.0);\n")
    s = s[:start] + body + s[end:]
    write(S, s)
    print('debug_view.rvshader: display and channel are told, not derived')

# ------------------------------------------------------------- PostProcess.h
H = 'RageV/src/RageV/Renderer/PostProcess.h'
s = read(H)
if not has(s, 'int display'):
    s = rep(s,
        "\t\t\t\t\t\t\t  int mode, float scale, float frameMix,\n",
        "\t\t\t\t\t\t\t  int mode, float scale, float frameMix,\n"
        "\t\t\t\t\t\t\t  // RT-12: how to show the number, rather than which\n"
        "\t\t\t\t\t\t\t  // view this is. See debug_view.rvshader's block.\n"
        "\t\t\t\t\t\t\t  int display, int channel, bool fromCounts, bool logRamp,\n")
    write(H, s)
    print('PostProcess.h: DebugView takes the style')
else:
    print('PostProcess.h already done')

C = 'RageV/src/RageV/Renderer/PostProcess.cpp'
s = read(C)
if not has(s, 'float Display'):
    s = rep(s,
        "\t\t\t\t\t\t\t\tint mode, float scale, float frameMix,\n"
        "\t\t\t\t\t\t\t\tFormat outputFormat)\n",
        "\t\t\t\t\t\t\t\tint mode, float scale, float frameMix,\n"
        "\t\t\t\t\t\t\t\tint display, int channel, bool fromCounts, bool logRamp,\n"
        "\t\t\t\t\t\t\t\tFormat outputFormat)\n")
    s = rep(s,
        "\t\t\tPostParams Base;\n"
        "\t\t\tfloat FrameMix = 0.2f;\n"
        "\t\t};\n",
        "\t\t\tPostParams Base;\n"
        "\t\t\tfloat FrameMix = 0.2f;\n"
        "\t\t\t// RT-12. Forty-four bytes in all, well inside the 128 every\n"
        "\t\t\t// device guarantees.\n"
        "\t\t\tfloat Display = 0.0f;\n"
        "\t\t\tfloat Channel = 3.0f;\n"
        "\t\t\tfloat FromCounts = 0.0f;\n"
        "\t\t\tfloat LogRamp = 0.0f;\n"
        "\t\t};\n")
    s = rep(s,
        "\t\tparams.FrameMix = Math::Clamp(frameMix, 0.0f, 1.0f);\n",
        "\t\tparams.FrameMix = Math::Clamp(frameMix, 0.0f, 1.0f);\n"
        "\t\tparams.Display = (float)display;\n"
        "\t\tparams.Channel = (float)channel;\n"
        "\t\tparams.FromCounts = fromCounts ? 1.0f : 0.0f;\n"
        "\t\tparams.LogRamp = logRamp ? 1.0f : 0.0f;\n")
    write(C, s)
    print('PostProcess.cpp: the style is pushed')
else:
    print('PostProcess.cpp already done')
