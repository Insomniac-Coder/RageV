# -*- coding: utf-8 -*-
"""RT-20: both fixes against the resolve as shipped, on every case the surface test exists for.

  park      the garage parked at the owner's shot, frames 150-189: the flicker
            itself. References from rt20_edges.py's run of the same build:
            r20_nogeo (the test off) and r20_nojit (the jitter off, the floor).
  cube3     the chrome cube crossing in front of the wall and poles at 3 m/s,
            camera parked -- the moving-object case RT-6 and RT-6.2 were judged on.
  cubeslow  the same cube at a crawl, under a pixel a frame: where a jitter flip
            and a real uncovering look most alike.
  dolly     burst.py's camera dolly, 0.6 m/s for 1.5 s then still: parallax.

Every moving case has a truth arm, `--aa=ssaa --ssaa=2`: no history in the
resolve at all, so nothing in it can trail.

Usage: rt20_measure.py run <case> [arm ...]     render (all arms by default)
       rt20_measure.py analyse <case>            numbers and sheets, no render
"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stage_run  # noqa: E402
import rt20_arms  # noqa: E402,F401  (registers r20A / r20B)

S = stage_run.SHOTS
OUT = os.path.join(stage_run.OUT, 'rt20')
TRUTH = ['--aa=ssaa', '--ssaa=2']
# `ship` was rendered before patch_rt20.py, when the source was HEAD's; `F` is the
# source after it -- the fix as kept. Staged variants apply to the source as it
# stands, so A, B and C only build against HEAD's text.
FIXES = [('ship', 'head', []), ('A', 'r20A', []), ('B', 'r20B', []), ('C', 'r20C', []), ('F', 'ship', [])]

CASES = {
    'park': dict(arms=FIXES, opts=dict(first=150, frames=40)),
    # About 3.9 px a frame at the wall; frames 100-219 carry it from beside the
    # car past the thin pipe (~116) to the first chrome pole (~200).
    'cube3': dict(arms=FIXES + [('truth', 'head', TRUTH)],
                  opts=dict(first=100, frames=120, cube=(-9.0, 3.0, 6.0))),
    # About 0.52 px a frame; its trailing edge crosses the thin pipe near frame 180.
    'cubeslow': dict(arms=FIXES + [('truth', 'head', TRUTH)],
                     opts=dict(first=150, frames=60, cube=(-4.4, 0.4, 30.0))),
    'dolly': dict(arms=FIXES + [('truth', 'head', TRUTH)],
                  opts=dict(first=40, frames=80, speed=0.6, stop=1.5)),
    # The garage with no cube, through the truth arm, over both cube windows:
    # where a cube frame differs from it is where the cube, its reflection and
    # its shadow are -- the mask the trail is measured behind.
    'bg': dict(arms=[('truth', 'head', TRUTH)], opts=dict(first=100, frames=120)),
}


def tag(case, arm):
    return 'r20f_%s_%s' % (case, arm)


def run(case, only):
    c = CASES[case]
    arms = [(tag(case, a), v, f, dict(c['opts'])) for a, v, f in c['arms'] if not only or a in only]
    stage_run.run_arms(arms)


# --- reading ---------------------------------------------------------------------
def rgb(t, n):
    return np.asarray(Image.open(os.path.join(S, 'rt5b_%s_%d.png' % (t, n))).convert('RGB'), dtype=np.float32)


def luma(a):
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def regions(h, w):
    r = lambda y0, y1, x0, x1: (slice(int(h * y0 / 1230), int(h * y1 / 1230)),
                                slice(int(w * x0 / 2000), int(w * x1 / 2000)))
    return {'car': r(560, 760, 560, 900), 'wall': r(250, 550, 1020, 1220), 'poles': r(250, 800, 1100, 1500),
            'tubes': r(0, 250, 300, 1700), 'floor': r(880, 1180, 300, 1700)}


def edge_mask(L, level=12.0):
    g = np.zeros_like(L)
    g[:, 1:] = np.abs(np.diff(L, axis=1))
    gy = np.zeros_like(L)
    gy[1:, :] = np.abs(np.diff(L, axis=0))
    return np.maximum(g, gy) > level


def label(arr, text):
    im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, im.width, 16), fill=(0, 0, 0))
    d.text((4, 3), text, fill=(255, 255, 255))
    return im


def signed(v, gain):
    out = np.zeros(v.shape + (3,), dtype=np.float32)
    out[..., 0] = np.clip(-v * gain, 0, 255)
    out[..., 1] = np.clip(v * gain, 0, 255)
    return out


def grey(v, gain):
    return np.repeat(np.clip(v * gain, 0, 255)[..., None], 3, axis=2)


def sheet(rows, path):
    """rows: list of lists of PIL images, all the same size; written as a grid."""
    w, h = rows[0][0].size
    out = Image.new('RGB', (w * max(len(r) for r in rows), h * len(rows)), (30, 30, 30))
    for j, r in enumerate(rows):
        for i, im in enumerate(r):
            out.paste(im, (i * w, j * h))
    out.save(path)
    print('wrote', path)


# --- park -----------------------------------------------------------------------
def analyse_park():
    first, count = 150, 40
    arms = [('ship', tag('park', 'ship')), ('A', tag('park', 'A')), ('B', tag('park', 'B')), ('C', tag('park', 'C')),
            ('F', tag('park', 'F')),
            ('test off', 'r20_nogeo'), ('jitter off', 'r20_nojit'), ('ship (09-13 run)', 'r20_ship')]
    arms = [(n, t) for n, t in arms if os.path.exists(os.path.join(S, 'rt5b_%s_%d.png' % (t, first + count - 1)))]
    frames = {n: [luma(rgb(t, k)) for k in range(first, first + count)] for n, t in arms}
    h, w = frames[arms[0][0]][0].shape
    reg = regions(h, w)
    print('parked, frames %d-%d: per-frame change on edge / flat / bright-edge pixels, levels' % (first, first + count - 1))
    for n, _ in arms:
        out = []
        for k, sl in reg.items():
            e, f, b = [], [], []
            for i in range(count - 1):
                A, B = frames[n][i][sl], frames[n][i + 1][sl]
                d = np.abs(B - A)
                m = edge_mask(A)
                e.append(d[m].mean()); f.append(d[~m].mean())
                bm = m & (A > 150)
                b.append(d[bm].mean() if bm.any() else 0)
            out.append('%s %5.2f/%4.2f/%5.2f' % (k, np.mean(e), np.mean(f), np.mean(b)))
        print('  %-16s %s' % (n, '  '.join(out)))

    # Converged: each arm's 40-frame mean against the test-off arm's, which is
    # the resolve before RT-6 -- right on a parked camera -- at the edges.
    if 'test off' in frames:
        ref = np.mean(frames['test off'], axis=0)
        em = edge_mask(ref)
        print('40-frame mean against the test-off arm\'s, at its edges: mean |diff| / pixels over 4 levels')
        for n, _ in arms:
            m = np.mean(frames[n], axis=0)
            d = np.abs(m - ref)
            print('  %-16s %.2f / %.2f%%' % (n, d[em].mean(), 100 * (d[em] > 4).mean()))

    # Sheet: tubes, car, poles crops; frame 180, its change to 181 (x4), the mean.
    crops = {'tubes': (0, 250, 700, 1300), 'car': (560, 760, 560, 900), 'poles': (250, 560, 1120, 1480)}
    names = [n for n, _ in arms if not n.startswith('ship (')]
    for cname, (y0, y1, x0, x1) in crops.items():
        ys = slice(int(h * y0 / 1230), int(h * y1 / 1230))
        xs = slice(int(w * x0 / 2000), int(w * x1 / 2000))
        rows = []
        for n, t in [a for a in arms if a[0] in names]:
            f180, f181 = rgb(t, 180)[ys, xs], rgb(t, 181)[ys, xs]
            mean = np.mean([rgb(t, k)[ys, xs] for k in range(first, first + count)], axis=0)
            rows.append([label(np.kron(f180, np.ones((2, 2, 1))), '%s: frame 180' % n),
                         label(np.kron(grey(np.abs(luma(f181) - luma(f180)), 4), np.ones((2, 2, 1))),
                               '%s: change 180->181 x4' % n),
                         label(np.kron(mean, np.ones((2, 2, 1))), '%s: 40-frame mean' % n)])
        sheet(rows, os.path.join(OUT, 'park_%s.png' % cname))


# --- moving cases ----------------------------------------------------------------
def analyse_moving(case):
    c = CASES[case]
    first, count = c['opts']['first'], c['opts']['frames']
    names = [n for n in ('ship', 'A', 'B', 'C', 'F')
             if os.path.exists(os.path.join(S, 'rt5b_%s_%d.png' % (tag(case, n), first + count - 1)))]
    T = [luma(rgb(tag(case, 'truth'), k)) for k in range(first, first + count)]
    X = {n: [luma(rgb(tag(case, n), k)) for k in range(first, first + count)] for n in names}
    h, w = T[0].shape
    print('%s, frames %d-%d, against the SSAA truth (no history in the resolve)' % (case, first, first + count - 1))
    # Where each fix differs from the resolve as shipped, by more than 4 levels:
    # the only pixels the change touches. Error there against the truth, both arms.
    for n in names[1:]:
        diff_px, err_x, err_s, worse, better = 0, [], [], 0, 0
        for i in range(count):
            m = np.abs(X[n][i] - X['ship'][i]) > 4
            diff_px += m.mean()
            if m.any():
                ex, es = np.abs(X[n][i] - T[i])[m], np.abs(X['ship'][i] - T[i])[m]
                err_x.append(ex.mean()); err_s.append(es.mean())
                worse += ((ex - es) > 8).sum(); better += ((es - ex) > 8).sum()
        print('  %s vs ship: %.3f%% of pixels differ by >4; there, error to truth %s %.2f, ship %.2f; '
              'pixels 8+ worse %d, 8+ better %d'
              % (n, 100 * diff_px / count, n, np.mean(err_x) if err_x else 0, np.mean(err_s) if err_s else 0,
                 worse, better))
    # Flicker the truth does not have: frame-to-frame change minus the truth's,
    # on the truth's edges.
    # The dolly moves until frame 90 (1.5 s at 0.0166) and stands still after:
    # the two halves are different questions, so they are counted apart.
    spans = [('all', first, first + count - 1)]
    if case == 'dolly':
        spans = [('moving', first, 89), ('stopped', 92, first + count - 1)]
    for sname, a, b in spans:
        for n in names:
            r = []
            for k in range(a, b):
                i = k - first
                m = edge_mask(T[i])
                r.append(np.abs((X[n][i + 1] - X[n][i]) - (T[i + 1] - T[i]))[m].mean())
            print('  %-7s %-5s change beyond the truth\'s, on its edges: %.2f levels' % (sname, n, np.mean(r)))

    if not case.startswith('cube'):
        return
    if not os.path.exists(os.path.join(S, 'rt5b_%s_%d.png' % (tag('bg', 'truth'), first + count - 1))):
        print('  (no no-cube truth rendered yet: run bg for the trail)')
        return
    # **The trail.** The cube's footprint each frame is where the truth differs
    # from the no-cube truth by more than 24 levels (the cube, its reflection,
    # its shadow). Behind it: pixels the footprint covered in any of the last
    # `back` frames and does not cover now. Everything the resolve still shows
    # of the cube there is trail; the truth shows none. Split into the static
    # silhouettes in that region (the no-cube truth's own edges, option A's
    # risk) and the rest.
    B = {k: luma(rgb(tag('bg', 'truth'), k)) for k in range(first, first + count)}
    grown = lambda m: m | np.roll(m, 1, 0) | np.roll(m, -1, 0) | np.roll(m, 1, 1) | np.roll(m, -1, 1)
    foot = [grown(np.abs(T[i] - B[first + i]) > 24) for i in range(count)]

    # **By how long ago the cube left.** A trail is error that is there just
    # after the cube passes and fades; the flicker fix changes every edge in
    # the frame whether the cube passed it or not. So each pixel's age -- frames
    # since the footprint last covered it -- and the error to the truth binned
    # by it, with the pixels the cube never reached as the baseline. Frames
    # from 20 on, so the cube has had time to leave something behind.
    ages = [(1, 2), (3, 5), (6, 10), (11, 20), (21, 40)]
    age = np.full(foot[0].shape, 10 ** 6, dtype=np.int64)
    acc = {n: {cls: {b: [0.0, 0, 0] for b in ages + ['never']} for cls in ('sil', 'flat')} for n in names}
    for i in range(count):
        age = np.where(foot[i], 0, age + 1)
        if i < 20:
            continue
        edges = grown(edge_mask(B[first + i]))
        for b in ages + ['never']:
            m = (age >= 10 ** 5) if b == 'never' else ((age >= b[0]) & (age <= b[1]))
            for cls, cm in (('sil', m & edges), ('flat', m & ~edges)):
                if not cm.any():
                    continue
                for n in names:
                    e = np.abs(X[n][i] - T[i])[cm]
                    a = acc[n][cls][b]
                    a[0] += e.sum(); a[1] += e.size; a[2] += (e > 16).sum()
    print('  error to truth by frames since the cube left (mean levels, %% over 16), static silhouettes | flat')
    head = ''.join('%14s' % ('%d-%d' % b if b != 'never' else 'never reached') for b in ages + ['never'])
    for cls in ('sil', 'flat'):
        print('    %-5s %s' % (cls, head))
        for n in names:
            cells = ''.join('%14s' % ('%.2f/%4.1f%%' % (acc[n][cls][b][0] / max(acc[n][cls][b][1], 1),
                                                    100.0 * acc[n][cls][b][2] / max(acc[n][cls][b][1], 1)))
                            for b in ages + ['never'])
            print('    %-5s %s' % (n, cells))
    print('    pixels per bin (silhouettes): %s' % ' '.join(str(acc['ship']['sil'][b][1]) for b in ages + ['never']))
    back = 4 if case == 'cube3' else 12
    rows = {n: dict(sil=[], flat=[], sil16=0, flat16=0, nsil=0, nflat=0) for n in names}
    for i in range(back, count):
        behind = np.zeros_like(foot[i])
        for j in range(1, back + 1):
            behind |= foot[i - j]
        behind &= ~foot[i]
        sil = behind & grown(edge_mask(B[first + i]))
        flat = behind & ~sil
        for n in names:
            e = np.abs(X[n][i] - T[i])
            if sil.any():
                rows[n]['sil'].append(e[sil].mean()); rows[n]['sil16'] += (e[sil] > 16).sum(); rows[n]['nsil'] += sil.sum()
            if flat.any():
                rows[n]['flat'].append(e[flat].mean()); rows[n]['flat16'] += (e[flat] > 16).sum(); rows[n]['nflat'] += flat.sum()
    print('  behind the cube (covered in the last %d frames, not now), error to truth: '
          'static silhouettes mean / pixels over 16 levels; flat wall the same' % back)
    for n in names:
        r = rows[n]
        print('  %-5s silhouettes %.2f / %.2f%% (%d px)   flat %.2f / %.2f%% (%d px)'
              % (n, np.mean(r['sil']) if r['sil'] else 0, 100.0 * r['sil16'] / max(r['nsil'], 1), r['nsil'],
                 np.mean(r['flat']) if r['flat'] else 0, 100.0 * r['flat16'] / max(r['nflat'], 1), r['nflat']))


def moving_sheet(case, frames, box, name, arms=('ship', 'A', 'B')):
    """Crops of each arm and the truth at `frames`, box = (y0, y1, x0, x1) in
    the 1600x900 frame, at 2x; under them each arm's error to the truth
    against ship's (green: nearer the truth than ship, red: further), x3."""
    y0, y1, x0, x1 = box
    up = lambda a: np.kron(a, np.ones((2, 2, 1), dtype=np.float32))
    rows = []
    for k in frames:
        t = rgb(tag(case, 'truth'), k)[y0:y1, x0:x1]
        s = rgb(tag(case, 'ship'), k)[y0:y1, x0:x1]
        row = [label(up(rgb(tag(case, n), k)[y0:y1, x0:x1]), '%s frame %d' % (n, k)) for n in arms]
        row.append(label(up(t), 'truth (SSAA, no history) frame %d' % k))
        rows.append(row)
        es = np.abs(luma(s) - luma(t))
        row = [label(up(np.zeros_like(t)), '')]
        for n in arms[1:]:
            ex = np.abs(luma(rgb(tag(case, n), k)[y0:y1, x0:x1]) - luma(t))
            row.append(label(up(signed(es - ex, 3)), '%s: green nearer truth than ship, red further, x3' % n))
        row.append(label(up(signed(luma(rgb(tag(case, 'A'), k)[y0:y1, x0:x1]) - luma(s), 3)),
                         'A - ship x3 (green brighter)'))
        rows.append(row)
    sheet(rows, os.path.join(OUT, '%s_%s.png' % (case, name)))


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    if len(sys.argv) < 3 or sys.argv[1] not in ('run', 'analyse', 'sheet') or sys.argv[2] not in CASES:
        sys.exit(__doc__)
    if sys.argv[1] == 'run':
        run(sys.argv[2], sys.argv[3:])
    elif sys.argv[1] == 'sheet':
        # sheet <case> <name> <y0,y1,x0,x1> <frame> [frame ...]
        moving_sheet(sys.argv[2], [int(f) for f in sys.argv[5:]],
                     tuple(int(v) for v in sys.argv[4].split(',')), sys.argv[3])
    elif sys.argv[2] == 'park':
        analyse_park()
    else:
        analyse_moving(sys.argv[2])
