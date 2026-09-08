"""RT-15: is the moving cube keeping any history, and did the picture change?

The cube is the near-mirror block above the car's tail. Its rectangle is fixed
and hand-picked from a rendered frame, so it is the same pixels in both arms --
the geometry does not depend on the accumulator.
"""
import sys, glob, re, os
import numpy as np
from PIL import Image

SHOTS = r'C:\Users\ism19\Code\RageV\build\garage_burst'
CUBE = (545, 315, 630, 390)      # x0, y0, x1, y1
WALL = (700, 315, 800, 390)      # the static graffiti wall beside it, as a control


def frames(tag):
    got = sorted(glob.glob(os.path.join(SHOTS, tag + '_[0-9]*.png')),
                 key=lambda f: int(re.search(r'_(\d+)\.png$', f).group(1)))
    return got


def arr(path):
    return np.asarray(Image.open(path).convert('RGB'), dtype=float)


def luma(a):
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def crop(a, box):
    x0, y0, x1, y1 = box
    return a[y0:y1, x0:x1]


def report(tag, htag):
    pic = frames(tag)
    his = frames(htag)
    # History length: the debug view writes frames/Memory into the picture, so
    # the level is proportional to how many frames stand behind the pixel.
    h = [luma(crop(arr(f), CUBE)).mean() for f in his]
    hw = [luma(crop(arr(f), WALL)).mean() for f in his]
    # Temporal noise: how much a pixel changes frame to frame.
    L = [luma(arr(f)) for f in pic]
    d = [np.abs(crop(L[i], CUBE) - crop(L[i - 1], CUBE)).mean() for i in range(1, len(L))]
    dw = [np.abs(crop(L[i], WALL) - crop(L[i - 1], WALL)).mean() for i in range(1, len(L))]
    return (np.mean(h), np.mean(hw), np.mean(d), np.mean(dw),
            np.mean([luma(crop(a, CUBE)).mean() for a in map(arr, pic)]))


rows = []
for name, t, h in (('before', 'rt15_before', 'rt15_before_h'),
                   ('after', 'rt15_after', 'rt15_after_h')):
    rows.append((name,) + report(t, h))

print('%-8s %10s %10s %10s %10s %10s' % ('arm', 'histCube', 'histWall', 'noiseCube', 'noiseWall', 'meanCube'))
for r in rows:
    print('%-8s %10.4f %10.4f %10.4f %10.4f %10.4f' % r)

# And the plain difference between the two arms' pictures, per pixel.
b, a = frames('rt15_before'), frames('rt15_after')
for i in range(min(len(b), len(a))):
    diff = np.abs(luma(arr(a[i])) - luma(arr(b[i])))
    print('frame %s  whole-frame mean %.4f  max %.1f  cube mean %.4f'
          % (re.search(r'_(\d+)\.png$', b[i]).group(1), diff.mean(), diff.max(),
             crop(diff, CUBE).mean()))
