# -*- coding: utf-8 -*-
"""RT-15d: the vertical bands on the moving chrome cube (owner, 2026-09-15 night: "address the
bands, spoiler trail is second priority now").

The clean reference is the same cube stopped where frame 120 puts it (r15d_stopped, frames
170-189 averaged): zoomed, it has no lines and no bars. The artifact is moving frame 120 minus that
reference, over the cube's face above the wing (x 700..780, y 322..388) and the band below the
wing (y 400..416). Two numbers per run:
  bands   the column-to-column detail of the artifact (sd of its high-passed column means)
  lines   the row-to-row detail of the artifact (the same, along rows)
and a zoomed strip of every run beside the reference.

Arms, each staged on the RT-15c build (the cube crossing the car, frames 100-121, TAA):
  rays1      --reflection-moving-rays=1 (RT-15b's four rays off)
  nohitconf  HitConfidence always one
  noimagemix the image distance not carried from the surface's old place
  aanone     --aa=none

Usage: rt15d_bands.py render [arm ...] | measure [tag ...]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import rt15_items as it  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
N = '\n'
ACC = 'reflection_accumulate.rvshader'
stage_run.VARIANTS['nohitconf'] = [(ACC, '\t\t\tconst float hit = HitConfidence(g_FreshImage, c.reflector.a, roughness);' + N,
                                         '\t\t\tconst float hit = 1.0;' + N)]
stage_run.VARIANTS['noimagemix'] = [(ACC, '\t\t\timage = mix(atSurface.reflector.a, image, 1.0 / min(atSurface.past.a + 1.0, 8.0));' + N,
                                          '\t\t\timage = image;' + N)]
MOVE = dict(frames=22, first=100, cube=(-9.0, 3.0, 6.0))
ARMS = {'rays1': ('ship', ['--reflection-moving-rays=1']), 'nohitconf': ('nohitconf', []),
        'noimagemix': ('noimagemix', []), 'aanone': ('ship', ['--aa=none'])}
S = stage_run.SHOTS
LW = np.array([0.2126, 0.7152, 0.0722])
FACE = (700, 780, 322, 388)
BAND = (700, 780, 400, 416)


def img(tag, f):
    return np.asarray(Image.open(os.path.join(S, 'rt5b_%s_%d.png' % (tag, f))).convert('RGB'), np.float64)


def hp(p, r=6):
    return p - np.convolve(np.pad(p, r, mode='edge'), np.ones(2 * r + 1) / (2 * r + 1), mode='valid')


def measure(tags):
    ref = np.mean([img('r15d_stopped', f) for f in range(170, 190)], axis=0) @ LW
    strips = []
    for tag in tags:
        a = img(tag, 120) @ LW
        out = []
        for name, (x0, x1, y0, y1) in (('face', FACE), ('under wing', BAND)):
            art = a[y0:y1, x0:x1] - ref[y0:y1, x0:x1]
            out.append('%s bands %5.2f lines %5.2f off %5.2f' % (name, np.std(hp(art.mean(axis=0))), np.std(hp(art.mean(axis=1), 3)),
                                                              np.abs(art).mean()))
        print('%-22s %s' % (tag, ' | '.join(out)))
        strips.append(img(tag, 120)[312:420, 690:790])
    strips.append(np.mean([img('r15d_stopped', f) for f in range(170, 190)], axis=0)[312:420, 690:790])
    row = np.concatenate([np.pad(s, ((0, 0), (0, 3), (0, 0))) for s in strips], axis=1).astype(np.uint8)
    path = os.path.join(stage_run.ROOT, 'build', 'rt15', 'objectaware', 'bands_arms.png')
    Image.fromarray(row).resize((row.shape[1] * 4, row.shape[0] * 4), Image.NEAREST).save(path)
    print(path, '| strips:', ', '.join(tags), ', stopped reference')


if __name__ == '__main__':
    if sys.argv[1] == 'render':
        try:
            for arm in (sys.argv[2:] or list(ARMS)):
                variant, flags = ARMS[arm]
                s2.arms_at('owner', [('r15d_b_' + arm, variant, flags, MOVE)])
        finally:
            print('restored:', stage_run.restore())
    else:
        measure(sys.argv[2:])
