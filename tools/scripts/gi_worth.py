"""RT2-2e: what the bounce is worth on screen -- GI on against GI off, by diff image.

    python tools/scripts/gi_worth.py                 # every case
    python tools/scripts/gi_worth.py room garage     # some of them
    python tools/scripts/gi_worth.py leaks           # the leak fixtures, bake against live

Each case renders twice from the same parked camera -- the engine's own
screenshots, sixteen frames from frame 400 averaged (a live bounce never reports
settled: its cache keeps solving, and one frame of it moves a level or so
against the next, which the average takes out) -- once as the scene is set up
(with `--gi-source=realtime` where a bake would otherwise stand in for the live
bounce), once with `--gi=off` added, which removes every form of realtime
indirect light and nothing else. The difference is in display levels (0-255 of
the tone-mapped picture, luminance): its mean over the lit region (pixels at
least 8 levels bright in either picture), the share of that region moving by
two levels or more, and a diff image (x8) beside the two pictures in
build/rt2-2e/. The new GI scenes must show at least two levels over their lit
region (RT2-2e's gate); the garage and the camp are re-recorded, as RT-3's
+0.38 and +0.59 predate the fix that made the bounce's aimed rays work.
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RUNTIME = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
OUT = os.path.join(ROOT, 'build', 'rt2-2e')
REALTIME = ['--gi-source=realtime']

# name: (scene, camera or None for the scene's own, extra flags, new GI scene?)
CASES = {
    'room':     ('scenes/gi_room_bright.rage', None, [], True),
    # The same with the sky's light darkened where the sky cannot be seen,
    # which the project leaves off: what of the room's light is the sky's.
    'roomsky':  ('scenes/gi_room_bright.rage', None, ['--rt-sky-visibility=full'], True),
    'daylit':   ('scenes/showroom_daylit.rage', '0,1.355368,7.262258,0.01,0,5.000002', REALTIME, True),
    'garage':   ('scenes/showroom.rage', '0,1.355368,7.262258,0.01,0,5.000002', REALTIME, False),
    # The garage's second lighting mode, reached through the scene's own
    # ShowroomMode script. (The roadmap's "bright bay" was the old showroom's;
    # its four downlights left the scene in d34c905, so mode 2 today is the
    # brighter key and fills of the same garage.)
    # Scripts are created on the first 1/60 s fixed step, which the 1 us clock
    # below never reaches -- so this case runs on a 60 Hz clock (a later
    # --frame-time wins); --camera has already taken the camera's own script off.
    'mode2':    ('scenes/showroom.rage', '0,1.355368,7.262258,0.01,0,5.000002',
                 REALTIME + ['--script-field=ShowroomMode.StartMode=2', '--frame-time=0.0166667'], False),
    'camp':     ('scenes/camp.rage', '3.457404,4.595,6.063566,0.01,-26.000019,30.000013', REALTIME, False),
}

# The leak fixtures (`leaks` on the command line): sealed rooms, the bake
# against the live bounce from the same camera. The sealed room should read
# the same either way, so the mean |diff| over the frame is what the bake
# leaks (the original black-sky four held 0.000 on all but _s3). The _sky
# twins add a grey sky and ambient 0.25, which a bake hit takes with no
# visibility (C-N3) -- the leak those four could not show.
#
# A name ending in `+vis` runs with the sky's light darkened where the sky
# cannot be seen (`--rt-sky-visibility=full`). The project leaves that off,
# and then the pixel itself takes the whole sky inside the sealed room, which
# reads near white at the fixtures' fixed exposure and buries the bake's own
# leak under the pixel's; with it on, the live room is dark and what the bake
# adds stands out.
LEAKS = ['irradiance_leak', 'irradiance_leak_sky', 'irradiance_leak_point', 'irradiance_leak_point_sky',
         'irradiance_leak_s2', 'irradiance_leak_s2_sky', 'irradiance_leak_s3', 'irradiance_leak_s3_sky',
         'irradiance_leak_sky+vis', 'irradiance_leak_point_sky+vis', 'irradiance_leak_s2_sky+vis',
         'irradiance_leak_s3_sky+vis']


def leaks(names):
    print(f"{'fixture':30s} {'baked':>7s} {'live':>7s} {'mean |baked-live|':>18s} {'max':>6s}")
    for name in names:
        scene = 'scenes/%s.rage' % name.split('+')[0]
        flags = ['--rt-sky-visibility=full'] if name.endswith('+vis') else []
        tag = name.replace('+', '_')
        baked = luminance(render(scene, None, flags, os.path.join(OUT, f'{tag}_baked.png')))
        live = luminance(render(scene, None, flags + REALTIME, os.path.join(OUT, f'{tag}_live.png')))
        diff = np.abs(baked - live)
        Image.fromarray(np.clip(diff * 8, 0, 255).astype(np.uint8)).save(os.path.join(OUT, f'{tag}_diff_x8.png'))
        print(f"{name:30s} {baked.mean():7.1f} {live.mean():7.1f} {diff.mean():18.3f} {diff.max():6.1f}", flush=True)


FIRST, COUNT = 400, 16


def render(scene, camera, flags, out):
    """The mean of COUNT frames from FIRST, written to `out`; returned as RGB floats."""
    command = [os.path.join(RUNTIME, 'RageVRuntime.exe'),
               '--project=' + os.path.join(ROOT, 'SampleProject', 'SampleProject.rvproject'),
               '--rhi=vulkan', '--render-defaults=off', '--vsync=off', '--width=1600', '--height=900',
               '--frame-cap=30', '--frame-time=0.000001', '--screenshot-frame=%d' % FIRST,
               '--screenshot-count=%d' % COUNT, '--benchmark=%d' % (FIRST + COUNT + 2),
               '--scene=' + scene, '--screenshot=' + out] + (['--camera=' + camera] if camera else []) + flags
    p = subprocess.run(command, cwd=RUNTIME, capture_output=True, text=True, errors='replace',
                       timeout=3600, stdin=subprocess.DEVNULL)
    frames = ['%s_%d.png' % (out[:-4], f) for f in range(FIRST, FIRST + COUNT)]
    missing = [f for f in frames if not os.path.exists(f)]
    if p.returncode != 0 or missing:
        raise SystemExit(f'{out}: exit {p.returncode}, {len(missing)} frame(s) missing\n'
                         + '\n'.join(p.stdout.splitlines()[-5:]))
    mean = np.mean([np.asarray(Image.open(f).convert('RGB')).astype(np.float64) for f in frames], axis=0)
    for f in frames:
        os.remove(f)
    Image.fromarray(np.clip(mean + 0.5, 0, 255).astype(np.uint8)).save(out)
    return mean


def luminance(rgb):
    return rgb @ np.array([0.2126, 0.7152, 0.0722])


def main():
    names = sys.argv[1:] or list(CASES)
    os.makedirs(OUT, exist_ok=True)
    if names[0] == 'leaks':
        return leaks(names[1:] or LEAKS)
    print(f"{'case':8s} {'lit px':>7s} {'mean on-off':>12s} {'mean |diff|':>12s} {'>=2 levels':>11s}   gate")
    for name in names:
        scene, camera, flags, new = CASES[name]
        on, off = os.path.join(OUT, f'{name}_on.png'), os.path.join(OUT, f'{name}_off.png')
        a = luminance(render(scene, camera, flags, on))
        b = luminance(render(scene, camera, flags + ['--gi=off'], off))
        lit = (a >= 8) | (b >= 8)
        diff = a - b
        moved = np.abs(diff) >= 2
        Image.fromarray(np.clip(np.abs(diff) * 8, 0, 255).astype(np.uint8)).save(
            os.path.join(OUT, f'{name}_diff_x8.png'))
        mean_signed = diff[lit].mean() if lit.any() else 0.0
        mean_abs = np.abs(diff[lit]).mean() if lit.any() else 0.0
        share = moved[lit].mean() * 100 if lit.any() else 0.0
        gate = ('PASS' if mean_abs >= 2 else 'FAIL') if new else 're-recorded'
        print(f"{name:8s} {lit.mean() * 100:6.1f}% {mean_signed:+12.2f} {mean_abs:12.2f} {share:10.1f}%   {gate}",
              flush=True)


if __name__ == '__main__':
    main()
