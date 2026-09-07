"""Per-arm temporal change under the dolly: the pole band, the back wall
between the poles, and the band's edge pixels only (gradient above a
threshold in the reference frame), band-following; plus after the stop."""
import numpy as np, sys, os
from PIL import Image
sys.path.insert(0, r'C:\Users\ism19\Code\RageV\tools\scripts\garage')
from burst_compare import band_of, band_x
S = r'C:\Users\ism19\Code\RageV\build\garage_burst' + '\\'
L = lambda p: (lambda a: 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2])(np.asarray(Image.open(S + p).convert('RGB'), dtype=float))

def wall(a, n):
    h, w = a.shape; x0 = int(w * (band_x(n) + 300) / 2000); y0 = int(h * 250 / 1230)
    return a[y0:y0 + int(h * 300 / 1230), x0:x0 + int(w * 200 / 2000)]

def metrics(tag, lo=60, hi=100):
    fr = {n: L('%s_%d.png' % (tag, n)) for n in range(lo, hi + 1)}
    shape = fr[lo].shape
    band, wallc, edge = [], [], []
    for n in range(lo, hi):
        a = fr[n][band_of(shape, n)]; b = fr[n+1][band_of(shape, n+1)]
        d = np.abs(b - a)
        band.append(d.mean())
        gx = np.abs(np.diff(a, axis=1)); g = np.zeros_like(a); g[:, 1:] = gx
        strong = g > 25.0
        edge.append(d[strong].mean() if strong.any() else 0.0)
        wallc.append(np.abs(wall(fr[n+1], n+1) - wall(fr[n], n)).mean())
    still = {n: L('%s_%d.png' % (tag, n)) for n in range(150, 171)}
    fl = (slice(int(shape[0]*880/1230), int(shape[0]*1180/1230)), slice(int(shape[1]*300/2000), int(shape[1]*1700/2000)))
    parked = np.mean([np.abs(still[n+1][fl] - still[n][fl]).mean() for n in range(150, 170)])
    return np.mean(band), np.mean(edge), np.mean(wallc), parked

print('%-28s %10s %10s %10s %12s' % ('arm', 'band', 'band edges', 'back wall', 'floor parked'))
for tag, name in (('new2_burst', 'previous build'), ('inline_burst', 'in-line (old, TAA)'), ('v3f_burst', 'shipped'),
                  ('cand_fewest4', 'mirrors 4 frames'), ('cand_slack3_dolly', 'slack x3'),
                  ('cand_rays4_dolly', 'rough rays 4'), ('cand_both_dolly', 'slack x3 + rays 4'), ('cand_alpharaw_dolly', 'weight unfiltered'), ('cand_alphahalf_dolly', 'weight half-filtered')):
    if os.path.exists(S + '%s_170.png' % tag):
        b, e, w, p = metrics(tag)
        print('%-28s %10.2f %10.2f %10.2f %12.2f' % (name, b, e, w, p))
