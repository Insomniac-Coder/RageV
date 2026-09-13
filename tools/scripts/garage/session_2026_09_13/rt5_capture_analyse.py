# -*- coding: utf-8 -*-
"""Compare two runs' --capture-signals means, in linear values, stage by stage.

Each capture is <shots>/<tag>_<name><attachment>.npy: a float array (height,
width, 4), the mean of a history over the screenshot frames. For every capture
the two tags share, prints the relative difference of the luminance, B against
A, over the whole picture and the owner's four regions -- in per cent, because
a linear value has no "levels" and a half-per-cent darkening is the question.

Usage: rt5_capture_analyse.py <tagA> <tagB> [names...]
"""
import os, sys
import numpy as np

S = r'C:\Users\ism19\Code\RageV\build\garage_burst'
A, B = 'rt5b_' + sys.argv[1], 'rt5b_' + sys.argv[2]
names = sys.argv[3:] or ['direct0', 'direct3', 'reflections0', 'occlusion0', 'gi0', 'taa0']


def regions(h, w):
    def r(y0, y1, x0, x1):
        return (slice(int(h * y0 / 1230), int(h * y1 / 1230)), slice(int(w * x0 / 2000), int(w * x1 / 2000)))
    return {'whole': (slice(None), slice(None)), 'floor': r(880, 1180, 300, 1700),
            'car': r(560, 760, 560, 900), 'wall': r(250, 550, 1020, 1220), 'poles': r(250, 800, 600, 840)}


print('%s against %s: (B - A) / A of the mean luminance, per cent' % (B, A))
for name in names:
    pa, pb = os.path.join(S, '%s_%s.npy' % (A, name)), os.path.join(S, '%s_%s.npy' % (B, name))
    if not (os.path.exists(pa) and os.path.exists(pb)):
        continue
    a, b = np.load(pa).astype(np.float64), np.load(pb).astype(np.float64)
    if a.shape != b.shape:
        print('  %-14s shapes differ %s %s' % (name, a.shape, b.shape))
        continue
    finite = np.isfinite(a).all(axis=2) & np.isfinite(b).all(axis=2)
    la = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    lb = 0.2126 * b[..., 0] + 0.7152 * b[..., 1] + 0.0722 * b[..., 2]
    row = []
    for key, sl in regions(*la.shape).items():
        m = finite[sl]
        ma, mb = la[sl][m].mean(), lb[sl][m].mean()
        row.append('%s %+7.3f%%' % (key, 100.0 * (mb - ma) / ma if ma != 0 else float('nan')))
    bad = (~finite).sum()
    print('  %-14s %s%s' % (name, '  '.join(row), ('   (%d non-finite texels skipped)' % bad) if bad else ''))
