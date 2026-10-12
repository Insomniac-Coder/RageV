# -*- coding: utf-8 -*-
"""RT-24: where the trails are -- edges the mid-swing frame has and the settled one does not.

Reads spin_measure.py's `<tag>_moving.png` and `<tag>_settled.png` (build/rt24) and paints
every such edge red over the settled frame, dimmed: a pole's reflection left behind is a
red line the settled picture has no edge under, the car's bumper outline a red curve on the
floor. Grain makes red specks too; a trail is a line.

    ghost_map.py <tag> [<tag> ...]
"""
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'build', 'rt24')


def edges(path):
    a = np.asarray(Image.open(path).convert('L').filter(ImageFilter.GaussianBlur(1)), dtype=np.float32)
    gx = np.zeros_like(a)
    gy = np.zeros_like(a)
    gx[:, 1:-1] = a[:, 2:] - a[:, :-2]
    gy[1:-1, :] = a[2:, :] - a[:-2, :]
    return np.hypot(gx, gy)


def ghosts(tag, frame='moving'):
    moving = edges(os.path.join(OUT, '%s_%s.png' % (tag, frame)))
    settled = edges(os.path.join(OUT, tag + '_settled.png'))
    near = np.asarray(Image.fromarray((settled > 12).astype(np.uint8) * 255)
                      .filter(ImageFilter.MaxFilter(7))) > 0
    return (moving > 24) & ~near


if __name__ == '__main__':
    for arg in sys.argv[1:]:
        tag, _, frame = arg.partition(':')
        frame = frame or 'moving'
        g = ghosts(tag, frame)
        # Specks against lines: a pixel whose 5x5 holds at least five ghost pixels sits on
        # something drawn, not on a lone grain.
        dense = np.asarray(Image.fromarray(g.astype(np.uint8) * 51).filter(ImageFilter.BoxBlur(2)),
                           dtype=np.float32) * 25.0 / 51.0 >= 5.0
        lines = g & dense
        floor = slice(int(g.shape[0] * 0.55), None)
        print('%-26s ghost edges: floor %.3f%%, of which in lines %.3f%%' % (
            arg, 100.0 * g[floor].mean(), 100.0 * lines[floor].mean()))
        base = np.asarray(Image.open(os.path.join(OUT, tag + '_settled.png')).convert('RGB'),
                          dtype=np.float32) * 0.4
        base[g] = [120, 0, 0]
        base[lines] = [255, 40, 40]
        Image.fromarray(base.astype(np.uint8)).save(os.path.join(OUT, '%s_%s_ghosts.png' % (tag, frame)))
