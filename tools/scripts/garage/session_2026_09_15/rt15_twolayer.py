# -*- coding: utf-8 -*-
"""RT-15 item 2 as two layers: the renders, and the owner's circled sheets with it beside the rest.

  off     the moving layer switched off (staged: MovingLayerLive() false) -- item 2 off
  two     the build: the resolve splits each picture by what its rays struck, the still layer
          is reprojected as before, the moving layer follows the moving thing

Same places, frames and crops as rt15_item2_pictures.py / rt15_item2_circled.py, so the sheets
line up with the ones the owner already has: OFF, ON plain, ON best mix, ON two layers, CORRECT.

Usage: rt15_twolayer.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFont, ImageFilter  # noqa: E402
import rt15_item2_pictures as pp  # noqa: E402
import rt15_item2_enhanced as en  # noqa: E402

it = pp.it
stage_run, s2 = it.stage_run, it.s2
ACC = it.ACC
stage_run.VARIANTS['layersoff'] = [(ACC, 'bool MovingLayerLive() { return u_Reflection.Change.z > 0.5; }',
                                    'bool MovingLayerLive() { return false; }')]
BOLD = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 28)
TEXT = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 24)
H, W, S = 112, 200, 3


def tag(case, arm):
    return ('r15t2_sphere_%s' % arm) if case == 'sphere' else 'rt5b_r15t2_%s_%s' % (case, arm)


def load(case, arm, f):
    return np.asarray(Image.open(it.S(tag(case, arm), f)).convert('RGB'), dtype=float)[:860]


def render():
    for case, c in pp.CASES.items():
        arms = (('off', 'layersoff'), ('two', 'ship'))
        if case == 'sphere':
            for arm, variant in arms:
                it.sphere_burst('r15t2_sphere_%s' % arm, variant, [], c['first'], pp.COUNT)
        else:
            moving = dict(BURST_SLIDE='=porsche_992_gt3_r|1.0|2.0') if case == 'car' else dict(cube=(-9.0, 3.0, 6.0))
            cam = 'close' if case == 'car' else 'owner'
            s2.arms_at(cam, [('r15t2_%s_%s' % (case, arm), variant, [], dict(frames=pp.COUNT, first=c['first'], **moving))
                             for arm, variant in arms])


def sheet(case, head, notes):
    c = pp.CASES[case]
    k = c['pose']
    plain = np.asarray(Image.open(it.S(it.tag_of(case, 'ship'), k)).convert('RGB'), dtype=float)[:860]
    shots = [('OFF', load(case, 'off', k)), ('ON, plain', plain), ('ON, best mix', pp.load(case, 'A', k)),
             ('ON, two layers', load(case, 'two', k)), ('CORRECT', it.CASES[case]['ref']())]
    # The crop and circle of the sheets the owner has: where plain and OFF differ most.
    old_off = pp.load(case, 'B', k)
    d = np.abs(plain - old_off).mean(axis=2)
    sm = np.asarray(Image.fromarray(np.clip(d, 0, 255).astype(np.uint8)).filter(ImageFilter.BoxBlur(20)), dtype=float)
    y, x = np.unravel_index(np.argmax(sm), sm.shape)
    y0 = int(np.clip(y - H // 2, 0, 860 - H))
    x0 = int(np.clip(x - W // 2, 0, 1600 - W))
    ref = shots[-1][1][y0:y0 + H, x0:x0 + W]
    lum = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    lo, hi = np.percentile(lum(ref), 1), np.percentile(lum(ref), 99)
    dc = sm[y0:y0 + H, x0:x0 + W]
    ys, xs = np.nonzero(dc > 0.45 * dc.max())
    pad = 6
    ell = ((xs.min() - pad) * S, (ys.min() - pad) * S, (xs.max() + pad) * S, (ys.max() + pad) * S)
    pw, ph = W * S, H * S
    top, label_h, note_h = 96, 44, 76
    out = Image.new('RGB', (pw * len(shots) + 12 * (len(shots) - 1), top + label_h + ph + note_h), (45, 45, 45))
    dr = ImageDraw.Draw(out)
    dr.text((12, 10), head[0], font=BOLD, fill=(255, 255, 255))
    dr.text((12, 52), head[1], font=TEXT, fill=(255, 215, 130))
    for i, (name, full) in enumerate(shots):
        px = i * (pw + 12)
        tile = Image.fromarray(en.enhance(full[y0:y0 + H, x0:x0 + W], lo, hi)).resize((pw, ph), Image.NEAREST)
        ImageDraw.Draw(tile).ellipse(ell, outline=(255, 230, 0), width=5)
        dr.rectangle((px, top, px + pw, top + label_h), fill=(0, 0, 0))
        colour = (150, 255, 150) if name == 'CORRECT' else (120, 200, 255) if name == 'ON, two layers' else (255, 255, 255)
        dr.text((px + 10, top + 6), name, font=BOLD, fill=colour)
        out.paste(tile, (px, top + label_h))
        for j, line in enumerate(notes[i]):
            dr.text((px + 10, top + label_h + ph + 8 + j * 30), line, font=TEXT, fill=(235, 235, 235))
    path = os.path.join(it.OUT, 'item2_twolayer_%s.png' % case)
    out.save(path)
    # And the numbers, for the record: error against CORRECT inside the crop, per column.
    for name, full in shots[:-1]:
        e = np.abs(lum(full[y0:y0 + H, x0:x0 + W]) - lum(ref))
        print('   %-15s error in the circle crop %.2f' % (name, e.mean()))
    return path


if __name__ == '__main__':
    if 'render' in sys.argv:
        render()
    blank = [['', '']] * 5
    for case in pp.CASES:
        print(case)
        print(sheet(case, ['', ''], blank))
