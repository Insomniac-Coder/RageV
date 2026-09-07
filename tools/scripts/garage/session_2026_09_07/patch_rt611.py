# -*- coding: utf-8 -*-
"""RT-6.11: Catmull-Rom history reconstruction, re-measured.

**An expired negative result.** This was built once, measured flat (19.830
against 19.774 moving, 3.817 against 3.816 still) and reverted, and the reason
is still written in `taa_resolve.rvshader`:

    "At this scene's speed the neighbourhood clip is discarding the history the
     sharper kernel would have preserved, so the kernel has nothing to do."

**RT-6 changed exactly that.** The geometric test and its nine-tap neighbour
search now keep history the colour clip used to throw away, and RT-6.8 narrowed
the clip box to one surface so it discards less again. The premise the
measurement rested on is gone, so the measurement is worth taking again.

**Where it applies, and where it must not.** Anything moving reprojects between
texels, so the history is resampled every frame and bilinear resampling
compounds into softness -- that is the whole argument for a sharper kernel. But
Catmull-Rom's negative lobes ring at an edge, and at a disocclusion there is no
history to reconstruct at all. So:

    the centre texel matched      -> Catmull-Rom, nine taps
    a neighbour served            -> point, as RT-6 already does; the texels
                                     between belong to the other side of an edge
    disoccluded or off screen     -> the current frame, as before

and the result is clamped at zero, because a negative lobe over a bright edge
can produce a negative radiance that the tone curve turns into a black rim.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/taa_resolve.rvshader'
s = read(S)
if has(s, 'SampleCatmullRom'):
    print('already done')
    raise SystemExit(0)

s = rep(s,
    "// Y, Co, Cg. The transform is exact and cheap",
    "// **RT-6.11: a sharper history fetch than bilinear.** Nine taps arranged as\n"
    "// the standard five bilinear fetches would be, written out because this\n"
    "// pass reads one texture and clarity is worth more here than four fetches.\n"
    "//\n"
    "// Bilinear resampling of a history that moves between texels every frame\n"
    "// compounds into softness; a Catmull-Rom kernel does not. It has negative\n"
    "// lobes, which ring at an edge -- so the caller uses it only where the\n"
    "// reprojected texel itself matched, and the result is floored at zero,\n"
    "// because a negative lobe across a bright edge makes a negative radiance\n"
    "// that the tone curve renders as a black rim.\n"
    "vec4 SampleCatmullRom(sampler2D tex, vec2 uv, vec2 texelSize)\n"
    "{\n"
    "\tconst vec2 samplePos = uv / texelSize;\n"
    "\tconst vec2 centre = floor(samplePos - 0.5) + 0.5;\n"
    "\tconst vec2 f = samplePos - centre;\n"
    "\n"
    "\tconst vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));\n"
    "\tconst vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);\n"
    "\tconst vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));\n"
    "\tconst vec2 w3 = f * f * (-0.5 + 0.5 * f);\n"
    "\t// The middle two taps folded into one bilinear fetch, which is what makes\n"
    "\t// this nine samples of the kernel out of five reads of the texture.\n"
    "\tconst vec2 w12 = w1 + w2;\n"
    "\tconst vec2 offset12 = w2 / max(w12, vec2(1.0e-5));\n"
    "\n"
    "\tconst vec2 p0 = (centre - 1.0) * texelSize;\n"
    "\tconst vec2 p3 = (centre + 2.0) * texelSize;\n"
    "\tconst vec2 p12 = (centre + offset12) * texelSize;\n"
    "\n"
    "\tvec4 sum = vec4(0.0);\n"
    "\tsum += texture(tex, vec2(p0.x,  p0.y))  * (w0.x  * w0.y);\n"
    "\tsum += texture(tex, vec2(p12.x, p0.y))  * (w12.x * w0.y);\n"
    "\tsum += texture(tex, vec2(p3.x,  p0.y))  * (w3.x  * w0.y);\n"
    "\tsum += texture(tex, vec2(p0.x,  p12.y)) * (w0.x  * w12.y);\n"
    "\tsum += texture(tex, vec2(p12.x, p12.y)) * (w12.x * w12.y);\n"
    "\tsum += texture(tex, vec2(p3.x,  p12.y)) * (w3.x  * w12.y);\n"
    "\tsum += texture(tex, vec2(p0.x,  p3.y))  * (w0.x  * w3.y);\n"
    "\tsum += texture(tex, vec2(p12.x, p3.y))  * (w12.x * w3.y);\n"
    "\tsum += texture(tex, vec2(p3.x,  p3.y))  * (w3.x  * w3.y);\n"
    "\treturn max(sum, vec4(0.0));\n"
    "}\n"
    "\n"
    "// Y, Co, Cg. The transform is exact and cheap")

s = rep(s,
    "\tconst vec4 historySample = neighbourServed\n"
    "\t\t\t\t\t\t\t ? texelFetch(u_History, historyTexel, 0)\n"
    "\t\t\t\t\t\t\t : texture(u_History, historyUV);\n",
    "\t// **RT-6.11: Catmull-Rom where the reprojected texel itself matched.**\n"
    "\t// Point where a neighbour served, for RT-6's reason -- the texels between\n"
    "\t// belong to the other side of an edge, and a nine-tap kernel across them\n"
    "\t// is exactly the blend the surface test just refused, four times wider.\n"
    "\tconst vec4 historySample = neighbourServed\n"
    "\t\t\t\t\t\t\t ? texelFetch(u_History, historyTexel, 0)\n"
    "\t\t\t\t\t\t\t : SampleCatmullRom(u_History, historyUV, u_Params.TexelSize);\n")

write(S, s)
print('taa_resolve.rvshader: Catmull-Rom where it is safe')
