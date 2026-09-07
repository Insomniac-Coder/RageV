"""Crops of the same frames from several burst arms, side by side.

    burst_zoom.py poles <tag> [<tag> ...] [--frames=75,100,118] [--out=name]
    burst_zoom.py floor <tag> [<tag> ...] [--frames=100] [--out=name]
    burst_zoom.py diff  <reference> <tag> [<tag> ...] [--frames=75,100,118]

`poles` is a 2x zoom of the left poles of the band burst_compare.py follows
(one row per arm, one column per frame); `floor` the wet floor under them
(one tile per arm per frame); `diff` a heat map of |arm - reference| x 4.
The band moves with the dolly, so a frame's crop is at that frame's band.
Sheets go beside the frames in build/garage_burst.
"""
import os, sys
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
from burst_compare import band_x, SHOTS


def frame(tag, n):
    return Image.open(os.path.join(SHOTS, '%s_%d.png' % (tag, n))).convert('RGB')


def region(im, n, dx, dy, w, h):
    W, H = im.size
    x0 = int(W * band_x(n) / 2000) + dx
    y0 = int(H * 170 / 1230) + dy
    return im.crop((x0, y0, x0 + w, y0 + h))


def sheet(tiles, gap=8):
    tw, th = tiles[0][0].size
    cols = max(len(r) for r in tiles)
    out = Image.new('RGB', ((tw + gap) * cols, (th + gap) * len(tiles)), (70, 70, 70))
    for i, row in enumerate(tiles):
        for j, t in enumerate(row):
            out.paste(t, (j * (tw + gap), i * (th + gap)))
    return out


def main(argv):
    opts = dict(a.lstrip('-').split('=', 1) for a in argv if a.startswith('--'))
    args = [a for a in argv if not a.startswith('--')]
    kind, tags = args[0], args[1:]
    frames = [int(v) for v in opts.get('frames', '75,100,118' if kind != 'floor' else '100').split(',')]
    name = opts.get('out', '%s_%s' % (kind, '_'.join(tags)))
    if kind == 'poles':
        rows = [[region(frame(t, n), n, 20, 40, 260, 330).resize((520, 660), Image.NEAREST)
                 for n in frames] for t in tags]
        out = sheet(rows)
    elif kind == 'floor':
        rows = [[region(frame(t, n), n, -60, 560, 560, 240).resize((1120, 480), Image.NEAREST)
                 for n in frames] for t in tags]
        out = sheet(rows)
    elif kind == 'diff':
        ref, arms = tags[0], tags[1:]
        rows = []
        for n in frames:
            r = np.asarray(region(frame(ref, n), n, 20, 40, 260, 330), dtype=float)
            row = []
            for t in arms:
                a = np.asarray(region(frame(t, n), n, 20, 40, 260, 330), dtype=float)
                d = np.clip(np.abs(a - r).mean(-1) * 4.0, 0, 255).astype(np.uint8)
                row.append(Image.fromarray(d).convert('RGB').resize((520, 660), Image.NEAREST))
                print('frame %d  |%s - %s| = %.2f' % (n, t, ref, np.abs(a - r).mean()))
            rows.append(row)
        out = sheet(rows)
    else:
        raise SystemExit('poles, floor or diff')
    path = os.path.join(SHOTS, name + '.png')
    out.save(path)
    print(out.size, path)


if __name__ == '__main__':
    main(sys.argv[1:])
