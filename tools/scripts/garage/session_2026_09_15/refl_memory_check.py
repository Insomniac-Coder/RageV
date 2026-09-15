# -*- coding: utf-8 -*-
"""Is the reflection memory calming the picture? (owner, 2026-09-15: "so much flicker and
jitter ... seems like reflection is just not implemented the right way")

A clean A/B: the accumulator as shipped, and the same accumulator averaging nothing --
the picture it writes is this frame's resolved rays (`kept = fresh.rgb`), while the frame
count, and so the lit shader's trust in the picture, is left exactly as shipped. The only
difference between the arms is the averaging. Measured as frame-to-frame change of the
final picture: parked garage (owner's shot), the chrome cube crossing, the car driving.

Usage: refl_memory_check.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_14'))
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import stage_run  # noqa: E402
import rt13_stage2 as s2  # noqa: E402

ACC = 'reflection_accumulate.rvshader'
WRITE = '\to_Accumulated = vec4(StoreAsHalf(kept, HalfRoundingDraw(roundingPixel, roundingFrame, 0u)),\n'
stage_run.VARIANTS['noaverage'] = [(ACC, WRITE, '\tkept = fresh.rgb;\n' + WRITE)]
CASES = {
    'parked': ('owner', dict(frames=40, first=150)),
    'cube': ('owner', dict(frames=40, first=100, cube=(-9.0, 3.0, 6.0))),
    'car': ('close', dict(frames=20, first=55, BURST_SLIDE=s2.CAR)),
}

if __name__ == '__main__' and 'render' in sys.argv:
    for case, (cam, opts) in CASES.items():
        s2.arms_at(cam, [('mem_%s_%s' % (case, arm), variant, [], dict(opts))
                         for arm, variant in (('ship', 'ship'), ('noavg', 'noaverage'))])

S = os.path.join(stage_run.SHOTS, 'rt5b_mem_%s_%s_%d.png')
for case, (cam, opts) in (CASES.items() if __name__ == '__main__' else []):
    rows = {}
    for arm in ('ship', 'noavg'):
        fr = [np.asarray(Image.open(S % (case, arm, k)).convert('L'), dtype=float)[:860]
              for k in range(opts['first'], opts['first'] + opts['frames'])]
        d = [np.abs(fr[i] - fr[i - 1]) for i in range(1, len(fr))]
        rows[arm] = (np.mean([x.mean() for x in d]), np.mean([(x > 8).mean() for x in d]) * 100,
                     np.mean([x[430:860].mean() for x in d]))
    print('%-6s frame-to-frame change   with memory: %.2f levels (%.2f%% px > 8, lower half %.2f)'
          '   averaging nothing: %.2f (%.2f%%, %.2f)' % ((case,) + rows['ship'] + rows['noavg']))
