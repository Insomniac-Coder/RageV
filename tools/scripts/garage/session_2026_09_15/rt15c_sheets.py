# -*- coding: utf-8 -*-
"""RT-15c sheets for the owner: plain titles, circles on the place to look, a note under each picture.

  streaks    the bottom of the cube by the car's wing: object check off | on (and B1's rule)
  floor      the floor under the cube and the chrome pipes: B1 (reflections after TAA) | new
  car        the driving car: reflections after TAA | before TAA (the new default)
  still      parked garage and camera dolly flicker: object check off | on

Usage: rt15c_sheets.py [streaks|floor|car|still ...]
"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

S = r'C:\Users\ism19\Code\RageV\build\garage_burst'
OUT = r'C:\Users\ism19\Code\RageV\build\rt15\objectaware'
os.makedirs(OUT, exist_ok=True)
BIG = ImageFont.truetype('arialbd.ttf', 30)
MID = ImageFont.truetype('arialbd.ttf', 24)
TXT = ImageFont.truetype('arial.ttf', 23)
BG, WH, Y, RED, GRN = (18, 18, 18), (235, 235, 235), (255, 220, 0), (255, 95, 95), (120, 230, 120)
LW = np.array([0.2126, 0.7152, 0.0722], np.float32)


def frame(tag, f):
    return Image.open(os.path.join(S, 'rt5b_%s_%d.png' % (tag, f))).convert('RGB')


def arr(tag, f):
    return np.asarray(frame(tag, f), np.float32)


def luma(a):
    return a @ LW


def flicker(tag, frames):
    prev, acc = luma(arr(tag, frames[0])), None
    for f in frames[1:]:
        cur = luma(arr(tag, f))
        d = np.abs(cur - prev)
        acc = d if acc is None else acc + d
        prev = cur
    return acc / (len(frames) - 1)


def boxblur(a, r):
    im = Image.fromarray(np.clip(a * 10.0, 0, 255).astype(np.uint8))
    return np.asarray(im.filter(ImageFilter.BoxBlur(r)), np.float32) / 10.0


def diffimg(a, b, gain=4.0):
    d = np.clip(np.abs(a - b).max(axis=2) * gain, 0, 255).astype(np.uint8)
    return Image.fromarray(d).convert('RGB')


def ellipse(im, box, cx, cy, rx, ry, z, width=4):
    x0, y0 = box[0], box[1]
    ImageDraw.Draw(im).ellipse(((cx - x0 - rx) * z, (cy - y0 - ry) * z, (cx - x0 + rx) * z, (cy - y0 + ry) * z),
                               outline=Y, width=width)


def grid(path, title, subtitle, columns, rows, cell_w, cell_h, notes):
    """columns: [(heading, colour)]; rows: [[(image, label) per column]]; notes: [text per column]."""
    gap, top, head_h, note_h = 18, 96, 40, 90
    W = gap + len(columns) * (cell_w + gap)
    H = top + head_h + len(rows) * (cell_h + 34 + gap) + note_h
    out = Image.new('RGB', (W, H), BG)
    d = ImageDraw.Draw(out)
    d.text((gap, 12), title, font=BIG, fill=WH)
    d.text((gap, 54), subtitle, font=TXT, fill=Y)
    for c, (heading, colour) in enumerate(columns):
        x = gap + c * (cell_w + gap)
        d.text((x, top), heading, font=MID, fill=colour)
        for r, row in enumerate(rows):
            im, label = row[c]
            y = top + head_h + r * (cell_h + 34 + gap)
            d.text((x, y), label, font=TXT, fill=WH)
            out.paste(im, (x, y + 30))
        d.multiline_text((x, H - note_h + 8), notes[c], font=MID, fill=WH, spacing=6)
    out.save(path)
    print(path, out.size)
    return path


def streaks():
    box, z = (630, 330, 810, 450), 2.5
    size = (int((box[2] - box[0]) * z), int((box[3] - box[1]) * z))
    arms = [('r15c_cube_nocheck', 'OBJECT CHECK OFF', RED), ('r15c_cube_new', 'OBJECT CHECK ON (new)', GRN)]
    rows = []
    for f in (114, 120, 126):
        rows.append([(frame(t, f).crop(box).resize(size, Image.NEAREST), 'frame %d' % f) for t, _, _ in arms])
    return grid(os.path.join(OUT, '1_spoiler_streaks.png'),
                'SPOILER STREAKS: bottom of the moving cube where it passes the car\'s wing',
                'Zoomed in, normal brightness, three different frames. Reflections before TAA in both.',
                [(h, c) for _, h, c in arms], rows, size[0], size[1],
                ['Look for vertical bright bars\nat the bottom of the cube.', 'Same place, same frames.'])


def floor():
    boxes = [((600, 300, 920, 700), [(760, 575, 110, 100)], 'FLOOR UNDER THE CUBE'),
             ((880, 240, 1200, 560), [(945, 370, 22, 80), (1050, 370, 22, 80), (1165, 370, 22, 80)], 'CHROME PIPES')]
    arms = [('r15arm_b', 'B1: REFLECTIONS AFTER TAA', RED), ('r15c_cube_new', 'NEW: BEFORE TAA + OBJECT CHECK', GRN)]
    z = 1.4
    rows = []
    for box, circles, name in boxes:
        size = (int((box[2] - box[0]) * z), int((box[3] - box[1]) * z))
        pics, difs = [], []
        for t, _, _ in arms:
            a, b = arr(t, 119), arr(t, 120)
            p = frame(t, 120).crop(box).resize(size, Image.LANCZOS)
            q = diffimg(b[box[1]:box[3], box[0]:box[2]], a[box[1]:box[3], box[0]:box[2]]).resize(size, Image.NEAREST)
            for im in (p, q):
                for cx, cy, rx, ry in circles:
                    ellipse(im, box, cx, cy, rx, ry, z)
            pics.append((p, name + ': the picture'))
            difs.append((q, name + ': what flickers (bright = flicker)'))
        rows += [pics, difs]
    return grid(os.path.join(OUT, '2_floor_and_pipes.png'),
                'SPECKLES UNDER THE CUBE AND FLICKERING PIPES (cube crossing the car, frame 120)',
                'Bottom picture of each pair: what changes between two frames. Bright dots = speckles, bright lines = flicker.',
                [(h, c) for _, h, c in arms], rows, int(320 * z), int(400 * z),
                ['Speckles in the floor circle,\nflicker on the pipes.', 'Compare the same circles.'])


def car():
    a_tag, b_tag = 'r15c_car_after', 'r15c_car_before'
    frames = range(70, 100)
    acc = None
    for f in frames:
        d = np.abs(luma(arr(a_tag, f)) - luma(arr(b_tag, f)))
        acc = d if acc is None else acc + d
    acc = boxblur(acc / len(frames), 24)
    acc[860:] = 0
    y, x = np.unravel_index(np.argmax(acc), acc.shape)
    print('car: after vs before differ most at x=%d y=%d (%.2f levels mean)' % (x, y, acc[y, x]))
    bw, bh, z = 360, 240, 1.6
    x0, y0 = int(np.clip(x - bw // 2, 0, 1600 - bw)), int(np.clip(y - bh // 2, 0, 860 - bh))
    box = (x0, y0, x0 + bw, y0 + bh)
    size = (int(bw * z), int(bh * z))
    k = 85
    pa, pb = arr(a_tag, k), arr(b_tag, k)
    rows = [[(frame(a_tag, k).crop(box).resize(size, Image.LANCZOS), 'the picture, frame %d' % k),
             (frame(b_tag, k).crop(box).resize(size, Image.LANCZOS), 'the picture, frame %d' % k)],
            [(diffimg(pa[y0:y0 + bh, x0:x0 + bw], pb[y0:y0 + bh, x0:x0 + bw]).resize(size, Image.NEAREST),
              'where the two differ (bright = different)'),
             (diffimg(pa[y0:y0 + bh, x0:x0 + bw], pb[y0:y0 + bh, x0:x0 + bw]).resize(size, Image.NEAREST),
              'the same difference')]]
    for row in rows:
        for im, _ in row:
            ellipse(im, box, x, y, 70, 55, z)
    full = (frame(a_tag, k).resize((800, 450)), frame(b_tag, k).resize((800, 450)))
    for im in full:
        ImageDraw.Draw(im).rectangle((x0 / 2, y0 / 2, (x0 + bw) / 2, (y0 + bh) / 2), outline=Y, width=3)
    rows.insert(0, [(full[0].resize(size), 'whole shot (yellow box = the zoom below)'),
                    (full[1].resize(size), 'whole shot (yellow box = the zoom below)')])
    return grid(os.path.join(OUT, '3_car_trail.png'),
                'THE DRIVING CAR: does its reflection trail come back with reflections before TAA?',
                'Same frame in both. The circle is where the two differ most over frames 70-99.',
                [('REFLECTIONS AFTER TAA (B1\'s way)', RED), ('REFLECTIONS BEFORE TAA (new)', GRN)], rows,
                size[0], size[1], ['', ''])


def still():
    out = []
    for case, frames in (('parked', list(range(150, 190))), ('dolly', list(range(60, 120)))):
        off, on = flicker('r15c_%s_nocheck' % case, frames), flicker('r15c_%s_new' % case, frames)
        more = boxblur(on - off, 6)
        more[860:] = 0
        y, x = np.unravel_index(np.argmax(np.abs(more)), more.shape)
        print('%s: flicker per frame, whole frame: check off %.3f, check on %.3f; biggest local change %.2f at x=%d y=%d'
              % (case, off[:860].mean(), on[:860].mean(), more[y, x], x, y))
        size = (800, 450)
        def show(m):
            return Image.fromarray(np.clip(m * 12.0, 0, 255).astype(np.uint8)).convert('RGB').resize(size, Image.LANCZOS)
        a, b = show(off), show(on)
        pa, pb = frame('r15c_%s_nocheck' % case, frames[len(frames) // 2]).resize(size), frame('r15c_%s_new' % case, frames[len(frames) // 2]).resize(size)
        if abs(more[y, x]) > 0.5:
            for im in (a, b, pa, pb):
                ImageDraw.Draw(im).ellipse((x / 2 - 40, y / 2 - 40, x / 2 + 40, y / 2 + 40), outline=Y, width=3)
        name = 'PARKED GARAGE (nothing moves)' if case == 'parked' else 'CAMERA DOLLY (camera moves)'
        out.append(grid(os.path.join(OUT, '4_%s_flicker.png' % case),
                        name + ': does the object check make edges flicker?',
                        'Bottom: how much each pixel flickers from frame to frame, averaged (bright = flickers).',
                        [('OBJECT CHECK OFF', RED), ('OBJECT CHECK ON (new)', GRN)],
                        [[(pa, 'the picture'), (pb, 'the picture')], [(a, 'flicker'), (b, 'flicker')]],
                        size[0], size[1],
                        ['Whole-frame flicker: %.2f' % off[:860].mean(), 'Whole-frame flicker: %.2f' % on[:860].mean()]))
    return out


if __name__ == '__main__':
    todo = sys.argv[1:] or ['streaks', 'floor', 'car', 'still']
    for name in todo:
        globals()[name]()
