# -*- coding: utf-8 -*-
"""RT-24: how many speckles a swing leaves, from the frames spin_measure.py saved.

A speckle the motion made is a pixel in the full-speed frame (<tag>_moving, which crosses the
settled pose) far brighter than both the median of its own 5x5 and the same pixel in the settled
frame (<tag>_settled): more than 32 display levels (a clear dot) or 64 (a white one). Measured
over the floor and the car (rows 380-870). Counting bright-against-neighbourhood alone was tried
first and counted the pooled reflections' real, sharper highlights as speckle.

    speckle_count.py <tag> [<tag> ...]
"""
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'build', 'rt24')
ROWS = slice(380, 870)


def motion_speckles(tag):
    """Dots the motion made: in the full-speed frame, which crosses the settled pose, a pixel far
    above its own 5x5 median AND far above the same pixel settled -- so a real highlight, which
    the settled frame has too, is not counted."""
    mv = Image.open(os.path.join(OUT, tag + '_moving.png')).convert('L')
    st = np.asarray(Image.open(os.path.join(OUT, tag + '_settled.png')).convert('L')).astype(np.int32)[ROWS]
    lum = np.asarray(mv).astype(np.int32)[ROWS]
    med = np.asarray(mv.filter(ImageFilter.MedianFilter(5))).astype(np.int32)[ROWS]
    out = []
    for lift in (32, 64):
        out.append(int(((lum - med > lift) & (lum - st > lift)).sum()))
    return out


def main(tags):
    print('%-16s %20s %20s' % ('run', 'motion dots >32', 'motion dots >64'))
    for tag in tags:
        a, b = motion_speckles(tag)
        print('%-16s %20d %20d' % (tag, a, b))


if __name__ == '__main__':
    main(sys.argv[1:])
