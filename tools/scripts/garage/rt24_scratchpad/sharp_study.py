"""Parked garage, settled frame, one arm each: where did the tube reflections' sharpness go?
usage: sharp_study.py tag:stage:flags ...   (stage and flags may be empty; flags space-joined)"""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import glob, io, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

sys.path.insert(0, (_RAGEV + '/tools/scripts/garage'))
import spin_measure as sm

sr, burst = sm.sr, sm.burst
FRAME = 240


def capture(tag, stage_name, flags):
    shots = os.path.join(sm.OUT, tag + '_frames')
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots)
    io.open(os.path.join(sr.SCENES, sr.HEAD_SCENE), 'wb').write(sm.orbit_scene(0.0, 1.0))
    burst.sync_bake('showroom_head')
    try:
        if not sr.restore():
            sys.exit('the staged shader copies did not restore')
        sm.stage(stage_name or None)
        p = subprocess.run([os.path.join(sm.RT, 'RageVRuntime.exe'), '--project=' + sr.PROJECT,
                            '--scene=scenes/' + sr.HEAD_SCENE, '--rhi=vulkan', '--render-defaults=off',
                            '--width=1600', '--height=900', '--import-cache=off', '--vsync=off',
                            '--frame-time=%.9f' % (1.0 / sm.FPS),
                            '--screenshot-frame=%d' % FRAME, '--screenshot-count=1',
                            '--screenshot=' + os.path.join(shots, 'f.png'), '--benchmark=%d' % (FRAME + 2)] + flags,
                           cwd=sm.RT, capture_output=True, text=True, errors='replace', timeout=1800)
        bad = [l for l in (p.stdout + p.stderr).splitlines() if 'compile' in l.lower() and 'irradiance fill' not in l]
        if bad:
            print(tag, 'COMPILE:', bad[:2])
    finally:
        sr.restore()
        for f in (sr.HEAD_SCENE, sr.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(sr.SCENES, f))
            except OSError:
                pass
    got = sorted(glob.glob(os.path.join(shots, '*.png')))
    out = os.path.join(sm.OUT, 'sharp_%s.png' % tag)
    shutil.copy(got[-1], out)
    shutil.rmtree(shots, ignore_errors=True)
    return out


def detail(path):
    a = np.asarray(Image.open(path).convert('L')).astype(float)[560:860]
    return np.abs(np.diff(a, axis=1)).mean() + np.abs(np.diff(a, axis=0)).mean(), a.mean()


for spec in sys.argv[1:]:
    tag, stage_name, flags = (spec.split(':') + ['', ''])[:3]
    path = capture(tag, stage_name, flags.split() if flags else [])
    d, m = detail(path)
    print('%-12s floor detail %5.2f  floor mean %6.2f   (%s %s)' % (tag, d, m, stage_name or 'no arm', flags))
