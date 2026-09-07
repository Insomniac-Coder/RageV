"""Per-pixel comparison of two stills: mean and 99th-percentile absolute
difference in 8-bit levels, the share of pixels off by more than two levels,
the same over named regions, and a signed diff image (x8 by default; green =
the second image brighter, red = darker).
Usage: python diff_still.py a.png b.png out.png [gain] [name=y0,y1,x0,x1 ...]"""
import sys
import numpy as np
from PIL import Image

a = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.float32)
b = np.asarray(Image.open(sys.argv[2]).convert('RGB')).astype(np.float32)
assert a.shape == b.shape, (a.shape, b.shape)
gain = float(sys.argv[4]) if len(sys.argv) > 4 and '=' not in sys.argv[4] else 8.0
regions = [arg for arg in sys.argv[4:] if '=' in arg]

d = b - a
mag = np.abs(d).mean(axis=2)
print(f"{sys.argv[1]} -> {sys.argv[2]}: mean |d| {mag.mean():.3f} levels, p99 {np.percentile(mag, 99):.2f}, "
      f"max {mag.max():.1f}, {100.0 * (mag > 2.0).mean():.2f}% of pixels off by > 2")
for r in regions:
    name, box = r.split('=')
    y0, y1, x0, x1 = [int(v) for v in box.split(',')]
    m = mag[y0:y1, x0:x1]
    print(f"   {name:10s} mean {m.mean():.3f}  p99 {np.percentile(m, 99):.2f}  >2: {100.0 * (m > 2.0).mean():.2f}%")

lum = d.mean(axis=2) * gain
out = np.zeros_like(a)
out[..., 1] = np.clip(lum, 0, 255)
out[..., 0] = np.clip(-lum, 0, 255)
Image.fromarray(out.astype(np.uint8)).save(sys.argv[3])
print('wrote', sys.argv[3])
