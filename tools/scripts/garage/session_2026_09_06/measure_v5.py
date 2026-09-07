"""Parked drift over 1/4/8/16 frames, motion change, and the settle, for the
arms named on the command line (each needs <tag>_still_150..169 and
<tag>_burst_30..189 frames; new2_burst stands for both of its own)."""
import numpy as np, os, sys, subprocess
sys.path.insert(0, r'C:\Users\ism19\Code\RageV\tools\scripts\garage')
from burst_compare import band_of, band_x
S = r'C:\Users\ism19\Code\RageV\build\garage_burst' + os.sep
def L(p):
    a = np.asarray(Image.open(S + p).convert('RGB'), dtype=float); return 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2]
from PIL import Image
def regs(a):
    h, w = a.shape
    return {'floor': a[int(h*880/1230):int(h*1180/1230), int(w*300/2000):int(w*1700/2000)],
            'wall': a[int(h*250/1230):int(h*550/1230), int(w*1020/2000):int(w*1220/2000)],
            'car': a[int(h*560/1230):int(h*760/1230), int(w*560/2000):int(w*900/2000)]}
def wall(a, n):
    h, w = a.shape; x0 = int(w * (band_x(n) + 300) / 2000); y0 = int(h * 250 / 1230)
    return a[y0:y0 + int(h * 300 / 1230), x0:x0 + int(w * 200 / 2000)]

arms = sys.argv[1:] or ['new2', 'v4', 'v5']
print('parked drift over 1 / 4 / 8 / 16 frames (floor | wall | car):')
for arm in arms:
    still = 'new2_burst' if arm == 'new2' else arm + '_still'
    if not os.path.exists(S + '%s_169.png' % still): print('  %-6s missing' % arm); continue
    fr = {n: regs(L('%s_%d.png' % (still, n))) for n in range(150, 170)}
    row = []
    for k in ('floor', 'wall', 'car'):
        vals = [np.mean([np.abs(fr[n+g][k] - fr[n][k]).mean() for n in range(150, 170 - g)]) for g in (1, 4, 8, 16)]
        row.append('%5.2f %5.2f %5.2f %5.2f' % tuple(vals))
    print('  %-6s %s' % (arm, ' | '.join(row)))
print('moving, dolly frames 60-100: band / band edges / back wall per-frame change; floor after the stop vs the converged end still at 1/4/8/12/20/30 frames:')
ref = regs(L('endpose_ref.png'))['floor']
for arm in arms:
    tag = 'new2_burst' if arm == 'new2' else arm + '_burst'
    if not os.path.exists(S + '%s_189.png' % tag): print('  %-6s missing' % arm); continue
    fr = {n: L('%s_%d.png' % (tag, n)) for n in list(range(60, 101)) + [121, 124, 128, 132, 140, 150]}
    shape = fr[60].shape; band = []; edges = []; wl = []
    for n in range(60, 100):
        a = fr[n][band_of(shape, n)]; b = fr[n+1][band_of(shape, n+1)]; d = np.abs(b - a)
        band.append(d.mean()); g = np.zeros_like(a); g[:, 1:] = np.abs(np.diff(a, axis=1)); edges.append(d[g > 25].mean())
        wl.append(np.abs(wall(fr[n+1], n+1) - wall(fr[n], n)).mean())
    settle = ' '.join('%.1f' % np.abs(regs(fr[n])['floor'] - ref).mean() for n in (121, 124, 128, 132, 140, 150))
    print('  %-6s %5.2f / %5.2f / %5.2f   settle %s' % (arm, np.mean(band), np.mean(edges), np.mean(wl), settle))
