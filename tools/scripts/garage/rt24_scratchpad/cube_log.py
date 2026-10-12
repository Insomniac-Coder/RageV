"""The cube drive once with the engine's log kept: shader failures, and the change passes running."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io, os, subprocess, sys
sys.path.insert(0, (_RAGEV + '/tools/scripts/garage'))
import watch_arm as w
sr, em, burst = w.sr, w.em, w.burst
io.open(os.path.join(sr.SCENES, sr.HEAD_SCENE), 'wb').write(w.wm.still_camera(em.scene(8)))
burst.sync_bake('showroom_head')
exe_dir = os.path.join(sr.ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
try:
    sr.restore()
    p = subprocess.run([os.path.join(exe_dir, 'RageVRuntime.exe'), '--project=' + sr.PROJECT,
                        '--scene=scenes/' + sr.HEAD_SCENE, '--rhi=vulkan', '--render-defaults=off',
                        '--import-cache=off', '--vsync=off', '--width=1600', '--height=900',
                        '--camera=-4.3,1.0,-2,3.2,25,8', '--frame-time=0.0166667', '--benchmark=120',
                        '--pass-timings=on'], cwd=exe_dir, capture_output=True, text=True, errors='replace', timeout=900)
    log = p.stdout + p.stderr
    print('exit', p.returncode, '| shader failures:', sum(1 for l in log.splitlines() if 'compilation failed' in l
          or 'did not compile' in l or 'no compute pipeline' in l), '| Vulkan messages:', log.count('[Vulkan]'))
    print('\n'.join(l.split('[benchmark]')[-1] for l in log.splitlines()
                    if any(k in l for k in ('ReflectionRecord', 'ReflectionRelight', 'scene/ReflectionTrace ',
                                            'ReflectionChangeMap', 'frame  mean')))[:1500])
finally:
    sr.restore()
    for n in (sr.HEAD_SCENE, sr.HEAD_SCENE + '.meta'):
        try: os.remove(os.path.join(sr.SCENES, n))
        except OSError: pass
