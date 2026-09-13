# -*- coding: utf-8 -*-
"""RT-5 part 3, before anything is built: is there a bias, and is it the bound?

The row says the history bound clips a skewed K-sample estimate and leaves the
floor and the car -0.16 levels dark. That number was taken on 2026-09-06 at
K = 4, before RT-1 changed the light score and before the preset moved the
garage to K = 8. So it is measured again, and attributed, before a line of the
accumulator changes.

**The truth for the direct light is every light** (`--rays-per-pixel=0`): the
pass shades all of them, the garage's lamps have no source radius, so the
fresh estimate carries no sampling noise at all. The truest form of it is with
the bound off as well, where the history is exactly the average of identical
frames.

Arms, each a parked burst at the owner's shot, frames 150..249:

  ship     the accumulator as committed
  noclamp  the bound off on both payloads
  noclamp1 the bound off on the first payload only
  noclamp2 the bound off on the twin only

each at the preset's K and at every light. The staged shader is swapped for an
arm and restored after, and the restore is checked byte for byte. The scene is
HEAD's showroom, written beside the working copy, because the working copy was
re-saved by the editor on 2026-09-11 and nothing measured here should depend on
an uncommitted file.

Writes a float mean of frames 186..249 per arm to build/rt5/<arm>.npy; the
PNGs stay in build/garage_burst for the per-frame metrics.
"""
import io, os, re, shutil, subprocess, sys, time
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders', 'reflection_accumulate.rvshader')
STAGED = os.path.join(RT, 'assets', 'shaders', 'reflection_accumulate.rvshader')
SCENES = os.path.join(ROOT, 'SampleProject', 'assets', 'scenes')
HEAD_SCENE = 'showroom_head.rage'
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')
OUT = os.path.join(ROOT, 'build', 'rt5')
BURST = os.path.join(ROOT, 'tools', 'scripts', 'garage', 'burst.py')
CRLF, LF = chr(13) + chr(10), chr(10)
FIRST, COUNT = 150, 100
MEAN_FROM = 186

CLAMP1 = 'const vec3 held = clamp(c.past.rgb, mean - halfWidth, mean + halfWidth);'
CLAMP2 = 'const vec3 held2 = clamp(past2.rgb, mean2 - halfWidth2, mean2 + halfWidth2);'

raw = io.open(SRC, encoding='utf-8', newline='').read()
crlf = CRLF in raw
text = raw.replace(CRLF, LF)
for probe in (CLAMP1, CLAMP2):
    if text.count(probe) != 1:
        sys.exit('bound line matched %d: %s' % (text.count(probe), probe))


def variant(first, twin):
    s = text
    if not first:
        s = s.replace(CLAMP1, 'const vec3 held = c.past.rgb;')
    if not twin:
        s = s.replace(CLAMP2, 'const vec3 held2 = past2.rgb;')
    return s.replace(LF, CRLF) if crlf else s


SHADERS = {
    'ship':     variant(True, True),
    'noclamp':  variant(False, False),
    'noclamp1': variant(False, True),
    'noclamp2': variant(True, False),
}
if SHADERS['ship'] != raw:
    sys.exit('the unmodified variant does not reproduce the source byte for byte')

ARMS = []
for shader in ('ship', 'noclamp', 'noclamp1', 'noclamp2'):
    ARMS.append((shader + '_k8', shader, []))
    ARMS.append((shader + '_k0', shader, ['--rays-per-pixel=0']))
only = sys.argv[1:]
if only:
    # A named arm from the table above, or a new one spelled
    # tag=shader[,--flag[,--flag...]] -- the attribution runs that follow
    # the first eight add flags rather than shaders.
    known = {a[0]: a for a in ARMS}
    ARMS = []
    for spec in only:
        if '=' in spec and not spec.startswith('--'):
            tag, rest = spec.split('=', 1)
            parts = rest.split(',')
            if parts[0] not in SHADERS:
                sys.exit('no shader variant named %s' % parts[0])
            ARMS.append((tag, parts[0], parts[1:]))
        elif spec in known:
            ARMS.append(known[spec])
        else:
            sys.exit('no arm named %s' % spec)


def frames(tag):
    fs = [f for f in os.listdir(SHOTS) if re.match(re.escape(tag) + r'_\d+\.png$', f)]
    return sorted(fs, key=lambda f: int(re.search(r'_(\d+)\.png$', f).group(1)))


os.makedirs(OUT, exist_ok=True)
head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                      cwd=ROOT, capture_output=True, check=True).stdout
io.open(os.path.join(SCENES, HEAD_SCENE), 'wb').write(head)
try:
    for tag, shader, extra in ARMS:
        io.open(STAGED, 'w', encoding='utf-8', newline='').write(SHADERS[shader])
        t0 = time.time()
        env = dict(os.environ, BURST_SCENE=HEAD_SCENE)
        env.pop('BURST_SWITCH', None)
        env.pop('BURST_SLIDE', None)
        cmd = [sys.executable, BURST, 'rt5b_' + tag, '--speed=0', '--stop=0.1',
               '--frames=%d' % COUNT, '--from=%d' % FIRST]
        cmd += ['--extra=' + e for e in extra]
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=3600,
                           errors='replace', env=env)
        got = frames('rt5b_' + tag)
        if len(got) != COUNT:
            print(p.stdout[-3000:], p.stderr[-3000:])
            sys.exit('%s: %d frames of %d' % (tag, len(got), COUNT))
        nums = [int(re.search(r'_(\d+)\.png$', f).group(1)) for f in got]
        if nums != list(range(FIRST, FIRST + COUNT)):
            sys.exit('%s: frames out of order or missing' % tag)
        acc = None
        for f, n in zip(got, nums):
            if n < MEAN_FROM:
                continue
            a = np.asarray(Image.open(os.path.join(SHOTS, f)).convert('RGB'), dtype=np.float64)
            acc = a if acc is None else acc + a
        mean = acc / float(FIRST + COUNT - MEAN_FROM)
        np.save(os.path.join(OUT, tag + '.npy'), mean.astype(np.float32))
        print('%-12s %d frames, %.0f s, frame mean %.3f' % (tag, len(got), time.time() - t0, mean.mean()))
finally:
    shutil.copyfile(SRC, STAGED)
    same = io.open(SRC, 'rb').read() == io.open(STAGED, 'rb').read()
    print('staged shader restored and identical to source:', same)
    for f in (HEAD_SCENE, HEAD_SCENE + '.meta'):
        try:
            os.remove(os.path.join(SCENES, f))
        except OSError:
            pass
