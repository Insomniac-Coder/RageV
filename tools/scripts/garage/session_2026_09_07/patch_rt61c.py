"""RT-6.1, part C: the composite emits the motion the resolve should use."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/reflection_composite.rvshader'
s = read(S)
if has(s, 'o_Motion'):
    print('reflection_composite.rvshader already emits the motion')
    raise SystemExit(0)

s = rep(s,
    "layout(location = 0) in vec2 v_UV;\n"
    "layout(location = 0) out vec4 o_Color;\n",
    "layout(location = 0) in vec2 v_UV;\n"
    "layout(location = 0) out vec4 o_Color;\n"
    "// **RT-6.1: the motion the temporal resolve should reproject this pixel by.**\n"
    "// The scene's velocity lane where the pixel is mostly its surface, the\n"
    "// virtual image's where it is mostly reflection. Which is what lets this\n"
    "// pass run *before* the resolve instead of after it -- see the note at the\n"
    "// top, and RT-6.1's record for what happens without it.\n"
    "layout(location = 1) out vec2 o_Motion;\n")

s = rep(s,
    "layout(set = 0, binding = 1) uniform sampler2D u_Reflection;\n",
    "layout(set = 0, binding = 1) uniform sampler2D u_Reflection;\n"
    "// The accumulator's fourth lane: how far the picture at this texel moved,\n"
    "// already in the velocity lane's units. Zero where it had no history.\n"
    "layout(set = 0, binding = 2) uniform sampler2D u_ReflectionMotion;\n"
    "// The scene's own velocity, for everything that is not mostly a reflection.\n"
    "layout(set = 0, binding = 3) uniform sampler2D u_Velocity;\n")

s = rep(s,
    "\tconst vec3 added = reflection.a > 0.0 ? max(reflection.rgb, vec3(0.0)) : vec3(0.0);\n"
    "\t// The weight has done its work; nothing after this reads the alpha.\n"
    "\to_Color = vec4(scene.rgb + max(scene.a, 0.0) * added, 0.0);\n",
    "\tconst vec3 added = reflection.a > 0.0 ? max(reflection.rgb, vec3(0.0)) : vec3(0.0);\n"
    "\tconst float weight = max(scene.a, 0.0);\n"
    "\t// The weight has done its work; nothing after this reads the alpha.\n"
    "\to_Color = vec4(scene.rgb + weight * added, 0.0);\n"
    "\n"
    "\t// **Which motion this pixel moves by -- chosen, never averaged.**\n"
    "\t//\n"
    "\t// A pixel is a surface plus a reflection of something else, and the two\n"
    "\t// move differently: as the eye slides, a tube's image slides across the\n"
    "\t// wet floor at its own rate while the floor slides at the floor's. There\n"
    "\t// is one history to fetch and one velocity to fetch it with, so the honest\n"
    "\t// answer is to follow whichever of the two the pixel is mostly made of.\n"
    "\t// Averaging them would give a velocity that describes neither -- the same\n"
    "\t// reason every velocity read in this engine is point sampled.\n"
    "\t//\n"
    "\t// The threshold is the reflection carrying more of the pixel's brightness\n"
    "\t// than everything else in it, which on the garage floor is the wet\n"
    "\t// concrete under the tubes and the car, and is not the dry wall behind a\n"
    "\t// faint sheen.\n"
    "\tconst vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);\n"
    "\tconst float reflected = dot(weight * added, kLuma);\n"
    "\tconst float rest = dot(max(scene.rgb, vec3(0.0)), kLuma);\n"
    "\tconst vec2 surfaceMotion = texture(u_Velocity, uv).xy;\n"
    "\tconst vec2 imageMotion = texture(u_ReflectionMotion, uv).xy;\n"
    "\t// A zero lane means the accumulator had no history to measure a shift\n"
    "\t// from; the surface's motion is the better guess then, not a standstill.\n"
    "\tconst bool haveImage = dot(imageMotion, imageMotion) > 0.0;\n"
    "\to_Motion = (reflected > rest && haveImage) ? imageMotion : surfaceMotion;\n")

write(S, s)
print('reflection_composite.rvshader: the motion output')
