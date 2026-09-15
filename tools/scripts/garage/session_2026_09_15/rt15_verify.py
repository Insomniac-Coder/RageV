# -*- coding: utf-8 -*-
"""RT-15b verification (2026-09-15 evening): the streak mark and the moving rays, before and after.

BEFORE is STATE A reproduced on this build: the accumulator's mark staged to zero (nothing is
ever marked, so nothing is refused for it) and --reflection-moving-rays=1 (one ray a texel, the
draw index the frame's -- bit for bit the one-ray trace). AFTER is the build as it stands.

  render   parked garage (owner's shot, frames 150-189), the camera dolly (0.6 m/s, 60-119), the
           car driving at the close-up (frames 40-99), the chrome sphere crossing (116-124)
  sheets   the owner's sheets: moving 0.12 cube under TAA before | after | stopped, the wing
           band the same, the car body and the sphere before | after; and the numbers for the
           parked garage and the dolly (expected: identical apart from the known noise)
  cost     --benchmark with the car driving, both arms, the reflection passes' GPU time

Usage: rt15_verify.py render|sheets|cost
"""
import io, os, re, statistics, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
import rt15_cube_metal as m  # noqa: E402
import rt15_taa_pairs as tp  # noqa: E402
import burst  # noqa: E402

stage_run, s2, sw, it = m.stage_run, m.s2, m.sw, m.it
ACC, N = 'reflection_accumulate.rvshader', '\n'
stage_run.VARIANTS['nomark'] = [(ACC, "const float kSilhouetteMoverMark = 0.125;" + N, "const float kSilhouetteMoverMark = 0.0;" + N)]
BEFORE = ('nomark', ['--reflection-moving-rays=1'])
AFTER = ('ship', [])
ARMS = {'before': BEFORE, 'after': AFTER}
CAR = s2.CAR
OUT = os.path.join(m.OUT, 'verify')
L = it.L


def render():
    for arm, (variant, flags) in ARMS.items():
        s2.arms_at('owner', [('r15v_parked_' + arm, variant, flags, dict(frames=40, first=150, mean_from=150)),
                             ('r15v_dolly_' + arm, variant, flags, dict(frames=60, first=60, speed=0.6, stop=1.5))])
        s2.arms_at('close', [('r15v_car_' + arm, variant, flags, dict(frames=60, first=40, BURST_SLIDE=CAR))])
        it.sphere_burst('r15v_sph_' + arm, variant, flags, m.K - 4, 9)


def grain(a, y0, y1, x0, x1):
    im = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).convert('L')
    g = np.asarray(im, dtype=float)
    med = np.asarray(im.filter(ImageFilter.MedianFilter(3)), dtype=float)
    return (np.abs(g - med)[y0:y1, x0:x1] > 16).mean() * 100


def sheets():
    os.makedirs(OUT, exist_ok=True)
    made = []
    # --- the parked garage and the dolly: numbers, and a diff picture if anything moved
    for case, ks in (('parked', range(150, 190)), ('dolly', range(60, 120))):
        worst, share = 0, 0.0
        for k in ks:
            b = np.asarray(Image.open(m.S('rt5b_r15v_%s_before' % case, k)).convert('RGB'), dtype=int) if False else None
        d = [np.abs(np.asarray(Image.open(os.path.join(stage_run.SHOTS, 'rt5b_r15v_%s_after_%d.png' % (case, k))).convert('RGB'), dtype=int)
                    - np.asarray(Image.open(os.path.join(stage_run.SHOTS, 'rt5b_r15v_%s_before_%d.png' % (case, k))).convert('RGB'), dtype=int)) for k in ks]
        print('%-7s after vs before: largest pixel difference %d, pixels differing %.4f%% (mean over frames)'
              % (case, max(x.max() for x in d), np.mean([(x > 0).any(axis=2).mean() for x in d]) * 100))
    b = m.mean_npy('r15v_parked_before'); a = m.mean_npy('r15v_parked_after'); sa = m.mean_npy('r15m_a_parked')
    for name, x in (('after vs STATE A run 7 (old exe)', np.abs(a - sa)), ('before vs STATE A run 7 (old exe)', np.abs(b - sa))):
        dd = x.max(axis=2)
        print('parked mean, %s: mean %.2f levels, max %.0f, pixels over 8: %.3f%%' % (name, dd.mean(), dd.max(), (dd > 8).mean() * 100))

    # --- the moving 0.12 cube under TAA: before | after | stopped (after)
    nocube = m.mean_npy('r15m_b_parked')
    rect = m.face_rect(m.mean_npy('r15m_a_c00_ref'), nocube)
    fy0, fy1, fx0, fx1 = rect
    mrect = (fy0, fy1, fx0 + 4, fx1 + 4)
    mir = m.img('rt5b_r15m_mirror', 151)[:, ::-1]
    mir_inner = L(mir)[fy0 + 8:fy1 - 8, fx0 + 8:fx1 - 8]
    for aa, aalabel in (('taa', 'TAA (the project setting)'), ('none', 'anti-aliasing off')):
        before = m.img('rt5b_r15taa_%s_c12_mov' % aa, 121)
        after = m.img('rt5b_r15taag_%s_c12_mov' % aa, 121)
        stopped = m.mean_npy('r15taag_%s_c12_ref' % aa)
        nb = tp.stats('r15taa_%s_c12_mov' % aa, mrect, m.K - 4, 9, mir_inner)
        na = tp.stats('r15taag_%s_c12_mov' % aa, mrect, m.K - 4, 9, mir_inner)
        ns = tp.stats('r15taag_%s_c12_ref' % aa, rect, 150, 20, mir_inner)
        pad = 40
        crop = lambda x, r: x[r[0] - pad:r[1] + pad, r[2] - pad:r[3] + pad]
        circ = ((fx1 - fx0) // 2 + pad, (fy1 - fy0) // 2 + pad, max(fx1 - fx0, fy1 - fy0) // 2 + 6)
        made.append(m.sheet(os.path.join(OUT, 'cube_r12_moving_%s.png' % aa),
                            'Chrome cube, roughness 0.12, moving at 3 m/s, frame 121, %s -- look inside the circle (normal brightness)' % aalabel,
                            [crop(before, mrect), crop(after, mrect), crop(stopped, rect)],
                            ['BEFORE (STATE A): speckle over the whole face, dashed bright rows, and the vertical bars at the bottom where the wing crosses. %.1f levels from the true mirror, %.2f of change frame to frame.' % (nb[1], nb[3]),
                             'AFTER: the face is smooth grain with no dots, no dashed rows and no bars at the wing. %.1f levels from the true mirror, %.2f of change frame to frame.' % (na[1], na[3]),
                             'STOPPED, SETTLED (after the fix, mean of 20 frames): what the moving face should approach. %.1f levels from the true mirror.' % ns[1]],
                            circle=circ, gain=1.0, scale=4))
        # the wing band: the spoiler streaks
        by0, by1, bx0, bx1 = 384, 424, fx0 - 30, fx1 + 20
        bcirc = ((bx1 - bx0) // 2, (by1 - by0) // 2, 60)
        made.append(m.sheet(os.path.join(OUT, 'streaks_%s.png' % aa),
                            'The cube\'s lower band where it crosses the car\'s wing, roughness 0.12, moving, frame 121, %s -- look inside the circle (brightened x2)' % aalabel,
                            [before[by0:by1, bx0:bx1], after[by0:by1, bx0:bx1], stopped[by0:by1, bx0:bx1]],
                            ['BEFORE (STATE A): bright vertical bars one frame of travel apart, fading to the left.',
                             'AFTER: no bars; the band is the cube\'s own dark reflection of the wing.',
                             'STOPPED, SETTLED (after the fix): the band as it is when nothing moves.'],
                            circle=bcirc, gain=2.0, scale=6))
    # --- the driving car at the close-up: frame 76, body crop, grain
    y0, y1, x0, x1 = 380, 760, 250, 1250
    cb, ca = m.img('rt5b_r15v_car_before', 76), m.img('rt5b_r15v_car_after', 76)
    print('car body at frame 76: grain before %.2f%%, after %.2f%%; |after - before| mean %.2f levels over the body'
          % (grain(cb, y0, y1, x0, x1), grain(ca, y0, y1, x0, x1), np.abs(L(ca) - L(cb))[y0:y1, x0:x1].mean()))
    gb = np.mean([grain(m.img('rt5b_r15v_car_before', k), y0, y1, x0, x1) for k in range(55, 90)])
    ga = np.mean([grain(m.img('rt5b_r15v_car_after', k), y0, y1, x0, x1) for k in range(55, 90)])
    print('car body while driving (frames 55-89): grain before %.2f%%, after %.2f%%' % (gb, ga))
    ccirc = ((x1 - x0) // 2, (y1 - y0) // 2, 190)
    made.append(m.sheet(os.path.join(OUT, 'car_driving.png'),
                        'The car driving at the close-up (1 m/s), frame 76, TAA -- look inside the circle at the body (normal brightness)',
                        [cb[y0:y1, x0:x1], ca[y0:y1, x0:x1]],
                        ['BEFORE (STATE A): grain %.1f%% of body pixels.' % grain(cb, y0, y1, x0, x1),
                         'AFTER: grain %.1f%%; the body\'s reflections now come from four rays a texel while it moves.' % grain(ca, y0, y1, x0, x1)],
                        circle=ccirc, gain=1.0, scale=1))
    # --- the sphere crossing: frame 121
    sy0, sy1, sx0, sx1 = 250, 470, 560, 900
    sb, sa_ = m.img('r15v_sph_before', 121), m.img('r15v_sph_after', 121)
    print('sphere at frame 121: grain before %.2f%%, after %.2f%%' % (grain(sb, sy0, sy1, sx0, sx1), grain(sa_, sy0, sy1, sx0, sx1)))
    made.append(m.sheet(os.path.join(OUT, 'sphere_moving.png'),
                        'The chrome sphere (roughness 0.05) crossing at 3 m/s, frame 121, TAA -- look inside the circle (brightened x2)',
                        [sb[sy0:sy1, sx0:sx1], sa_[sy0:sy1, sx0:sx1]],
                        ['BEFORE (STATE A): grain %.1f%%.' % grain(sb, sy0, sy1, sx0, sx1), 'AFTER: grain %.1f%%.' % grain(sa_, sy0, sy1, sx0, sx1)],
                        circle=((sx1 - sx0) // 2, (sy1 - sy0) // 2, 100), gain=2.0, scale=2))
    for f in made:
        print(f)


PASS_NAMES = ('ReflectionTrace', 'ReflectionResolve', 'ReflectionAccumulate', 'DirectAccumulate', 'Scene')


def cost():
    os.makedirs(OUT, exist_ok=True)
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'], cwd=stage_run.ROOT, capture_output=True, check=True).stdout
    staged = {v: stage_run.build_variant(v) for v in ('nomark', 'ship')}
    try:
        io.open(os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE), 'wb').write(head)
        burst.SCENE = stage_run.HEAD_SCENE
        os.environ['BURST_SLIDE'] = '=porsche_992_gt3_r|0.5|12.0'
        burst.make_scene(0.0, 0.1)
        rows = {a: [] for a in ARMS}
        for i, arm in enumerate(['before', 'after', 'after', 'before']):
            if not stage_run.restore():
                sys.exit('staged copies did not restore')
            variant, flags = ARMS[arm]
            for shader, text in staged[variant].items():
                io.open(os.path.join(stage_run.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
            cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'), '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                   '--scene=scenes/showroom_burst.rage', '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                   '--width=1600', '--height=900', '--benchmark=240', '--frame-time=0.0166', '--import-cache=off',
                   '--pass-timings=on', '--camera=' + s2.CAMS['owner']] + flags
            p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=1800, errors='replace')
            text = p.stdout + p.stderr
            io.open(os.path.join(OUT, 'cost_%d_%s.log' % (i, arm)), 'w', encoding='utf-8').write(text)
            mm = re.search(r'frame\s+mean\s+([0-9.]+) ms', text)
            r = {'frame': float(mm.group(1)) if mm else 0.0}
            for name in PASS_NAMES:
                pm = re.search(r'\[benchmark\]\s+(?:[A-Za-z]+/)?%s\s+([0-9.]+)\s+([0-9.]+)' % re.escape(name), text)
                if pm:
                    r[name] = float(pm.group(2))
            rows[arm].append(r)
            print('run %d %-6s ' % (i, arm) + '  '.join('%s %.3f' % (k, v) for k, v in r.items()))
            sys.stdout.flush()
        for arm in ARMS:
            keys = sorted({k for r in rows[arm] for k in r})
            print('  %-6s ' % arm + '  '.join('%s %.3f' % (k, statistics.mean(r.get(k, 0.0) for r in rows[arm])) for k in keys))
    finally:
        os.environ.pop('BURST_SLIDE', None)
        print('staged copies restored and identical to source:', stage_run.restore())
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta', 'showroom_burst.rage', 'showroom_burst.rage.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass


if __name__ == '__main__':
    {'render': render, 'sheets': sheets, 'cost': cost}[sys.argv[1]]()
