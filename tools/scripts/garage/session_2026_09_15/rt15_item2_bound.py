# -*- coding: utf-8 -*-
"""RT-15 item 2: a displaced history held to this frame's neighbourhood.

rt15_item2_rules.py found following the struck point's travel smears a glossy floor: the
history the displaced lookup reads is another floor texel's, with other wet and dry detail
and another piece of the still background, and the bound on a glossy surface opens to twelve
spreads (so tube lines on paint are not pulled down) -- nothing refuses the mismatch. Here a
texel whose lookup was displaced takes the rough surface's bound (Probe.x, three spreads)
instead, under each rule:

  asbuilt_t  the mean blend        own_t  the own ray
  major5_t   5 of 9 movers         major7_t  7 of 9 movers

Usage: rt15_item2_bound.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
import rt15_items as it  # noqa: E402
import rt15_item2_rules as ru  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
ACC = it.ACC
N = '\n'
TIGHT = [
    (ACC, 'bool g_MovingCurved = false;' + N, 'bool g_MovingCurved = false;' + N + 'bool g_Displaced = false;' + N),
    (ACC, '\t\tvec3 imagePoint = P + sight * image - MirrorVector(seenTravel, N);' + N,
          '\t\tvec3 imagePoint = P + sight * image - MirrorVector(seenTravel, N);' + N
          + '\t\tg_Displaced = dot(seenTravel, seenTravel) > 0.0;' + N),
    (ACC, '\t\t\t\tc = atSurface;' + N + '\t\t\t\tchoice = 0.5;' + N,
          '\t\t\t\tc = atSurface;' + N + '\t\t\t\tchoice = 0.5;' + N + '\t\t\t\tg_Displaced = false;' + N),
    (ACC, '\t\t\tc = atSurface;' + N + '\t\t\thave = true;' + N,
          '\t\t\tc = atSurface;' + N + '\t\t\thave = true;' + N + '\t\t\tg_Displaced = false;' + N),
    (ACC, '\t\t\tconst float width = PositionLane()' + N,
          '\t\t\tconst float width = (PositionLane() || g_Displaced)' + N),
]
stage_run.VARIANTS['asbuilt_t'] = list(TIGHT)
for rule in ('own', 'major5', 'major7'):
    stage_run.VARIANTS[rule + '_t'] = stage_run.VARIANTS['tr_' + rule] + TIGHT
RULES = [('i1', None), ('asbuilt', None), ('major7', None), ('asbuilt_t', 'asbuilt_t'), ('own_t', 'own_t'),
         ('major5_t', 'major5_t'), ('major7_t', 'major7_t')]


def tag(case, rule):
    if rule == 'i1':
        return it.tag_of(case, 'i1')
    if rule == 'asbuilt':
        return it.tag_of(case, 'i12')
    if rule == 'major7':
        return ('r15r_sphere_major7') if case == 'sphere' else 'rt5b_r15r_%s_major7' % case
    return ('r15b_sphere_%s' % rule) if case == 'sphere' else 'rt5b_r15b_%s_%s' % (case, rule)


if __name__ == '__main__':
    if 'render' in sys.argv:
        for case, c in it.CASES.items():
            arms = [(rule, variant) for rule, variant in RULES if variant]
            if case == 'sphere':
                for rule, variant in arms:
                    it.sphere_burst('r15b_sphere_%s' % rule, variant, it.NOBLUR, c['k'] - 4, 9)
            else:
                moving = dict(BURST_SLIDE='=porsche_992_gt3_r|1.0|2.0') if case == 'car' else dict(cube=(-9.0, 3.0, 6.0))
                s2.arms_at(c['cam'], [('r15b_%s_%s' % (case, rule), variant, it.NOBLUR,
                                       dict(frames=9, first=c['k'] - 4, **moving)) for rule, variant in arms])
    for case, c in it.CASES.items():
        refL = it.L(c['ref']())
        best = {'car': 76, 'cube': 123, 'sphere': 121}[case]
        print(case, 'frame', best)
        for rule, _ in RULES:
            im = Image.open(it.S(tag(case, rule), best)).convert('RGB')
            a = np.asarray(im, dtype=float)[:860]
            g = np.asarray(im.convert('L'), dtype=float)[:860]
            med = np.asarray(im.convert('L').filter(ImageFilter.MedianFilter(3)), dtype=float)[:860]
            cells = []
            for name, (y0, y1, x0, x1) in c['regions'].items():
                e = np.abs(it.L(a) - refL)[y0:y1, x0:x1]
                cells.append('%s: error %.2f (>16 %.1f%%) grain %.1f%%' % (
                    name, e.mean(), (e > 16).mean() * 100, (np.abs(g - med)[y0:y1, x0:x1] > 16).mean() * 100))
            print('   %-9s %s' % (rule, '   '.join(cells)))
