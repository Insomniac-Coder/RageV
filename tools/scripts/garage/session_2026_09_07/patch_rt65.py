# -*- coding: utf-8 -*-
"""RT-6.5: the accumulator tests the material, not just the roughness.

**The gap, from §4C of the owner's specular specification.** Every history test
the reflection accumulator has asks about the reflector's *geometry* -- same
plane, same facing -- with one material question, roughness, and a tolerance of
0.5 on it. Roughness does not separate a metal from a dielectric. A brushed
steel panel and a painted one at the same roughness pass every test the
accumulator has, and they shade nothing alike: one is a mirror of the room, the
other is mostly its own albedo. At the boundary between them the history is
accepted and the wrong picture is dragged across.

**Where it goes, and the shape that was rejected.** The contract's three
attachments are full, and RT-12 took the last two spare channels in the motion
lane for §11's direction. So the choice was:

  *(a) a fifth attachment* -- an honest place for a surface id **and** the
  metallic, and a real cost: the contract is shared, so it is bandwidth on four
  signals every frame for one signal's test. Both outside reviews warn about
  exactly this lane growth (§20), and **RT-14 is filed to measure the G-buffer's
  bandwidth before anything is added to it**. Adding a lane the week before
  measuring whether the lanes are affordable is the wrong order.

  *(b) the metallic packed into the roughness channel's integer part* -- free,
  and it addresses the case §4C actually names. `o_Extra.r` has exactly one
  reader and one writer, and roughness lives in [0,1] with the whole integer
  part unused. A half float at magnitude 2-3 still resolves about 0.002, which
  is far finer than a test whose tolerance is 0.5.

(b), and the surface id is **deferred to after RT-14** rather than dropped --
recorded in the item, with the reason, so it is a decision and not an omission.

**And it fades rather than snapping**, like everything RT-6.4 touched: a
mismatch takes the memory down to a floor instead of refusing the history, so a
material boundary gets a short memory rather than the noise of a one-frame
estimate on the surfaces that show noise worst.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
s = read(S)
if has(s, 'kMaterialAgree'):
    print('already done')
    raise SystemExit(0)

# --- the constant and the unpack ------------------------------------------
s = rep(s,
    "bool HistoryAt(vec3 world, ivec2 size, vec3 P, vec3 N, float roughness,\n",
    "// **RT-6.5: how much a history is worth when the material underneath it\n"
    "// changed.** A metal and a dielectric at the same roughness pass every\n"
    "// other test this accumulator has and shade nothing alike -- one is a\n"
    "// mirror of the room, the other is mostly its own albedo.\n"
    "//\n"
    "// Scaled, not refused, for RT-6.3's reason: refusing trades a smear for the\n"
    "// noise of a one-frame estimate, on exactly the surfaces that show noise\n"
    "// worst. A sixth of the memory is short enough that the fresh rays take\n"
    "// over within a few frames and long enough that they are not read alone.\n"
    "const float kMaterialAgree = 0.15;\n"
    "\n"
    "// **The roughness channel carries the metallic in its integer part.**\n"
    "// Roughness is in [0,1] and the whole integer part of `o_Extra.r` was\n"
    "// unused; a half float at magnitude two still resolves about 0.002, which\n"
    "// is far finer than a test whose tolerance is 0.5. The alternative was a\n"
    "// fifth attachment on a contract four signals share, a week before RT-14\n"
    "// measures whether the lanes already there are affordable.\n"
    "float PackMaterial(float roughness, float metallic)\n"
    "{\n"
    "\treturn clamp(roughness, 0.0, 1.0) + (metallic >= 0.5 ? 2.0 : 0.0);\n"
    "}\n"
    "\n"
    "bool  UnpackMetal(float packed)     { return packed >= 1.5; }\n"
    "float UnpackRoughness(float packed) { return packed - (packed >= 1.5 ? 2.0 : 0.0); }\n"
    "\n"
    "bool HistoryAt(vec3 world, ivec2 size, vec3 P, vec3 N, float roughness,\n")

# --- the signature takes the metallic --------------------------------------
s = rep(s,
    "bool HistoryAt(vec3 world, ivec2 size, vec3 P, vec3 N, float roughness,\n"
    "bool HistoryAt(vec3 world, ivec2 size, vec3 P, vec3 N, float roughness,\n",
    "bool HistoryAt(vec3 world, ivec2 size, vec3 P, vec3 N, float roughness,\n") \
    if s.count("bool HistoryAt(vec3 world, ivec2 size, vec3 P, vec3 N, float roughness,\n") > 1 else s

s = read(S) if False else s

# --- the gate reads the unpacked roughness, and the metal joins the confidence
s = rep(s,
    "\t\t\tif (abs(c.extra.r - roughness) > 0.5)\n"
    "\t\t\t{\n"
    "\t\t\t\tif (k == 0) g_Refusal = 5;\n"
    "\t\t\t\tcontinue;\n"
    "\t\t\t}\n",
    "\t\t\t// RT-6.5: the same cutoff, on the roughness unpacked from beside the\n"
    "\t\t\t// metallic. Unchanged in behaviour -- what is new is the bit above it.\n"
    "\t\t\tif (abs(UnpackRoughness(c.extra.r) - roughness) > 0.5)\n"
    "\t\t\t{\n"
    "\t\t\t\tif (k == 0) g_Refusal = 5;\n"
    "\t\t\t\tcontinue;\n"
    "\t\t\t}\n")

s = rep(s,
    "\t\t\tc.matchConfidence = smoothstep(0.8, 0.94, facing)\n"
    "\t\t\t\t\t\t\t  * (1.0 - smoothstep(0.35 * planeTolerance, planeTolerance,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t  offPlane));\n",
    "\t\t\t//\n"
    "\t\t\t// **RT-6.5: and by whether it is even the same kind of surface.** The\n"
    "\t\t\t// one axis nothing here has ever tested: a metal/dielectric boundary\n"
    "\t\t\t// passes the normal, the plane and the roughness, and the two sides\n"
    "\t\t\t// reflect nothing alike. Multiplied in like the rest, so a candidate\n"
    "\t\t\t// marginal on two counts is worth less than one marginal on either.\n"
    "\t\t\tconst float material = UnpackMetal(c.extra.r) == (g_Metallic >= 0.5)\n"
    "\t\t\t\t\t\t\t\t ? 1.0 : kMaterialAgree;\n"
    "\t\t\tc.matchConfidence = smoothstep(0.8, 0.94, facing)\n"
    "\t\t\t\t\t\t\t  * (1.0 - smoothstep(0.35 * planeTolerance, planeTolerance,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t  offPlane))\n"
    "\t\t\t\t\t\t\t  * material;\n")

# --- this pixel's metallic, as a global beside the other two ---------------
s = rep(s,
    "vec2 g_ImageMotion = vec2(0.0);\n",
    "vec2 g_ImageMotion = vec2(0.0);\n"
    "// RT-6.5: this pixel's metallic, for the history gate. A global for the\n"
    "// same reason the motion is one -- HistoryAt is called from two places and\n"
    "// threading one more parameter through both says nothing the name does not.\n"
    "float g_Metallic = 0.0;\n")

# --- filled where the surface is read, and written back out ----------------
s = rep(s,
    "\tconst vec4 surface = texelFetch(u_Surface, texel, 0);\n"
    "\tconst float roughness = clamp(surface.b, 0.0, 1.0);\n",
    "\tconst vec4 surface = texelFetch(u_Surface, texel, 0);\n"
    "\tconst float roughness = clamp(surface.b, 0.0, 1.0);\n"
    "\t// RT-6.5: the G-buffer's normal attachment carries the metallic in a,\n"
    "\t// beside the roughness in b. Read here once, used by every candidate.\n"
    "\tg_Metallic = clamp(surface.a, 0.0, 1.0);\n")

s = rep(s,
    "\to_Extra = vec4(roughness, momMean, momMeanSq, 0.5 * choice + float(g_Refusal));\n",
    "\t// RT-6.5: the roughness with the metallic in its integer part, so the\n"
    "\t// next frame can tell a metal from a dielectric at the same roughness.\n"
    "\to_Extra = vec4(PackMaterial(roughness, g_Metallic), momMean, momMeanSq,\n"
    "\t\t\t\t   0.5 * choice + float(g_Refusal));\n")

write(S, s)
print('reflection_accumulate.rvshader: the material joins the match confidence')
