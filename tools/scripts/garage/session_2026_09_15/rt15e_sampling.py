# -*- coding: utf-8 -*-
"""RT-15e: the moving 0.12 cube's grid of bands is the reflection rays' sampling pattern.

reflection_trace's GlossyReflectionLD draws each ray from Halton(frame) shifted by a per-texel
rotation. That rotation is `fract(vec2(texel) * vec2(a1, a2))`: its x depends on the column alone
and its y on the row alone. x chooses how far the ray tilts from the mirror direction, y which
way. So in any frame a whole column tilts by the same amount and a whole row turns the same way.
A stopped texel averages all 64 frames of the sequence and the pattern cancels; a moving cube's
texels have histories a few frames to a few tens of frames old, so the partial sums keep a
column-coherent and a row-coherent bias -- vertical bands and horizontal lines. A sphere's normal
turns every pixel, which scrambles it; a flat face shows it whole.

Variants of that one line (everything else as built):
  rothash   a hashed rotation per texel: no stratification between neighbours
  rotr2     the R2 sequence indexed by the texel (x + 1601 y), in fixed point: neighbours
            still take well-spread offsets, and neither component follows a row or a column

Arms: the cube crossing the car (frames 100-121), the cube stopped where frame 120 puts it
(frames 150-189), the parked garage (frames 150-169). TAA, the scene's roughness 0.12.

Usage: rt15e_sampling.py render [variant ...] | measure
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import rt15_items as it  # noqa: E402
import rt15d_bands as bands  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
TRACE = 'reflection_trace.rvshader'
OLD = 'vec2(texel) * vec2(0.7548776662, 0.5698402910)'
stage_run.VARIANTS['rothash'] = [(TRACE, OLD,
    'vec2(float(BudgetHash(uint(texel.x) ^ (uint(texel.y) << 16u)) & 0xFFFFu), '
    'float(BudgetHash((uint(texel.y) * 7919u) ^ uint(texel.x) ^ 0x9E3779B9u) & 0xFFFFu)) / 65536.0')]
stage_run.VARIANTS['rotr2'] = [(TRACE, OLD,
    'vec2(float(((uint(texel.x) + uint(texel.y) * 1601u) * 3242174889u) >> 8u), '
    'float(((uint(texel.x) + uint(texel.y) * 1601u) * 2447445413u) >> 8u)) / 16777216.0')]
FT = 0.0166


def arms(variant):
    return [('r15e_%s_move' % variant, variant, [], dict(frames=22, first=100, cube=(-9.0, 3.0, 6.0))),
            ('r15e_%s_stop' % variant, variant, [], dict(frames=40, first=150, cube=(-9.0, 3.0, 120 * FT))),
            ('r15e_%s_parked' % variant, variant, [], dict(frames=20, first=150))]


if __name__ == '__main__':
    if sys.argv[1] == 'render':
        try:
            for v in (sys.argv[2:] or ['rothash', 'rotr2']):
                s2.arms_at('owner', arms(v))
        finally:
            print('restored:', stage_run.restore())
    else:
        bands.measure(['r15c_cube_new', 'r15e_rothash_move', 'r15e_rotr2_move'])
