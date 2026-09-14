# -*- coding: utf-8 -*-
"""RT-13 stage 3: glass reflected by the shared reflection passes, against today's glass.

`--glass-layer=on` now also traces, resolves and accumulates the reflections on
the glass layer, and the nearest pane reads that settled picture instead of
casting its own rays (stage 2's lamp light as before). Off is today's glass.
The harness is stage 2's (rt13_stage2.py): HEAD's showroom through stage_run.

Usage: rt13_stage3.py <mode>
  probe       paint the panes that read the settled reflection (a staged shader), one frame
  look        parked, both cameras: today, stage 2 only, stages 2+3; frames 150..249
  moving      the car driven, close-up: today, stage 2 only, stages 2+3
  switch      the tubes off at frame 80, close-up: today, stage 2 only, stages 2+3
  aamodes     MSAA and SSAA, parked, close-up: today, stages 2+3
  cost        --benchmark=240 --pass-timings=on, both cameras, off on on off
  valid       --validation=on under TAA, MSAA and SSAA, close-up
  analyse_*   the numbers and sheets for look, moving, switch, aamodes
"""
import io, os, re, statistics, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rt13_stage2 as s2  # noqa: E402
stage_run, burst = s2.stage_run, s2.burst

OUT = os.path.join(stage_run.ROOT, 'build', 'rt13', 's3')
FRAG = 'include/pbr_fragment.glsl'
SETTLED_MIX = '\t\t\tprefiltered = mix(prefiltered, settledReflection.rgb, settledShare);\n'
# Stage 2 alone: the pane never takes the settled reflection and casts its own rays.
stage_run.VARIANTS['stage2only'] = [(FRAG,
    '\t\tconst bool glassReflected = directSignal && textureSize(u_GlassReflection, 0).x > 1;\n',
    '\t\tconst bool glassReflected = false;\n')]
# Green where the pane reads the settled picture, its brightness the share.
stage_run.VARIANTS['probe'] = [(FRAG, SETTLED_MIX,
    '\t\t\tprefiltered = vec3(0.0, 40.0 * settledShare, 0.0);\n')]

ARMS = (('today', 'ship', ['--glass-layer=off']),
        ('stage2', 'stage2only', ['--glass-layer=on']),
        ('stage3', 'ship', ['--glass-layer=on']))


def probe():
    s2.arms_at('close', [('s3probe', 'probe', ['--glass-layer=on'], dict(frames=2, first=150))])


def look():
    for cam in s2.CAMS:
        s2.arms_at(cam, [('s3look_%s_%s' % (cam, tag), variant, flags, dict(frames=100, first=150, mean_from=186))
                         for tag, variant, flags in ARMS])


def moving():
    s2.arms_at('close', [('s3move_%s' % tag, variant, flags, dict(frames=160, first=40, BURST_SLIDE=s2.CAR))
                         for tag, variant, flags in ARMS])


def switch():
    s2.arms_at('close', [('s3sw_%s' % tag, variant, flags, dict(frames=100, first=60, BURST_SWITCH=s2.SWITCH))
                         for tag, variant, flags in ARMS])


def aamodes():
    arms = []
    for aa in ('msaa', 'ssaa'):
        arms += [('s3aa_%s_%s' % (aa, tag), variant, ['--aa=' + aa] + flags, dict(frames=40, first=150, mean_from=170))
                 for tag, variant, flags in ARMS if tag != 'stage2']
    s2.arms_at('close', arms)


def _amp(a, b, gain=8.0):
    import numpy as np
    return np.repeat(np.clip(np.abs(s2._luma(a) - s2._luma(b)) * gain, 0, 255)[..., None], 3, axis=2)


def analyse_look():
    import numpy as np
    os.makedirs(OUT, exist_ok=True)
    for cam in s2.CAMS:
        m = s2.glass_mask(cam)
        body = ~m
        body[860:, :] = False
        mean = {tag: np.load(os.path.join(stage_run.OUT, 's3look_%s_%s.npy' % (cam, tag))) for tag, _, _ in ARMS}
        print('%s, mean of frames 186..249, |difference| in luminance levels' % cam)
        for a, b in (('stage2', 'today'), ('stage3', 'today'), ('stage3', 'stage2')):
            d = np.abs(s2._luma(mean[a]) - s2._luma(mean[b]))
            print('   %s vs %s  glass %s' % (a, b, s2._stats(d, m)))
            print('   %s vs %s  other %s' % (a, b, s2._stats(d, body)))
        print('   glass mean level: today %.2f, stage 2 %.2f, stage 3 %.2f'
              % tuple(s2._luma(mean[t])[m].mean() for t in ('today', 'stage2', 'stage3')))
        for tag, _, _ in ARMS:
            prev, rows = None, []
            for k in range(186, 250):
                cur = s2._luma(s2._img('rt5b_s3look_%s_%s_%d.png' % (cam, tag, k)))
                if prev is not None:
                    rows.append(np.abs(cur - prev)[m].mean())
                prev = cur
            print('   %-6s glass frame-to-frame %.3f levels' % (tag, np.mean(rows)))
        ys, xs = np.where(m)
        y0, y1 = max(ys.min() - 40, 0), min(ys.max() + 40, 860)
        x0, x1 = max(xs.min() - 40, 0), min(xs.max() + 40, mean['today'].shape[1])
        c = lambda a: a[y0:y1, x0:x1]
        s2._sheet(os.path.join(OUT, 'look_%s_means.png' % cam),
                  [c(mean['today']), c(mean['stage3']), c(_amp(mean['stage3'], mean['today']))],
                  ['today, mean 186-249', 'stages 2+3, mean 186-249', '|difference| x8'])
        f = {tag: s2._img('rt5b_s3look_%s_%s_249.png' % (cam, tag)) for tag, _, _ in ARMS}
        s2._sheet(os.path.join(OUT, 'look_%s_frame249.png' % cam),
                  [c(f['today']), c(f['stage3']), c(_amp(f['stage3'], f['today']))],
                  ['today, frame 249', 'stages 2+3, frame 249', '|difference| x8'])


def analyse_moving():
    import numpy as np
    from PIL import Image, ImageFilter
    bg = np.array([128.0, 128.0, 0.0])
    rows = []
    for k in range(40, 200):
        mk = s2._img('rt5b_s2move_mask_%d.png' % k)   # stage 2's per-frame glass mask, same motion
        m = np.abs(mk - bg).max(axis=2) > 3.0
        m[860:, :] = False
        if not m.any():
            continue
        L = {tag: s2._luma(s2._img('rt5b_s3move_%s_%d.png' % (tag, k))) for tag, _, _ in ARMS}
        # Grain on the glass: each pixel against its 3x3 median, per arm.
        grain = {}
        for tag, _, _ in ARMS:
            im = Image.open(os.path.join(stage_run.SHOTS, 'rt5b_s3move_%s_%d.png' % (tag, k))).convert('L')
            a = np.asarray(im, dtype=float)
            med = np.asarray(im.filter(ImageFilter.MedianFilter(3)), dtype=float)
            grain[tag] = (np.abs(a - med)[m] > 16).mean() * 100
        d3 = np.abs(L['stage3'] - L['today'])[m]
        rows.append((k, m.sum(), d3.mean(), (d3 > 4).mean() * 100, grain['today'], grain['stage2'], grain['stage3']))
    print('frame  glass px | stage3 vs today: mean  >4%  | glass grain >16: today stage2 stage3')
    for r in rows[::10] + rows[-1:]:
        print('%5d  %8d | %6.2f %5.1f | %6.2f %6.2f %6.2f' % r)
    a = np.array([r[2:] for r in rows if r[0] <= 160])
    b = np.array([r[2:] for r in rows if r[0] > 160])
    print('driving (40-160): stage3 vs today mean %.2f, >4 %.1f%% | grain today %.2f, stage2 %.2f, stage3 %.2f' % tuple(a.mean(axis=0)))
    print('parked  (161-199): stage3 vs today mean %.2f, >4 %.1f%% | grain today %.2f, stage2 %.2f, stage3 %.2f' % tuple(b.mean(axis=0)))
    for k in (64, 100, 180):
        f = {tag: s2._img('rt5b_s3move_%s_%d.png' % (tag, k)) for tag, _, _ in ARMS}
        s2._sheet(os.path.join(OUT, 'moving_%d.png' % k),
                  [f['today'][330:760, 250:1450], f['stage3'][330:760, 250:1450], _amp(f['stage3'], f['today'])[330:760, 250:1450]],
                  ['today, frame %d (driving until 160)' % k, 'stages 2+3', '|difference| x8'])


def analyse_switch():
    import numpy as np
    m = s2.glass_mask('close')
    series = {tag: np.array([s2._luma(s2._img('rt5b_s3sw_%s_%d.png' % (tag, k)))[m] for k in range(60, 160)])
              for tag, _, _ in ARMS}
    before, after = series['today'][18], series['today'][99]
    moved = (before - after) > 4.0
    print('glass pixels the switch darkens by more than 4 levels: %d of %d' % (moved.sum(), m.sum()))
    print('frames after the switch | light left: today  stage2  stage3')
    for k in (80, 81, 82, 84, 88, 96, 110, 130, 159):
        left = []
        for tag in ('today', 'stage2', 'stage3'):
            s = series[tag]
            drop = s[18][moved] - s[99][moved]
            left.append(np.mean((s[k - 60][moved] - s[99][moved]) / np.maximum(drop, 1e-3)) * 100)
        print('%5d (%3d)              | %6.1f%% %6.1f%% %6.1f%%' % ((k, k - 80) + tuple(left)))
    for k in (82, 90, 110):
        f = {tag: s2._img('rt5b_s3sw_%s_%d.png' % (tag, k)) for tag, _, _ in ARMS}
        s2._sheet(os.path.join(OUT, 'switch_%d.png' % k),
                  [f['today'][330:760, 250:1450], f['stage3'][330:760, 250:1450], _amp(f['stage3'], f['today'])[330:760, 250:1450]],
                  ['today, frame %d (tubes off at 80)' % k, 'stages 2+3', '|difference| x8'])


def analyse_aamodes():
    import numpy as np
    from PIL import Image, ImageFilter
    m = s2.glass_mask('close')
    body = ~m
    body[860:, :] = False
    for aa in ('msaa', 'ssaa'):
        mean = {tag: np.load(os.path.join(stage_run.OUT, 's3aa_%s_%s.npy' % (aa, tag))) for tag in ('today', 'stage3')}
        d = np.abs(s2._luma(mean['stage3']) - s2._luma(mean['today']))
        print('%s stage3 vs today  glass %s' % (aa, s2._stats(d, m)))
        print('%s stage3 vs today  other %s' % (aa, s2._stats(d, body)))
        for tag in ('today', 'stage3'):
            rows = []
            for k in range(170, 190):
                im = Image.open(os.path.join(stage_run.SHOTS, 'rt5b_s3aa_%s_%s_%d.png' % (aa, tag, k))).convert('L')
                a = np.asarray(im, dtype=float)
                med = np.asarray(im.filter(ImageFilter.MedianFilter(3)), dtype=float)
                rows.append((np.abs(a - med)[m] > 24).mean() * 100)
            print('   %-6s speckles on glass %.2f%%' % (tag, np.mean(rows)))
        f = {tag: s2._img('rt5b_s3aa_%s_%s_189.png' % (aa, tag)) for tag in ('today', 'stage3')}
        s2._sheet(os.path.join(OUT, 'aa_%s_189.png' % aa),
                  [f['today'][330:760, 250:1450], f['stage3'][330:760, 250:1450], _amp(f['stage3'], f['today'])[330:760, 250:1450]],
                  ['today, %s, frame 189' % aa, 'stages 2+3', '|difference| x8'])


PASSES = re.compile(r'scene/(Transparent|GlassLayer|GlassDirectTrace|GlassDirectAccumulate|GlassReflectionTrace|'
                    r'GlassReflectionResolve|GlassReflectionAccumulate)\s+([\d.]+)\s+([\d.]+)')


def _scene():
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=stage_run.ROOT, capture_output=True, check=True).stdout
    io.open(os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE), 'wb').write(head)
    burst.SCENE = stage_run.HEAD_SCENE
    burst.make_scene(0.0, 0.1)


def _clean():
    print('staged copies restored and identical to source:', stage_run.restore())
    for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta', 'showroom_burst.rage', 'showroom_burst.rage.meta'):
        try:
            os.remove(os.path.join(stage_run.SCENES, f))
        except OSError:
            pass


def _runtime(extra):
    return [os.path.join(stage_run.RT, 'RageVRuntime.exe'), '--project=' + os.path.join(stage_run.ROOT, 'SampleProject'),
            '--scene=scenes/showroom_burst.rage', '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
            '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off'] + extra


def cost():
    os.makedirs(OUT, exist_ok=True)
    try:
        _scene()
        for cam in s2.CAMS:
            frames, passes = {'off': [], 'on': []}, {}
            for i, arm in enumerate(['off', 'on', 'on', 'off']):
                if not stage_run.restore():
                    sys.exit('staged copies did not restore')
                p = subprocess.run(_runtime(['--benchmark=240', '--pass-timings=on', '--camera=' + s2.CAMS[cam],
                                             '--glass-layer=' + arm]),
                                   cwd=stage_run.RT, capture_output=True, text=True, timeout=1800, errors='replace')
                io.open(os.path.join(OUT, 'cost_%s_%d_%s.log' % (cam, i, arm)), 'w', encoding='utf-8').write(p.stdout + p.stderr)
                frame = re.search(r'frame\s+mean ([\d.]+) ms', p.stdout)
                if not frame:
                    print(p.stdout[-2000:])
                    sys.exit('%s run %d (%s): no benchmark lines' % (cam, i, arm))
                frames[arm].append(float(frame.group(1)))
                for name, _, gpu in PASSES.findall(p.stdout):
                    passes.setdefault((arm, name), []).append(float(gpu))
                print('%s run %d %-3s frame %.3f ms' % (cam, i, arm, frames[arm][-1]))
                sys.stdout.flush()
            for arm in ('off', 'on'):
                print('%s %-3s frame %.3f ms (%s)' % (cam, arm, statistics.mean(frames[arm]),
                                                     ', '.join('%.3f' % f for f in frames[arm])))
                total = 0.0
                for (a, name), rows in sorted(passes.items()):
                    if a == arm:
                        total += statistics.mean(rows)
                        print('    %-26s GPU %.3f ms' % (name, statistics.mean(rows)))
                print('    %-26s GPU %.3f ms' % ('(glass passes together)', total))
    finally:
        _clean()


def valid():
    os.makedirs(OUT, exist_ok=True)
    try:
        _scene()
        for aa in ('taa', 'msaa', 'ssaa'):
            shot = os.path.join(OUT, 'valid_%s.png' % aa)
            p = subprocess.run(_runtime(['--camera=' + s2.CAMS['close'], '--glass-layer=on', '--validation=on',
                                         '--aa=' + aa, '--screenshot=' + shot, '--screenshot-frame=1',
                                         '--screenshot-count=40']),
                               cwd=stage_run.RT, capture_output=True, text=True, timeout=3600, errors='replace')
            text = p.stdout + chr(10) + p.stderr
            io.open(os.path.join(OUT, 'valid_%s.log' % aa), 'w', encoding='utf-8').write(text)
            kinds = {}
            for line in text.splitlines():
                mm = re.search(r'(VUID-[A-Za-z0-9_-]+|UNASSIGNED-[A-Za-z0-9_-]+)', line)
                if mm:
                    kinds[mm.group(1)] = kinds.get(mm.group(1), 0) + 1
            got = [f for f in os.listdir(OUT) if f.startswith('valid_%s_' % aa) and f.endswith('.png')]
            print('%s: exit %d, %d frames, %d validation messages %s' % (aa, p.returncode, len(got), sum(kinds.values()), kinds))
    finally:
        _clean()


if __name__ == '__main__':
    {'probe': probe, 'look': look, 'moving': moving, 'switch': switch, 'aamodes': aamodes, 'cost': cost,
     'valid': valid, 'analyse_look': analyse_look, 'analyse_moving': analyse_moving,
     'analyse_switch': analyse_switch, 'analyse_aamodes': analyse_aamodes}[sys.argv[1]]()
