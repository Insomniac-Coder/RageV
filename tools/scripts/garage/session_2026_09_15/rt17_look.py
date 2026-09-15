# -*- coding: utf-8 -*-
"""RT-17, first look: does a moving object leave its reflection behind in a still reflector?

The case RT-17 names: a still floor or pole, a still camera, something moving in the
reflection. Every reflector test passes; RT-6.10's hit distance catches a change in how far
the ray went, and not a car crossing at about the same distance. Rendered as shipped (RT-18
in) and with the accumulator averaging nothing (refl_memory_check.py's variant) -- where the
two differ coherently behind the moving object's reflection, the history kept its old image.

  carwide -- the car driven by its root, the owner's shot: the floor under it and the poles
  car     -- the same, the close-up
  cube    -- the chrome cube crossing, the owner's shot: its floor reflection and the poles

Usage: rt17_look.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_14'))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import stage_run  # noqa: E402
import rt13_stage2 as s2  # noqa: E402
import refl_memory_check  # noqa: E402,F401  -- registers the 'noaverage' variant

CASES = {
    'carwide': ('owner', dict(frames=40, first=50, BURST_SLIDE=s2.CAR)),
    'car': ('close', dict(frames=40, first=50, BURST_SLIDE=s2.CAR)),
    'cube': ('owner', dict(frames=40, first=100, cube=(-9.0, 3.0, 6.0))),
}
OUT = os.path.join(stage_run.ROOT, 'build', 'rt17')

if __name__ == '__main__':
    if 'render' in sys.argv:
        for case, (cam, opts) in CASES.items():
            s2.arms_at(cam, [('r17_%s_%s' % (case, arm), variant, [], dict(opts))
                             for arm, variant in (('ship', 'ship'), ('noavg', 'noaverage'))])
    os.makedirs(OUT, exist_ok=True)
    S = lambda tag, k: os.path.join(stage_run.SHOTS, 'rt5b_%s_%d.png' % (tag, k))
    for case, (cam, opts) in CASES.items():
        for k in (opts['first'] + 10, opts['first'] + 25):
            a = np.asarray(Image.open(S('r17_%s_ship' % case, k)).convert('RGB'), dtype=float)[:860]
            b = np.asarray(Image.open(S('r17_%s_noavg' % case, k)).convert('RGB'), dtype=float)[:860]
            # Signed: where the averaged picture is brighter than this frame's rays alone.
            lum = lambda x: 0.2126 * x[..., 0] + 0.7152 * x[..., 1] + 0.0722 * x[..., 2]
            d = lum(a) - lum(b)
            heat = np.zeros_like(a)
            heat[..., 0] = np.clip(d * 6.0, 0, 255)
            heat[..., 2] = np.clip(-d * 6.0, 0, 255)
            s2._sheet(os.path.join(OUT, '%s_%d.png' % (case, k)), [a, b, heat],
                      ['as shipped, frame %d' % k, 'this frame only (no averaging)',
                       'red: averaged brighter, blue: darker (x6)'])
        print(case, 'sheets written')
