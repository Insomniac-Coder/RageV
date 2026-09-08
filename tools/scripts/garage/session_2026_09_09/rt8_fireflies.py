# -*- coding: utf-8 -*-
"""The sea's red fireflies: how many, how bright, and does the garage mind.

The owner reported speckles in the water, red because the bridge's lamps are.
They are not new -- the stack as it stood measured 0.142% of sea pixels
spiking more than 25 levels above their neighbours against 0.142% with every
one of today's switches off -- but they are real, and they are the classic
reservoir firefly: a lamp drawn with a small probability and paid an enormous
weight.

A firefly is counted here as a pixel whose luminance stands more than N levels
above the mean of its 3x3 neighbourhood, inside the sea. Three thresholds,
because "a few faint ones" and "a handful of blazing ones" are different
complaints and one number cannot tell them apart.

The garage is the no-regression arm: the bound this tightens is every
diffuse-kind signal's, so the direct light and the traced bounce feel it too.
"""
import os, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')

SCENES = {
    'pier':    ('scenes/GoldenGateDemo.rage', '70,4.5,705,0.01,-46.98,-2.86'),
    'glitter': ('scenes/GoldenGateDemo.rage', '500,2.5,180,0.01,-90,-1.146'),
    'garage':  ('scenes/showroom.rage', '-2.3,0.72,-2,11,0,4'),
}


def shot(tag, scene, camera, extra=(), frame=60):
    out = os.path.join(SHOTS, tag + '.png')
    if os.path.exists(out):
        os.remove(out)
    p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                        '--project=' + os.path.join(ROOT, 'SampleProject'),
                        '--scene=' + scene, '--rhi=vulkan',
                        '--render-defaults=off', '--vsync=off',
                        '--width=1600', '--height=900',
                        '--screenshot=' + out, '--screenshot-frame=%d' % frame,
                        '--screenshot-count=1', '--frame-time=0.0166',
                        '--camera=' + camera] + list(extra),
                       cwd=RT, capture_output=True, text=True, timeout=1800,
                       errors='replace')
    if not os.path.exists(out):
        print('  ', tag, 'NO FRAME')
        for line in ((p.stdout or '') + (p.stderr or '')).splitlines():
            if 'did not compile' in line or 'ERROR' in line:
                print('   !!', line.strip()[:200])
        return None
    return out


def arr(p):
    return np.asarray(Image.open(p).convert('RGB'), dtype=float)


def fireflies(path, mask):
    a = arr(path)
    lum = a @ np.array([0.2126, 0.7152, 0.0722])
    pad = np.pad(lum, 1, mode='edge')
    box = sum(pad[dy:dy + lum.shape[0], dx:dx + lum.shape[1]]
              for dy in range(3) for dx in range(3)) / 9.0
    spike = (lum - box)[mask]
    return (100 * (spike > 25).mean(), 100 * (spike > 60).mean(),
            100 * (spike > 120).mean(), spike.max(), lum[mask].mean(),
            lum[mask].std())


masks = {}
for name, (scene, cam) in SCENES.items():
    if name == 'garage':
        masks[name] = None
        continue
    p = shot('ff_mask_%s' % name, scene, cam, extra=['--debug-view=water-mask'])
    if not p:
        sys.exit('no mask for ' + name)
    masks[name] = arr(p).max(axis=2) > 8.0

print()
print('%-8s %-7s %-8s %-8s %-9s %-8s %-8s %-8s'
      % ('scene', 'bound', '>25 lvl', '>60 lvl', '>120 lvl', 'worst', 'mean', 'sd'))
for name, (scene, cam) in SCENES.items():
    m = masks[name]
    for arm, extra in (('loose', ['--water-contract=off', '--water-direct=off']),
                       ('tight', [])):
        p = shot('ff_%s_%s' % (name, arm), scene, cam, extra=extra)
        if not p:
            continue
        mask = m if m is not None else np.ones(arr(p).shape[:2], dtype=bool)
        a, b, c, worst, mean, sd = fireflies(p, mask)
        print('%-8s %-7s %-8.3f %-8.3f %-9.4f %-8.0f %-8.2f %-8.2f'
              % (name, arm, a, b, c, worst, mean, sd))
