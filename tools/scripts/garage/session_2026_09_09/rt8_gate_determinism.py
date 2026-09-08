# -*- coding: utf-8 -*-
"""Is the bridge repeatable at these cameras at all?

The null test said glitter and pier changed and deck did not. Before that is
read as a defect, the same build is run twice with nothing altered: an A/A. A
difference here means the scene is not repeatable at that camera and no null
test taken there means anything.
"""
import os, subprocess
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')

CAMERAS = {
    'glitter': '500,2.5,180,0.01,-90,-1.146',
    'pier':    '70,4.5,705,0.01,-46.98,-2.86',
    'deck':    '0,76.4,950,0.01,0,0',
}


def shot(tag, camera, frame=60):
    out = os.path.join(SHOTS, tag + '.png')
    if os.path.exists(out):
        os.remove(out)
    subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                    '--project=' + os.path.join(ROOT, 'SampleProject'),
                    '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan',
                    '--render-defaults=off', '--vsync=off',
                    '--width=1600', '--height=900',
                    '--screenshot=' + out, '--screenshot-frame=%d' % frame,
                    '--screenshot-count=1', '--frame-time=0.0166',
                    '--camera=' + camera],
                   cwd=RT, capture_output=True, text=True, timeout=1800,
                   errors='replace')
    return out if os.path.exists(out) else None


for name, cam in CAMERAS.items():
    a = shot('aa_%s_1' % name, cam)
    b = shot('aa_%s_2' % name, cam)
    if not (a and b):
        print('%-8s missing a frame' % name); continue
    x = np.asarray(Image.open(a).convert('RGB'), dtype=float)
    y = np.asarray(Image.open(b).convert('RGB'), dtype=float)
    d = np.abs(x - y)
    print('%-8s A/A: max %.0f levels, %.4f%% of pixels differ  %s'
          % (name, d.max(), 100.0 * (d.max(axis=2) > 0).mean(),
             'repeatable' if d.max() == 0 else '*** NOT REPEATABLE ***'))
