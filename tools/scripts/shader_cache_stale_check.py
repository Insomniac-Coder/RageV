"""RT2-9's stale-cache check: an edited shader is compiled again, never served from the cache.

The runtime reads the staged copies beside its executable. With the caches on and warm, the staged tonemap is
edited to halve the picture, one frame is captured, the copy is put back byte for byte, and a frame is captured
again. Pass: the edited frame is about half as bright as the baseline (the edit was compiled, not a stale
entry served), and the restored frame matches the baseline exactly (the old entry is found again, no rebuild
needed to get back).
"""
import os
import shutil
import subprocess

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
STAGED = os.path.join(RT, 'assets', 'shaders', 'tonemap.rvshader')
OUT = os.path.join(ROOT, 'build', 'stale_check')
WAS = '\to_Color = vec4(color, 1.0);'
NOW = '\to_Color = vec4(color * 0.5, 1.0);   // stale-cache check, put back after'


def frame(tag):
    path = os.path.join(OUT, tag + '.png')
    subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                    '--project=' + os.path.join(ROOT, 'SampleProject', 'SampleProject.rvproject'),
                    '--scene=scenes/showroom.rage', '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                    '--width=1600', '--height=900', '--frame-time=0.000001', '--screenshot-frame=20',
                    '--screenshot=' + path, '--benchmark=22', '--shader-cache=on'],
                   cwd=RT, capture_output=True, timeout=1800)
    return np.asarray(Image.open(path).convert('L'), dtype=float)


os.makedirs(OUT, exist_ok=True)
original = open(STAGED, 'rb').read()
assert original.count(WAS.encode()) == 1, 'the anchor is not in the staged tonemap'
try:
    base = frame('base')
    open(STAGED, 'wb').write(original.replace(WAS.encode(), NOW.encode()))
    edited = frame('edited')
finally:
    open(STAGED, 'wb').write(original)
restored = frame('restored')
print('edited / baseline brightness: %.3f (about 0.5 means the edit was compiled)' % (edited.mean() / base.mean()))
print('restored against baseline: %d pixels differ' % int((np.abs(restored - base) > 0).sum()))
shutil.rmtree(OUT, ignore_errors=True)
