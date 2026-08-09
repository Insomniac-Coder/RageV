"""Generates RageV's application icon.

Kept as a script rather than a checked-in binary somebody drew once, for the
same reason the sky.hdr generator is: an icon that can be regenerated can be
changed. The mark is the manual's wordmark reduced to what survives at 16
pixels -- a two-tone V, the red half from `Rage` and the light half from the
`V`, on the site's near-black.

No third-party imaging library. zlib and struct are enough to write a PNG, and
an ICO is a header plus a list of images. Adding Pillow as a build-time
dependency to draw one icon would be a poor trade in a repository that builds
from a clone with nothing but a compiler.

    python tools/scripts/make_icon.py

Writes RageVEditor/icon.ico, RageVRuntime/icon.ico, and the PNGs the window
uses at runtime.
"""
import math
import os
import struct
import zlib

# --- the mark ---------------------------------------------------------------

BACKGROUND = (0x0E, 0x0E, 0x12)
ACCENT     = (0xE0, 0x30, 0x30)   # the manual's --accent
LIGHT      = (0xF2, 0xF2, 0xF6)

CORNER_RADIUS = 0.20              # of the icon's width
STROKE_HALF   = 0.072             # half the V's stroke width

# The V, as two segments meeting at a point. Coordinates are y-up: the apex is
# the low number. Sitting a touch above the true centre, because a V is
# bottom-heavy and an optically centred one has to be nudged up.
APEX  = (0.500, 0.272)
LEFT  = (0.250, 0.752)
RIGHT = (0.750, 0.752)

SUPERSAMPLE = 4


def _rounded_rect_coverage(x, y, radius):
    """Signed coverage of the unit square with rounded corners."""
    cx = min(max(x, radius), 1.0 - radius)
    cy = min(max(y, radius), 1.0 - radius)
    dx, dy = x - cx, y - cy
    if dx == 0.0 and dy == 0.0:
        return True
    return (dx * dx + dy * dy) <= radius * radius


def _distance_to_segment(px, py, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    wx, wy = px - ax, py - ay
    length = vx * vx + vy * vy
    t = 0.0 if length == 0.0 else max(0.0, min(1.0, (wx * vx + wy * vy) / length))
    dx, dy = px - (ax + t * vx), py - (ay + t * vy)
    return math.sqrt(dx * dx + dy * dy)


def _sample(x, y):
    """Colour and alpha at a point in the unit square, or None for outside."""
    if not _rounded_rect_coverage(x, y, CORNER_RADIUS):
        return None

    # Right stroke drawn second so the light half wins the join at the apex,
    # which is what keeps the point from muddying at small sizes.
    if _distance_to_segment(x, y, *RIGHT, *APEX) <= STROKE_HALF:
        return LIGHT
    if _distance_to_segment(x, y, *LEFT, *APEX) <= STROKE_HALF:
        return ACCENT
    return BACKGROUND


def render(size):
    """RGBA bytes, top-down, supersampled for antialiasing."""
    pixels = bytearray(size * size * 4)
    step = 1.0 / (size * SUPERSAMPLE)
    weight = 1.0 / (SUPERSAMPLE * SUPERSAMPLE)

    for py in range(size):
        for px in range(size):
            r = g = b = a = 0.0
            for sy in range(SUPERSAMPLE):
                for sx in range(SUPERSAMPLE):
                    x = (px * SUPERSAMPLE + sx + 0.5) * step
                    # Image rows run top-down; the mark is defined y-up.
                    y = 1.0 - (py * SUPERSAMPLE + sy + 0.5) * step
                    colour = _sample(x, y)
                    if colour is None:
                        continue
                    r += colour[0] * weight
                    g += colour[1] * weight
                    b += colour[2] * weight
                    a += 255.0 * weight

            offset = (py * size + px) * 4
            pixels[offset + 0] = int(r + 0.5)
            pixels[offset + 1] = int(g + 0.5)
            pixels[offset + 2] = int(b + 0.5)
            pixels[offset + 3] = int(a + 0.5)
    return bytes(pixels)


# --- PNG --------------------------------------------------------------------

def _chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(size, rgba):
    raw = bytearray()
    for row in range(size):
        raw.append(0)                                  # filter: none
        raw += rgba[row * size * 4:(row + 1) * size * 4]

    return (b"\x89PNG\r\n\x1a\n"
            + _chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + _chunk(b"IEND", b""))


# --- ICO --------------------------------------------------------------------

def _dib(size, rgba):
    """A 32-bit bottom-up DIB, which is what Explorer wants for small sizes.

    PNG-compressed entries are legal from Vista onwards but are only reliably
    handled at 256; below that some shells still expect a DIB. Emitting both --
    DIB small, PNG large -- is what every well-behaved .ico does.
    """
    header = struct.pack("<IiiHHIIiiII",
                         40, size, size * 2, 1, 32, 0, size * size * 4, 0, 0, 0, 0)

    body = bytearray()
    for row in range(size - 1, -1, -1):                # bottom-up
        for column in range(size):
            offset = (row * size + column) * 4
            r, g, b, a = rgba[offset:offset + 4]
            body += bytes((b, g, r, a))                # BGRA

    # The AND mask is unused with an alpha channel but must still be present,
    # one bit per pixel, each row padded to four bytes.
    stride = ((size + 31) // 32) * 4
    body += bytes(stride * size)
    return header + bytes(body)


def write_ico(images):
    """images: [(size, rgba)], smallest first."""
    entries = bytearray()
    payloads = []
    offset = 6 + 16 * len(images)

    for size, rgba in images:
        data = write_png(size, rgba) if size >= 128 else _dib(size, rgba)
        payloads.append(data)
        entries += struct.pack("<BBBBHHII",
                               size if size < 256 else 0,
                               size if size < 256 else 0,
                               0, 0, 1, 32, len(data), offset)
        offset += len(data)

    return struct.pack("<HHH", 0, 1, len(images)) + bytes(entries) + b"".join(payloads)


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

    sizes = [16, 24, 32, 48, 64, 128, 256]
    rendered = [(size, render(size)) for size in sizes]
    print("rendered " + ", ".join("%dx%d" % (s, s) for s in sizes))

    ico = write_ico(rendered)
    for target in ("RageVEditor", "RageVRuntime"):
        path = os.path.join(root, target, "icon.ico")
        with open(path, "wb") as handle:
            handle.write(ico)
        print("wrote %s (%d bytes)" % (path, len(ico)))

    # The window icon is set at runtime through GLFW, which wants raw pixels
    # rather than an .ico. 48 is what the taskbar and alt-tab pick up; 32 is the
    # title bar.
    #
    # Written once, into the engine's asset root. RageVEditor/assets is the only
    # copy of these; the runtime stages what it needs from there, the same way
    # it does shaders and fonts.
    for size in (32, 48):
        rgba = dict(rendered)[size]
        path = os.path.join(root, "RageVEditor", "assets", "icon-%d.png" % size)
        with open(path, "wb") as handle:
            handle.write(write_png(size, rgba))
        print("wrote %s" % path)


if __name__ == "__main__":
    main()
