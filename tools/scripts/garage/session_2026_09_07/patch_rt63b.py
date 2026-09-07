"""RT-6.3, part B: the direction test in the shader, and its effect on memory."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
s = read(S)
if has(s, 'kMirrorSensitivity'):
    print('already done')
    raise SystemExit(0)

# --- the push field ------------------------------------------------------
s = rep(s, "\tvec4 Blur;\n",
           "\tvec4 Blur;\n"
           "\t// RT-6.3: last frame's eye in xyz, w one when it is real.\n"
           "\tvec4 PreviousEye;\n")

# --- the confidence ------------------------------------------------------
s = rep(s,
    "float Luma(vec3 c)\n"
    "{\n"
    "\treturn dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "}\n",
    "float Luma(vec3 c)\n"
    "{\n"
    "\treturn dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "}\n"
    "\n"
    "// **RT-6.3: how far the reflection direction may swing before last frame's\n"
    "// picture stops being this frame's reflection** -- as a cosine, scaled by\n"
    "// the lobe.\n"
    "//\n"
    "// Every other test here asks about the *reflector*: is it the same plane,\n"
    "// facing the same way, about as rough. On a smooth metal the camera is\n"
    "// orbiting, all of them pass -- the surface has not changed at all -- and\n"
    "// the reflection has changed completely, because R = reflect(-V, N) swings\n"
    "// with the view. The history is accepted and the old image is dragged\n"
    "// across the metal. That is why smearing is worst on shiny surfaces and\n"
    "// mild on rough ones, and no amount of surface validation can see it.\n"
    "//\n"
    "// A mirror's lobe is a line: cos(1.8 degrees) is the whole of its\n"
    "// tolerance. A rough surface integrates over most of a hemisphere, so the\n"
    "// same swing changes almost nothing and its history keeps its memory.\n"
    "// Smooth rather than a cutoff: a step on a continuous quantity makes the\n"
    "// memory drop out as the camera turns, which reads as a pop rather than as\n"
    "// a reflection catching up. These two are the numbers to tune.\n"
    "const float kMirrorSensitivity = 0.9995;\n"
    "const float kRoughSensitivity = 0.86;\n"
    "\n"
    "float DirectionConfidence(vec3 nowDir, vec3 thenDir, float roughness)\n"
    "{\n"
    "\tconst float agree = dot(nowDir, thenDir);\n"
    "\tconst float lowest = mix(kMirrorSensitivity, kRoughSensitivity,\n"
    "\t\t\t\t\t\t\t clamp(roughness, 0.0, 1.0));\n"
    "\treturn smoothstep(lowest, mix(lowest, 1.0, 0.5), agree);\n"
    "}\n")

# --- and it shortens the memory -----------------------------------------
s = rep(s,
    "\t\t\tmemory = max(memory, fewest);\n"
    "\t\t\tframes = min(c.past.a + 1.0, memory);\n",
    "\t\t\tmemory = max(memory, fewest);\n"
    "\n"
    "\t\t\t// **RT-6.3: and shortened by how much the reflection itself moved.**\n"
    "\t\t\t// The reflector's own normal is in the candidate, so last frame's\n"
    "\t\t\t// reflection direction is exact rather than guessed: the eye was\n"
    "\t\t\t// there, the surface faced that way, the mirror direction follows.\n"
    "\t\t\t//\n"
    "\t\t\t// The memory is scaled, not the history refused. Refusing would trade\n"
    "\t\t\t// a smear for the noise of a one-frame estimate on exactly the\n"
    "\t\t\t// surfaces that show noise worst; scaling lets the fresh rays take\n"
    "\t\t\t// over at the rate the reflection is actually changing. The floor is\n"
    "\t\t\t// `fewest`, which a mirror already sets to one.\n"
    "\t\t\tif (u_Reflection.PreviousEye.w > 0.5)\n"
    "\t\t\t{\n"
    "\t\t\t\tconst vec3 wasN = OctDecode(c.reflector.rg);\n"
    "\t\t\t\tconst vec3 thenSight = normalize(P - u_Reflection.PreviousEye.xyz);\n"
    "\t\t\t\tconst float confidence =\n"
    "\t\t\t\t\tDirectionConfidence(reflect(sight, N), reflect(thenSight, wasN),\n"
    "\t\t\t\t\t\t\t\t\t\troughness);\n"
    "\t\t\t\tmemory = max(mix(fewest, memory, confidence), fewest);\n"
    "\t\t\t}\n"
    "\t\t\tframes = min(c.past.a + 1.0, memory);\n")

write(S, s)
print('reflection_accumulate.rvshader: the direction test is in')
