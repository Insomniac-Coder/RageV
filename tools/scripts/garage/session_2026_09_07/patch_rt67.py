# -*- coding: utf-8 -*-
"""RT-6.7: the sky/geometry transition is a disocclusion.

**The defect.** `Matches()` opens with

    if (now.x >= 1.0 || was.x >= 1.0)
        return true;

-- the history is accepted whenever *either* side has no surface under it. That
is right when both are sky, and wrong at the transition, which is exactly a
disocclusion in both directions: geometry appearing where sky was inherits stale
sky, and sky appearing where geometry was inherits a stale object.

**Where the search comes in, and why nothing else needs changing.** `Matches` is
called once per candidate inside the nine-tap loop, so returning false here
rejects *that neighbour* and lets the search carry on. A pixel that is geometry
now and was sky at the centre texel will look at the eight neighbours and take
one that was the same geometry if there is one; only if none is does the pixel
fall through to `disoccluded` and take the current frame whole. The recovery
path the RT-6 record calls the difference between "a ghost" and "aliasing at
every silhouette" is already there and this change routes through it.

**Two-sided on purpose, and the bridge is the test.** The garage is interior and
has no sky, so it cannot show this at all. On the bridge the sky boundary is
cables, suspenders, lamp standards and tower ribs -- one-to-two pixel members
that RT-3.1 already measured as this engine's worst thin-geometry case. Refusing
there buys a ghost fix at the risk of aliasing on exactly those members, which
is what the measurement has to settle.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/taa_resolve.rvshader'
s = read(S)
if has(s, 'nowSky'):
    print('already done')
    raise SystemExit(0)

s = rep(s,
    "// A pixel with no surface under it now, or none there last frame, is not a\n"
    "// disocclusion to refuse: it is the sky, and the sky reprojects perfectly\n"
    "// well. Left to the box, exactly as it was before this test existed.\n"
    "bool Matches(vec4 now, vec4 was)\n"
    "{\n"
    "\t// Sky on either side: nothing to compare, and nothing that ghosts.\n"
    "\tif (now.x >= 1.0 || was.x >= 1.0)\n"
    "\t\treturn true;\n",
    "// **Sky is a surface like any other for this purpose** (RT-6.7). Sky where\n"
    "// there was sky reprojects perfectly well and is accepted. Sky where there\n"
    "// was geometry, or geometry where there was sky, is a *disocclusion* -- the\n"
    "// one thing this test exists to catch -- and until RT-6.7 it was the one\n"
    "// case that always passed.\n"
    "bool Matches(vec4 now, vec4 was)\n"
    "{\n"
    "\t// **RT-6.7: both, or neither.** The old test was `now || was`, which\n"
    "\t// accepted the transition in both directions: geometry appearing where\n"
    "\t// sky was inherits stale sky, and sky appearing where geometry was\n"
    "\t// inherits a stale object. Those are silhouette trails and halos, and\n"
    "\t// they are what a history test is for.\n"
    "\t//\n"
    "\t// Refusing here is not the same as refusing the pixel: this runs once per\n"
    "\t// candidate inside the nine-tap search, so a geometry pixel whose centre\n"
    "\t// texel was sky simply takes the neighbour that was the same geometry.\n"
    "\t// Only when none of the nine is does the pixel take the current frame\n"
    "\t// whole -- which is the correct answer for something genuinely uncovered.\n"
    "\tconst bool nowSky = now.x >= 1.0;\n"
    "\tconst bool wasSky = was.x >= 1.0;\n"
    "\tif (nowSky && wasSky)\n"
    "\t\treturn true;\n"
    "\tif (nowSky != wasSky)\n"
    "\t\treturn false;\n")

write(S, s)
print('taa_resolve.rvshader: the sky transition is a disocclusion')
