# -*- coding: utf-8 -*-
"""RT-17: the reflection accumulator tests what its rays struck, by identity.

Arms (staged accumulators; the engine as built writes the identity lane in every arm):
  off      the identity test switched off (IdentityConfidence returns one) -- the picture
           before RT-17, since nothing else reads the new lane
  ship     RT-17 as built
  idprobe  as built, painting red where the identity test took trust away

Usage: rt17_test.py probe [render]         -- where it fires: parked garage and the car driving
       rt17_test.py garage [render]        -- off/ship: parked, dolly, cube, car (close), car (wide)
       rt17_test.py bridge [render]        -- off/ship: deck, pier, glitter
       rt17_test.py ghost [render]         -- rt17_ref's settled-pose comparison with RT-17 on
"""
import io, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_14'))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import stage_run  # noqa: E402
import rt13_stage2 as s2  # noqa: E402
import rt18_test as t18  # noqa: E402

ACC = 'reflection_accumulate.rvshader'
N = '\n'
OFF = (ACC, '\tif (u_Reflection.Change.y < 0.5)' + N + '\t\treturn 1.0;' + N,
       '\tif (true)' + N + '\t\treturn 1.0;' + N)
stage_run.VARIANTS['idoff'] = [OFF]
stage_run.VARIANTS['idprobe'] = [
    (ACC, 'bool g_SilhouetteMoving = false;' + N, 'bool g_SilhouetteMoving = false;' + N + 'float g_IdDiag = 1.0;' + N),
    (ACC, '\t\t\t\t\t\t\t  * material * idPenalty * hit * identity;' + N,
          '\t\t\t\t\t\t\t  * material * idPenalty * hit * identity;' + N + '\t\t\tg_IdDiag = identity;' + N),
    (ACC, '\t\t\t\tkept2 = mix(held2, fresh2.rgb, 1.0 / frames2);' + N,
          '\t\t\t\tkept2 = mix(held2, fresh2.rgb, 1.0 / frames2);' + N),
    (ACC, '\tconst uvec2 roundingPixel = uvec2(texel);' + N,
          '\tif (g_Kept) kept = mix(kept, vec3(8.0, 0.0, 0.0), 1.0 - g_IdDiag);' + N + '\tconst uvec2 roundingPixel = uvec2(texel);' + N),
]
CASES = dict(t18.GARAGE)
CASES['carwide'] = ('owner', dict(frames=40, first=50, BURST_SLIDE=s2.CAR))
S = lambda tag, k: os.path.join(stage_run.SHOTS, 'rt5b_%s_%d.png' % (tag, k))
L = lambda p: np.asarray(Image.open(p).convert('L'), dtype=float)[:860]
OUT = os.path.join(stage_run.ROOT, 'build', 'rt17')


def probe(render):
    cases = {'parked': CASES['parked'], 'car': CASES['car'], 'carwide': CASES['carwide']}
    if render:
        for case, (cam, opts) in cases.items():
            s2.arms_at(cam, [('r17p_%s' % case, 'idprobe', [], dict(opts, frames=12))])
    os.makedirs(OUT, exist_ok=True)
    for case, (cam, opts) in cases.items():
        k = opts['first'] + 8
        a = np.asarray(Image.open(S('r17p_%s' % case, k)).convert('RGB'), dtype=float)[:860]
        red = (a[..., 0] > 120) & (a[..., 0] > 2.5 * a[..., 1]) & (a[..., 0] > 2.5 * a[..., 2])
        print('%-8s frame %d: pixels painted red (the identity test fired): %.3f%%' % (case, k, red.mean() * 100))
        Image.fromarray(a.astype(np.uint8)).save(os.path.join(OUT, 'probe_%s_%d.png' % (case, k)))


def garage(render):
    if render:
        for case, (cam, opts) in CASES.items():
            s2.arms_at(cam, [('r17_%s_%s' % (case, arm), variant, [], dict(opts))
                             for arm, variant in (('off', 'idoff'), ('on', 'ship'))])
    for case, (cam, opts) in CASES.items():
        ks = range(opts['first'], opts['first'] + opts['frames'])
        off = [L(S('r17_%s_off' % case, k)) for k in ks]
        on = [L(S('r17_%s_on' % case, k)) for k in ks]
        ch = lambda fr: np.mean([np.abs(fr[i] - fr[i - 1]).mean() for i in range(1, len(fr))])
        d = np.array([np.abs(a - b) for a, b in zip(on, off)])
        print('%-8s frame-to-frame off %.3f on %.3f   on vs off: mean %.3f, px >4 %.3f%%, px >16 %.3f%%, max %.0f'
              % (case, ch(off), ch(on), d.mean(), (d > 4).mean() * 100, (d > 16).mean() * 100, d.max()))


def bridge(render):
    shots = os.path.join(stage_run.ROOT, 'build', 'rt17', 'bridge')
    if render:
        os.makedirs(shots, exist_ok=True)
        staged = {arm: stage_run.build_variant(v) for arm, v in (('off', 'idoff'), ('on', 'ship'))}
        try:
            for cam, pose in t18.BRIDGE.items():
                for arm in ('off', 'on'):
                    if not stage_run.restore():
                        sys.exit('restore failed')
                    for shader, text in staged[arm].items():
                        io.open(os.path.join(stage_run.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
                    tag = '%s_%s' % (cam, arm)
                    cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'), '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                           '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                           '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off',
                           '--camera=' + pose, '--screenshot=' + os.path.join(shots, tag + '.png'),
                           '--screenshot-frame=%d' % t18.B_FIRST, '--screenshot-count=%d' % t18.B_COUNT]
                    p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=3600, errors='replace')
                    print('%-12s exit %d' % (tag, p.returncode))
                    sys.stdout.flush()
        finally:
            print('staged copies restored and identical to source:', stage_run.restore())
    ks = range(t18.B_FIRST, t18.B_FIRST + t18.B_COUNT)
    for cam in t18.BRIDGE:
        off = [L(os.path.join(shots, '%s_off_%d.png' % (cam, k))) for k in ks]
        on = [L(os.path.join(shots, '%s_on_%d.png' % (cam, k))) for k in ks]
        ch = lambda fr: np.mean([np.abs(fr[i] - fr[i - 1]).mean() for i in range(1, len(fr))])
        d = np.array([np.abs(a - b) for a, b in zip(on, off)])
        print('%-8s frame-to-frame off %.3f on %.3f   on vs off: mean %.3f, px >4 %.3f%%, max %.0f'
              % (cam, ch(off), ch(on), d.mean(), (d > 4).mean() * 100, d.max()))


if __name__ == '__main__':
    render = 'render' in sys.argv
    {'probe': lambda: probe(render), 'garage': lambda: garage(render), 'bridge': lambda: bridge(render)}[sys.argv[1]]()
