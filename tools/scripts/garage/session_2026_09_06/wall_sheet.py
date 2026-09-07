"""Where the back wall's frame-to-frame change lives: for each arm, the wall
window at frame 80, frame 81, and |81-80| stretched x8; plus the change split
into edge pixels (gradient > 12 in frame 80) and flat pixels; plus a dolly
check (column of peak luminance in the pole band at frames 60 and 100)."""
import numpy as np, sys, os
from PIL import Image
sys.path.insert(0, 'C:/Users/ism19/Code/RageV/tools/scripts/garage')
from burst_compare import band_of, band_x
S = 'C:/Users/ism19/Code/RageV/build/garage_burst/'
def RGB(p): return np.asarray(Image.open(S + p).convert('RGB'), dtype=float)
def Y(a): return 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2]
def wallbox(shape, n):
    h, w = shape[:2]; x0 = int(w * (band_x(n) + 300) / 2000); y0 = int(h * 250 / 1230)
    return (slice(y0, y0 + int(h * 300 / 1230)), slice(x0, x0 + int(w * 200 / 2000)))
arms = [a for a in ('new2_burst', 'v5_burst', 'attr_norefl', 'attr_norefl_noaa') if os.path.exists(S + a + '_81.png')]
rows = []
print('%-18s %8s %8s %8s   %s' % ('arm', 'edge chg', 'flat chg', 'flat px%', 'pole peak col f60 / f100'))
for a in arms:
    f0, f1 = RGB(a + '_80.png'), RGB(a + '_81.png')
    w0, w1 = f0[wallbox(f0.shape, 80)], f1[wallbox(f1.shape, 81)]
    y0, y1 = Y(w0), Y(w1); d = np.abs(y1 - y0)
    gx = np.zeros_like(y0); gx[:, 1:] = np.abs(np.diff(y0, axis=1)); gy = np.zeros_like(y0); gy[1:, :] = np.abs(np.diff(y0, axis=0))
    edge = np.maximum(gx, gy) > 12.0
    b60 = Y(RGB(a + '_60.png')); b100 = Y(RGB(a + '_100.png'))
    c60 = int(np.argmax(b60[band_of(b60.shape, 60)].mean(axis=0))) ; c100 = int(np.argmax(b100[band_of(b100.shape, 100)].mean(axis=0)))
    print('%-18s %8.2f %8.2f %8.1f   %d / %d' % (a, d[edge].mean(), d[~edge].mean(), 100.0 * (~edge).mean(), c60, c100))
    dd = np.clip(np.repeat((d * 8.0)[..., None], 3, axis=2), 0, 255)
    rows.append(np.concatenate([np.clip(w0, 0, 255), np.clip(w1, 0, 255), dd], axis=1))
sheet = np.concatenate(rows, axis=0).astype(np.uint8)
Image.fromarray(sheet).save(S + 'wall_attr_sheet.png'); print('sheet', sheet.shape)
