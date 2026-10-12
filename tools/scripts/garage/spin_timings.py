# -*- coding: utf-8 -*-
"""RT2-0 arm C: which reflection passes run while the camera swings.

The swing test captures pictures, and the engine prints no pass table in screenshot
mode. This plays the same scene for the still start and the swing only, in benchmark
mode, and prints the "render graph, by pass" lines for the reflection chain. A pass
that never ran is absent from the table.

    spin_timings.py <tag> [--swing=15] [--rate=2] [--spin=2] [--stage=NAME] [--still] [engine flags ...]

--still plays the swing scene with a zero swing: the parked control.
"""
import io
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import spin_measure as sm       # noqa: E402

sr, burst = sm.sr, sm.burst


def main(argv):
    tag = argv[0]
    swing, rate, spin, stage_name, still, flags = 15.0, 2.0, 2.0, None, False, []
    for a in argv[1:]:
        if a.startswith('--swing='):
            swing = float(a.split('=', 1)[1])
        elif a.startswith('--rate='):
            rate = float(a.split('=', 1)[1])
        elif a.startswith('--spin='):
            spin = float(a.split('=', 1)[1])
        elif a.startswith('--stage='):
            stage_name = a.split('=', 1)[1]
        elif a == '--still':
            still = True
        else:
            flags.append(a)
    frames = int(round((sm.DELAY + spin) * sm.FPS))
    # A zero swing would fall through to the turn; a turn at speed zero is parked.
    scene = sm.orbit_scene(0.0, spin) if still else sm.orbit_scene(0.0, spin, swing, rate)
    io.open(os.path.join(sr.SCENES, sr.HEAD_SCENE), 'wb').write(scene)
    burst.sync_bake('showroom_head')
    try:
        if not sr.restore():
            sys.exit('the staged shader copies did not restore')
        sm.stage(stage_name)
        p = subprocess.run([os.path.join(sm.RT, 'RageVRuntime.exe'), '--project=' + sr.PROJECT,
                            '--scene=scenes/' + sr.HEAD_SCENE, '--rhi=vulkan', '--render-defaults=off',
                            '--width=1600', '--height=900', '--import-cache=off', '--vsync=off',
                            '--frame-time=%.9f' % (1.0 / sm.FPS), '--benchmark=%d' % frames] + flags,
                           cwd=sm.RT, capture_output=True, text=True, errors='replace', timeout=1800)
    finally:
        sr.restore()
        for f in (sr.HEAD_SCENE, sr.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(sr.SCENES, f))
            except OSError:
                pass
    log = p.stdout + p.stderr
    io.open(os.path.join(sm.OUT, tag + '_timings.txt'), 'w', encoding='utf-8').write(log)
    print('%s  (%s, %d frames, exit %d)' % (tag, 'parked' if still else 'swing +-%g at %g Hz' % (swing, rate),
                                            frames, p.returncode))
    rows = [l.split('[benchmark]', 1)[1].strip() for l in log.splitlines()
            if '[benchmark]' in l and ('Reflection' in l or 'frame  mean' in l or 'whole frame' in l)]
    print('\n'.join('   ' + r for r in rows) or '   (no reflection passes in the table)')


if __name__ == '__main__':
    main(sys.argv[1:])
