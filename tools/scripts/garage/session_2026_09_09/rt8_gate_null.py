# -*- coding: utf-8 -*-
"""The honest null test: HEAD's engine against the patched one.

The staged-shader A/B could not answer this, because the C++ changed too --
the counter block went from thirty-two lanes a slot to sixty-four, and if
anything downstream reads those numbers the frame could move without the sea's
shader having anything to do with it. So this builds both.

Tracked changes are stashed, the runtime is built and shot, the stash is
restored, and it is built and shot again. Anything but bit-identical is a
defect in the measurement, which decides nothing by design.
"""
import os, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')
CMAKE = (r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE'
         r'/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')

CAMERAS = {
    'glitter': '500,2.5,180,0.01,-90,-1.146',
    'pier':    '70,4.5,705,0.01,-46.98,-2.86',
    'deck':    '0,76.4,950,0.01,0,0',
}


def git(*args):
    p = subprocess.run(['git'] + list(args), cwd=ROOT, capture_output=True,
                       text=True, timeout=300, errors='replace')
    return p.stdout.strip()


def build():
    p = subprocess.run([CMAKE, '--build', 'build', '--config', 'Release',
                        '--target', 'RageVRuntime', '--', '-m', '-v:m'],
                       cwd=ROOT, capture_output=True, text=True, timeout=3600,
                       errors='replace')
    if p.returncode != 0:
        print(p.stdout[-3000:]); sys.exit('build failed')


def shot(tag, camera, frame=60):
    out = os.path.join(SHOTS, tag + '.png')
    if os.path.exists(out):
        os.remove(out)
    subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                    '--project=' + os.path.join(ROOT, 'SampleProject'),
                    '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan',
                    '--render-defaults=off', '--vsync=off',
                    '--width=1600', '--height=900',
                    '--screenshot=' + out, '--screenshot-frame=%d' % frame,
                    '--screenshot-count=1', '--frame-time=0.0166',
                    '--camera=' + camera],
                   cwd=RT, capture_output=True, text=True, timeout=1800,
                   errors='replace')
    return out if os.path.exists(out) else None


stashed = False
try:
    print(git('stash', 'push', '-m', 'rt8-job3-null'))
    stashed = 'No local changes' not in git('stash', 'list')
    if not stashed:
        sys.exit('nothing was stashed -- refusing to compare a build with itself')
    print('building HEAD...')
    build()
    for name, cam in CAMERAS.items():
        shot('null_%s_head' % name, cam)
finally:
    if stashed:
        print(git('stash', 'pop'))

print('building the measurement back...')
build()
for name, cam in CAMERAS.items():
    shot('null_%s_now' % name, cam)

print()
print('null test -- the measurement decides nothing, so these must be identical')
bad = 0
for name in CAMERAS:
    a = os.path.join(SHOTS, 'null_%s_head.png' % name)
    b = os.path.join(SHOTS, 'null_%s_now.png' % name)
    if not (os.path.exists(a) and os.path.exists(b)):
        print('  %-8s missing a frame' % name); bad += 1; continue
    x = np.asarray(Image.open(a).convert('RGB'), dtype=float)
    y = np.asarray(Image.open(b).convert('RGB'), dtype=float)
    d = np.abs(x - y)
    ok = d.max() == 0
    bad += 0 if ok else 1
    print('  %-8s max %3.0f levels, %7.4f%% of pixels differ  %s'
          % (name, d.max(), 100.0 * (d.max(axis=2) > 0).mean(),
             'BIT-IDENTICAL' if ok else '*** CHANGED ***'))
print()
print('verdict:', 'clean' if bad == 0 else 'NOT CLEAN')
