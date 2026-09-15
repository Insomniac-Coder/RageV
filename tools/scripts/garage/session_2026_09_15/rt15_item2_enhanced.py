# -*- coding: utf-8 -*-
"""RT-15 item 2 for the owner, enhanced so the differences can be seen (2026-09-15: "zooming in
doesn't do shit either, enhance the details").

For each place, one sheet:
  row 1  OFF, ON, CORRECT -- the same crop, the same fixed enhancement on all three (levels taken
         from CORRECT, then the fine detail amplified), so what differs is the picture, not the filter
  row 2  how far OFF is from CORRECT, how far ON is from CORRECT (black = matches, yellow = wrong,
         same scale), and what switching item 2 on changed

The crop is centred where ON and OFF differ most at the pose frame that has a settled picture.

Usage: rt15_item2_enhanced.py
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cv2  # noqa: E402
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFont, ImageFilter  # noqa: E402
import rt15_item2_pictures as pp  # noqa: E402

it = pp.it
BOLD = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 30)
TEXT = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 26)
CROP_H, CROP_W, SCALE = 112, 200, 4


def spot(case):
    c = pp.CASES[case]
    a = pp.load(case, 'A', c['pose'])
    b = pp.load(case, 'B', c['pose'])
    d = np.abs(a - b).mean(axis=2)
    sm = np.asarray(Image.fromarray(np.clip(d, 0, 255).astype(np.uint8)).filter(ImageFilter.BoxBlur(20)), dtype=float)
    y, x = np.unravel_index(np.argmax(sm), sm.shape)
    y0 = int(np.clip(y - CROP_H // 2, 0, 860 - CROP_H))
    x0 = int(np.clip(x - CROP_W // 2, 0, 1600 - CROP_W))
    return y0, x0


def enhance(crop, lo, hi):
    """Fixed levels (from the correct picture), then the fine detail amplified four times."""
    x = (crop - lo) / max(hi - lo, 1.0)
    base = cv2.GaussianBlur(x, (0, 0), 3.0)
    x = base + 4.0 * (x - base)
    return np.clip(x * 255.0, 0, 255).astype(np.uint8)


def heat(e):
    m = cv2.applyColorMap(np.clip(e * 8.0, 0, 255).astype(np.uint8), cv2.COLORMAP_INFERNO)
    return cv2.cvtColor(m, cv2.COLOR_BGR2RGB)


def panel(arr, title):
    img = Image.fromarray(arr).resize((CROP_W * SCALE, CROP_H * SCALE), Image.NEAREST)
    out = Image.new('RGB', (img.size[0], img.size[1] + 44), (0, 0, 0))
    out.paste(img, (0, 44))
    ImageDraw.Draw(out).text((10, 6), title, font=BOLD, fill=(255, 255, 255))
    return out


def sheet(case, caption):
    c = pp.CASES[case]
    y0, x0 = spot(case)
    sl = (slice(y0, y0 + CROP_H), slice(x0, x0 + CROP_W))
    off = pp.load(case, 'B', c['pose'])[sl]
    on = pp.load(case, 'A', c['pose'])[sl]
    ref = it.CASES[case]['ref']()[sl]
    lum = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    lo, hi = np.percentile(lum(ref), 1), np.percentile(lum(ref), 99)
    rows = [[panel(enhance(off, lo, hi), 'OFF'), panel(enhance(on, lo, hi), 'ON'), panel(enhance(ref, lo, hi), 'CORRECT')],
            [panel(heat(np.abs(lum(off) - lum(ref))), 'OFF: where it is wrong'),
             panel(heat(np.abs(lum(on) - lum(ref))), 'ON: where it is wrong'),
             panel(heat(np.abs(lum(on) - lum(off))), 'What switching ON changed')]]
    pw, ph = rows[0][0].size
    top = 90
    out = Image.new('RGB', (pw * 3 + 24, top + ph * 2 + 12), (45, 45, 45))
    d = ImageDraw.Draw(out)
    for i, line in enumerate(caption):
        d.text((12, 8 + 38 * i), line, font=BOLD if i == 0 else TEXT, fill=(255, 255, 255) if i == 0 else (255, 215, 130))
    for r, row in enumerate(rows):
        for col, p in enumerate(row):
            out.paste(p, (col * (pw + 12), top + r * (ph + 12)))
    path = os.path.join(it.OUT, 'item2_enhanced_%s.png' % case)
    out.save(path)
    # And where the crop is, on the whole frame.
    full = Image.fromarray(np.clip(pp.load(case, 'B', c['pose']) * c['boost'] * 0.8, 0, 255).astype(np.uint8))
    ImageDraw.Draw(full).rectangle((x0 - 4, y0 - 4, x0 + CROP_W + 4, y0 + CROP_H + 4), outline=(255, 60, 60), width=6)
    full.resize((960, 516)).save(os.path.join(it.OUT, 'item2_enhanced_%s_where.png' % case))
    return path


if __name__ == '__main__':
    captions = {
        'car': ['Where item 2 HELPS: a light reflected in the floor, right beside the driving car',
                'Look at the bottom row. Yellow = wrong. OFF has a big yellow blob on the bright streak; ON’s blob is a little smaller, so ON is a little closer to correct.'],
        'cube': ['Where item 2 HURTS: the floor under the chrome cube as it crosses',
                 'Look at the bottom row. Yellow = wrong. OFF is dark almost everywhere; ON has a bright patch in the middle, where the floor’s light ripples got smeared sideways.'],
        'sphere': ['Where item 2 HURTS: the floor under the chrome sphere as it crosses',
                   'Look at the bottom row. Yellow = wrong. OFF is dark almost everywhere; ON has a bright patch in the middle, where the floor’s light ripples got smeared sideways.'],
    }
    for case in pp.CASES:
        print(sheet(case, captions[case]))
