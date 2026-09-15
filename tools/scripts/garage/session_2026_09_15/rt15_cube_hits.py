# -*- coding: utf-8 -*-
"""Why is the mirror cube black? (owner, 2026-09-15: "the cube just doesn't reflect the environment
correctly ... it should reflect ... bits of car and the things in front of the cube")

The cube's normal faces the camera and its reflection rays leave back toward it and land about
33 m away (rt15 capture); the view behind the camera shows a lit wall (build/rt15/reverse_view.png),
yet the traced picture there is black. This stages the reflection trace to write, instead of the
radiance, what lit each ray's hit:

  r  the live lights' direct light at the hit (luminance)
  g  everything else ShadeTraced adds -- ambient, probe, the field's baked direct light (luminance)
  b  100 for a miss, + 10 for a static hit, + the irradiance field's weight at the hit (0..1)
  a  the distance the ray travelled (as shipped)

and captures the accumulated lanes over the stopped roughness-0 cube.

Usage: rt15_cube_hits.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
import rt15_items as it  # noqa: E402
import rt15_cube_sweep as sw  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
N = '\n'
INC = 'include/pbr_fragment.glsl'
TRACE = 'reflection_trace.rvshader'
stage_run.VARIANTS['hitdiag'] = [
    (INC, "// `reach` is how far the ray may travel, in world metres. A reflection wants" + N,
          "float g_DiagFieldWeight = -1.0;" + N + "// `reach` is how far the ray may travel, in world metres. A reflection wants" + N),
    (INC, "\t\thitFieldWeight = IrradianceFieldWeight(hitPosition, hitNormal);" + N,
          "\t\thitFieldWeight = IrradianceFieldWeight(hitPosition, hitNormal);" + N + "\tg_DiagFieldWeight = hitFieldWeight;" + N),
    (TRACE, "\to_Reflection = vec4(min(radiance, vec3(64.0)) * tint, travelled);" + N,
            "\tconst vec3 kDiagLuma = vec3(0.2126, 0.7152, 0.0722);" + N
            + "\tconst float diagDirect = hit.Missed ? 0.0 : dot(hit.Direct, kDiagLuma);" + N
            + "\tconst float diagRest = dot(radiance - (hit.Missed ? vec3(0.0) : hit.Direct + hit.Emissive), kDiagLuma);" + N
            + "\tconst float diagFlags = (hit.Missed ? 100.0 : 0.0) + (hit.Static ? 10.0 : 0.0) + max(g_DiagFieldWeight, 0.0);" + N
            + "\to_Reflection = vec4(diagDirect, diagRest, diagFlags, travelled);" + N),
]
CROP = (660, 290, 200, 130)   # x, y, w, h: the stopped cube's face at the owner's shot

if __name__ == '__main__':
    if 'render' in sys.argv:
        try:
            sw.with_roughness(0.0)
            s2.arms_at('owner', [('r15h_cube', 'hitdiag', ['--capture-signals=reflections,crop=%d:%d:%d:%d' % CROP],
                                  dict(frames=2, first=150, cube=(-9.0, 3.0, 120 * 0.0166)))])
        finally:
            stage_run.CUBE_ENTITY = sw.BASE
    base = os.path.join(stage_run.SHOTS, 'rt5b_r15h_cube_reflections%d_crop.npy')
    pic, surf, ext, mot, ident = [np.load(base % i)[-1] for i in range(5)]
    ids = ident[..., 0]
    cube = ids == 278 if (ids == 278).any() else ids > 200
    print('cube texels:', cube.sum(), 'ids present:', np.unique(ids[np.abs(ids - np.round(ids)) < 1e-3])[:12])
    direct, rest, flags, dist = pic[..., 0][cube], pic[..., 1][cube], pic[..., 2][cube], pic[..., 3][cube]
    missed = flags >= 99.5
    static = (np.mod(flags, 100.0) >= 9.5)
    weight = np.mod(np.mod(flags, 100.0), 10.0)
    print('missed %.1f%%  static hits %.1f%%  field weight at static hits: mean %.2f, zero at %.1f%%'
          % (missed.mean() * 100, static.mean() * 100, weight[static].mean() if static.any() else -1,
             (weight[static] < 0.01).mean() * 100 if static.any() else -1))
    print('direct (live lights) luma: median %.4f  p90 %.4f' % (np.median(direct), np.percentile(direct, 90)))
    print('rest (ambient+probe+field) luma: median %.4f  p90 %.4f' % (np.median(rest), np.percentile(rest, 90)))
    print('distance travelled: median %.1f m' % np.median(dist))
    rows = cube.any(axis=1)
    for r in np.nonzero(rows)[0][::12]:
        sel = cube[r]
        print('  row %3d: direct %.4f  rest %.4f  flags %s  dist %.1f' % (
            r + CROP[1], pic[r, sel, 0].mean(), pic[r, sel, 1].mean(),
            np.unique(np.round(pic[r, sel, 2], 1))[:4], pic[r, sel, 3].mean()))
