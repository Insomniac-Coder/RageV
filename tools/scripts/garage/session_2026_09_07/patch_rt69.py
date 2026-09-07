# -*- coding: utf-8 -*-
"""RT-6.9: a camera cut throws the history away.

**The gap.** Every `Invalidate()` in the frame graph means "this filter did not
run this frame". Nothing detects a discontinuity in the *camera*: a teleport, a
scene load, a cut between two viewpoints. Reprojecting across one of those is
meaningless -- the velocity buffer describes a motion that never happened -- and
what it produces is a whole frame of smear that then takes thirty frames to
fade. It is the one temporal failure with no gradual version.

**Detection, and why it is a speed rather than a distance.** A cut is not "the
camera moved a long way", it is "the camera moved a long way *in one frame*". A
dolly at 1.5 m/s and a jump of eight metres are the same distance over enough
frames, so the test divides by `DeltaSeconds`. **A hundred metres a second** is
360 km/h: past anything a camera travels and comfortably clear of the fastest
thing in these scenes. The turn is the same argument -- **forty-five degrees in
a single frame** is 2700 degrees a second at sixty frames.

**Why `Forward` had to be stored.** `CameraMotion` kept the view-projection and
(since RT-6.3) the eye, but a previous *facing* is not recoverable from those
without inverting a matrix, so it is written beside the eye by the same code.

**What is not invalidated: the exposure.** A cut into a brighter room should
still adapt rather than snap, and how fast is a look decision that belongs to
whoever set the adaptation rate. The geometric histories have no such argument:
across a cut they hold a picture of somewhere else.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# --------------------------------------------------- the previous facing
T = 'RageV/src/RageV/Renderer/TemporalHistory.h'
s = read(T)
if not has(s, 'Vec4 Forward'):
    s = rep(s,
        "\t\tVec4 Eye{ 0.0f, 0.0f, 0.0f, 0.0f };\n",
        "\t\tVec4 Eye{ 0.0f, 0.0f, 0.0f, 0.0f };\n"
        "\t\t// RT-6.9: and which way it looked. A previous facing is not\n"
        "\t\t// recoverable from the view-projection without inverting it, and the\n"
        "\t\t// camera-cut test needs one; written beside the eye by the same code.\n"
        "\t\tVec4 Forward{ 0.0f, 0.0f, -1.0f, 0.0f };\n")
    write(T, s)
    print('TemporalHistory.h: CameraMotion carries the facing')

R = 'RageV/src/RageV/Renderer/Renderer3D.cpp'
s = read(R)
if not has(s, 'motion->Forward'):
    s = rep(s,
        "\t\t\tmotion->Eye = Vec4(cameraTransform[3][0], cameraTransform[3][1],\n"
        "\t\t\t\t\t\t\t   cameraTransform[3][2], 1.0f);\n",
        "\t\t\tmotion->Eye = Vec4(cameraTransform[3][0], cameraTransform[3][1],\n"
        "\t\t\t\t\t\t\t   cameraTransform[3][2], 1.0f);\n"
        "\t\t\t// RT-6.9: and which way it looked. The camera's -Z in world, the\n"
        "\t\t\t// same convention the light direction above uses.\n"
        "\t\t\tmotion->Forward = Vec4(-cameraTransform[2][0], -cameraTransform[2][1],\n"
        "\t\t\t\t\t\t\t\t   -cameraTransform[2][2], 0.0f);\n")
    write(R, s)
    print('Renderer3D.cpp: the facing is recorded')

# ------------------------------------------------------------- the test
F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if has(s, 'RT-6.9'):
    print('the cut test is already in')
    raise SystemExit(0)

anchor = "\t\tconst uint32_t velocityIndex = (uint32_t)sceneDesc.ExtraColors.size() + 1;\n"
s = rep(s, anchor,
    "\t\t// **RT-6.9: a camera cut throws every temporal history away.**\n"
    "\t\t//\n"
    "\t\t// Reprojecting across a teleport, a scene load or a cut between two\n"
    "\t\t// viewpoints is meaningless: the velocity buffer describes a motion that\n"
    "\t\t// never happened, and what comes out is a whole frame of smear that then\n"
    "\t\t// takes thirty frames to fade. It is the one temporal failure with no\n"
    "\t\t// gradual version, so it gets the one blunt response.\n"
    "\t\t//\n"
    "\t\t// A speed rather than a distance: a dolly and a jump cover the same\n"
    "\t\t// ground given enough frames, and it is the *rate* that makes a\n"
    "\t\t// reprojection nonsense. A hundred metres a second is 360 km/h, past\n"
    "\t\t// anything a camera travels here; forty-five degrees in one frame is\n"
    "\t\t// 2700 degrees a second at sixty.\n"
    "\t\t//\n"
    "\t\t// The exposure is deliberately left alone -- a cut into a brighter room\n"
    "\t\t// should still adapt rather than snap, and how fast is a look decision.\n"
    "\t\tif (desc.History && desc.DeltaSeconds > 1.0e-5f)\n"
    "\t\t{\n"
    "\t\t\tconst CameraMotion& was = desc.History->Motion();\n"
    "\t\t\tif (was.Eye.w > 0.5f && desc.History->HasHistory())\n"
    "\t\t\t{\n"
    "\t\t\t\tconst Mat4 toWorld = Math::Inverse(desc.View);\n"
    "\t\t\t\tconst Vec3 eyeNow(toWorld[3][0], toWorld[3][1], toWorld[3][2]);\n"
    "\t\t\t\tconst Vec3 facingNow = Math::Normalize(\n"
    "\t\t\t\t\tVec3(-toWorld[2][0], -toWorld[2][1], -toWorld[2][2]));\n"
    "\t\t\t\tconst Vec3 eyeWas(was.Eye.x, was.Eye.y, was.Eye.z);\n"
    "\t\t\t\tconst Vec3 facingWas = Math::Normalize(\n"
    "\t\t\t\t\tVec3(was.Forward.x, was.Forward.y, was.Forward.z));\n"
    "\t\t\t\tconstexpr float kCutMetresPerSecond = 100.0f;\n"
    "\t\t\t\tconstexpr float kCutFacing = 0.7071f;   // forty-five degrees\n"
    "\t\t\t\tconst float speed = Math::Length(eyeNow - eyeWas) / desc.DeltaSeconds;\n"
    "\t\t\t\tconst float turned = Math::Dot(facingNow, facingWas);\n"
    "\t\t\t\tif (speed > kCutMetresPerSecond || turned < kCutFacing)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tRV_CORE_TRACE(\"Camera cut: {0:.1f} m/s, facing dot {1:.3f}; \"\n"
    "\t\t\t\t\t\t\t\t  \"temporal histories dropped\", speed, turned);\n"
    "\t\t\t\t\tdesc.History->Invalidate();\n"
    "\t\t\t\t\tif (desc.Reflections)  desc.Reflections->Invalidate();\n"
    "\t\t\t\t\tif (desc.Indirect)     desc.Indirect->Invalidate();\n"
    "\t\t\t\t\tif (desc.TaaGuide)     desc.TaaGuide->Invalidate();\n"
    "\t\t\t\t\tif (desc.DirectLight)  desc.DirectLight->Invalidate();\n"
    "\t\t\t\t\tif (desc.GiLight)      desc.GiLight->Invalidate();\n"
    "\t\t\t\t\tif (desc.Occlusion)    desc.Occlusion->Invalidate();\n"
    "\t\t\t\t\tif (desc.RayBudget)    desc.RayBudget->Invalidate();\n"
    "\t\t\t\t}\n"
    "\t\t\t}\n"
    "\t\t}\n"
    "\n" + anchor)
write(F, s)
print('FrameGraphBuilder.cpp: the cut is detected')
