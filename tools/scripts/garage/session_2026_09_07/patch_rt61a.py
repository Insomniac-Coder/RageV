"""RT-6.1, part A: the accumulator writes the virtual image's screen motion.

The temporal resolve reprojects every pixel by the *surface's* motion, and a
mirror image does not move with the surface carrying it -- which is why the
reflection had to be composited after the resolve, and why it therefore had no
final temporal filter at all. The accumulator already knows the right motion:
it reprojects by the virtual image, at `P + sight * image`, and `HistoryAt`
leaves that point's previous NDC in the candidate.

So the accumulator writes it out. Attachment 3 is free on the specular variant
(the pair kind's fourth is its twin, and the pair kind is diffuse), and the
convention is the scene's own velocity lane's exactly -- clip NDC, this frame's
jitter out, halved -- so the resolve can read either lane with the same code.

Zero where there is no history to reproject from: the resolve reads that as
"no motion of its own" and keeps the surface's, which is the old behaviour.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
s = read(S)
if has(s, 'o_Motion'):
    print('reflection_accumulate.rvshader already writes the motion')
    raise SystemExit(0)

# --- the attachment ------------------------------------------------------
s = rep(s,
    "#ifdef RV_SIGNAL_PAIR\n"
    "layout(location = 3) out vec4 o_Accumulated2;\n",
    "#ifdef RV_SIGNAL_PAIR\n"
    "layout(location = 3) out vec4 o_Accumulated2;\n"
    "#else\n"
    "// **RT-6.1: where the picture at this texel moved, in the scene velocity\n"
    "// lane's own units.** Not the surface's motion -- the virtual image's, which\n"
    "// is the whole reason this accumulator reprojects the way it does. The\n"
    "// temporal resolve reads it so the reflection can be composited *before* it\n"
    "// and still be reprojected correctly; without it the resolve drags the\n"
    "// reflection along the floor's motion and the wet ground smears into\n"
    "// horizontal bands (measured 2026-09-05 and again 2026-09-07).\n"
    "//\n"
    "// Only on the specular variant: a diffuse signal moves with its surface, so\n"
    "// its motion *is* the velocity lane's and a second copy would say nothing.\n"
    "layout(location = 3) out vec4 o_Motion;\n"
    "#endif\n")

# --- zero on the paths that write no history ----------------------------
s = rep(s,
    "\t\to_Accumulated = vec4(0.0);\n"
    "\t\to_Surface = vec4(0.0, 0.0, 0.0, -1.0);\n"
    "\t\to_Extra = vec4(0.0);\n",
    "\t\to_Accumulated = vec4(0.0);\n"
    "\t\to_Surface = vec4(0.0, 0.0, 0.0, -1.0);\n"
    "\t\to_Extra = vec4(0.0);\n"
    "#ifndef RV_SIGNAL_PAIR\n"
    "\t\t// Nothing here reflects, so there is no image to have moved. Zero is\n"
    "\t\t// read by the resolve as \"use the surface's\", which is right.\n"
    "\t\to_Motion = vec4(0.0);\n"
    "#endif\n")

# --- and the real one ----------------------------------------------------
s = rep(s,
    "\to_Accumulated = vec4(kept, frames);\n",
    "#ifndef RV_SIGNAL_PAIR\n"
    "\t// **The image's motion, in the velocity lane's units.** The accumulator\n"
    "\t// already measures it: `shift` below is how far the picture moved since\n"
    "\t// last frame, both sides on the unjittered grid, which is the scene\n"
    "\t// shader's o_Velocity with the sign the other way round and the halving\n"
    "\t// left off. Zero with no history: nothing to difference, and the resolve\n"
    "\t// then keeps the surface's motion, which is what it did before this.\n"
    "\to_Motion = vec4(g_HaveMotion ? g_ImageMotion : vec2(0.0), 0.0, 0.0);\n"
    "#endif\n"
    "\to_Accumulated = vec4(kept, frames);\n")

# --- and it is measured where the accumulator already measures it --------
s = rep(s,
    "\t\t\tconst vec2 shift = c.thenNdc - (nowNdc - u_Scene.Jitter.xy);\n",
    "\t\t\tconst vec2 shift = c.thenNdc - (nowNdc - u_Scene.Jitter.xy);\n"
    "\t\t\t// RT-6.1: the same quantity the temporal resolve wants, in its units.\n"
    "\t\t\tg_ImageMotion = -shift * 0.5;\n"
    "\t\t\tg_HaveMotion = true;\n")

# --- the two globals the write above reads -------------------------------
s = rep(s,
    "int g_Refusal = 0;\n",
    "int g_Refusal = 0;\n"
    "// RT-6.1: where the picture this texel took came from, last frame -- set\n"
    "// when a candidate is accepted, so the motion write below has something to\n"
    "// difference against.\n"
    "vec2 g_ImageMotion = vec2(0.0);\n"
    "bool g_HaveMotion = false;\n")

write(S, s)
print('reflection_accumulate.rvshader: the motion lane')
