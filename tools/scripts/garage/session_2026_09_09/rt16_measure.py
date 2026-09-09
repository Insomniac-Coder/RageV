# -*- coding: utf-8 -*-
"""RT-16: how many frames does a reflection outlive its light?

The owner reported it on 2026-09-07: switch the car's lights off and their
reflection lingers on the wet floor for a few seconds. Two temporal filters run
in series on that pixel since RT-6.1 put the composite above the resolve -- the
reflection accumulator's own memory, and then TAA's -- and memories in series
compound rather than add.

Half of this item is a measurement and this is it: turn the tubes off mid-burst
and count. Four arms, so the answer names which filter is holding the light
rather than blaming "temporal".

  both      as shipped
  no-taa    TAA's still feedback off (--taa-still-feedback=0)
  no-accum  the accumulator's history off (--reflection-history=off)
  neither   both off -- the floor of what the ray rate alone can do

**The region is found, not drawn.** The floor pixels that were bright before
the switch and dark long after are exactly the reflection that has to go, so
the mask comes from the sequence itself and cannot be a rectangle drawn where
the answer looked best.
"""
import os, re, shutil, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')
BURST = os.path.join(ROOT, 'tools', 'scripts', 'garage', 'burst.py')

# The tubes and their lenses, off at 1.328 s -- frame 80 of a 0.0166 s step.
SWITCH = 'Tube ,Bottom light bars|1.328'
FIRST, COUNT = 60, 150          # frames 60..209: twenty before the switch
ARMS = {
    'both':     [],
    'no-taa':   ['--taa-still-feedback=0'],
    'no-accum': ['--reflection-history=off'],
    'neither':  ['--taa-still-feedback=0', '--reflection-history=off'],
}


def burst(tag, extra):
    env = dict(os.environ, BURST_SWITCH=SWITCH)
    args = [sys.executable, BURST, tag, '--speed=0', '--stop=0.1',
            '--frames=%d' % COUNT, '--from=%d' % FIRST]
    for e in extra:
        args.append('--extra=' + e)
    p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True,
                       timeout=3600, errors='replace', env=env)
    made = sorted(f for f in os.listdir(SHOTS)
                  if f.startswith(tag + '_') and f.endswith('.png'))
    if len(made) < COUNT:
        print(' ', tag, 'only %d frames' % len(made))
        print(p.stdout[-800:])
    return made


def lum(path):
    a = np.asarray(Image.open(os.path.join(SHOTS, path)).convert('RGB'), dtype=float)
    return a @ np.array([0.2126, 0.7152, 0.0722])


print('capturing four arms, %d frames each -- the tubes go out at frame 80' % COUNT)
series = {}
for name, extra in ARMS.items():
    tag = 'rt16_' + name
    for f in os.listdir(SHOTS):
        if f.startswith(tag + '_'):
            os.remove(os.path.join(SHOTS, f))
    frames = burst(tag, extra)
    if not frames:
        sys.exit('no frames for ' + name)
    series[name] = frames
    print('  %-9s %d frames' % (name, len(frames)))

# **The mask, taken from the shipped arm.** Bright before the switch, dark at
# the end of the run: that is the reflection whose life is being counted.
base = series['both']
before = lum(base[15])                     # frame 75, five before the switch
after = lum(base[-1])                      # frame 209, long after
mask = (before > np.percentile(before, 90)) & (after < before * 0.6)
print('\nthe region: %.2f%% of the frame, %d pixels' % (100 * mask.mean(), mask.sum()))

print('\n%-9s %-8s %-8s %-8s %-9s' % ('arm', 'before', 'settled', 'frames', 'seconds'))
for name, frames in series.items():
    v = np.array([lum(f)[mask].mean() for f in frames])
    start, end = v[:15].mean(), v[-10:].mean()
    span = start - end
    if span <= 0.5:
        print('%-9s %-8.2f %-8.2f  no decay to measure' % (name, start, end))
        continue
    # Frames after the switch until it is within 5% of where it ends up.
    reached = np.where(v[20:] <= end + 0.05 * span)[0]
    n = int(reached[0]) if len(reached) else -1
    print('%-9s %-8.2f %-8.2f %-8s %-9s'
          % (name, start, end, n if n >= 0 else 'never',
             '%.2f' % (n * 0.0166) if n >= 0 else '--'))
