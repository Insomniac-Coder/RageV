# -*- coding: utf-8 -*-
"""RT-18 / RT-17, first look: where the moving cube's and the driving car's reflections break.

Two moving cases, each rendered as shipped and through the reflection debug views
that say what the accumulator did with every texel's history:
  cube  -- stage_run's chrome cube crossing the garage at 3 m/s (RT-20's case), the owner's shot
  car   -- the car driven at 1 m/s by its root, the close-up
and one arm with the reflection history off, the raw frame the history is averaging.

Usage: rt18_diag.py [render] [sheets]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_14'))
import stage_run  # noqa: E402
import rt13_stage2 as s2  # noqa: E402

OUT = os.path.join(stage_run.ROOT, 'build', 'rt18')
VIEWS = [('ship', []), ('refusal', ['--debug-view=reflection-refusal']),
         ('choice', ['--debug-view=reflection-choice']), ('picture', ['--debug-view=reflection-picture']),
         ('taarefusal', ['--debug-view=taa-refusal']), ('nohist', ['--reflection-history=off'])]
CASES = {
    'cube': ('owner', dict(frames=40, first=100, cube=(-9.0, 3.0, 6.0))),
    'car': ('close', dict(frames=20, first=55, BURST_SLIDE=s2.CAR)),
}


def render():
    for case, (cam, opts) in CASES.items():
        s2.arms_at(cam, [('d18_%s_%s' % (case, view), 'ship', flags, dict(opts)) for view, flags in VIEWS])


def sheets():
    import numpy as np
    from PIL import Image
    os.makedirs(OUT, exist_ok=True)
    for case, (cam, opts) in CASES.items():
        k = opts['first'] + opts['frames'] // 2
        tiles = []
        for view, _ in VIEWS:
            tiles.append(np.asarray(Image.open(os.path.join(stage_run.SHOTS, 'rt5b_d18_%s_%s_%d.png' % (case, view, k))).convert('RGB'))[:860])
        s2._sheet(os.path.join(OUT, '%s_%d_views.png' % (case, k)), tiles, [v for v, _ in VIEWS])
        # Four consecutive shipped frames and four raw ones, full size, for the eye.
        for view in ('ship', 'nohist'):
            fr = [np.asarray(Image.open(os.path.join(stage_run.SHOTS, 'rt5b_d18_%s_%s_%d.png' % (case, view, k + i))).convert('RGB'))[:860] for i in range(4)]
            top = np.concatenate(fr[:2], axis=1)
            bot = np.concatenate(fr[2:], axis=1)
            Image.fromarray(np.concatenate([top, bot], axis=0)).resize((1600, 860)).save(os.path.join(OUT, '%s_%s_%d_4frames.png' % (case, view, k)))
        print(case, 'sheets at frame', k)


if __name__ == '__main__':
    if 'render' in sys.argv:
        render()
    if 'sheets' in sys.argv:
        sheets()
