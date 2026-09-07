"""Parked per-frame change (frames 170-189), floor drift over 1/4/8/16
frames, and for converged stills the floor's high-frequency energy.
Usage: parked_stats.py <arm>... ; stills: parked_stats.py --hf <still.png>..."""
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
    return {'floor': a[int(h*880/1230):int(h*1180/1230), int(w*300/2000):int(w*1700/2000)], 'car': a[int(h*560/1230):int(h*760/1230), int(w*560/2000):int(w*900/2000)],
            'wall': a[int(h*250/1230):int(h*550/1230), int(w*1020/2000):int(w*1220/2000)], 'poles': a[int(h*250/1230):int(h*800/1230), int(w*600/2000):int(w*840/2000)]}
args = sys.argv[1:]
if args and args[0] == '--hf':
    for p in args[1:]:
        fl = regs(Y(p))['floor']; print('  %-30s floor HF %5.2f  mean %5.1f' % (p, (fl - box(fl)).std(), fl.mean()))
else:
    print('parked per-frame change 170-189 (floor car wall poles) | floor drift over 1/4/8/16 frames')
    for arm in args:
        fr = {n: regs(Y('%s_%d.png' % (arm, n))) for n in range(170, 190)}
        pf = ' '.join('%s %4.2f' % (k, np.mean([np.abs(fr[n+1][k] - fr[n][k]).mean() for n in range(170, 189)])) for k in ('floor', 'car', 'wall', 'poles'))
        drift = ' '.join('%4.2f' % np.mean([np.abs(fr[n+g]['floor'] - fr[n]['floor']).mean() for n in range(170, 190 - g)]) for g in (1, 4, 8, 16))
        print('  %-9s %s | %s' % (arm, pf, drift))
