"""Parked edge shake: per-frame change on edge pixels (gradient > 12), flat
pixels, and bright edges (> 150), frames 150-169 of <arm>_still runs."""
import numpy as np, sys
from PIL import Image
S = 'C:/Users/ism19/Code/RageV/build/garage_burst/'
def Y(p):
    a = np.asarray(Image.open(S + p).convert('RGB'), dtype=float); return 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2]
def regs(a):
    h, w = a.shape
    return {'floor': a[int(h*880/1230):int(h*1180/1230), int(w*300/2000):int(w*1700/2000)], 'car': a[int(h*560/1230):int(h*760/1230), int(w*560/2000):int(w*900/2000)],
            'wall': a[int(h*250/1230):int(h*550/1230), int(w*1020/2000):int(w*1220/2000)], 'poles': a[int(h*250/1230):int(h*800/1230), int(w*1100/2000):int(w*1500/2000)],
            'tubes': a[int(h*0/1230):int(h*250/1230), int(w*300/2000):int(w*1700/2000)]}
print('parked: edge / flat / bright-edge per-frame change, frames 150-169')
for arm in sys.argv[1:]:
    fr = [regs(Y('%s_%d.png' % (arm, n))) for n in range(150, 170)]; out = []
    for k in ('floor', 'car', 'wall', 'poles', 'tubes'):
        e = []; f = []; b = []
        for i in range(19):
            A, B = fr[i][k], fr[i+1][k]; d = np.abs(B - A); g = np.zeros_like(A); g[:, 1:] = np.abs(np.diff(A, axis=1)); gy = np.zeros_like(A); gy[1:, :] = np.abs(np.diff(A, axis=0)); m = np.maximum(g, gy) > 12
            e.append(d[m].mean() if m.any() else 0); f.append(d[~m].mean()); bm = m & (A > 150); b.append(d[bm].mean() if bm.any() else 0)
        out.append('%s %4.2f/%4.2f/%4.2f' % (k, np.mean(e), np.mean(f), np.mean(b)))
    print('  %-16s %s' % (arm, '  '.join(out)))
