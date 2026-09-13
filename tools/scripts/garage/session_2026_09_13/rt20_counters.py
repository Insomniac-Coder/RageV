# -*- coding: utf-8 -*-
"""RT-20: how often each path of the resolve fires, from the benchmark's own counters.

rt20_arms.py's probe variants move one kind of pixel into the "sky" refusal lane
(zero in the garage, whose sky is geometry -- RT-6.7): the pixels whose history
RT-6's search replaced with a neighbour's, or the pixels a fix's rule kept.
Each run is the garage from HEAD, parked at the owner's shot, 120 frames with a
fifth discarded as warm-up; `cube` adds rt20_measure's 3 m/s cube.

Usage: rt20_counters.py [scene:variant ...]   (default: the table below)
"""
import io, os, re, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stage_run  # noqa: E402
import rt20_arms  # noqa: E402,F401

# HEAD's search substitutions; the kept fix's substitutions left, and its rule's keeps.
RUNS = [('parked', 'r20probe_served'), ('parked', 'r20probe_Fserved'), ('parked', 'r20probe_F'),
        ('cube', 'r20probe_served'), ('cube', 'r20probe_Fserved'), ('cube', 'r20probe_F')]
LOGS = os.path.join(stage_run.OUT, 'rt20', 'counters')


def main(runs):
    os.makedirs(LOGS, exist_ok=True)
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=stage_run.ROOT, capture_output=True, check=True).stdout
    scene_path = os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE)
    results = []
    try:
        for scene, variant in runs:
            if not stage_run.restore():
                sys.exit('staged copies did not restore before %s' % variant)
            io.open(scene_path, 'wb').write(stage_run.moving_cube_scene(head, -9.0, 3.0, 6.0) if scene == 'cube' else head)
            for shader, text in stage_run.build_variant(variant).items():
                io.open(os.path.join(stage_run.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
            log = os.path.join(LOGS, '%s_%s.log' % (scene, variant))
            cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'), '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                   '--scene=scenes/' + stage_run.HEAD_SCENE, '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                   '--width=1600', '--height=900', '--benchmark=120', '--frame-time=0.0166', '--import-cache=off',
                   '--camera=-2.3,0.72,-2,11,0,4']
            p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=1800, errors='replace')
            io.open(log, 'w', encoding='utf-8').write(p.stdout + p.stderr)
            conf = re.search(r'temporal confidence: ([\d.]+)% of pixels reused', p.stdout)
            sky = re.search(r'temporal refusals:.*sky ([\d.]+)%', p.stdout)
            results.append((scene, variant, conf.group(1) if conf else '?', sky.group(1) if sky else '?'))
            print('%-7s %-18s reused %s%%, moved to the sky lane %s%%' % results[-1])
            sys.stdout.flush()
    finally:
        print('staged copies restored and identical to source:', stage_run.restore())
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass
    return results


if __name__ == '__main__':
    runs = [tuple(a.split(':', 1)) for a in sys.argv[1:]] or RUNS
    main(runs)
