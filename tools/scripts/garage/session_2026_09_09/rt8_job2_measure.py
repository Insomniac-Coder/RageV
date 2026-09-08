# -*- coding: utf-8 -*-
"""RT-8 job 2: the sea's traced mirror, reconstructed or not.

Three bridge cameras. The sea's band is taken from the water-mask debug view
rather than a hand-drawn rectangle, so the metric is measured where the water
actually is and the pier and towers cannot dilute it.

Two numbers per camera:
  speckle -- mean |pixel - its 3x3 mean| inside the mask, the fizz the
             flicker protocol counts, lower is smoother
  detail  -- the standard deviation inside the mask, so a "smoother" result
             that is really a haze is caught: contrast must not fall

Interleaved A/B/B/A: this laptop's GPU drifts, and a single pair is not a
result. The pictures are deterministic, so what interleaving guards here is a
mistake in the harness, not thermal noise -- an arm that renders differently
the second time has not been controlled.
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
    'deck':    '0,76.4,950,0.01,0,0',
}


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
            if 'did not compile' in line or 'ERROR' in line:
                print('   !!', line.strip()[:220])
        return None
    return out


def arr(p):
    return np.asarray(Image.open(p).convert('RGB'), dtype=float)


def box3(a):
    pad = np.pad(a, ((1, 1), (1, 1), (0, 0)), mode='edge')
    out = np.zeros_like(a)
    for dy in (0, 1, 2):
        for dx in (0, 1, 2):
            out += pad[dy:dy + a.shape[0], dx:dx + a.shape[1], :]
    return out / 9.0


def measure(path, mask):
    a = arr(path)
    lum = a @ np.array([0.2126, 0.7152, 0.0722])
    lum = lum[:, :, None]
    speck = np.abs(lum - box3(lum))[:, :, 0]
    if mask.sum() < 100:
        return float('nan'), float('nan')
    return float(speck[mask].mean()), float(lum[:, :, 0][mask].std())


# --- the sea's band, from the mask view ---------------------------------
masks = {}
for name, cam in CAMERAS.items():
    p = run('j2_mask_%s' % name, cam, extra=['--debug-view=water-mask'])
    if not p:
        sys.exit('no mask for ' + name)
    m = arr(p).max(axis=2) > 8.0
    masks[name] = m
    print('%-8s water covers %.1f%% of the frame' % (name, 100.0 * m.mean()))

# --- A/B/B/A ------------------------------------------------------------
ARMS = {'own': ['--water-ray-contract=off'], 'contract': ['--water-ray-contract=on']}
order = ['own', 'contract', 'contract', 'own']
shots = {}
for name, cam in CAMERAS.items():
    for i, arm in enumerate(order):
        shots.setdefault((name, arm), []).append(
            run('j2_%s_%s_%d' % (name, arm, i), cam, extra=ARMS[arm]))

print()
print('%-8s %-9s %-9s %-9s %-9s' % ('camera', 'speckle', '', 'detail(sd)', ''))
print('%-8s %-9s %-9s %-9s %-9s' % ('', 'own', 'contract', 'own', 'contract'))
for name in CAMERAS:
    vals = {}
    for arm in ('own', 'contract'):
        ps = [p for p in shots[(name, arm)] if p]
        if len(ps) != 2:
            vals[arm] = (float('nan'), float('nan')); continue
        a = measure(ps[0], masks[name])
        b = measure(ps[1], masks[name])
        if abs(a[0] - b[0]) > 1e-9:
            print('  !! %s/%s not repeatable: %.4f vs %.4f' % (name, arm, a[0], b[0]))
        vals[arm] = a
    print('%-8s %-9.4f %-9.4f %-9.2f %-9.2f'
          % (name, vals['own'][0], vals['contract'][0],
             vals['own'][1], vals['contract'][1]))

print()
print('per-pixel difference inside the sea, own vs contract')
for name in CAMERAS:
    a, b = shots[(name, 'own')][0], shots[(name, 'contract')][0]
    if not (a and b):
        continue
    d = np.abs(arr(a) - arr(b)).max(axis=2)
    m = masks[name]
    print('  %-8s max %3.0f levels, %5.2f%% of sea pixels differ'
          % (name, d[m].max() if m.any() else 0.0,
             100.0 * (d[m] > 0).mean() if m.any() else 0.0))
