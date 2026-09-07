# -*- coding: utf-8 -*-
"""RT-12, stage 1: the temporal resolve says *why* it refused a history.

The reflection accumulator has written a refusal reason since RT-first T4
(`g_Refusal`, the integer part of `o_Extra.a`, with the choice in the
fraction). TAA has written nothing, so a ghost could be seen and never traced
to the clause that let it through -- and this session spent four staged probes
asking questions this lane answers by being looked at.

**The lane is free.** `o_Moments.w` has been a 0/1 validity flag since WR-16 S0,
described there as what "the ray budget's temporal consumers *will* read"; no
consumer exists yet and the only reader today is the debug view. Widening it to
a reason keeps that future use intact, because zero still means "this pixel
reused its history".

**The encoding is the accumulator's, deliberately**: the reason in the integer
part, the outcome in the fraction. A pixel that failed the centre texel's test
but recovered from a neighbour is a different event from one that found nothing
and took the current frame whole, and reading both off one ramp is the point.

    0    the reprojected texel matched
    1    off screen
    2    no history at all (first frame, resize)
    3    sky/geometry transition        4  object id
    5    depth                          6  normal
    +0.5 the nine-tap search found nothing either -- the pixel is disoccluded

so 5.0 reads "the centre failed on depth and a neighbour served" and 5.5 reads
"failed on depth and nothing nearby was this surface".
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/taa_resolve.rvshader'
s = read(S)
if has(s, 'g_Refusal'):
    print('already done')
    raise SystemExit(0)

# --- the reason, and Matches() reporting which clause said no --------------
s = rep(s,
    "// **Which texel near `pastUv` holds this surface's history?** Three\n",
    "// **RT-12: why the reprojected texel was refused**, in the reflection\n"
    "// accumulator's own encoding so the two views read alike. Set by the\n"
    "// centre tap only -- the neighbours are candidates, not this pixel's\n"
    "// history, and their reasons are not this pixel's story.\n"
    "const uint kKept        = 0u;\n"
    "const uint kOffScreen   = 1u;\n"
    "const uint kNoHistory   = 2u;\n"
    "const uint kSkyCrossing = 3u;\n"
    "const uint kObjectId    = 4u;\n"
    "const uint kDepth       = 5u;\n"
    "const uint kNormal      = 6u;\n"
    "uint g_Refusal = kKept;\n"
    "\n"
    "// **Which texel near `pastUv` holds this surface's history?** Three\n")

# Matches() records the reason for the centre tap.
s = rep(s,
    "bool Matches(vec4 now, vec4 was)\n"
    "{\n",
    "bool Matches(vec4 now, vec4 was, bool centre)\n"
    "{\n")
s = rep(s,
    "\tconst bool nowSky = now.x >= 1.0;\n", "", 0) if False else s

s = rep(s,
    "\t// Sky on either side: nothing to compare, and nothing that ghosts.\n"
    "\tif (now.x >= 1.0 || was.x >= 1.0)\n"
    "\t\treturn true;\n",
    "\t// Sky on either side: nothing to compare, and nothing that ghosts.\n"
    "\t// (RT-6.7 changes this to \"both, or neither\"; it is filed separately\n"
    "\t// and its reason code kSkyCrossing is already reserved here so the two\n"
    "\t// items do not have to touch the same lines twice.)\n"
    "\tif (now.x >= 1.0 || was.x >= 1.0)\n"
    "\t\treturn true;\n")

s = rep(s,
    "\tif (abs(now.w - was.w) > 0.5)\n"
    "\t\treturn false;\n",
    "\tif (abs(now.w - was.w) > 0.5)\n"
    "\t{\n"
    "\t\tif (centre) g_Refusal = kObjectId;\n"
    "\t\treturn false;\n"
    "\t}\n")

s = rep(s,
    "\tif (abs(now.x - was.x) > kDepthTolerance * max(now.x, 1.0e-4))\n"
    "\t\treturn false;\n",
    "\tif (abs(now.x - was.x) > kDepthTolerance * max(now.x, 1.0e-4))\n"
    "\t{\n"
    "\t\tif (centre) g_Refusal = kDepth;\n"
    "\t\treturn false;\n"
    "\t}\n")

s = rep(s,
    "\treturn dot(DecodeOct(now.yz), DecodeOct(was.yz)) >= kNormalTolerance;\n",
    "\tif (dot(DecodeOct(now.yz), DecodeOct(was.yz)) < kNormalTolerance)\n"
    "\t{\n"
    "\t\tif (centre) g_Refusal = kNormal;\n"
    "\t\treturn false;\n"
    "\t}\n"
    "\treturn true;\n")

s = rep(s,
    "\t\tif (Matches(now, texelFetch(u_GuidePrevious, at, 0)))\n",
    "\t\tif (Matches(now, texelFetch(u_GuidePrevious, at, 0), k == 0))\n")

# --- every exit writes it --------------------------------------------------
s = rep(s,
    "\t\to_Color = current;\n"
    "\t\t// One frame seen, and its luminance is the whole of what is known.\n"
    "\t\t// Validity zero: nothing was reused.\n"
    "\t\to_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma, 0.0);\n",
    "\t\to_Color = current;\n"
    "\t\t// One frame seen, and its luminance is the whole of what is known.\n"
    "\t\t// RT-12: the reason lane, where zero still means \"reused\" so the\n"
    "\t\t// validity WR-16 S0 put here survives as `w == 0`.\n"
    "\t\to_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma,\n"
    "\t\t\t\t\t\t  float(kNoHistory));\n")

s = rep(s,
    "\t\to_Color = current;\n"
    "\t\t// Disoccluded: the count starts again here. Carrying the old one would\n"
    "\t\t// tell the next frame to trust the fluctuation of a surface that is\n"
    "\t\t// not the one now under this pixel.\n"
    "\t\to_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma, 0.0);\n",
    "\t\to_Color = current;\n"
    "\t\t// Disoccluded: the count starts again here. Carrying the old one would\n"
    "\t\t// tell the next frame to trust the fluctuation of a surface that is\n"
    "\t\t// not the one now under this pixel.\n"
    "\t\t// RT-12: off screen has no clause to blame; a disocclusion carries the\n"
    "\t\t// clause that refused the centre, plus the half that says the search\n"
    "\t\t// found nothing either.\n"
    "\t\to_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma,\n"
    "\t\t\t\t\t\t  offScreen ? float(kOffScreen)\n"
    "\t\t\t\t\t\t\t\t\t: float(g_Refusal) + 0.5);\n")

s = rep(s,
    "\t// Validity one: this pixel's history was reprojected and blended in.\n"
    "\to_Moments = vec4(frames, momMean, momMeanSq, 1.0);\n",
    "\t// RT-12: reached here the history was reused, so the reason is whatever\n"
    "\t// refused the *centre* texel -- zero when it matched outright, and the\n"
    "\t// clause that sent the search to a neighbour otherwise. That difference\n"
    "\t// is the one the view exists to show: a recovered pixel and a\n"
    "\t// disoccluded one are a half apart on the ramp, not a whole category.\n"
    "\to_Moments = vec4(frames, momMean, momMeanSq, float(g_Refusal));\n")

write(S, s)
print('taa_resolve.rvshader: the refusal reason is written')
