"""Probe: does the scene's velocity lane carry the cube's motion at all?

Stages a variant of the accumulator -- into the runtime's copy only -- whose
motion attachment holds the raw velocity lane instead of the image motion, so
`--debug-view=reflection-motion` draws the lane itself. Grey is zero. The copy
is restored from source and compared before this exits.
"""
import io, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders', 'reflection_accumulate.rvshader')
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
STAGED = os.path.join(RT, 'assets', 'shaders', 'reflection_accumulate.rvshader')

OLD = ('\to_Motion = vec4(g_HaveMotion ? g_ImageMotion : vec2(0.0),\n'
       '\t\t\t\t\tOctEncode(reflect(sight, N)));')
NEW = '\to_Motion = vec4(texelFetch(u_Velocity, texel, 0).xy * 10.0, 0.0, 0.0);'

s = io.open(SRC, encoding='utf-8', newline='').read()
body = s.replace('\r\n', '\n')
if body.count(OLD) != 1:
    sys.exit('probe anchor matched %d times' % body.count(OLD))
io.open(STAGED, 'w', encoding='utf-8', newline='\r\n').write(body.replace(OLD, NEW))
print('probe staged')

try:
    env = dict(os.environ, BURST_SCENE='showroom_moving.rage')
    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'scripts', 'garage', 'burst.py'),
                    'rt15_vel', '--speed=0', '--frames=1', '--from=80',
                    '--extra=--debug-view=reflection-motion'],
                   cwd=ROOT, env=env, capture_output=True, text=True, timeout=900)
finally:
    shutil.copyfile(SRC, STAGED)
    same = io.open(SRC, 'rb').read() == io.open(STAGED, 'rb').read()
    print('staged copy restored and identical:', same)

p = os.path.join(ROOT, 'build', 'garage_burst', 'rt15_vel.png')
if not os.path.exists(p):
    sys.exit('no frame rendered')
a = np.asarray(Image.open(p).convert('RGB'), dtype=float)
for name, (x0, y0, x1, y1) in (('cube', (600, 320, 660, 380)),
                               ('wall right of it', (700, 320, 760, 380)),
                               ('floor', (700, 700, 760, 760))):
    c = a[y0:y1, x0:x1]
    print('%-18s R %6.2f  G %6.2f   (128 = no motion)  spread %.2f'
          % (name, c[..., 0].mean(), c[..., 1].mean(), c[..., 0].std()))
