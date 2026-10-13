"""RT2-1's gate for the ray counters: off (the default) against --ray-counters=on.

The picture must be identical (one frame at a pinned clock, garage and Headland), and the time the counters
cost is measured as a palindrome: off on on off off on on off, 240 frames at a pinned step, the frame mean and
the GPU's whole-frame time.
"""
import os
import re
import statistics
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
OUT = os.path.join(ROOT, 'build', 'ray_counters_ab')
SHOTS = [('garage', ['--scene=scenes/showroom.rage']),
         ('headland', ['--scene=scenes/GoldenGateDemo.rage', '--camera=500,89.47,-1100,0.01,-157.08,8.88'])]


def run(scene, counters, extra):
    p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                        '--project=' + os.path.join(ROOT, 'SampleProject', 'SampleProject.rvproject'),
                        '--rhi=vulkan', '--render-defaults=off', '--vsync=off', '--width=1600', '--height=900',
                        '--ray-counters=' + ('on' if counters else 'off')] + scene + extra,
                       cwd=RT, capture_output=True, text=True, errors='replace', timeout=1800)
    if p.returncode != 0:
        sys.exit('run failed (exit %d): %s' % (p.returncode, (p.stdout + p.stderr)[-800:]))
    return p.stdout + p.stderr


def number(pattern, log):
    m = re.search(pattern, log)
    return float(m.group(1)) if m else float('nan')


os.makedirs(OUT, exist_ok=True)
for name, scene in SHOTS:
    shots = {}
    for counters in (False, True):
        path = os.path.join(OUT, '%s_%d.png' % (name, counters))
        run(scene, counters, ['--frame-time=0.000001', '--screenshot-frame=60', '--screenshot=' + path,
                              '--benchmark=62'])
        shots[counters] = np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)
    diff = np.abs(shots[True] - shots[False]).max(axis=-1)
    print('%-9s pixels, counters on against off: %d differ, largest %d levels'
          % (name, int((diff > 0).sum()), int(diff.max())), flush=True)
    got = {False: [], True: []}
    for counters in (False, True, True, False, False, True, True, False):
        log = run(scene, counters, ['--frame-time=0.0166667', '--benchmark=240', '--pass-timings=on'])
        got[counters].append((number(r'frame\s+mean ([\d.]+) ms', log),
                              number(r'whole frame \(GPU\)\s+([\d.]+) ms', log)))
    for counters in (False, True):
        rows = got[counters]
        print('%-9s counters %-3s frame %6.2f +- %.2f ms | GPU %6.2f +- %.2f ms'
              % (name, 'on' if counters else 'off', statistics.mean(r[0] for r in rows),
                 statistics.stdev(r[0] for r in rows), statistics.mean(r[1] for r in rows),
                 statistics.stdev(r[1] for r in rows)), flush=True)
for f in os.listdir(OUT):
    os.remove(os.path.join(OUT, f))
os.rmdir(OUT)
