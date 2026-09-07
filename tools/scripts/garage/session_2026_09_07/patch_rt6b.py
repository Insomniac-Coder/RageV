"""RT-6, part B: the temporal resolve rejects a history by geometry, not by
colour alone.

Until now the only thing that could refuse a history was the reprojection
landing off screen; everything else was left to the 3x3 colour box. That box is
strictest where the picture has detail -- which is where the history is usually
right -- and blindest across a flat wall, which is exactly where a ghost of
whatever used to be in front of it survives. With last frame's depth, normal
and object id kept (taa_guide.rvshader), the resolve can ask the question
directly.

The box stays. It is what catches the things geometry cannot see: a shadow
moving across a static floor, a light switching, a reflection sliding over a
surface that never moved. Geometry answers "is this the same surface", the box
answers "is the light on it the same", and neither substitutes for the other.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/taa_resolve.rvshader'
s = read(S)
if has(s, 'u_GuidePrevious'):
    print('taa_resolve.rvshader already has the geometric test')
    raise SystemExit(0)

# --- the two lanes, and the switch --------------------------------------
s = rep(s,
    "layout(set = 0, binding = 3) uniform sampler2D u_Moments;\n",
    "layout(set = 0, binding = 3) uniform sampler2D u_Moments;\n"
    "\n"
    "// **RT-6: the identity lanes, this frame's and last frame's.** Packed by\n"
    "// taa_guide.rvshader: clip depth in x, the octahedral shading normal in yz,\n"
    "// the signed object id in w. Bindings 6 and 7 because 4 is the acceleration\n"
    "// structure and 5 the counters. Point sampled -- an id interpolated between\n"
    "// two objects names neither, and a depth halfway between two surfaces is the\n"
    "// depth of neither.\n"
    "layout(set = 0, binding = 6) uniform sampler2D u_GuideCurrent;\n"
    "layout(set = 0, binding = 7) uniform sampler2D u_GuidePrevious;\n")

s = rep(s,
    "\tfloat StillFeedback;\n"
    "\tfloat Pad;\n"
    "} u_Params;\n",
    "\tfloat StillFeedback;\n"
    "\t// RT-6: one when the identity lanes are bound and hold a real frame, so\n"
    "\t// the geometric test runs; zero on the first frame, after a resize, and\n"
    "\t// under --taa-geometry=off, which is the reference arm.\n"
    "\tfloat Geometry;\n"
    "} u_Params;\n"
    "\n"
    "// **How far the three tests may disagree before the history is refused.**\n"
    "//\n"
    "// Depth is compared *relative* to the depth itself, because a depth buffer's\n"
    "// own precision falls off that way and a fixed tolerance in clip units is a\n"
    "// millimetre near and a hundred metres far. Two per cent is loose enough\n"
    "// that a surface sliding under the camera passes and tight enough that a\n"
    "// wall behind a moving object does not.\n"
    "const float kDepthTolerance = 0.02;\n"
    "// The normal, as a cosine. 0.9 is about twenty-five degrees: a curved\n"
    "// surface turning under the camera keeps its history, a face meeting\n"
    "// another face at an edge does not.\n"
    "const float kNormalTolerance = 0.9;\n"
    "\n"
    "// Octahedral decode, the same one the G-buffer encodes with. Local, so this\n"
    "// pass pulls in no lighting header for two lines of arithmetic.\n"
    "vec3 DecodeOct(vec2 e)\n"
    "{\n"
    "\tvec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));\n"
    "\tconst float t = max(-n.z, 0.0);\n"
    "\tn.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);\n"
    "\treturn normalize(n);\n"
    "}\n"
    "\n"
    "// **Is the history under `pastUv` the same surface that is under this\n"
    "// pixel?** Three questions, and any one of them saying no is enough.\n"
    "//\n"
    "// The id is the exact one and the other two are its safety net: an id\n"
    "// matches across a whole object, so a wall occluding itself -- a corner\n"
    "// turning out of view -- passes the id and fails the plane. Neither test\n"
    "// alone is the answer, which is why all three are here.\n"
    "//\n"
    "// A pixel with no surface under it now, or none there last frame, is not a\n"
    "// disocclusion to refuse: it is the sky, and the sky reprojects perfectly\n"
    "// well. Left to the box, exactly as it was before this test existed.\n"
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
    "}\n")

# --- the test, beside the off-screen rejection ---------------------------
s = rep(s,
    "\tif (any(lessThan(historyUV, vec2(0.0))) || any(greaterThan(historyUV, vec2(1.0))))\n"
    "\t{\n"
    "\t\to_Color = current;\n"
    "\t\t// Disoccluded: the count starts again here. Carrying the old one would\n"
    "\t\t// tell the next frame to trust the fluctuation of a surface that is\n"
    "\t\t// not the one now under this pixel.\n"
    "\t\to_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma, 0.0);\n"
    "\t\tCountTemporal(false);\n"
    "\t\treturn;\n"
    "\t}\n",
    "\t// **RT-6: off the edge, or a different surface.** The second is the one\n"
    "\t// the colour box could never be trusted with. The two are one branch\n"
    "\t// because the consequence is identical -- there is no history for this\n"
    "\t// pixel, so this frame stands alone and the count restarts.\n"
    "\tconst bool offScreen = any(lessThan(historyUV, vec2(0.0)))\n"
    "\t\t\t\t\t\t|| any(greaterThan(historyUV, vec2(1.0)));\n"
    "\tconst bool disoccluded = !offScreen && u_Params.Geometry > 0.5\n"
    "\t\t\t\t\t\t   && !SameSurface(uv, historyUV);\n"
    "\tif (offScreen || disoccluded)\n"
    "\t{\n"
    "\t\to_Color = current;\n"
    "\t\t// Disoccluded: the count starts again here. Carrying the old one would\n"
    "\t\t// tell the next frame to trust the fluctuation of a surface that is\n"
    "\t\t// not the one now under this pixel.\n"
    "\t\to_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma, 0.0);\n"
    "\t\tCountTemporal(false);\n"
    "\t\treturn;\n"
    "\t}\n")

# --- stillness gets the geometric half too -------------------------------
s = rep(s,
    "\tconst float motionTexels = length(velocity / u_Params.TexelSize);\n"
    "\tconst float stillness = motionTexels < kStillWithinTexels ? 1.0 : 0.0;\n",
    "\tconst float motionTexels = length(velocity / u_Params.TexelSize);\n"
    "\t// **Still, and known to be the same surface** (RT-6). The velocity test\n"
    "\t// alone says a pixel did not move; it cannot say the thing that did not\n"
    "\t// move is the thing that was there before. Reaching this line already\n"
    "\t// means the geometric test passed, so the two together are what the long\n"
    "\t// still feedback is safe to stand on -- and where the identity lanes are\n"
    "\t// not bound this is exactly the old test, unchanged.\n"
    "\tconst float stillness = motionTexels < kStillWithinTexels ? 1.0 : 0.0;\n")

write(S, s)
print('taa_resolve.rvshader: the geometric test is in')
