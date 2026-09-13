# -*- coding: utf-8 -*-
"""RT-5 part 3: read the bias arms written by rt5_bias_measure.py.

Every number is a difference of two 64-frame parked means, in 8-bit levels of
the final picture, so it carries the tone curve -- fine for "does the picture
move and where", not for a filter's time constant. The low-passed column is a
9x9 box of the difference: the grain left in a 64-frame mean averages out
there and a bias does not.

The diff images are the judgement; the table is the index into them. Red is
darker than the second arm, green brighter, both amplified eight times.
"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw

ROOT = r'C:\Users\ism19\Code\RageV'
OUT = os.path.join(ROOT, 'build', 'rt5')


def load(arm):
    return np.load(os.path.join(OUT, arm + '.npy')).astype(np.float64)


def luma(a):
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def box(a, r=4):
    k = 2 * r + 1
    p = np.pad(a, r, mode='edge')
    c = np.zeros((p.shape[0] + 1, p.shape[1] + 1))
    c[1:, 1:] = p.cumsum(0).cumsum(1)
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / float(k * k)


def regions(h, w):
    # The owner's shot, in its 2000x1230 coordinates, as bias_check.py has them.
    def r(y0, y1, x0, x1):
        return (slice(int(h * y0 / 1230), int(h * y1 / 1230)), slice(int(w * x0 / 2000), int(w * x1 / 2000)))
    return {'floor': r(880, 1180, 300, 1700), 'car': r(560, 760, 560, 900),
            'wall': r(250, 550, 1020, 1220), 'poles': r(250, 800, 600, 840)}


def compare(a, b):
    A, B = load(a), load(b)
    d = luma(A) - luma(B)
    lp = box(d)
    h, w = d.shape
    row = ['%-26s' % ('%s - %s' % (a, b)), 'frame %+6.3f' % d.mean()]
    for name, sl in regions(h, w).items():
        row.append('%s %+6.3f' % (name, d[sl].mean()))
    row.append('|lp| %5.3f' % np.abs(lp).mean())
    row.append('>0.5 %5.2f%%' % (100.0 * (np.abs(lp) > 0.5).mean()))
    row.append('>1 %5.2f%%' % (100.0 * (np.abs(lp) > 1.0).mean()))
    print('  '.join(row))
    return A, d


def diff_image(d, gain=8.0):
    out = np.zeros(d.shape + (3,))
    out[..., 0] = np.clip(-d * gain, 0, 255)
    out[..., 1] = np.clip(d * gain, 0, 255)
    return out.astype(np.uint8)


def panel(name, tiles):
    ims = []
    for label, arr in tiles:
        im = Image.fromarray(arr).resize((arr.shape[1] // 2, arr.shape[0] // 2), Image.BILINEAR)
        ImageDraw.Draw(im).rectangle((0, 0, im.width, 18), fill=(0, 0, 0))
        ImageDraw.Draw(im).text((6, 3), label, fill=(255, 255, 255))
        ims.append(im)
    W = sum(i.width for i in ims[:2])
    rows = [ims[i:i + 2] for i in range(0, len(ims), 2)]
    H = sum(r[0].height for r in rows)
    sheet = Image.new('RGB', (W, H))
    y = 0
    for r in rows:
        x = 0
        for i in r:
            sheet.paste(i, (x, y))
            x += i.width
        y += r[0].height
    path = os.path.join(OUT, name)
    sheet.save(path)
    print('  wrote', path)


print('difference of 64-frame parked means, levels; lp = 9x9 low-pass of the difference')
pairs = [('ship_k8', 'noclamp_k0'), ('ship_k0', 'noclamp_k0'), ('ship_k8', 'ship_k0'),
         ('noclamp_k8', 'noclamp_k0'), ('noclamp1_k8', 'noclamp1_k0'), ('noclamp2_k8', 'noclamp2_k0'),
         ('noclamp_k8', 'ship_k8')]
got = {}
for a, b in pairs:
    if all(os.path.exists(os.path.join(OUT, x + '.npy')) for x in (a, b)):
        got[(a, b)] = compare(a, b)

if ('ship_k8', 'ship_k0') in got:
    frame = got[('ship_k8', 'ship_k0')][0].clip(0, 255).astype(np.uint8)
    tiles = [('shipped, K=8 (64-frame mean)', frame)]
    for key, label in ((('ship_k8', 'ship_k0'), 'sampled - every light, bound on  (x8)'),
                       (('noclamp_k8', 'noclamp_k0'), 'sampled - every light, bound off (x8)'),
                       (('ship_k0', 'noclamp_k0'), 'bound on - bound off, every light (x8)')):
        if key in got:
            tiles.append((label, diff_image(got[key][1])))
    panel('bias_sheet.png', tiles)
