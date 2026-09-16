# -*- coding: utf-8 -*-
"""RT-15f: the history fix -- the young blur on any reflector that moves on its own.

The accumulator now marks a texel whose surface moved on its own (flat or curved), and the blur
pass reads that mark as it already did for curved movers: a blur of THIS frame's picture across
the same surface, radius `--reflection-moving-blur` times (1 - frames/BlurFrames)^2, nothing once
the history is grown. A texel young because the camera moved is not marked.

  radius   0 (flat movers unmarked, as before), 4, 8, 12 (the shipped number for curved movers)
  rays     --reflection-moving-rays 1 against 4, at the chosen radius

Arms: the owner's shot, the chrome cube crossing the car at the scene's roughness 0.12, TAA.
Sheets and a grain number: the share of pixels more than 16 levels from a 3x3 median of the
cube's face (speckle), and the face against the same cube stopped in the same place.

Usage: rt15f_historyfix.py render [tag ...] | measure [tag ...]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
import rt15_items as it  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
S = stage_run.SHOTS
OUT = os.path.join(stage_run.ROOT, 'build', 'rt15', 'objectaware')
MOVE = dict(frames=22, first=100, cube=(-9.0, 3.0, 6.0))
FACE = (700, 780, 322, 388)      # x0, x1, y0, y1 at frame 120
LW = np.array([0.2126, 0.7152, 0.0722])
ARMS = {
    'blur0': ['--reflection-moving-blur=0'],
    'blur4': ['--reflection-moving-blur=4'],
    'blur8': ['--reflection-moving-blur=8'],
    'blur12': [],
    'blur4_rays1': ['--reflection-moving-blur=4', '--reflection-moving-rays=1'],
    'blur8_rays1': ['--reflection-moving-blur=8', '--reflection-moving-rays=1'],
}


def img(tag, f):
    return np.asarray(Image.open(os.path.join(S, 'rt5b_%s_%d.png' % (tag, f))).convert('RGB'), np.float64)


def grain(a, box):
    """The share of pixels more than 16 levels from their 3x3 median: speckle, not detail."""
    x0, x1, y0, y1 = box
    g = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).convert('L')
    med = np.asarray(g.filter(ImageFilter.MedianFilter(3)), np.float64)
    return (np.abs(np.asarray(g, np.float64) - med)[y0:y1, x0:x1] > 16).mean() * 100


def hp(p, r=6):
    return p - np.convolve(np.pad(p, r, mode='edge'), np.ones(2 * r + 1) / (2 * r + 1), mode='valid')


def measure(tags):
    ref = np.mean([img('r15d_stopped', f) for f in range(170, 190)], axis=0)
    x0, x1, y0, y1 = FACE
    print('%-14s %8s %8s %8s' % ('arm', 'speckle%', 'off ref', 'detail'))
    tiles = []
    for tag in tags:
        a = img(tag, 120)
        art = (a[y0:y1, x0:x1] - ref[y0:y1, x0:x1]) @ LW
        detail = np.std(hp((a[y0:y1, x0:x1] @ LW).mean(axis=0)))
        print('%-14s %8.2f %8.2f %8.2f' % (tag, grain(a, FACE), np.abs(art).mean(), detail))
        tiles.append(a[300:425, 690:790].astype(np.uint8))
    tiles.append(ref[300:425, 690:790].astype(np.uint8))
    row = np.concatenate([np.pad(t, ((0, 0), (0, 3), (0, 0))) for t in tiles], axis=1)
    path = os.path.join(OUT, '8_historyfix_arms.png')
    Image.fromarray(row).resize((row.shape[1] * 4, row.shape[0] * 4), Image.NEAREST).save(path)
    print(path, '| strips:', ', '.join(tags), ', stopped reference')


if __name__ == '__main__':
    todo = sys.argv[2:] or list(ARMS)
    if sys.argv[1] == 'render':
        try:
            s2.arms_at('owner', [('r15f_hf_' + a, 'ship', ARMS[a], dict(MOVE)) for a in todo])
        finally:
            print('restored:', stage_run.restore())
    else:
        measure(['r15f_hf_' + a for a in todo])
