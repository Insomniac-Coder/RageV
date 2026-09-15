# -*- coding: utf-8 -*-
"""RT-15 item 2: the three versions and the correct picture, side by side, with the place to look
circled on the pictures themselves (owner, 2026-09-15: "I don't know what to look at, and you
haven't explained the best mix").

Columns: OFF (item 2 off), ON plain (item 2 as first built: always follow), ON best mix (follow only
where most rays around the pixel hit the moving object, and keep the followed old picture only where
it looks more like this frame), CORRECT (the object stopped there and settled). Items 1 and 3 on in
all three. Same fixed enhancement on every panel (levels from CORRECT, fine detail amplified).

Usage: rt15_item2_circled.py
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cv2  # noqa: E402
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFont, ImageFilter  # noqa: E402
import rt15_item2_pictures as pp  # noqa: E402
import rt15_item2_enhanced as en  # noqa: E402

it = pp.it
BOLD = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 30)
TEXT = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 26)
H, W, S = 112, 200, 3


def frames(case):
    c = pp.CASES[case]
    k = c['pose']
    plain = np.asarray(Image.open(it.S(it.tag_of(case, 'ship'), k)).convert('RGB'), dtype=float)[:860]
    return [('OFF', pp.load(case, 'B', k)), ('ON, plain', plain), ('ON, best mix', pp.load(case, 'A', k)),
            ('CORRECT', it.CASES[case]['ref']())]


def build(case, head, notes):
    shots = frames(case)
    off, plain = shots[0][1], shots[1][1]
    d = np.abs(plain - off).mean(axis=2)
    sm = np.asarray(Image.fromarray(np.clip(d, 0, 255).astype(np.uint8)).filter(ImageFilter.BoxBlur(20)), dtype=float)
    y, x = np.unravel_index(np.argmax(sm), sm.shape)
    y0 = int(np.clip(y - H // 2, 0, 860 - H))
    x0 = int(np.clip(x - W // 2, 0, 1600 - W))
    ref = shots[3][1][y0:y0 + H, x0:x0 + W]
    lum = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    lo, hi = np.percentile(lum(ref), 1), np.percentile(lum(ref), 99)
    # The circle: where plain and OFF differ, inside the crop.
    dc = sm[y0:y0 + H, x0:x0 + W]
    ys, xs = np.nonzero(dc > 0.45 * dc.max())
    cy0, cy1, cx0, cx1 = ys.min(), ys.max(), xs.min(), xs.max()
    pad = 6
    ell = ((cx0 - pad) * S, (cy0 - pad) * S, (cx1 + pad) * S, (cy1 + pad) * S)
    pw, ph = W * S, H * S
    top, label_h, note_h = 96, 44, 76
    out = Image.new('RGB', (pw * 4 + 36, top + label_h + ph + note_h), (45, 45, 45))
    dr = ImageDraw.Draw(out)
    dr.text((12, 10), head[0], font=BOLD, fill=(255, 255, 255))
    dr.text((12, 52), head[1], font=TEXT, fill=(255, 215, 130))
    for i, (name, full) in enumerate(shots):
        px = i * (pw + 12)
        crop = en.enhance(full[y0:y0 + H, x0:x0 + W], lo, hi)
        tile = Image.fromarray(crop).resize((pw, ph), Image.NEAREST)
        td = ImageDraw.Draw(tile)
        td.ellipse(ell, outline=(255, 230, 0), width=5)
        dr.rectangle((px, top, px + pw, top + label_h), fill=(0, 0, 0))
        dr.text((px + 10, top + 6), name, font=BOLD, fill=(150, 255, 150) if name == 'CORRECT' else (255, 255, 255))
        out.paste(tile, (px, top + label_h))
        for k, line in enumerate(notes[i]):
            dr.text((px + 10, top + label_h + ph + 8 + k * 32), line, font=TEXT, fill=(235, 235, 235))
    path = os.path.join(it.OUT, 'item2_circled_%s.png' % case)
    out.save(path)
    return path


if __name__ == '__main__':
    which = sys.argv[1] if len(sys.argv) > 1 else None
    blank = [['', ''], ['', ''], ['', ''], ['', '']]
    for case in pp.CASES:
        if which and case != which:
            continue
        print(build(case, ['', ''], blank))
