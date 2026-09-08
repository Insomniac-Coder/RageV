# -*- coding: utf-8 -*-
"""RT-8 job 3: run the bridge and read what the shared gate would do to the sea.

The claim on the record is that the signal contract's geometric history gate
cannot hold a sea. The water's own accumulate now runs that gate beside its own
and acts on none of it, so this is the answer with no picture changed.
"""
import os, re, subprocess, sys

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')

CAMERAS = {
    'glitter': '500,2.5,180,0.01,-90,-1.146',
    'pier':    '70,4.5,705,0.01,-46.98,-2.86',
    'deck':    '0,76.4,950,0.01,0,0',
}

WANT = ('water under the shared gate', 'water gate refusals', 'water gate detail',
        'reflection history', 'temporal confidence')


def run(camera, frames=120):
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


for name, cam in CAMERAS.items():
    out = run(cam)
    print('=' * 74)
    print(name)
    print('=' * 74)
    hit = False
    for line in out.splitlines():
        if any(w in line for w in WANT):
            print('  ' + line.split('] ')[-1].strip())
            hit = True
    if not hit:
        print('  NO COUNTER LINES')
        for line in out.splitlines():
            if 'error' in line.lower() or 'did not compile' in line:
                print('  !! ' + line.strip())
        print(out[-1500:])
