# -*- coding: utf-8 -*-
"""The chrome cube across roughness: is what falls apart the young history on a wide lobe?

The debug views (build/rt15/views_cube.png) show the moving cube's history as a ramp across
its face: a flat mirror sliding in its own plane shows a still picture, so each screen texel's
history starts when the cube arrives over it -- a few frames at the leading side, forty at the
trailing. The sphere holds one to four frames everywhere and looks right, because at roughness
0.05 one ray is nearly the picture. The owner's two investigation documents (2026-09-15) put a
roughness sweep second, after switching the averaging off (done: the averaged cube is nearer its
settled picture than one frame is -- 5.57 against 10.19 -- so the rays are right and the history
is the question).

For each roughness: the cube stopped at frame 120 and settled (frames 150-169), the moving cube
(frames 116-124, as built), and the moving cube averaging nothing. Scored on the cube's face,
split into its leading half (young history) and trailing half.

Usage: rt15_cube_sweep.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
import rt15_items as it  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
ROUGH = (0.0, 0.05, 0.12, 0.25, 0.5)
K = 120
BASE = stage_run.CUBE_ENTITY
FACE = (300, 420, 640, 860)


def with_roughness(r):
    stage_run.CUBE_ENTITY = BASE.replace('        Roughness: 0.12\n', '        Roughness: %g\n' % r)
    assert stage_run.CUBE_ENTITY != BASE or r == 0.12


if __name__ == '__main__':
    if 'render' in sys.argv:
        try:
            for r in ROUGH:
                with_roughness(r)
                tag = 'r15s_%02d' % int(round(r * 100))
                s2.arms_at('owner', [
                    (tag + '_ref', 'ship', [], dict(frames=20, first=150, mean_from=150, cube=(-9.0, 3.0, K * 0.0166))),
                    (tag + '_ship', 'ship', [], dict(frames=9, first=K - 4, cube=(-9.0, 3.0, 6.0))),
                    (tag + '_noavg', 'noaverage', it.NOBLUR, dict(frames=9, first=K - 4, cube=(-9.0, 3.0, 6.0)))])
        finally:
            stage_run.CUBE_ENTITY = BASE
    L = it.L
    tiles, labels = [], []
    for r in ROUGH:
        tag = 'r15s_%02d' % int(round(r * 100))
        ref = np.load(os.path.join(stage_run.OUT, tag + '_ref.npy'))[:860]
        load = lambda t, f: np.asarray(Image.open(it.S('rt5b_' + t, f)).convert('RGB'), dtype=float)[:860]
        errs = {f: np.abs(L(load(tag + '_ship', f)) - L(ref)).mean() for f in range(K - 4, K + 5)}
        best = min(errs, key=errs.get)
        y0, y1, x0, x1 = FACE
        cells = []
        for arm in ('ship', 'noavg'):
            a = load('%s_%s' % (tag, arm), best)
            e = np.abs(L(a) - L(ref))[y0:y1, x0:x1]
            half = (x1 - x0) // 2
            g = np.asarray(Image.fromarray(a.astype(np.uint8)).convert('L'), dtype=float)
            med = np.asarray(Image.fromarray(a.astype(np.uint8)).convert('L').filter(ImageFilter.MedianFilter(3)), dtype=float)
            cells.append('%s: face %.2f (leading half %.2f, trailing %.2f) grain %.1f%%' % (
                arm, e.mean(), e[:, half:].mean(), e[:, :half].mean(), (np.abs(g - med)[y0:y1, x0:x1] > 16).mean() * 100))
            if arm == 'ship':
                tiles.append(np.clip(a[y0 - 20:y1 + 20, x0 - 20:x1 + 20], 0, 255).repeat(2, 0).repeat(2, 1))
                labels.append('roughness %g, moving' % r)
        tiles.append(np.clip(ref[y0 - 20:y1 + 20, x0 - 20:x1 + 20], 0, 255).repeat(2, 0).repeat(2, 1))
        labels.append('roughness %g, settled' % r)
        print('roughness %-4g (pose frame %d)   %s' % (r, best, '   '.join(cells)))
    s2._sheet(os.path.join(it.OUT, 'cube_roughness.png'), tiles, labels)
