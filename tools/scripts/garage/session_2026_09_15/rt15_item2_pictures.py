# -*- coding: utf-8 -*-
"""RT-15 item 2, the two options as pictures for the owner (2026-09-15: "show me some pictures").

  B  following the reflected object off (items 1 and 3 on, denoise 12)
  A  following on in its best form: only where most of the 3x3's rays struck the moving object,
     and only where the followed old picture matches this frame better than the unfollowed one

Three places: the floor under the driving car (close-up), the floor under the chrome cube and
under the chrome sphere (owner's shot). For each: a still sheet at the pose that has a settled
reference -- B, A, settled, and each option's difference from settled (brighter is further from
right) -- and a clip of 24 frames with B and A side by side.

Usage: rt15_item2_pictures.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFilter  # noqa: E402
import rt15_items as it  # noqa: E402
import rt15_item2_dual as du  # noqa: E402  -- registers dual07m5

stage_run, s2 = it.stage_run, it.s2
stage_run.VARIANTS['optA'] = stage_run.VARIANTS['dual07m5']
stage_run.VARIANTS['optB'] = stage_run.VARIANTS['i2off']
CASES = {
    'car': dict(first=60, pose=76, boost=1.8),
    'cube': dict(first=108, pose=123, boost=2.4),
    'sphere': dict(first=108, pose=121, boost=2.4),
}
COUNT = 24


def tag(case, opt):
    return ('r15p2_sphere_%s' % opt) if case == 'sphere' else 'rt5b_r15p2_%s_%s' % (case, opt)


def load(case, opt, f):
    return np.asarray(Image.open(it.S(tag(case, opt), f)).convert('RGB'), dtype=float)[:860]


def crop_box(case):
    """Where the two options differ most at the pose frame, padded to a 16:9 box."""
    c = CASES[case]
    d = np.abs(it.L(load(case, 'A', c['pose'])) - it.L(load(case, 'B', c['pose'])))
    d = np.asarray(Image.fromarray(np.clip(d * 10, 0, 255).astype(np.uint8)).filter(ImageFilter.BoxBlur(12)), dtype=float)
    y, x = np.unravel_index(np.argmax(d), d.shape)
    h, w = 180, 320
    y0 = int(np.clip(y - h // 2, 0, 860 - h))
    x0 = int(np.clip(x - w // 2, 0, 1600 - w))
    return y0, y0 + h, x0, x0 + w


def label(img, text):
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, img.size[0], 18), fill=(0, 0, 0))
    d.text((5, 3), text, fill=(255, 255, 255))
    return img


def tile(a, box, boost, scale):
    y0, y1, x0, x1 = box
    t = Image.fromarray(np.clip(a[y0:y1, x0:x1] * boost, 0, 255).astype(np.uint8))
    return t.resize((t.size[0] * scale, t.size[1] * scale), Image.NEAREST)


def heat(a, ref, box, scale):
    y0, y1, x0, x1 = box
    e = np.abs(it.L(a) - it.L(ref))[y0:y1, x0:x1]
    t = Image.fromarray(np.clip(e * 6, 0, 255).astype(np.uint8)).convert('RGB')
    return t.resize((t.size[0] * scale, t.size[1] * scale), Image.NEAREST)


if __name__ == '__main__':
    if 'render' in sys.argv:
        for case, c in CASES.items():
            if case == 'sphere':
                for opt in ('B', 'A'):
                    it.sphere_burst('r15p2_sphere_%s' % opt, 'opt' + opt, [], c['first'], COUNT)
            else:
                moving = dict(BURST_SLIDE='=porsche_992_gt3_r|1.0|2.0') if case == 'car' else dict(cube=(-9.0, 3.0, 6.0))
                cam = 'close' if case == 'car' else 'owner'
                s2.arms_at(cam, [('r15p2_%s_%s' % (case, opt), 'opt' + opt, [], dict(frames=COUNT, first=c['first'], **moving))
                                 for opt in ('B', 'A')])
    for case, c in CASES.items():
        box = crop_box(case)
        ref = it.CASES[case]['ref']()
        b, a = load(case, 'B', c['pose']), load(case, 'A', c['pose'])
        scale = 3
        panels = [
            [label(tile(b, box, c['boost'], scale), 'B: following off'),
             label(tile(a, box, c['boost'], scale), 'A: following on (best version)'),
             label(tile(ref, box, c['boost'], scale), 'what it should look like (stopped, settled)')],
            [label(heat(b, ref, box, scale), 'B: wrongness (brighter = more wrong)'),
             label(heat(a, ref, box, scale), 'A: wrongness (brighter = more wrong)'),
             label(Image.new('RGB', (320 * scale, 180 * scale)), '')],
        ]
        W, H = 320 * scale, 180 * scale
        sheet = Image.new('RGB', (W * 3 + 16, H * 2 + 8), (40, 40, 40))
        for r, row in enumerate(panels):
            for col, im in enumerate(row):
                sheet.paste(im, (col * (W + 8), r * (H + 8)))
        sheet.save(os.path.join(it.OUT, 'item2_%s_still.png' % case))
        frames = []
        for f in range(c['first'], c['first'] + COUNT):
            left = label(tile(load(case, 'B', f), box, c['boost'], 2), 'B: following off   frame %d' % f)
            right = label(tile(load(case, 'A', f), box, c['boost'], 2), 'A: following on')
            both = Image.new('RGB', (left.size[0] * 2 + 8, left.size[1]), (40, 40, 40))
            both.paste(left, (0, 0))
            both.paste(right, (left.size[0] + 8, 0))
            frames.append(both.convert('P', palette=Image.ADAPTIVE, colors=255))
        frames[0].save(os.path.join(it.OUT, 'item2_%s.gif' % case), save_all=True, append_images=frames[1:],
                       duration=120, loop=0)
        print('%s: crop rows %d-%d, columns %d-%d' % ((case,) + box))
