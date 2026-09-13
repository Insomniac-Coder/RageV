# -*- coding: utf-8 -*-
"""RT-5 part 4: prove the anti-lag or delete it.

The anti-lag (built 2026-09-09, off): in the reflection accumulator and the
temporal resolve, where the surface tests agree and the pixel did not move, a
history whose mean sits more than N of the pixel's own standard deviations from
what its neighbours report now is news, and the memory restarts. `--anti-lag=N`,
zero off; `--anti-lag-floor` under the noise estimate (0.02).

It exists for RT-16 -- a light switched off leaves the floor seconds later -- so
it is proven on that arm or not at all, and it is only worth having if a still
picture does not pay for it:

  fade   the car's four Realtime lamps on from the start, the owner's lights
         button pressed off at 8.3 s (stage_run.lamps_off_scene), frames 470-899:
         rt5_fade_analyse.py's curve, off against each N.
  park   the garage parked, frames 150-189: per-frame change on edges and flat
         pixels (rt20_measure's regions), and the 40-frame mean against off.
  bridge the Deck camera parked, frames 136-199: the same, by thirds.

Usage: rt5p4_antilag.py run fade|park|bridge [N ...]   (N 0 is off)
       rt5p4_antilag.py analyse park|bridge [N ...]
"""
import os, subprocess, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stage_run  # noqa: E402
import rt20_measure as M  # noqa: E402
import rt20_bridge as BR  # noqa: E402


def flags(n):
    return ['--anti-lag=%g' % n] if n > 0 else []


def tag(case, n):
    return 'al_%s_%g' % (case, n)


def run(case, ns):
    if case == 'fade':
        stage_run.run_arms([(tag('fade', n), 'ship', flags(n), dict(first=470, frames=430, lamps_off_at=8.3))
                            for n in ns])
    elif case == 'park':
        stage_run.run_arms([(tag('park', n), 'ship', flags(n), dict(first=150, frames=40)) for n in ns])
    elif case == 'bridge':
        os.makedirs(BR.SHOTS, exist_ok=True)
        for n in ns:
            t = tag('bridge', n)
            for f in BR.frames_of(t):
                os.remove(os.path.join(BR.SHOTS, f))
            cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'),
                   '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                   '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
                   '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off',
                   '--camera=' + BR.CAMERAS['deck'], '--screenshot=' + os.path.join(BR.SHOTS, t + '.png'),
                   '--screenshot-frame=%d' % BR.FIRST, '--screenshot-count=%d' % BR.COUNT] + flags(n)
            p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=3600, errors='replace')
            print('%-16s %d frames %s' % (t, len(BR.frames_of(t)), ' '.join(flags(n))))
            if len(BR.frames_of(t)) != BR.COUNT:
                print(p.stdout[-2000:], p.stderr[-2000:])
                sys.exit('bridge arm %s incomplete' % t)


def change_table(frames, bands, label):
    out = []
    for name, sl in bands.items():
        e, f = [], []
        for i in range(len(frames) - 1):
            A, B = frames[i][sl], frames[i + 1][sl]
            d = np.abs(B - A)
            m = M.edge_mask(A)
            if m.any():
                e.append(d[m].mean())
            f.append(d[~m].mean())
        out.append('%s %5.2f/%4.2f' % (name, np.mean(e) if e else float('nan'), np.mean(f)))
    print('  %-8s %s' % (label, '  '.join(out)))


def analyse(case, ns):
    if case == 'park':
        L = {n: [M.luma(M.rgb(tag('park', n), k)) for k in range(150, 190)] for n in ns}
        h, w = L[ns[0]][0].shape
        bands = M.regions(h, w)
    else:
        L = {}
        for n in ns:
            fs = BR.frames_of(tag('bridge', n))
            L[n] = [M.luma(np.asarray(Image.open(os.path.join(BR.SHOTS, f)).convert('RGB'), dtype=np.float32))
                    for f in fs]
        h, w = L[ns[0]][0].shape
        bands = {'top': slice(0, h // 3), 'middle': slice(h // 3, 2 * h // 3), 'bottom': slice(2 * h // 3, h)}
        bands = {k: (v, slice(0, w)) for k, v in bands.items()}
    print('%s, per-frame change on edge / flat pixels, levels' % case)
    for n in ns:
        change_table(L[n], bands, 'off' if n == 0 else 'N=%g' % n)
    if 0 in L:
        ref = np.mean(L[0], axis=0)
        print('the whole-run mean against off: mean |diff| / pixels over 2 levels')
        for n in ns:
            d = np.abs(np.mean(L[n], axis=0) - ref)
            print('  %-8s %.3f / %.3f%%' % ('off' if n == 0 else 'N=%g' % n, d.mean(), 100 * (d > 2).mean()))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ns = [float(x) for x in sys.argv[3:]] or [0.0, 2.0, 3.0]
    ns = [int(x) if x == int(x) else x for x in ns]
    if sys.argv[1] == 'run':
        run(sys.argv[2], ns)
    else:
        analyse(sys.argv[2], ns)
