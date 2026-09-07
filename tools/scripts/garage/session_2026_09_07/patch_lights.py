# -*- coding: utf-8 -*-
"""Two things the owner asked for.

**1. The studio rig follows the car.** `Key Light`, `Kicker Left` and `Kicker
Right` are a three-point product rig aimed at the car -- a key above and in
front, two rim lights on the flanks. It was built centred on **x = 0**, and this
morning the car moved from x = -2.3 to **x = -4.3** while the rig stayed put.

In the tube-lit mode nothing shows, because all three sit at intensity 0. In the
studio mode the rig is broken: `Kicker Right` at x = +3.4 is **7.7 m from a car
it has a range of 7 to reach**, so it lights nothing at all; `Kicker Left` at
-3.4 has crossed to the car's inboard side instead of rimming its flank; the key
is off-centre by 4.3 m. The same -2 m the car, the headlamps and the tail lights
already took.

**2. A light at zero intensity stops participating.** Nothing filtered on
intensity anywhere -- not when the scene collects lights, not when they are
binned into clusters. A light at zero was still uploaded, still occupied cluster
cells across its whole `Range`, and was still walked and scored by every
fragment. It always lost the reservoir draw, because its contribution is zero;
it simply cost the walk. The garage measured **26.4 lights per fragment of 30,
eight of them guaranteed dark** -- most of a third of the scan.

**Why this is safe for the bake, which is the one thing it could break.**
`CollectLights()` feeds three callers, and one of them is `LightingHash()` --
change what it returns and a stale hash invalidates the baked field, which is
1283 frames to rebuild. It does not change here: the hash **already skips
Realtime lights** (Light.h), every zero-intensity light in this project is
Realtime, and every *baked* light in it has a non-zero intensity. A baked light
at zero would change the hash -- and should, because a light contributing
nothing is not part of the lighting the hash describes; it would cost one
re-bake, once.
"""
import sys, os, io, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# ------------------------------------------------------- 1. the rig moves
S = 'SampleProject/assets/scenes/showroom.rage'
s = io.open(S, encoding='utf-8', newline='').read()
MOVES = {'Key Light': ('[0, 5.2, 1.2]', '[-2, 5.2, 1.2]'),
         'Kicker Left': ('[-3.4, 1.5, -2.6]', '[-5.4, 1.5, -2.6]'),
         'Kicker Right': ('[3.4, 1.5, -2.6]', '[1.4, 1.5, -2.6]')}
moved = 0
for tag, (old, new) in MOVES.items():
    i = s.find('Tag: ' + tag)
    if i < 0:
        raise SystemExit('no entity tagged %r' % tag)
    block = s[i:i + 420]
    if 'Position: ' + new in block:
        print('%s already moved' % tag)
        continue
    if 'Position: ' + old not in block:
        raise SystemExit('%s is not at %s -- it reads:\n%s' % (tag, old, block[:220]))
    s = s[:i] + block.replace('Position: ' + old, 'Position: ' + new, 1) + s[i + 420:]
    moved += 1
    print('%-13s %s -> %s' % (tag, old, new))
if moved:
    io.open(S, 'w', encoding='utf-8', newline='').write(s)

# --------------------------------------------- 2. dark lights do not travel
C = 'RageV/src/RageV/Scene/Scene.cpp'
s = read(C)
if has(s, 'A light at zero intensity is not a light'):
    print('the filter is already in')
else:
    s = rep(s,
        "\t\t\tdata.Color = light.Light.Color;\n"
        "\t\t\tdata.Intensity = light.Light.Intensity;\n",
        "\t\t\tdata.Color = light.Light.Color;\n"
        "\t\t\tdata.Intensity = light.Light.Intensity;\n"
        "\t\t\t// **A light at zero intensity is not a light.** Nothing filtered on\n"
        "\t\t\t// this before, so a dark light was uploaded, occupied cluster cells\n"
        "\t\t\t// across its whole Range, and was walked and scored by every\n"
        "\t\t\t// fragment -- always losing the reservoir draw, because its\n"
        "\t\t\t// contribution is zero. It cost the scan and nothing else. The\n"
        "\t\t\t// garage carries eight of them, of thirty, for a scene already\n"
        "\t\t\t// walking 26.4 lights a fragment: most of a third of the work.\n"
        "\t\t\t//\n"
        "\t\t\t// Kept in the scene rather than deleted is the right authoring --\n"
        "\t\t\t// the other lighting mode's rig, switched by value rather than by\n"
        "\t\t\t// adding and removing entities -- so the filter belongs here, at\n"
        "\t\t\t// the point the renderer is told what exists.\n"
        "\t\t\t//\n"
        "\t\t\t// **The one thing this could break, and does not.** CollectLights\n"
        "\t\t\t// also feeds LightingHash, and a changed hash invalidates a baked\n"
        "\t\t\t// field -- 1283 frames for this scene. The hash already skips\n"
        "\t\t\t// Realtime lights (Light.h), every zero-intensity light in this\n"
        "\t\t\t// project is Realtime, and every baked light in it is non-zero, so\n"
        "\t\t\t// nothing moves. A *baked* light at zero would change the hash, and\n"
        "\t\t\t// should: a light contributing nothing is not part of the lighting\n"
        "\t\t\t// the hash names. That costs one re-bake, once.\n"
        "\t\t\t//\n"
        "\t\t\t// The colour is not tested. A black colour contributes nothing\n"
        "\t\t\t// either, but zero intensity is the way a light is switched off in\n"
        "\t\t\t// practice, and a test nobody's data exercises is a test nobody\n"
        "\t\t\t// maintains.\n"
        "\t\t\tif (!(data.Intensity > 0.0f))\n"
        "\t\t\t\tcontinue;\n")
    write(C, s)
    print('Scene.cpp: dark lights are not collected')
