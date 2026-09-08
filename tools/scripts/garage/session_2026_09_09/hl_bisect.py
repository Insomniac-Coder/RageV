# -*- coding: utf-8 -*-
"""Which of today's commits changed the headland shot?

The owner's before/after is the headland camera, and flag-level bisection did
not isolate it -- several of today's fixes are not behind flags. So this builds
each commit of the day in turn and shoots the same frame.
"""
import os, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')
CMAKE = (r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE'
         r'/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
CAM = '500,89.47,-1100,0.01,-157.08,8.88'

COMMITS = [
    ('a0de589', 'before today'),
    ('ab3a96a', 'job 3 - the sea averaged on the contract'),
    ('95dae20', 'job 2 - the sea mirror ray as a signal'),
    ('af00f8c', 'job 1 built + normal/sun/score fixes'),
    ('331f2f8', 'the choose/shade split'),
]


def run(args, **kw):
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True,
                          timeout=3600, errors='replace', **kw)


def build():
    p = run([CMAKE, '--build', 'build', '--config', 'Release',
             '--target', 'RageVRuntime', '--', '-m', '-v:m'])
    if p.returncode != 0:
        print(p.stdout[-2000:])
        sys.exit('build failed')


def shot(tag):
    out = os.path.join(SHOTS, 'bis_%s.png' % tag)
    if os.path.exists(out):
        os.remove(out)
    subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                    '--project=' + os.path.join(ROOT, 'SampleProject'),
                    '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan',
                    '--render-defaults=off', '--vsync=off',
                    '--width=2560', '--height=1600',
                    '--screenshot=' + out, '--screenshot-frame=60',
                    '--screenshot-count=1', '--frame-time=0.0166',
                    '--camera=' + CAM],
                   cwd=RT, capture_output=True, text=True, timeout=1800,
                   errors='replace')
    return out if os.path.exists(out) else None


# Untracked scratch is fine; a modified tracked file is not -- a checkout
# would carry it across commits and silently mix two builds.
if run(['git', 'status', '--porcelain', '--untracked-files=no']).stdout.strip():
    sys.exit('working tree is not clean; refusing to check out')

try:
    for sha, _ in COMMITS:
        run(['git', 'checkout', '-q', sha])
        build()
        if not shot(sha):
            print(sha, 'NO FRAME')
finally:
    run(['git', 'checkout', '-q', 'main'])
    build()

def arr(p):
    return np.asarray(Image.open(p).convert('RGB'), dtype=float)

base = os.path.join(SHOTS, 'bis_%s.png' % COMMITS[0][0])
b = arr(base)
print()
print('%-9s %-44s %-11s %-8s' % ('commit', 'what it was', 'vs before', 'meanR'))
for sha, what in COMMITS:
    p = os.path.join(SHOTS, 'bis_%s.png' % sha)
    if not os.path.exists(p):
        print('%-9s %-44s missing' % (sha, what)); continue
    a = arr(p)
    d = np.abs(a - b).max(axis=2)
    print('%-9s %-44s max %3.0f %5.2f%% %-8.3f'
          % (sha, what, d.max(), 100 * (d > 0).mean(), a[..., 0].mean()))
