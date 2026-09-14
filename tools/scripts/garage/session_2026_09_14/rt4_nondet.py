# -*- coding: utf-8 -*-
"""Run-to-run differences in the traced reflections (found 2026-09-14 re-measuring R11).

Runs a sequence of parked garage arms, frames 150-167, and prints for every pair
of arms with the same variant and flags whether their frames are bit-identical,
and if not, the first differing frame and how much. The order is the experiment:
an arm's result may depend on what ran before it.

Usage: rt4_nondet.py <sequence name>
"""
import itertools, os, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
import stage_run  # noqa: E402

src = open(os.path.join(HERE, 'rt4_r11.py'), encoding='utf-8').read()
exec(src.split("ARMS = [")[0].split("import rt20_measure as M  # noqa: E402")[1])   # the R11 variants

FIRST, COUNT = 150, 18
MC_OFF = ['--measured-change=off']
SEQUENCES = {
    # the same shipped arm before and after a staged variant
    'order': [('nd_s1', 'ship', []), ('nd_s2', 'ship', []), ('nd_t1', 'r11truth', []),
              ('nd_s3', 'ship', []), ('nd_s4', 'ship', []), ('nd_t2', 'r11truth', []), ('nd_s5', 'ship', [])],
    # and with the measured change off
    'ordermc': [('nd_m1', 'ship', MC_OFF), ('nd_m2', 'ship', MC_OFF), ('nd_mt1', 'r11truth', MC_OFF),
                ('nd_m3', 'ship', MC_OFF), ('nd_m4', 'ship', MC_OFF)],
}


def frames(tag):
    return [np.asarray(Image.open(os.path.join(stage_run.SHOTS, 'rt5b_%s_%d.png' % (tag, n))).convert('RGB'),
                       dtype=np.int16) for n in range(FIRST, FIRST + COUNT)]


def compare(seq):
    loaded = {t: frames(t) for t, _, _ in seq}
    for (ta, va, fa), (tb, vb, fb) in itertools.combinations(seq, 2):
        if va != vb or fa != fb:
            continue
        first, worst, px = None, 0, 0
        for i, (x, y) in enumerate(zip(loaded[ta], loaded[tb])):
            d = np.abs(x - y).max(axis=-1)
            if d.max() > 0:
                first = FIRST + i if first is None else first
                worst = max(worst, int(d.max()))
                px = max(px, int((d > 0).sum()))
        print('  %-7s %-7s %s' % (ta, tb, 'identical' if first is None else
                                  'differ from frame %d, up to %d levels, up to %d px a frame' % (first, worst, px)))


if __name__ == '__main__':
    name = sys.argv[1]
    seq = SEQUENCES[name]
    if '--analyse' not in sys.argv:
        stage_run.run_arms([(t, v, f, dict(first=FIRST, frames=COUNT)) for t, v, f in seq])
    print('%s: pairs with the same variant and flags' % name)
    compare(seq)
