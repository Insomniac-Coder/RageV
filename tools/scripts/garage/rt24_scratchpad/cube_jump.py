"""Does the cube jump at start-up? Frame 3 of the live cube drive on the real clock vs a fixed 1/60 clock."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io, os, subprocess, sys
G = (_RAGEV + '/tools/scripts/garage')
sys.path.insert(0, G)
import watch_arm as w
sr, em, burst = w.sr, w.em, w.burst
out = (_RAGEV + '/build/rt24/cube_jump')
os.makedirs(out, exist_ok=True)
io.open(os.path.join(sr.SCENES, sr.HEAD_SCENE), 'wb').write(w.wm.still_camera(em.scene(8)))
burst.sync_bake('showroom_head')
exe_dir = os.path.join(sr.ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
try:
    sr.restore()
    for tag, clock in (('real', []), ('fixed', ['--frame-time=0.0166667'])):
        p = subprocess.run([os.path.join(exe_dir, 'RageVRuntime.exe'), '--project=' + sr.PROJECT,
                            '--scene=scenes/' + sr.HEAD_SCENE, '--rhi=vulkan', '--render-defaults=off',
                            '--import-cache=off', '--vsync=on', '--width=1600', '--height=900',
                            '--camera=-4.3,1.0,-2,3.2,25,8', '--screenshot-frame=3',
                            '--screenshot=' + os.path.join(out, tag + '.png'), '--benchmark=6'] + clock,
                           cwd=exe_dir, capture_output=True, text=True, errors='replace', timeout=900)
        print(tag, 'exit', p.returncode)
finally:
    sr.restore()
    for n in (sr.HEAD_SCENE, sr.HEAD_SCENE + '.meta'):
        try: os.remove(os.path.join(sr.SCENES, n))
        except OSError: pass
