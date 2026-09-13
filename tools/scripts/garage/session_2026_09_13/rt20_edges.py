# -*- coding: utf-8 -*-
"""RT-20: which step of the temporal resolve makes a parked camera's edges shake?

The garage, parked at the owner's shot, the jitter on: per-frame change on edge
pixels (gradient over 12), flat pixels and bright edges (edge and over 150), in
edge_shake.py's regions, frames 150-189. One suspect removed per arm, all else
as committed:

  ship      the resolve as it is
  nogeo     RT-6's surface test and neighbour search off (--taa-geometry=off)
  nobox     RT-6.8's same-surface box off (--taa-box-geometry=off)
  nocr      RT-6.11's Catmull-Rom history fetch made bilinear
  noclip    the neighbourhood clip off
  nojit     the jitter off -- the floor this is all measured against

and a benchmark of the parked frame for the resolve's own refusal counts.
"""
import os, re, subprocess, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stage_run  # noqa: E402

S = stage_run.SHOTS
TAA = stage_run.TAA
NOCR = (TAA, '\t\t\t\t\t\t\t : SampleCatmullRom(u_History, historyUV, u_Params.TexelSize);',
        '\t\t\t\t\t\t\t : texture(u_History, historyUV);')
stage_run.VARIANTS['taanocr'] = [NOCR]
stage_run.VARIANTS['taanoclip'] = [(TAA,) + stage_run.TAA_CLIP]

FIRST, COUNT = 150, 40
ARMS = [
    ('r20_ship', 'ship', [], {}),
    ('r20_nogeo', 'ship', ['--taa-geometry=off'], {}),
    ('r20_nobox', 'ship', ['--taa-box-geometry=off'], {}),
    ('r20_nocr', 'taanocr', [], {}),
    ('r20_noclip', 'taanoclip', [], {}),
    ('r20_nojit', 'ship', [], {'no_jitter': True}),
]
only = sys.argv[1:]
if only and only[0] != '--analyse':
    ARMS = [a for a in ARMS if a[0] in only]
if not (only and only[0] == '--analyse'):
    stage_run.run_arms([(tag, v, flags, dict(opts, first=FIRST, frames=COUNT)) for tag, v, flags, opts in ARMS])


def Y(tag, n):
    a = np.asarray(Image.open(os.path.join(S, 'rt5b_%s_%d.png' % (tag, n))).convert('RGB'), dtype=np.float32)
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def regs(a):
    h, w = a.shape
    r = lambda y0, y1, x0, x1: a[int(h * y0 / 1230):int(h * y1 / 1230), int(w * x0 / 2000):int(w * x1 / 2000)]
    return {'car': r(560, 760, 560, 900), 'wall': r(250, 550, 1020, 1220), 'poles': r(250, 800, 1100, 1500),
            'tubes': r(0, 250, 300, 1700), 'floor': r(880, 1180, 300, 1700)}


print('parked, frames %d-%d: per-frame change on edge / flat / bright-edge pixels, levels' % (FIRST, FIRST + COUNT - 1))
for tag, _, _, _ in ARMS:
    if not os.path.exists(os.path.join(S, 'rt5b_%s_%d.png' % (tag, FIRST + COUNT - 1))):
        continue
    fr = [regs(Y(tag, n)) for n in range(FIRST, FIRST + COUNT)]
    out = []
    for k in ('car', 'wall', 'poles', 'tubes', 'floor'):
        e, f, b = [], [], []
        for i in range(COUNT - 1):
            A, B = fr[i][k], fr[i + 1][k]
            d = np.abs(B - A)
            g = np.zeros_like(A); g[:, 1:] = np.abs(np.diff(A, axis=1))
            gy = np.zeros_like(A); gy[1:, :] = np.abs(np.diff(A, axis=0))
            m = np.maximum(g, gy) > 12
            e.append(d[m].mean() if m.any() else 0); f.append(d[~m].mean())
            bm = m & (A > 150); b.append(d[bm].mean() if bm.any() else 0)
        out.append('%s %5.2f/%4.2f/%5.2f' % (k, np.mean(e), np.mean(f), np.mean(b)))
    print('  %-11s %s' % (tag[4:], '  '.join(out)))
