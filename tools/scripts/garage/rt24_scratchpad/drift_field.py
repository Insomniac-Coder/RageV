"""Where the reflection's lookup puts things: block-match <tag>_moving.png (a centre
crossing of the drift arm) against <tag>_settled.png (the same pose, settled), 32 px
blocks over the floor, and print each block's best shift in pixels (dx,dy) and how much
of the mismatch that shift explains. usage: drift_field.py <tag> [<tag> ...]"""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import sys
import numpy as np
from PIL import Image

OUT = (_RAGEV + '/build/rt24/')
B, SX, SY = 32, 16, 6


def luma(p):
    a = np.asarray(Image.open(p).convert('RGB')).astype(np.float64)
    return a @ [0.2126, 0.7152, 0.0722]


def field(tag):
    m, s = luma(OUT + tag + '_moving.png'), luma(OUT + tag + '_settled.png')
    rows = []
    for y0 in range(480, 860 - B + 1, B):
        row = []
        for x0 in range(SX, 1600 - B - SX + 1, B):
            ref = s[y0:y0 + B, x0:x0 + B]
            if ref.std() < 2.0:
                row.append(None)
                continue
            best, at = None, (0, 0)
            for dy in range(-SY, SY + 1):
                if y0 + dy < 0 or y0 + dy + B > m.shape[0]:
                    continue
                for dx in range(-SX, SX + 1):
                    e = np.abs(m[y0 + dy:y0 + dy + B, x0 + dx:x0 + dx + B] - ref).mean()
                    if best is None or e < best:
                        best, at = e, (dx, dy)
            e0 = np.abs(m[y0:y0 + B, x0:x0 + B] - ref).mean()
            row.append((at[0], at[1], e0, best))
        rows.append((y0, row))
    return rows


for tag in sys.argv[1:]:
    rows = field(tag)
    print('%s: best horizontal shift per 32 px block (moving vs settled), "." = flat block' % tag)
    allx, e0s, e1s = [], [], []
    for y0, row in rows:
        print('  y%4d ' % y0 + ''.join('   .' if b is None else '%4d' % b[0] for b in row))
        for b in row:
            if b is not None:
                allx.append(b[0]); e0s.append(b[2]); e1s.append(b[3])
    allx = np.array(allx)
    print('  blocks %d  |dx| median %.1f  mean %.1f  share |dx|>=2: %.0f%%' %
          (len(allx), np.median(np.abs(allx)), np.abs(allx).mean(), (np.abs(allx) >= 2).mean() * 100))
    print('  mismatch in place %.2f  after the best shift %.2f (levels)' % (np.mean(e0s), np.mean(e1s)))
