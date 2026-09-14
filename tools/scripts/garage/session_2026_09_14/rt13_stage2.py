# -*- coding: utf-8 -*-
"""RT-13 stage 2: glass lit by the shared lamp light, against today's glass.

`--glass-layer=on` now draws the glass layer, traces the direct light on it,
settles it on the direct light's contract and has the nearest pane read it; off
is today's glass, which walks every lamp in the transparent draw (stage 1 was
pixel-identical to off). HEAD's showroom through stage_run, so the owner's
working copy is never read.

Usage: rt13_stage2.py <mode> [...]
  mask      one frame of --debug-view=glass-layer at each camera
  look      parked, both cameras, off/on, frames 150..249, mean from 186
  moving    the car driven (1 m/s for 2 s) at the close-up, off/on, frames 40..199
  cost      --benchmark=240 --pass-timings=on, both cameras, off on on off
  valid     --validation=on, parked, close-up, 72 frames, on
"""
import io, os, re, statistics, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
import stage_run  # noqa: E402
sys.path.insert(0, os.path.join(HERE, '..'))
import burst  # noqa: E402

CAMS = {'owner': '-2.3,0.72,-2,11,0,4', 'close': '-4.3,1.0,-2,3.2,25,8'}
OUT = os.path.join(stage_run.ROOT, 'build', 'rt13', 's2')
# The root only: a bare prefix also matches the car's 48 parts, and a part whose
# parent slides as well moves twice -- the car comes apart (HANDOFF, item 6).
CAR = '=porsche_992_gt3_r|1.0|2.0'


def arms_at(cam, arms):
    os.environ['BURST_CAM'] = CAMS[cam]
    stage_run.run_arms(arms)


def mask():
    for cam in CAMS:
        arms_at(cam, [('s2mask_' + cam, 'ship', ['--glass-layer=on', '--debug-view=glass-layer'],
                       dict(frames=2, first=150))])


def look():
    for cam in CAMS:
        arms_at(cam, [('s2look_%s_%s' % (cam, arm), 'ship', ['--glass-layer=' + arm],
                       dict(frames=100, first=150, mean_from=186)) for arm in ('off', 'on')])


NO_LIGHT = ('\t\tLo += texelFetch(u_GlassDirectDiffuse, glassTexel, 0).rgb * albedo / PI\n'
            '\t\t\t+ texelFetch(u_GlassDirectSpecular, glassTexel, 0).rgb;\n',
            '\t\tLo += vec3(0.0);\n')
stage_run.VARIANTS['glassnolight'] = [('include/pbr_fragment.glsl',) + NO_LIGHT]


def moving():
    """Off, on, the lamp light removed from the front pane (how big the term is), and
    the layer's debug view for a per-frame glass mask."""
    arms_at('close', [('s2move_off', 'ship', ['--glass-layer=off'], dict(frames=160, first=40, BURST_SLIDE=CAR)),
                      ('s2move_on', 'ship', ['--glass-layer=on'], dict(frames=160, first=40, BURST_SLIDE=CAR)),
                      ('s2move_nolight', 'glassnolight', ['--glass-layer=on'],
                       dict(frames=160, first=40, BURST_SLIDE=CAR)),
                      ('s2move_mask', 'ship', ['--glass-layer=on', '--debug-view=glass-layer'],
                       dict(frames=160, first=40, BURST_SLIDE=CAR))])


def analyse_moving():
    import numpy as np
    bg = np.array([128.0, 128.0, 0.0])
    rows = []
    for k in range(40, 200):
        mk = _img('rt5b_s2move_mask_%d.png' % k)
        m = np.abs(mk - bg).max(axis=2) > 3.0
        m[860:, :] = False
        off = _luma(_img('rt5b_s2move_off_%d.png' % k))
        on = _luma(_img('rt5b_s2move_on_%d.png' % k))
        nl = _luma(_img('rt5b_s2move_nolight_%d.png' % k))
        if not m.any():
            continue
        d_on, d_nl = np.abs(on - off)[m], np.abs(nl - off)[m]
        rows.append((k, m.sum(), d_on.mean(), (d_on > 4).mean() * 100, d_on.max(),
                     d_nl.mean(), (d_nl > 4).mean() * 100, np.abs(on - off)[~m & (np.arange(off.shape[0])[:, None] < 860)].max()))
    print('frame  glass px | shared vs today: mean  >4%  max | removed vs today: mean  >4% | outside max')
    for r in rows[::10] + rows[-1:]:
        print('%5d  %8d | %6.2f %5.1f %4.0f | %6.2f %5.1f | %4.0f' % r)
    a = np.array([r[2:] for r in rows])
    moving_rows = [r for r in rows if r[0] <= 160]
    b = np.array([r[2:] for r in moving_rows])
    print('all frames:     shared mean %.3f, >4 %.2f%%, max %.0f | removed mean %.3f, >4 %.2f%%'
          % (a[:, 0].mean(), a[:, 1].mean(), a[:, 2].max(), a[:, 3].mean(), a[:, 4].mean()))
    print('while driving:  shared mean %.3f, >4 %.2f%%, max %.0f | removed mean %.3f, >4 %.2f%%'
          % (b[:, 0].mean(), b[:, 1].mean(), b[:, 2].max(), b[:, 3].mean(), b[:, 4].mean()))
    worst = max(rows, key=lambda r: r[2])[0]
    print('worst frame', worst)
    for k in (worst, 100, 199):
        off, on, nl = (_img('rt5b_s2move_%s_%d.png' % (arm, k)) for arm in ('off', 'on', 'nolight'))
        amp = lambda a, b: np.repeat(np.clip(np.abs(_luma(a) - _luma(b)) * 16.0, 0, 255)[..., None], 3, axis=2)
        _sheet(os.path.join(OUT, 'moving_%d.png' % k), [off[:860], on[:860], amp(on, off)[:860], amp(nl, off)[:860]],
               ['today, frame %d' % k, 'shared', 'shared |diff| x16', 'lamp light removed |diff| x16'])


PASSES = re.compile(r'scene/(Transparent|GlassLayer|GlassDirectTrace|GlassDirectAccumulate|ResolveTransparent)'
                    r'\s+([\d.]+)\s+([\d.]+)')


def cost():
    os.makedirs(OUT, exist_ok=True)
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=stage_run.ROOT, capture_output=True, check=True).stdout
    try:
        io.open(os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE), 'wb').write(head)
        burst.SCENE = stage_run.HEAD_SCENE
        burst.make_scene(0.0, 0.1)
        for cam in CAMS:
            frames = {'off': [], 'on': []}
            passes = {}
            for i, arm in enumerate(['off', 'on', 'on', 'off']):
                if not stage_run.restore():
                    sys.exit('staged copies did not restore')
                cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'),
                       '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                       '--scene=scenes/showroom_burst.rage', '--rhi=vulkan', '--render-defaults=off',
                       '--vsync=off', '--width=1600', '--height=900', '--benchmark=240',
                       '--frame-time=0.0166', '--import-cache=off', '--pass-timings=on',
                       '--camera=' + CAMS[cam], '--glass-layer=' + arm]
                p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=1800,
                                   errors='replace')
                io.open(os.path.join(OUT, 'cost_%s_%d_%s.log' % (cam, i, arm)), 'w',
                        encoding='utf-8').write(p.stdout + p.stderr)
                frame = re.search(r'frame\s+mean ([\d.]+) ms', p.stdout)
                if not frame:
                    print(p.stdout[-2000:])
                    sys.exit('%s run %d (%s): no benchmark lines' % (cam, i, arm))
                frames[arm].append(float(frame.group(1)))
                found = PASSES.findall(p.stdout)
                for name, _, gpu in found:
                    passes.setdefault((arm, name), []).append(float(gpu))
                print('%s run %d %-3s frame %.3f ms  %s' % (cam, i, arm, frames[arm][-1],
                      ' '.join('%s %s' % (n, g) for n, _, g in found)))
                sys.stdout.flush()
            for arm in ('off', 'on'):
                print('%s %-3s frame %.3f ms (%s)' % (cam, arm, statistics.mean(frames[arm]),
                      ', '.join('%.3f' % f for f in frames[arm])))
                for (a, name), rows in sorted(passes.items()):
                    if a == arm:
                        print('    %-24s GPU %.3f ms' % (name, statistics.mean(rows)))
    finally:
        print('staged copies restored and identical to source:', stage_run.restore())
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta',
                  'showroom_burst.rage', 'showroom_burst.rage.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass


def valid():
    os.makedirs(OUT, exist_ok=True)
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=stage_run.ROOT, capture_output=True, check=True).stdout
    try:
        io.open(os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE), 'wb').write(head)
        burst.SCENE = stage_run.HEAD_SCENE
        burst.make_scene(0.0, 0.1)
        cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'),
               '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
               '--scene=scenes/showroom_burst.rage', '--rhi=vulkan', '--render-defaults=off',
               '--vsync=off', '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off',
               '--camera=' + CAMS['close'], '--glass-layer=on', '--validation=on',
               '--screenshot=' + os.path.join(OUT, 'valid.png'), '--screenshot-frame=1',
               '--screenshot-count=72']
        p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=3600,
                           errors='replace')
        text = p.stdout + '\n' + p.stderr
        io.open(os.path.join(OUT, 'valid.log'), 'w', encoding='utf-8').write(text)
        kinds = {}
        for line in text.splitlines():
            m = re.search(r'(VUID-[A-Za-z0-9_\-]+|UNASSIGNED-[A-Za-z0-9_\-]+)', line)
            if m:
                kinds[m.group(1)] = kinds.get(m.group(1), 0) + 1
        got = [f for f in os.listdir(OUT) if re.match(r'valid_\d+\.png$', f)]
        print('exit %d, %d frames, %d validation messages' % (p.returncode, len(got), sum(kinds.values())))
        for k, n in sorted(kinds.items(), key=lambda kv: -kv[1]):
            print('   %5d  %s' % (n, k))
    finally:
        print('staged copies restored and identical to source:', stage_run.restore())
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta',
                  'showroom_burst.rage', 'showroom_burst.rage.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass


def _img(name):
    import numpy as np
    from PIL import Image
    return np.asarray(Image.open(os.path.join(stage_run.SHOTS, name)).convert('RGB'), dtype=np.float64)


def _luma(a):
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def glass_mask(cam):
    """Where the layer holds a pane: the debug view differs from its flat clear. The
    credit bar along the bottom is left out."""
    import numpy as np
    a = _img('rt5b_s2mask_%s_151.png' % cam)
    bg = np.array([128.0, 128.0, 0.0])
    m = np.abs(a - bg).max(axis=2) > 3.0
    m[860:, :] = False
    return m


def _stats(d, where):
    import numpy as np
    v = d[where]
    if v.size == 0:
        return 'no pixels'
    return ('%7d px  mean %.2f  >1: %5.1f%%  >4: %5.1f%%  >16: %5.1f%%  max %.0f'
            % (v.size, v.mean(), 100.0 * (v > 1).mean(), 100.0 * (v > 4).mean(),
               100.0 * (v > 16).mean(), v.max()))


def _sheet(path, panels, labels):
    """Panels side by side, labelled; each an HxWx3 array in 0..255."""
    import numpy as np
    from PIL import Image, ImageDraw
    h = max(p.shape[0] for p in panels)
    w = sum(p.shape[1] for p in panels) + 8 * (len(panels) - 1)
    out = Image.new('RGB', (w, h + 22), (0, 0, 0))
    draw = ImageDraw.Draw(out)
    x = 0
    for p, label in zip(panels, labels):
        out.paste(Image.fromarray(np.clip(p, 0, 255).astype(np.uint8)), (x, 22))
        draw.text((x + 4, 4), label, fill=(255, 255, 255))
        x += p.shape[1] + 8
    out.save(path)


def analyse_look():
    import numpy as np
    os.makedirs(OUT, exist_ok=True)
    for cam in CAMS:
        m = glass_mask(cam)
        off = np.load(os.path.join(stage_run.OUT, 's2look_%s_off.npy' % cam))
        on = np.load(os.path.join(stage_run.OUT, 's2look_%s_on.npy' % cam))
        d = np.abs(_luma(on) - _luma(off))
        body = ~m
        body[860:, :] = False
        print('%s  mean of frames 186..249, |on - off| in luminance levels' % cam)
        print('   glass      ' + _stats(d, m))
        print('   everything else ' + _stats(d, body))
        signed = (_luma(on) - _luma(off))[m]
        print('   glass signed mean %+.2f (on brighter where positive)' % signed.mean())
        # Frame-to-frame change on the glass, parked: the flicker.
        for arm in ('off', 'on'):
            prev, rows = None, []
            for k in range(186, 250):
                cur = _luma(_img('rt5b_s2look_%s_%s_%d.png' % (cam, arm, k)))
                if prev is not None:
                    rows.append(np.abs(cur - prev)[m].mean())
                prev = cur
            print('   %-3s glass frame-to-frame %.3f levels (max frame %.3f)' % (arm, np.mean(rows), np.max(rows)))
        # The picture: both means, the difference x8, and one frame of each.
        ys, xs = np.where(m)
        y0, y1 = max(ys.min() - 40, 0), min(ys.max() + 40, 860)
        x0, x1 = max(xs.min() - 40, 0), min(xs.max() + 40, off.shape[1])
        diff = np.repeat(np.clip(np.abs(on - off).max(axis=2) * 8.0, 0, 255)[..., None], 3, axis=2)
        f_off = _img('rt5b_s2look_%s_off_249.png' % cam)
        f_on = _img('rt5b_s2look_%s_on_249.png' % cam)
        crop = lambda a: a[y0:y1, x0:x1]
        _sheet(os.path.join(OUT, 'look_%s_means.png' % cam),
               [crop(off), crop(on), crop(diff)],
               ['today (loop), mean 186-249', 'shared lamp light, mean 186-249', '|difference| x8'])
        _sheet(os.path.join(OUT, 'look_%s_frame249.png' % cam),
               [crop(f_off), crop(f_on), crop(np.repeat(np.clip(np.abs(f_on - f_off).max(axis=2) * 8.0, 0, 255)[..., None], 3, axis=2))],
               ['today, frame 249', 'shared, frame 249', '|difference| x8'])


# **Which signal is the moving body's grain?** (owner, 2026-09-14 night: the car's
# body reflections go noisy and trail while it drives; its glass stays steady.)
# Today's glass throughout; each arm swaps one signal for its reference form.
ISOLATE = [('iso_base', []), ('iso_nodirect', ['--direct-signal=off']),
           ('iso_norefl', ['--rt-reflections=off']), ('iso_nogi', ['--gi-signal=off']),
           ('iso_noao', ['--ao-signal=off'])]


def isolate():
    arms_at('close', [(tag, 'ship', ['--glass-layer=off'] + flags, dict(frames=40, first=50, BURST_SLIDE=CAR))
                      for tag, flags in ISOLATE])


def analyse_isolate():
    """Grain: how far each pixel stands from its 3x3 median, over the car's body while
    it drives (frames 55..89) -- the same box and frames in every arm, since the car
    is in the same place in all of them."""
    import numpy as np
    from PIL import Image, ImageFilter
    box = (380, 250, 1250, 760)   # y0, x0, y1, x1 around the body at the close-up
    rows = {}
    for tag, _ in ISOLATE:
        vals = []
        for k in range(55, 90):
            im = Image.open(os.path.join(stage_run.SHOTS, 'rt5b_%s_%d.png' % (tag, k))).convert('L')
            a = np.asarray(im, dtype=np.float64)
            med = np.asarray(im.filter(ImageFilter.MedianFilter(3)), dtype=np.float64)
            g = np.abs(a - med)[box[0]:box[2], box[1]:box[3]]
            vals.append((g.mean(), (g > 16).mean() * 100))
        v = np.array(vals)
        rows[tag] = v
        print('%-14s grain %.2f levels, %.2f%% of pixels > 16 from their neighbourhood' % (tag, v[:, 0].mean(), v[:, 1].mean()))
    crops = [np.asarray(Image.open(os.path.join(stage_run.SHOTS, 'rt5b_%s_64.png' % tag)).convert('RGB'))[300:760, 250:1450]
             for tag, _ in ISOLATE]
    _sheet(os.path.join(OUT, 'isolate_64.png'), crops,
           ['today (all signals)', 'lamp light: no memory', 'reflections: probe only', 'bounce: old path', 'occlusion: old path'])


# The project runs TAA; the other two modes that change the scene target -- MSAA's
# samples and SSAA's size -- parked at the close-up, off/on, with the lamp light
# removed as the size of the term.
def aamodes():
    arms = []
    for aa in ('msaa', 'ssaa'):
        arms += [('s2aa_%s_off' % aa, 'ship', ['--aa=' + aa, '--glass-layer=off'], dict(frames=40, first=150, mean_from=170)),
                 ('s2aa_%s_on' % aa, 'ship', ['--aa=' + aa, '--glass-layer=on'], dict(frames=40, first=150, mean_from=170)),
                 ('s2aa_%s_nolight' % aa, 'glassnolight', ['--aa=' + aa, '--glass-layer=on'],
                  dict(frames=40, first=150, mean_from=170))]
    arms_at('close', arms)


def analyse_aamodes():
    import numpy as np
    m = glass_mask('close')
    for aa in ('msaa', 'ssaa'):
        off, on, nl = (np.load(os.path.join(stage_run.OUT, 's2aa_%s_%s.npy' % (aa, arm))) for arm in ('off', 'on', 'nolight'))
        body = ~m
        body[860:, :] = False
        print(aa)
        print('   shared vs today, glass      ' + _stats(np.abs(_luma(on) - _luma(off)), m))
        print('   removed vs today, glass     ' + _stats(np.abs(_luma(nl) - _luma(off)), m))
        print('   shared vs today, elsewhere  ' + _stats(np.abs(_luma(on) - _luma(off)), body))
        ys, xs = np.where(m)
        y0, y1, x0, x1 = max(ys.min() - 20, 0), min(ys.max() + 20, 860), max(xs.min() - 20, 0), min(xs.max() + 20, off.shape[1])
        amp = lambda a, b: np.repeat(np.clip(np.abs(_luma(a) - _luma(b)) * 16.0, 0, 255)[..., None], 3, axis=2)[y0:y1, x0:x1]
        _sheet(os.path.join(OUT, 'aa_%s.png' % aa), [off[y0:y1, x0:x1], amp(on, off), amp(nl, off)],
               ['today, %s, mean 170-189' % aa, 'shared lamp light |diff| x16', 'lamp light removed |diff| x16'])


def valid_aa():
    """Validation under the two modes, 30 frames each."""
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=stage_run.ROOT, capture_output=True, check=True).stdout
    try:
        io.open(os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE), 'wb').write(head)
        burst.SCENE = stage_run.HEAD_SCENE
        burst.make_scene(0.0, 0.1)
        for aa in ('msaa', 'ssaa'):
            cmd = [os.path.join(stage_run.RT, 'RageVRuntime.exe'),
                   '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
                   '--scene=scenes/showroom_burst.rage', '--rhi=vulkan', '--render-defaults=off',
                   '--vsync=off', '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off',
                   '--camera=' + CAMS['close'], '--glass-layer=on', '--validation=on', '--aa=' + aa,
                   '--screenshot=' + os.path.join(OUT, 'valid_%s.png' % aa), '--screenshot-frame=1',
                   '--screenshot-count=30']
            p = subprocess.run(cmd, cwd=stage_run.RT, capture_output=True, text=True, timeout=3600, errors='replace')
            text = p.stdout + chr(10) + p.stderr
            io.open(os.path.join(OUT, 'valid_%s.log' % aa), 'w', encoding='utf-8').write(text)
            kinds = {}
            for line in text.splitlines():
                mm = re.search(r'(VUID-[A-Za-z0-9_\-]+|UNASSIGNED-[A-Za-z0-9_\-]+)', line)
                if mm:
                    kinds[mm.group(1)] = kinds.get(mm.group(1), 0) + 1
            got = [f for f in os.listdir(OUT) if re.match(r'valid_%s_\d+\.png$' % aa, f)]
            print('%s: exit %d, %d frames, %d validation messages %s' % (aa, p.returncode, len(got), sum(kinds.values()), kinds))
    finally:
        print('staged copies restored and identical to source:', stage_run.restore())
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta',
                  'showroom_burst.rage', 'showroom_burst.rage.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass


# **A light switched off, on the glass.** The glass history has no measured change,
# so its lamp light may fade over frames where today's loop goes dark at once. The
# garage's tubes and their bars off at frame 80 (burst.py's own example), parked.
SWITCH = 'Tube ,Bottom light bars|1.328'


def switch():
    arms_at('close', [('s2sw_%s' % arm, 'ship', ['--glass-layer=' + arm], dict(frames=100, first=60, BURST_SWITCH=SWITCH))
                      for arm in ('off', 'on')])


def analyse_switch():
    import numpy as np
    m = glass_mask('close')
    series = {}
    for arm in ('off', 'on'):
        series[arm] = np.array([_luma(_img('rt5b_s2sw_%s_%d.png' % (arm, k)))[m] for k in range(60, 160)])
    # The drop on each glass pixel, frame 78 (lit) to 159 (settled dark), and how
    # much of it is left k frames after the switch -- over the pixels the switch
    # moved by more than four levels in today's arm.
    before, after = series['off'][18], series['off'][99]
    moved = (before - after) > 4.0
    print('glass pixels the switch darkens by more than 4 levels: %d of %d' % (moved.sum(), m.sum()))
    print('frames after the switch | light left: today   shared')
    for k in (80, 81, 82, 84, 88, 96, 110, 130, 159):
        left = {}
        for arm in ('off', 'on'):
            s = series[arm]
            drop = s[18][moved] - s[99][moved]
            left[arm] = np.mean((s[k - 60][moved] - s[99][moved]) / np.maximum(drop, 1e-3)) * 100
        print('%5d (%3d)              | %6.1f%%  %6.1f%%' % (k, k - 80, left['off'], left['on']))
    d = np.abs(series['on'] - series['off'])
    print('largest |on - off| on the glass per frame, frames 80-100:', ' '.join('%.0f' % d[k - 60].max() for k in range(80, 101)))
    for k in (82, 90):
        off, on = _img('rt5b_s2sw_off_%d.png' % k), _img('rt5b_s2sw_on_%d.png' % k)
        amp = np.repeat(np.clip(np.abs(_luma(on) - _luma(off)) * 8.0, 0, 255)[..., None], 3, axis=2)
        _sheet(os.path.join(OUT, 'switch_%d.png' % k), [off[330:760, 250:1450], on[330:760, 250:1450], amp[330:760, 250:1450]],
               ['today, frame %d (tubes off at 80)' % k, 'shared lamp light', '|difference| x8'])


if __name__ == '__main__':
    {'mask': mask, 'look': look, 'moving': moving, 'cost': cost, 'valid': valid,
     'analyse_look': analyse_look, 'analyse_moving': analyse_moving, 'isolate': isolate, 'analyse_isolate': analyse_isolate, 'aamodes': aamodes, 'analyse_aamodes': analyse_aamodes, 'valid_aa': valid_aa, 'switch': switch, 'analyse_switch': analyse_switch}[sys.argv[1]]()
