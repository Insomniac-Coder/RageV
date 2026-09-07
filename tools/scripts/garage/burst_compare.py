"""Three bursts of the same dolly, compared frame by frame.

    burst_compare.py <reference tag> <tag> [<tag> ...] [--stop-frame=120]

The reference is the in-line form (`--reflection-pass=off`): one exact
mirror ray per pixel on a mirror, no history, so on the chrome poles it is
the right answer for every frame of the motion, and the distance from it is
what the accumulator's history costs. Rougher surfaces in the band are grain
in the reference, the same grain for every arm, so the arms compare to each
other through it.

Per arm:
  motion  -- mean |arm - reference| over the pole band while the dolly moves
  still   -- the same once it has stopped, from thirty frames after
  settle  -- frames after the stop until |frame - last| falls under 0.5
             levels in the band (the owner's "takes two seconds")
  peak    -- the worst single frame's distance from the reference

And a strip of crops, mid-motion and just after the stop, one row per arm,
written beside the frames as compare_<tag>.png.
"""
import os, re, sys, glob
import numpy as np
from PIL import Image

SHOTS = r'C:\Users\ism19\Code\RageV\build\garage_burst'


def frames_of(tag):
    got = sorted(glob.glob(os.path.join(SHOTS, tag + '_*.png')),
                 key=lambda f: int(re.search(r'_(\d+)\.png$', f).group(1)))
    return {int(re.search(r'_(\d+)\.png$', f).group(1)): f for f in got}


def luma(path):
    a = np.asarray(Image.open(path).convert('RGB'), dtype=float)
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


# The dolly walks the poles across the frame: at 1.5 m/s from frame 0 to 120
# the group right of the car goes from x 1150 to 720 (of 2000), so the band
# follows them, linearly in the camera's position, and stays put after.
BAND = dict(x_start=1150, x_stop=720, width=620, frame_start=30, frame_stop=120,
            y0=170, y1=760)


def band_x(frame):
    t = min(max((frame - BAND['frame_start'])
                / float(BAND['frame_stop'] - BAND['frame_start']), 0.0), 1.0)
    return BAND['x_start'] + (BAND['x_stop'] - BAND['x_start']) * t


def band_of(shape, frame):
    h, w = shape
    x0 = int(w * band_x(frame) / 2000)
    x1 = x0 + int(w * BAND['width'] / 2000)
    y0, y1 = int(h * BAND['y0'] / 1230), int(h * BAND['y1'] / 1230)
    return (slice(y0, y1), slice(x0, x1))


def crop_of(im, frame):
    w, h = im.size
    box = band_of((h, w), frame)
    return im.crop((box[1].start, box[0].start, box[1].stop, box[0].stop))


def main(argv):
    opts = dict(a.lstrip('-').split('=', 1) for a in argv if a.startswith('--'))
    tags = [a for a in argv if not a.startswith('--')]
    stop = int(opts.get('stop-frame', 120))
    ref_tag, arms = tags[0], tags[1:]
    ref = frames_of(ref_tag)
    numbers = sorted(ref)
    shape = luma(ref[numbers[0]]).shape
    refL = {n: luma(ref[n])[band_of(shape, n)] for n in numbers}
    print('%-14s %8s %8s %8s %8s' % ('arm', 'motion', 'still', 'settle', 'peak'))
    strips = []
    for tag in arms:
        arm = frames_of(tag)
        common = [n for n in numbers if n in arm]
        L = {n: luma(arm[n])[band_of(shape, n)] for n in common}
        d = {n: float(np.mean(np.abs(L[n] - refL[n]))) for n in common}
        moving = [d[n] for n in common if n < stop - 2]
        still = [d[n] for n in common if n >= stop + 30]
        last = L[common[-1]]   # the band is still from frame_stop on, so these line up
        settle = None
        for n in common:
            if n < stop:
                continue
            if float(np.mean(np.abs(L[n] - last))) < 0.5:
                settle = n - stop
                break
        print('%-14s %8.2f %8.2f %8s %8.2f' % (
            tag, np.mean(moving), np.mean(still),
            '%d' % settle if settle is not None else '>%d' % (common[-1] - stop),
            max(d.values())))
        # The per-frame trace, for the plot in the handoff.
        with open(os.path.join(SHOTS, 'trace_%s.txt' % tag), 'w') as f:
            for n in common:
                f.write('%d %.3f %.3f\n' % (n, d[n], float(np.mean(np.abs(L[n] - last)))))
        mid = common[len([n for n in common if n < stop]) // 2]
        after = min((n for n in common if n >= stop + 3), default=common[-1])
        row = []
        for n in (mid, after):
            row.append(crop_of(Image.open(arm[n]).convert('RGB'), n))
        strips.append((tag, row))
    ref_mid = common[len([n for n in common if n < stop]) // 2]
    ref_after = min((n for n in common if n >= stop + 3), default=common[-1])
    row = []
    for n in (ref_mid, ref_after):
        row.append(crop_of(Image.open(ref[n]).convert('RGB'), n))
    strips.append((ref_tag, row))
    cw, ch = strips[0][1][0].size
    sheet = Image.new('RGB', (cw * 2 + 10, (ch + 10) * len(strips)), (40, 40, 40))
    for i, (tag, row) in enumerate(strips):
        for j, crop in enumerate(row):
            sheet.paste(crop, (j * (cw + 10), i * (ch + 10)))
    out = os.path.join(SHOTS, 'compare_' + '_'.join(arms) + '.png')
    sheet.save(out)
    print('crops (mid-motion frame %d, after the stop frame %d), one row per arm then the reference: %s'
          % (ref_mid, ref_after, out))


if __name__ == '__main__':
    main(sys.argv[1:])
