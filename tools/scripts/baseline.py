#!/usr/bin/env python3
"""RT2-2a: the baseline harness -- the standard camera set, parked in every AA mode and over a
60-frame swing, against a stored baseline, in one command.

    python tools/scripts/baseline.py record [--name=NAME] [--scenes=a,b] [--aa=all|none,taa,...] [--no-swing]
    python tools/scripts/baseline.py check  [--name=NAME] [the same filters]

Every "diff 0" gate uses it. `record` renders the set and stores it under build/baseline/NAME/
(NAME defaults to "default"); `check` renders it again and compares every frame pixel for pixel,
printing a count per scene and writing a diff picture for each differing frame whose baseline
picture is kept, into build/baseline/NAME_check/. Exit 0 when every frame matches, 1 when any
differs, 2 when a render failed or the baseline lacks what was asked for.

Parked frames are taken at `--screenshot-frame=settled`: the first frame every history in force
is full (TemporalMemory), not a frame number chosen for whichever memory was longest at the time.
The swing is `--camera-swing` -- the owner's drag test, 15 degrees either side, twice a second, for
a second, after two still -- on the clock at 1/60 s; its 60 frames are compared by pixel hash and
every tenth is kept as a picture. ponytail: the whole swing at full size would be about 600 MB of
PNGs, so a differing frame between the kept ones is counted but has no diff picture; keep more
(SWING_KEEP) when one is needed.

The cameras are the measurement camera (`--camera`), so scenes framed by a script (the garage's
mouse orbit, the camp's) are held at the pose their script settles to -- printed by the runtime
as "Captured from --camera=..." -- and can be swung like any other.
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RUNTIME = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
STORE = os.path.join(ROOT, 'build', 'baseline')

BRIDGE = 'scenes/GoldenGateDemo.rage'
SCENES = {
    'garage':   ('scenes/showroom.rage', '0,1.355368,7.262258,0.01,0,5.000002'),
    'headland': (BRIDGE, '500,89.47,-1100,0.01,-157.08,8.88'),
    'deck':     (BRIDGE, '0,76.4,950,0.01,0,0'),
    'glitter':  (BRIDGE, '500,2.5,180,0.01,-90,-1.146'),
    'camp':     ('scenes/camp.rage', '3.457404,4.595,6.063566,0.01,-26.000019,30.000013'),
}
AA_MODES = ['none', 'fxaa', 'smaa', 'taa', 'msaa', 'ssaa']
WIDTH, HEIGHT = 1600, 900
SWING = '15,2,1,2'                 # degrees, rate, seconds, delay
SWING_FIRST, SWING_FRAMES = 121, 60  # the delay ends at frame 120 on a 1/60 s clock
SWING_KEEP = 10                    # every tenth swing frame is kept as a picture


def render(scene, camera, out, flags):
    command = [os.path.join(RUNTIME, 'RageVRuntime.exe'),
               '--project=' + os.path.join(ROOT, 'SampleProject', 'SampleProject.rvproject'),
               '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
               '--width=%d' % WIDTH, '--height=%d' % HEIGHT,
               '--scene=' + scene, '--camera=' + camera, '--screenshot=' + out] + flags
    p = subprocess.run(command, cwd=RUNTIME, capture_output=True, text=True, errors='replace', timeout=3600)
    return p.returncode, p.stdout + p.stderr


def pixels(path):
    return np.asarray(Image.open(path).convert('RGB'))


def digest(image):
    return hashlib.sha256(image.tobytes()).hexdigest()


def shots(args):
    """(scene, kind, key, flags, files): each run, and the frames it writes."""
    runs = []
    for scene in args.scenes:
        for aa in args.aa:
            runs.append((scene, 'parked', aa, ['--frame-time=0.000001', '--aa=' + aa,
                                               '--screenshot-frame=settled'], None))
        if args.swing:
            runs.append((scene, 'swing', 'swing', ['--frame-time=0.0166667', '--camera-swing=' + SWING,
                                                   '--screenshot-frame=%d' % SWING_FIRST,
                                                   '--screenshot-count=%d' % SWING_FRAMES],
                         list(range(SWING_FIRST, SWING_FIRST + SWING_FRAMES))))
    return runs


def run_all(args, sink):
    """Render every run into a scratch folder and hand each frame to `sink(scene, key, frame, path)`."""
    scratch = os.path.join(STORE, '_run')
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch)
    failed = []
    try:
        for scene, kind, key, flags, frames in shots(args):
            start = time.time()
            out = os.path.join(scratch, '%s_%s.png' % (scene, key))
            code, log = render(SCENES[scene][0], SCENES[scene][1], out, flags)
            if frames is None:
                paths = [(None, out)]
            else:
                stem = out[:-4]
                paths = [(f, '%s_%d.png' % (stem, f)) for f in frames]
            missing = [p for _, p in paths if not os.path.exists(p)]
            if code != 0 or missing:
                failed.append('%s %s: exit %d, %d frame(s) missing' % (scene, key, code, len(missing)))
                tail = [l for l in log.splitlines() if 'error' in l.lower() or 'NO NUMBERS' in l][-3:]
                print('  %-9s %-6s FAILED (exit %d)%s' % (scene, key, code,
                      ''.join('\n      ' + l for l in tail)), flush=True)
                continue
            for frame, path in paths:
                sink(scene, key, frame, path)
                os.remove(path)
            print('  %-9s %-6s %4.0f s' % (scene, key, time.time() - start), flush=True)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    return failed


def frame_name(scene, key, frame):
    return '%s_%s.png' % (scene, key) if frame is None else '%s_%s_%03d.png' % (scene, key, frame)


def kept(key, frame):
    return frame is None or (frame - SWING_FIRST) % SWING_KEEP == 0


def record(args):
    folder = os.path.join(STORE, args.name)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    manifest = {'commit': subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT,
                                         capture_output=True, text=True).stdout.strip(),
                'dirty': bool(subprocess.run(['git', 'status', '--porcelain', '--untracked-files=no'],
                                             cwd=ROOT, capture_output=True, text=True).stdout.strip()),
                'recorded': time.strftime('%Y-%m-%d %H:%M'), 'size': [WIDTH, HEIGHT], 'swing': SWING,
                'frames': {}}

    def sink(scene, key, frame, path):
        image = pixels(path)
        name = frame_name(scene, key, frame)
        manifest['frames'][name] = digest(image)
        if kept(key, frame):
            shutil.copyfile(path, os.path.join(folder, name))

    failed = run_all(args, sink)
    json.dump(manifest, open(os.path.join(folder, 'manifest.json'), 'w'), indent=1)
    size = sum(os.path.getsize(os.path.join(folder, f)) for f in os.listdir(folder))
    print('recorded %d frames (%d kept as pictures, %.0f MB) in %s'
          % (len(manifest['frames']), len([f for f in os.listdir(folder) if f.endswith('.png')]),
             size / 1e6, folder))
    if failed:
        print('FAILED: ' + '; '.join(failed))
        return 2
    return 0


def check(args):
    folder = os.path.join(STORE, args.name)
    try:
        manifest = json.load(open(os.path.join(folder, 'manifest.json')))
    except OSError:
        print('no baseline named %r -- record one first' % args.name)
        return 2
    out = os.path.join(STORE, args.name + '_check')
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    per_scene = {}
    absent = []

    def sink(scene, key, frame, path):
        name = frame_name(scene, key, frame)
        tally = per_scene.setdefault(scene, {'frames': 0, 'differ': 0, 'pixels': 0, 'notes': []})
        tally['frames'] += 1
        if name not in manifest['frames']:
            absent.append(name)
            return
        image = pixels(path)
        if digest(image) == manifest['frames'][name]:
            return
        tally['differ'] += 1
        reference = os.path.join(folder, name)
        if not os.path.exists(reference):
            tally['notes'].append('%s differs (no picture kept)' % name)
            return
        base = pixels(reference).astype(int)
        delta = np.abs(base - image.astype(int)).max(axis=2)
        count, worst = int((delta > 0).sum()), int(delta.max())
        tally['pixels'] += count
        heat = np.clip(delta * 8, 0, 255).astype(np.uint8)
        strip = np.concatenate([base.astype(np.uint8), image, np.stack([heat, heat // 4, heat // 4], 2)], 1)
        Image.fromarray(strip).save(os.path.join(out, name[:-4] + '_diff.png'))
        tally['notes'].append('%s: %d px differ, max %d levels' % (name, count, worst))

    print('checking against %s (recorded %s at %s%s)' % (args.name, manifest['recorded'], manifest['commit'],
                                                          ', with uncommitted changes' if manifest['dirty'] else ''))
    failed = run_all(args, sink)
    differing = 0
    for scene, tally in per_scene.items():
        state = 'identical' if not tally['differ'] else '%d of %d frames DIFFER (%d px in the kept ones)' % (
            tally['differ'], tally['frames'], tally['pixels'])
        print('%-9s %d frames: %s' % (scene, tally['frames'], state))
        for note in tally['notes']:
            print('    ' + note)
        differing += tally['differ']
    if absent:
        print('not in the baseline (record with the same filters): ' + ', '.join(absent[:6]))
    if differing:
        print('diff pictures in ' + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    if failed or absent:
        print('FAILED: ' + '; '.join(failed) if failed else '')
        return 2
    print('OK: every frame identical' if not differing else 'DIFFERS')
    return 1 if differing else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('mode', choices=['record', 'check'])
    parser.add_argument('--name', default='default')
    parser.add_argument('--scenes', default=','.join(SCENES))
    parser.add_argument('--aa', default='all')
    parser.add_argument('--no-swing', dest='swing', action='store_false')
    args = parser.parse_args()
    args.scenes = [s for s in args.scenes.split(',') if s]
    args.aa = AA_MODES if args.aa == 'all' else [a for a in args.aa.split(',') if a]
    unknown = [s for s in args.scenes if s not in SCENES] + [a for a in args.aa if a not in AA_MODES]
    if unknown:
        sys.exit('unknown: ' + ', '.join(unknown))
    return record(args) if args.mode == 'record' else check(args)


if __name__ == '__main__':
    sys.exit(main())
