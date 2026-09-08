"""Is the sea's new motion attachment carrying anything?

Nothing reads it yet, so the only honest check is to make it visible: a staged
variant writes the motion into the water's colour instead, and the bridge's
glitter camera looks straight at the sea. Restored and compared byte for byte
whatever happens.
"""
import io, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders', 'include', 'pbr_fragment.glsl')
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
STAGED = os.path.join(RT, 'assets', 'shaders', 'include', 'pbr_fragment.glsl')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')
CAM = '500,2.5,180,0.01,-90,-1.146'      # the glitter camera, looking at the sea

OLD = '\to_MaterialWater = vec4(albedo, clamp(surface.Specular, 0.0, 1.0));'
RUNS = [
    ('rt8_sea_plain', OLD),
    # x400 so a motion of a few thousandths of the screen reads as a colour.
    ('rt8_sea_motion',
     '\to_MaterialWater = vec4(abs(\n'
     '\t\t((v_ClipPos.xy / max(abs(v_ClipPos.w), 1e-6) * sign(v_ClipPos.w)\n'
     '\t\t  - u_Scene.Jitter.xy)\n'
     '\t\t - (thenNDC - u_Scene.Jitter.zw)) * 0.5) * 400.0, 0.0,\n'
     '\t\tclamp(surface.Specular, 0.0, 1.0));'),
]

src = io.open(SRC, encoding='utf-8', newline='').read().replace('\r\n', '\n')
if src.count(OLD) != 1:
    sys.exit('anchor matched %d times' % src.count(OLD))

try:
    for tag, body in RUNS:
        io.open(STAGED, 'w', encoding='utf-8', newline='\r\n').write(src.replace(OLD, body, 1))
        out = os.path.join(SHOTS, tag + '.png')
        if os.path.exists(out):
            os.remove(out)
        p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                            '--project=' + os.path.join(ROOT, 'SampleProject'),
                            '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan',
                            '--render-defaults=off', '--vsync=off',
                            '--width=1600', '--height=900',
                            '--screenshot=' + out, '--screenshot-frame=60',
                            '--screenshot-count=1', '--frame-time=0.0166',
                            '--camera=' + CAM],
                           cwd=RT, capture_output=True, text=True, timeout=1800,
                           errors='replace')
        if not os.path.exists(out):
            print(tag, 'NO FRAME'); print(p.stdout[-1500:]); print(p.stderr[-800:]); continue
        a = np.asarray(Image.open(out).convert('RGB'), dtype=float)
        # The sea fills the lower half from this camera.
        sea = a[500:850, 200:1400]
        print('%-16s sea band: R %6.2f  G %6.2f  B %6.2f   (R,G carry the motion)'
              % (tag, sea[..., 0].mean(), sea[..., 1].mean(), sea[..., 2].mean()))
finally:
    shutil.copyfile(SRC, STAGED)
    print('restored and identical:',
          io.open(SRC, 'rb').read() == io.open(STAGED, 'rb').read())
