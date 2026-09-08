# -*- coding: utf-8 -*-
"""RT-8 job 3: does the twin's new memory disturb the direct light?

The pair kind has one other user -- the direct light, whose specular twin
forgets in four frames while its diffuse keeps sixty-four. That case wants the
twin *shorter*, which the old `min(frames, PairMemory)` gave it, so the change
must be checked there rather than assumed harmless.

A shader-only A/B, and here that control is valid: the change touches no
binding, no attachment and no push-constant slot, so the old line runs against
exactly the pipeline the new one does. (The earlier water A/B was invalid for
precisely the opposite reason -- see rt8_gate_control.py.)

The garage, parked and on the dolly, which is where the direct light was tuned.
"""
import io, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
STAGED = os.path.join(RT, 'assets', 'shaders', 'reflection_accumulate.rvshader')
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders', 'reflection_accumulate.rvshader')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')
CRLF, LF = chr(13) + chr(10), chr(10)

# The garage at its benchmark pose, and the bridge's pier for a scene where
# the pair also runs over water.
SHOTSETS = {
    'garage':  ('scenes/showroom.rage', '-2.3,0.72,-2,11,0,4'),
    'pier':    ('scenes/GoldenGateDemo.rage', '70,4.5,705,0.01,-46.98,-2.86'),
}

new = io.open(SRC, encoding='utf-8', newline='').read().replace(CRLF, LF)

# The old twin rule, put back with everything else left alone.
old = new.replace('''				float frames2 = frames;
				if (u_Reflection.Tuning.w > 0.0)
				{
					const float memory2 =
						max(ShortenedMemory(max(u_Reflection.Tuning.w, 1.0),
											moved, slack, fewest, silhouette,
											neighbourMotion) * reduced, fewest);
					frames2 = min(past2.a + 1.0, memory2);
				}''',
'''				float frames2 = u_Reflection.Tuning.w > 0.0
							  ? min(frames, u_Reflection.Tuning.w) : frames;''')
if old == new:
    sys.exit('could not restore the old twin rule')
old = old.replace('	o_Accumulated2 = vec4(kept2, twinFrames);',
                  '	o_Accumulated2 = vec4(kept2, frames);')


def shot(tag, scene, camera, frame=60):
    out = os.path.join(SHOTS, tag + '.png')
    if os.path.exists(out):
        os.remove(out)
    p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                        '--project=' + os.path.join(ROOT, 'SampleProject'),
                        '--scene=' + scene, '--rhi=vulkan',
                        '--render-defaults=off', '--vsync=off',
                        '--width=1600', '--height=900',
                        '--screenshot=' + out, '--screenshot-frame=%d' % frame,
                        '--screenshot-count=1', '--frame-time=0.0166',
                        '--camera=' + camera],
                       cwd=RT, capture_output=True, text=True, timeout=1800,
                       errors='replace')
    if not os.path.exists(out):
        print('  ', tag, 'NO FRAME')
        for line in ((p.stdout or '') + (p.stderr or '')).splitlines():
            if 'did not compile' in line or 'ERROR' in line:
                print('   !!', line.strip()[:220])
        return None
    return out


def arr(p):
    return np.asarray(Image.open(p).convert('RGB'), dtype=float)


try:
    for tag, text in (('before', old), ('after', new)):
        io.open(STAGED, 'w', encoding='utf-8', newline='').write(text)
        for name, (scene, cam) in SHOTSETS.items():
            shot('twin_%s_%s' % (name, tag), scene, cam)
finally:
    shutil.copyfile(SRC, STAGED)
    print('staged copy restored and identical:',
          io.open(SRC, 'rb').read() == io.open(STAGED, 'rb').read())

print()
print("the twin's own memory against the old ceiling")
for name in SHOTSETS:
    a = os.path.join(SHOTS, 'twin_%s_before.png' % name)
    b = os.path.join(SHOTS, 'twin_%s_after.png' % name)
    if not (os.path.exists(a) and os.path.exists(b)):
        print('  %-8s missing a frame' % name); continue
    d = np.abs(arr(a) - arr(b))
    print('  %-8s max %3.0f levels, %6.3f%% of pixels differ  %s'
          % (name, d.max(), 100.0 * (d.max(axis=2) > 0).mean(),
             'bit-identical' if d.max() == 0 else 'changed'))
