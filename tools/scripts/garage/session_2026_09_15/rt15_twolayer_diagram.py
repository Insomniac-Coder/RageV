# -*- coding: utf-8 -*-
"""RT-15 item 2: how the two-layer version works, as a drawing for the owner."""
from PIL import Image, ImageDraw, ImageFont

OUT = r'C:\Users\ism19\Code\RageV\build\rt15\item2_twolayer_diagram.png'
BIG = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 40)
BOLD = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 30)
TEXT = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 25)
W, H = 1970, 640
img = Image.new('RGB', (W, H), (28, 28, 32))
d = ImageDraw.Draw(img, 'RGBA')
d.text((20, 18), 'How the two-layer version works', font=BIG, fill=(255, 255, 255))
CAR, LIGHT, FLOOR = (240, 200, 40), (235, 245, 255), (62, 70, 82)
PW = 630


def panel(i, title, colour=(255, 255, 255)):
    x0 = 10 + i * (PW + 20)
    d.rectangle((x0, 80, x0 + PW, H - 10), fill=(44, 44, 50))
    d.text((x0 + 14, 92), title, font=BOLD, fill=colour)
    return x0


def floor_patch(x, y, w, h, lights=True, car=True, car_dx=0):
    d.rectangle((x, y, x + w, y + h), fill=FLOOR, outline=(150, 160, 175), width=2)
    if lights:
        for k in range(3):
            for r in range(2):
                cx, cy = x + 45 + k * (w - 90) // 2, y + 35 + r * (h - 70)
                d.ellipse((cx - 26, cy - 15, cx + 26, cy + 15), fill=LIGHT + (235,))
    if car:
        d.rounded_rectangle((x + w // 2 - 45 + car_dx, y + h // 2 - 22, x + w // 2 + 45 + car_dx, y + h // 2 + 22),
                            radius=10, fill=CAR + (230,))


def arrow(xa, xb, y, colour, width=5):
    d.line((xa, y, xb, y), fill=colour, width=width)
    d.polygon([(xb, y), (xb - 16, y - 10), (xb - 16, y + 10)], fill=colour)


def words(x, y, lines, colour=(230, 230, 230)):
    for k, line in enumerate(lines):
        d.text((x, y + k * 32), line, font=TEXT, fill=colour)


# 1. the problem
x = panel(0, '1. One spot on the floor shows two things')
floor_patch(x + 115, 150, 400, 230)
words(x + 14, 420, ['The ceiling lights\u2019 reflection (white),', 'which stays still, and the moving',
                    'car\u2019s reflection (yellow). The old', 'version stored both as ONE colour.'])

# 2. two layers
x = panel(1, '2. Now they are kept apart', (120, 200, 255))
d.text((x + 20, 140), 'Still layer', font=BOLD, fill=(255, 255, 255))
floor_patch(x + 20, 180, 260, 150, lights=True, car=False)
words(x + 20, 340, ['Stays where it is.'])
d.text((x + 340, 140), 'Moving layer', font=BOLD, fill=(255, 255, 255))
floor_patch(x + 340, 180, 260, 150, lights=False, car=True, car_dx=40)
arrow(x + 400, x + 560, 350, (150, 255, 150))
words(x + 340, 370, ['Follows the car.'])
words(x + 20, 450, ['Each layer\u2019s older frames are found', 'the way that layer moves, so the', 'lights are never dragged and the car', 'leaves no ghost behind.'])

# 3. recombined, after the smoothing
x = panel(2, '3. Put back together', (150, 255, 150))
floor_patch(x + 115, 150, 400, 230, car_dx=40)
words(x + 14, 420, ['The still layer goes through the final', 'smoothing step as before. The moving', 'layer is added AFTER it, so that step',
                    'cannot drag the still lights along.'])
img.save(OUT)
print(OUT)
