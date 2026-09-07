# -*- coding: utf-8 -*-
"""RT-6.6: the moments follow the texel the colour came from.

**The defect.** RT-6 taught the resolve to search the eight neighbours for a
history belonging to this surface, and to fetch the colour from whichever texel
won. It did not move the *moments* with it: they are still read at the
reprojected uv, which is the centre texel -- the one the search had just
rejected. So on every pixel the search recovers, the blend is weighted by a
different surface's history:

  `prevMoments.x` is the frame count, and `alpha = max(1/frames, 1 - feedback)`
  -- how much of this frame enters the pixel at all.
  `.y` and `.z` are the running luminance moments, and their variance is the
  floor the neighbourhood box may not narrow below.

Both of the things the moments exist for, wrong, on 1.1-1.7% of the garage's
pixels a frame and 3.5-4.2% of the bridge's (RT-6's own measured recovery rate).

**Not a bilinear-versus-point question.** `u_Moments` is bound `Sampling::Point`
(PostProcess::TemporalResolve), so `texture(u_Moments, historyUV)` already
resolves to a single texel -- the centre one. The colour comes from the
neighbour and the moments from the centre: two different texels, two different
surfaces.

**The fix, and why it is one bool.** Where the history is read from is one
decision, so it is made once and used by both reads. Writing the condition at
each read is what allowed the second copy to go missing in the first place.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/taa_resolve.rvshader'
s = read(S)
if has(s, 'neighbourServed'):
    print('already done')
    raise SystemExit(0)

# --- the binding's comment says what it now promises ----------------------
s = rep(s,
    "// Last frame's counter and moments, reprojected with the colour. Point\n"
    "// sampled: a count is not a thing to interpolate.\n",
    "// Last frame's counter and moments, reprojected with the colour. Point\n"
    "// sampled: a count is not a thing to interpolate.\n"
    "// **RT-6.6: and read at the same texel the colour is.** When RT-6's\n"
    "// neighbour search recovers a history one texel over, the count and the\n"
    "// fluctuation belong to that texel too. Reading them at the centre -- the\n"
    "// texel the search had just rejected -- weights this pixel's blend by a\n"
    "// surface it is not blending.\n")

# --- one decision, made once ----------------------------------------------
s = rep(s,
    "\t\tCountTemporal(false);\n"
    "\t\treturn;\n"
    "\t}\n"
    "\n"
    "\t// The 3x3 neighbourhood, in YCoCg, as a box -- and the alpha's range\n",
    "\t\tCountTemporal(false);\n"
    "\t\treturn;\n"
    "\t}\n"
    "\n"
    "\t// **RT-6.6: where the history is read from, decided once.** The colour,\n"
    "\t// the count and the moments have to come from the same texel or the\n"
    "\t// blend is weighted by a surface it is not blending: `prevMoments.x` is\n"
    "\t// the frame count that sets how much of this frame enters at all, and\n"
    "\t// `.y`/`.z` set the floor the box may not narrow below.\n"
    "\t//\n"
    "\t// RT-6 added the neighbour search and moved only the colour. `u_Moments`\n"
    "\t// is point sampled, so this was never a filtering question -- the colour\n"
    "\t// came from the neighbour and the moments from the centre, which is the\n"
    "\t// texel the search had just rejected as a different surface.\n"
    "\t//\n"
    "\t// **One bool rather than the condition written at each read**, because\n"
    "\t// the defect was exactly the second copy of that condition going missing.\n"
    "\tconst bool neighbourServed = u_Params.Geometry > 0.5 && !geometryBilinear;\n"
    "\n"
    "\t// The 3x3 neighbourhood, in YCoCg, as a box -- and the alpha's range\n")

# --- the moments follow it -------------------------------------------------
s = rep(s,
    "\tconst vec4 prevMoments = u_Params.HasMoments > 0.5\n"
    "\t\t\t\t\t\t   ? texture(u_Moments, historyUV) : vec4(0.0);\n",
    "\tconst vec4 prevMoments = u_Params.HasMoments > 0.5\n"
    "\t\t\t\t\t\t   ? (neighbourServed\n"
    "\t\t\t\t\t\t\t  ? texelFetch(u_Moments, historyTexel, 0)\n"
    "\t\t\t\t\t\t\t  : texture(u_Moments, historyUV))\n"
    "\t\t\t\t\t\t   : vec4(0.0);\n")

# --- and so does the colour, through the same bool -------------------------
s = rep(s,
    "\tconst vec4 historySample = (u_Params.Geometry > 0.5 && !geometryBilinear)\n"
    "\t\t\t\t\t\t\t ? texelFetch(u_History, historyTexel, 0)\n"
    "\t\t\t\t\t\t\t : texture(u_History, historyUV);\n",
    "\tconst vec4 historySample = neighbourServed\n"
    "\t\t\t\t\t\t\t ? texelFetch(u_History, historyTexel, 0)\n"
    "\t\t\t\t\t\t\t : texture(u_History, historyUV);\n")

write(S, s)
print('taa_resolve.rvshader: the moments follow the colour')
