# -*- coding: utf-8 -*-
"""RT-18: the silhouette exemption only where nothing moves -- which "moves"?

The accumulator keeps a silhouette texel's own history untested so a parked edge
flipping sides under the jitter averages into its coverage (WR-16 R5). Moving, that
history is the other side's picture and is left inside the object (the cube's stripes,
the car's trail; rt18_silhouette_test.py). Two readings of "moving":

  silany     any motion in the 3x3 (the velocity lane's length) -- a camera move counts
  silobject  an object moving on its own in the 3x3 (ObjectShift on each neighbour) --
             a camera move alone keeps the exemption, exactly as shipped

Cases: the garage parked, the camera dolly (burst.py's 0.6 m/s), the cube crossing, the
car driving, and the bridge's deck, pier and glitter cameras (the sea's accumulate runs
the same code, and the sea moves).

Usage: rt18_test.py render [garage|bridge]  |  rt18_test.py analyse [garage|bridge]
"""
import io, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_14'))
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
import stage_run  # noqa: E402
import rt13_stage2 as s2  # noqa: E402

ACC = 'reflection_accumulate.rvshader'
N = '\n'
GLOBAL = (ACC, 'float g_BlendFrames = 1.0;' + N, 'float g_BlendFrames = 1.0;' + N + 'bool g_SilhouetteMoving = false;' + N)
EXEMPT = (ACC, '\t\tif (!(silhouette && k == 0))' + N, '\t\tif (!(silhouette && k == 0 && !g_SilhouetteMoving))' + N)
stage_run.VARIANTS['silany'] = [GLOBAL, EXEMPT,
    (ACC, '\t\tconst bool silhouette = AtSilhouette(texel, size, P, N, eyeDistance, neighbourMotion);' + N,
          '\t\tconst bool silhouette = AtSilhouette(texel, size, P, N, eyeDistance, neighbourMotion);' + N
          + '\t\tg_SilhouetteMoving = neighbourMotion >= kMotionFloor;' + N)]
stage_run.VARIANTS['silobject'] = [GLOBAL, EXEMPT,
    (ACC, '\t\tconst vec2 objectShift = ObjectShift(texel, P, nowNdc);' + N,
          '\t\tconst vec2 objectShift = ObjectShift(texel, P, nowNdc);' + N
          + '\t\tif (silhouette)' + N
          + '\t\t{' + N
          + '\t\t\tfor (int y = -1; y <= 1 && !g_SilhouetteMoving; ++y)' + N
          + '\t\t\t\tfor (int x = -1; x <= 1 && !g_SilhouetteMoving; ++x)' + N
          + '\t\t\t\t{' + N
          + '\t\t\t\t\tconst ivec2 at = clamp(texel + ivec2(x, y), ivec2(0), size - 1);' + N
          + '\t\t\t\t\tvec3 Pn, Nn;' + N
          + '\t\t\t\t\tif (!SurfaceAt(at, size, Pn, Nn))' + N
          + '\t\t\t\t\t\tcontinue;' + N
          + '\t\t\t\t\tconst vec2 uvAt = (vec2(at) + 0.5) / vec2(size);' + N
          + '\t\t\t\t\tconst float rowAt = u_Reflection.History.w > 0.5 ? 1.0 - uvAt.y : uvAt.y;' + N
          + '\t\t\t\t\tconst vec2 shiftAt = ObjectShift(at, Pn, vec2(uvAt.x * 2.0 - 1.0, rowAt * 2.0 - 1.0));' + N
          + '\t\t\t\t\tg_SilhouetteMoving = dot(shiftAt, shiftAt) > 0.0;' + N
          + '\t\t\t\t}' + N
          + '\t\t}' + N)]
ARMS = ['ship', 'silany', 'silobject']

GARAGE = {
    'parked': ('owner', dict(frames=40, first=150)),
    'dolly': ('owner', dict(frames=60, first=60, speed=0.6, stop=1.5)),
    'cube': ('owner', dict(frames=40, first=100, cube=(-9.0, 3.0, 6.0))),
    'car': ('close', dict(frames=20, first=55, BURST_SLIDE=s2.CAR)),
}
BRIDGE = {
    'deck': '0,76.4,950,0.01,0,0',
    'pier': '70,4.5,705,0.01,-46.98,-2.86',
    'glitter': '500,2.5,180,0.01,-90,-1.146',
}
BRIDGE_SHOTS = os.path.join(stage_run.ROOT, 'build', 'rt18', 'bridge')
B_FIRST, B_COUNT = 136, 48


def render_garage():
    for case, (cam, opts) in GARAGE.items():
        s2.arms_at(cam, [('r18_%s_%s' % (case, arm), arm, [], dict(opts)) for arm in ARMS])


def render_bridge():
    os.makedirs(BRIDGE_SHOTS, exist_ok=True)
    staged = {arm: stage_run.build_variant(arm) for arm in ARMS}
    try:
        for cam, pose in BRIDGE.items():
            for arm in ARMS:
                if not stage_run.restore():
                    sys.exit('staged copies did not restore')
                for shader, text in staged[arm].items():
                    io.open(os.path.join(stage_run.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
                tag = '%s_%s' % (cam, arm)
                for f in os.listdir(BRIDGE_SHOTS):
                    if re.match(re.escape(tag) + r'_\d+\.png$', f):
                        os.remove(os.path.join(BRIDGE_SHOTS, f))
                cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'), '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                       '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                       '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off',
                       '--camera=' + pose, '--screenshot=' + os.path.join(BRIDGE_SHOTS, tag + '.png'),
                       '--screenshot-frame=%d' % B_FIRST, '--screenshot-count=%d' % B_COUNT]
                p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=3600, errors='replace')
                got = [f for f in os.listdir(BRIDGE_SHOTS) if re.match(re.escape(tag) + r'_\d+\.png$', f)]
                print('%-16s %d frames (exit %d)' % (tag, len(got), p.returncode))
                sys.stdout.flush()
    finally:
        print('staged copies restored and identical to source:', stage_run.restore())


def _lum(path):
    return np.asarray(Image.open(path).convert('L'), dtype=float)


def _series(path_of, frames):
    fr = [_lum(path_of(k)) for k in frames]
    return fr


def analyse_garage():
    S = lambda case, arm, k: os.path.join(stage_run.SHOTS, 'rt5b_r18_%s_%s_%d.png' % (case, arm, k))
    for case, (cam, opts) in GARAGE.items():
        ks = range(opts['first'], opts['first'] + opts['frames'])
        base = _series(lambda k: S(case, 'ship', k), ks)
        print(case)
        for arm in ARMS:
            fr = base if arm == 'ship' else _series(lambda k: S(case, arm, k), ks)
            change = np.mean([np.abs(fr[i] - fr[i - 1])[:860].mean() for i in range(1, len(fr))])
            if arm == 'ship':
                print('   %-10s frame-to-frame %.3f' % (arm, change))
                continue
            d = np.array([np.abs(a - b)[:860] for a, b in zip(fr, base)])
            print('   %-10s frame-to-frame %.3f   vs shipped: mean %.3f, px >4 %.3f%%, px >16 %.3f%%, max %.0f'
                  % (arm, change, d.mean(), (d > 4).mean() * 100, (d > 16).mean() * 100, d.max()))


def analyse_bridge():
    S = lambda cam, arm, k: os.path.join(BRIDGE_SHOTS, '%s_%s_%d.png' % (cam, arm, k))
    ks = range(B_FIRST, B_FIRST + B_COUNT)
    for cam in BRIDGE:
        base = _series(lambda k: S(cam, 'ship', k), ks)
        print(cam)
        for arm in ARMS:
            fr = base if arm == 'ship' else _series(lambda k: S(cam, arm, k), ks)
            change = np.mean([np.abs(fr[i] - fr[i - 1])[:860].mean() for i in range(1, len(fr))])
            if arm == 'ship':
                print('   %-10s frame-to-frame %.3f' % (arm, change))
                continue
            d = np.array([np.abs(a - b)[:860] for a, b in zip(fr, base)])
            mean_d = np.abs(np.mean(fr, axis=0) - np.mean(base, axis=0))[:860]
            print('   %-10s frame-to-frame %.3f   vs shipped per frame: mean %.3f, px >4 %.3f%%, max %.0f;'
                  ' mean images: px >2 %.3f%%, max %.1f' % (arm, change, d.mean(), (d > 4).mean() * 100, d.max(),
                                                          (mean_d > 2).mean() * 100, mean_d.max()))


if __name__ == '__main__':
    what = sys.argv[2] if len(sys.argv) > 2 else 'garage'
    if sys.argv[1] == 'render':
        render_garage() if what == 'garage' else render_bridge()
    else:
        analyse_garage() if what == 'garage' else analyse_bridge()
