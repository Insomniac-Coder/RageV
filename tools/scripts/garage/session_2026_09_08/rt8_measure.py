"""RT-8: does the sea reprojecting by its own motion change anything?

Four bridge cameras, the same frame each, before and after. The sea's own bands
are where it should show; the deck and the towers are the no-regression arms.
The runtime is driven directly -- burst.py cannot run the bridge.
"""
import io, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
STAGED = os.path.join(RT, 'assets', 'shaders')
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')
SESSION = os.path.join(ROOT, 'tools', 'scripts', 'garage', 'session_2026_09_08')

CAMERAS = {
    'glitter': '500,2.5,180,0.01,-90,-1.146',
    'pier':    '70,4.5,705,0.01,-46.98,-2.86',
    'deck':    '0,76.4,950,0.01,0,0',
}


def render(tag, camera, frames=1, frame=60):
    out = os.path.join(SHOTS, tag + '.png')
    if os.path.exists(out):
        os.remove(out)
    p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                        '--project=' + os.path.join(ROOT, 'SampleProject'),
                        '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan',
                        '--render-defaults=off', '--vsync=off',
                        '--width=1600', '--height=900',
                        '--screenshot=' + out, '--screenshot-frame=%d' % frame,
                        '--screenshot-count=%d' % frames, '--frame-time=0.0166',
                        '--camera=' + camera],
                       cwd=RT, capture_output=True, text=True, timeout=1800,
                       errors='replace')
    if not os.path.exists(out):
        print(tag, 'NO FRAME'); print(p.stdout[-1200:]); print(p.stderr[-600:])
        return None
    return out


def arr(p):
    return np.asarray(Image.open(p).convert('RGB'), dtype=float)


# The before arm: the resolve as it was, with the water lane declared but the
# branch that uses it removed, so the only difference is the branch itself.
before = io.open(os.path.join(SRC, 'taa_resolve.rvshader'),
                 encoding='utf-8', newline='').read()
plain = before.replace('water.z > 0.5 ? water.xy : texture(u_Velocity, uv).xy',
                       'texture(u_Velocity, uv).xy')
if plain == before:
    sys.exit('the water branch is not in the source')

try:
    io.open(os.path.join(STAGED, 'taa_resolve.rvshader'), 'w',
            encoding='utf-8', newline='').write(plain)
    for name, cam in CAMERAS.items():
        render('rt8_%s_before' % name, cam)
    shutil.copyfile(os.path.join(SRC, 'taa_resolve.rvshader'),
                    os.path.join(STAGED, 'taa_resolve.rvshader'))
    for name, cam in CAMERAS.items():
        render('rt8_%s_after' % name, cam)
finally:
    shutil.copyfile(os.path.join(SRC, 'taa_resolve.rvshader'),
                    os.path.join(STAGED, 'taa_resolve.rvshader'))
    print('staged copy restored and identical:',
          io.open(os.path.join(SRC, 'taa_resolve.rvshader'), 'rb').read()
          == io.open(os.path.join(STAGED, 'taa_resolve.rvshader'), 'rb').read())

print()
for name in CAMERAS:
    a = os.path.join(SHOTS, 'rt8_%s_before.png' % name)
    b = os.path.join(SHOTS, 'rt8_%s_after.png' % name)
    if not (os.path.exists(a) and os.path.exists(b)):
        print('%-8s missing a frame' % name); continue
    d = np.abs(arr(a) - arr(b))
    print('%-8s whole frame: mean %.4f  max %.1f  differing %.2f%%'
          % (name, d.mean(), d.max(), 100.0 * (d.max(axis=2) > 0).mean()))
