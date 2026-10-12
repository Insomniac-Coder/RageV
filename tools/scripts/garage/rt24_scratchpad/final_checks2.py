"""RT-24 final checks, part 2 (headless): the committed build (HEAD, 76ab6aa, before RT-24) against the
working tree. Gate 7: cost as a palindrome (head now now head head now now head) in the garage and at
Headland, 1600x900 and 2560x1600; the lamp pool on its own (8 against 0). Gate 5's headless half: the
bridge's glint flicker, full frame and cable band, HEAD against now, parked, clock pinned."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import glob
import os
import re
import shutil
import statistics
import subprocess
import sys

TREES = {
    'head': 'C:/Users/ism19/Code/RageV-head',
    'now': (_RAGEV + ''),
}
OUT = (_RAGEV + '/build/rt24/final')
CHECK = (_RAGEV + '/tools/scripts/check_glint_flicker.py')
sys.path.insert(0, (_RAGEV + '/tools/scripts/garage'))
import spin_measure as sm   # noqa: E402  (the main tree's staged shaders are synced by sr.restore)

sr = sm.sr
os.makedirs(OUT, exist_ok=True)
HEADLAND = '--camera=500,89.47,-1100,0.01,-157.08,8.88'
ORDER = ['head', 'now', 'now', 'head', 'head', 'now', 'now', 'head']


def runtime(tree):
    rt = os.path.join(TREES[tree], 'build', 'bin', 'Release', 'RageVRuntime')
    return rt, os.path.join(rt, 'RageVRuntime.exe'), os.path.join(TREES[tree], 'SampleProject')


def run(tree, args, log):
    rt, exe, project = runtime(tree)
    if tree == 'now' and not sr.restore():
        sys.exit('staged shaders did not restore')
    p = subprocess.run([exe, '--project=' + project, '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                        '--import-cache=off'] + args, cwd=rt, capture_output=True, text=True,
                       errors='replace', timeout=3600)
    open(os.path.join(OUT, log), 'w', encoding='utf-8').write(p.stdout + p.stderr)
    return p.stdout + p.stderr


def frame_ms(text):
    m = re.search(r'frame\s+mean ([\d.]+) ms', text)
    return float(m.group(1)) if m else float('nan')


def cost(label, scene_args, width, height, extra_now=None, extra_head=None, order=ORDER, names=None):
    got = {}
    for i, arm in enumerate(order):
        tree = 'head' if arm == 'head' else 'now'
        extra = (extra_head if arm == 'head' else extra_now) or []
        if names:
            extra = names[arm]
        text = run(tree, scene_args + ['--width=%d' % width, '--height=%d' % height, '--frame-time=0.0166',
                                       '--benchmark=240'] + extra,
                   '%s_%dx%d_run%d_%s.log' % (label, width, height, i, arm))
        got.setdefault(arm, []).append(frame_ms(text))
    line = '%-9s %dx%d  ' % (label, width, height) + '   '.join(
        '%s %.2f +- %.2f ms' % (a, statistics.mean(v), statistics.stdev(v) if len(v) > 1 else 0.0)
        for a, v in got.items())
    print(line, flush=True)
    return line


lines = []
for w, h in ((1600, 900), (2560, 1600)):
    lines.append(cost('garage', ['--scene=scenes/showroom.rage'], w, h))
    lines.append(cost('headland', ['--scene=scenes/GoldenGateDemo.rage', HEADLAND], w, h))
# The lamp pool alone, in the working tree: 8 (as it ships) against 0.
lines.append(cost('lamppool', ['--scene=scenes/showroom.rage'], 1600, 900,
                  order=['p0', 'p8', 'p8', 'p0', 'p0', 'p8', 'p8', 'p0'],
                  names={'p0': ['--reflection-lamp-pool=0'], 'p8': []}))

# The bridge, parked at its authored camera: 16 frames, clock pinned.
for tree in ('head', 'now'):
    shots = os.path.join(OUT, 'bridge_' + tree)
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots)
    run(tree, ['--scene=scenes/GoldenGateDemo.rage', '--width=1600', '--height=900', '--frame-time=0.000001',
               '--screenshot-frame=240', '--screenshot-count=16', '--screenshot=' + os.path.join(shots, 'f.png'),
               '--benchmark=258'], 'bridge_%s.log' % tree)
    files = sorted(glob.glob(os.path.join(shots, '*.png')))
    for region in (None, '0,250,1600,420'):
        out = subprocess.run([sys.executable, CHECK] + (['--region', region] if region else []) + files,
                             capture_output=True, text=True).stdout
        lines.append('bridge %-4s %-10s %s' % (tree, 'cable band' if region else 'full frame',
                                              ' | '.join(l.strip() for l in out.splitlines()[1:])))
        print(lines[-1], flush=True)
open(os.path.join(OUT, 'summary.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
sr.restore()
