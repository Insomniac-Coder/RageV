# -*- coding: utf-8 -*-
"""The water test (2026-09-14, owner: "yes run the water test").

Does the bridge's lamp light linger on the sea after the lamps go out, the way the
car's headlamp pool lingered on the garage floor?

The bridge's 120 sodium lamps are switched off at 3.0 s (the engine's Switcher,
as the car's lights button is staged for the garage), at the glitter camera, and
frames 170-290 are kept. Two more runs of the same camera give, frame for frame,
the lamps' light itself: lamps switched off long before (0.5 s) and lamps never
switched off (99.5 s -- not a whole number: see RT-MEASURED-CHANGE's traps). The
waves and the beacons run on the scene clock, so they are the same in all three.
Each three runs once with measured change off and once as the engine ships.

The scene is edited in place and restored byte for byte after every run: its
baked lighting is found by the scene's own name, so a renamed copy would render
without it.

Usage: water_test.py run [arm ...]   |   water_test.py analyse
"""
import io, os, re, subprocess, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
import stage_run  # noqa: E402
import rt20_bridge as BR  # noqa: E402

SCENES = stage_run.SCENES
SCENE = os.path.join(SCENES, 'GoldenGateDemo.rage')
META = os.path.join(SCENES, 'goldengatedemo.rage.meta')
OUT = os.path.join(stage_run.OUT, 'water')
CAMERA = BR.CAMERAS['glitter']
FIRST, COUNT = 170, 121
SWITCH = {'fade': 3.0, 'truth': 0.5, 'lit': 99.5}
ARMS = [('fade_off', 'fade', ['--measured-change=off']), ('truth_off', 'truth', ['--measured-change=off']),
        ('lit_off', 'lit', ['--measured-change=off']),
        ('fade_on', 'fade', []), ('truth_on', 'truth', []), ('lit_on', 'lit', [])]


def switched(raw, seconds):
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8').replace('\r\n', '\n')
    block = ('    NativeScriptComponent:\n      Script: Switcher\n      Fields:\n'
             '        AtSeconds: %g\n        Intensity: 0\n        Emissive: 0\n' % seconds)
    out, count = [], 0
    for part in re.split(r'(?=\n  - EntityID: )', text):
        tag = re.search(r'\n      Tag: (.*)', part)
        if tag and tag.group(1).strip().startswith('Sodium ') and 'LightComponent' in part:
            if 'NativeScriptComponent' in part:
                sys.exit('%s already has a native script' % tag.group(1))
            body = part.rstrip('\n')
            part = body + '\n' + block.rstrip('\n') + part[len(body):]
            count += 1
        out.append(part)
    if count != 120:
        sys.exit('the switch attached to %d sodium lamps, not 120' % count)
    text = ''.join(out)
    return (text.replace('\n', '\r\n') if crlf else text).encode('utf-8')


def frames_of(tag):
    got = [f for f in os.listdir(OUT) if re.match(re.escape(tag) + r'_\d+\.png$', f)]
    return sorted(got, key=lambda f: int(re.search(r'_(\d+)\.png$', f).group(1)))


def run(only, first=None, count=None, extra=(), prefix=''):
    global FIRST, COUNT
    FIRST = first if first is not None else FIRST
    COUNT = count if count is not None else COUNT
    os.makedirs(OUT, exist_ok=True)
    scene_bytes = io.open(SCENE, 'rb').read()
    meta_bytes = io.open(META, 'rb').read() if os.path.exists(META) else None
    try:
        for tag, kind, flags in ARMS:
            if only and tag not in only:
                continue
            flags = list(flags) + list(extra)
            tag = prefix + tag
            for f in frames_of(tag):
                os.remove(os.path.join(OUT, f))
            io.open(SCENE, 'wb').write(switched(scene_bytes, SWITCH[kind]))
            cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'),
                   '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                   '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                   '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off',
                   '--camera=' + CAMERA, '--screenshot=' + os.path.join(OUT, tag + '.png'),
                   '--screenshot-frame=%d' % FIRST, '--screenshot-count=%d' % COUNT] + flags
            p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=3600, errors='replace')
            # A single frame is written under the name as given, not numbered.
            got = 1 if COUNT == 1 and os.path.exists(os.path.join(OUT, tag + '.png')) else len(frames_of(tag))
            print('%-10s %d frames, lamps off at %g s %s' % (tag, got, SWITCH[kind], ' '.join(flags)))
            sys.stdout.flush()
            if got != COUNT:
                print(p.stdout[-3000:], p.stderr[-3000:])
                sys.exit('%s incomplete' % tag)
    finally:
        io.open(SCENE, 'wb').write(scene_bytes)
        if meta_bytes is not None:
            io.open(META, 'wb').write(meta_bytes)
        ok = io.open(SCENE, 'rb').read() == scene_bytes and (
            meta_bytes is None or io.open(META, 'rb').read() == meta_bytes)
        print('scene and meta restored byte for byte:', ok)


def luma(tag, n):
    a = np.asarray(Image.open(os.path.join(OUT, '%s_%d.png' % (tag, n))).convert('RGB'), dtype=np.float32)
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


if __name__ == '__main__':
    if len(sys.argv) >= 2 and sys.argv[1] == 'run':
        run(sys.argv[2:])
    elif len(sys.argv) >= 3 and sys.argv[1] == 'capture':
        # The histories' own values at one frame (--capture-signals), for the
        # arms named: which filter still holds the lamps' light.
        run(sys.argv[3:], first=int(sys.argv[2]), count=1,
            extra=['--capture-signals=direct,reflections,gi,taa'], prefix='cap%s_' % sys.argv[2])
    else:
        sys.exit(__doc__)
