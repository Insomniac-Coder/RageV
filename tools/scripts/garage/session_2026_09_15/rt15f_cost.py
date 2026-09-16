# -*- coding: utf-8 -*-
"""What RT-15 costs, as built (2026-09-16, laptop on mains).

  before  every RT-15 dial off: item 1's choice and item 2's travel staged out (rt15_items),
          the hit's specular half and the probe at the hit staged out (rt15_scene_arms's
          nospecall), --reflection-moving-blur=0 and --reflection-moving-rays=1. What is left
          in is the object check and the band fixes, both a fetch or two a texel.
  ship    the build as it stands.

Two scenes, HEAD's showroom with the harness's own cube: the car driving across the owner's
shot at 0.5 m/s, and the same shot parked. A B B A per scene, 300 frames a run, and the
reflection passes' own GPU time beside the frame mean (the laptop's GPU drifts ~1 ms over a
session; the palindrome is what makes a small number readable -- docs/HANDOFF.md).

Usage: rt15f_cost.py
"""
import io, os, re, statistics, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
import rt15_items as it  # noqa: E402  -- registers i12off
import rt15_scene_arms as sa  # noqa: E402  -- registers nospecall
import rt15_checks as ck  # noqa: E402
import burst  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
OUT = os.path.join(stage_run.ROOT, 'build', 'rt15')
# item 2's own arm is gone with the moving layer (off by default, it costs nothing); item 1's stays.
stage_run.VARIANTS['rt15off'] = [it.I1_OFF] + stage_run.VARIANTS['nospecall']
ARMS = {'before': ('rt15off', ['--reflection-moving-blur=0', '--reflection-moving-rays=1']),
        'ship': ('ship', [])}


def main():
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=stage_run.ROOT, capture_output=True, check=True).stdout
    staged = {v: stage_run.build_variant(v) for v, _ in ARMS.values()}
    try:
        io.open(os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE), 'wb').write(head)
        burst.SCENE = stage_run.HEAD_SCENE
        for scene, slide in (('driving', '=porsche_992_gt3_r|0.5|12.0'), ('parked', None)):
            if slide:
                os.environ['BURST_SLIDE'] = slide
            else:
                os.environ.pop('BURST_SLIDE', None)
            burst.make_scene(0.0, 0.1)
            rows = {a: [] for a in ARMS}
            for i, arm in enumerate(['before', 'ship', 'ship', 'before']):
                if not stage_run.restore():
                    sys.exit('staged copies did not restore')
                variant, flags = ARMS[arm]
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
                io.open(os.path.join(OUT, 'rt15f_cost_%s_%d_%s.log' % (scene, i, arm)), 'w', encoding='utf-8').write(text)
                m = re.search(r'frame\s+mean\s+([0-9.]+) ms', text)
                if not m:
                    print(text[-1500:])
                    sys.exit('%s run %d: no benchmark report' % (scene, i))
                r = {'frame': float(m.group(1))}
                g = re.search(r'whole frame \(GPU\)\s+([0-9.]+) ms', text)
                if g:
                    r['gpu'] = float(g.group(1))
                for name in ck.PASS_NAMES:
                    pm = re.search(r'\[benchmark\]\s+(?:[A-Za-z]+/)?%s\s+([0-9.]+)\s+([0-9.]+)' % re.escape(name), text)
                    if pm:
                        r[name] = float(pm.group(2))
                rows[arm].append(r)
                print('%-7s run %d %-6s frame %.3f  gpu %.3f  %s' % (scene, i, arm, r['frame'], r.get('gpu', 0),
                      '  '.join('%s %.3f' % (n, r[n]) for n in ck.PASS_NAMES if n in r)))
                sys.stdout.flush()
            for arm in ARMS:
                keys = sorted({k for r in rows[arm] for k in r})
                print('  %-7s %-6s %s' % (scene, arm, '  '.join(
                    '%s %.3f' % (k, statistics.mean(r.get(k, 0.0) for r in rows[arm])) for k in keys)))
            print('  %-7s ship - before: frame %+.3f ms, gpu %+.3f ms' % (
                scene,
                statistics.mean(r['frame'] for r in rows['ship']) - statistics.mean(r['frame'] for r in rows['before']),
                statistics.mean(r.get('gpu', 0.0) for r in rows['ship']) - statistics.mean(r.get('gpu', 0.0) for r in rows['before'])))
            sys.stdout.flush()
    finally:
        os.environ.pop('BURST_SLIDE', None)
        print('staged copies restored and identical to source:', stage_run.restore())
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta', 'showroom_burst.rage', 'showroom_burst.rage.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass


if __name__ == '__main__':
    main()
