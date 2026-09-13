# -*- coding: utf-8 -*-
"""The rounding fix on the bridge: the committed shaders against today's, at a camera.

The bridge is the scene the sea's own lamp average runs in by default, and the
scene every earlier water item was judged at, so the whole day's change is
looked at here as a picture: `before` stages the five shaders as committed at
HEAD (and dither.glsl without its guard), `after` is the working tree. The C++
is today's in both -- the three push blocks that gained a frame number carry it
in their last four bytes, which the committed shaders never read.

Parked at a camera with the clock pinned; 64 frames, their float mean, one
frame, and the difference of both at x8 (green: after is brighter).

Usage: rt5_bridge_ab.py [camera-name] [first] [count]
"""
import io, os, re, shutil, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders')
STAGED = os.path.join(RT, 'assets', 'shaders')
OUT = os.path.join(ROOT, 'build', 'rt5')
SHOTS = os.path.join(OUT, 'bridge')
CAMERAS = {
    'pier':    '70,4.5,705,0.01,-46.98,-2.86',
    'glitter': '500,2.5,180,0.01,-90,-1.146',
    'deck':    '0,76.4,950,0.01,0,0',
}
FILES = ['reflection_accumulate.rvshader', 'taa_resolve.rvshader', 'water_accumulate.rvshader',
         'gi_denoise.rvshader', 'tile_budget.rvshader', 'include/dither.glsl']

camera = sys.argv[1] if len(sys.argv) > 1 else 'pier'
first = int(sys.argv[2]) if len(sys.argv) > 2 else 136
count = int(sys.argv[3]) if len(sys.argv) > 3 else 64
os.makedirs(SHOTS, exist_ok=True)


def restore():
    ok = True
    for f in FILES:
        shutil.copyfile(os.path.join(SRC, f), os.path.join(STAGED, f))
        ok = ok and io.open(os.path.join(SRC, f), 'rb').read() == io.open(os.path.join(STAGED, f), 'rb').read()
    return ok


def run(tag):
    for f in os.listdir(SHOTS):
        if f.startswith(tag + '_'):
            os.remove(os.path.join(SHOTS, f))
    cmd = [os.path.join(RT, 'RageVRuntime.exe'), '--project=' + os.path.join(ROOT, 'SampleProject'),
           '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
           '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off',
           '--camera=' + CAMERAS[camera], '--screenshot=' + os.path.join(SHOTS, tag + '.png'),
           '--screenshot-frame=%d' % first, '--screenshot-count=%d' % count]
    p = subprocess.run(cmd, cwd=RT, capture_output=True, text=True, timeout=3600, errors='replace')
    got = sorted([f for f in os.listdir(SHOTS) if re.match(re.escape(tag) + r'_\d+\.png$', f)],
                 key=lambda f: int(re.search(r'_(\d+)\.png$', f).group(1)))
    if len(got) != count:
        print(p.stdout[-2000:], p.stderr[-2000:])
        sys.exit('%s: %d frames of %d' % (tag, len(got), count))
    frames = [np.asarray(Image.open(os.path.join(SHOTS, f)).convert('RGB'), dtype=np.float64) for f in got]
    return np.mean(frames, axis=0), frames[-1]


head = {f: subprocess.run(['git', 'show', 'HEAD:RageVEditor/assets/shaders/' + f], cwd=ROOT,
                          capture_output=True, check=True).stdout for f in FILES}
try:
    for f, data in head.items():
        io.open(os.path.join(STAGED, f), 'wb').write(data)
    before_mean, before_last = run('%s_before' % camera)
finally:
    print('staged shaders restored and identical to source:', restore())
after_mean, after_last = run('%s_after' % camera)

L = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
d = L(after_mean) - L(before_mean)
h, w = d.shape
print('%s, 64-frame means, after - before, 8-bit levels: whole %+.3f; top third %+.3f, middle %+.3f, bottom third %+.3f'
      % (camera, d.mean(), d[:h // 3].mean(), d[h // 3:2 * h // 3].mean(), d[2 * h // 3:].mean()))
print('  pixels brighter by more than 1 level %.2f%%, darker by more than 1 level %.2f%%'
      % (100 * (d > 1).mean(), 100 * (d < -1).mean()))


def label(arr, text):
    im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(im)
    dr.rectangle((0, 0, im.width, 26), fill=(0, 0, 0))
    dr.text((8, 7), text, fill=(255, 255, 255))
    return im


def signed(lum, gain):
    out = np.zeros(lum.shape + (3,))
    out[..., 0] = np.clip(-lum * gain, 0, 255)
    out[..., 1] = np.clip(lum * gain, 0, 255)
    return out


label(before_last, 'BRIDGE %s, BEFORE (committed shaders) - frame %d' % (camera, first + count - 1)).save(
    os.path.join(OUT, 'bridge_%s_1_before.png' % camera))
label(after_last, 'BRIDGE %s, AFTER (the rounding fix) - frame %d' % (camera, first + count - 1)).save(
    os.path.join(OUT, 'bridge_%s_2_after.png' % camera))
sheet = Image.new('RGB', (1600, 1800))
sheet.paste(label(signed(L(after_last) - L(before_last), 8), 'after - before, one frame, x8   (green: after is brighter, red: after is darker)'), (0, 0))
sheet.paste(label(signed(d, 8), 'after - before, averaged over %d frames, x8' % count), (0, 900))
sheet.save(os.path.join(OUT, 'bridge_%s_3_diff.png' % camera))
print('wrote bridge_%s_{1_before,2_after,3_diff}.png' % camera)
