# -*- coding: utf-8 -*-
"""RT-15 item 2, two layers with the moving one added after the temporal resolve: the owner's
circled sheets (same format as rt15_item2_circled.py).

  carwide  the car's own reflection in the floor, the owner's shot: OFF, ON (two layers), CORRECT
  car      beside the car at the close-up: OFF, ON (two layers), CORRECT
  cube     the floor under the chrome cube: OFF, ON plain, ON best mix, ON two layers, CORRECT
  sphere   the same under the chrome sphere

Usage: rt15_twolayer_sheets.py [case ...]   (notes are filled in below after looking)
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFont, ImageFilter  # noqa: E402
import rt15_twolayer as tl  # noqa: E402
import rt15_item2_enhanced as en  # noqa: E402

it, pp = tl.it, tl.pp
BOLD = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 28)
TEXT = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 24)
H, W, S = 112, 200, 3
SHOTS = it.stage_run.SHOTS


def img(name):
    return np.asarray(Image.open(os.path.join(SHOTS, name)).convert('RGB'), dtype=float)[:860]


def centre_between(a, b, scale=4.0):
    d = np.abs(a - b).mean(axis=2)
    sm = np.asarray(Image.fromarray(np.clip(d * scale, 0, 255).astype(np.uint8)).filter(ImageFilter.BoxBlur(20)), dtype=float)
    y, x = np.unravel_index(np.argmax(sm), sm.shape)
    y0 = int(np.clip(y - H // 2, 0, 860 - H))
    x0 = int(np.clip(x - W // 2, 0, 1600 - W))
    dc = sm[y0:y0 + H, x0:x0 + W]
    ys, xs = np.nonzero(dc > 0.45 * dc.max())
    return y0, x0, (xs.min(), ys.min(), xs.max(), ys.max())


def build(name, shots, ref, box, head, notes):
    y0, x0, (ex0, ey0, ex1, ey1) = box
    crop_ref = ref[y0:y0 + H, x0:x0 + W]
    lum = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    lo, hi = np.percentile(lum(crop_ref), 1), np.percentile(lum(crop_ref), 99)
    pad = 6
    ell = ((ex0 - pad) * S, (ey0 - pad) * S, (ex1 + pad) * S, (ey1 + pad) * S)
    cols = shots + [('CORRECT', ref)]
    pw, ph = W * S, H * S
    top, label_h, note_h = 96, 44, 76
    out = Image.new('RGB', (pw * len(cols) + 12 * (len(cols) - 1), top + label_h + ph + note_h), (45, 45, 45))
    dr = ImageDraw.Draw(out)
    dr.text((12, 10), head[0], font=BOLD, fill=(255, 255, 255))
    dr.text((12, 52), head[1], font=TEXT, fill=(255, 215, 130))
    for i, (label, full) in enumerate(cols):
        px = i * (pw + 12)
        tile = Image.fromarray(en.enhance(full[y0:y0 + H, x0:x0 + W], lo, hi)).resize((pw, ph), Image.NEAREST)
        ImageDraw.Draw(tile).ellipse(ell, outline=(255, 230, 0), width=5)
        dr.rectangle((px, top, px + pw, top + label_h), fill=(0, 0, 0))
        colour = (150, 255, 150) if label == 'CORRECT' else (120, 200, 255) if 'two layers' in label else (255, 255, 255)
        dr.text((px + 10, top + 6), label, font=BOLD, fill=colour)
        out.paste(tile, (px, top + label_h))
        for j, line in enumerate(notes[i] if i < len(notes) else []):
            dr.text((px + 10, top + label_h + ph + 8 + j * 30), line, font=TEXT, fill=(235, 235, 235))
    path = os.path.join(it.OUT, 'item2_final_%s.png' % name)
    out.save(path)
    for label, full in shots:
        e = np.abs(lum(full[y0:y0 + H, x0:x0 + W]) - lum(crop_ref))
        print('   %-16s error inside the crop %.2f' % (label, e.mean()))
    return path


def cases():
    out = {}
    k = 76
    off, two = img('rt5b_r15t3_carwide_off_%d.png' % k), img('rt5b_r15t3_carwide_two_%d.png' % k)
    ref = np.load(os.path.join(it.stage_run.OUT, 'r15t3_carwide_ref.npy'))[:860]
    out['carwide'] = ([('OFF', off), ('ON, two layers', two)], ref, centre_between(off, two))
    k = pp.CASES['car']['pose']
    off, two = tl.load('car', 'off', k), img('rt5b_r15t3_car_two_%d.png' % k)
    out['car'] = ([('OFF', off), ('ON, two layers', two)], it.CASES['car']['ref'](), centre_between(off, two))
    for case in ('cube', 'sphere'):
        k = pp.CASES[case]['pose']
        plain = np.asarray(Image.open(it.S(it.tag_of(case, 'ship'), k)).convert('RGB'), dtype=float)[:860]
        off = tl.load(case, 'off', k)
        two = img(('r15t3_sphere_two_%d.png' if case == 'sphere' else 'rt5b_r15t3_cube_two_%d.png') % k)
        out[case] = ([('OFF', off), ('ON, plain', plain), ('ON, best mix', pp.load(case, 'A', k)), ('ON, two layers', two)],
                     it.CASES[case]['ref'](), centre_between(plain, pp.load(case, 'B', k), 1.0))
    return out


NOTES = {}
HEADS = {}

if __name__ == '__main__':
    want = [a for a in sys.argv[1:]] or ['carwide', 'car', 'cube', 'sphere']
    all_cases = cases()
    for name in want:
        shots, ref, box = all_cases[name]
        print(name)
        print(build(name, shots, ref, box, HEADS.get(name, ['', '']), NOTES.get(name, [])))
