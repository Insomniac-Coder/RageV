# -*- coding: utf-8 -*-
"""RT-24: the smear a fast camera spin leaves, measured instead of watched.

The garage with its mouse camera swapped for the Orbiter (SampleProject/Source/
Orbiter.cpp): held still two seconds so every history settles, a turn round the car
at the owner's drag pace, then held still at the new pose. The frame at the stop
against the same pose three seconds later is the smear; the frames between say how
fast it clears. A run with no turn is the floor -- what differs between two frames
of a still camera anyway (the stochastic signals' own flicker).

    spin_measure.py <tag> [--speed=240] [--spin=1.0] [--stage=NAME] [--no-frame-memory]
                    [engine flags ...]

Clock pinned (--frame-time, never 0), so the pose at every frame is the same in
every arm. Arms are watch_arm.py's VARIANTS. Captures are deleted after scoring;
the stop frame, the settled frame and the difference picture are kept in
build/rt24/<tag>_*.png for showing.
"""
import glob
import io
import os
import re
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import watch_arm as wa          # noqa: E402
sr, burst = wa.sr, wa.burst

FPS = 60
DELAY = 2.0
SETTLE = 3.0
OUT = os.path.join(sr.ROOT, 'build', 'rt24')
RT = os.path.join(sr.ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
N = chr(10)


PAN = '--pan' in sys.argv


def orbit_scene(speed, spin, swing=0.0, rate=2.0):
    text = io.open(os.path.join(sr.SCENES, 'showroom.rage'), 'rb').read().decode('utf-8')
    text = text.replace(chr(13) + N, N)
    block = re.search('    ManagedScriptComponent:' + N + '      Script: ShowroomCamera' + N
                      + '      Fields:' + N + '(?:        .*' + N + ')+', text)
    if not block:
        sys.exit('no ShowroomCamera block to swap out')
    fields = dict(re.findall(r'        (\w+): (\S+)', block.group(0)))
    # The turn ends at the authored pose -- the owner's default view, the car, the
    # poles and both screen edges in frame -- so it starts that far back.
    if not swing:
        fields['Yaw'] = '%g' % (float(fields['Yaw']) - speed * spin)
    orbit = ('    NativeScriptComponent:' + N + '      Script: Orbiter' + N + '      Fields:' + N
             + ''.join('        %s: %s%s' % (k, fields[k], N)
                       for k in ('TargetX', 'TargetY', 'TargetZ', 'Yaw', 'Pitch', 'Distance'))
             + '        Speed: %g%s        Delay: %g%s        SpinFor: %g%s' % (speed, N, DELAY, N, spin, N)
             + '        Swing: %g%s        Rate: %g%s' % (swing, N, rate, N)
             # RT-24: the swing as a pan about the camera -- no parallax (Orbiter.Pan).
             + ('        Pan: 1' + N if PAN else ''))
    return (text[:block.start()] + orbit + text[block.end():]).encode('utf-8')


def stage(name):
    if not name:
        return
    triples = []
    for part in name.split('+'):
        spec = wa.VARIANTS[part]
        triples += spec if isinstance(spec, list) else [spec]
    texts = {}
    for shader, was, now in triples:
        src = os.path.join(sr.ROOT, 'RageVEditor', 'assets', 'shaders', shader)
        if shader not in texts:
            texts[shader] = io.open(src, encoding='utf-8', newline='').read()
        text = texts[shader]
        eol = chr(13) + N if text.count(chr(13) + N) > text.count(N) // 2 else N
        a, b = was.replace(N, eol), now.replace(N, eol)
        if text.count(a) != 1:
            sys.exit('%s: anchor matched %d times' % (name, text.count(a)))
        texts[shader] = text.replace(a, b)
    for shader, text in texts.items():
        io.open(os.path.join(sr.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)


def project(no_frame_memory, moving_feedback=None):
    """Edit the project for one run (restored byte for byte after it): the frame
    filter's memory off, or its moving feedback set (RT2-0 arm A3) -- the owner's own
    setting, so a changed value is only ever an arm, never a default."""
    edits = []
    if no_frame_memory:
        edits += [('    TemporalFeedback: 0.9', '    TemporalFeedback: 0'),
                  ('    TemporalStillFeedback: 0.98', '    TemporalStillFeedback: 0')]
    elif moving_feedback is not None:
        edits += [('    TemporalFeedback: 0.9', '    TemporalFeedback: %g' % moving_feedback)]
    if not edits:
        return
    text = sr.PROJECT_BYTES.decode('utf-8')
    eol = chr(13) + N if chr(13) + N in text else N
    for was, now in edits:
        if text.count(was + eol) != 1:
            sys.exit('%s matched %d times' % (was.strip(), text.count(was + eol)))
        text = text.replace(was + eol, now + eol)
    io.open(sr.PROJECT, 'wb').write(text.encode('utf-8'))


def blocks(x, b=8):
    h, w = x.shape
    h, w = h - h % b, w - w % b
    return x[:h, :w].reshape(h // b, b, w // b, b).mean(axis=(1, 3))


def smear(cur, truth):
    """Grain-free: 8x8 block means, so a young picture's grain averages out and a
    displaced or washed-out reflection does not. Whole frame, and the floor (the
    bottom 45%) where the owner sees it."""
    d = np.abs(blocks(cur) - blocks(truth))
    floor = d[int(d.shape[0] * 0.55):]
    return 100.0 * (d > 6).mean(), 100.0 * (floor > 6).mean()


def luma(path):
    a = np.asarray(Image.open(path).convert('RGB'), dtype=np.float32)
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2], a


# **The spots RT-24 is judged on** (x0, y0, x1, y1 at 1600x900, the swing's settled pose):
# the right pole's reflection on the wet floor, the car's reflection under the bumper,
# and a patch of the ceiling tubes' reflections on the floor. RT2-0 arm 0 (2026-09-24).
SPOTS = {'pole': (1400, 560, 1560, 760), 'car': (660, 560, 940, 700), 'tubes': (420, 660, 1040, 850)}


def spot(x, name):
    x0, y0, x1, y1 = SPOTS[name]
    return x[y0:y1, x0:x1]


def detail(x):
    """High-frequency energy: the picture minus its 5x5 average. The moving frame's
    against the settled frame's says how much sharpness a swing costs (1.00 = as sharp)."""
    from numpy.lib.stride_tricks import sliding_window_view
    b = sliding_window_view(np.pad(x, 2, mode='edge'), (5, 5)).mean(axis=(2, 3))
    return np.abs(x - b).mean()


def run(tag, speed, spin, stage_name, no_frame_memory, flags):
    stop = int(round((DELAY + spin) * FPS))
    last = stop + int(SETTLE * FPS)
    shots = os.path.join(OUT, tag + '_frames')
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots)
    path = os.path.join(sr.SCENES, sr.HEAD_SCENE)
    io.open(path, 'wb').write(orbit_scene(speed, spin))
    burst.sync_bake('showroom_head')
    try:
        if not sr.restore():
            sys.exit('the staged shader copies did not restore')
        stage(stage_name)
        project(no_frame_memory)
        p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'), '--project=' + sr.PROJECT,
                            '--scene=scenes/' + sr.HEAD_SCENE, '--rhi=vulkan', '--render-defaults=off',
                            '--width=1600', '--height=900', '--import-cache=off', '--vsync=off',
                            '--frame-time=%.9f' % (1.0 / FPS),
                            '--screenshot-frame=%d' % stop, '--screenshot-count=%d' % (last - stop + 1),
                            '--screenshot=' + os.path.join(shots, 'f.png'),
                            '--benchmark=%d' % (last + 2)] + flags,
                           cwd=RT, capture_output=True, text=True, errors='replace', timeout=3600)
        bad = [l for l in (p.stdout + p.stderr).splitlines()
               if 'compile' in l.lower() and 'irradiance fill' not in l]
        if bad:
            print('COMPILE:', bad[:3])
    finally:
        sr.restore()
        io.open(sr.PROJECT, 'wb').write(sr.PROJECT_BYTES)
        for f in (sr.HEAD_SCENE, sr.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(sr.SCENES, f))
            except OSError:
                pass
    frames = sorted(glob.glob(os.path.join(shots, '*.png')),
                    key=lambda f: int(re.search(r'(\d+)\.png$', f).group(1)))
    if len(frames) < last - stop + 1:
        sys.exit('%d frames captured of %d' % (len(frames), last - stop + 1))
    truth, truth_rgb = luma(frames[-1])
    h, w = truth.shape
    edge = np.zeros_like(truth, dtype=bool)
    edge[:, :160] = edge[:, -160:] = True
    print('%s  (turn %g deg/s for %gs, %s)' % (tag, speed, spin,
          ' '.join(([('--stage=' + stage_name)] if stage_name else [])
                   + (['--no-frame-memory'] if no_frame_memory else []) + flags) or 'as it ships'))
    print('   after stop   mean diff   >16 levels (frame)   >16 (screen edges)')
    for k in (0, 1, 2, 5, 10, 20, 30, 60, 120):
        cur, rgb = luma(frames[k])
        d = np.abs(cur - truth)
        print('   %3d frames   %8.2f    %8.2f%%           %8.2f%%' % (
            k, d.mean(), 100.0 * (d > 16).mean(), 100.0 * (d[edge] > 16).mean()))
        if k == 0:
            stop_rgb = rgb
    diff = np.abs(stop_rgb - truth_rgb).max(axis=2)
    pic = np.clip(diff * 4.0, 0, 255).astype(np.uint8)
    Image.fromarray(pic).save(os.path.join(OUT, tag + '_diff.png'))
    Image.fromarray(stop_rgb.astype(np.uint8)).save(os.path.join(OUT, tag + '_stop.png'))
    Image.fromarray(truth_rgb.astype(np.uint8)).save(os.path.join(OUT, tag + '_settled.png'))
    shutil.rmtree(shots, ignore_errors=True)




def swing_run(tag, swing, rate, spin, stage_name, no_frame_memory, flags, moving_feedback=None, window=1.0):
    """The owner's test (2026-09-23): at the front, a fast swing `swing` degrees either
    side of centre, `rate` times a second, for `spin` seconds, then still at centre. Every
    half period the camera crosses centre at full speed -- on exactly the pose it settles
    at -- so those burst frames against the settled one are the smear *during* motion."""
    stop = int(round((DELAY + spin) * FPS))
    # The last `window` seconds of the swing are captured and scored (one by default; a
    # slow swing crosses centre less often and needs more to have crossings to score).
    first = int(round((DELAY + spin - window) * FPS)) - 3
    last = stop + int(SETTLE * FPS)
    shots = os.path.join(OUT, tag + '_frames')
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots)
    io.open(os.path.join(sr.SCENES, sr.HEAD_SCENE), 'wb').write(orbit_scene(0.0, spin, swing, rate))
    burst.sync_bake('showroom_head')
    try:
        if not sr.restore():
            sys.exit('the staged shader copies did not restore')
        stage(stage_name)
        project(no_frame_memory, moving_feedback)
        p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'), '--project=' + sr.PROJECT,
                            '--scene=scenes/' + sr.HEAD_SCENE, '--rhi=vulkan', '--render-defaults=off',
                            '--width=1600', '--height=900', '--import-cache=off', '--vsync=off',
                            '--frame-time=%.9f' % (1.0 / FPS),
                            '--screenshot-frame=%d' % first, '--screenshot-count=%d' % (last - first + 1),
                            '--screenshot=' + os.path.join(shots, 'f.png'),
                            '--benchmark=%d' % (last + 2)] + flags,
                           cwd=RT, capture_output=True, text=True, errors='replace', timeout=3600)
        # The engine's own log, kept beside the pictures: its "render graph, by pass"
        # table says which passes ran at all (RT2-0 arm C checks ReflectionBudget).
        io.open(os.path.join(OUT, tag + '_log.txt'), 'w', encoding='utf-8').write(p.stdout + p.stderr)
        bad = [l for l in (p.stdout + p.stderr).splitlines()
               if 'compile' in l.lower() and 'irradiance fill' not in l]
        if bad:
            print('COMPILE:', bad[:3])
    finally:
        sr.restore()
        io.open(sr.PROJECT, 'wb').write(sr.PROJECT_BYTES)
        for f in (sr.HEAD_SCENE, sr.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(sr.SCENES, f))
            except OSError:
                pass
    frames = sorted(glob.glob(os.path.join(shots, '*.png')),
                    key=lambda f: int(re.search(r'(\d+)\.png$', f).group(1)))
    if len(frames) < last - first + 1:
        sys.exit('%d frames captured of %d' % (len(frames), last - first + 1))
    at = lambda frame: frames[frame - first]
    truth, truth_rgb = luma(at(last))
    print('%s  (swing +-%g deg at %g Hz for %gs, %s)' % (tag, swing, rate, spin,
          ' '.join(([('--stage=' + stage_name)] if stage_name else [])
                   + (['--no-frame-memory'] if no_frame_memory else [])
                   + (['--moving-feedback=%g' % moving_feedback] if moving_feedback is not None else [])
                   + flags) or 'as it ships'))
    # The crossings in the captured second: turning = n / (2 rate). The tick that lands
    # on it is found, not assumed -- a frame off at full speed is four degrees off.
    worst = None
    crossings = []
    print('   crossing centre at full speed   mean diff   >16 levels   smear blocks (frame / floor)')
    n = 0
    while True:
        turning = n / (2.0 * rate)
        n += 1
        if turning < spin - window + 1e-6:
            continue
        if turning > spin - 1e-6:
            break
        guess = int(round((DELAY + turning) * FPS))
        best = None
        for f in range(guess - 2, guess + 3):
            if first <= f <= stop - 1:
                cur, rgb = luma(at(f))
                d = np.abs(cur - truth)
                if best is None or d.mean() < best[0]:
                    best = (d.mean(), f, d, rgb, cur)
        if best:
            crossings.append(best[4])
            s = smear(best[4], truth)
            print('   frame %4d                      %8.2f    %8.2f%%      %6.2f%% / %6.2f%%' % (
                best[1], best[0], 100.0 * (best[2] > 16).mean(), s[0], s[1]))
            if worst is None or best[0] > worst[0]:
                worst = best
    # **The scores RT2-0 is gated on** (arm 0). Lower is better for all but sharpness.
    #   ghost    mean |moving - settled| over the spot, on the worst crossing (the frame
    #            saved as _moving.png, as every RT-24 number before 2026-09-24 was taken)
    #            and averaged over every crossing;
    #   grain    the per-pixel spread across the crossings, which all sit on the settled
    #            pose, so an ideal picture would not change between them at all;
    #   sharp    the floor's fine detail on the worst crossing against the settled frame's.
    scores = {'crossings': len(crossings)}
    stack = np.stack(crossings) if crossings else None
    print('   scores (lower is better; sharpness 1.00 = as sharp as settled)')
    for name in SPOTS:
        if SPOTS[name] is None or stack is None:
            continue
        ghost = float(np.abs(spot(worst[4], name) - spot(truth, name)).mean())
        ghost_all = float(np.mean([np.abs(spot(c, name) - spot(truth, name)).mean() for c in crossings]))
        grain = float(spot(stack.std(axis=0), name).mean())
        scores[name] = {'ghost': round(ghost, 2), 'ghost_all': round(ghost_all, 2), 'grain': round(grain, 2)}
        print('   %-6s ghost %6.2f (all crossings %6.2f)   grain %6.2f' % (name, ghost, ghost_all, grain))
    floor = slice(480, 860)
    scores['sharp'] = round(float(detail(worst[4][floor]) / max(detail(truth[floor]), 1e-6)), 3)
    print('   floor sharpness %.3f' % scores['sharp'])
    # **Edges** (RT2-0, 2026-09-24): the settled frame's sharpest tenth of pixels -- outlines,
    # pole and car silhouettes and their reflections -- and how far every crossing sits from
    # it there. Jitter and shimmer at outlines show here first; lower is better.
    gy, gx = np.gradient(truth)
    grad = np.hypot(gx, gy)
    mask = grad > np.percentile(grad, 90)
    scores['edges'] = round(float(np.mean([np.abs(c - truth)[mask].mean() for c in crossings])), 2)
    print('   edges  %6.2f' % scores['edges'])
    print('   after stop   mean diff   >16 levels   pole   car')
    scores['after'] = {}
    for k in (0, 2, 5, 10, 20, 30, 60):
        cur, rgb_k = luma(at(stop + k))
        if k in (0, 2, 5):
            Image.fromarray(rgb_k.astype(np.uint8)).save(os.path.join(OUT, '%s_after%d.png' % (tag, k)))
        d = np.abs(cur - truth)
        pole, car = float(spot(d, 'pole').mean()), float(spot(d, 'car').mean())
        scores['after'][k] = {'mean': round(float(d.mean()), 3), 'pole': round(pole, 2), 'car': round(car, 2)}
        print('   %3d frames   %8.2f    %8.2f%%    %5.2f  %5.2f' % (k, d.mean(), 100.0 * (d > 16).mean(), pole, car))
    # The parked floor: two settled frames half a second apart, so what the stochastic
    # signals change between frames of a still camera anyway. "Settled" means within it.
    d = np.abs(luma(at(last - 30))[0] - truth)
    scores['parked'] = {'mean': round(float(d.mean()), 3), 'pole': round(float(spot(d, 'pole').mean()), 2),
                        'car': round(float(spot(d, 'car').mean()), 2)}
    print('   parked floor %8.2f                 %5.2f  %5.2f' % (d.mean(), scores['parked']['pole'], scores['parked']['car']))
    import json
    io.open(os.path.join(OUT, tag + '_scores.json'), 'w', encoding='utf-8').write(json.dumps(scores, indent=1))
    for f in range(stop - 8, stop, 2):
        Image.open(at(f)).save(os.path.join(OUT, '%s_before%d.png' % (tag, stop - f)))
    moving = worst[3].astype(np.uint8)
    Image.fromarray(moving).save(os.path.join(OUT, tag + '_moving.png'))
    Image.fromarray(truth_rgb.astype(np.uint8)).save(os.path.join(OUT, tag + '_settled.png'))
    diff = np.clip(np.abs(worst[3] - truth_rgb).max(axis=2) * 4.0, 0, 255).astype(np.uint8)
    Image.fromarray(diff).save(os.path.join(OUT, tag + '_movingdiff.png'))
    # For showing: the car and the poles, mid-swing above, settled below, full size.
    box = (560, 0, 1600, 620)
    a, b = Image.fromarray(moving).crop(box), Image.fromarray(truth_rgb.astype(np.uint8)).crop(box)
    pair = Image.new('RGB', (a.width, a.height * 2 + 8), (200, 0, 0))
    pair.paste(a, (0, 0))
    pair.paste(b, (0, a.height + 8))
    pair.save(os.path.join(OUT, tag + '_pair.png'))
    shutil.rmtree(shots, ignore_errors=True)


if __name__ == '__main__':
    tag = sys.argv[1]
    speed, spin, stage_name, nfm, rest = 240.0, 1.0, None, False, []
    for a in sys.argv[2:]:
        if a.startswith('--speed='):
            speed = float(a.split('=', 1)[1])
        elif a.startswith('--spin='):
            spin = float(a.split('=', 1)[1])
        elif a.startswith('--stage='):
            stage_name = a.split('=', 1)[1]
        elif a == '--no-frame-memory':
            nfm = True
        else:
            rest.append(a)
    os.makedirs(OUT, exist_ok=True)
    swing = [float(a.split('=', 1)[1]) for a in rest if a.startswith('--swing=')]
    rate = [float(a.split('=', 1)[1]) for a in rest if a.startswith('--rate=')]
    mfb = [float(a.split('=', 1)[1]) for a in rest if a.startswith('--moving-feedback=')]
    win = [float(a.split('=', 1)[1]) for a in rest if a.startswith('--window=')]
    rest = [a for a in rest if not a.startswith(('--swing=', '--rate=', '--moving-feedback=', '--window=', '--pan'))]
    if swing:
        swing_run(tag, swing[0], rate[0] if rate else 2.0, spin if spin != 1.0 else 2.0, stage_name, nfm, rest,
                  mfb[0] if mfb else None, win[0] if win else 1.0)
    else:
        run(tag, speed, spin, stage_name, nfm, rest)
