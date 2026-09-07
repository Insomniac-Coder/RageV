# -*- coding: utf-8 -*-
"""RT-6.10: the accumulator validates what the ray *hit*.

**The hole, and it is the one RT-6.3 cannot close by construction.** Every test
this pass has asks about the *reflector*: is it the same plane, facing the same
way, as rough, the same material, the same object. RT-6.3 added the reflection
*direction*, which catches a camera orbiting a mirror. All of them pass in this
case:

    a polished wall that does not move
    a camera that does not move
    a moving object in the reflection

The reflector is identical, so every reflector test passes. The eye did not
move, so `R = reflect(-V, N)` did not swing and the direction test passes too.
The memory stays full and the moving object smears across the wall. Both outside
reviews found this independently.

**What tells you, and it is already stored.** `o_Surface.a` is the virtual image
distance -- how far behind the surface the reflected picture sits, which for a
flat mirror is the ray's own travel. When the thing being reflected moves toward
or away from the mirror, that distance changes. The accumulator has been
computing it every frame and **smoothing** it into the history rather than
comparing it:

    image = mix(atSurface.reflector.a, image, 1 / min(frames + 1, 8))

so a moving reflected object drags the stored distance along instead of being
noticed. The fresh value is kept before that blend and compared.

**Scaled by roughness, because the two ends are genuinely different.** A mirror
sends one ray to one place and its hit distance is steady, so a change means the
scene changed. A rough surface samples a wide lobe and its hit distance varies
enormously between neighbouring frames for no reason at all -- testing it there
would refuse history for the sampling, not for the content. So the tolerance is
loose where the lobe is wide, and the whole term is folded into RT-6.4's match
confidence rather than refusing the history outright, for RT-6.3's reason:
refusing trades a smear for the noise of a one-frame estimate on exactly the
surfaces that show noise worst.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
s = read(S)
if has(s, 'g_FreshImage'):
    print('already done')
    raise SystemExit(0)

s = rep(s,
    "float g_ObjectId = 0.0;\n",
    "float g_ObjectId = 0.0;\n"
    "// RT-6.10: this frame's raw image distance, before it is blended into the\n"
    "// history's. The comparison has to be against the *fresh* value -- the\n"
    "// blended one has already been dragged toward whatever the history holds,\n"
    "// which is the thing being tested.\n"
    "float g_FreshImage = 0.0;\n")

s = rep(s,
    "const float kIdAgree = 0.25;\n",
    "const float kIdAgree = 0.25;\n"
    "\n"
    "// **RT-6.10: how far the reflected picture may move before the history\n"
    "// stops describing it**, as a fraction of the distance itself.\n"
    "//\n"
    "// A mirror sends one ray to one place: its hit distance is steady frame to\n"
    "// frame, so a change of a few per cent means the *scene* changed -- an\n"
    "// object moved in the reflection, which is the case no reflector test can\n"
    "// see. A rough surface samples a wide lobe and its hit distance swings\n"
    "// wildly between frames for no reason but the sampling, so the same test\n"
    "// there would refuse history for noise. Hence the two ends, and the\n"
    "// interpolation between them.\n"
    "const float kMirrorHitTolerance = 0.05;\n"
    "const float kRoughHitTolerance = 1.50;\n"
    "\n"
    "// One where the reflected picture is where it was, falling to a floor as it\n"
    "// moves. Smooth for RT-6.4's reason: a cutoff on a continuous quantity makes\n"
    "// the memory step out as something crosses the reflection, which reads as a\n"
    "// pop rather than as a reflection catching up.\n"
    "const float kHitAgree = 0.2;\n"
    "\n"
    "float HitConfidence(float nowImage, float wasImage, float roughness)\n"
    "{\n"
    "\t// Relative, not absolute: a metre of change matters on a reflection two\n"
    "\t// metres deep and not on one forty metres deep. The floor in the divisor\n"
    "\t// keeps a reflection sitting on the surface from dividing by nothing.\n"
    "\tconst float moved = abs(nowImage - wasImage)\n"
    "\t\t\t\t\t  / max(max(nowImage, wasImage), 0.25);\n"
    "\tconst float tolerance = mix(kMirrorHitTolerance, kRoughHitTolerance,\n"
    "\t\t\t\t\t\t\t\tclamp(roughness, 0.0, 1.0));\n"
    "\treturn mix(1.0, kHitAgree, smoothstep(tolerance, tolerance * 3.0, moved));\n"
    "}\n")

s = rep(s,
    "\t\t\tconst float wasId = texelFetch(u_HistoryIdent, pastTexel, 0).r;\n",
    "\t\t\t// **RT-6.10: and has what the ray hits moved?** The one question the\n"
    "\t\t\t// reflector tests cannot ask -- a static mirror, a static camera and a\n"
    "\t\t\t// moving object in the reflection pass every one of them.\n"
    "\t\t\tconst float hit = HitConfidence(g_FreshImage, c.reflector.a, roughness);\n"
    "\t\t\tconst float wasId = texelFetch(u_HistoryIdent, pastTexel, 0).r;\n")

s = rep(s,
    "\t\t\t\t\t\t\t  * material * idPenalty;\n",
    "\t\t\t\t\t\t\t  * material * idPenalty * hit;\n")

# the fresh value, kept before the blend that would hide it
s = rep(s,
    "\tvec3 kept = fresh.rgb;\n",
    "\t// RT-6.10: kept before anything blends it. Below, `image` is mixed toward\n"
    "\t// the history's own so the parallax is steady frame to frame; the test\n"
    "\t// needs the value *this frame's ray* produced.\n"
    "\tg_FreshImage = image;\n"
    "\n"
    "\tvec3 kept = fresh.rgb;\n")

write(S, s)
print('reflection_accumulate.rvshader: the reflected content is validated')
