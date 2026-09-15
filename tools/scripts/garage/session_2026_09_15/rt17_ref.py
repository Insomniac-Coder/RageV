# -*- coding: utf-8 -*-
"""RT-17: a converged reference per pose, so a moving object's lag can be seen at all.

The moving run's frame k against a second run whose object stops exactly where it is at
frame k and then stands still long enough to settle (the mean of frames 150-169). What the
moving frame holds beyond this frame's noise is what the histories kept of where the object
was. The no-averaging arm (refl_memory_check's variant) is the noise alone: its error
against the same reference has no lag in it, so the error the averaged arm has *beyond* it
is the ghost.

Usage: rt17_ref.py [render]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_14'))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import stage_run  # noqa: E402
import rt13_stage2 as s2  # noqa: E402
import refl_memory_check  # noqa: E402,F401

FT = 0.0166
K = 75   # mid-drive: the car moves at 1 m/s for 2 s
CASES = {
    'carwide': ('owner', lambda stop: dict(BURST_SLIDE='=porsche_992_gt3_r|1.0|%g' % stop)),
    'car': ('close', lambda stop: dict(BURST_SLIDE='=porsche_992_gt3_r|1.0|%g' % stop)),
}
OUT = os.path.join(stage_run.ROOT, 'build', 'rt17')
S = lambda tag, k: os.path.join(stage_run.SHOTS, 'rt5b_%s_%d.png' % (tag, k))


def lum(p):
    a = np.asarray(Image.open(p).convert('RGB'), dtype=float)[:860]
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


if __name__ == '__main__':
    if 'render' in sys.argv:
        for case, (cam, slide) in CASES.items():
            # Stop where frame K stands: the slide has run K frames of FT each by then.
            opts = dict(frames=20, first=150, mean_from=150, **slide(K * FT))
            s2.arms_at(cam, [('r17_%s_ref%d' % (case, K), 'ship', [], opts)])
    os.makedirs(OUT, exist_ok=True)
    for case, (cam, slide) in CASES.items():
        ref = np.load(os.path.join(stage_run.OUT, 'r17_%s_ref%d.npy' % (case, K)))
        ref = 0.2126 * ref[..., 0] + 0.7152 * ref[..., 1] + 0.0722 * ref[..., 2]
        ref = ref[:860]
        # Which neighbouring moving frame the stopped car matches: the pose check.
        errs = {k: np.abs(lum(S('r17_%s_ship' % case, k)) - ref).mean() for k in range(K - 3, K + 4)}
        best = min(errs, key=errs.get)
        ship = lum(S('r17_%s_ship' % case, best))
        noavg = lum(S('r17_%s_noavg' % case, best))
        e_ship, e_noavg = np.abs(ship - ref), np.abs(noavg - ref)
        print('%s: the stopped car matches moving frame %d (mean error by frame: %s)'
              % (case, best, ', '.join('%d:%.2f' % (k, v) for k, v in sorted(errs.items()))))
        for name, (y0, y1) in (('whole', (0, 860)), ('lower half (floor)', (430, 860))):
            print('   %-20s error vs settled: averaged %.2f, one frame %.2f   px where averaged is worse by >16: %.2f%%'
                  % (name, e_ship[y0:y1].mean(), e_noavg[y0:y1].mean(),
                     ((e_ship - e_noavg)[y0:y1] > 16).mean() * 100))
        excess = np.clip(e_ship - e_noavg, 0, None)
        heat = np.zeros(ship.shape + (3,))
        heat[..., 0] = np.clip(excess * 6.0, 0, 255)
        img = lambda p: np.asarray(Image.open(p).convert('RGB'))[:860]
        refimg = np.load(os.path.join(stage_run.OUT, 'r17_%s_ref%d.npy' % (case, K)))[:860]
        s2._sheet(os.path.join(OUT, '%s_ghost_%d.png' % (case, best)),
                  [img(S('r17_%s_ship' % case, best)), refimg, heat],
                  ['moving, frame %d' % best, 'stopped there, settled', 'where the moving frame is wrong beyond its noise (x6)'])
