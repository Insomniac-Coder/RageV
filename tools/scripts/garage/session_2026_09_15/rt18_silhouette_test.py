# -*- coding: utf-8 -*-
"""RT-18, the first test: does the silhouette exemption leave the cube's stripes?

The accumulator keeps a silhouette texel's own history with no test at all, whichever
side of the edge it showed last frame, so a parked edge that flips sides under the
jitter averages into its coverage instead of shaking (WR-16 R5). When the edge moves,
that history is the other side's picture and the edge's highlight -- and next frame the
texel is inside the object, where the history now passes every test and stays.

  silmoving -- the exemption kept only where nothing around the edge moves (the 3x3's
               fastest motion under an eighth of a texel, kMotionFloor): a parked edge
               is exactly as shipped, a moving one is tested like any other texel.

Cases: the cube crossing and the car driving (against refl_memory_check's mem_*_ship and
mem_*_noavg), and the parked garage, which must come out identical.

Usage: rt18_silhouette_test.py [render]
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
N = '\n'
stage_run.VARIANTS['silmoving'] = [
    (ACC, 'float g_BlendFrames = 1.0;' + N,
          'float g_BlendFrames = 1.0;' + N + 'bool g_SilhouetteMoving = false;' + N),
    (ACC, '\t\tif (!(silhouette && k == 0))' + N,
          '\t\tif (!(silhouette && k == 0 && !g_SilhouetteMoving))' + N),
    (ACC, '\t\tconst bool silhouette = AtSilhouette(texel, size, P, N, eyeDistance, neighbourMotion);' + N,
          '\t\tconst bool silhouette = AtSilhouette(texel, size, P, N, eyeDistance, neighbourMotion);' + N
          + '\t\tg_SilhouetteMoving = neighbourMotion >= kMotionFloor;' + N),
]
CASES = {
    'parked': ('owner', dict(frames=40, first=150), (430, 860, 0, 1600)),
    'cube': ('owner', dict(frames=40, first=100, cube=(-9.0, 3.0, 6.0)), (300, 420, 600, 840)),
    'car': ('close', dict(frames=20, first=55, BURST_SLIDE=s2.CAR), (380, 760, 250, 1250)),
}

if 'render' in sys.argv:
    for case, (cam, opts, _) in CASES.items():
        s2.arms_at(cam, [('mem_%s_silmoving' % case, 'silmoving', [], dict(opts))])

S = os.path.join(stage_run.SHOTS, 'rt5b_mem_%s_%s_%d.png')
for case, (cam, opts, (y0, y1, x0, x1)) in CASES.items():
    print(case)
    arms = ['ship', 'silmoving'] + (['noavg'] if case != 'parked' else [])
    for tag in arms:
        grain, change, prev = [], [], None
        for k in range(opts['first'], opts['first'] + opts['frames']):
            im = Image.open(S % (case, tag, k)).convert('L')
            a = np.asarray(im, dtype=float)
            med = np.asarray(im.filter(ImageFilter.MedianFilter(3)), dtype=float)
            g = np.abs(a - med)[y0:y1, x0:x1]
            grain.append(((g > 16).mean() * 100, a[y0:y1, x0:x1].mean()))
            if prev is not None:
                change.append(np.abs(a - prev)[y0:y1, x0:x1].mean())
            prev = a
        grain = np.array(grain)
        print('   %-10s grain %.2f%% px   mean level %.2f   frame-to-frame %.2f'
              % (tag, grain[:, 0].mean(), grain[:, 1].mean(), np.mean(change)))
    if case == 'parked':
        d = max(np.abs(np.asarray(Image.open(S % (case, 'ship', k)), dtype=float)
                       - np.asarray(Image.open(S % (case, 'silmoving', k)), dtype=float)).max()
                for k in range(opts['first'], opts['first'] + opts['frames']))
        print('   parked: largest pixel difference over the run, shipped vs silmoving: %.0f levels' % d)
        continue
    k = opts['first'] + opts['frames'] // 2
    tiles = []
    for tag in arms:
        t = np.asarray(Image.open(S % (case, tag, k)).convert('RGB'))[y0:y1, x0:x1]
        tiles.append(t.repeat(2, 0).repeat(2, 1) if case == 'cube' else t)
    s2._sheet(os.path.join(stage_run.ROOT, 'build', 'rt18', 'silmoving_%s_%d.png' % (case, k)), tiles,
              ['as shipped', 'moving edges tested', 'this frame only (no averaging)'])
