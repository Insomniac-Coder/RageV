"""Side-by-side crops for the owner: the versions in rows, the floor's tube reflections and the car in columns."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import os
import sys
from PIL import Image, ImageDraw, ImageFont

D = (_RAGEV + '/build/rt24/')
kind = sys.argv[1]            # 'moving' (mid-swing frame) or 'parked'
rows = [('Before fix 1 (halo)', 'lp_prefix1', 'sharp_prefix1'),
        ('Fix 1, today (grain)', 'lp_fix1', 'sharp_pre_a'),
        ('Pooled, 4 neighbours', 'lp_pool4', 'sharp_pool4'),
        ('Pooled, 8 neighbours', 'lp_pool8', 'sharp_pool8')]
crops = [(330, 630, 1060, 870), (640, 380, 960, 640)]   # tube reflections on the floor; the car and its reflection
font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 22)
pieces = []
for label, swing, parked in rows:
    path = D + (swing + '_moving.png' if kind == 'moving' else parked + '.png')
    if not os.path.exists(path):
        continue
    im = Image.open(path).convert('RGB')
    cells = [im.crop(c) for c in crops]
    h = max(c.height for c in cells)
    row = Image.new('RGB', (sum(c.width for c in cells) + 10 * (len(cells) - 1), h + 34), (18, 16, 17))
    x = 0
    for c in cells:
        row.paste(c, (x, 34))
        x += c.width + 10
    ImageDraw.Draw(row).text((8, 4), label, fill=(240, 235, 232), font=font)
    pieces.append(row)
out = Image.new('RGB', (max(p.width for p in pieces), sum(p.height for p in pieces) + 8 * (len(pieces) - 1)), (18, 16, 17))
y = 0
for p in pieces:
    out.paste(p, (0, y))
    y += p.height + 8
dst = D + 'lamp_pool_%s.png' % kind
out.save(dst)
print(dst, out.size)
