"""RT2-9's gate: launch time with the shader and pipeline caches on and off, and the picture both ways.

    launch_time.py [scene]        default scenes/showroom.rage (the garage)

Each launch runs to the end of its first frame (--benchmark=1) and is timed from the outside, wall clock.
The order is a palindrome after one launch that fills the caches from empty: off, on, on, off, off, on, on,
off -- so a slow drift in the machine (heat, a background job) falls on both arms alike. Then one frame at a
pinned clock is captured with the caches on and with them off, and the two must be pixel-identical: the
caches may change how fast the picture arrives, never the picture.
"""
import os
import re
import shutil
import statistics
import subprocess
import sys
import time

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
EXE = os.path.join(RT, 'RageVRuntime.exe')
PROJECT = os.path.join(ROOT, 'SampleProject', 'SampleProject.rvproject')
CACHE = os.path.join(RT, 'ShaderCache')
OUT = os.path.join(ROOT, 'build', 'launch_time')


def launch(scene, cache, extra=()):
    began = time.perf_counter()
    p = subprocess.run([EXE, '--project=' + PROJECT, '--scene=' + scene, '--rhi=vulkan', '--render-defaults=off',
                        '--vsync=off', '--width=1600', '--height=900', '--benchmark=1',
                        '--shader-cache=' + ('on' if cache else 'off')] + list(extra),
                       cwd=RT, capture_output=True, text=True, errors='replace', timeout=1800)
    seconds = time.perf_counter() - began
    log = p.stdout + p.stderr
    pick = lambda pattern: (re.search(pattern, log).group(0) if re.search(pattern, log) else '-')
    return seconds, p.returncode, pick(r'Shaders this run: .*'), pick(r'Pipelines this run: .*'), log


def main(argv):
    scene = argv[0] if argv else 'scenes/showroom.rage'
    os.makedirs(OUT, exist_ok=True)
    shutil.rmtree(CACHE, ignore_errors=True)
    seconds, code, shaders, pipelines, _ = launch(scene, True)
    print('fill (cache empty)  %5.1f s  exit %d | %s | %s' % (seconds, code, shaders, pipelines), flush=True)
    times = {'off': [], 'on': []}
    for arm in ('off', 'on', 'on', 'off', 'off', 'on', 'on', 'off'):
        seconds, code, shaders, pipelines, _ = launch(scene, arm == 'on')
        times[arm].append(seconds)
        print('%-18s %5.1f s  exit %d | %s | %s' % ('cache ' + arm, seconds, code, shaders, pipelines), flush=True)
    for arm, values in times.items():
        print('cache %-3s mean %.1f s  (+- %.1f, %d launches)' % (arm, statistics.mean(values), statistics.stdev(values),
                                                                 len(values)))

    shots = {}
    for arm in ('on', 'off'):
        path = os.path.join(OUT, 'frame_%s.png' % arm)
        subprocess.run([EXE, '--project=' + PROJECT, '--scene=' + scene, '--rhi=vulkan', '--render-defaults=off',
                        '--vsync=off', '--width=1600', '--height=900', '--frame-time=0.000001',
                        '--screenshot-frame=60', '--screenshot=' + path, '--benchmark=62',
                        '--shader-cache=' + arm], cwd=RT, capture_output=True, timeout=1800)
        shots[arm] = np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)
    diff = np.abs(shots['on'] - shots['off'])
    print('frame 60, cache on against off: %d pixels differ, largest difference %d levels'
          % (int((diff.max(axis=-1) > 0).sum()), int(diff.max())))
    shutil.rmtree(OUT, ignore_errors=True)


if __name__ == '__main__':
    main(sys.argv[1:])
