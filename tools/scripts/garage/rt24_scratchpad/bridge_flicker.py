import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
# Bridge flicker, the frame filter's check before and after 2026-09-23's change.
# before = the working-tree taa_resolve with the relaxed check everywhere again
# (the rule as it was at HEAD); after = the working tree as it is.
import glob, io, os, subprocess, sys
import numpy as np
from PIL import Image

ROOT = (_RAGEV + r'')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'scripts', 'garage', 'session_2026_09_13'))
import stage_run as sr  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bridge')
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
BEFORE = [('\t\t\t\t\t\t\t  * (outline && !covered ? 1.0 : 0.0);', ';'),
          ('if (u_Params.Material > 0.5 && !covered)', 'if (u_Params.Material > 0.5)')]


def stage(arm):
    assert sr.restore()
    if arm == 'before':
        src = os.path.join(sr.SRC_DIR, 'taa_resolve.rvshader')
        raw = io.open(src, encoding='utf-8', newline='').read()
        eol = '\r\n' if raw.count('\r\n') > raw.count('\n') // 2 else '\n'
        t = raw
        for a, b in BEFORE:
            a = a.replace('\n', eol)
            assert t.count(a) == 1, a
            t = t.replace(a, b)
        io.open(os.path.join(sr.STAGED_DIR, 'taa_resolve.rvshader'), 'w', encoding='utf-8', newline='').write(t)


def capture(arm):
    d = os.path.join(OUT, arm)
    os.makedirs(d, exist_ok=True)
    for f in glob.glob(os.path.join(d, '*.png')):
        os.remove(f)
    stage(arm)
    p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'), '--project=' + sr.PROJECT,
                        '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan', '--render-defaults=off',
                        '--width=1600', '--height=900', '--frame-time=0.000001', '--vsync=off',
                        '--screenshot-frame=240', '--screenshot-count=16',
                        '--screenshot=' + os.path.join(d, 'f.png'), '--benchmark=260'],
                       cwd=RT, capture_output=True, text=True, errors='replace', timeout=1800)
    bad = [l for l in (p.stdout + p.stderr).splitlines() if 'failed to compile' in l or 'did not compile' in l]
    return sorted(glob.glob(os.path.join(d, '*.png'))), bad


def blinks(files, threshold=6.0):
    frames = np.stack([np.asarray(Image.open(f).convert('L'), dtype=np.float32) for f in files])
    swing = frames.max(0) - frames.min(0)
    d = np.diff(frames, axis=0)
    s = np.sign(d)
    s[np.abs(d) < 1.0] = 0
    rev = np.zeros(frames.shape[1:], dtype=np.int32)
    last = np.zeros(frames.shape[1:], dtype=np.float32)
    for k in range(s.shape[0]):
        nz = s[k] != 0
        rev += (nz & (last != 0) & (s[k] != last)).astype(np.int32)
        last = np.where(nz, s[k], last)
    return (swing > threshold) & (rev >= 2)


try:
    res = {}
    for arm in ('before', 'after'):
        files, bad = capture(arm)
        print(arm, len(files), 'frames', 'COMPILE FAILURES: %s' % bad if bad else '')
        res[arm] = blinks(files)
    for arm, m in res.items():
        print('%-7s blinking %.3f%% of the frame' % (arm, 100.0 * m.mean()))
    # Where they differ: red blinks only before, green only after, white both.
    b, a = res['before'], res['after']
    base = np.asarray(Image.open(sorted(glob.glob(os.path.join(OUT, 'after', '*.png')))[0]).convert('RGB'),
                      dtype=np.float32) * 0.35
    base[b & ~a] = [255, 0, 0]
    base[a & ~b] = [0, 255, 0]
    base[a & b] = [255, 255, 255]
    Image.fromarray(base.astype(np.uint8)).save(os.path.join(OUT, 'blink_diff.png'))
    print('map:', os.path.join(OUT, 'blink_diff.png'), 'only-before', int((b & ~a).sum()),
          'only-after', int((a & ~b).sum()), 'both', int((a & b).sum()))
finally:
    print('restored', sr.restore())
