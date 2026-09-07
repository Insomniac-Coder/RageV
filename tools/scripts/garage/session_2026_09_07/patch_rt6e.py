"""RT-6, part E: a refused history looks at its neighbours before giving up.

A hard reset is the wrong answer to "the texel I reprojected to is a different
surface". At a silhouette the surface this pixel wants is usually one texel
away -- the reprojection is right to within the resampling, and it is the
*rounding* that lands on the far side of an edge. Throwing the history away
there trades a ghost for aliasing, and measured on the garage that is roughly a
wash: refusing 1.1-1.7% of pixels moved the crude proxies by nothing worth
having.

The reconstruction contract already solved this, and RT-5 will inherit it:
`reflection_accumulate` searches the reprojected texel and its eight
neighbours, takes the first that matches the surface, and only refuses when
none does. The same nine taps here, for the same reason -- and the fetch turns
point where a neighbour served, because the texels between the two belong to
the other side of the edge and a bilinear read there is the very blend the test
just refused.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/taa_resolve.rvshader'
s = read(S)
if has(s, 'MatchingTexel'):
    print('taa_resolve.rvshader already searches its neighbours')
    raise SystemExit(0)

s = rep(s,
    "// **Is the history under `pastUv` the same surface that is under this\n"
    "// pixel?** Three questions, and any one of them saying no is enough.\n",
    "// **Which texel near `pastUv` holds this surface's history?** Three\n"
    "// questions, and any one of them saying no is enough --\n")

s = rep(s,
    "bool SameSurface(vec2 uv, vec2 pastUv)\n"
    "{\n"
    "\tconst ivec2 size = textureSize(u_GuideCurrent, 0);\n"
    "\tconst ivec2 nowTexel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);\n"
    "\tconst ivec2 pastTexel = clamp(ivec2(pastUv * vec2(size)), ivec2(0), size - 1);\n"
    "\tconst vec4 now = texelFetch(u_GuideCurrent, nowTexel, 0);\n"
    "\tconst vec4 was = texelFetch(u_GuidePrevious, pastTexel, 0);\n"
    "\n"
    "\t// Sky on either side: nothing to compare, and nothing that ghosts.\n"
    "\tif (now.x >= 1.0 || was.x >= 1.0)\n"
    "\t\treturn true;\n"
    "\n"
    "\t// **The object, first.** Exactly equal or it is not the same object --\n"
    "\t// the lane holds a whole number the G-buffer wrote, negated for a static\n"
    "\t// surface, so an epsilon here would only let neighbouring ids through.\n"
    "\tif (abs(now.w - was.w) > 0.5)\n"
    "\t\treturn false;\n"
    "\n"
    "\t// **Then the depth**, which catches an object occluding itself.\n"
    "\tif (abs(now.x - was.x) > kDepthTolerance * max(now.x, 1.0e-4))\n"
    "\t\treturn false;\n"
    "\n"
    "\t// **Then the normal**, which catches the two faces of a corner: same\n"
    "\t// object, same distance, and a different surface all the same.\n"
    "\treturn dot(DecodeOct(now.yz), DecodeOct(was.yz)) >= kNormalTolerance;\n"
    "}\n",
    "bool Matches(vec4 now, vec4 was)\n"
    "{\n"
    "\t// Sky on either side: nothing to compare, and nothing that ghosts.\n"
    "\tif (now.x >= 1.0 || was.x >= 1.0)\n"
    "\t\treturn true;\n"
    "\n"
    "\t// **The object, first.** Exactly equal or it is not the same object --\n"
    "\t// the lane holds a whole number the G-buffer wrote, negated for a static\n"
    "\t// surface, so an epsilon here would only let neighbouring ids through.\n"
    "\tif (abs(now.w - was.w) > 0.5)\n"
    "\t\treturn false;\n"
    "\n"
    "\t// **Then the depth**, which catches an object occluding itself.\n"
    "\tif (abs(now.x - was.x) > kDepthTolerance * max(now.x, 1.0e-4))\n"
    "\t\treturn false;\n"
    "\n"
    "\t// **Then the normal**, which catches the two faces of a corner: same\n"
    "\t// object, same distance, and a different surface all the same.\n"
    "\treturn dot(DecodeOct(now.yz), DecodeOct(was.yz)) >= kNormalTolerance;\n"
    "}\n"
    "\n"
    "// **The reprojected texel, or the nearest neighbour that is this surface,\n"
    "// or nothing.** Returns the texel to read the history from and whether one\n"
    "// was found; `bilinear` says whether the centre itself matched, because\n"
    "// only then are the texels between it and its neighbours this surface too.\n"
    "//\n"
    "// The search is SVGF's and `reflection_accumulate` already runs it for the\n"
    "// same reason: at a silhouette the reprojection is right to within the\n"
    "// resampling and it is the *rounding* that lands on the far side of an\n"
    "// edge. Refusing there trades a ghost for aliasing; taking the neighbour\n"
    "// that kept this surface's history keeps the picture, one texel over.\n"
    "ivec2 MatchingTexel(vec2 uv, vec2 pastUv, out bool found, out bool bilinear)\n"
    "{\n"
    "\tconst ivec2 size = textureSize(u_GuideCurrent, 0);\n"
    "\tconst ivec2 nowTexel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);\n"
    "\tconst ivec2 centre = clamp(ivec2(pastUv * vec2(size)), ivec2(0), size - 1);\n"
    "\tconst vec4 now = texelFetch(u_GuideCurrent, nowTexel, 0);\n"
    "\n"
    "\tconst ivec2 offsets[9] = ivec2[9](ivec2(0, 0), ivec2(1, 0), ivec2(-1, 0),\n"
    "\t\t\t\t\t\t\t\t\t  ivec2(0, 1), ivec2(0, -1), ivec2(1, 1),\n"
    "\t\t\t\t\t\t\t\t\t  ivec2(-1, -1), ivec2(1, -1), ivec2(-1, 1));\n"
    "\tfor (int k = 0; k < 9; ++k)\n"
    "\t{\n"
    "\t\tconst ivec2 at = clamp(centre + offsets[k], ivec2(0), size - 1);\n"
    "\t\tif (Matches(now, texelFetch(u_GuidePrevious, at, 0)))\n"
    "\t\t{\n"
    "\t\t\tfound = true;\n"
    "\t\t\tbilinear = (k == 0);\n"
    "\t\t\treturn at;\n"
    "\t\t}\n"
    "\t}\n"
    "\tfound = false;\n"
    "\tbilinear = false;\n"
    "\treturn centre;\n"
    "}\n")

# --- the call site: search, and only reject when nothing matched ---------
s = rep(s,
    "\tconst bool disoccluded = !offScreen && u_Params.Geometry > 0.5\n"
    "\t\t\t\t\t\t   && !SameSurface(uv, historyUV);\n",
    "\tbool geometryFound = true;\n"
    "\tbool geometryBilinear = true;\n"
    "\tivec2 historyTexel = ivec2(0);\n"
    "\tif (!offScreen && u_Params.Geometry > 0.5)\n"
    "\t\thistoryTexel = MatchingTexel(uv, historyUV, geometryFound, geometryBilinear);\n"
    "\tconst bool disoccluded = !offScreen && u_Params.Geometry > 0.5 && !geometryFound;\n")

# --- the history fetch follows the texel the search found ---------------
s = rep(s,
    "\tconst vec4 historySample = texture(u_History, historyUV);\n",
    "\t// **Bilinear where the reprojected texel itself matched; that texel's own\n"
    "\t// value where a neighbour served.** The texels between the two belong to\n"
    "\t// the other side of the edge, and a filtered read across them is exactly\n"
    "\t// the blend the surface test just refused.\n"
    "\tconst vec4 historySample = (u_Params.Geometry > 0.5 && !geometryBilinear)\n"
    "\t\t\t\t\t\t\t ? texelFetch(u_History, historyTexel, 0)\n"
    "\t\t\t\t\t\t\t : texture(u_History, historyUV);\n")

write(S, s)
print('taa_resolve.rvshader: the neighbourhood search is in')
