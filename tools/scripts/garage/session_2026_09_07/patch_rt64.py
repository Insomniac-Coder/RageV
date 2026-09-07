"""RT-6.4: the current-sample filter comes down, and the history tests fade
instead of snapping.

**Two things, both owner-asked.**

*The filter.* `kFilterSigma` is a Gaussian over the 3x3 applied to the current
sample before it is blended -- Unreal's filtered-current, there so the jitter
does not move a bright edge a tenth of the way to whichever side it landed on.
Its cost is that it blurs every pixel of every frame, and RT-6.2 measured it as
the actual "TAA blurs essential detail" lever after the material-aware clamp
measured flat: at 0.15 instead of 0.5 the garage reads wall 22.86 -> 23.28,
floor 9.02 -> 9.53, poles 13.74 -> 14.32, for about four per cent more
frame-to-frame change.

*The confidences.* The accumulator's normal and plane tests are hard cutoffs:
a history is whole at dot 0.801 and gone at 0.799. Nothing in the picture
changes that sharply, so what the eye sees as the camera turns is the memory
dropping out in a step -- a pop rather than a reflection catching up. The tests
stay binary where they gate the neighbour search, because that search needs a
yes or a no; what becomes smooth is how much the *accepted* candidate is
trusted, folded into the memory exactly the way RT-6.3's direction confidence
is. A marginal match now gets a short memory instead of full trust or nothing.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# --- the current-sample filter -------------------------------------------
T = 'RageVEditor/assets/shaders/taa_resolve.rvshader'
s = read(T)
if not has(s, 'RT-6.4'):
    s = rep(s,
        "const float kFilterSigma = 0.5;\n",
        "// **RT-6.4: 0.5 -> 0.15** (owner-asked). This is the blur, and RT-6.2\n"
        "// found it by elimination: the material-aware clamp measured flat because\n"
        "// on a detailed surface the neighbourhood box is already wide, so the\n"
        "// clamp only bites where there is no detail to lose. This filter bites\n"
        "// everywhere, every frame. Narrowed, the garage reads wall 22.86 -> 23.28,\n"
        "// floor 9.02 -> 9.53, poles 13.74 -> 14.32, at about four per cent more\n"
        "// frame-to-frame change -- and the jitter's edge wobble that the filter\n"
        "// exists for is now also held by the geometric test and the neighbour\n"
        "// search (RT-6), which did not exist when 0.5 was chosen.\n"
        "const float kFilterSigma = 0.15;\n")
    write(T, s)
    print('taa_resolve.rvshader: kFilterSigma 0.5 -> 0.15')
else:
    print('taa_resolve.rvshader already done')

# --- the confidences ------------------------------------------------------
S = 'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
s = read(S)
if has(s, 'c.matchConfidence'):
    print('reflection_accumulate.rvshader already fades its tests')
    raise SystemExit(0)

s = rep(s,
    "\tivec2 pastTexel;  // the texel the candidate came from (RV_SIGNAL_PAIR reads its twin there)\n"
    "\tbool  bilinear;   // whether the picture was sampled at pastUv rather than fetched\n"
    "};\n",
    "\tivec2 pastTexel;  // the texel the candidate came from (RV_SIGNAL_PAIR reads its twin there)\n"
    "\tbool  bilinear;   // whether the picture was sampled at pastUv rather than fetched\n"
    "\t// **RT-6.4: how well it matched, not just that it did.** One where the\n"
    "\t// reflector is the same to well inside the tolerances, falling to zero at\n"
    "\t// their edge. The tests below stay binary because the neighbour search\n"
    "\t// needs a yes or a no; this is how much the winner is then trusted.\n"
    "\tfloat matchConfidence;\n"
    "};\n")

s = rep(s,
    "\t\tif (!(silhouette && k == 0))\n"
    "\t\t{\n"
    "\t\t\tconst vec3 wasN = OctDecode(c.reflector.rg);\n"
    "\t\t\tif (dot(wasN, N) < 0.8)\n"
    "\t\t\t{\n"
    "\t\t\t\tif (k == 0) g_Refusal = 3;\n"
    "\t\t\t\tcontinue;\n"
    "\t\t\t}\n"
    "\t\t\tif (abs(dot(wasN, P) - c.reflector.b) > 0.05 + 0.01 * eyeDistance)\n"
    "\t\t\t{\n"
    "\t\t\t\tif (k == 0) g_Refusal = 4;\n"
    "\t\t\t\tcontinue;\n"
    "\t\t\t}\n"
    "\t\t\tif (abs(c.extra.r - roughness) > 0.5)\n"
    "\t\t\t{\n"
    "\t\t\t\tif (k == 0) g_Refusal = 5;\n"
    "\t\t\t\tcontinue;\n"
    "\t\t\t}\n"
    "\t\t}\n",
    "\t\tc.matchConfidence = 1.0;\n"
    "\t\tif (!(silhouette && k == 0))\n"
    "\t\t{\n"
    "\t\t\tconst vec3 wasN = OctDecode(c.reflector.rg);\n"
    "\t\t\tconst float facing = dot(wasN, N);\n"
    "\t\t\tif (facing < 0.8)\n"
    "\t\t\t{\n"
    "\t\t\t\tif (k == 0) g_Refusal = 3;\n"
    "\t\t\t\tcontinue;\n"
    "\t\t\t}\n"
    "\t\t\tconst float planeTolerance = 0.05 + 0.01 * eyeDistance;\n"
    "\t\t\tconst float offPlane = abs(dot(wasN, P) - c.reflector.b);\n"
    "\t\t\tif (offPlane > planeTolerance)\n"
    "\t\t\t{\n"
    "\t\t\t\tif (k == 0) g_Refusal = 4;\n"
    "\t\t\t\tcontinue;\n"
    "\t\t\t}\n"
    "\t\t\tif (abs(c.extra.r - roughness) > 0.5)\n"
    "\t\t\t{\n"
    "\t\t\t\tif (k == 0) g_Refusal = 5;\n"
    "\t\t\t\tcontinue;\n"
    "\t\t\t}\n"
    "\t\t\t// **RT-6.4: and how well, not merely whether.** A history is whole at\n"
    "\t\t\t// dot 0.801 and gone at 0.799 with a cutoff, and nothing in a picture\n"
    "\t\t\t// changes that sharply -- what the eye sees as the camera turns is the\n"
    "\t\t\t// memory stepping out rather than a reflection catching up. Full trust\n"
    "\t\t\t// well inside each tolerance, falling to none at its edge.\n"
    "\t\t\tc.matchConfidence = smoothstep(0.8, 0.94, facing)\n"
    "\t\t\t\t\t\t\t  * (1.0 - smoothstep(0.35 * planeTolerance, planeTolerance,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t  offPlane));\n"
    "\t\t}\n")

s = rep(s,
    "\t\t\tif (u_Reflection.PreviousEye.w > 0.5)\n"
    "\t\t\t{\n"
    "\t\t\t\tconst vec3 wasN = OctDecode(c.reflector.rg);\n"
    "\t\t\t\tconst vec3 thenSight = normalize(P - u_Reflection.PreviousEye.xyz);\n"
    "\t\t\t\tconst float confidence =\n"
    "\t\t\t\t\tDirectionConfidence(reflect(sight, N), reflect(thenSight, wasN),\n"
    "\t\t\t\t\t\t\t\t\t\troughness);\n"
    "\t\t\t\tmemory = max(mix(fewest, memory, confidence), fewest);\n"
    "\t\t\t}\n",
    "\t\t\tif (u_Reflection.PreviousEye.w > 0.5)\n"
    "\t\t\t{\n"
    "\t\t\t\tconst vec3 wasN = OctDecode(c.reflector.rg);\n"
    "\t\t\t\tconst vec3 thenSight = normalize(P - u_Reflection.PreviousEye.xyz);\n"
    "\t\t\t\tconst float confidence =\n"
    "\t\t\t\t\tDirectionConfidence(reflect(sight, N), reflect(thenSight, wasN),\n"
    "\t\t\t\t\t\t\t\t\t\troughness);\n"
    "\t\t\t\tmemory = max(mix(fewest, memory, confidence), fewest);\n"
    "\t\t\t}\n"
    "\t\t\t// RT-6.4: and by how well the reflector itself matched. Multiplied\n"
    "\t\t\t// rather than taken as a minimum: a candidate that is marginal on two\n"
    "\t\t\t// counts at once is worth less than one marginal on either.\n"
    "\t\t\tmemory = max(mix(fewest, memory, c.matchConfidence), fewest);\n")

write(S, s)
print('reflection_accumulate.rvshader: the tests fade')
