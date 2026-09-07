"""Is the moving-frame error smear (low frequency) or noise (high frequency)?
Box-blur 9x9 both the frame and the converged reference; the error of the
blurred pair is the low-frequency part, the rest is grain. Floor sheet too."""
import numpy as np, sys, os
from PIL import Image, ImageFilter
S = 'C:/Users/ism19/Code/RageV/build/garage_burst/'
def Y(p):
    a = np.asarray(Image.open(S + p).convert('RGB'), dtype=float); return 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2]
def box(a, r=4):
    k = 2 * r + 1; c = np.cumsum(np.cumsum(np.pad(a, r + 1, mode='edge'), axis=0), axis=1)
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / float(k * k)
def regs(a):
    h, w = a.shape
    return {'floor': a[int(h*880/1230):int(h*1180/1230), int(w*300/2000):int(w*1700/2000)],
            'car': a[int(h*560/1230):int(h*760/1230), int(w*560/2000):int(w*900/2000)],
            'wall': a[int(h*250/1230):int(h*550/1230), int(w*1020/2000):int(w*1220/2000)]}
arm = sys.argv[1] if len(sys.argv) > 1 else 'v5_burst'
print('%-24s %s' % ('|frame - converged|', '  '.join('%-22s' % (k + ' total/low/high') for k in ('floor', 'car', 'wall'))))
for label, frame, ref in (('moving f70', arm + '_70.png', 'ref70b.png'), ('moving f100', arm + '_100.png', 'ref100b.png'),
                          ('parked f125', arm + '_125.png', 'endpose_ref.png'), ('parked f180', arm + '_180.png', 'endpose_ref.png')):
    F, R = Y(frame), Y(ref); row = []
    for k in ('floor', 'car', 'wall'):
        f, r = regs(F)[k], regs(R)[k]; tot = np.abs(f - r).mean(); low = np.abs(box(f) - box(r)).mean()
        row.append('%5.1f /%5.1f /%5.1f      ' % (tot, low, max(tot - low, 0.0)))
    print('%-24s %s' % (label, '  '.join(row)))
# floor sheet: frame 70, ref70b, |diff| x4, then the parked pair
def crop(p):
    a = np.asarray(Image.open(S + p).convert('RGB'), dtype=float); h, w = a.shape[:2]
    return a[int(h*880/1230):int(h*1180/1230), int(w*700/2000):int(w*1500/2000)]
rows = []
for f, r in ((arm + '_70.png', 'ref70b.png'), (arm + '_180.png', 'endpose_ref.png')):
    A, B = crop(f), crop(r); D = np.clip(np.abs(A - B) * 4.0, 0, 255)
    rows.append(np.concatenate([A, B, D], axis=1))
sheet = np.concatenate(rows, axis=0).astype(np.uint8)
Image.fromarray(sheet).resize((sheet.shape[1] // 2, sheet.shape[0] // 2)).save(S + 'floor_smear_sheet.png'); print('sheet saved')
