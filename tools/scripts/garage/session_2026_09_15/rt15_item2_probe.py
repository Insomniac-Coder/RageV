# -*- coding: utf-8 -*-
"""RT-15 item 2: where is the struck point's travel applied, and how far?

Item 2 moved the floor in front of the *parked* car further from its settled picture in the
cube case, where only the cube moves. This paints the accumulated picture by the image
point's displacement, in texels on last frame's grid: green where the lookup moved at all,
brighter with more travel (full at four texels), red where it moved over a texel.

Usage: rt15_item2_probe.py [render] [cube|car|sphere]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import rt15_items as it  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
ACC = it.ACC
N = '\n'
stage_run.VARIANTS['travelprobe'] = [
    (ACC, 'bool g_MovingCurved = false;' + N, 'bool g_MovingCurved = false;' + N + 'float g_TravelTexels = -1.0;' + N),
    (ACC, '\t\tvec3 imagePoint = P + sight * image - MirrorVector(seenTravel, N);' + N,
          '\t\tvec3 imagePoint = P + sight * image - MirrorVector(seenTravel, N);' + N
          + '\t\tif (dot(seenTravel, seenTravel) > 0.0)' + N
          + '\t\t{' + N
          + '\t\t\tconst vec4 a = u_Reflection.PreviousViewProjection * vec4(P + sight * image, 1.0);' + N
          + '\t\t\tconst vec4 b = u_Reflection.PreviousViewProjection * vec4(imagePoint, 1.0);' + N
          + '\t\t\tg_TravelTexels = length((b.xy / b.w - a.xy / a.w) * 0.5 * vec2(size));' + N
          + '\t\t}' + N),
    (ACC, '\tconst uvec2 roundingPixel = uvec2(texel);' + N,
          '\tif (g_TravelTexels >= 0.0)' + N
          + '\t\tkept = g_TravelTexels > 1.0 ? vec3(8.0, 0.0, 0.0) : vec3(0.0, 8.0 * clamp(0.25 + g_TravelTexels, 0.0, 1.0), 0.0);' + N
          + '\tconst uvec2 roundingPixel = uvec2(texel);' + N),
]

if __name__ == '__main__':
    case = next((a for a in sys.argv[1:] if a in it.CASES), 'cube')
    c = it.CASES[case]
    if 'render' in sys.argv:
        if case == 'sphere':
            it.sphere_burst('r15p_sphere', 'travelprobe', it.NOBLUR, c['k'] - 1, 3)
        else:
            moving = dict(BURST_SLIDE='=porsche_992_gt3_r|1.0|2.0') if case == 'car' else dict(cube=(-9.0, 3.0, 6.0))
            s2.arms_at(c['cam'], [('r15p_%s' % case, 'travelprobe', it.NOBLUR, dict(frames=3, first=c['k'] - 1, **moving))])
    tag = 'r15p_sphere' if case == 'sphere' else 'rt5b_r15p_%s' % case
    a = np.asarray(Image.open(it.S(tag, c['k'])).convert('RGB'), dtype=float)[:860]
    green = (a[..., 1] > 150) & (a[..., 1] > 2.5 * a[..., 0]) & (a[..., 1] > 2.5 * a[..., 2])
    red = (a[..., 0] > 150) & (a[..., 0] > 2.5 * a[..., 1]) & (a[..., 0] > 2.5 * a[..., 2])
    print('%s frame %d: displaced under a texel %.2f%% of pixels, over a texel %.2f%%' % (case, c['k'], green.mean() * 100, red.mean() * 100))
    Image.fromarray(a.astype(np.uint8)).save(os.path.join(it.OUT, 'travelprobe_%s.png' % case))
