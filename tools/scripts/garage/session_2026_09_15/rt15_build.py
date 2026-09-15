# -*- coding: utf-8 -*-
"""RT-15, the three items as built: a smoke run before any measurement.

  smoke   the car driving at the close-up with validation on and the glass layer on:
          shader compile errors, validation messages by kind, and which reflection
          passes ran (the young blur's three should appear while the car moves)

Usage: rt15_build.py smoke
"""
import io, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_14'))
sys.path.insert(0, os.path.join(HERE, '..'))
import stage_run  # noqa: E402
import rt13_stage2 as s2  # noqa: E402
import burst  # noqa: E402

OUT = os.path.join(stage_run.ROOT, 'build', 'rt15')


def smoke():
    os.makedirs(OUT, exist_ok=True)
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=stage_run.ROOT, capture_output=True, check=True).stdout
    os.environ['BURST_SLIDE'] = s2.CAR
    try:
        io.open(os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE), 'wb').write(head)
        burst.SCENE = stage_run.HEAD_SCENE
        burst.make_scene(0.0, 0.1)
        if not stage_run.restore():
            sys.exit('staged copies did not restore')
        cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'),
               '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
               '--scene=scenes/showroom_burst.rage', '--rhi=vulkan', '--render-defaults=off',
               '--vsync=off', '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off',
               '--camera=' + s2.CAMS['close'], '--glass-layer=on', '--validation=on', '--pass-timings=on',
               '--screenshot=' + os.path.join(OUT, 'smoke.png'), '--screenshot-frame=60',
               '--screenshot-count=12']
        p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=3600, errors='replace')
        text = p.stdout + '\n' + p.stderr
        io.open(os.path.join(OUT, 'smoke.log'), 'w', encoding='utf-8').write(text)
        kinds = {}
        for line in text.splitlines():
            m = re.search(r'(VUID-[A-Za-z0-9_\-]+|UNASSIGNED-[A-Za-z0-9_\-]+)', line)
            if m:
                kinds[m.group(1)] = kinds.get(m.group(1), 0) + 1
        compile_errors = [l for l in text.splitlines() if re.search(r'did not compile|ERROR: |error:', l)]
        got = [f for f in os.listdir(OUT) if re.match(r'smoke_\d+\.png$', f)]
        print('exit %d, %d frames, %d validation messages, %d compile-error lines'
              % (p.returncode, len(got), sum(kinds.values()), len(compile_errors)))
        for k, n in sorted(kinds.items(), key=lambda kv: -kv[1]):
            print('   %5d  %s' % (n, k))
        for l in compile_errors[:20]:
            print('   ', l[:300])
        passes = sorted(set(re.findall(r'\b((?:Glass)?Reflection[A-Za-z0-9]+)\b', text)))
        print('reflection passes named in the log:', ' '.join(passes))
    finally:
        os.environ.pop('BURST_SLIDE', None)
        print('staged copies restored and identical to source:', stage_run.restore())
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta',
                  'showroom_burst.rage', 'showroom_burst.rage.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass


if __name__ == '__main__':
    {'smoke': smoke}[sys.argv[1]]()
