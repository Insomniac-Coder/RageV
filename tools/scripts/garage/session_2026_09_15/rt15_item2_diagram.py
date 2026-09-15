# -*- coding: utf-8 -*-
"""RT-15 item 2 as a drawing: what it does, and its catch on a shiny floor (owner, 2026-09-15)."""
import os
from PIL import Image, ImageDraw, ImageFont

OUT = r'C:\Users\ism19\Code\RageV\build\rt15\item2_diagram.png'
BOLD = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 32)
TEXT = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 25)
BIG = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 40)

PW, PH = 480, 520
W = PW * 4 + 50
H = 90 + PH + 60 + 330
img = Image.new('RGB', (W, H), (28, 28, 32))
d = ImageDraw.Draw(img, 'RGBA')
d.text((20, 20), 'What item 2 does', font=BIG, fill=(255, 255, 255))

FLOOR = 250
CAR = (240, 200, 40)


def panel_box(i, y0=90, h=PH):
    x0 = 10 + i * (PW + 10)
    d.rectangle((x0, y0, x0 + PW, y0 + h), fill=(44, 44, 50))
    return x0, y0


def floor(x0, y0):
    d.rectangle((x0 + 10, y0 + FLOOR, x0 + PW - 10, y0 + FLOOR + 130), fill=(62, 70, 82))
    d.line((x0 + 10, y0 + FLOOR, x0 + PW - 10, y0 + FLOOR), fill=(150, 160, 175), width=3)


def car(x0, y0, cx, alpha=255, outline=False):
    box = (x0 + cx, y0 + FLOOR - 70, x0 + cx + 130, y0 + FLOOR)
    if outline:
        d.rounded_rectangle(box, radius=14, outline=(200, 200, 200, 160), width=3)
    else:
        d.rounded_rectangle(box, radius=14, fill=CAR + (alpha,))


def reflection(x0, y0, cx, alpha):
    d.rounded_rectangle((x0 + cx, y0 + FLOOR + 4, x0 + cx + 130, y0 + FLOOR + 74), radius=14,
                        fill=(CAR[0], CAR[1], CAR[2], alpha))


def arrow(xa, xb, y, colour=(255, 255, 255), width=5):
    d.line((xa, y, xb, y), fill=colour, width=width)
    d.polygon([(xb, y), (xb - 18, y - 11), (xb - 18, y + 11)], fill=colour)


def words(x0, y0, lines, top=15, font=TEXT, colour=(230, 230, 230)):
    for k, line in enumerate(lines):
        d.text((x0 + 14, y0 + top + k * 32), line, font=font, fill=colour)


# 1. last frame
x0, y0 = panel_box(0)
d.text((x0 + 14, y0 + 10), '1. Last frame', font=BOLD, fill=(255, 255, 255))
floor(x0, y0)
car(x0, y0, 60)
reflection(x0, y0, 60, 150)
words(x0, y0, ['A car above a shiny floor,', 'and its reflection in the floor.'], top=FLOOR + 150)

# 2. this frame
x0, y0 = panel_box(1)
d.text((x0 + 14, y0 + 10), '2. This frame', font=BOLD, fill=(255, 255, 255))
floor(x0, y0)
car(x0, y0, 60, outline=True)
car(x0, y0, 280)
reflection(x0, y0, 280, 150)
arrow(x0 + 200, x0 + 270, y0 + FLOOR - 95)
words(x0, y0, ['The car moved right.', 'Its reflection moved with it.'], top=FLOOR + 150)

# 3. off
x0, y0 = panel_box(2)
d.text((x0 + 14, y0 + 10), '3. Item 2 OFF', font=BOLD, fill=(255, 150, 150))
floor(x0, y0)
car(x0, y0, 280)
reflection(x0, y0, 280, 120)
reflection(x0, y0, 60, 60)
d.text((x0 + 78, y0 + FLOOR + 22), 'ghost', font=BOLD, fill=(255, 150, 150))
words(x0, y0, ['Reflections are smoothed by mixing', 'in older frames, from the SAME spot.', 'The old reflection lingers as a ghost.'],
      top=FLOOR + 150)

# 4. on
x0, y0 = panel_box(3)
d.text((x0 + 14, y0 + 10), '4. Item 2 ON', font=BOLD, fill=(150, 255, 150))
floor(x0, y0)
car(x0, y0, 280)
reflection(x0, y0, 280, 150)
arrow(x0 + 120, x0 + 270, y0 + FLOOR + 40, colour=(150, 255, 150))
words(x0, y0, ['The older frames are taken from', 'where the car WAS, and moved', 'along with it. No ghost.'], top=FLOOR + 150)

# 5. the catch
y1 = 90 + PH + 70
d.text((20, y1 - 48), 'The catch: a shiny floor also reflects things that did NOT move', font=BOLD, fill=(255, 215, 130))
half = (W - 30) // 2
for j, (title, stretched) in enumerate((('Correct: the ceiling lights\u2019 reflections stay round', False),
                                          ('Item 2 ON: they get dragged along with the car, and smear', True))):
    bx = 10 + j * (half + 10)
    d.rectangle((bx, y1, bx + half, y1 + 320), fill=(44, 44, 50))
    d.text((bx + 14, y1 + 10), title, font=BOLD if j == 0 else BOLD, fill=(150, 255, 150) if j == 0 else (255, 150, 150))
    d.rectangle((bx + 20, y1 + 70, bx + half - 20, y1 + 300), fill=(62, 70, 82))
    for k in range(5):
        cx = bx + 70 + k * ((half - 140) // 4)
        for r in range(2):
            cy = y1 + 112 + r * 150
            if stretched and 1 <= k <= 3:
                d.ellipse((cx - 70, cy - 12, cx + 70, cy + 12), fill=(235, 245, 255, 150))
                d.ellipse((cx - 30, cy - 18, cx + 30, cy + 18), fill=(235, 245, 255, 230))
            else:
                d.ellipse((cx - 32, cy - 20, cx + 32, cy + 20), fill=(235, 245, 255, 235))
    # the moving car's reflection among them
    mx = bx + half // 2
    d.rounded_rectangle((mx - 60, y1 + 155, mx + 60, y1 + 215), radius=12, fill=(CAR[0], CAR[1], CAR[2], 170))
    if stretched:
        arrow(mx - 150, mx + 150, y1 + 285, colour=(255, 150, 150), width=4)
img.save(OUT)
print(OUT, img.size)
