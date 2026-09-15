# -*- coding: utf-8 -*-
"""RT-15 item 2: both image histories -- the one following the struck point, the one standing still.

A glossy floor's picture at a texel is a lobe: a moving object where rays strike it, and the
still room around. Followed by the mover's travel (rt15_item2_rules.py), the still part is
smeared along the motion over the whole memory -- the cube's floor went from 2.27 to 9.48
against its settled picture, worse than no averaging (7.79). Held still, the mover's part lags.
Item 1 met the same kind of question with a per-pixel choice, so here: where a lookup was
displaced, the undisplaced image history is fetched too, and the displaced one is kept only
where its picture sits nearer this frame's 3x3 mean (in the neighbourhood's spreads) --

  dual07   clearly nearer (0.7 of the still one's distance), mean-blend travel
  dual10   nearer at all, mean-blend travel
  dual07m5 clearly nearer, the 5-of-9 majority travel

Usage: rt15_item2_dual.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
import rt15_items as it  # noqa: E402
import rt15_item2_rules as ru  # noqa: E402
import rt15_item2_bound as bd  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
ACC = it.ACC
N = '\n'
OLD = ('\t\tCandidate c;' + N
       + '\t\tbool have = HistoryAt(imagePoint, size, P, N, roughness, eyeDistance,' + N
       + '\t\t                      silhouette, vec2(0.0), c);' + N)


def dual(margin):
    return (ACC, OLD, OLD
            + '\t\tif (dot(seenTravel, seenTravel) > 0.0)' + N
            + '\t\t{' + N
            + '\t\t\tconst vec3 stillPoint = (haveSurface && dot(objectShift, objectShift) > 0.0 && atSurface.bilinear)' + N
            + '\t\t\t                      ? ImageThen(P, N, sight, image, atSurface, vec3(0.0))' + N
            + '\t\t\t                      : P + sight * image;' + N
            + '\t\t\tCandidate still;' + N
            + '\t\t\tif (HistoryAt(stillPoint, size, P, N, roughness, eyeDistance, silhouette, vec2(0.0), still))' + N
            + '\t\t\t{' + N
            + '\t\t\t\tif (!have)' + N
            + '\t\t\t\t{' + N
            + '\t\t\t\t\tc = still;' + N
            + '\t\t\t\t\thave = true;' + N
            + '\t\t\t\t}' + N
            + '\t\t\t\telse' + N
            + '\t\t\t\t{' + N
            + '\t\t\t\t\tvec3 nm, ns;' + N
            + '\t\t\t\t\tNeighbourhood(texel, size, nm, ns);' + N
            + '\t\t\t\t\tconst float sp = max(Luma(ns), 1.0e-4);' + N
            + '\t\t\t\t\tconst float byMoved = abs(Luma(c.past.rgb) - Luma(nm)) / sp;' + N
            + '\t\t\t\t\tconst float byStill = abs(Luma(still.past.rgb) - Luma(nm)) / sp;' + N
            + '\t\t\t\t\tif (!(byMoved < %.2f * byStill))' % margin + N
            + '\t\t\t\t\t\tc = still;' + N
            + '\t\t\t\t}' + N
            + '\t\t\t}' + N
            + '\t\t}' + N)


stage_run.VARIANTS['dual07'] = [dual(0.7)]
stage_run.VARIANTS['dual10'] = [dual(1.0)]
stage_run.VARIANTS['dual07m5'] = stage_run.VARIANTS['tr_major5'] + [dual(0.7)]
RULES = [('i1', None), ('asbuilt', None), ('major7', None), ('dual07', 'dual07'), ('dual10', 'dual10'),
         ('dual07m5', 'dual07m5')]


def tag(case, rule):
    if rule in ('i1', 'asbuilt', 'major7'):
        return bd.tag(case, rule)
    return ('r15d_sphere_%s' % rule) if case == 'sphere' else 'rt5b_r15d_%s_%s' % (case, rule)


def regions_extra(case):
    # The cube's floor reflection block, from the travel probe (rows 547-655, columns 524-791).
    return {'cube block': (547, 656, 524, 792)} if case == 'cube' else {}


if __name__ == '__main__':
    if 'render' in sys.argv:
        for case, c in it.CASES.items():
            arms = [(rule, variant) for rule, variant in RULES if variant]
            if case == 'sphere':
                for rule, variant in arms:
                    it.sphere_burst('r15d_sphere_%s' % rule, variant, it.NOBLUR, c['k'] - 4, 9)
            else:
                moving = dict(BURST_SLIDE='=porsche_992_gt3_r|1.0|2.0') if case == 'car' else dict(cube=(-9.0, 3.0, 6.0))
                s2.arms_at(c['cam'], [('r15d_%s_%s' % (case, rule), variant, it.NOBLUR,
                                       dict(frames=9, first=c['k'] - 4, **moving)) for rule, variant in arms])
    for case, c in it.CASES.items():
        refL = it.L(c['ref']())
        best = {'car': 76, 'cube': 123, 'sphere': 121}[case]
        print(case, 'frame', best)
        regions = dict(c['regions'], **regions_extra(case))
        for rule, _ in RULES:
            im = Image.open(it.S(tag(case, rule), best)).convert('RGB')
            a = np.asarray(im, dtype=float)[:860]
            g = np.asarray(im.convert('L'), dtype=float)[:860]
            med = np.asarray(im.convert('L').filter(ImageFilter.MedianFilter(3)), dtype=float)[:860]
            cells = []
            for name, (y0, y1, x0, x1) in regions.items():
                e = np.abs(it.L(a) - refL)[y0:y1, x0:x1]
                cells.append('%s: %.2f (>16 %.1f%%) grain %.1f%%' % (
                    name, e.mean(), (e > 16).mean() * 100, (np.abs(g - med)[y0:y1, x0:x1] > 16).mean() * 100))
            print('   %-9s %s' % (rule, '   '.join(cells)))
