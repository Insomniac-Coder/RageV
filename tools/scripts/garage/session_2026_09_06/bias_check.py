"""Signed low-frequency difference of a parked frame or a converged still
against a reference still (9x9 box), per region, plus parked per-frame
change and drift. Usage: bias_check.py <frame.png> [<ref.png>] (defaults
to endpose_ref.png = v5 converged)."""
import numpy as np, sys
from PIL import Image
S = 'C:/Users/ism19/Code/RageV/build/garage_burst/'
def Y(p):
    a = np.asarray(Image.open(S + p).convert('RGB'), dtype=float); return 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2]
def box(a, r=4):
    k = 2*r+1; p = np.pad(a, r, mode='edge'); c = np.zeros((p.shape[0]+1, p.shape[1]+1)); c[1:, 1:] = p.cumsum(0).cumsum(1)
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / float(k*k)
def regs(a):
    h, w = a.shape
    return {'floor': a[int(h*880/1230):int(h*1180/1230), int(w*300/2000):int(w*1700/2000)],
            'car': a[int(h*560/1230):int(h*760/1230), int(w*560/2000):int(w*900/2000)],
            'wall': a[int(h*250/1230):int(h*550/1230), int(w*1020/2000):int(w*1220/2000)],
            'poles': a[int(h*250/1230):int(h*800/1230), int(w*600/2000):int(w*840/2000)]}
frames = sys.argv[1:] or ['v5_burst_180.png', 'v6_burst_180.png']
refname = 'endpose_ref.png'
if len(frames) > 1 and frames[-1].startswith('ref='): refname = frames.pop()[4:]
ref = regs(Y(refname))
print('signed low-frequency difference (frame - %s): mean / mean|.| / where ref>100 / where ref<=100' % refname)
for f in frames:
    F = regs(Y(f)); row = []
    for k in ('floor', 'car', 'wall', 'poles'):
        d = box(F[k]) - box(ref[k]); br = ref[k] > 100
        row.append('%s %+5.1f / %4.1f / %+5.1f / %+5.1f' % (k, d.mean(), np.abs(d).mean(), d[br].mean() if br.any() else 0, d[~br].mean()))
    print('  %-22s %s' % (f, ' | '.join(row)))
