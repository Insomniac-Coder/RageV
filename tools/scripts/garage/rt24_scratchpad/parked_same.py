"""Parked garage, as it ships and with a staged arm: are the frames bit-identical?
usage: parked_same.py <stage>"""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import glob, io, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

sys.path.insert(0, (_RAGEV + '/tools/scripts/garage'))
import spin_measure as sm

sr, burst = sm.sr, sm.burst
FIRST, COUNT = 150, 20


def capture(tag, stage_name):
    shots = os.path.join(sm.OUT, tag + '_frames')
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots)
    io.open(os.path.join(sr.SCENES, sr.HEAD_SCENE), 'wb').write(sm.orbit_scene(0.0, 1.0))
    burst.sync_bake('showroom_head')
    try:
        if not sr.restore():
            sys.exit('the staged shader copies did not restore')
        sm.stage(stage_name)
        subprocess.run([os.path.join(sm.RT, 'RageVRuntime.exe'), '--project=' + sr.PROJECT,
                        '--scene=scenes/' + sr.HEAD_SCENE, '--rhi=vulkan', '--render-defaults=off',
                        '--width=1600', '--height=900', '--import-cache=off', '--vsync=off',
                        '--frame-time=%.9f' % (1.0 / sm.FPS),
                        '--screenshot-frame=%d' % FIRST, '--screenshot-count=%d' % COUNT,
                        '--screenshot=' + os.path.join(shots, 'f.png'), '--benchmark=%d' % (FIRST + COUNT + 2)],
                       cwd=sm.RT, capture_output=True, text=True, errors='replace', timeout=1800)
    finally:
        sr.restore()
        for f in (sr.HEAD_SCENE, sr.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(sr.SCENES, f))
            except OSError:
                pass
    return sorted(glob.glob(os.path.join(shots, '*.png')))


a = capture('parked_ship', None)
b = capture('parked_arm', sys.argv[1])
print('frames', len(a), len(b))
for fa, fb in zip(a, b):
    x = np.asarray(Image.open(fa)).astype(int)
    y = np.asarray(Image.open(fb)).astype(int)
    d = np.abs(x - y)
    print('%-12s max %3d  pixels differing %d' % (os.path.basename(fa), d.max(), int((d.max(axis=2) > 0).sum())))
def change(frames):
    ys = [np.asarray(Image.open(f).convert('RGB')).astype(float) @ [0.2126, 0.7152, 0.0722] for f in frames]
    regions = {'floor': (slice(480, 860), slice(0, 1600)), 'pole': (slice(560, 760), slice(1400, 1560)),
               'car': (slice(560, 700), slice(660, 940)), 'tubes': (slice(660, 850), slice(420, 1040))}
    return ' '.join('%s %.3f' % (k, np.mean([np.abs(ys[i + 1][r] - ys[i][r]).mean() for i in range(len(ys) - 1)]))
                    for k, r in regions.items())
x = np.asarray(Image.open(a[-1]).convert('RGB')).astype(float)
y = np.asarray(Image.open(b[-1]).convert('RGB')).astype(float)
Image.fromarray(np.clip(np.abs(x - y).max(axis=2) * 25, 0, 255).astype(np.uint8)).save(os.path.join(sm.OUT, 'parked_diff.png'))
Image.fromarray(x.astype(np.uint8)).save(os.path.join(sm.OUT, 'parked_ship.png'))
Image.fromarray(y.astype(np.uint8)).save(os.path.join(sm.OUT, 'parked_arm.png'))
print('parked change per frame (lower is steadier):')
print('  as it ships  ' + change(a))
print('  with the arm ' + change(b))
for t in ('parked_ship', 'parked_arm'):
    shutil.rmtree(os.path.join(sm.OUT, t + '_frames'), ignore_errors=True)
