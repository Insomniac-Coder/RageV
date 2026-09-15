# -*- coding: utf-8 -*-
"""Whole-scene arms (owner, 2026-09-15 night: speckles on the wall under the cube while it crosses
the car, edge jitter on the chrome pipes, speckles elsewhere).

Every arm: the owner's camera, the whole frame, the chrome cube at the scene's roughness 0.12
crossing the car (3 m/s from x=-9), the project's TAA, frames 100..140.

  a   HEAD 3432e08: the locked B1 diff reverse-applied to the engine files, rebuilt, rendered,
      re-applied, rebuilt, and the tree checked against B1 again (`head`)
  b   B1 as it stands
  c   B1 without the moving layer added after TAA: the composite adds the whole picture before
      the resolve and the pass after it adds nothing
  d   B1 without the hit's specular half and the probe at the hit (reflection and water rays;
      the bounce never had either -- ShadeTraced's probe is unread there)
  e   B1 without edit 4: no silhouette-mover mark, one ray a texel

Sheets per arm (build/rt15/metal/arms/): frame 120 with the wall under the cube and a chrome
pipe circled, the frame-to-frame difference x4, and the numbers: mean frame-to-frame change
on the wall under the cube, on the pipe, and over the rest of the frame.

Usage: rt15_scene_arms.py render | head | sheets
"""
import hashlib, io, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import rt15_cube_metal as m  # noqa: E402

stage_run, s2, sw, it = m.stage_run, m.s2, m.sw, m.it
N = '\n'
TRACE, INC, ACC = 'reflection_trace.rvshader', 'include/pbr_fragment.glsl', 'reflection_accumulate.rvshader'
COMP, COMPM, WATER = 'reflection_composite.rvshader', 'reflection_composite_moving.rvshader', 'water_trace.rvshader'
stage_run.VARIANTS['nopost'] = [
    (COMP, "\tif (u_Params.A > 0.5)" + N + "\t\tadded = max(added - max(texture(u_ReflectionMoving, uv).rgb, vec3(0.0)), vec3(0.0));" + N,
           "\tif (false)" + N + "\t\tadded = max(added - max(texture(u_ReflectionMoving, uv).rgb, vec3(0.0)), vec3(0.0));" + N),
    (COMPM, "\tconst vec3 added = moving.a > 0.0 ? max(moving.rgb, vec3(0.0)) : vec3(0.0);" + N, "\tconst vec3 added = vec3(0.0);" + N),
]
stage_run.VARIANTS['nospecall'] = [
    (TRACE, "#define RV_HIT_SPECULAR" + N, ""),
    (TRACE, "\tconst float hitProbe = ProbeSlotAt(hit.Position);" + N, "\tconst float hitProbe = 0.0;" + N),
    (INC, "#if !defined(RV_TRACE_ONLY) && !defined(RV_HIT_SPECULAR)" + N + "#define RV_HIT_SPECULAR" + N + "#endif" + N, ""),
    (WATER, "#define RV_HIT_SPECULAR" + N, ""),
    (WATER, "\tconst float hitProbe = ProbeSlotAt(hit.Position);" + N, "\tconst float hitProbe = 0.0;" + N),
]
stage_run.VARIANTS['noedit4'] = [(ACC, "const float kSilhouetteMoverMark = 0.125;" + N, "const float kSilhouetteMoverMark = 0.0;" + N)]
ARMS = {'b': ('ship', []), 'c': ('nopost', []), 'd': ('nospecall', []), 'e': ('noedit4', ['--reflection-moving-rays=1'])}
LABELS = {'a': 'HEAD 3432e08 (the pushed code)', 'b': 'STATE B1', 'c': 'B1, moving layer not added after TAA',
          'd': 'B1 without hit specular and probe-at-hit', 'e': 'B1 without edit 4 (no mark, one ray)'}
FIRST, COUNT = 100, 41
OUT = os.path.join(m.OUT, 'arms')
ROOT = stage_run.ROOT
DIFF = os.path.join(HERE, 'rt15_state_B1_LOCKED.diff')
CMAKE = r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
B1_MD5 = {'RageVEditor/assets/shaders/reflection_trace.rvshader': '211a971f769acc08d34776446739bfaa',
          'RageVEditor/assets/shaders/reflection_accumulate.rvshader': '854b625dfc89c8faa36cf60a9ac9c1a8',
          'RageVEditor/assets/shaders/include/pbr_fragment.glsl': '43c785d212ada7e504f1fe9b3838f511'}


def render(arms=None):
    for arm in (arms or ['b', 'c', 'd', 'e']):
        variant, flags = ARMS[arm]
        s2.arms_at('owner', [('r15arm_%s' % arm, variant, flags, dict(frames=COUNT, first=FIRST, cube=(-9.0, 3.0, 6.0)))])


def git(*args):
    return subprocess.run(['git'] + list(args), cwd=ROOT, capture_output=True, text=True)


def build():
    p = subprocess.run([CMAKE, '--build', 'build', '--config', 'Release', '--', '-m'], cwd=ROOT, capture_output=True, text=True, errors='replace')
    errors = [l for l in p.stdout.splitlines() if 'error C' in l or 'Build FAILED' in l]
    return p.returncode == 0 and not errors, errors[:5]


def md5(rel):
    return hashlib.md5(io.open(os.path.join(ROOT, rel), 'rb').read()).hexdigest()


def head():
    """HEAD's engine: reverse-apply the locked diff, build, render, re-apply, build, check."""
    chk = git('apply', '-R', '--check', DIFF)
    if chk.returncode != 0:
        sys.exit('the locked diff does not reverse-apply cleanly:\n' + chk.stderr)
    r = git('apply', '-R', DIFF)
    if r.returncode != 0:
        sys.exit('reverse apply failed:\n' + r.stderr)
    print('locked diff reverse-applied: engine files are HEAD\'s')
    try:
        ok, errs = build()
        print('build of HEAD:', 'ok' if ok else 'FAILED %s' % errs)
        if ok:
            s2.arms_at('owner', [('r15arm_a', 'ship', [], dict(frames=COUNT, first=FIRST, cube=(-9.0, 3.0, 6.0)))])
    finally:
        r = git('apply', DIFF)
        print('locked diff re-applied:', 'ok' if r.returncode == 0 else 'FAILED ' + r.stderr)
        ok, errs = build()
        print('build of B1:', 'ok' if ok else 'FAILED %s' % errs)
        for rel, h in B1_MD5.items():
            print('  %-60s %s' % (rel, 'B1' if md5(rel) == h else 'DIFFERS FROM B1'))
        print('staged copies restored and identical to source:', stage_run.restore())


L = it.L
# The regions, in the 1600x900 frame: the crop shown; the wall under the cube's path at frame
# 120 (the cube's face is y 318..417, x 642..788; the wall below it); a chrome pipe column found
# from the parked mean (the brightest vertical line left of the cube's path).
CROP = (300, 560, 520, 920)          # y0, y1, x0, x1
WALL = (430, 520, 640, 800)
REST_EXCLUDE = (290, 440)            # the cube's rows, left out of "the rest of the frame"


def pipe_column():
    parked = m.mean_npy('r15m_b_parked')
    prof = L(parked)[300:560, 520:640].mean(axis=0)
    return 520 + int(np.argmax(prof))


def sheets(arms=('a', 'b', 'c', 'd', 'e')):
    os.makedirs(OUT, exist_ok=True)
    px = pipe_column()
    PIPE = (300, 560, px - 6, px + 7)
    print('chrome pipe column at x=%d' % px)
    y0, y1, x0, x1 = CROP
    print('%-4s %-44s %12s %12s %12s' % ('arm', 'look', 'wall change', 'pipe change', 'rest change'))
    for arm in arms:
        tag = 'r15arm_%s' % arm
        if not os.path.exists(os.path.join(stage_run.SHOTS, 'rt5b_%s_120.png' % tag)):
            print('%s: no frames' % arm)
            continue
        frames = [m.img('rt5b_' + tag, k) for k in range(FIRST, FIRST + COUNT)]
        wall, pipe, rest = [], [], []
        for i in range(1, len(frames)):
            d = np.abs(L(frames[i]) - L(frames[i - 1]))
            wall.append(d[WALL[0]:WALL[1], WALL[2]:WALL[3]].mean())
            pipe.append(d[PIPE[0]:PIPE[1], PIPE[2]:PIPE[3]].mean())
            r = d[:860].copy()
            r[REST_EXCLUDE[0]:REST_EXCLUDE[1], :] = np.nan
            rest.append(np.nanmean(r))
        print('%-4s %-44s %12.2f %12.2f %12.2f' % (arm, LABELS[arm], np.mean(wall), np.mean(pipe), np.mean(rest)))
        f120, f119 = frames[120 - FIRST], frames[119 - FIRST]
        diff = np.repeat(np.clip(np.abs(f120 - f119).max(axis=2) * 4.0, 0, 255)[..., None], 3, axis=2)
        panels = [f120[y0:y1, x0:x1], diff[y0:y1, x0:x1]]
        # circles: the wall under the cube, and the pipe
        from PIL import ImageDraw
        sheet = m.sheet(os.path.join(OUT, 'arm_%s.png' % arm),
                        '%s -- frame 120, the cube crossing the car (roughness 0.12, TAA): the frame at normal brightness | frame 120 minus 119, x4' % LABELS[arm],
                        panels, ['Circles: the wall under the cube (large) and a chrome pipe (small). Wall change frame to frame %.2f levels, pipe %.2f, rest of frame %.2f.' % (np.mean(wall), np.mean(pipe), np.mean(rest)),
                                 'Difference between consecutive frames, x4: speckle shows as scattered dots, edge jitter as bright outlines.'],
                        circle=None, gain=1.0, scale=2)
        im = Image.open(sheet)
        d = ImageDraw.Draw(im)
        for col in range(2):
            ox = col * ((x1 - x0) * 2 + 12)
            cx, cy = ((WALL[2] + WALL[3]) // 2 - x0) * 2 + ox, ((WALL[0] + WALL[1]) // 2 - y0) * 2 + 44
            d.ellipse([cx - 110, cy - 70, cx + 110, cy + 70], outline=(255, 230, 0), width=3)
            pcx, pcy = (px - x0) * 2 + ox, ((PIPE[0] + PIPE[1]) // 2 - y0) * 2 + 44
            d.ellipse([pcx - 30, pcy - 120, pcx + 30, pcy + 120], outline=(255, 230, 0), width=3)
        im.save(sheet)
        print(sheet)


if __name__ == '__main__':
    {'render': lambda: render(), 'head': head, 'sheets': lambda: sheets()}[sys.argv[1]]()
