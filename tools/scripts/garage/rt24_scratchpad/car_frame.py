"""Framing check for a closer car drive: the first and the last frame at a camera, fixed clock."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io, os, subprocess, sys
sys.path.insert(0, (_RAGEV + '/tools/scripts/garage'))
import watch_arm as w
from PIL import Image
sr = w.sr
cam, seconds = sys.argv[1], float(sys.argv[2])
io.open(os.path.join(sr.SCENES, sr.HEAD_SCENE), 'wb').write(w.wm.still_camera(w.scene_for('car', seconds)))
w.burst.sync_bake('showroom_head')
exe_dir = os.path.join(sr.ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
out = (_RAGEV + '/build/rt24/car_frame')
os.makedirs(out, exist_ok=True)
last = int(seconds * 60) + 5
try:
    sr.restore()
    for n in (2, last):
        subprocess.run([os.path.join(exe_dir, 'RageVRuntime.exe'), '--project=' + sr.PROJECT,
                        '--scene=scenes/' + sr.HEAD_SCENE, '--rhi=vulkan', '--render-defaults=off',
                        '--import-cache=off', '--vsync=off', '--width=1600', '--height=900', '--camera=' + cam,
                        '--frame-time=0.0166667', '--screenshot-frame=%d' % n,
                        '--screenshot=' + os.path.join(out, 'f%d.png' % n), '--benchmark=%d' % (n + 2)],
                       cwd=exe_dir, capture_output=True, timeout=900)
finally:
    sr.restore()
    for f in (sr.HEAD_SCENE, sr.HEAD_SCENE + '.meta'):
        try: os.remove(os.path.join(sr.SCENES, f))
        except OSError: pass
a, b = Image.open(os.path.join(out, 'f2.png')), Image.open(os.path.join(out, 'f%d.png' % last))
s = Image.new('RGB', (800, 900)); s.paste(a.resize((800, 450)), (0, 0)); s.paste(b.resize((800, 450)), (0, 450))
s.save(os.path.join(out, 'both.png'))
