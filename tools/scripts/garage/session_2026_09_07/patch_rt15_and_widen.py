# -*- coding: utf-8 -*-
"""Two owner decisions: file the reprojection defect, and take widen 8."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# ------------------------------------------------------- kStableWiden -> 8
S = 'RageVEditor/assets/shaders/taa_resolve.rvshader'
s = read(S)
if has(s, 'const float kStableWiden = 8.0;'):
    print('widen already 8')
else:
    s = rep(s,
        "// Two is the width, and what stops it ghosting is that everything else the\n"
        "// resolve does still holds: the geometric test (RT-6) has already refused a\n"
        "// history that is a different surface, the pixel's own temporal spread still\n"
        "// sets the floor, and light that genuinely changes on a static rough surface\n"
        "// -- a shadow crossing a floor -- moves the whole neighbourhood, so the box\n"
        "// moves with it rather than being escaped.\n"
        "const float kStableWiden = 2.0;\n",
        "// **Eight, and it is measured rather than guessed** (owner, 2026-09-07).\n"
        "// It sat at two only because a wider clamp *might* smear behind a moving\n"
        "// object and no scene here had one. `showroom_moving.rage` now does -- a\n"
        "// chrome cube crossing a matte wall with the camera still -- and in the band\n"
        "// the cube has just vacated the residue against the no-history truth is\n"
        "// **8.56 levels at widen 1, 2, 4 and 8 alike**. The arms are live (1 against\n"
        "// 8 differs by 0.108 levels across the frame) and differ by 0.021 in the\n"
        "// trail: the clamp width simply does not reach the ghost.\n"
        "//\n"
        "// What stops it ghosting is everything else the resolve does: RT-6's\n"
        "// geometric test has already refused a history that is a different surface,\n"
        "// the pixel's own temporal spread still sets the floor, and light that\n"
        "// genuinely changes on a static rough surface -- a shadow crossing a floor\n"
        "// -- moves the whole neighbourhood, so the box moves with it rather than\n"
        "// being escaped. Worth +1.43% detail over widen 1 on the garage dolly.\n"
        "const float kStableWiden = 8.0;\n")
    write(S, s)
    print('kStableWiden 2.0 -> 8.0')

# ---------------------------------------------------------------- RT-15
D = 'docs/RT-SERIES.md'
s = read(D)
if has(s, 'RT-15'):
    print('RT-15 already filed')
    raise SystemExit(0)

row = ("| **RT-15** | **The reflection accumulator reprojects by object motion, not only the camera's.** "
       "It finds last frame's texel by pushing this frame's world position through last frame's "
       "view-projection -- which asks where the point *would* have been if it had not moved. For "
       "anything that moves it was somewhere else, so every history test refuses, `frames` falls to "
       "one, and the pixel shows a **single ray**: noise on a mirror, and the ghosting the owner sees "
       "while driving the car. `u_Velocity` is already bound to the pass and used only for the "
       "silhouette test. The fix is to reproject by that lane where it describes object motion, and "
       "to decide what a *rotating* reflector does, which a screen-space velocity cannot express. "
       "**Measure on `showroom_moving.rage`**, which is the only scene that shows it. | "
       "found 2026-09-07 by the moving-object scene | Every reflection on anything that moves is "
       "currently a one-sample estimate. | medium — **do early; it is the largest visible defect "
       "found this week** |\n")

s = rep(s, "| **RT-14** | **The G-buffer's bandwidth, measured before anything is packed.**",
        row + "| **RT-14** | **The G-buffer's bandwidth, measured before anything is packed.**")

s = rep(s,
    "| **RT-14** | ✅ **done 2026-09-07** — and it says do not pack the G-buffer | — | — | the G-buffer's bandwidth, measured before anything is packed |",
    "| **RT-14** | ✅ **done 2026-09-07** — and it says do not pack the G-buffer | — | — | the G-buffer's bandwidth, measured before anything is packed |\n"
    "| **RT-15** | open — **new, do early** | 2-3 d | moderate | the reflection accumulator reprojects by object motion |")

s = rep(s,
    "**Eighteen of twenty-eight items are closed, three more are part-done, seven are open. Every RT-6.x sub-item is finished.**",
    "**Eighteen of twenty-nine items are closed, three more are part-done, eight are open. Every RT-6.x sub-item is finished.**")

s = rep(s,
    "| RT-14 | 1 d to measure | low | The measurement *is* the item;",
    "| RT-15 | 2-3 d | moderate | The velocity lane is per pixel and screen-space, which describes a "
    "translating reflector and not a rotating one -- a turning mirror's history moves in a way no "
    "screen velocity encodes, and deciding what to do there is most of the design. The accumulator "
    "already reprojects by the *virtual image* for the camera's motion, so object motion has to "
    "compose with that rather than replace it. `showroom_moving.rage` is the arm. |\n"
    "| RT-14 | 1 d to measure | low | The measurement *is* the item;")

write(D, s)
print('RT-15 filed')
