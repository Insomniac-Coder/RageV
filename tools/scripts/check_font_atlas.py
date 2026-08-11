"""Reconstruct glyphs from a baked atlas, the way the shader will.

An MSDF atlas is a picture of a picture: looking at it tells you the generator
produced *something* three-coloured, and almost nothing about whether the
median of those three channels is the shape you asked for. Edge colouring is
the part that goes subtly wrong -- a corner that rounds off, a thin stroke that
develops a notch -- and none of it is visible in the field itself.

So this does what the fragment shader does. It samples the atlas bilinearly,
takes the median of R, G and B, and thresholds it. If the result reads as the
letter, the generator, the packing, the y-flip and the plane bounds are all
right together, which is four claims a screenshot of the atlas cannot make.

    python tools/scripts/check_font_atlas.py <name.rvfont> [--out sheet.png]

Exits non-zero if a glyph fails to reconstruct.
"""

import argparse
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    sys.exit("pyyaml is required: pip install pyyaml")

try:
    from PIL import Image
except ImportError:
    sys.exit("pillow is required: pip install pillow")


def median(a, b, c):
    return max(min(a, b), min(max(a, b), c))


def sample(pixels, width, height, x, y):
    """Bilinear, in pixel coordinates, clamped at the edges."""
    x = min(max(x, 0.0), width - 1.001)
    y = min(max(y, 0.0), height - 1.001)

    x0, y0 = int(x), int(y)
    fx, fy = x - x0, y - y0
    x1, y1 = min(x0 + 1, width - 1), min(y0 + 1, height - 1)

    out = []
    for c in range(3):
        # getdata() hands back 0..255. The shader sees 0..1, and a median of
        # ~200 against a 0.5 threshold is "solid" for every pixel of every
        # glyph -- which is exactly how this first reported.
        p00 = pixels[y0 * width + x0][c] / 255.0
        p10 = pixels[y0 * width + x1][c] / 255.0
        p01 = pixels[y1 * width + x0][c] / 255.0
        p11 = pixels[y1 * width + x1][c] / 255.0
        top = p00 + (p10 - p00) * fx
        bottom = p01 + (p11 - p01) * fx
        out.append(top + (bottom - top) * fy)
    return out


def reconstruct(font, pixels, glyph, size):
    """Draw one glyph at `size` pixels per em, the way the shader would."""
    rect = glyph.get("Rect")
    plane = glyph.get("Plane")
    if not rect or not plane:
        return None

    ax, ay, aw, ah = rect
    left, bottom, right, top = plane

    width = max(int(round((right - left) * size)), 1)
    height = max(int(round((top - bottom) * size)), 1)

    atlas_w, atlas_h = font["AtlasWidth"], font["AtlasHeight"]
    image = Image.new("L", (width, height), 0)
    out = image.load()

    for py in range(height):
        for px in range(width):
            # Where this output pixel sits inside the glyph's atlas rect, with
            # the same y flip the baker applied.
            u = (px + 0.5) / width
            v = (py + 0.5) / height

            # The recorded rectangle is simply where the glyph is in the image,
            # so this is a straight lerp across it. The baker already flipped
            # the field's rows into image order.
            sx = ax + u * aw
            sy = ay + v * ah

            r, g, b = sample(pixels, atlas_w, atlas_h, sx, sy)
            d = median(r, g, b) - 0.5

            # No derivatives here, so a hard threshold rather than a smoothstep.
            out[px, py] = 255 if d > 0 else 0

    return image


def coverage(image):
    """Fraction of the glyph box that is inside the shape."""
    data = list(image.getdata())
    return sum(1 for v in data if v > 127) / max(len(data), 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("metrics", type=Path)
    parser.add_argument("--out", type=Path, default=Path("atlas_check.png"))
    parser.add_argument("--size", type=int, default=64)
    parser.add_argument("--text", default="AWEgjq@#8&Bm")
    args = parser.parse_args()

    font = yaml.safe_load(args.metrics.read_text())
    atlas_path = args.metrics.parent / font["Atlas"]

    image = Image.open(atlas_path).convert("RGB")
    pixels = list(image.getdata())

    by_code = {g["C"]: g for g in font["Glyphs"]}

    failures = []
    rendered = []

    for ch in args.text:
        glyph = by_code.get(ord(ch))
        if glyph is None:
            failures.append(f"'{ch}' is not in the atlas")
            continue

        drawn = reconstruct(font, pixels, glyph, args.size)
        if drawn is None:
            failures.append(f"'{ch}' has no atlas rect")
            continue

        filled = coverage(drawn)

        # A letterform covers roughly 15-30% of its own box. Empty means the
        # threshold is on the wrong side or the glyph missed the atlas; over
        # half means the field is inside-out and you are looking at the page
        # with the letter cut out of it.
        #
        # The upper bound is 0.55 rather than something safe like 0.90 because
        # **the first version of this check passed on an inside-out atlas**:
        # winding reversed, 'A' measured 77.6% instead of 18.8%, and a 0.90
        # ceiling waved it through. A bound that cannot catch the failure it is
        # aimed at is decoration.
        if filled < 0.02:
            failures.append(f"'{ch}' reconstructs to almost nothing ({filled:.1%})")
        elif filled > 0.55:
            failures.append(f"'{ch}' covers {filled:.1%} of its box -- the field "
                            f"is probably inside-out (check contour winding)")

        rendered.append((ch, drawn, filled))

    if rendered:
        pad = 4
        sheet_w = sum(d.width + pad for _, d, _ in rendered) + pad
        sheet_h = max(d.height for _, d, _ in rendered) + 2 * pad
        sheet = Image.new("L", (sheet_w, sheet_h), 40)
        x = pad
        for _, drawn, _ in rendered:
            sheet.paste(drawn, (x, pad))
            x += drawn.width + pad
        sheet.save(args.out)
        print(f"wrote {args.out}")

    print(f"{font['Font']}: {len(font['Glyphs'])} glyphs, "
          f"atlas {font['AtlasWidth']}x{font['AtlasHeight']}, "
          f"em {font['EmSize']} px, range {font['PxRange']} px")
    for ch, _, filled in rendered:
        print(f"  '{ch}'  {filled:.1%} covered")

    if failures:
        print("\nFAIL")
        for line in failures:
            print(f"  {line}")
        return 1

    print(f"\nall {len(rendered)} glyphs reconstruct from the median of three channels")
    return 0


if __name__ == "__main__":
    sys.exit(main())
