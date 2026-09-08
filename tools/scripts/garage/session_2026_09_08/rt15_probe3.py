"""Probe, through a channel proved live first.

Run 1 writes a constant into the accumulator's picture: if `reflection-picture`
does not turn red, nothing below it means anything. Run 2 writes the velocity
lane's magnitude into the same channel. Staged into the runtime copy only and
restored, verified byte for byte, whatever happens.
"""
import io, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders', 'reflection_accumulate.rvshader')
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
STAGED = os.path.join(RT, 'assets', 'shaders', 'reflection_accumulate.rvshader')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')

OLD = '\to_Accumulated = vec4(kept, frames);'
RUNS = [
    ('rt15_p_const', '\to_Accumulated = vec4(4.0, 0.0, 0.0, frames);'),
    # The lane is in texture-coordinate units; a texel is 1/1600 across, so
    # x1600 puts one texel of travel at 1.0 and the scale of 4 keeps it on
    # screen. Grey nothing, bright a texel or more.
    ('rt15_p_vel_dolly', '\to_Accumulated = vec4(vec3(length(texelFetch(u_Velocity, texel, 0).xy)'
                   ' * 1600.0), frames);'),
]

src = io.open(SRC, encoding='utf-8', newline='').read().replace('\r\n', '\n')
if src.count(OLD) != 1:
    sys.exit('anchor matched %d times' % src.count(OLD))

try:
    for tag, new in RUNS:
        io.open(STAGED, 'w', encoding='utf-8', newline='\r\n').write(src.replace(OLD, new))
        env = dict(os.environ, BURST_SCENE='showroom_moving.rage')
        p = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'scripts', 'garage', 'burst.py'),
                            tag, '--speed=0.6', '--frames=1', '--from=80',
                            '--extra=--debug-view=reflection-picture'],
                           cwd=ROOT, env=env, capture_output=True, text=True, timeout=900)
        img = os.path.join(SHOTS, tag + '.png')
        if not os.path.exists(img):
            print(tag, 'NO FRAME'); print(p.stdout[-800:]); continue
        a = np.asarray(Image.open(img).convert('RGB'), dtype=float)
        print('--', tag)
        for name, (x0, y0, x1, y1) in (('cube', (600, 320, 660, 380)),
                                       ('wall beside it', (700, 320, 760, 380)),
                                       ('wet floor', (700, 700, 760, 760))):
            c = a[y0:y1, x0:x1]
            print('   %-15s R %6.1f G %6.1f B %6.1f' %
                  (name, c[..., 0].mean(), c[..., 1].mean(), c[..., 2].mean()))
finally:
    shutil.copyfile(SRC, STAGED)
    print('restored and identical:',
          io.open(SRC, 'rb').read() == io.open(STAGED, 'rb').read())
