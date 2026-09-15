# -*- coding: utf-8 -*-
"""RT-15's three items as built: what they must not change, and what they cost.

  still   the parked garage (owner's shot, frames 150-189) and the camera dolly (0.6 m/s,
          frames 60-119): the build with every item off against the build as it stands, and
          the parked build against HEAD's own frames from this morning (rt5b_mem_parked_ship)
  bridge  the deck, pier and glitter cameras (48 frames from 136) against HEAD's
          (build/rt17/bridge/*_on_*, RT-17's arms, which rendered identical on and off)
  cost    --benchmark with the car driving across the owner's shot (0.5 m/s), and parked:
          every item off against the build, A B B A per scene, the reflection passes' GPU time

Usage: rt15_checks.py still [render] | bridge [render] | cost
"""
import io, os, re, statistics, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import rt15_items as it  # noqa: E402  -- registers i12off
import rt18_test as t18  # noqa: E402
import burst  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
OUT = os.path.join(stage_run.ROOT, 'build', 'rt15')
STILL = {
    'parked': ('owner', dict(frames=40, first=150)),
    'dolly': ('owner', dict(frames=60, first=60, speed=0.6, stop=1.5)),
}
BRIDGE_SHOTS = os.path.join(OUT, 'bridge')
S = lambda tag, k: os.path.join(stage_run.SHOTS, 'rt5b_%s_%d.png' % (tag, k))
load = lambda p: np.asarray(Image.open(p).convert('RGB'), dtype=int)


def still(render):
    if render:
        for case, (cam, opts) in STILL.items():
            s2.arms_at(cam, [('r15c_%s_%s' % (case, arm), variant, flags, dict(opts))
                             for arm, variant, flags in (('before', 'i12off', it.NOBLUR), ('ship', 'ship', []))])
    for case, (cam, opts) in STILL.items():
        ks = range(opts['first'], opts['first'] + opts['frames'])
        d = [np.abs(load(S('r15c_%s_ship' % case, k)) - load(S('r15c_%s_before' % case, k))) for k in ks]
        print('%-7s build vs every item off: largest pixel difference %d, pixels differing %.4f%%'
              % (case, max(x.max() for x in d), np.mean([(x > 0).any(axis=2).mean() for x in d]) * 100))
    d = [np.abs(load(S('r15c_parked_ship', k)) - load(S('mem_parked_ship', k))) for k in range(150, 190)]
    print('parked  build vs HEAD (this morning): largest pixel difference %d, pixels differing %.4f%%'
          % (max(x.max() for x in d), np.mean([(x > 0).any(axis=2).mean() for x in d]) * 100))


def bridge(render):
    if render:
        os.makedirs(BRIDGE_SHOTS, exist_ok=True)
        try:
            for cam, pose in t18.BRIDGE.items():
                if not stage_run.restore():
                    sys.exit('staged copies did not restore')
                tag = '%s_ship' % cam
                for f in os.listdir(BRIDGE_SHOTS):
                    if re.match(re.escape(tag) + r'_\d+\.png$', f):
                        os.remove(os.path.join(BRIDGE_SHOTS, f))
                cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'), '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                       '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                       '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off',
                       '--camera=' + pose, '--screenshot=' + os.path.join(BRIDGE_SHOTS, tag + '.png'),
                       '--screenshot-frame=%d' % t18.B_FIRST, '--screenshot-count=%d' % t18.B_COUNT]
                p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=3600, errors='replace')
                got = [f for f in os.listdir(BRIDGE_SHOTS) if re.match(re.escape(tag) + r'_\d+\.png$', f)]
                print('%-14s %d frames (exit %d)' % (tag, len(got), p.returncode))
                sys.stdout.flush()
        finally:
            print('staged copies restored and identical to source:', stage_run.restore())
    head = os.path.join(stage_run.ROOT, 'build', 'rt17', 'bridge')
    for cam in t18.BRIDGE:
        d = [np.abs(load(os.path.join(BRIDGE_SHOTS, '%s_ship_%d.png' % (cam, k)))
                    - load(os.path.join(head, '%s_on_%d.png' % (cam, k))))
             for k in range(t18.B_FIRST, t18.B_FIRST + t18.B_COUNT)]
        print('%-8s build vs HEAD: largest pixel difference %d, pixels differing %.4f%%'
              % (cam, max(x.max() for x in d), np.mean([(x > 0).any(axis=2).mean() for x in d]) * 100))


PASS_NAMES = ('ReflectionTrace', 'ReflectionResolve', 'ReflectionAccumulate', 'ReflectionBlur', 'ReflectionBlur2',
              'ReflectionBlur4', 'ReflectionComposite', 'Scene')


def cost():
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=stage_run.ROOT, capture_output=True, check=True).stdout
    staged = {v: stage_run.build_variant(v) for v in ('i12off', 'ship')}
    arms = {'before': ('i12off', it.NOBLUR), 'ship': ('ship', [])}
    try:
        io.open(os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE), 'wb').write(head)
        burst.SCENE = stage_run.HEAD_SCENE
        for scene, slide in (('driving', '=porsche_992_gt3_r|0.5|12.0'), ('parked', None)):
            if slide:
                os.environ['BURST_SLIDE'] = slide
            else:
                os.environ.pop('BURST_SLIDE', None)
            burst.make_scene(0.0, 0.1)
            rows = {a: [] for a in arms}
            for i, arm in enumerate(['before', 'ship', 'ship', 'before']):
                if not stage_run.restore():
                    sys.exit('staged copies did not restore')
                variant, flags = arms[arm]
                for shader, text in staged[variant].items():
                    io.open(os.path.join(stage_run.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
                cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'),
                       '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                       '--scene=scenes/showroom_burst.rage', '--rhi=vulkan', '--render-defaults=off',
                       '--vsync=off', '--width=1600', '--height=900', '--benchmark=300',
                       '--frame-time=0.0166', '--import-cache=off', '--pass-timings=on',
                       '--camera=' + s2.CAMS['owner']] + flags
                p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=1800, errors='replace')
                text = p.stdout + p.stderr
                io.open(os.path.join(OUT, 'cost_%s_%d_%s.log' % (scene, i, arm)), 'w', encoding='utf-8').write(text)
                m = re.search(r'frame\s+mean\s+([0-9.]+) ms', text)
                if not m:
                    print(text[-2000:])
                    sys.exit('%s run %d: no benchmark report' % (scene, i))
                r = {'frame': float(m.group(1))}
                g = re.search(r'whole frame \(GPU\)\s+([0-9.]+) ms', text)
                if g:
                    r['gpu'] = float(g.group(1))
                for name in PASS_NAMES:
                    pm = re.search(r'\[benchmark\]\s+(?:[A-Za-z]+/)?%s\s+([0-9.]+)\s+([0-9.]+)' % re.escape(name), text)
                    if pm:
                        r[name] = float(pm.group(2))
                rows[arm].append(r)
                print('%-7s run %d %-6s frame %.3f  gpu %.3f  %s' % (scene, i, arm, r['frame'], r.get('gpu', 0),
                      '  '.join('%s %.3f' % (n, r[n]) for n in PASS_NAMES if n in r)))
                sys.stdout.flush()
            for arm in arms:
                keys = sorted({k for r in rows[arm] for k in r})
                print('  %-7s %-6s %s' % (scene, arm, '  '.join('%s %.3f' % (k, statistics.mean(r.get(k, 0.0) for r in rows[arm]))
                                                                for k in keys)))
    finally:
        os.environ.pop('BURST_SLIDE', None)
        print('staged copies restored and identical to source:', stage_run.restore())
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta', 'showroom_burst.rage', 'showroom_burst.rage.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass


if __name__ == '__main__':
    render = 'render' in sys.argv
    {'still': lambda: still(render), 'bridge': lambda: bridge(render), 'cost': cost}[sys.argv[1]]()
