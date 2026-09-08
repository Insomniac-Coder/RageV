"""How big is the object-motion correction, in texels, where nothing is moving?

Writes length(ObjectShift(...)) into the accumulator's picture -- the channel
already proved live -- so `reflection-picture` reads it directly. x1600 puts one
texel of correction at 1.0, and the view's own scale of 4 divides by four, so a
level of 64 is one texel. Runs on the parked garage and on the moving scene.
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
NEW = ('\to_Accumulated = vec4(vec3(length(ObjectShift(texel, P, nowNdc)) * 1600.0),'
       ' frames);')

src = io.open(SRC, encoding='utf-8', newline='').read().replace('\r\n', '\n')
if src.count(OLD) != 1:
    sys.exit('anchor matched %d times' % src.count(OLD))
if 'vec2 ObjectShift(' not in src:
    sys.exit('the shift patch is not applied to source')

try:
    io.open(STAGED, 'w', encoding='utf-8', newline='\r\n').write(src.replace(OLD, NEW))
    runs = [('rt15_shift_park', 'showroom.rage', ['--parked', 'rt15_shift_park']),
            ('rt15_shift_move', 'showroom_moving.rage',
             ['rt15_shift_move', '--speed=0', '--frames=1', '--from=80'])]
    for tag, scene, args in runs:
        env = dict(os.environ, BURST_SCENE=scene)
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'scripts', 'garage', 'burst.py')]
                       + args + ['--extra=--debug-view=reflection-picture'],
                       cwd=ROOT, env=env, capture_output=True, text=True, timeout=900)
        img = os.path.join(SHOTS, tag + '.png')
        if not os.path.exists(img):
            print(tag, 'NO FRAME'); continue
        a = np.asarray(Image.open(img).convert('RGB'), dtype=float)[..., 0] / 64.0
        print('-- %s   (units: texels of correction)' % tag)
        print('   whole frame: mean %.3f  median %.3f  95th %.3f  max %.3f'
              % (a.mean(), np.median(a), np.percentile(a, 95), a.max()))
        if tag.endswith('move'):
            c = a[320:380, 600:660]
            w = a[320:380, 700:760]
            print('   moving panel: mean %.3f   static wall: mean %.3f' % (c.mean(), w.mean()))
finally:
    shutil.copyfile(SRC, STAGED)
    print('restored and identical:',
          io.open(SRC, 'rb').read() == io.open(STAGED, 'rb').read())
