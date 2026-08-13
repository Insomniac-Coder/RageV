#!/usr/bin/env python3
"""Generates the editor's viewport gizmo icons (7.4).

A light, a camera, a reflection probe and an audio source have no geometry,
so without a mark in the viewport they are invisible -- and what cannot be
seen cannot be clicked. These are the marks.

One atlas rather than four files: `UIRenderer::DrawImage` already takes a UV
sub-rect, so a strip costs one texture binding for every icon in the scene
and the draws batch into one.

Drawn white with the shape in the alpha channel, because the renderer tints:
one atlas serves the normal and the selected colour, and a theme change needs
no new art.

**With a dark outline baked in**, which is not decoration. A mark is drawn
against the scene -- sky, ground, whatever is being built -- so a light one
disappears over a bright sky and a dark one over a shadow. The outline is the
same lesson the viewport grid learned about picking a colour for a panel and
drawing it on a horizon. Because the tint *multiplies*, a white interior takes
the tint and a black ring stays black, so one atlas still serves every colour.

No third-party imaging library, for the reason `make_icon.py` gives: zlib and
struct are enough to write a PNG, and a repository that builds from a clone
with nothing but a compiler should not grow a build-time dependency to draw
four glyphs.

    python tools/scripts/make_editor_icons.py

Writes RageVEditor/assets/icons/gizmos.png.
"""

import math
import os
import struct
import zlib

CELL = 128                 # pixels per icon
SUPERSAMPLE = 4            # coverage samples per axis; 16 per pixel
OUTLINE = 7                # pixels at CELL; about 1.5 at the size marks draw
KINDS = ["light", "camera", "probe", "audio"]


# --- the shapes -------------------------------------------------------------
#
# Each takes a point in its own cell's [0,1] square and answers whether the
# point is inside the mark. Coverage -- and so the anti-aliasing -- comes from
# sampling these, so a shape only has to be a predicate and never has to know
# about pixels.

def light(x, y):
    """A sun: a core with eight rays."""
    dx, dy = x - 0.5, y - 0.5
    r = math.hypot(dx, dy)

    if r <= 0.185:
        return True

    if 0.255 <= r <= 0.455:
        # Angular distance to the nearest spoke, eight of them.
        angle = math.atan2(dy, dx)
        step = math.pi / 4.0
        offset = abs(angle - round(angle / step) * step)
        # Narrower further out, so the rays taper rather than fan.
        return offset < 0.15 - 0.16 * (r - 0.255)

    return False


def camera(x, y):
    """A body with a lens barrel pointing right."""
    # Body, with the corners rounded by insetting and testing a disc radius.
    if _rounded_box(x, y, 0.14, 0.34, 0.63, 0.72, 0.05):
        return True

    # The barrel: a trapezoid widening away from the body.
    if 0.63 <= x <= 0.84:
        t = (x - 0.63) / 0.21
        half = 0.075 + 0.075 * t
        return abs(y - 0.53) <= half

    return False


def probe(x, y):
    """A sphere: a ring with a highlight, which is what a probe captures."""
    dx, dy = x - 0.5, y - 0.5
    r = math.hypot(dx, dy)

    if 0.30 <= r <= 0.395:
        return True

    # The specular highlight, up and to the left like every render of a ball.
    return math.hypot(x - 0.375, y - 0.375) <= 0.085


def audio(x, y):
    """A speaker with two waves."""
    # The cone: a box for the driver, a trapezoid for the horn.
    if 0.19 <= x <= 0.33 and abs(y - 0.5) <= 0.10:
        return True
    if 0.33 <= x <= 0.50:
        t = (x - 0.33) / 0.17
        if abs(y - 0.5) <= 0.10 + 0.24 * t:
            return True

    # Two arcs, opening right.
    dx, dy = x - 0.50, y - 0.5
    if dx > 0.0:
        r = math.hypot(dx, dy)
        # Inside the cone's angle, so the waves read as coming out of it.
        if abs(math.atan2(dy, dx)) < 0.85:
            for radius in (0.60, 0.78):
                if abs(r - radius * 0.44) <= 0.030:
                    return True

    return False


SHAPES = { "light": light, "camera": camera, "probe": probe, "audio": audio }


def _rounded_box(x, y, x0, y0, x1, y1, radius):
    # The usual trick: clamp the point into the inner rectangle and measure.
    cx = min(max(x, x0 + radius), x1 - radius)
    cy = min(max(y, y0 + radius), y1 - radius)
    return math.hypot(x - cx, y - cy) <= radius


# --- rasterizing ------------------------------------------------------------

def coverage(shape):
    """The shape's anti-aliased alpha, one cell, as a list of rows of floats."""
    field = []
    for py in range(CELL):
        row = []
        for px in range(CELL):
            hits = 0
            for sy in range(SUPERSAMPLE):
                for sx in range(SUPERSAMPLE):
                    # Sample at pixel centres of the sub-grid.
                    x = (px + (sx + 0.5) / SUPERSAMPLE) / CELL
                    y = (py + (sy + 0.5) / SUPERSAMPLE) / CELL
                    if shape(x, y):
                        hits += 1
            row.append(hits / (SUPERSAMPLE * SUPERSAMPLE))
        field.append(row)
    return field


def dilate(field, radius):
    """The field grown by `radius` pixels -- a max over a disc.

    Separable would be faster and would grow a square; the marks have round
    features and a square halo on a sun's rays looks like a mistake.
    """
    offsets = [(dx, dy)
               for dy in range(-radius, radius + 1)
               for dx in range(-radius, radius + 1)
               if dx * dx + dy * dy <= radius * radius]

    grown = []
    for y in range(CELL):
        row = []
        for x in range(CELL):
            best = 0.0
            for dx, dy in offsets:
                sx, sy = x + dx, y + dy
                if 0 <= sx < CELL and 0 <= sy < CELL:
                    value = field[sy][sx]
                    if value > best:
                        best = value
                        if best >= 1.0:
                            break
            row.append(best)
        grown.append(row)
    return grown


def rasterize():
    """The atlas as RGBA rows: a white mark, a dark ring, shaped alpha."""
    cells = []
    for kind in KINDS:
        inner = coverage(SHAPES[kind])
        outer = dilate(inner, OUTLINE)
        cells.append((inner, outer))

    width = CELL * len(KINDS)
    rows = []

    for py in range(CELL):
        row = bytearray()
        for px in range(width):
            inner, outer = cells[px // CELL]
            a_in = inner[py][px % CELL]
            a_out = outer[py][px % CELL]

            # White over black, composited in straight alpha -- which is what
            # the UI shader blends, so no premultiplication here.
            alpha = a_in + a_out * (1.0 - a_in)
            value = a_in / alpha if alpha > 0.0 else 0.0

            level = round(255 * value)
            row += bytes((level, level, level, round(255 * alpha)))
        rows.append(bytes(row))

    return width, CELL, rows


def png(width, height, rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)          # filter type: none
        raw += row

    def chunk(tag, data):
        out = struct.pack(">I", len(data)) + tag + data
        return out + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)   # 6 = RGBA
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def main():
    root = os.path.join(os.path.dirname(__file__), "..", "..")
    directory = os.path.join(root, "RageVEditor", "assets", "icons")
    os.makedirs(directory, exist_ok=True)

    width, height, rows = rasterize()
    path = os.path.join(directory, "gizmos.png")
    with open(path, "wb") as f:
        f.write(png(width, height, rows))

    print(f"  {os.path.relpath(path, root)} ({width}x{height}, "
          f"{len(KINDS)} icons: {', '.join(KINDS)})")


if __name__ == "__main__":
    main()
