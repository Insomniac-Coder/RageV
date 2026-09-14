# -*- coding: utf-8 -*-
"""Measured change, phase 1 (docs/RT-MEASURED-CHANGE.md): today, the new build
with the check off, and the new build with it on.

  park    the garage parked, frames 150-189: off must match today texel for
          texel; on must too, because nothing in the scene changes.
  fade    the car's lamps on, the lights button pressed at 8.3 s, frames
          470-899: rt5_fade_analyse.py's curve, today against on.
  cube3   the chrome cube crossing at 3 m/s, frames 100-219 (rt20_measure's).
  dolly   the camera dolly, frames 40-119 (rt20_measure's).
  bridge  the Deck camera parked, frames 136-199.

Arms: base (today's binaries, rendered before the change was built), off (the
new build, no flag), on (the new build, --measured-change=on).

Usage: mc_measure.py run <case> <arm> [<extra flag> ...]
       mc_measure.py same <case> <armA> <armB>     texel-for-texel comparison
"""
import os, re, subprocess, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
import stage_run  # noqa: E402
import rt20_bridge as BR  # noqa: E402

CASES = {
    'park': dict(first=150, frames=40),
    'fade': dict(first=470, frames=430, lamps_off_at=8.3),
    'cube3': dict(first=100, frames=120, cube=(-9.0, 3.0, 6.0)),
    'dolly': dict(first=40, frames=80, speed=0.6, stop=1.5),
    # The map itself (--debug-view=change), a few frames either side of the
    # switch, and parked: black where nothing changed is the whole claim.
    'fadeview': dict(first=492, frames=16, lamps_off_at=8.3),
    'parkview': dict(first=150, frames=8),
    # RT-7: the garage parked with its ceiling tubes 2.4 m long (the bars' length),
    # beside `park`, whose tubes have none.
    'tubes': dict(first=150, frames=40, tube_length=2.4),
    # The tubes at the bars' measured size: 3.07 m long and 0.07 m thick.
    'tubesize': dict(first=150, frames=40, tube_length=3.07, tube_radius=0.07),
    # The first frames of a run, parked: what the check measures while the scene
    # is still arriving.
    'startview': dict(first=1, frames=16),
    # The reflection layer alone around the switch (--debug-view=reflection-picture).
    'fadereflect': dict(first=496, frames=40, lamps_off_at=8.3),
    # The histories' own values (--capture-signals), about 20 frames after the
    # switch and at the settled end: which one still holds the lamps' light.
    'fadecap20': dict(first=518, frames=5, lamps_off_at=8.3),
    'fadecapend': dict(first=880, frames=5, lamps_off_at=8.3),
    # **The moving camera** (2026-09-14): the same lights button pressed while the
    # camera dollies along world X at 0.4 m/s for twelve seconds, and the same
    # dolly with the lamps off long before (the light's end state, frame for
    # frame) and never off (the light itself, frame for frame).
    'fademove': dict(first=480, frames=180, lamps_off_at=8.3, speed=0.4, stop=12.0),
    'fademovetruth': dict(first=480, frames=180, lamps_off_at=0.5, speed=0.4, stop=12.0),
    # Not a whole number of seconds: "AtSeconds: 100" put the camera a sub-texel
    # out of step with the other arms (measured, 2026-09-14); 99.5 does not.
    'fademovelit': dict(first=480, frames=180, lamps_off_at=99.5, speed=0.4, stop=12.0),
    # The histories' own values ten frames after the switch, under the dolly:
    # which filter holds what is left (--capture-signals).
    'fademovecap': dict(first=511, frames=1, lamps_off_at=8.3, speed=0.4, stop=12.0),
    'fademovecaptruth': dict(first=511, frames=1, lamps_off_at=0.5, speed=0.4, stop=12.0),
    'fademovecaplit': dict(first=511, frames=1, lamps_off_at=99.5, speed=0.4, stop=12.0),
    # And the same three with the camera parked, to hold the dolly's numbers against.
    'fadecap10': dict(first=511, frames=1, lamps_off_at=8.3),
    'fadecap10truth': dict(first=511, frames=1, lamps_off_at=0.5),
    'fadecap10lit': dict(first=511, frames=1, lamps_off_at=99.5),
    # Its maps either side of the switch, forced: black before it, lit at it.
    'fademoveview': dict(first=492, frames=16, lamps_off_at=8.3, speed=0.4, stop=12.0),
    # And the chrome cube crossing while the camera dollies: the scene changes
    # every frame the camera moves, so the re-light runs every frame.
    'cubemove': dict(first=100, frames=120, cube=(-9.0, 3.0, 6.0), speed=0.4, stop=12.0),
}
BRIDGE = 'bridge'


def tag(case, arm):
    return 'mc_%s_%s' % (case, arm)


def flags_for(arm, extra):
    # On by default since 2026-09-14: `on*` states on, `def*` states nothing (the
    # default itself), every other arm states off.
    if arm.startswith('def'):
        return list(extra)
    return (['--measured-change=on'] if arm.startswith('on') else ['--measured-change=off']) + list(extra)


def run(case, arm, extra):
    t = tag(case, arm)
    if case != BRIDGE:
        stage_run.run_arms([(t, 'ship', flags_for(arm, extra), dict(CASES[case]))])
        return
    os.makedirs(BR.SHOTS, exist_ok=True)
    for f in BR.frames_of(t):
        os.remove(os.path.join(BR.SHOTS, f))
    cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'),
           '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
           '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
           '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off',
           '--camera=' + BR.CAMERAS['deck'], '--screenshot=' + os.path.join(BR.SHOTS, t + '.png'),
           '--screenshot-frame=%d' % BR.FIRST, '--screenshot-count=%d' % BR.COUNT] + flags_for(arm, extra)
    p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=3600, errors='replace')
    got = len(BR.frames_of(t))
    print('%-18s %d frames %s' % (t, got, ' '.join(flags_for(arm, extra))))
    if got != BR.COUNT:
        print(p.stdout[-3000:], p.stderr[-3000:])
        sys.exit('bridge arm %s incomplete' % t)


def frames(case, arm):
    t = tag(case, arm)
    if case == BRIDGE:
        d, names = BR.SHOTS, BR.frames_of(t)
    else:
        d, names = stage_run.SHOTS, stage_run.frames_of(t)
    return [(int(re.search(r'_(\d+)\.png$', n).group(1)), os.path.join(d, n)) for n in names]


def same(case, a, b):
    A, B = dict(frames(case, a)), dict(frames(case, b))
    common = sorted(set(A) & set(B))
    if not common:
        sys.exit('no common frames for %s %s/%s' % (case, a, b))
    worst, differing = 0, []
    for n in common:
        x = np.asarray(Image.open(A[n]).convert('RGB'), dtype=np.int16)
        y = np.asarray(Image.open(B[n]).convert('RGB'), dtype=np.int16)
        d = np.abs(x - y).max(axis=2)
        if d.any():
            differing.append((n, int((d > 0).sum()), int(d.max())))
        worst = max(worst, int(d.max()))
    print('%s: %s vs %s, %d frames compared, %d differ, worst %d levels'
          % (case, a, b, len(common), len(differing), worst))
    for n, count, peak in differing[:12]:
        print('  frame %d: %d pixels, up to %d levels' % (n, count, peak))


def moving(case, arms=('base', 'on')):
    """Against rt20_measure's SSAA truth of the same frames (no history anywhere):
    where the arms differ, which is nearer the truth; flicker the truth does not
    have, on its edges; and for the cube, the error behind it by frames since it
    left -- the trail."""
    import rt20_measure as M
    o = CASES[case]
    first, count = o['first'], o['frames']
    T = [M.luma(M.rgb(M.tag(case, 'truth'), k)) for k in range(first, first + count)]
    X = {a: [M.luma(M.rgb(tag(case, a), k)) for k in range(first, first + count)] for a in arms}
    a0, a1 = arms
    px, e0, e1, worse, better = 0.0, [], [], 0, 0
    for i in range(count):
        m = np.abs(X[a1][i] - X[a0][i]) > 4
        px += m.mean()
        if m.any():
            x0, x1 = np.abs(X[a0][i] - T[i])[m], np.abs(X[a1][i] - T[i])[m]
            e0.append(x0.mean()); e1.append(x1.mean())
            worse += int(((x1 - x0) > 8).sum()); better += int(((x0 - x1) > 8).sum())
    print('%s: %s vs %s differ by >4 on %.3f%% of pixels; there, error to truth %s %.2f, %s %.2f; '
          '8+ levels worse %d, better %d' % (case, a1, a0, 100 * px / count, a0, np.mean(e0) if e0 else 0,
                                              a1, np.mean(e1) if e1 else 0, worse, better))
    spans = [('all', first, first + count - 1)]
    if case == 'dolly':
        spans = [('moving', first, 89), ('stopped', 92, first + count - 1)]
    for sname, a, b in spans:
        for arm in arms:
            r = []
            for k in range(a, b):
                i = k - first
                m = M.edge_mask(T[i])
                r.append(np.abs((X[arm][i + 1] - X[arm][i]) - (T[i + 1] - T[i]))[m].mean())
            print('  %-7s %-5s change beyond the truth\'s, on its edges: %.2f levels' % (sname, arm, np.mean(r)))
    if case != 'cube3':
        return
    B = {k: M.luma(M.rgb(M.tag('bg', 'truth'), k)) for k in range(first, first + count)}
    grown = lambda m: m | np.roll(m, 1, 0) | np.roll(m, -1, 0) | np.roll(m, 1, 1) | np.roll(m, -1, 1)
    foot = [grown(np.abs(T[i] - B[first + i]) > 24) for i in range(count)]
    ages = [(1, 2), (3, 5), (6, 10), (11, 20), (21, 40)]
    age = np.full(foot[0].shape, 10 ** 6, dtype=np.int64)
    acc = {arm: {b: [0.0, 0, 0] for b in ages} for arm in arms}
    for i in range(count):
        age = np.where(foot[i], 0, age + 1)
        if i < 20:
            continue
        for b in ages:
            m = (age >= b[0]) & (age <= b[1])
            if not m.any():
                continue
            for arm in arms:
                e = np.abs(X[arm][i] - T[i])[m]
                acc[arm][b][0] += e.sum(); acc[arm][b][1] += e.size; acc[arm][b][2] += (e > 16).sum()
    print('  behind the cube, error to truth by frames since it left (mean levels / %% over 16)')
    print('        ' + ''.join('%14s' % ('%d-%d' % b) for b in ages))
    for arm in arms:
        print('  %-5s ' % arm + ''.join('%14s' % ('%.2f/%4.1f%%' % (acc[arm][b][0] / max(acc[arm][b][1], 1),
                                                                   100.0 * acc[arm][b][2] / max(acc[arm][b][1], 1)))
                                       for b in ages))


def mapcount(case, arm):
    """A change view (--debug-view=change and its two siblings) frame by frame:
    the pixels that are not black above the label strip, and the brightest."""
    worst, lit = 0, []
    for n, p in frames(case, arm):
        m = np.asarray(Image.open(p).convert('RGB'), dtype=np.int16).max(axis=2)[:860]
        count = int((m > 0).sum())
        worst = max(worst, int(m.max()))
        if count:
            lit.append((n, count, int((m > 64).sum()), int(m.max())))
    print('%s %s: %d frames, %d with any change shown, brightest %d'
          % (case, arm, len(frames(case, arm)), len(lit), worst))
    for n, count, bright, peak in lit[:12]:
        print('  frame %d: %d pixels, %d over 64, peak %d' % (n, count, bright, peak))


def fademove(arms, truth='truth', lit='lit'):
    """The lights button under a moving camera, frame for frame: at each frame,
    the lamps' light is the lit arm less the truth arm (same camera, lamps never
    off against long off), and what an arm still shows of it is that arm less
    the truth, over the pixels the lamps light by more than three levels."""
    import rt20_measure as M
    o = CASES['fademove']
    first, count = o['first'], o['frames']
    T = lambda k: M.luma(M.rgb(tag('fademovetruth', truth), k))
    L = lambda k: M.luma(M.rgb(tag('fademovelit', lit), k))
    X = {a: (lambda k, a=a: M.luma(M.rgb(tag('fademove', a), k))) for a in arms}
    switch = first + int(round((o['lamps_off_at'] - first * 0.0166) / 0.0166))
    rows = {a: [] for a in arms}
    for k in range(first, first + count):
        t = T(k)
        light = L(k) - t
        region = light > 3.0
        total = light[region].sum()
        for a in arms:
            rows[a].append((X[a](k) - t)[region].sum() / max(total, 1e-6))
    print('fademove: the switch lands near frame %d; share of the lamps\' light still shown' % switch)
    print('  frames after   ' + ''.join('%12s' % a for a in arms))
    for d in (-5, 0, 1, 3, 5, 10, 20, 30, 45, 60, 90, 120, 150):
        i = switch - first + d
        if 0 <= i < count:
            print('  %4d          ' % d + ''.join('%11.1f%%' % (100 * rows[a][i]) for a in arms))
    for level in (0.5, 0.25, 0.10, 0.05, 0.02):
        found = []
        for a in arms:
            r = np.array(rows[a][switch - first:])
            idx = np.where(r <= level)[0]
            found.append(str(int(idx[0])) if len(idx) else 'never')
        print('  until %4.0f%% left: %s' % (100 * level, ' / '.join(found)))


def diffsheet(case, a, b, frames_at, box=None, gain=8):
    """Each arm and the signed difference b - a at x`gain`, per chosen frame."""
    import rt20_measure as M
    A, Bf = dict(frames(case, a)), dict(frames(case, b))
    rows = []
    for n in frames_at:
        x = np.asarray(Image.open(A[n]).convert('RGB'), dtype=np.float32)
        y = np.asarray(Image.open(Bf[n]).convert('RGB'), dtype=np.float32)
        if box:
            y0, y1, x0, x1 = box
            x, y = x[y0:y1, x0:x1], y[y0:y1, x0:x1]
        d = M.signed(M.luma(y) - M.luma(x), gain)
        rows.append([M.label(x, '%s %s, frame %d' % (case, a, n)), M.label(y, '%s %s, frame %d' % (case, b, n)),
                     M.label(d, '%s minus %s, x%d (green brighter)' % (b, a, gain))])
    out = os.path.join(stage_run.OUT, 'mc')
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, 'mc_%s_%s_vs_%s.png' % (case, b, a))
    M.sheet(rows, path)
    print('wrote', path)


if __name__ == '__main__':
    if len(sys.argv) >= 4 and sys.argv[1] == 'run':
        run(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif len(sys.argv) == 5 and sys.argv[1] == 'same':
        same(sys.argv[2], sys.argv[3], sys.argv[4])
    elif len(sys.argv) >= 3 and sys.argv[1] == 'moving':
        moving(sys.argv[2], tuple(sys.argv[3:5]) if len(sys.argv) >= 5 else ('base', 'on'))
    elif len(sys.argv) == 4 and sys.argv[1] == 'mapcount':
        mapcount(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 3 and sys.argv[1] == 'fademove':
        fademove(sys.argv[2:])
    elif len(sys.argv) >= 6 and sys.argv[1] == 'diffsheet':
        diffsheet(sys.argv[2], sys.argv[3], sys.argv[4], [int(v) for v in sys.argv[5].split(',')])
    else:
        sys.exit(__doc__)
