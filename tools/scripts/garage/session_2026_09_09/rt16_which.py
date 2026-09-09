# -*- coding: utf-8 -*-
"""RT-16: which term inside the accumulator holds the dead light?

The four-arm measurement says the lag is the *series* -- 111 frames as shipped,
110 with TAA's still feedback off, 101 with the accumulator's history off, and
31 with both off. Removing either filter alone changes almost nothing, which is
what "memories in series compound" looks like from the outside.

This looks inside the accumulator. Its history is held to the fresh
neighbourhood's spread, and three things decide how far it may sit from it:

  ramp     the bound opens to twelve spreads on a smooth surface, on the
           argument that a mirror's rays hardly scatter -- and a wet floor is
           smooth, so the bound there is at its widest
  moments  a floor under the bound from the pixel's own recent variance, with
           a sixteen-frame memory of its own
  memory   how many frames the average carries at all

Each is ablated on its own, staged over the shipped shader, and the frames-to-
settle re-counted. The picture is wrong on purpose in each arm and the number
is honest -- the contract `--water-ablate` and `--shade-lights` already carry.
"""
import io, os, re, shutil, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')
BURST = os.path.join(ROOT, 'tools', 'scripts', 'garage', 'burst.py')
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders', 'reflection_accumulate.rvshader')
STAGED = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime',
                      'assets', 'shaders', 'reflection_accumulate.rvshader')
CRLF, LF = chr(13) + chr(10), chr(10)
SWITCH = 'Tube ,Bottom light bars|1.328'
FIRST, COUNT = 60, 150

full = io.open(SRC, encoding='utf-8', newline='').read().replace(CRLF, LF)

RAMP = '''			const float width = PositionLane()
							  ? max(u_Reflection.Probe.x, 0.0)
							  : mix(12.0, max(u_Reflection.Probe.x, 0.0),
									smoothstep(0.08, 0.3, roughness));'''
MOMENTS = '''			const vec3 halfWidth = max(sd * width, vec3(kTemporalSigma * sigma));'''

arms = {'shipped': full}
a = full.replace(RAMP, '			const float width = max(u_Reflection.Probe.x, 0.0);')
if a == full:
    sys.exit('the width ramp is not where this expects it')
arms['no-ramp'] = a
b = full.replace(MOMENTS, '			const vec3 halfWidth = sd * width;')
if b == full:
    sys.exit('the moments floor is not where this expects it')
arms['no-moments'] = b
c = a.replace(MOMENTS, '			const vec3 halfWidth = sd * width;')
arms['neither'] = c


def frames(tag):
    fs = [f for f in os.listdir(SHOTS) if f.startswith(tag + '_') and f.endswith('.png')]
    return sorted(fs, key=lambda f: int(re.search(r'_(\d+)\.png$', f).group(1)))


def lum(p):
    a = np.asarray(Image.open(os.path.join(SHOTS, p)).convert('RGB'), dtype=float)
    return a @ np.array([0.2126, 0.7152, 0.0722])


def burst(tag):
    env = dict(os.environ, BURST_SWITCH=SWITCH)
    subprocess.run([sys.executable, BURST, tag, '--speed=0', '--stop=0.1',
                    '--frames=%d' % COUNT, '--from=%d' % FIRST],
                   cwd=ROOT, capture_output=True, text=True, timeout=3600,
                   errors='replace', env=env)
    return frames(tag)


try:
    for name, text in arms.items():
        io.open(STAGED, 'w', encoding='utf-8', newline='').write(text)
        for f in os.listdir(SHOTS):
            if f.startswith('rt16w_' + name + '_'):
                os.remove(os.path.join(SHOTS, f))
        got = burst('rt16w_' + name)
        print('  %-11s %d frames' % (name, len(got)))
finally:
    shutil.copyfile(SRC, STAGED)
    print('staged copy restored and identical:',
          io.open(SRC, 'rb').read() == io.open(STAGED, 'rb').read())

base = frames('rt16w_shipped')
nums = [int(re.search(r'_(\d+)\.png$', f).group(1)) for f in base]
before, after = lum(base[nums.index(75)]), lum(base[-1])
mask = (before > np.percentile(before, 85)) & (after < before * 0.75)
print('\nregion: %.2f%% of the frame' % (100 * mask.mean()))
print('\n%-11s %-8s %-8s %-9s %-9s' % ('arm', 'lit', 'settled', 'frames', 'seconds'))
for name in arms:
    fs = frames('rt16w_' + name)
    if len(fs) < COUNT:
        print('%-11s only %d frames' % (name, len(fs))); continue
    v = np.array([lum(f)[mask].mean() for f in fs])
    lit, end = v[:20].mean(), v[-15:].mean()
    span = lit - end
    if span <= 0.5:
        print('%-11s %-8.2f %-8.2f nothing to time' % (name, lit, end)); continue
    r = np.where(v[20:] <= end + 0.05 * span)[0]
    n = int(r[0]) if len(r) else -1
    print('%-11s %-8.2f %-8.2f %-9s %-9s'
          % (name, lit, end, n if n >= 0 else 'never',
             '%.2f' % (n * 0.0166) if n >= 0 else '--'))
