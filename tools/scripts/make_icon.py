"""Generates RageV's application icon.

Kept as a script rather than a checked-in binary somebody drew once, for the
same reason the sky.hdr generator is: an icon that can be regenerated can be
changed. And it was: the first mark was a rounded square holding a V drawn as
two thick segments, which gave it rounded corners *and* rounded stroke caps and
read as soft at every size. The owner asked for minimalist and edgy, picked
this one out of six, and the difference is that **there is no curve anywhere in
it** -- a chamfered tile and a V built from straight-sided quads with mitred
ends and a real point.

No third-party imaging library. zlib and struct are enough to write a PNG, and
an ICO is a header plus a list of images. Adding Pillow as a build-time
dependency to draw one icon would be a poor trade in a repository that builds
from a clone with nothing but a compiler.

    python tools/scripts/make_icon.py

Writes RageVEditor/icon.ico, RageVRuntime/icon.ico, and the PNGs the window
uses at runtime.
"""
import os
import struct
import zlib

# --- the mark ---------------------------------------------------------------
#
# **Coordinates are y-down**, the way an image is stored and the way the mark
# was drawn, so the numbers below read in the same direction as the picture.
# The old mark was y-up and `render` flipped for it; nothing flips now.
#
# Every shape is a *convex* polygon, listed clockwise, and everything is a
# point-in-polygon test. That is what makes the edges hard: a distance to a
# segment has a rounded cap whether or not anybody wanted one, and the first
# icon had four of them.

BACKGROUND = (0x0E, 0x0E, 0x12)
ACCENT     = (0xE0, 0x30, 0x30)   # the manual's --accent, shared deliberately
LIGHT      = (0xF2, 0xF2, 0xF6)

# The tile: a square with the top-left and bottom-right corners cut off.
# Opposite corners rather than adjacent ones, so the cuts read as a single
# diagonal gesture through the mark rather than as a bevel on one end.
CHAMFER = 0.26
TILE = ((CHAMFER, 0.0), (1.0, 0.0), (1.0, 1.0 - CHAMFER),
        (1.0 - CHAMFER, 1.0), (0.0, 1.0), (0.0, CHAMFER))

# The V, as a letter rather than a wedge: `TOP` and `APEX` are where it starts
# and ends, `OUTER` is how far in the outer edges begin and `WIDTH` is the
# stroke.
TOP, APEX = 0.24, 0.78
OUTER, WIDTH = 0.18, 0.155

# Where the two inner edges meet on the centre line. **Derived, not chosen**:
# they are parallel to the outer edges, so this follows from the four numbers
# above -- and deriving it is what keeps the apex a real point when any of them
# is nudged. A hand-picked value gives a V whose inside stops short of its
# outside the first time the stroke changes.
_INNER = OUTER + WIDTH
_MEET = TOP + (0.5 - _INNER) * (APEX - TOP) / (0.5 - OUTER)

V_LEFT  = ((OUTER, TOP), (_INNER, TOP), (0.5, _MEET), (0.5, APEX))
V_RIGHT = ((1.0 - _INNER, TOP), (1.0 - OUTER, TOP), (0.5, APEX), (0.5, _MEET))

# 8 rather than 4. The coverage here is a count of samples inside the shape
# rather than a distance, so the number of levels an edge can take *is* the
# supersample squared -- and a chamfer is a long diagonal across the whole
# tile, which is where 16 levels show as steps. 64 does not, and the whole set
# renders in about a second either way.
SUPERSAMPLE = 8


def _inside(x, y, points):
    """Whether a point is inside a convex polygon listed clockwise, y-down.

    The cross product of each edge with the vector to the point keeps its sign
    all the way round a convex polygon, so one loop and no special cases.
    """
    for i in range(len(points)):
        ax, ay = points[i]
        bx, by = points[(i + 1) % len(points)]
        if (bx - ax) * (y - ay) - (by - ay) * (x - ax) < 0.0:
            return False
    return True


def _sample(x, y):
    """Colour and alpha at a point in the unit square, or None for outside."""
    if not _inside(x, y, TILE):
        return None

    # The light half tested first so it wins the seam at the apex, which is
    # what keeps the point from muddying at small sizes. The two quads share
    # the centre line exactly, so this decides a boundary rather than an
    # overlap.
    if _inside(x, y, V_RIGHT):
        return LIGHT
    if _inside(x, y, V_LEFT):
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
                    y = (py * SUPERSAMPLE + sy + 0.5) * step
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
