"""Horizontal shift (px, argmin of mean |diff| over -14..14) between the
Slider-stop references and the burst frames, and the burst's own per-frame
shift, per region -- to turn the 6 px pose error into a tick count."""
import numpy as np
from PIL import Image
S = 'C:/Users/ism19/Code/RageV/build/garage_burst/'
def Y(p):
    a = np.asarray(Image.open(S + p).convert('RGB'), dtype=float); return 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2]
def regs(a):
    h, w = a.shape
    return {'floor': a[int(h*880/1230):int(h*1180/1230), int(w*300/2000):int(w*1700/2000)],
            'car': a[int(h*560/1230):int(h*760/1230), int(w*560/2000):int(w*900/2000)],
            'poles': a[int(h*250/1230):int(h*800/1230), int(w*1100/2000):int(w*1500/2000)]}
def shift(a, b):
    best = None
    for s in range(-14, 15):
        if s >= 0: d = np.abs(a[:, s:] - b[:, :a.shape[1]-s]).mean()
        else: d = np.abs(a[:, :s] - b[:, -s:]).mean()
        if best is None or d < best[1]: best = (s, d)
    return best
for ref, n in (('ref70x.png', 70), ('ref100x.png', 100)):
    R = regs(Y(ref)); B = regs(Y('v5_burst_%d.png' % n)); B1 = regs(Y('v5_burst_%d.png' % (n + 1))); Bm = regs(Y('v5_burst_%d.png' % (n - 1)))
    for k in ('floor', 'car', 'poles'):
        print('frame %d %-6s ref-vs-burst shift %3d px (resid %.2f) | burst n->n+1 %3d px | n-1->n %3d px' % (n, k, *shift(R[k], B[k]), shift(B[k], B1[k])[0], shift(Bm[k], B[k])[0]))
