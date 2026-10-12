"""Writes the pre-footprint (v5) resolve shader to the path given, from the
current source, by reversing patch_footprint.py's edits."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import sys
src = (_RAGEV + '/RageVEditor/assets/shaders/reflection_resolve.rvshader')
s = open(src, 'rb').read().decode('utf-8')
i = s.index('\t// The footprint.'); tail = 'const float hitTolerance = max(0.3 * hitDistance, 0.05);\n'; j = s.index(tail) + len(tail)
s = s[:i] + '\tconst float sigma = float(kMaxRadius) * 0.6 * smoothstep(0.05, 0.45, roughness);\n\tif (sigma < 0.15)\n\t\treturn;\n' + s[j:]
a = '\t\t\tfloat w = exp(-(float(x * x) * invTwoSigmaSq.x + float(y * y) * invTwoSigmaSq.y));\n'; assert s.count(a) == 1
s = s.replace(a, '\t\t\tfloat w = exp(-float(x * x + y * y) / (2.0 * sigma * sigma));\n')
b = '\t\t\tw *= exp(-abs(f.a - hitDistance) / hitTolerance);\n'; assert s.count(b) == 1; s = s.replace(b, '')
open(sys.argv[1], 'wb').write(s.encode('utf-8')); print('wrote', sys.argv[1])
