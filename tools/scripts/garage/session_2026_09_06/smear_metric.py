"""The smear metric: |moving frame - converged still at the same pose| per
region, against |parked frame - converged still at the end pose|. Also a
pose check (px shift on the car, must be 0) and the split into edge/flat
pixels of the reference (edge = gradient > 12): smear shows as flat-pixel
error next to edges; noise shows as flat-pixel error everywhere."""
import numpy as np, sys, os
from PIL import Image
S = 'C:/Users/ism19/Code/RageV/build/garage_burst/'
def Y(p):
    a = np.asarray(Image.open(S + p).convert('RGB'), dtype=float); return 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2]
def regs(a, n=None):
    h, w = a.shape
    r = {'floor': a[int(h*880/1230):int(h*1180/1230), int(w*300/2000):int(w*1700/2000)],
         'car': a[int(h*560/1230):int(h*760/1230), int(w*560/2000):int(w*900/2000)],
         'wall': a[int(h*250/1230):int(h*550/1230), int(w*1020/2000):int(w*1220/2000)]}
    if n is not None:
        x = 1150 - (1150 - 720) * (n - 30) / 90.0   # the poles' x over frames 30..120
        r['poles'] = a[int(h*250/1230):int(h*800/1230), int(w*(x-120)/2000):int(w*(x+120)/2000)]
    return r
def shift(a, b):
    best = None
    for s in range(-6, 7):
        d = np.abs(a[:, s:] - b[:, :a.shape[1]-s]).mean() if s >= 0 else np.abs(a[:, :s] - b[:, -s:]).mean()
        if best is None or d < best[1]: best = (s, d)
    return best[0]
def err(frame, ref, n=None):
    F, R = regs(Y(frame), n), regs(Y(ref), n); out = {}
    for k in F:
        d = np.abs(F[k] - R[k]); g = np.zeros_like(R[k]); g[:, 1:] = np.abs(np.diff(R[k], axis=1)); gy = np.zeros_like(R[k]); gy[1:, :] = np.abs(np.diff(R[k], axis=0))
        e = np.maximum(g, gy) > 12.0
        out[k] = (d.mean(), d[e].mean() if e.any() else 0.0, d[~e].mean())
    return out
arm = sys.argv[1] if len(sys.argv) > 1 else 'v5_burst'
print('arm', arm, '| pose check, car shift px: f70 %d, f100 %d, f180 %d' % (
    shift(regs(Y('ref70b.png'))['car'], regs(Y(arm + '_70.png'))['car']),
    shift(regs(Y('ref100b.png'))['car'], regs(Y(arm + '_100.png'))['car']),
    shift(regs(Y('endpose_ref.png'))['car'], regs(Y(arm + '_180.png'))['car'])))
print('%-26s %s' % ('|frame - converged|', '  '.join('%-20s' % k for k in ('floor', 'car', 'wall', 'poles'))))
print('%-26s %s' % ('', '  '.join('%-20s' % 'all / edge / flat' for _ in range(4))))
for label, frame, ref, n in (('moving f70', arm + '_70.png', 'ref70b.png', 70), ('moving f100', arm + '_100.png', 'ref100b.png', 100),
                             ('parked f125 (5 after)', arm + '_125.png', 'endpose_ref.png', 120), ('parked f140 (20 after)', arm + '_140.png', 'endpose_ref.png', 120),
                             ('parked f180 (60 after)', arm + '_180.png', 'endpose_ref.png', 120)):
    if not os.path.exists(S + frame): continue
    e = err(frame, ref, n)
    print('%-26s %s' % (label, '  '.join('%5.1f /%5.1f /%5.1f  ' % e[k] for k in ('floor', 'car', 'wall', 'poles'))))
print('--- parked edge shake: per-frame change on edge pixels (grad > 12) and flat pixels, frames 170-189, poles + car + floor regions')
for a in ('v5_burst', 'attr_norefl', 'new2_burst'):
    if not os.path.exists(S + a + '_189.png'): continue
    fr = [Y(a + '_%d.png' % n) for n in range(170, 190)]; E = {k: [] for k in ('floor', 'car', 'wall', 'poles')}; Fl = {k: [] for k in E}
    for i in range(19):
        A, B = regs(fr[i], 120), regs(fr[i+1], 120)
        for k in E:
            d = np.abs(B[k] - A[k]); g = np.zeros_like(A[k]); g[:, 1:] = np.abs(np.diff(A[k], axis=1)); gy = np.zeros_like(A[k]); gy[1:, :] = np.abs(np.diff(A[k], axis=0)); e = np.maximum(g, gy) > 12.0
            E[k].append(d[e].mean() if e.any() else 0.0); Fl[k].append(d[~e].mean())
    print('%-14s %s' % (a, '  '.join('%s edge %4.2f flat %4.2f' % (k, np.mean(E[k]), np.mean(Fl[k])) for k in E)))
