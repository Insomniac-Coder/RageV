# -*- coding: utf-8 -*-
"""RT-8 job 3: how far the sea's picture may travel before the memory halves.

The contract shortens a memory in proportion to how far the picture moved,
which the private pass never did -- and on a sea, where every pixel moves every
frame, that costs a little smoothness standing still. This sweeps the dial.

Reported beside the speckle: the standard deviation and the 99.9th percentile
inside the sea. A memory long enough to smooth glitter into a haze shows up as
falling contrast and dimming peaks, which is how the sixteen-frame glint was
argued for in the first place.
"""
import os, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')

CAMERAS = {
    'glitter': '500,2.5,180,0.01,-90,-1.146',
    'pier':    '70,4.5,705,0.01,-46.98,-2.86',
}
SLACKS = [1.0, 2.0, 4.0, 8.0, 16.0]


def run(tag, camera, extra=(), frame=60):
    out = os.path.join(SHOTS, tag + '.png')
    if os.path.exists(out):
        os.remove(out)
    p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                        '--project=' + os.path.join(ROOT, 'SampleProject'),
                        '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan',
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
            if 'did not compile' in line or 'ERROR' in line or 'expects' in line:
                print('   !!', line.strip()[:200])
        return None
    return out


def arr(p):
    return np.asarray(Image.open(p).convert('RGB'), dtype=float)


def box3(a):
    pad = np.pad(a, ((1, 1), (1, 1)), mode='edge')
    out = np.zeros_like(a)
    for dy in (0, 1, 2):
        for dx in (0, 1, 2):
            out += pad[dy:dy + a.shape[0], dx:dx + a.shape[1]]
    return out / 9.0


def stats(path, mask):
    a = arr(path)
    lum = a @ np.array([0.2126, 0.7152, 0.0722])
    speck = np.abs(lum - box3(lum))
    return (float(speck[mask].mean()), float(lum[mask].std()),
            float(np.percentile(lum[mask], 99.9)))


masks = {}
for name, cam in CAMERAS.items():
    p = run('sl_mask_%s' % name, cam, extra=['--debug-view=water-mask'])
    if not p:
        sys.exit('no mask for ' + name)
    masks[name] = arr(p).max(axis=2) > 8.0

print()
print('%-8s %-10s %-9s %-9s %-9s' % ('camera', 'arm', 'speckle', 'sd', 'p99.9'))
for name, cam in CAMERAS.items():
    p = run('sl_%s_own' % name, cam, extra=['--water-contract=off'])
    if p:
        s, sd, pk = stats(p, masks[name])
        print('%-8s %-10s %-9.4f %-9.2f %-9.1f' % (name, 'own', s, sd, pk))
    for k in SLACKS:
        p = run('sl_%s_%g' % (name, k), cam,
                extra=['--water-contract=on', '--water-lamp-slack=%g' % k])
        if not p:
            continue
        s, sd, pk = stats(p, masks[name])
        print('%-8s %-10s %-9.4f %-9.2f %-9.1f' % (name, 'slack %g' % k, s, sd, pk))
