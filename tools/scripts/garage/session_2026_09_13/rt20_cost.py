# -*- coding: utf-8 -*-
"""RT-20: what the kept fix costs -- HEAD's resolve against the source's, parked.

The garage from HEAD at the owner's shot, `--benchmark=240`. This laptop's GPU
drifts and the first run of a pair reads fast, so the arms run as a palindrome,
head F F head head F F head, and the spread is quoted (project_ragev_build_and_run).

Usage: rt20_cost.py            run and print
"""
import io, os, re, statistics, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stage_run  # noqa: E402
import rt20_arms  # noqa: E402,F401  (registers 'head')

ORDER = ['head', 'F', 'F', 'head', 'head', 'F', 'F', 'head']
LOGS = os.path.join(stage_run.OUT, 'rt20', 'cost')


def main():
    os.makedirs(LOGS, exist_ok=True)
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=stage_run.ROOT, capture_output=True, check=True).stdout
    scene_path = os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE)
    got = {'head': [], 'F': []}
    try:
        io.open(scene_path, 'wb').write(head)
        for i, arm in enumerate(ORDER):
            if not stage_run.restore():
                sys.exit('staged copies did not restore before run %d' % i)
            if arm == 'head':
                for shader, text in stage_run.build_variant('head').items():
                    io.open(os.path.join(stage_run.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
            cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'), '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                   '--scene=scenes/' + stage_run.HEAD_SCENE, '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                   '--width=1600', '--height=900', '--benchmark=240', '--frame-time=0.0166', '--import-cache=off',
                   '--camera=-2.3,0.72,-2,11,0,4']
            p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=1800, errors='replace')
            io.open(os.path.join(LOGS, 'run%d_%s.log' % (i, arm)), 'w', encoding='utf-8').write(p.stdout + p.stderr)
            frame = re.search(r'frame\s+mean ([\d.]+) ms', p.stdout)
            taa = re.search(r'scene/TAA resolve\s+[\d.]+\s+([\d.]+)', p.stdout)
            if not frame or not taa:
                print(p.stdout[-2000:])
                sys.exit('run %d (%s): no benchmark lines' % (i, arm))
            got[arm].append((float(frame.group(1)), float(taa.group(1))))
            print('run %d %-4s frame %.3f ms, TAA resolve %.3f ms' % (i, arm, got[arm][-1][0], got[arm][-1][1]))
            sys.stdout.flush()
    finally:
        print('staged copies restored and identical to source:', stage_run.restore())
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass
    for arm, rows in got.items():
        f = [r[0] for r in rows]
        t = [r[1] for r in rows]
        print('%-4s frame %.3f +- %.3f ms   TAA resolve %.3f +- %.3f ms'
              % (arm, statistics.mean(f), statistics.stdev(f), statistics.mean(t), statistics.stdev(t)))


if __name__ == '__main__':
    main()
