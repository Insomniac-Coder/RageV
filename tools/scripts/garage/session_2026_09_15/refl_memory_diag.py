# -*- coding: utf-8 -*-
"""Which rule sets the reflection memory on a moving object? (option B, 2026-09-15)

A staged reflection_accumulate writes the memory's steps into the lanes the capture
reads back -- the image-motion lane (3) and the id lane's spare channels (4) -- for
the chrome cube crossing the garage and the car driving, cropped to the object:
  diagA: 3 = (memory after the motion rules, direction confidence, match confidence,
              memory after both), 4.ba = (the hit-distance confidence, measured change)
  diagB: 3 = (the facing term, the plane term, material x id, texels the picture moved),
         4.ba = (the silhouette's neighbour motion or -1, the frames the blend used)
-1 marks a texel with no history candidate. The picture's frames are lane 0's alpha.

Usage: refl_memory_diag.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_14'))
import numpy as np  # noqa: E402
import stage_run  # noqa: E402
import rt13_stage2 as s2  # noqa: E402

ACC = 'reflection_accumulate.rvshader'
N = '\n'
COMMON = [
    (ACC, '\tfloat matchConfidence;' + N + '};',
          '\tfloat matchConfidence;' + N + '\tfloat diagFacing; float diagPlane; float diagIdMat; float diagHit;' + N + '};'),
    (ACC, '\t\tc.matchConfidence = 1.0;' + N,
          '\t\tc.matchConfidence = 1.0;' + N + '\t\tc.diagFacing = 1.0; c.diagPlane = 1.0; c.diagIdMat = 1.0; c.diagHit = 1.0;' + N),
    (ACC, '\t\t\t\t\t\t\t  * material * idPenalty * hit;' + N,
          '\t\t\t\t\t\t\t  * material * idPenalty * hit;' + N
          + '\t\t\tc.diagFacing = smoothstep(0.8, 0.94, facing); c.diagPlane = planeAgree; c.diagIdMat = material * idPenalty; c.diagHit = hit;' + N),
    (ACC, 'float g_BlendFrames = 1.0;' + N,
          'float g_BlendFrames = 1.0;' + N
          + 'float g_DiagMotion = -1.0; float g_DiagDirection = -1.0; float g_DiagMatch = -1.0; float g_DiagMemory = -1.0;' + N
          + 'float g_DiagHit = -1.0; float g_DiagMeasured = -1.0; float g_DiagFacing = -1.0; float g_DiagPlane = -1.0;' + N
          + 'float g_DiagIdMat = -1.0; float g_DiagMoved = -1.0; float g_DiagSil = -1.0;' + N),
    (ACC, '\t\t\t\t\t\t\t\t\t\t   neighbourMotion);' + N + N + '\t\t\t// **RT-6.3',
          '\t\t\t\t\t\t\t\t\t\t   neighbourMotion);' + N
          + '\t\t\tg_DiagMotion = memory; g_DiagMoved = moved; g_DiagSil = silhouette ? neighbourMotion : -1.0;' + N + N + '\t\t\t// **RT-6.3'),
    (ACC, '\t\t\t\tmemory = max(mix(fewest, memory, confidence), fewest);' + N,
          '\t\t\t\tg_DiagDirection = confidence;' + N + '\t\t\t\tmemory = max(mix(fewest, memory, confidence), fewest);' + N),
    (ACC, '\t\t\tmemory = max(mix(fewest, memory, c.matchConfidence), fewest);' + N,
          '\t\t\tmemory = max(mix(fewest, memory, c.matchConfidence), fewest);' + N
          + '\t\t\tg_DiagMatch = c.matchConfidence; g_DiagFacing = c.diagFacing; g_DiagPlane = c.diagPlane;' + N
          + '\t\t\tg_DiagIdMat = c.diagIdMat; g_DiagHit = c.diagHit; g_DiagMemory = memory;' + N),
    (ACC, '\t\t\t\tmeasured = MeasuredChangeAt(vec2(texel) + 0.5 + travelled);' + N,
          '\t\t\t\tmeasured = MeasuredChangeAt(vec2(texel) + 0.5 + travelled);' + N + '\t\t\t\tg_DiagMeasured = measured.x;' + N),
]
OUT_OLD = ('\to_Motion = vec4(g_HaveMotion ? g_ImageMotion : vec2(0.0),' + N
           + '\t\t\t\t\tOctEncode(reflect(sight, N)));' + N
           + '\t// RT-6.5: who this texel was, for the next frame\'s fourth check.' + N
           + '\to_Ident = vec4(g_ObjectId, u_Reflection.Change.x > 0.5 ? g_BlendFrames : 0.0, 0.0, 0.0);' + N)
stage_run.VARIANTS['diagA'] = COMMON + [(ACC, OUT_OLD,
    '\to_Motion = vec4(g_DiagMotion, g_DiagDirection, g_DiagMatch, g_DiagMemory);' + N
    + '\to_Ident = vec4(g_ObjectId, u_Reflection.Change.x > 0.5 ? g_BlendFrames : 0.0, g_DiagHit, g_DiagMeasured);' + N)]
stage_run.VARIANTS['diagB'] = COMMON + [(ACC, OUT_OLD,
    '\to_Motion = vec4(g_DiagFacing, g_DiagPlane, g_DiagIdMat, g_DiagMoved);' + N
    + '\to_Ident = vec4(g_ObjectId, u_Reflection.Change.x > 0.5 ? g_BlendFrames : 0.0, g_DiagSil, g_BlendFrames);' + N)]

CASES = {
    'cube': ('owner', dict(frames=12, first=110, cube=(-9.0, 3.0, 6.0)), '600:300:240:120'),
    'car': ('close', dict(frames=12, first=58, BURST_SLIDE=s2.CAR), '550:480:300:150'),
}

if 'render' in sys.argv:
    for case, (cam, opts, crop) in CASES.items():
        s2.arms_at(cam, [('rmd_%s_%s' % (case, v), v, ['--capture-signals=reflections,crop=' + crop], dict(opts))
                         for v in ('diagA', 'diagB')])


def load(case, variant, lane):
    return np.load(os.path.join(stage_run.SHOTS, 'rt5b_rmd_%s_%s_reflections%d_crop.npy' % (case, variant, lane)))


for case in CASES:
    a0, a3, a4 = load(case, 'diagA', 0), load(case, 'diagA', 3), load(case, 'diagA', 4)
    b3, b4 = load(case, 'diagB', 3), load(case, 'diagB', 4)
    # The object's texels: its id, taken as the most common non-zero id in the crop.
    ids = a4[..., 0]
    vals, counts = np.unique(np.round(ids[ids != 0]), return_counts=True)
    print('%s: crop %s, frames %d; ids in the crop (top): %s' % (case, CASES[case][2], a0.shape[0],
          ', '.join('%d x%d' % (v, c) for v, c in sorted(zip(vals, counts), key=lambda t: -t[1])[:5])))
    for label, obj in (('object', None),):
        pass
    for which, sel in (('all glossy texels', a0[..., 3] > 0),):
        frames = a0[..., 3][sel]
        print('   %-18s n=%d  frames kept: median %.1f  mean %.1f  <=2: %.0f%%' % (which, sel.sum(), np.median(frames), frames.mean(), (frames <= 2).mean() * 100))
    for v, vals in sorted(zip(vals, counts), key=lambda t: -t[1])[:3]:
        sel = (np.abs(ids - v) < 0.5) & (a0[..., 3] > 0)
        if sel.sum() < 50:
            continue
        had = sel & (a3[..., 3] >= 0)
        def q(x, m):
            x = x[m]
            return 'median %.2f  p10 %.2f' % (np.median(x), np.percentile(x, 10)) if x.size else 'n/a'
        print('   id %d: %d texels, %.0f%% had a history candidate; frames kept median %.1f' % (v, sel.sum(), had.sum() * 100.0 / max(sel.sum(), 1), np.median(a0[..., 3][sel])))
        print('      memory after motion rules  ', q(a3[..., 0], had))
        print('      direction confidence       ', q(a3[..., 1], had & (a3[..., 1] >= 0)))
        print('      match confidence           ', q(a3[..., 2], had))
        print('      memory after all           ', q(a3[..., 3], had))
        print('      hit-distance confidence    ', q(a4[..., 2], had))
        print('      measured change share      ', q(a4[..., 3], had & (a4[..., 3] >= 0)))
        hb = (np.abs(b4[..., 0] - v) < 0.5) & (b3[..., 3] >= 0)
        print('      facing term                ', q(b3[..., 0], hb))
        print('      plane term                 ', q(b3[..., 1], hb))
        print('      material x id              ', q(b3[..., 2], hb))
        print('      texels the picture moved   ', q(b3[..., 3], hb))
        sil = hb & (b4[..., 2] >= 0)
        print('      at a silhouette            %.0f%%' % (sil.sum() * 100.0 / max(hb.sum(), 1)))
        print('      frames the blend used      ', q(b4[..., 3], hb))
