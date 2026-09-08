"""Where is the moving object, really -- and how much history does it hold?

The rectangle used for the first numbers was picked by eye off one frame. This
finds the moving pixels instead: the cube travels 3 m/s with the camera still,
so it is the only thing whose picture changes shape over six frames. The two
arms are geometrically identical, so a box found in one is honest in both.
"""
import glob, os, re
import numpy as np
from PIL import Image

SHOTS = r'C:\Users\ism19\Code\RageV\build\garage_burst'


def frames(tag):
    return sorted(glob.glob(os.path.join(SHOTS, tag + '_[0-9]*.png')),
                  key=lambda f: int(re.search(r'_(\d+)\.png$', f).group(1)))


def luma(p):
    a = np.asarray(Image.open(p).convert('RGB'), dtype=float)
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


f = frames('rt15_before')
first, last = luma(f[0]), luma(f[-1])
moved = np.abs(last - first)
# The moving object is where the picture changed most between the ends.
mask = moved > np.percentile(moved, 99.5)
ys, xs = np.nonzero(mask)
print('moving pixels: %d, box x %d..%d  y %d..%d'
      % (mask.sum(), xs.min(), xs.max(), ys.min(), ys.max()))
# The densest block of that mask, which is the object rather than its trail.
h, w = mask.shape
best, box = -1, None
for y0 in range(0, h - 60, 20):
    for x0 in range(0, w - 60, 20):
        c = mask[y0:y0 + 60, x0:x0 + 60].sum()
        if c > best:
            best, box = c, (x0, y0, x0 + 60, y0 + 60)
print('densest 60x60 block: %s with %d moving pixels' % (box, best))

x0, y0, x1, y1 = box
for tag in ('rt15_before_h', 'rt15_after_h'):
    v = [luma(p)[y0:y1, x0:x1].mean() for p in frames(tag)]
    print('%-16s history level in that block: %s' % (tag, ' '.join('%.1f' % x for x in v)))
for tag in ('rt15_before', 'rt15_after'):
    L = [luma(p) for p in frames(tag)]
    d = [np.abs(L[i][y0:y1, x0:x1] - L[i - 1][y0:y1, x0:x1]).mean() for i in range(1, len(L))]
    print('%-16s frame-to-frame change there: %.3f' % (tag, np.mean(d)))
