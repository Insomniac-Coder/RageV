# -*- coding: utf-8 -*-
"""RT-12's first real catch: the temporal resolve decodes normals wrongly.

**Found by the instrument, on its first honest look.** The new
`reflection-direction` view showed a hard vertical seam down the middle of a
flat floor, where the direction must vary smoothly. `reflection-normal` over the
same band is uniform to within a level, so the normal was not the cause -- the
decode was.

`include/octahedral.glsl` is explicit that its encoding is **[0,1]²**:

    // Unit vector to [0,1]^2.
    vec2 OctEncode(vec3 n) { ...; return e * 0.5 + 0.5; }
    vec3 OctDecode(vec2 e) { e = e * 2.0 - 1.0; ... }

and its own header says the three users "must agree exactly, because two of them
write what the third reads ... a second copy of either half is how those stop
agreeing". `taa_resolve.rvshader` made exactly that second copy: a local
`DecodeOct` that omits `e * 2 - 1` and so reads a [0,1] encoding as though it
were [-1,1]. The G-buffer writes `OctEncode(N)`, `taa_guide` copies those two
channels through untouched, and the resolve has been decoding them wrongly since
RT-6 landed.

**What it actually broke, stated carefully.** Both sides of the comparison go
through the same wrong function, so `dot(f(a), f(b)) >= 0.9` still tests whether
two normals are *similar* -- which is why RT-6 measured a sensible refusal rate
and a visibly sharper bridge. What it is not is the test the constant describes:
`kNormalTolerance = 0.9` is documented as "about twenty-five degrees" and, under
a decode that maps the encoded square through the wrong fold, it is not. The
distortion is worst near the octahedron's diagonals, which is where a normal
pointing sideways lands -- walls and the sides of pillars.

So this is a correctness fix to a test whose *effective* tolerance was unknown,
not a repair of something visibly broken. The frame change is measured in the
record rather than assumed to be nil.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

CORRECT = (
"{\n"
"\t// **The engine's encoding is [0,1]^2, not [-1,1]^2** -- see\n"
"\t// include/octahedral.glsl, whose OctEncode ends `e * 0.5 + 0.5`. This\n"
"\t// copy omitted the matching `e * 2 - 1` and read the square through the\n"
"\t// wrong fold. Kept local rather than included, for the reason the original\n"
"\t// gives (no lighting header for four lines of arithmetic) -- but now it is\n"
"\t// the same four lines.\n"
"\te = e * 2.0 - 1.0;\n"
"\tvec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));\n"
"\tif (n.z < 0.0)\n"
"\t\tn.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0,\n"
"\t\t\t\t\t\t\t\t\t\tn.y >= 0.0 ? 1.0 : -1.0);\n"
"\treturn normalize(n);\n"
"}\n")

WRONG = (
"{\n"
"\tvec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));\n"
"\tconst float t = max(-n.z, 0.0);\n"
"\tn.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);\n"
"\treturn normalize(n);\n"
"}\n")

for path in ('RageVEditor/assets/shaders/taa_resolve.rvshader',
             'RageVEditor/assets/shaders/debug_view.rvshader'):
    s = read(path)
    if has(s, 'e = e * 2.0 - 1.0;'):
        print('%s already correct' % path)
        continue
    # The signature line differs slightly between the two; anchor on the body.
    s = rep(s, "vec3 DecodeOct(vec2 e)\n" + WRONG,
            "vec3 DecodeOct(vec2 e)\n" + CORRECT)
    write(path, s)
    print('%s: decodes the engine\'s own encoding' % path)
