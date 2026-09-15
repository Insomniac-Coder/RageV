# -*- coding: utf-8 -*-
"""The cube's vertical streaks at the car's wing height (owner, 2026-09-15: "the spoiler of the car
causes vertical streaks on the lower side of the cube when it crosses the car ... very noticeable
if the cube is very shiny").

The streaks run across the cube's whole bottom band, a frame's travel apart (~6 px), and the
picture averaging nothing is plain dark there (build/rt15/spoiler_nocube.png) -- so they are
history. Hypothesis: a flat mirror sliding in its plane shows a still picture, so a texel the
leading edge newly covers has no image history (last frame it was the wall), and the accumulator
falls back to the *surface's* history -- the cube point's texel last frame, at the edge, whose
picture carries the wall's highlights through the jittered silhouette. Each frame's newly covered
strip inherits one, and on a dark mirror nothing refreshes it for the full memory.

  probe      paints the history each texel took: green the image's, red the surface's, blue none
  nofall     the surface fallback refused on a flat, smooth reflector moving on its own
             (curvature at or under kCurvedMover, roughness under 0.3)

At roughness 0 and at the cube's 0.12; scored against each roughness's settled cube (rt15_cube_sweep).

Usage: rt15_cube_streaks.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFilter  # noqa: E402
import rt15_items as it  # noqa: E402
import rt15_cube_sweep as sw  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
ACC = it.ACC
N = '\n'
FALLBACK = '\t\tif (!have && haveSurface)' + N
NOFALL = (ACC, FALLBACK, '\t\tif (!have && haveSurface && !(dot(objectShift, objectShift) > 0.0'
          ' && curvature <= kCurvedMover && roughness < 0.3))' + N)
PROBE = (ACC, '\tconst uvec2 roundingPixel = uvec2(texel);' + N,
         '\tif (u_Reflection.History.x > 0.5)' + N
         + '\t\tkept = choice > 0.75 ? vec3(0.0, 4.0, 0.0) : choice > 0.25 ? vec3(6.0, 0.0, 0.0) : vec3(0.0, 0.0, 4.0);' + N
         + '\tconst uvec2 roundingPixel = uvec2(texel);' + N)
stage_run.VARIANTS['probe'] = [PROBE]
stage_run.VARIANTS['nofall'] = [NOFALL]
stage_run.VARIANTS['nofallprobe'] = [NOFALL, PROBE]
ROUGH = (0.0, 0.12)
FRAME = 121

if __name__ == '__main__':
    if 'render' in sys.argv:
        try:
            for r in ROUGH:
                sw.with_roughness(r)
                tag = 'r15k_%02d' % int(round(r * 100))
                s2.arms_at('owner', [(tag + '_' + v, v, [], dict(frames=9, first=sw.K - 4, cube=(-9.0, 3.0, 6.0)))
                                     for v in ('probe', 'nofall', 'nofallprobe')])
        finally:
            stage_run.CUBE_ENTITY = sw.BASE
    L = it.L
    y0, y1, x0, x1 = 300, 430, 640, 880
    rows = []
    for r in ROUGH:
        rt = '%02d' % int(round(r * 100))
        ref = np.load(os.path.join(stage_run.OUT, 'r15s_%s_ref.npy' % rt))[:860]
        load = lambda t: np.asarray(Image.open(it.S('rt5b_' + t, FRAME)).convert('RGB'), dtype=float)[:860]
        ship, nofall = load('r15s_%s_ship' % rt), load('r15k_%s_nofall' % rt)
        for name, a in (('build', ship), ('no surface fallback on flat movers', nofall)):
            e = np.abs(L(a) - L(ref))
            print('roughness %-4g %-36s cube face %.2f   bottom band (rows 395-425) %.2f' % (
                r, name, e[300:420, 640:860].mean(), e[395:425, 640:860].mean()))
        rows += [('roughness %g: which history (build)' % r, load('r15k_%s_probe' % rt)),
                 ('roughness %g: which history (no fallback)' % r, load('r15k_%s_nofallprobe' % rt)),
                 ('roughness %g: build (x2)' % r, ship * 2), ('roughness %g: no fallback (x2)' % r, nofall * 2),
                 ('roughness %g: settled (x2)' % r, ref * 2)]
    h, w = (y1 - y0) * 2, (x1 - x0) * 2
    out = Image.new('RGB', ((w + 8) * 5, (h + 18) * len(ROUGH)), (0, 0, 0))
    d = ImageDraw.Draw(out)
    for i, (label, a) in enumerate(rows):
        tile = Image.fromarray(np.clip(a[y0:y1, x0:x1], 0, 255).astype(np.uint8)).resize((w, h), Image.NEAREST)
        x, y = (i % 5) * (w + 8), (i // 5) * (h + 18)
        out.paste(tile, (x, y + 18))
        d.text((x + 4, y + 3), label, fill=(255, 255, 255))
    out.save(os.path.join(it.OUT, 'cube_streaks.png'))
