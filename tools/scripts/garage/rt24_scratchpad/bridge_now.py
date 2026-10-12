"""The bridge, parked at its authored camera, as the working tree ships: glint flicker, full frame and cable band."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import glob, os, shutil, subprocess, sys
sys.path.insert(0, (_RAGEV + '/tools/scripts/garage'))
import spin_measure as sm
sr = sm.sr
CHECK = (_RAGEV + '/tools/scripts/check_glint_flicker.py')
shots = os.path.join(sm.OUT, 'bridge_now_frames')
shutil.rmtree(shots, ignore_errors=True); os.makedirs(shots)
try:
    sr.restore()
    subprocess.run([os.path.join(sm.RT, 'RageVRuntime.exe'), '--project=' + sr.PROJECT,
                    '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan', '--render-defaults=off',
                    '--width=1600', '--height=900', '--frame-time=0.000001', '--vsync=off',
                    '--screenshot-frame=240', '--screenshot-count=16',
                    '--screenshot=' + os.path.join(shots, 'f.png'), '--benchmark=258'],
                   cwd=sm.RT, capture_output=True, text=True, errors='replace', timeout=1800)
finally:
    sr.restore()
files = sorted(glob.glob(os.path.join(shots, '*.png')))
for region in (None, '0,250,1600,420'):
    out = subprocess.run([sys.executable, CHECK] + (['--region', region] if region else []) + files,
                         capture_output=True, text=True).stdout
    print('%-10s %s' % ('cable band' if region else 'full frame', out.splitlines()[1].strip() if len(out.splitlines()) > 1 else out))
shutil.rmtree(shots, ignore_errors=True)
