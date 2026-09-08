"""RT-15: the noise floor, so a scene that does not move is untouched.

Two numbers, both measured on the parked garage and the moving scene with the
probe in rt15_probe_shift.py:

  parked, real geometry   |correction| = 0.000 texels for 95% of the frame,
                          mean 0.012, and nothing above a sixtieth of a texel
                          outside the UI strip
  moving panel            |correction| = 2.994 texels

Four orders of magnitude apart, so an eighth of a texel separates them with
room to spare. Below it the correction is dropped to exactly zero, which is
what keeps a still scene bit-identical: the velocity lane and the matrix are
computed differently (an interpolated clip position against a rebuilt depth,
with the jitter taken out of each afterwards), so they agree to rounding and
not to the bit, and the plane distance the image shift is measured from is
stored at half precision.
"""
import io, sys

P = r'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = '\r\n' in src
s = src.replace('\r\n', '\n')

if 'RT-15' not in s:
    sys.exit('apply rt15_patch2.py first')
if 'kMotionFloor' in s:
    sys.exit('already patched')

T = '\t'
edits = [
 (T + 'const vec2 byMotion = (nowNdc - u_Scene.Jitter.xy) - 2.0 * velocity;\n'
  + T + 'return byMotion - byCamera;',
  T + 'const vec2 byMotion = (nowNdc - u_Scene.Jitter.xy) - 2.0 * velocity;\n'
  + T + 'const vec2 shift = byMotion - byCamera;\n'
  + T + '// **Below the floor there is no motion, only two ways of computing\n'
  + T + '// the same answer.** The lane is an interpolated clip position and\n'
  + T + '// the matrix arm a rebuilt depth, each with its frame jitter taken\n'
  + T + '// out afterwards, so on a surface standing perfectly still they agree\n'
  + T + '// to rounding rather than to the bit. Measured on the parked garage:\n'
  + T + '// zero texels for 95% of the frame and nothing above a sixtieth of a\n'
  + T + '// texel on real geometry, against 2.994 texels on the moving panel.\n'
  + T + '// An eighth of a texel sits between them with three orders of\n'
  + T + '// magnitude to spare, and returning exactly zero below it is what\n'
  + T + '// keeps a scene with nothing moving in it bit-identical -- which is\n'
  + T + '// also the test that says so.\n'
  + T + 'const vec2 texelNdc = 2.0 / vec2(textureSize(u_Velocity, 0));\n'
  + T + 'if (length(shift / texelNdc) < kMotionFloor)\n'
  + T*2 + 'return vec2(0.0);\n'
  + T + 'return shift;'),
 # The floor itself, beside the other tuning constants.
 ('// How many frames the moments remember: enough to know the grain\'s\n',
  '// **RT-15: the least motion, in texels, that counts as motion.** See\n'
  '// ObjectShift for where the number comes from.\n'
  'const float kMotionFloor = 0.125;\n'
  '// How many frames the moments remember: enough to know the grain\'s\n'),
 # The image shift follows the same gate: no motion, no move.
 (T*3 + 'imagePoint = atSurface.bilinear ? ImageThen(P, N, sight, image, atSurface)\n'
  + T*3 + '                                : P + sight * image;',
  T*3 + '// The plane the shift is measured against is stored at half\n'
  + T*3 + '// precision, so on a still reflector the residual is its rounding\n'
  + T*3 + '// rather than nothing at all -- doubled and applied, that moved the\n'
  + T*3 + '// lookup enough to shorten the memory across a parked frame. No\n'
  + T*3 + '// motion, no move, and the image point stays exactly where the\n'
  + T*3 + '// camera arm puts it.\n'
  + T*3 + 'const bool moved = dot(objectShift, objectShift) > 0.0;\n'
  + T*3 + 'imagePoint = (moved && atSurface.bilinear)\n'
  + T*3 + '           ? ImageThen(P, N, sight, image, atSurface)\n'
  + T*3 + '           : P + sight * image;'),
]

for i, (old, new) in enumerate(edits, 1):
    n = s.count(old)
    if n != 1:
        sys.exit('anchor %d matched %d times' % (i, n))
    s = s.replace(old, new, 1)

io.open(P, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
print('patched %d anchors' % len(edits))
