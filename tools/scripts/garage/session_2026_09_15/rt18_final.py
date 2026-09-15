# -*- coding: utf-8 -*-
"""RT-18 as committed to the source: the same pictures as the tested variant, what it costs,
and the two ways to smooth the band the moving cube uncovers each frame.

  verify   the shipped source against rt18_test.py's silobject arms (cube, car, parked, dolly)
  banding  --reflection-blur=6 / 12 on top of RT-18: the cube and the car, and the dolly's floor
           (what the young blur costs a moving camera's detail, the reason it was retired)
  cost     --benchmark=240 --pass-timings=on, HEAD's accumulator against the source, off on on off,
           the owner's shot parked and with the cube crossing

Usage: rt18_final.py verify|banding|cost [render]
"""
import io, os, re, statistics, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_14'))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
import stage_run  # noqa: E402
import rt13_stage2 as s2  # noqa: E402
burst = s2.burst
import rt18_test as t18  # noqa: E402

ACC = 'reflection_accumulate.rvshader'
HEAD_ACC = subprocess.run(['git', 'show', 'HEAD:RageVEditor/assets/shaders/' + ACC], cwd=stage_run.ROOT,
                          capture_output=True, check=True).stdout.decode('utf-8')
stage_run.VARIANTS['headacc'] = [(ACC, None, HEAD_ACC)]
S = lambda tag, k: os.path.join(stage_run.SHOTS, 'rt5b_%s_%d.png' % (tag, k))
L = lambda p: np.asarray(Image.open(p).convert('L'), dtype=float)


def verify(render):
    if render:
        for case, (cam, opts) in t18.GARAGE.items():
            s2.arms_at(cam, [('r18_%s_final' % case, 'ship', [], dict(opts))])
    for case, (cam, opts) in t18.GARAGE.items():
        ks = range(opts['first'], opts['first'] + opts['frames'])
        d = max(np.abs(L(S('r18_%s_final' % case, k)) - L(S('r18_%s_silobject' % case, k))).max() for k in ks)
        print('%-7s source vs the tested variant: largest pixel difference over the run %.0f levels' % (case, d))


def banding(render):
    cases = {'cube': t18.GARAGE['cube'], 'car': t18.GARAGE['car'], 'dolly': t18.GARAGE['dolly']}
    if render:
        for case, (cam, opts) in cases.items():
            s2.arms_at(cam, [('r18_%s_blur%d' % (case, r), 'ship', ['--reflection-blur=%d' % r], dict(opts)) for r in (6, 12)])
    boxes = {'cube': (300, 420, 600, 840), 'car': (380, 760, 250, 1250), 'dolly': (430, 860, 0, 1600)}
    for case, (cam, opts) in cases.items():
        y0, y1, x0, x1 = boxes[case]
        print(case)
        for tag in ('final', 'blur6', 'blur12'):
            grain, detail, change, prev = [], [], [], None
            for k in range(opts['first'], opts['first'] + opts['frames']):
                im = Image.open(S('r18_%s_%s' % (case, tag), k)).convert('L')
                a = np.asarray(im, dtype=float)
                med = np.asarray(im.filter(ImageFilter.MedianFilter(3)), dtype=float)
                g = np.abs(a - med)[y0:y1, x0:x1]
                grain.append((g > 16).mean() * 100)
                # Detail: the high-frequency energy a blur takes -- the picture against a 5x5 box.
                box = np.asarray(im.filter(ImageFilter.BoxBlur(2)), dtype=float)
                detail.append(np.abs(a - box)[y0:y1, x0:x1].mean())
                if prev is not None:
                    change.append(np.abs(a - prev)[y0:y1, x0:x1].mean())
                prev = a
            print('   %-7s grain %.2f%%   detail %.3f   frame-to-frame %.3f' % (tag, np.mean(grain), np.mean(detail), np.mean(change)))
        k = opts['first'] + opts['frames'] // 2
        tiles = []
        for tag in ('final', 'blur6', 'blur12'):
            t = np.asarray(Image.open(S('r18_%s_%s' % (case, tag), k)).convert('RGB'))[y0:y1, x0:x1]
            tiles.append(t.repeat(3, 0).repeat(3, 1) if case == 'cube' else t)
        s2._sheet(os.path.join(stage_run.ROOT, 'build', 'rt18', 'banding_%s_%d.png' % (case, k)), tiles,
                  ['RT-18', 'RT-18 + young blur 6', 'RT-18 + young blur 12'])


PASSES = re.compile(r'scene/(ReflectionAccumulate|DirectAccumulate|OcclusionAccumulate|GiAccumulate)\s+[\d.]+\s+([\d.]+)')


def cost():
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'], cwd=stage_run.ROOT,
                          capture_output=True, check=True).stdout
    staged = {'head': stage_run.build_variant('headacc'), 'rt18': {}}
    try:
        for case in ('parked', 'cube'):
            scene = stage_run.moving_cube_scene(head, -9.0, 3.0, 60.0) if case == 'cube' else head
            io.open(os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE), 'wb').write(scene)
            burst.SCENE = stage_run.HEAD_SCENE
            burst.make_scene(0.0, 0.1)
            frames, passes = {'head': [], 'rt18': []}, {}
            for i, arm in enumerate(['head', 'rt18', 'rt18', 'head']):
                if not stage_run.restore():
                    sys.exit('restore failed')
                for shader, text in staged[arm].items():
                    io.open(os.path.join(stage_run.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
                cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'), '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                       '--scene=scenes/showroom_burst.rage', '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                       '--width=1600', '--height=900', '--benchmark=240', '--frame-time=0.0166', '--import-cache=off',
                       '--pass-timings=on', '--camera=' + s2.CAMS['owner']]
                p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=1800, errors='replace')
                fr = re.search(r'frame\s+mean ([\d.]+) ms', p.stdout)
                if not fr:
                    print(p.stdout[-1500:])
                    sys.exit('no benchmark lines')
                frames[arm].append(float(fr.group(1)))
                for name, gpu in PASSES.findall(p.stdout):
                    passes.setdefault((arm, name), []).append(float(gpu))
                print('%s run %d %-4s frame %.3f' % (case, i, arm, frames[arm][-1]))
                sys.stdout.flush()
            for arm in ('head', 'rt18'):
                print('%s %-4s frame %.3f ms  %s' % (case, arm, statistics.mean(frames[arm]),
                      '  '.join('%s %.3f' % (n, statistics.mean(r)) for (a, n), r in sorted(passes.items()) if a == arm)))
    finally:
        print('staged copies restored and identical to source:', stage_run.restore())
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta', 'showroom_burst.rage', 'showroom_burst.rage.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass


if __name__ == '__main__':
    render = 'render' in sys.argv
    {'verify': lambda: verify(render), 'banding': lambda: banding(render), 'cost': cost}[sys.argv[1]]()
