# -*- coding: utf-8 -*-
"""Was the null test's control valid?

The bisect says the gate and the signature change nothing and the *restructure*
moved the picture -- which would be a real defect, since replacing an early
return with a flag cannot change any value this shader computes.

The other reading is that the control was invalid. The committed shader
declares six inputs and two outputs; the patched pipeline binds eight and
declares three. Staging the committed shader against the patched C++ therefore
runs a pass whose resource set is written past its own layout, which is not
"the engine as it was" -- it is a mismatch.

So this arm is the committed *logic* with the patched *layout*: the early
return back, the bindings and the third output declared and written. If it
matches the restructure, the control was the problem and nothing about the
restructure changed the sea.
"""
import io, os, shutil, subprocess, sys
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
    'deck':    '0,76.4,950,0.01,0,0',
}

CRLF, LF = chr(13) + chr(10), chr(10)
full = io.open(SRC, encoding='utf-8', newline='').read().replace(CRLF, LF)


def cut(text, old, new, what):
    if text.count(old) != 1:
        sys.exit('%s matched %d' % (what, text.count(old)))
    return text.replace(old, new, 1)


# The gate call and the signature value go; the declarations stay.
arm = cut(full, '''	bool gateKept = false, gateStrict = false, gateHalf = false, gatePlane = false;
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
''', '', 'gate call')
arm = cut(arm, '''	const vec3 N = OctDecode(texelFetch(u_SurfaceIn, texel, 0).rg);
	o_Signature = water ? vec4(OctEncode(N), dot(N, position.xyz), 1.0) : vec4(0.0);
''', '	o_Signature = vec4(0.0);\n', 'signature write')

# ...and the early return goes back, so the ONLY difference left from the
# restructure arm is the shape of the control flow.
arm = cut(arm, '''	const bool water = position.w > 0.5;
''', '''	if (position.w <= 0.5)
	{
		o_Diffuse = vec4(0.0);
		o_Specular = vec4(0.0);
		o_Signature = vec4(0.0);
		return;
	}
	const bool water = true;
''', 'early return back')

restructure = cut(full, '''	bool gateKept = false, gateStrict = false, gateHalf = false, gatePlane = false;
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
''', '', 'gate call 2')
restructure = cut(restructure, '''	const vec3 N = OctDecode(texelFetch(u_SurfaceIn, texel, 0).rg);
	o_Signature = water ? vec4(OctEncode(N), dot(N, position.xyz), 1.0) : vec4(0.0);
''', '	o_Signature = vec4(0.0);\n', 'signature write 2')

ARMS = [('earlyreturn', arm), ('restructure', restructure), ('full', full)]


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
                print('   !!', line.strip()[:220])
        return None
    return out


def arr(p):
    return np.asarray(Image.open(p).convert('RGB'), dtype=float)


try:
    for tag, text in ARMS:
        io.open(STAGED, 'w', encoding='utf-8', newline='').write(text)
        for name, cam in CAMERAS.items():
            shot('ct_%s_%s' % (tag, name), cam)
finally:
    shutil.copyfile(SRC, STAGED)

print()
print('the committed logic under the patched layout, against the restructure')
for a_tag, b_tag in (('earlyreturn', 'restructure'), ('earlyreturn', 'full')):
    for name in CAMERAS:
        a = os.path.join(SHOTS, 'ct_%s_%s.png' % (a_tag, name))
        b = os.path.join(SHOTS, 'ct_%s_%s.png' % (b_tag, name))
        if not (os.path.exists(a) and os.path.exists(b)):
            print('%-12s -> %-12s %-8s missing' % (a_tag, b_tag, name)); continue
        d = np.abs(arr(a) - arr(b))
        print('%-12s -> %-12s %-8s max %3.0f levels, %7.4f%% of pixels  %s'
              % (a_tag, b_tag, name, d.max(), 100.0 * (d.max(axis=2) > 0).mean(),
                 'BIT-IDENTICAL' if d.max() == 0 else '*** MOVED ***'))
