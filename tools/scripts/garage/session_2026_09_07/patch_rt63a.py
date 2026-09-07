"""RT-6.3: the reflection accumulator validates the reflection *direction*.

**The gap, named exactly.** The accumulator tests whether the history it
reprojected to is the same *reflector* -- same normal, same plane, about as
rough -- and every one of those tests passes on a smooth metal that the camera
is orbiting. The surface has not changed at all. What has changed is the
reflection: R = reflect(-V, N) swings with the view direction, and on a mirror a
degree of view change is a completely different image. So the history is
accepted, blended, and the old image is dragged across the metal. **That is why
smearing is worst on shiny surfaces and mild on rough ones**, and no amount of
surface validation can see it, because the surface is innocent.

**What this adds.** The reflector's normal is already stored, and the previous
camera position is now recorded beside the previous view-projection, so the
previous reflection direction is recoverable exactly:

    R_prev = reflect(normalize(P - eyePrev), N_stored)
    R_now  = reflect(sight, N)

and their agreement scaled by how wide the lobe is. A mirror needs the two to
agree almost exactly; a rough surface integrates over so much of the hemisphere
that a large swing changes little. Smooth, not a cutoff: a hard threshold on a
continuous quantity makes the memory pop as the camera turns.

The confidence multiplies the memory rather than refusing the history outright.
Refusing would trade a smear for the noise of a one-frame estimate on the very
surfaces that show noise worst; shortening the memory lets the fresh rays take
over at the rate the reflection is actually changing.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# --- the previous camera position, recorded and passed -------------------
T = 'RageV/src/RageV/Renderer/TemporalHistory.h'
s = read(T)
if not has(s, 'Vec4 Eye'):
    s = rep(s,
        "\tstruct CameraMotion\n"
        "\t{\n"
        "\t\tMat4 ViewProjection{ 1.0f };\n"
        "\t\tVec2 Jitter{ 0.0f, 0.0f };\n"
        "\t};\n",
        "\tstruct CameraMotion\n"
        "\t{\n"
        "\t\tMat4 ViewProjection{ 1.0f };\n"
        "\t\tVec2 Jitter{ 0.0f, 0.0f };\n"
        "\t\t// RT-6.3: where the eye was. The reflection accumulator needs it to\n"
        "\t\t// rebuild last frame's reflection direction -- the one thing that\n"
        "\t\t// changes on a mirror the camera orbits while every surface test it\n"
        "\t\t// has says nothing has changed at all.\n"
        "\t\tVec4 Eye{ 0.0f, 0.0f, 0.0f, 0.0f };\n"
        "\t};\n")
    write(T, s)
    print('TemporalHistory.h: CameraMotion carries the eye')
else:
    print('TemporalHistory.h already done')

R = 'RageV/src/RageV/Renderer/Renderer3D.cpp'
s = read(R)
if not has(s, 'motion->Eye = '):
    s = rep(s,
        "\t\t\tmotion->ViewProjection = viewProjection;\n"
        "\t\t\tmotion->Jitter = jitter;\n",
        "\t\t\tmotion->ViewProjection = viewProjection;\n"
        "\t\t\tmotion->Jitter = jitter;\n"
        "\t\t\t// RT-6.3: and where it was seen from.\n"
        "\t\t\tmotion->Eye = Vec4(cameraTransform[3][0], cameraTransform[3][1],\n"
        "\t\t\t\t\t\t\t   cameraTransform[3][2], 1.0f);\n")
    s = rep(s,
        "\t\t\tVec4 Blur{ 12.0f, 32.0f, 1.5f, 8.0f };\n"
        "\t\t};\n",
        "\t\t\tVec4 Blur{ 12.0f, 32.0f, 1.5f, 8.0f };\n"
        "\t\t\t// RT-6.3: last frame's eye in xyz; w is one when it is real, so the\n"
        "\t\t\t// first frame of a chain does not compare against the origin.\n"
        "\t\t\tVec4 PreviousEye{ 0.0f, 0.0f, 0.0f, 0.0f };\n"
        "\t\t};\n")
    # Three call sites share the push block (the accumulate, the blur and the
    # pair's); filling it in all three keeps the struct meaning one thing.
    s = rep(s,
        "\t\tpush.PreviousViewProjection = motion.ViewProjection;\n",
        "\t\tpush.PreviousViewProjection = motion.ViewProjection;\n"
        "\t\tpush.PreviousEye = motion.Eye;\n", 3)
    write(R, s)
    print('Renderer3D.cpp: the eye recorded and pushed')
else:
    print('Renderer3D.cpp already done')

# --- and the test itself -------------------------------------------------
S = 'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
s = read(S)
if has(s, 'kMirrorSensitivity'):
    print('reflection_accumulate.rvshader already validates the direction')
    raise SystemExit(0)

s = rep(s,
    "\tvec4  Blur;\n",
    "\tvec4  Blur;\n"
    "\t// RT-6.3: last frame's eye, w one when it is real.\n"
    "\tvec4  PreviousEye;\n")

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
    "// **How far the reflection direction may swing before the history stops\n"
    "// describing the same reflection** -- as a cosine, and scaled by the lobe.\n"
    "//\n"
    "// A mirror's lobe is a line: a degree of swing is a different image\n"
    "// entirely, and the number below is cos(1.8 degrees). A rough surface\n"
    "// integrates over most of a hemisphere, so the same swing changes almost\n"
    "// nothing and its history stays worth the whole of its memory. Everything\n"
    "// between is the interpolation.\n"
    "//\n"
    "// These are the two numbers to tune if a scene smears or pops: raise the\n"
    "// mirror end toward 1 for a stricter mirror, lower the rough end for a\n"
    "// longer memory on matte metal.\n"
    "const float kMirrorSensitivity = 0.9995;\n"
    "const float kRoughSensitivity = 0.86;\n"
    "\n"
    "// One where last frame's reflection is still this frame's, falling smoothly\n"
    "// to zero as the two directions part. Smooth on purpose: a cutoff on a\n"
    "// continuous quantity makes the memory drop out in a step as the camera\n"
    "// turns, which reads as a pop rather than as a reflection catching up.\n"
    "float DirectionConfidence(vec3 nowDir, vec3 thenDir, float roughness)\n"
    "{\n"
    "\tconst float agree = dot(nowDir, thenDir);\n"
    "\tconst float floorAgree = mix(kMirrorSensitivity, kRoughSensitivity,\n"
    "\t\t\t\t\t\t\t\t clamp(roughness, 0.0, 1.0));\n"
    "\treturn smoothstep(floorAgree, mix(floorAgree, 1.0, 0.5), agree);\n"
    "}\n")

write(S, s)
print('reflection_accumulate.rvshader: the direction test declared')
