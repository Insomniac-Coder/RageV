# -*- coding: utf-8 -*-
"""RT-15d: where the spoiler streaks come from, on the RT-15c build (reflections before TAA, the
hard object check in, edit 4's rule gone -- and the streaks still there, owner 2026-09-15 night).

  nohist    the reflection accumulator keeps no history at all: the fresh picture alone
  ownonly   the history search reads the texel under the reprojection only, no neighbours
  cap       the build as it is, with the reflection lanes captured over the streak region

Owner's shot, the cube crossing the car (frames 100-140), TAA, cube roughness 0.12.

Usage: rt15d_streak_probe.py render | show
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
stage_run.VARIANTS['nohist'] = [(ACC, '\t\tif (have)' + N + '\t\t{' + N + '\t\t\tg_Kept = true;' + N,
                                      '\t\tif (false && have)' + N + '\t\t{' + N + '\t\t\tg_Kept = true;' + N)]
stage_run.VARIANTS['ownonly'] = [(ACC, '\tfor (int k = 0; k < 9; ++k)' + N, '\tfor (int k = 0; k < 1; ++k)' + N)]
CROP = (630, 330, 180, 120)   # x, y, w, h
MOVE = dict(frames=41, first=100, cube=(-9.0, 3.0, 6.0))

if __name__ == '__main__':
    if sys.argv[1] == 'render':
        try:
            s2.arms_at('owner', [('r15d_nohist', 'nohist', [], MOVE), ('r15d_ownonly', 'ownonly', [], MOVE),
                                 ('r15d_cap', 'ship', ['--capture-signals=reflections,crop=%d:%d:%d:%d' % CROP], MOVE)])
        finally:
            print('restored:', stage_run.restore())
    elif sys.argv[1] == 'show':
        x, y, w, h = CROP
        z = 3
        tags = ['r15c_cube_nocheck', 'r15c_cube_new', 'r15d_ownonly', 'r15d_nohist']
        rows = []
        for f in (114, 120, 126):
            row = [np.asarray(Image.open(os.path.join(stage_run.SHOTS, 'rt5b_%s_%d.png' % (t, f))).convert('RGB'))[y:y + h, x:x + w] for t in tags]
            rows.append(np.concatenate([np.pad(r, ((0, 4), (0, 4), (0, 0))) for r in row], axis=1))
        img = Image.fromarray(np.concatenate(rows, axis=0)).resize(((w + 4) * len(tags) * z, (h + 4) * 3 * z), Image.NEAREST)
        out = os.path.join(stage_run.ROOT, 'build', 'rt15', 'objectaware', 'probe_streaks.png')
        img.save(out)
        print(out, '| columns:', ', '.join(tags), '| rows: frames 114, 120, 126')
