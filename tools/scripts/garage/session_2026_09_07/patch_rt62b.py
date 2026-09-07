"""RT-6.2, part B: the clamp is shaped by the material under the pixel."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/taa_resolve.rvshader'
s = read(S)
if has(s, 'u_Material'):
    print('taa_resolve.rvshader already reads the material')
    raise SystemExit(0)

s = rep(s,
    "layout(set = 0, binding = 7) uniform sampler2D u_GuidePrevious;\n",
    "layout(set = 0, binding = 7) uniform sampler2D u_GuidePrevious;\n"
    "\n"
    "// **RT-6.2: what the surface under this pixel is made of.** The G-buffer's\n"
    "// normal attachment, whose B is roughness and A is metallic -- the same lane\n"
    "// every other pass reads its normal from, so this costs a binding and no\n"
    "// pass. Point sampled: halfway between two materials is a third material\n"
    "// that is not there.\n"
    "layout(set = 0, binding = 8) uniform sampler2D u_Material;\n")

s = rep(s,
    "\tfloat Geometry;\n"
    "} u_Params;\n",
    "\tfloat Geometry;\n"
    "\t// RT-6.2: one when the material lane is bound.\n"
    "\tfloat Material;\n"
    "\tfloat Pad2;\n"
    "\tfloat Pad3;\n"
    "} u_Params;\n"
    "\n"
    "// **How far the box may open on a surface whose shading does not depend on\n"
    "// where the eye is.**\n"
    "//\n"
    "// The neighbourhood box exists to catch a history that has gone wrong. On a\n"
    "// smooth metal it earns that every frame: the pixel's colour is a function\n"
    "// of view direction, so last frame's is genuinely a different answer and the\n"
    "// box cannot be too tight. On rough concrete it does not: the shading is the\n"
    "// same from any angle, the history is *right*, and clipping it toward the\n"
    "// local mean every frame is not protection -- it is the blur. It removes\n"
    "// precisely the detail the accumulation spent thirty frames building, which\n"
    "// is why a wet floor's gravel and a brick wall's grain go soft under motion\n"
    "// while a chrome pole stays crisp.\n"
    "//\n"
    "// Two is the width, and what stops it ghosting is that everything else the\n"
    "// resolve does still holds: the geometric test (RT-6) has already refused a\n"
    "// history that is a different surface, the pixel's own temporal spread still\n"
    "// sets the floor, and light that genuinely changes on a static rough surface\n"
    "// -- a shadow crossing a floor -- moves the whole neighbourhood, so the box\n"
    "// moves with it rather than being escaped.\n"
    "const float kStableWiden = 2.0;\n")

s = rep(s,
    "\tconst vec3 boxCentre = 0.5 * (lowest + highest);\n"
    "\tvec3 boxExtent = max(0.5 * (highest - lowest), vec3(1e-5));\n",
    "\tconst vec3 boxCentre = 0.5 * (lowest + highest);\n"
    "\tvec3 boxExtent = max(0.5 * (highest - lowest), vec3(1e-5));\n"
    "\n"
    "\t// **RT-6.2: widened by how view-stable the surface is.** Rough carries it\n"
    "\t// and metal takes it away -- a rough *metal* is still a metal, its lobe is\n"
    "\t// wide but it is entirely a function of the view. The window starts where\n"
    "\t// a surface stops behaving like a mirror (0.15, the gloss threshold the\n"
    "\t// reflection accumulator uses for the same distinction) and is fully open\n"
    "\t// by half rough, which is ordinary concrete, brick and paint.\n"
    "\tif (u_Params.Material > 0.5)\n"
    "\t{\n"
    "\t\tconst vec4 material = texelFetch(u_Material, ivec2(gl_FragCoord.xy), 0);\n"
    "\t\tconst float stability = (1.0 - clamp(material.a, 0.0, 1.0))\n"
    "\t\t\t\t\t\t\t  * smoothstep(0.15, 0.5, clamp(material.b, 0.0, 1.0));\n"
    "\t\tboxExtent *= mix(1.0, kStableWiden, stability);\n"
    "\t}\n")

write(S, s)
print('taa_resolve.rvshader: the clamp is material aware')
