# -*- coding: utf-8 -*-
"""RT-5 part 5, second half: the occlusion and indirect-light blurs, off against on.

The young-history blur spreads a signal while its history is short -- a
disocclusion, a camera move, the first frames after a cut -- and fades to
nothing by 32 frames of history. Parked and converged it does nothing, so it is
measured where it acts:

  dolly  burst.py's camera dolly (0.6 m/s for 1.5 s, then still), frames 40-119,
         against rt20_measure's SSAA truth of the same frames: per-frame change
         beyond the truth's on its edges (noise), the error to the truth, and
         pictures mid-move.
  park   parked, frames 150-189: whether a converged still moves at all.

Arms: on (the tuning's 6 and 12 texels), --ao-blur=0, --gi-blur=0, both 0.

Usage: rt5p5_blur.py run dolly|park [arm ...] | analyse dolly|park | sheet
"""
import os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stage_run  # noqa: E402
import rt20_measure as M  # noqa: E402

ARMS = {'on': [], 'ao0': ['--ao-blur=0'], 'gi0': ['--gi-blur=0'], 'off': ['--ao-blur=0', '--gi-blur=0']}
OPTS = {'dolly': dict(first=40, frames=80, speed=0.6, stop=1.5), 'park': dict(first=150, frames=40)}


def tag(case, arm):
    return 'bl_%s_%s' % (case, arm)


def run(case, arms):
    stage_run.run_arms([(tag(case, a), 'ship', ARMS[a], dict(OPTS[case])) for a in arms])


def analyse(case):
    first, count = OPTS[case]['first'], OPTS[case]['frames']
    arms = [a for a in ARMS if os.path.exists(os.path.join(M.S, 'rt5b_%s_%d.png' % (tag(case, a), first + count - 1)))]
    X = {a: [M.luma(M.rgb(tag(case, a), k)) for k in range(first, first + count)] for a in arms}
    if case == 'park':
        ref = np.mean(X['on'], axis=0)
        print('parked 40-frame means against blur on: mean |diff| / max / pixels over 1 level')
        for a in arms:
            d = np.abs(np.mean(X[a], axis=0) - ref)
            print('  %-4s %.4f / %.2f / %.4f%%' % (a, d.mean(), d.max(), 100 * (d > 1).mean()))
        return
    T = [M.luma(M.rgb(M.tag('dolly', 'truth'), k)) for k in range(first, first + count)]
    spans = [('moving', first, 89), ('stopped', 92, first + count - 1)]
    for sname, a0, b0 in spans:
        print('%s, frames %d-%d' % (sname, a0, b0))
        for a in arms:
            noise, err, flat_err = [], [], []
            for k in range(a0, b0):
                i = k - first
                m = M.edge_mask(T[i])
                noise.append(np.abs((X[a][i + 1] - X[a][i]) - (T[i + 1] - T[i]))[m].mean())
                e = np.abs(X[a][i] - T[i])
                err.append(e.mean())
                flat_err.append(e[~m].mean())
            print('  %-4s change beyond the truth\'s on its edges %.2f; error to truth %.2f (flat %.2f)'
                  % (a, np.mean(noise), np.mean(err), np.mean(flat_err)))


def sheet():
    """Mid-dolly crops: the floor under the car and the wall's base, each arm,
    and each arm minus blur-on at x4."""
    from PIL import Image
    first = OPTS['dolly']['first']
    boxes = {'car and floor': (380, 620, 300, 820), 'wall base': (300, 520, 900, 1420)}
    up = lambda v: np.kron(v, np.ones((2, 2, 1), dtype=np.float32))
    for bname, (y0, y1, x0, x1) in boxes.items():
        rows = []
        for k in (60, 80):
            on = M.rgb(tag('dolly', 'on'), k)[y0:y1, x0:x1]
            row = [M.label(up(M.rgb(tag('dolly', a), k)[y0:y1, x0:x1]), '%s: blur %s, frame %d' % (bname, a, k))
                   for a in ('on', 'off')]
            row.append(M.label(up(M.rgb(M.tag('dolly', 'truth'), k)[y0:y1, x0:x1]), 'truth (SSAA), frame %d' % k))
            rows.append(row)
            rows.append([M.label(up(M.signed(M.luma(M.rgb(tag('dolly', a), k)[y0:y1, x0:x1]) - M.luma(on), 4)),
                                 '%s minus on, x4 (green brighter)' % a) for a in ('ao0', 'gi0', 'off')])
        M.sheet(rows, os.path.join(M.OUT, 'blur_%s.png' % bname.replace(' ', '_')))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if sys.argv[1] == 'run':
        run(sys.argv[2], sys.argv[3:] or list(ARMS))
    elif sys.argv[1] == 'analyse':
        analyse(sys.argv[2])
    else:
        sheet()
