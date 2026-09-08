# -*- coding: utf-8 -*-
"""RT-8 job 3: prove the measurement before believing it.

Two arms.

**Null.** The gate decides nothing, so every picture must be what HEAD renders.
The committed water_accumulate is staged, three cameras are shot, the patched
one is staged back and they are shot again. Anything but bit-identical means
the restructure changed the sea.

**Probe.** A counter that reads a dead buffer prints a plausible number and
means nothing. So last frame's plane distance is shifted by an absurd constant
-- a hundred metres, far outside any tolerance -- and the plane refusal must go
to essentially everything. If it does not, the gate is not reading the history.
"""
import io, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
STAGED = os.path.join(RT, 'assets', 'shaders', 'water_accumulate.rvshader')
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders', 'water_accumulate.rvshader')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')

CAMERAS = {
    'glitter': '500,2.5,180,0.01,-90,-1.146',
    'pier':    '70,4.5,705,0.01,-46.98,-2.86',
    'deck':    '0,76.4,950,0.01,0,0',
}


def shot(tag, camera, frame=60):
    out = os.path.join(SHOTS, tag + '.png')
    if os.path.exists(out):
        os.remove(out)
    p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                        '--project=' + os.path.join(ROOT, 'SampleProject'),
                        '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan',
                        '--render-defaults=off', '--vsync=off',
                        '--width=1600', '--height=900',
                        '--screenshot=' + out, '--screenshot-frame=%d' % frame,
                        '--screenshot-count=1', '--frame-time=0.0166',
                        '--camera=' + camera],
                       cwd=RT, capture_output=True, text=True, timeout=1800,
                       errors='replace')
    if not os.path.exists(out):
        print(tag, 'NO FRAME'); print(p.stdout[-1500:]); print(p.stderr[-800:])
        return None
    return out


def bench(camera, frames=120):
    p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                        '--project=' + os.path.join(ROOT, 'SampleProject'),
                        '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan',
                        '--render-defaults=off', '--vsync=off',
                        '--width=1600', '--height=900',
                        '--benchmark=%d' % frames, '--frame-time=0.0166',
                        '--camera=' + camera],
                       cwd=RT, capture_output=True, text=True, timeout=1800,
                       errors='replace')
    return (p.stdout or '') + (p.stderr or '')


patched = io.open(SRC, encoding='utf-8', newline='').read()
committed = subprocess.run(
    ['git', 'show', 'HEAD:RageVEditor/assets/shaders/water_accumulate.rvshader'],
    cwd=ROOT, capture_output=True, timeout=120).stdout.decode('utf-8')

try:
    # ---- null arm -------------------------------------------------------
    io.open(STAGED, 'w', encoding='utf-8', newline='').write(committed)
    for name, cam in CAMERAS.items():
        shot('gate_%s_head' % name, cam)
    shutil.copyfile(SRC, STAGED)
    for name, cam in CAMERAS.items():
        shot('gate_%s_now' % name, cam)

    print()
    print('null test -- the gate decides nothing, so these must be identical')
    for name in CAMERAS:
        a = os.path.join(SHOTS, 'gate_%s_head.png' % name)
        b = os.path.join(SHOTS, 'gate_%s_now.png' % name)
        if not (os.path.exists(a) and os.path.exists(b)):
            print('  %-8s missing a frame' % name); continue
        x = np.asarray(Image.open(a).convert('RGB'), dtype=float)
        y = np.asarray(Image.open(b).convert('RGB'), dtype=float)
        d = np.abs(x - y)
        print('  %-8s max %.0f levels, %.4f%% of pixels differ  %s'
              % (name, d.max(), 100.0 * (d.max(axis=2) > 0).mean(),
                 'BIT-IDENTICAL' if d.max() == 0 else '*** CHANGED ***'))

    # ---- probe arm ------------------------------------------------------
    probe = patched.replace(
        'const float offPlane = abs(dot(wasN, P) - was.b);',
        'const float offPlane = abs(dot(wasN, P) - (was.b + 100.0));')
    if probe == patched:
        sys.exit('the plane residual is not where the probe expects it')
    io.open(STAGED, 'w', encoding='utf-8', newline='').write(probe)
    print()
    print('probe -- last frame\'s plane shifted 100 m; the plane test must refuse')
    for name, cam in CAMERAS.items():
        out = bench(cam)
        for line in out.splitlines():
            if 'water under the shared gate' in line or 'water gate refusals' in line:
                print('  %-8s %s' % (name, line.split('] ')[-1].strip()))
finally:
    shutil.copyfile(SRC, STAGED)
    same = (io.open(SRC, 'rb').read() == io.open(STAGED, 'rb').read())
    print()
    print('staged copy restored and identical:', same)
