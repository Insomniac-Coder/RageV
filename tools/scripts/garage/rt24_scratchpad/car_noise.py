"""The owner saw the car's reflections get noisy while the glowing cube drives (2026-10-12).
Captures the drive at the live window's close camera, frames 60-129, and the same scene with the
cube standing at its start; reports the car's frame-to-frame flicker of fine detail (high-pass).
usage: car_noise.py capture <tag> [engine flags...] | report <tag>..."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import os, sys
import numpy as np
from PIL import Image
G = (_RAGEV + '/tools/scripts/garage')
sys.path.insert(0, os.path.join(G, 'session_2026_09_15'))
import emitter_mover as em
em.s2.CAMS['owner'] = em.s2.CAMS['close']          # the live window's camera
SH = em.stage_run.SHOTS
CAR = (420, 800, 500, 1500)                         # rows, cols: hood, windscreen, door


def lum(p):
    a = np.asarray(Image.open(p).convert('RGB'), dtype=float)
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def hp(a, r=4):
    k = 2 * r + 1
    p = np.pad(a, r, mode='edge')
    c = np.zeros((p.shape[0] + 1, p.shape[1] + 1))
    c[1:, 1:] = p.cumsum(0).cumsum(1)
    return a - (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)


def flicker(tag):
    y0, y1, x0, x1 = CAR
    f = [hp(lum(os.path.join(SH, '%s_%d.png' % (tag, n)))[y0:y1, x0:x1]) for n in range(60, 130)]
    return np.mean([np.abs(f[i + 1] - f[i]).mean() for i in range(len(f) - 1)])


if __name__ != '__main__':
    pass
elif sys.argv[1] == 'capture':
    tag, extra = sys.argv[2], sys.argv[3:]
    em.burst(tag + '_drive', em.STOP, 60, 70, extra)
    em.burst(tag + '_still', 0.0001, 60, 70, extra)
else:
    for tag in sys.argv[2:]:
        d, s = flicker(tag + '_drive'), flicker(tag + '_still')
        print('%-14s car flicker: cube moving %.2f   cube still %.2f   ratio %.2f' % (tag, d, s, d / s))
