"""The bridge, parked, as it ships and with a staged arm: glint flicker (full frame and the
cable band) and whether the frames differ at all. usage: bridge_check.py <stage>"""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import glob, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

sys.path.insert(0, (_RAGEV + '/tools/scripts/garage'))
import spin_measure as sm

sr = sm.sr
CHECK = (_RAGEV + '/tools/scripts/check_glint_flicker.py')


def capture(tag, stage_name):
    shots = os.path.join(sm.OUT, tag + '_frames')
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots)
    try:
        if not sr.restore():
            sys.exit('the staged shader copies did not restore')
        sm.stage(stage_name)
        subprocess.run([os.path.join(sm.RT, 'RageVRuntime.exe'), '--project=' + sr.PROJECT,
                        '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan', '--render-defaults=off',
                        '--width=1600', '--height=900', '--frame-time=0.000001', '--vsync=off',
                        '--screenshot-frame=240', '--screenshot-count=16',
                        '--screenshot=' + os.path.join(shots, 'f.png'), '--benchmark=258'],
                       cwd=sm.RT, capture_output=True, text=True, errors='replace', timeout=1800)
    finally:
        sr.restore()
    return sorted(glob.glob(os.path.join(shots, '*.png')))


a = capture('bridge_ship', None)
b = capture('bridge_arm', sys.argv[1])
for tag, files in (('as it ships', a), ('with the arm', b)):
    for region in (None, '0,250,1600,420'):
        out = subprocess.run([sys.executable, CHECK] + (['--region', region] if region else []) + files,
                             capture_output=True, text=True).stdout
        print('%s %s: %s' % (tag, 'cable band' if region else 'full frame',
                             ' | '.join(l.strip() for l in out.splitlines()[1:])))
d = [np.abs(np.asarray(Image.open(x)).astype(int) - np.asarray(Image.open(y)).astype(int)).max() for x, y in zip(a, b)]
print('frames differ: max %d levels' % max(d))
for t in ('bridge_ship', 'bridge_arm'):
    shutil.rmtree(os.path.join(sm.OUT, t + '_frames'), ignore_errors=True)
