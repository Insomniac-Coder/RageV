"""Attribution of the back wall's motion noise: per-frame change in the pole
band / its edges / the wall between the poles under the dolly (frames 60-100),
and the same wall parked after the stop (frames 170-189) where rendered."""
import numpy as np, sys, os
from PIL import Image
sys.path.insert(0, r'C:\Users\ism19\Code\RageV\tools\scripts\garage')
from burst_compare import band_of, band_x
S = 'C:/Users/ism19/Code/RageV/build/garage_burst/'
L = lambda p: (lambda a: 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2])(np.asarray(Image.open(S + p).convert('RGB'), dtype=float))
def wall(a, n):
    h, w = a.shape; x0 = int(w * (band_x(n) + 300) / 2000); y0 = int(h * 250 / 1230)
    return a[y0:y0 + int(h * 300 / 1230), x0:x0 + int(w * 200 / 2000)]
def regs(a):
    h, w = a.shape
    return {'floor': a[int(h*880/1230):int(h*1180/1230), int(w*300/2000):int(w*1700/2000)],
            'wall': a[int(h*250/1230):int(h*550/1230), int(w*1020/2000):int(w*1220/2000)],
            'car': a[int(h*560/1230):int(h*760/1230), int(w*560/2000):int(w*900/2000)]}
def motion(tag, lo=60, hi=100):
    fr = {n: L('%s_%d.png' % (tag, n)) for n in range(lo, hi + 1)}
    shape = fr[lo].shape; band, wallc, edge = [], [], []
    for n in range(lo, hi):
        a = fr[n][band_of(shape, n)]; b = fr[n+1][band_of(shape, n+1)]; d = np.abs(b - a)
        band.append(d.mean()); g = np.zeros_like(a); g[:, 1:] = np.abs(np.diff(a, axis=1)); edge.append(d[g > 25].mean())
        wallc.append(np.abs(wall(fr[n+1], n+1) - wall(fr[n], n)).mean())
    return np.mean(band), np.mean(edge), np.mean(wallc)
def parked(tag, lo=170, hi=189):
    if not os.path.exists(S + '%s_%d.png' % (tag, hi)): return None
    fr = {n: regs(L('%s_%d.png' % (tag, n))) for n in range(lo, hi + 1)}
    return tuple(np.mean([np.abs(fr[n+1][k] - fr[n][k]).mean() for n in range(lo, hi)]) for k in ('floor', 'wall', 'car'))
print('%-22s %-34s %6s %6s %6s   %s' % ('arm', 'meaning', 'band', 'edges', 'wall', 'parked floor/wall/car (170-189)'))
for tag, name in (('new2_burst', 'yesterday, refl on, TAA'), ('v5_burst', 'v5, refl on, TAA'),
                  ('noaa_burst', 'refl on, no AA (older build)'), ('v3_noaa', 'refl on, no AA (v3)'),
                  ('attr_norefl', 'v5, refl OFF, new TAA'), ('attr_norefl_oldtaa', 'v5, refl OFF, committed TAA'),
                  ('attr_norefl_noaa', 'v5, refl OFF, no AA'), ('cand_noao', 'v5, --rt-ao=off (identical)')):
    if not os.path.exists(S + '%s_100.png' % tag): print('%-22s missing' % tag); continue
    b, e, w = motion(tag); p = parked(tag)
    print('%-22s %-34s %6.2f %6.2f %6.2f   %s' % (tag, name, b, e, w, '%.2f / %.2f / %.2f' % p if p else '-'))
