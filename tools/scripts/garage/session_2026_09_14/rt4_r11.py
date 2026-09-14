# -*- coding: utf-8 -*-
"""RT-4 / R11: is the converged wet floor darker than the truth?

R11 (docs/RENDERING-REVAMP.md): the floor settled 3.2-3.5 levels darker than an
unclamped converged reference, with the resolve's ratio cap the first suspect. Since
then the histories' half-float rounding was fixed (2026-09-13) and the reflection
chain moved before the lit pass (RT-4). Re-measured here, the garage parked, the
mean of frames 360-399:

  ship      the build as it is
  truth     no resolve (each texel's own ray), the accumulator unbounded, memory 400
  truthres  the same with the shipped resolve: the resolve's share of any bias
  gauss     shipped, but the resolve's weights Gaussian only (no pdf ratio, no cap)

Usage: rt4_r11.py [arm ...] | rt4_r11.py --analyse
"""
import os, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
import stage_run  # noqa: E402
import rt20_measure as M  # noqa: E402

ACC, RES = stage_run.ACC, 'reflection_resolve.rvshader'
MEMORY = [
    (ACC, '\t\t\tfloat memory = ShortenedMemory(max(u_Reflection.History.y, 1.0),',
     '\t\t\tfloat memory = ShortenedMemory(400.0,'),
    (ACC, '\t\t\tconst float reduced = memory / max(ShortenedMemory(max(u_Reflection.History.y, 1.0),',
     '\t\t\tconst float reduced = memory / max(ShortenedMemory(400.0,'),
]
NO_RESOLVE = [(RES, '\tif (alpha < 1.0e-4)\n\t\treturn;   // a mirror: its one ray is the whole lobe',
               '\tif (true)\n\t\treturn;   // R11 truth: each texel its own ray')]
GAUSS = [(RES, '\t\tconst float w = min(pdfHere / max(pdfTheirs, 1.0e-4), kMaxWeight) * exp(-2.0 * r * r);',
          '\t\tconst float w = exp(-2.0 * r * r);')]
stage_run.VARIANTS['r11truth'] = stage_run.VARIANTS['accfree'] + MEMORY + NO_RESOLVE
stage_run.VARIANTS['r11truthres'] = stage_run.VARIANTS['accfree'] + MEMORY
stage_run.VARIANTS['r11gauss'] = GAUSS

ARMS = [('r11_ship', 'ship'), ('r11_truth', 'r11truth'), ('r11_truthres', 'r11truthres'), ('r11_gauss', 'r11gauss')]
only = sys.argv[1:]
if not (only and only[0] == '--analyse'):
    arms = [(t, v, [], dict(first=360, frames=40, mean_from=360)) for t, v in ARMS if not only or t in only]
    stage_run.run_arms(arms)


def mean_of(tag):
    return np.load(os.path.join(stage_run.OUT, tag + '.npy'))


def box(a, k=9):
    c = np.cumsum(np.cumsum(np.pad(a, ((k // 2 + 1, k // 2), (k // 2 + 1, k // 2)), mode='edge'), 0), 1)
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)


truth = M.luma(mean_of('r11_truth'))
floor = (slice(520, 880), slice(0, 1600))
rows = []
print('garage parked, mean of frames 360-399; low-frequency (9x9) luma difference against the truth')
for tag, _ in ARMS:
    if tag == 'r11_truth' or not os.path.exists(os.path.join(stage_run.OUT, tag + '.npy')):
        continue
    d = box(M.luma(mean_of(tag)) - truth)
    f = d[floor]
    bright = truth[floor] > 80
    print('  %-13s floor mean %+.2f, mean|.| %.2f; bright floor %+.2f; whole frame %+.2f'
          % (tag[4:], f.mean(), np.abs(f).mean(), f[bright].mean() if bright.any() else 0.0, d.mean()))
    rows.append([M.label(mean_of(tag), tag[4:]), M.label(M.signed(d, 8), tag[4:] + ' minus truth, x8 (green brighter)')])
rows.append([M.label(mean_of('r11_truth'), 'truth'), M.label(np.zeros_like(mean_of('r11_truth')), '')])
M.sheet(rows, r'C:\Users\ism19\Code\RageV\build\rt5\rt4_r11_sheet.png')
