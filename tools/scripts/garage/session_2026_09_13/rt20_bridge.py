# -*- coding: utf-8 -*-
"""RT-20 on the bridge: both fixes against the resolve as shipped, parked at a camera.

The bridge is where RT-6 was judged ("visibly sharper" members at the Deck
camera) and where the sub-pixel geometry is -- suspender ropes, pickets, lamp
standards -- which a jitter flip and the surface test treat worst: a rope under
a pixel is sampled in some frames and missed in others. Arms, staged the way
stage_run stages them (the TAA shader only, restored byte for byte):

  ship   the resolve as committed
  A, B   rt20_arms.py's two fixes
  nogeo  --taa-geometry=off, the resolve before RT-6

Usage: rt20_bridge.py run [camera] [arm ...]  |  rt20_bridge.py analyse [camera]
"""
import io, os, re, shutil, subprocess, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stage_run  # noqa: E402
import rt20_arms  # noqa: E402,F401
import rt20_measure as M  # noqa: E402

ROOT = stage_run.ROOT
RT = stage_run.RT
SHOTS = os.path.join(stage_run.OUT, 'rt20', 'bridge')
CAMERAS = {
    'deck':    '0,76.4,950,0.01,0,0',
    'pier':    '70,4.5,705,0.01,-46.98,-2.86',
    'glitter': '500,2.5,180,0.01,-90,-1.146',
}
FIRST, COUNT = 136, 64
ARMS = [('ship', 'head', []), ('A', 'r20A', []), ('B', 'r20B', []), ('C', 'r20C', []),
        ('nogeo', 'head', ['--taa-geometry=off']), ('F', 'ship', [])]


def frames_of(t):
    got = [f for f in os.listdir(SHOTS) if re.match(re.escape(t) + r'_\d+\.png$', f)]
    return sorted(got, key=lambda f: int(re.search(r'_(\d+)\.png$', f).group(1)))


def run(camera, only):
    os.makedirs(SHOTS, exist_ok=True)
    staged = {v: stage_run.build_variant(v) for a, v, _ in ARMS if not only or a in only}
    try:
        for arm, variant, flags in ARMS:
            if only and arm not in only:
                continue
            if not stage_run.restore():
                sys.exit('staged copies did not restore before %s' % arm)
            for shader, text in staged[variant].items():
                io.open(os.path.join(stage_run.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
            t = '%s_%s' % (camera, arm)
            for f in frames_of(t):
                os.remove(os.path.join(SHOTS, f))
            cmd = [os.path.join(RT, 'RageVRuntime.exe'), '--project=' + os.path.join(ROOT, 'SampleProject'),
                   '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                   '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off',
                   '--camera=' + CAMERAS[camera], '--screenshot=' + os.path.join(SHOTS, t + '.png'),
                   '--screenshot-frame=%d' % FIRST, '--screenshot-count=%d' % COUNT] + flags
            p = subprocess.run(cmd, cwd=RT, capture_output=True, text=True, timeout=3600, errors='replace')
            if len(frames_of(t)) != COUNT:
                print(p.stdout[-2000:], p.stderr[-2000:])
                sys.exit('%s: %d frames of %d' % (t, len(frames_of(t)), COUNT))
            print('%-14s %-5s %d frames %s' % (t, variant, COUNT, ' '.join(flags)))
            sys.stdout.flush()
    finally:
        print('staged copies restored and identical to source:', stage_run.restore())


def analyse(camera):
    L = {}
    for arm, _, _ in ARMS:
        t = '%s_%s' % (camera, arm)
        if len(frames_of(t)) == COUNT:
            L[arm] = [M.luma(np.asarray(Image.open(os.path.join(SHOTS, f)).convert('RGB'), dtype=np.float32))
                      for f in frames_of(t)]
    h, w = L['ship'][0].shape
    # Thirds of the frame, top to bottom, and edges by the first frame's gradient.
    bands = {'top': slice(0, h // 3), 'middle': slice(h // 3, 2 * h // 3), 'bottom': slice(2 * h // 3, h)}
    print('bridge %s, parked, frames %d-%d: per-frame change on edge / flat pixels, levels' % (camera, FIRST, FIRST + COUNT - 1))
    for arm, frames in L.items():
        out = []
        for name, sl in bands.items():
            e, f = [], []
            for i in range(COUNT - 1):
                A, B = frames[i][sl], frames[i + 1][sl]
                d = np.abs(B - A)
                m = M.edge_mask(A)
                e.append(d[m].mean()); f.append(d[~m].mean())
            out.append('%s %5.2f/%4.2f' % (name, np.mean(e), np.mean(f)))
        print('  %-6s %s' % (arm, '  '.join(out)))


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'analyse'
    camera = sys.argv[2] if len(sys.argv) > 2 else 'deck'
    if cmd == 'run':
        run(camera, sys.argv[3:])
    else:
        analyse(camera)
