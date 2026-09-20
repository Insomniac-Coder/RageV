"""RT-13: the glass layer on by default -- the full check set.

Validation and shader compilation on the garage (parked, driving) and the
bridge, under TAA, MSAA and SSAA; then the pictures the default now changes,
against the same runs with `--glass-layer=off`, so what moved is only glass.

Writes its pictures to build/rt13/default/ and deletes them at the end unless
--keep is passed (the owner's rule: captures go when the debugging is done).
"""
import os, re, subprocess, sys, glob
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
PROJECT = os.path.join(ROOT, 'SampleProject')
OUT = os.path.join(ROOT, 'build', 'rt13', 'default')
os.makedirs(OUT, exist_ok=True)

GARAGE = ['--scene=scenes/showroom.rage', '--camera=-2.3,0.72,-2,11,0,4']
CLOSE = ['--scene=scenes/showroom.rage', '--camera=-4.3,1.0,-2,3.2,25,8']
BRIDGE = ['--scene=scenes/GoldenGateDemo.rage', '--camera=70,4.5,705,0.01,-46.98,-2.86']


def run(name, scene, extra, frames=40):
    """One render; returns (log, screenshot path)."""
    shot = os.path.join(OUT, name + '.png')
    cmd = [os.path.join(RT, 'RageVRuntime.exe'),
           '--project=' + PROJECT, '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
           '--width=1600', '--height=900', '--import-cache=off', '--frame-time=0.0166',
           '--screenshot=' + shot, '--screenshot-frame=%d' % frames, '--screenshot-count=1'
           ] + scene + extra
    p = subprocess.run(cmd, cwd=RT, capture_output=True, text=True, errors='replace')
    return p.stdout + p.stderr, shot


def complaints(log):
    vulkan = len(re.findall(r'\[Vulkan\]', log))
    shaders = len(re.findall(r'Shader compilation failed', log))
    return vulkan, shaders


def difference(a, b):
    x = np.asarray(Image.open(a).convert('RGB'), dtype=np.int16)
    y = np.asarray(Image.open(b).convert('RGB'), dtype=np.int16)
    d = np.abs(x - y).max(axis=2)
    return (d > 2).mean() * 100, (d > 16).mean() * 100, int(d.max())


ARMS = [
    ('garage_taa', GARAGE, ['--aa=taa']),
    ('garage_msaa', GARAGE, ['--aa=msaa']),
    ('garage_ssaa', GARAGE, ['--aa=ssaa']),
    ('close_taa', CLOSE, ['--aa=taa']),
    ('bridge_taa', BRIDGE, ['--aa=taa']),
]

print('%-14s %-28s %s' % ('arm', 'validation (on / off)', 'what the default changes'))
rows = []
for name, scene, extra in ARMS:
    on_log, on_shot = run(name + '_on', scene, extra)
    off_log, off_shot = run(name + '_off', scene, extra + ['--glass-layer=off'])
    on_v, on_s = complaints(on_log)
    off_v, off_s = complaints(off_log)
    if not (os.path.exists(on_shot) and os.path.exists(off_shot)):
        print('%-14s RENDER FAILED' % name)
        print(on_log[-1500:])
        continue
    p2, p16, worst = difference(on_shot, off_shot)
    rows.append((name, on_v, on_s, off_v, off_s, p2, p16, worst))
    print('%-14s %-28s %.3f%% of pixels over 2 levels, %.3f%% over 16, worst %d'
          % (name, 'vulkan %d/%d, shaders %d/%d' % (on_v, off_v, on_s, off_s), p2, p16, worst))

print()
bad = [r for r in rows if r[1] or r[2] or r[3] or r[4]]
print('validation clean everywhere' if not bad else 'VALIDATION MESSAGES: %s' % bad)

if '--keep' not in sys.argv:
    for f in glob.glob(os.path.join(OUT, '*.png')):
        os.remove(f)
    print('captures deleted (pass --keep to look at them)')
