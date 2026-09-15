# -*- coding: utf-8 -*-
"""Option B, first candidates for the moving reflections' grain (2026-09-15).

  blur6 / blur12 -- the reflections' young-history blur back on (--reflection-blur), which
                    widens only where a texel's history is young
  longmem        -- a staged accumulator whose memory never falls under 16 frames where a
                    history was found (the motion rules' cap lifted, for the test only)
against the shipped arms of refl_memory_check.py (mem_*_ship), on the cube crossing, the
car driving and the parked garage (the floor, for what each costs a still picture).

Numbers on the object: grain (pixels more than 16 levels from their 3x3 median) and
frame-to-frame change; on the parked floor, detail (the same median residual, a blur's
loss) and frame-to-frame change.

Usage: refl_moving_fix_test.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_14'))
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
import stage_run  # noqa: E402
import rt13_stage2 as s2  # noqa: E402

ACC = 'reflection_accumulate.rvshader'
stage_run.VARIANTS['longmem'] = [(ACC, '\treturn max(memory, fewest);\n', '\treturn max(memory, max(fewest, 16.0));\n')]
ARMS = [('blur6', 'ship', ['--reflection-blur=6']), ('blur12', 'ship', ['--reflection-blur=12']),
        ('longmem', 'longmem', [])]
CASES = {
    'parked': ('owner', dict(frames=40, first=150), (430, 860, 0, 1600)),     # the floor
    'cube': ('owner', dict(frames=40, first=100, cube=(-9.0, 3.0, 6.0)), (300, 420, 600, 840)),
    'car': ('close', dict(frames=20, first=55, BURST_SLIDE=s2.CAR), (380, 760, 250, 1250)),
}

if 'render' in sys.argv:
    for case, (cam, opts, _) in CASES.items():
        s2.arms_at(cam, [('mem_%s_%s' % (case, tag), variant, flags, dict(opts)) for tag, variant, flags in ARMS])

S = os.path.join(stage_run.SHOTS, 'rt5b_mem_%s_%s_%d.png')
for case, (cam, opts, (y0, y1, x0, x1)) in CASES.items():
    print(case)
    for tag in ['ship'] + [a[0] for a in ARMS]:
        grain, change, prev = [], [], None
        for k in range(opts['first'], opts['first'] + opts['frames']):
            im = Image.open(S % (case, tag, k)).convert('L')
            a = np.asarray(im, dtype=float)
            med = np.asarray(im.filter(ImageFilter.MedianFilter(3)), dtype=float)
            g = np.abs(a - med)[y0:y1, x0:x1]
            grain.append(((g > 16).mean() * 100, g.mean()))
            if prev is not None:
                change.append(np.abs(a - prev)[y0:y1, x0:x1].mean())
            prev = a
        grain = np.array(grain)
        print('   %-8s grain %.2f%% px (median residual %.2f)   frame-to-frame %.2f'
              % (tag, grain[:, 0].mean(), grain[:, 1].mean(), np.mean(change)))
    k = opts['first'] + opts['frames'] // 2
    tiles = [np.asarray(Image.open(S % (case, tag, k)).convert('RGB'))[y0:y1, x0:x1] for tag in ['ship'] + [a[0] for a in ARMS]]
    os.makedirs(os.path.join(stage_run.ROOT, 'build', 'rt18'), exist_ok=True)
    s2._sheet(os.path.join(stage_run.ROOT, 'build', 'rt18', 'fix_%s_%d.png' % (case, k)), tiles,
              ['as shipped'] + [a[0] for a in ARMS])
