# -*- coding: utf-8 -*-
"""What the temporal anti-aliasing does to the chrome cube's reflection (owner, 2026-09-15 evening:
the near-perfect reflection was run r15strk_cap -- STATE A with --aa=none).

STATE A (hit specular + the probe at the hit) is the locked base; every arm here runs it
unchanged ('ship'). Pairs, everything identical but the anti-aliasing: --aa=none against the
project's TAA, the owner's camera, cube roughness 0 and 0.12, stopped (at frame 120, frames
150-169) and moving (3 m/s, frames 116-124).

  render     the eight runs, unique tags r15taa_<none|taa>_c<00|12>_<ref|mov>
  sheets     build/rt15/metal/taa/: full frame and a crop around the cube at normal brightness,
             AA off | TAA, circled; and the numbers: face luma, distance to the true mirror,
             frame-to-frame change on the face (the flicker the owner sees live)
  fix        the same eight runs on the build after the fix, tags r15taaf_*, and the sheets
             TAA before | TAA after | AA off

Usage: rt15_taa_pairs.py render|sheets|fix|fixsheets
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import rt15_cube_metal as m  # noqa: E402

stage_run, s2, sw, it = m.stage_run, m.s2, m.sw, m.it
OUT = os.path.join(m.OUT, 'taa')
FT, K = m.FT, m.K
AA = {'none': ['--aa=none'], 'taa': []}
L = it.L


def render(prefix='r15taa', variant='ship'):
    try:
        for r in (0.0, 0.12):
            sw.with_roughness(r)
            rr = '%02d' % int(round(r * 100))
            arms = []
            for aa, flags in AA.items():
                arms.append(('%s_%s_c%s_ref' % (prefix, aa, rr), variant, flags,
                             dict(frames=20, first=150, mean_from=150, cube=(-9.0, 3.0, K * FT))))
                arms.append(('%s_%s_c%s_mov' % (prefix, aa, rr), variant, flags,
                             dict(frames=9, first=K - 4, cube=(-9.0, 3.0, 6.0))))
            s2.arms_at('owner', arms)
    finally:
        stage_run.CUBE_ENTITY = sw.BASE


def frames_of(tag, first, count):
    return [m.img('rt5b_' + tag, k) for k in range(first, first + count)]


def stats(tag, rect, first, count, mirror_inner):
    """Face luma, distance to the true mirror and frame-to-frame change, over the face's inner
    part, for the frames of one run."""
    fs = frames_of(tag, first, count)
    inner = lambda a: L(a)[rect[0] + 8:rect[1] - 8, rect[2] + 8:rect[3] - 8]
    vals = [inner(a) for a in fs]
    change = np.mean([np.abs(vals[i] - vals[i - 1]).mean() for i in range(1, len(vals))])
    mean = np.mean(vals, axis=0)
    err = np.abs(mean - mirror_inner)
    return mean.mean(), err.mean(), (err > 16).mean() * 100, change


def sheets(prefix='r15taa', out=None, extra=None, label_a='AA OFF (--aa=none)', label_b='TAA (project setting)'):
    """AA off | TAA per case; `extra` adds a third run prefix (the fixed build's TAA) as a
    third column, labelled by extra[1]."""
    out = out or OUT
    os.makedirs(out, exist_ok=True)
    mir = m.img('rt5b_r15m_mirror', 151)[:, ::-1]
    nocube = m.mean_npy('r15m_b_parked')
    rect = m.face_rect(m.mean_npy('r15m_a_c00_ref'), nocube)
    fy0, fy1, fx0, fx1 = rect
    mir_inner = L(mir)[fy0 + 8:fy1 - 8, fx0 + 8:fx1 - 8]
    made = []
    print('%-28s %9s %14s %8s %14s' % ('run', 'face luma', '|face-mirror|', '>16 %', 'frame-to-frame'))
    for r in (0.0, 0.12):
        rr = '%02d' % int(round(r * 100))
        for state, first, count, show in (('stopped', 150, 20, 160), ('moving', K - 4, 9, 121)):
            frect = rect if state == 'stopped' else (fy0, fy1, fx0 + 4, fx1 + 4)
            cols = [('%s_none_c%s_%s' % (prefix, rr, 'ref' if state == 'stopped' else 'mov'), label_a),
                    ('%s_taa_c%s_%s' % (prefix, rr, 'ref' if state == 'stopped' else 'mov'), label_b)]
            if extra:
                cols.append(('%s_taa_c%s_%s' % (extra[0], rr, 'ref' if state == 'stopped' else 'mov'), extra[1]))
            panels_full, panels_crop, caps = [], [], []
            for tag, label in cols:
                mean_l, err, over, change = stats(tag, frect, first, count, mir_inner)
                print('%-28s %9.1f %14.1f %8.0f %14.2f' % (tag, mean_l, err, over, change))
                a = m.img('rt5b_' + tag, show)
                panels_full.append(a[:860])
                pad = 40
                panels_crop.append(a[frect[0] - pad:frect[1] + pad, frect[2] - pad:frect[3] + pad])
                caps.append('%s: face luma %.0f, %.1f levels from the true mirror, %.2f levels of change frame to frame.'
                            % (label, mean_l, err, change))
            name = 'cube_r%s_%s' % (rr, state)
            circ_full = ((frect[2] + frect[3]) // 2, (frect[0] + frect[1]) // 2, 110)
            made.append(m.sheet(os.path.join(out, name + '_full.png'),
                                'Chrome cube, roughness %g, %s, frame %d, STATE A -- full frame at normal brightness; the circle is the cube' % (r, state, show),
                                panels_full, caps, circle=circ_full, gain=1.0, scale=1))
            pad = 40
            circ = ((frect[3] - frect[2]) // 2 + pad, (frect[1] - frect[0]) // 2 + pad, max(frect[3] - frect[2], frect[1] - frect[0]) // 2 + 6)
            made.append(m.sheet(os.path.join(out, name + '_crop.png'),
                                'Chrome cube, roughness %g, %s, frame %d, STATE A -- crop at normal brightness, look inside the circle' % (r, state, show),
                                panels_crop, caps, circle=circ, gain=1.0, scale=4))
    for f in made:
        print(f)


if __name__ == '__main__':
    {'render': render, 'sheets': sheets,
     'fix': lambda: render('r15taaf'),
     'fixsheets': lambda: sheets('r15taa', os.path.join(m.OUT, 'taa_fix'), extra=('r15taaf', 'TAA AFTER THE FIX'),
                                 label_a='AA OFF (the reference look)', label_b='TAA BEFORE THE FIX')}[sys.argv[1]]()
