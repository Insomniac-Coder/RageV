# -*- coding: utf-8 -*-
"""RT-15, first measurements: what limits the reflection on a moving reflector?

The car driven at the close-up and the chrome cube crossing the owner's shot, each against its
own pose stopped and settled (the drive stopped where a frame has it, frames 150-169), so every
arm is scored on the one thing that matters: how far the moving frame is from what the
reflection should settle to -- noise and lag together, split by region. Arms (staged
accumulators; the harness now finds the bake):

  ship        as shipped (RT-17, RT-18 in)
  noavg       the accumulator averaging nothing (the one-frame reference: no lag, all noise)
  longmem     the memory never under 16 frames where a history was found
  longnobound longmem, and the history never bounded by the fresh neighbourhood
  tightbound  the bound three spreads wide at every roughness (it opens to twelve on a mirror)

Usage: rt15_diag.py [render]
"""
import io, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_14'))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
import stage_run  # noqa: E402
import rt13_stage2 as s2  # noqa: E402
import refl_memory_check  # noqa: E402,F401  -- 'noaverage'

ACC = 'reflection_accumulate.rvshader'
N = '\n'
LONG = (ACC, '\treturn max(memory, fewest);' + N, '\treturn max(memory, max(fewest, 16.0));' + N)
NOBOUND = (ACC, '\t\t\tconst vec3 held = clamp(c.past.rgb, mean - halfWidth, mean + halfWidth);' + N,
           '\t\t\tconst vec3 held = c.past.rgb;' + N)
TIGHT = (ACC, '\t\t\t\t\t\t\t  : mix(12.0, max(u_Reflection.Probe.x, 0.0),' + N,
         '\t\t\t\t\t\t\t  : mix(max(u_Reflection.Probe.x, 0.0), max(u_Reflection.Probe.x, 0.0),' + N)
stage_run.VARIANTS['longmem'] = [LONG]
stage_run.VARIANTS['longnobound'] = [LONG, NOBOUND]
stage_run.VARIANTS['tightbound'] = [TIGHT]
ARMS = ['ship', 'noaverage', 'longmem', 'longnobound', 'tightbound']
FT = 0.0166
CASES = {
    # case: camera, the moving opts, the pose frame, regions
    'car': ('close', lambda stop: dict(BURST_SLIDE='=porsche_992_gt3_r|1.0|%g' % stop), 75,
            {'car body': (380, 760, 250, 1250), 'floor': (760, 860, 0, 1600)}),
    'cube': ('owner', lambda stop: dict(cube=(-9.0, 3.0, stop)), 120,
             {'cube face': (300, 420, 640, 860), 'floor': (560, 860, 0, 1600)}),
}
S = lambda tag, k: os.path.join(stage_run.SHOTS, 'rt5b_%s_%d.png' % (tag, k))
L = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def render():
    for case, (cam, moving, k, _) in CASES.items():
        runs = [('r15_%s_ref' % case, 'ship', [], dict(frames=20, first=150, mean_from=150, **moving(k * FT)))]
        for arm in ARMS:
            runs.append(('r15_%s_%s' % (case, arm), arm, [], dict(frames=9, first=k - 4, **moving(2.0 if case == 'car' else 6.0))))
        s2.arms_at(cam, runs)


def analyse():
    for case, (cam, moving, k, regions) in CASES.items():
        ref = np.load(os.path.join(stage_run.OUT, 'r15_%s_ref.npy' % case))[:860]
        refL = L(ref)
        # The pose the stopped object matches, from the shipped arm.
        errs = {f: np.abs(L(np.asarray(Image.open(S('r15_%s_ship' % case, f)).convert('RGB'), dtype=float)[:860]) - refL).mean()
                for f in range(k - 4, k + 5)}
        best = min(errs, key=errs.get)
        print('%s: the stopped pose matches frame %d' % (case, best))
        for arm in ARMS:
            im = Image.open(S('r15_%s_%s' % (case, arm), best)).convert('RGB')
            a = L(np.asarray(im, dtype=float)[:860])
            g = np.asarray(im.convert('L'), dtype=float)[:860]
            med = np.asarray(im.convert('L').filter(ImageFilter.MedianFilter(3)), dtype=float)[:860]
            cells = []
            for name, (y0, y1, x0, x1) in regions.items():
                e = np.abs(a - refL)[y0:y1, x0:x1]
                grain = (np.abs(g - med)[y0:y1, x0:x1] > 16).mean() * 100
                cells.append('%s: error %.2f (>16 %.1f%%) grain %.1f%%' % (name, e.mean(), (e > 16).mean() * 100, grain))
            print('   %-12s %s' % (arm, '   '.join(cells)))


if __name__ == '__main__':
    if 'render' in sys.argv:
        render()
    analyse()
