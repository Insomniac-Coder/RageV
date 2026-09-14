# -*- coding: utf-8 -*-
"""Measured change, phase 1: what the check costs in the garage, parked.

HEAD's showroom at the owner's shot, `--benchmark=240`, the check off against on,
as a palindrome (off on on off off on on off) because this laptop's GPU drifts
and the first run of a pair reads fast; the spread is quoted.

Usage: mc_cost.py [moving]

`moving` puts the camera on the engine's Slider at 0.4 m/s for the whole run
(burst.py's dolly), so the record is retaken every frame.
"""
import io, os, re, statistics, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
import stage_run  # noqa: E402
sys.path.insert(0, os.path.join(HERE, '..'))
import burst  # noqa: E402

ORDER = ['off', 'on', 'on', 'off', 'off', 'on', 'on', 'off']
LOGS = os.path.join(stage_run.OUT, 'mc', 'cost')
PASSES = re.compile(r'scene/(DirectRelight|DirectChangeFilter\d*|DirectChangeMap|DirectRecord|ReflectionRelight|ReflectionChangeFilter\d*|ReflectionChangeMap|ReflectionRecord|GiRelight|GiChangeFilter\d*|GiChangeMap|GiRecord)'
                    r'\s+[\d.]+\s+([\d.]+)')


def main(moving=False, extra=()):
    os.makedirs(LOGS, exist_ok=True)
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=stage_run.ROOT, capture_output=True, check=True).stdout
    scene_path = os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE)
    frames = {'off': [], 'on': []}
    passes = {}
    try:
        io.open(scene_path, 'wb').write(head)
        scene_name = stage_run.HEAD_SCENE
        if moving:
            burst.SCENE = stage_run.HEAD_SCENE
            burst.make_scene(0.4, 1000.0)
            scene_name = 'showroom_burst.rage'
        for i, arm in enumerate(ORDER):
            if not stage_run.restore():
                sys.exit('staged copies did not restore before run %d' % i)
            cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'), '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                   '--scene=scenes/' + scene_name, '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                   '--width=1600', '--height=900', '--benchmark=240', '--frame-time=0.0166', '--import-cache=off',
                   '--camera=-2.3,0.72,-2,11,0,4'] + ['--measured-change=' + arm] + list(extra)
            p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=1800, errors='replace')
            io.open(os.path.join(LOGS, 'run%d_%s.log' % (i, arm)), 'w', encoding='utf-8').write(p.stdout + p.stderr)
            frame = re.search(r'frame\s+mean ([\d.]+) ms', p.stdout)
            if not frame:
                print(p.stdout[-2000:])
                sys.exit('run %d (%s): no benchmark lines' % (i, arm))
            frames[arm].append(float(frame.group(1)))
            found = PASSES.findall(p.stdout)
            for name, ms in found:
                passes.setdefault(name, []).append(float(ms))
            print('run %d %-3s frame %.3f ms  %s' % (i, arm, frames[arm][-1],
                                                    ' '.join('%s %.3f' % (n, float(m)) for n, m in found)))
            sys.stdout.flush()
    finally:
        print('staged copies restored and identical to source:', stage_run.restore())
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta',
                  'showroom_burst.rage', 'showroom_burst.rage.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass
    for arm, rows in frames.items():
        print('%-3s frame %.3f +- %.3f ms' % (arm, statistics.mean(rows), statistics.stdev(rows)))
    total = 0.0
    for name, rows in passes.items():
        total += statistics.mean(rows)
        print('  %-20s %.3f +- %.3f ms' % (name, statistics.mean(rows), statistics.stdev(rows) if len(rows) > 1 else 0.0))
    print('  passes together %.3f ms' % total)


if __name__ == '__main__':
    rest = sys.argv[1:]
    moving = bool(rest) and rest[0] == 'moving'
    main(moving=moving, extra=rest[1:] if moving else rest)
