"""Burst-capture the garage under a camera dolly, and count the smear.

The reflection accumulator's failure is a reprojection that finds the wrong
surface, and a yaw about the eye cannot show it: under a pure rotation every
point on a line of sight lands on the same texel, so the history is right
wherever it is read from. A translation slides the near poles across the far
wall, which is where it goes wrong. So this copies showroom.rage, swaps the
camera's orbit script for the engine's Slider (Source/Slider.cpp: world X at
Speed m/s for StopAfter seconds, then still), bursts N frames, and deletes
the copy.

Usage:
    burst.py <tag> [--speed=0.6] [--stop=1.5] [--frames=150] [--from=40]
                   [--size=1600x900] [--extra=--flag ...]
    burst.py --parked <tag>            one frame at 60, no motion
    burst.py --analyse <tag>           the numbers again, no render

Numbers, per frame, on the pole band (the chrome poles right of the car in
the owner's shot, and the wall between them):
  trail  -- the luminance that the wall between the poles carries above the
            parked reference: a pole's reflection smeared onto the wall.
  settle -- after the dolly stops, the mean absolute difference to the last
            frame, so the frame it falls under a level is the settle time.
"""
import os, re, shutil, subprocess, sys, glob
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
# BURST_RUNTIME points the runs at another build (a worktree of an older
# commit, for a before/after); BURST_SCENE at another scene file in the
# scenes folder (the studio showroom kept from the sign-off commit); BURST_CAM
# at another pose. Defaults are the garage at the owner's shot.
RT = os.environ.get('BURST_RUNTIME', os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime'))
PROJECT = os.environ.get('BURST_PROJECT', os.path.join(ROOT, 'SampleProject'))
SCENES = os.path.join(PROJECT, 'assets', 'scenes')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')
SCENE = os.environ.get('BURST_SCENE', 'showroom.rage')
# The owner's shot: the orbit script's own defaults -- target (-2.3, 0.72,
# -2), distance 11, yaw 0, pitch 4 -- as --camera takes them.
CAM = os.environ.get('BURST_CAM', '-2.3,0.72,-2,11,0,4')


# BURST_MODE=2 authors the studio showroom's mode 2 into the copy -- what the
# ShowroomMode script's Apply() does at a button press, done to the file, so
# a run with no managed scripts (a worktree build has none) still opens in
# the bright room. The values are the script's own defaults at the pre-bridge
# commit; the probe is set to re-capture live, since its bake is mode 1's.
MODE2 = [
    ('Panel Light 00', 'Intensity', '14.7'), ('Panel Light 01', 'Intensity', '14.7'),
    ('Panel Light 02', 'Intensity', '14.7'), ('Panel Light 10', 'Intensity', '14.7'),
    ('Panel Light 11', 'Intensity', '14.7'), ('Panel Light 12', 'Intensity', '14.7'),
    ('Panel Light 20', 'Intensity', '14.7'), ('Panel Light 21', 'Intensity', '14.7'),
    ('Panel Light 22', 'Intensity', '14.7'),
    ('Kicker Left', 'Intensity', '26'), ('Kicker Right', 'Intensity', '26'),
    ('Bay Downlight Light 0', 'Intensity', '4.5'), ('Bay Downlight Light 1', 'Intensity', '4.5'),
    ('Bay Downlight Light 2', 'Intensity', '4.5'), ('Bay Downlight Light 3', 'Intensity', '4.5'),
    ('Key Light', 'Intensity', '46'), ('Key Light', 'InnerCone', '34'), ('Key Light', 'OuterCone', '62'),
    ('Luminaire', 'Material', '10734442940267648324'),
    ('Bay Downlight 0', 'OverrideEmissive', 'false'), ('Bay Downlight 1', 'OverrideEmissive', 'false'),
    ('Bay Downlight 2', 'OverrideEmissive', 'false'), ('Bay Downlight 3', 'OverrideEmissive', 'false'),
    ('Showroom Probe', 'Update', 'Realtime'),
]


def set_field(text, tag, field, value):
    start = text.find('      Tag: %s\n' % tag)
    assert start >= 0, 'no entity tagged ' + tag
    end = text.find('  - EntityID:', start)
    if end < 0:
        end = len(text)
    block = text[start:end]
    new_block, n = re.subn(r'^(\s+%s: ).*$' % re.escape(field), lambda m: m.group(1) + value, block, count=1, flags=re.M)
    assert n == 1, 'no field %s on %s' % (field, tag)
    return text[:start] + new_block + text[end:]


def make_scene(speed, stop):
    src = os.path.join(SCENES, SCENE)
    dst = os.path.join(SCENES, 'showroom_burst.rage')
    text = open(src, encoding='utf-8').read()
    if os.environ.get('BURST_MODE') == '2':
        for tag, field, value in MODE2:
            text = set_field(text, tag, field, value)
    # BURST_SWITCH="<tag prefix>[,<tag prefix>...]|<seconds>" attaches the
    # engine's Switcher (Source/Switcher.cpp) to every entity whose tag
    # starts with one of the prefixes: after <seconds> its light goes to
    # Intensity 0 and its mesh to a flat emissive of 0 -- the change test of
    # WR-16 R4 ("Tube ,Bottom light bars|1.328" switches the garage's tubes
    # and their lenses off at frame 80 of a --frame-time=0.0166 run).
    # BURST_SLIDE="<tag prefix>[,<tag prefix>...]|<speed m/s>|<stop s>" puts
    # the Slider (Source/Slider.cpp) on every entity whose tag starts with
    # one of the prefixes (a prefix written "=tag" must match the whole tag,
    # so a model's root does not take its parts along twice): the object
    # drives along world X while the camera,
    # at --speed=0, stands still -- the moving-object test of WR-16 R5
    # ("porsche_992_gt3_r|1.0|2.0" drives the car for two seconds).
    slide = os.environ.get('BURST_SLIDE')
    if slide:
        prefixes, speed_s, stop_s = slide.rsplit('|', 2)
        prefixes = [x for x in prefixes.split(',') if x]
        block = ('    NativeScriptComponent:\n      Script: Slider\n      Fields:\n'
                 '        Speed: %g\n        StopAfter: %g\n' % (float(speed_s), float(stop_s)))
        out = []
        count = 0
        for part in re.split(r'(?=\n  - EntityID: )', text):
            tag = re.search(r'\n      Tag: (.*)', part)
            if tag and any((tag.group(1).strip() == pfx[1:]) if pfx.startswith('=')
                           else tag.group(1).strip().startswith(pfx) for pfx in prefixes):
                assert 'NativeScriptComponent' not in part, tag.group(1)
                body = part.rstrip('\n')
                part = body + '\n' + block.rstrip('\n') + part[len(body):]
                count += 1
            out.append(part)
        text = ''.join(out)
        assert count > 0, 'BURST_SLIDE matched no entity'
        print('slider attached to %d entities: %s m/s for %s s' % (count, speed_s, stop_s))
    switch = os.environ.get('BURST_SWITCH')
    if switch:
        prefixes, seconds = switch.rsplit('|', 1)
        prefixes = [x for x in prefixes.split(',') if x]
        block = ('    NativeScriptComponent:\n      Script: Switcher\n      Fields:\n'
                 '        AtSeconds: %g\n        Intensity: 0\n        Emissive: 0\n' % float(seconds))
        out = []
        count = 0
        for part in re.split(r'(?=\n  - EntityID: )', text):
            tag = re.search(r'\n      Tag: (.*)', part)
            if tag and any((tag.group(1).strip() == pfx[1:]) if pfx.startswith('=')
                           else tag.group(1).strip().startswith(pfx) for pfx in prefixes):
                assert 'NativeScriptComponent' not in part, tag.group(1)
                body = part.rstrip('\n')
                part = body + '\n' + block.rstrip('\n') + part[len(body):]
                count += 1
            out.append(part)
        text = ''.join(out)
        assert count > 0, 'BURST_SWITCH matched no entity'
        print('switcher attached to %d entities at %s s' % (count, seconds))
    old = re.search(r'    ManagedScriptComponent:\n      Script: ShowroomCamera\n'
                    r'      Fields:\n(?:        .*\n)+', text)
    assert old, 'no ShowroomCamera block'
    new = ('    NativeScriptComponent:\n      Script: Slider\n      Fields:\n'
           '        Speed: %g\n        StopAfter: %g\n' % (speed, stop))
    text = text[:old.start()] + new + text[old.end():]
    open(dst, 'w', encoding='utf-8', newline='\n').write(text)
    return dst


def run(tag, speed, stop, frames, start, size, extra):
    os.makedirs(SHOTS, exist_ok=True)
    for f in glob.glob(os.path.join(SHOTS, tag + '_*.png')):
        os.remove(f)
    scene = make_scene(speed, stop)
    out = os.path.join(SHOTS, tag + '.png')
    cmd = [os.path.join(RT, 'RageVRuntime.exe'),
           '--project=' + PROJECT,
           '--scene=scenes/showroom_burst.rage', '--rhi=vulkan',
           '--render-defaults=off', '--vsync=off',
           '--width=%d' % size[0], '--height=%d' % size[1],
           '--screenshot=' + out, '--screenshot-frame=%d' % start,
           '--screenshot-count=%d' % frames,
           '--import-cache=off', '--frame-time=0.0166',
           '--camera=' + CAM] + list(extra)
    try:
        p = subprocess.run(cmd, cwd=RT, capture_output=True, text=True,
                           timeout=1800, errors='replace')
    finally:
        try:
            os.remove(scene)
        except OSError:
            pass
        meta = scene + '.meta'
        if os.path.exists(meta):
            os.remove(meta)
    got = sorted(glob.glob(os.path.join(SHOTS, tag + '_*.png')),
                 key=lambda f: int(re.search(r'_(\d+)\.png$', f).group(1)))
    if frames == 1 and os.path.exists(out):
        got = [out]
    if not got:
        print(p.stdout[-2000:])
        print(p.stderr[-2000:])
        raise SystemExit('no frames')
    return got


def frames_of(tag):
    got = sorted(glob.glob(os.path.join(SHOTS, tag + '_*.png')),
                 key=lambda f: int(re.search(r'_(\d+)\.png$', f).group(1)))
    return got or [os.path.join(SHOTS, tag + '.png')]


def luma(path):
    a = np.asarray(Image.open(path).convert('RGB'), dtype=float)
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def analyse(tag, parked=None):
    got = frames_of(tag)
    first = luma(got[0])
    h, w = first.shape
    # The pole band in the owner's 2000x1230 shot: x 1160..1590, y 170..760.
    x0, x1 = int(w * 1160 / 2000), int(w * 1590 / 2000)
    y0, y1 = int(h * 170 / 1230), int(h * 760 / 1230)
    band = (slice(y0, y1), slice(x0, x1))
    ref = luma(parked)[band] if parked else None
    last = luma(got[-1])[band]
    print('%-6s %8s %8s %8s' % ('frame', 'mean', 'trail', 'settle'))
    rows = []
    for f in got:
        n = int(re.search(r'_(\d+)\.png$', f).group(1)) if '_' in os.path.basename(f) else 0
        L = luma(f)[band]
        trail = float(np.mean(np.maximum(L - ref, 0.0))) if ref is not None else float('nan')
        settle = float(np.mean(np.abs(L - last)))
        rows.append((n, float(L.mean()), trail, settle))
        print('%-6d %8.2f %8.3f %8.3f' % rows[-1])
    return rows


def main(argv):
    if argv and argv[0] == '--analyse':
        analyse(argv[1]); return
    parked = argv and argv[0] == '--parked'
    if parked:
        argv = argv[1:]
    tag = argv[0]
    opts = dict(a.lstrip('-').split('=', 1) for a in argv[1:] if '=' in a and not a.startswith('--extra'))
    extra = [a.split('=', 1)[1] for a in argv[1:] if a.startswith('--extra=')]
    speed = 0.0 if parked else float(opts.get('speed', 0.6))
    stop = float(opts.get('stop', 1.5))
    frames = 1 if parked else int(opts.get('frames', 150))
    start = int(opts.get('from', 60 if parked else 40))
    size = tuple(int(v) for v in opts.get('size', '1600x900').split('x'))
    got = run(tag, speed, stop, frames, start, size, extra)
    print('%d frame(s) -> %s' % (len(got), got[0]))
    if not parked:
        analyse(tag, opts.get('parked'))


if __name__ == '__main__':
    main(sys.argv[1:])
