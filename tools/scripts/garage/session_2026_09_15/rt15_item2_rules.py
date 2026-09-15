# -*- coding: utf-8 -*-
"""RT-15 item 2: when is a texel's reflection the moving object, so its history should follow it?

As built, the travel is the texel's own ray's on a mirror and blends to the 3x3's mean (the
still hits counting zero) by roughness 0.3. The travel probe showed the cost: on the glossy
floor (roughness 0.05-0.25), single rays of a lobe land on the moving cube far outside its
reflection, and every such texel's lookup moved a fraction of a texel -- the wrong place for
the still picture it mostly shows, and motion enough for the smear cap to cut its memory.

Rules (the struck point's travel a texel's history follows):
  asbuilt  the mean blend above
  own      the texel's own ray, whatever the roughness
  major5   the mean of the movers' travel where at least 5 of the 3x3's rays struck a mover
  major7   the same at 7 of 9
  ownmaj5  major5, and the texel's own ray struck a mover too

Scored as rt15_items.py scores, against item 1 alone (item 2 off), blur off throughout.

Usage: rt15_item2_rules.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
import rt15_items as it  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
ACC = it.ACC
N = '\n'
BODY = ('\tconst float spread = smoothstep(0.05, 0.3, roughness);' + N
        + '\tif (spread <= 0.0)' + N
        + '\t\treturn own;' + N
        + '\tvec3 sum = vec3(0.0);' + N
        + '\tfor (int y = -1; y <= 1; ++y)' + N
        + '\t{' + N
        + '\t\tfor (int x = -1; x <= 1; ++x)' + N
        + '\t\t\tsum += texelFetch(u_FreshTravel, clamp(texel + ivec2(x, y), ivec2(0), size - 1), 0).rgb;' + N
        + '\t}' + N
        + '\treturn mix(own, sum / 9.0, spread);' + N)


def majority(count, need_own):
    return ('\tvec3 sum = vec3(0.0);' + N
            + '\tfloat movers = 0.0;' + N
            + '\tfor (int y = -1; y <= 1; ++y)' + N
            + '\t{' + N
            + '\t\tfor (int x = -1; x <= 1; ++x)' + N
            + '\t\t{' + N
            + '\t\t\tconst vec3 t = texelFetch(u_FreshTravel, clamp(texel + ivec2(x, y), ivec2(0), size - 1), 0).rgb;' + N
            + '\t\t\tif (dot(t, t) > 0.0)' + N
            + '\t\t\t{' + N
            + '\t\t\t\tsum += t;' + N
            + '\t\t\t\tmovers += 1.0;' + N
            + '\t\t\t}' + N
            + '\t\t}' + N
            + '\t}' + N
            + ('\tif (dot(own, own) <= 0.0)' + N + '\t\treturn vec3(0.0);' + N if need_own else '')
            + '\treturn movers >= %d.0 ? sum / movers : vec3(0.0);' % count + N)


stage_run.VARIANTS['tr_own'] = [(ACC, BODY, '\treturn own;' + N)]
stage_run.VARIANTS['tr_major5'] = [(ACC, BODY, majority(5, False))]
stage_run.VARIANTS['tr_major7'] = [(ACC, BODY, majority(7, False))]
stage_run.VARIANTS['tr_ownmaj5'] = [(ACC, BODY, majority(5, True))]
RULES = [('i1', 'i2off'), ('asbuilt', 'ship'), ('own', 'tr_own'), ('major5', 'tr_major5'),
         ('major7', 'tr_major7'), ('ownmaj5', 'tr_ownmaj5')]

if __name__ == '__main__':
    if 'render' in sys.argv:
        for case, c in it.CASES.items():
            arms = [(rule, variant) for rule, variant in RULES if rule not in ('i1', 'asbuilt')]
            if case == 'sphere':
                for rule, variant in arms:
                    it.sphere_burst('r15r_sphere_%s' % rule, variant, it.NOBLUR, c['k'] - 4, 9)
            else:
                moving = dict(BURST_SLIDE='=porsche_992_gt3_r|1.0|2.0') if case == 'car' else dict(cube=(-9.0, 3.0, 6.0))
                s2.arms_at(c['cam'], [('r15r_%s_%s' % (case, rule), variant, it.NOBLUR,
                                       dict(frames=9, first=c['k'] - 4, **moving)) for rule, variant in arms])
    for case, c in it.CASES.items():
        refL = it.L(c['ref']())
        best = {'car': 76, 'cube': 123, 'sphere': 121}[case]
        print(case, 'frame', best)
        for rule, _ in RULES:
            if rule in ('i1', 'asbuilt'):
                tag = it.tag_of(case, 'i1' if rule == 'i1' else 'i12')
            else:
                tag = ('r15r_sphere_%s' % rule) if case == 'sphere' else ('rt5b_r15r_%s_%s' % (case, rule))
            im = Image.open(it.S(tag, best)).convert('RGB')
            a = np.asarray(im, dtype=float)[:860]
            g = np.asarray(im.convert('L'), dtype=float)[:860]
            med = np.asarray(im.convert('L').filter(ImageFilter.MedianFilter(3)), dtype=float)[:860]
            cells = []
            for name, (y0, y1, x0, x1) in c['regions'].items():
                e = np.abs(it.L(a) - refL)[y0:y1, x0:x1]
                cells.append('%s: error %.2f (>16 %.1f%%) grain %.1f%%' % (
                    name, e.mean(), (e > 16).mean() * 100, (np.abs(g - med)[y0:y1, x0:x1] > 16).mean() * 100))
            print('   %-8s %s' % (rule, '   '.join(cells)))
