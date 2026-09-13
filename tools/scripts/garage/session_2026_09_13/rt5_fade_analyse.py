# -*- coding: utf-8 -*-
"""RT-5 part 3's claimed cost: does a switched-off light leave the picture later?

Reads two lamp-switch bursts written by stage_run (lamps on from the start,
the owner's lights button pressed off at a set time, every lamp Realtime) and
answers in three ways:

  the region   pixels the switch changed, from the settled picture before
               against the settled picture at the very end -- by *change*,
               not by brightness, which is what went wrong on 2026-09-09
  the curve    how much of that change is still to go, frame by frame
  per pixel    the frame each pixel stops being more than 10% from its end

Both arms are measured against their *own* settled end, and the two ends are
compared: an arm that has not finished by the last frame says so here rather
than bending the other arm's curve. Eight-bit and after the tone curve, so the
frame counts compare the two arms and are not either filter's time constant.

Usage: rt5_fade_analyse.py <tagA> <tagB> <first> <last> <switch_seconds>
"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw

S = r'C:\Users\ism19\Code\RageV\build\garage_burst'
O = r'C:\Users\ism19\Code\RageV\build\rt5'
A, B, FIRST, LAST = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
frames = list(range(FIRST, LAST + 1))


def rgb(tag, n):
    return np.asarray(Image.open(os.path.join(S, 'rt5b_%s_%d.png' % (tag, n))).convert('RGB'))


def luma(a):
    a = a.astype(np.float32)
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def box(a, r=2):
    k = 2 * r + 1
    p = np.pad(a, r, mode='edge')
    c = np.zeros((p.shape[0] + 1, p.shape[1] + 1))
    c[1:, 1:] = p.cumsum(0).cumsum(1)
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / float(k * k)


stacks = {t: np.stack([luma(rgb(t, n)) for n in frames]) for t in (A, B)}
a, b = stacks[A], stacks[B]

# The switch: the first frame whose drop in the lamps' own pixels is a step.
lens = (luma(rgb(A, FIRST)) - luma(rgb(A, LAST))) > 60
drops = [float((a[i] - a[i + 1])[lens].mean()) if lens.any() else 0.0 for i in range(len(frames) - 1)]
sw_i = int(np.argmax(drops)) + 1
sw = frames[sw_i]
print('switch: first frame drawn with the lamps off is %d (the lenses drop %.1f levels in one frame)'
      % (sw, drops[sw_i - 1]))

pre = slice(0, sw_i)
end = slice(len(frames) - 30, len(frames))
before_a, before_b = a[pre].mean(0), b[pre].mean(0)
after_a, after_b = a[end].mean(0), b[end].mean(0)
region = box(before_a - after_a) > 3.0
print('region: %.2f%% of the frame changed by more than 3 levels; the drop there averages %.1f levels'
      % (100 * region.mean(), (before_a - after_a)[region].mean()))
half = sw_i // 2
print('settled before?  first half against second half of the frames before, in the region: %s %.3f, %s %.3f'
      % (A, np.abs(a[:half].mean(0) - a[half:sw_i].mean(0))[region].mean(),
         B, np.abs(b[:half].mean(0) - b[half:sw_i].mean(0))[region].mean()))
tail = len(frames) - 60
print('settled at the end?  the last 30 frames against the 30 before them, in the region: %s %.3f, %s %.3f'
      % (A, np.abs(a[tail:tail + 30].mean(0) - a[end].mean(0))[region].mean(),
         B, np.abs(b[tail:tail + 30].mean(0) - b[end].mean(0))[region].mean()))
print('same picture before?  |%s - %s| %.3f levels;  same picture at the end?  %.3f levels'
      % (A, B, np.abs(before_a - before_b)[region].mean(), np.abs(after_a - after_b)[region].mean()))


def remaining(stack, before, after):
    span = (before - after)[region].sum()
    return np.array([(stack[i] - after)[region].sum() / span for i in range(len(frames))])


ra, rb = remaining(a, before_a, after_a), remaining(b, before_b, after_b)
print('\nafter the switch   still to go:  %-12s %-12s' % (A, B))
for d in (0, 1, 3, 5, 10, 20, 30, 45, 60, 90, 120, 180, 240, 300, 360):
    if sw_i + d < len(frames):
        print('  %3d frames (%.2f s)            %6.1f%%       %6.1f%%'
              % (d, d * 0.0166, 100 * ra[sw_i + d], 100 * rb[sw_i + d]))


def first_below(r, level):
    idx = np.where(r[sw_i:] <= level)[0]
    return str(int(idx[0])) if len(idx) else 'not by the end'


print('\nframes until only this much is left:   %s / %s' % (A, B))
for level in (0.5, 0.25, 0.10, 0.05, 0.02):
    print('  %4.0f%%   %s / %s' % (100 * level, first_below(ra, level), first_below(rb, level)))


def settle(stack, before, after):
    drop = before - after
    rem = (stack[sw_i:] - after[None]) / np.maximum(drop[None], 1e-3)
    above = rem > 0.10
    last = np.where(above.any(axis=0), above.shape[0] - np.argmax(above[::-1], axis=0), 0)
    return last[region]


sa, sb = settle(a, before_a, after_a), settle(b, before_b, after_b)
print('\nper pixel, frames until it stays within 10%% of its end: median / 75th / 90th percentile')
for tag, s in ((A, sa), (B, sb)):
    print('  %-12s %d / %d / %d' % (tag, np.median(s), np.percentile(s, 75), np.percentile(s, 90)))

# Pictures: the same instants after the switch, both arms, and the difference.
def label(arr, text):
    im = Image.fromarray(arr.astype(np.uint8)).resize((800, 450), Image.BILINEAR)
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 800, 18), fill=(0, 0, 0))
    d.text((6, 3), text, fill=(255, 255, 255))
    return im


rows = []
for d in (-10, 3, 30, 90, 180):
    n = sw + d
    if n > LAST:
        continue
    fa, fb = rgb(A, n).astype(np.int16), rgb(B, n).astype(np.int16)
    diff = (fb - fa).mean(axis=2)
    dm = np.zeros(diff.shape + (3,))
    dm[..., 0] = np.clip(-diff * 8, 0, 255)
    dm[..., 1] = np.clip(diff * 8, 0, 255)
    when = 'before the switch' if d < 0 else '%d frames (%.2f s) after the switch' % (d, d * 0.0166)
    rows.append([label(fa, '%s - %s' % (A, when)), label(fb, '%s - %s' % (B, when)),
                 label(dm, '%s minus %s, x8 (green: %s brighter)' % (B, A, B))])
sheet = Image.new('RGB', (2400, 450 * len(rows)))
for r, row in enumerate(rows):
    for c, im in enumerate(row):
        sheet.paste(im, (c * 800, r * 450))
path = os.path.join(O, 'fade_%s_vs_%s.png' % (A, B))
sheet.save(path)
print('\nwrote', path)
