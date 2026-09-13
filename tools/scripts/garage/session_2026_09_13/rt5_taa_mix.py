# -*- coding: utf-8 -*-
"""RT-5 part 3's darkening, traced to the temporal resolve's brightness mix.

The bias arms showed the accumulator's bound owns 0.02 of the floor's -0.16,
the sampler none of it (with no anti-aliasing the sampled light matches every
light), and TAA's memory none of it (still feedback 0 leaves it where it was).
What is left inside TAA and independent of its memory is the blend itself:

    blended = Expand(mix(Compress(current), Compress(history), 1 - alpha))

Compress is Y/(1+Y), concave, so the running mean it builds of a *noisy* input
converges below the input's mean -- Jensen's inequality -- by roughly the
input's variance over (1+Y), whatever the feedback. Two equal samples come
back unchanged, which is the property the shader's comment rests on and why a
still reference never shows it.

This stages a plain linear mix into the runtime's copy of taa_resolve, runs
the pair at K = 8 and at every light, and restores the copy. The contract is
as committed.
"""
import io, os, re, shutil, subprocess, sys, time
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders', 'taa_resolve.rvshader')
STAGED = os.path.join(RT, 'assets', 'shaders', 'taa_resolve.rvshader')
SCENES = os.path.join(ROOT, 'SampleProject', 'assets', 'scenes')
HEAD_SCENE = 'showroom_head.rage'
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')
OUT = os.path.join(ROOT, 'build', 'rt5')
BURST = os.path.join(ROOT, 'tools', 'scripts', 'garage', 'burst.py')
CRLF, LF = chr(13) + chr(10), chr(10)
FIRST, COUNT, MEAN_FROM = 150, 100, 186

MIX = ('\tvec3 blended = Expand(mix(Compress(centreYCoCg), Compress(history),\n'
       '\t\t\t\t\t\t\t  1.0 - alpha));')
LINEAR = '\tvec3 blended = mix(centreYCoCg, history, 1.0 - alpha);'

raw = io.open(SRC, encoding='utf-8', newline='').read()
crlf = CRLF in raw
text = raw.replace(CRLF, LF)
if text.count(MIX) != 1:
    sys.exit('the blend line matched %d' % text.count(MIX))
CLIP = '	history = ClipToBox(history, boxCentre, boxExtent);'
if text.count(CLIP) != 1:
    sys.exit('the clip line matched %d' % text.count(CLIP))
VARIANTS = {'linmix': text.replace(MIX, LINEAR), 'noclip': text.replace(CLIP, '')}
for k in VARIANTS:
    VARIANTS[k] = VARIANTS[k].replace(LF, CRLF) if crlf else VARIANTS[k]

# The second question, after the linear mix left the bias where it was: the
# neighbourhood clip, which binds on a noisy input whatever the feedback.
ARMS = [('linmix_k8', 'linmix', []), ('linmix_k0', 'linmix', ['--rays-per-pixel=0']),
        ('noclip_k8', 'noclip', []), ('noclip_k0', 'noclip', ['--rays-per-pixel=0'])]
only = sys.argv[1:]
if only:
    ARMS = [a for a in ARMS if a[0] in only]

head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                      cwd=ROOT, capture_output=True, check=True).stdout
io.open(os.path.join(SCENES, HEAD_SCENE), 'wb').write(head)
try:
    for tag, variant, extra in ARMS:
        io.open(STAGED, 'w', encoding='utf-8', newline='').write(VARIANTS[variant])
        t0 = time.time()
        env = dict(os.environ, BURST_SCENE=HEAD_SCENE)
        env.pop('BURST_SWITCH', None)
        env.pop('BURST_SLIDE', None)
        cmd = [sys.executable, BURST, 'rt5b_' + tag, '--speed=0', '--stop=0.1',
               '--frames=%d' % COUNT, '--from=%d' % FIRST] + ['--extra=' + e for e in extra]
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=3600,
                           errors='replace', env=env)
        got = sorted([f for f in os.listdir(SHOTS) if re.match(r'rt5b_%s_\d+\.png$' % tag, f)],
                     key=lambda f: int(re.search(r'_(\d+)\.png$', f).group(1)))
        if len(got) != COUNT:
            print(p.stdout[-3000:], p.stderr[-3000:])
            sys.exit('%s: %d frames of %d' % (tag, len(got), COUNT))
        acc = None
        for f in got:
            if int(re.search(r'_(\d+)\.png$', f).group(1)) < MEAN_FROM:
                continue
            a = np.asarray(Image.open(os.path.join(SHOTS, f)).convert('RGB'), dtype=np.float64)
            acc = a if acc is None else acc + a
        np.save(os.path.join(OUT, tag + '.npy'), (acc / float(FIRST + COUNT - MEAN_FROM)).astype(np.float32))
        print('%-10s %d frames, %.0f s' % (tag, len(got), time.time() - t0))
finally:
    shutil.copyfile(SRC, STAGED)
    print('staged taa_resolve restored and identical:', io.open(SRC, 'rb').read() == io.open(STAGED, 'rb').read())
    for f in (HEAD_SCENE, HEAD_SCENE + '.meta'):
        try:
            os.remove(os.path.join(SCENES, f))
        except OSError:
            pass
