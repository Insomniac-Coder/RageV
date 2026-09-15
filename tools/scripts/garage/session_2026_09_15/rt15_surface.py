# -*- coding: utf-8 -*-
"""RT-15: a moving reflector's history found by its surface rather than by its image, above a roughness.

rt15_diag.py found the driving car closer to its settled pose with no averaging at all than as
shipped: the history holds a misplaced reflection. The accumulator looks for the image's history
first (where the mirror image stood last frame) and the surface's only when that fails. A rough
lobe's picture is a blur that travels with its surface far more than with an image, and the
curved, moving car body is where the image's reprojection is least exact. Each arm takes the
surface's history first on a pixel that moved on its own, above the arm's roughness:

  surf00 every moving pixel   surf10 above 0.1   surf20 above 0.2   surf30 above 0.3

scored as rt15_diag.py scores (the same settled poses, frames and regions).

Usage: rt15_surface.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rt15_diag as d  # noqa: E402

stage_run, s2 = d.stage_run, d.s2
OLD = ('\t\tCandidate c;\n'
       '\t\tbool have = HistoryAt(imagePoint, size, P, N, roughness, eyeDistance,\n')
for tag, thresh in (('surf00', -1.0), ('surf10', 0.1), ('surf20', 0.2), ('surf30', 0.3)):
    stage_run.VARIANTS[tag] = [(d.ACC, OLD,
        '\t\tCandidate c;\n'
        '\t\tconst bool preferSurface = haveSurface && dot(objectShift, objectShift) > 0.0 && roughness > %.2f;\n'
        '\t\tbool have = !preferSurface && HistoryAt(imagePoint, size, P, N, roughness, eyeDistance,\n' % thresh)]
for tag, k in (('curv005', 0.05), ('curv02', 0.2), ('curv05', 0.5)):
    stage_run.VARIANTS[tag] = [(d.ACC, OLD,
        OLD.replace('\t\tbool have = HistoryAt(',
                    '\t\tconst bool preferSurface = haveSurface && dot(objectShift, objectShift) > 0.0 && curvature > %.3f;\n'
                    '\t\tbool have = !preferSurface && HistoryAt(' % k))]
# **closer**: both candidates on a pixel that moved on its own, and the one whose picture sits nearer the
# fresh 3x3's mean, in spreads -- the surface's only when it is clearly nearer (0.7 of the image's).
CHOICE = '\t\tchoice = have ? 1.0 : 0.0;\n\t\tif (!have && haveSurface)\n'
stage_run.VARIANTS['closer'] = [(d.ACC, CHOICE,
    '\t\tchoice = have ? 1.0 : 0.0;\n'
    '\t\tif (have && haveSurface && dot(objectShift, objectShift) > 0.0)\n'
    '\t\t{\n'
    '\t\t\tvec3 nearMean, nearSd;\n'
    '\t\t\tNeighbourhood(texel, size, nearMean, nearSd);\n'
    '\t\t\tconst float spread = max(Luma(nearSd), 1.0e-4);\n'
    '\t\t\tconst float byImage = abs(Luma(c.past.rgb) - Luma(nearMean)) / spread;\n'
    '\t\t\tconst float bySurface = abs(Luma(atSurface.past.rgb) - Luma(nearMean)) / spread;\n'
    '\t\t\tif (bySurface < 0.7 * byImage)\n'
    '\t\t\t{\n'
    '\t\t\t\tc = atSurface;\n'
    '\t\t\t\tchoice = 0.5;\n'
    '\t\t\t}\n'
    '\t\t}\n'
    '\t\tif (!have && haveSurface)\n')]
for tag, margin in (('closercurv07', 0.7), ('closercurv05', 0.5)):
    stage_run.VARIANTS[tag] = [(d.ACC, CHOICE, stage_run.VARIANTS['closer'][0][2]
        .replace('if (have && haveSurface && dot(objectShift, objectShift) > 0.0)',
                 'if (have && haveSurface && dot(objectShift, objectShift) > 0.0 && curvature > 0.2)')
        .replace('bySurface < 0.7 * byImage', 'bySurface < %.1f * byImage' % margin))]
d.ARMS = ['ship', 'noaverage', 'surf00', 'curv005', 'curv02', 'curv05', 'closer']
CURV = ('curv005', 'curv02', 'curv05')

if __name__ == '__main__':
    if 'render' in sys.argv:
        for case, (cam, moving, k, _) in d.CASES.items():
            runs = [('r15_%s_%s' % (case, arm), arm, [], dict(frames=9, first=k - 4, **moving(2.0 if case == 'car' else 6.0)))
                    for arm in (CURV if 'curv' in sys.argv else ('surf00', 'surf10', 'surf20', 'surf30'))]
            s2.arms_at(cam, runs)
    d.analyse()
