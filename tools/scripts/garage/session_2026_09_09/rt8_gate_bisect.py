# -*- coding: utf-8 -*-
"""Which half of the job-3 patch moved the picture?

The A/A says the bridge is repeatable, so the null test's failure is the
patch's. Four arms, each the one before it plus one thing:

  head       what is committed
  restructure  the early return replaced by a flag, nothing else
  signature    plus the third attachment written
  full         plus the gate and its counters

The first pair that differs names the cause.
"""
import io, os, re, shutil, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
STAGED = os.path.join(RT, 'assets', 'shaders', 'water_accumulate.rvshader')
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders', 'water_accumulate.rvshader')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')

CAMERAS = {
    'glitter': '500,2.5,180,0.01,-90,-1.146',
    'pier':    '70,4.5,705,0.01,-46.98,-2.86',
}

# Both arms are compared as LF text and staged as LF: a mixed-ending file is
# what makes a scripted shader edit silently do nothing here.
CRLF = chr(13) + chr(10)
LF = chr(10)
full = io.open(SRC, encoding='utf-8', newline='').read().replace(CRLF, LF)
head = subprocess.run(
    ['git', 'show', 'HEAD:RageVEditor/assets/shaders/water_accumulate.rvshader'],
    cwd=ROOT, capture_output=True, timeout=120).stdout.decode('utf-8').replace(CRLF, LF)

# --- arm 3: the gate computed but never called ---------------------------
signature = full.replace(
    '''	bool gateKept = false, gateStrict = false, gateHalf = false, gatePlane = false;
	int gateRefusal = 0;
	const bool decided = water && u_Lamps.History.x > 0.5;
	if (decided)
	{
		MeasureContractGate(texel, size, position.xyz, N,
							texelFetch(u_WaveMotion, texel, 0).xy,
							gateKept, gateStrict, gateHalf, gatePlane, gateRefusal);
	}
	CountWaterGate(water, decided, gateKept, gateStrict, gateHalf, gatePlane,
				   gateRefusal, clamped);
''', '')
if signature == full:
    sys.exit('could not remove the gate call')

# --- arm 2: and the signature write dropped too --------------------------
restructure = signature.replace(
    '''	const vec3 N = OctDecode(texelFetch(u_SurfaceIn, texel, 0).rg);
	o_Signature = water ? vec4(OctEncode(N), dot(N, position.xyz), 1.0) : vec4(0.0);
''', '	o_Signature = vec4(0.0);\n')
if restructure == signature:
    sys.exit('could not remove the signature write')

ARMS = [('head', head), ('restructure', restructure),
        ('signature', signature), ('full', full)]


def shot(tag, camera, frame=60):
    out = os.path.join(SHOTS, tag + '.png')
    if os.path.exists(out):
        os.remove(out)
    p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                        '--project=' + os.path.join(ROOT, 'SampleProject'),
                        '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan',
                        '--render-defaults=off', '--vsync=off',
                        '--width=1600', '--height=900',
                        '--screenshot=' + out, '--screenshot-frame=%d' % frame,
                        '--screenshot-count=1', '--frame-time=0.0166',
                        '--camera=' + camera],
                       cwd=RT, capture_output=True, text=True, timeout=1800,
                       errors='replace')
    if not os.path.exists(out):
        print('  ', tag, 'NO FRAME')
        for line in (p.stdout or '').splitlines():
            if 'did not compile' in line or 'error' in line.lower():
                print('   !!', line.strip()[:200])
        return None
    return out


def arr(p):
    return np.asarray(Image.open(p).convert('RGB'), dtype=float)


try:
    for tag, text in ARMS:
        io.open(STAGED, 'w', encoding='utf-8', newline='').write(text)
        for name, cam in CAMERAS.items():
            shot('bi_%s_%s' % (tag, name), cam)
finally:
    shutil.copyfile(SRC, STAGED)

print()
for i in range(len(ARMS) - 1):
    a_tag, b_tag = ARMS[i][0], ARMS[i + 1][0]
    for name in CAMERAS:
        a = os.path.join(SHOTS, 'bi_%s_%s.png' % (a_tag, name))
        b = os.path.join(SHOTS, 'bi_%s_%s.png' % (b_tag, name))
        if not (os.path.exists(a) and os.path.exists(b)):
            print('%-12s -> %-12s %-8s missing' % (a_tag, b_tag, name)); continue
        d = np.abs(arr(a) - arr(b))
        print('%-12s -> %-12s %-8s max %3.0f levels, %7.4f%% of pixels  %s'
              % (a_tag, b_tag, name, d.max(), 100.0 * (d.max(axis=2) > 0).mean(),
                 'identical' if d.max() == 0 else '*** MOVED ***'))
