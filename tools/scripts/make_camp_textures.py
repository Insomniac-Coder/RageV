#!/usr/bin/env python3
"""Every image the camp wears, generated here rather than borrowed.

    python tools/scripts/make_camp_textures.py

The camp started out wearing the courtyard's photographic soil and wood maps,
and that was wrong twice over.

**The first reason is a rule of the project**: an asset in the repository needs
a licence and a provenance, and a surface this simple is cheaper to derive than
to acquire -- the same argument `make_sky_hdr.py` and `make_fire_sound.py`
already make for a sky and a fire.

**The second reason is the look, and it is the one that decides what is in this
file.** These props are flat-shaded and eight-sided. *Realism is not the goal.*
A photographic albedo, a normal map and a parallax height are three different
ways of pretending a flat facet is not flat -- which is the exact opposite of
what a low-poly scene is doing, and they read as a smudge over the top of it.

So there are no normal maps here, no height maps and no roughness maps. There
is one colour map per surface, and every one of them is **flat patches of flat
colour**:

  * A **cellular** partition -- each pixel takes the value of the nearest
    jittered site, so the tile comes out as polygons of one value each. That is
    the same idea as the geometry: a small number of flat pieces, and the
    boundaries between them are what you see.
  * **Posterised** to a handful of steps, so the values themselves are a short
    list rather than a continuum. Four greens, not four thousand.
  * **Close together in value.** The map's whole job is to stop a large surface
    reading as one dead sheet of colour. Any more contrast than that and it
    starts competing with the facets, which are what the eye is supposed to be
    reading the form from.

**Seamless by construction, not by blending.** Every site and every band wraps,
so the last column is the neighbour of the first because the arithmetic says
so. Mirroring or cross-fading an edge leaves a seam the moment the tiling is
anything but 1 -- and on a flat-patch texture that seam is a visible straight
line, because there is no noise to hide it in.
"""

import argparse
import math
import pathlib
import struct
import sys
import zlib

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "SampleProject" / "assets"

# 256 rather than 512 or 1024. A map made of two dozen flat patches has nothing
# in it that a larger tile would resolve better, and these are meant to be
# cheap in a repository as well as on screen.
SIZE = 256

# Reproducible: two runs produce the same bytes, so a diff means the recipe
# changed rather than that the file was rewritten.
SEED = 0x726167655643


# --- files --------------------------------------------------------------------

def fnv1a(text):
    """The registry's handle arithmetic, so a generated asset can name itself.

    A handle is minted on the registry's first scan, which is *after* the scene
    that references it has been written -- so a generator cannot ask for one
    and writes the `.meta` itself instead. Same reason and same arithmetic as
    postprofile.py's.
    """
    h = 0xCBF29CE484222325
    for byte in text.encode("utf-8"):
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h or 0x726167655654


def write_meta(path, kind):
    """The sidecar, with the handle *and* the hash the registry would compute.

    Writing SourceHash: 0 and letting the first run fix it leaves every
    generated `.meta` modified after every check -- the churn that keeps
    turning up in `git status` and gets `git checkout`-ed away by hand.
    """
    data = pathlib.Path(path).read_bytes()
    h = 0xCBF29CE484222325
    for byte in data:
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF

    handle = fnv1a(pathlib.Path(path).name)
    pathlib.Path(str(path) + ".meta").write_text(
        "Handle: {0}\nType: {1}\nSourceHash: {2}\n".format(handle, kind, h),
        encoding="utf-8")
    return handle


def png(path, image):
    """Write a PNG. `image` is (h, w) grey, (h, w, 3) RGB or (h, w, 4) RGBA.

    No third-party imaging library, for the reason `make_icon.py` gives: zlib
    and struct are enough, and a repository that builds from a clone with only
    a compiler is worth more than the twenty lines this saves.
    """
    image = np.clip(image, 0.0, 1.0)
    data = (image * 255.0 + 0.5).astype(np.uint8)
    if data.ndim == 2:
        data = data[:, :, None]

    height, width, channels = data.shape
    colour_type = {1: 0, 3: 2, 4: 6}[channels]

    raw = bytearray()
    for row in range(height):
        # Filter 1, "sub": predict each pixel from the one to its left. On a
        # map made of flat patches almost every pixel equals its neighbour, so
        # this turns whole runs into zeroes and the file collapses. The old
        # noise maps used filter 0 because there was nothing to predict.
        raw.append(1)
        row_bytes = data[row].tobytes()
        previous = bytes(channels)
        for start in range(0, len(row_bytes), channels):
            pixel = row_bytes[start:start + channels]
            raw.extend((pixel[i] - previous[i]) & 0xFF for i in range(channels))
            previous = pixel

    def chunk(tag, payload):
        out = struct.pack(">I", len(payload)) + tag + payload
        return out + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0)
    blob = (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))

    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(blob)
    return len(blob)


# --- the shapes a flat-patch texture is made of -------------------------------

def cellular(size, count, rng, jitter=0.8):
    """Flat polygonal patches: every pixel takes its nearest site's value.

    This is the whole aesthetic in one function. Noise gives a continuous
    field, which is a photograph of a surface; a nearest-site partition gives
    **regions**, which is what a faceted object is made of, and the edges
    between them are straight for the same reason the model's are.

    Distances are measured the short way round the tile, so a patch that
    touches one edge continues on the other and the tiling has no seam.
    """
    sites = []
    values = []
    step = size / float(count)
    for cy in range(count):
        for cx in range(count):
            offset = rng.random(2) * jitter + (1.0 - jitter) * 0.5
            sites.append(((cx + offset[0]) * step, (cy + offset[1]) * step))
            values.append(rng.random())

    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64)
    best = np.full((size, size), np.inf)
    field = np.zeros((size, size))

    for (px, py), value in zip(sites, values):
        dx = np.abs(xs - px)
        dy = np.abs(ys - py)
        dx = np.minimum(dx, size - dx)
        dy = np.minimum(dy, size - dy)
        distance = dx * dx + dy * dy

        closer = distance < best
        best = np.where(closer, distance, best)
        field = np.where(closer, value, field)

    return field


def bands(size, count, rng, vertical=True):
    """Flat stripes of random value -- planks, bark plates, a brushed edge.

    The one-dimensional case of the same idea, and the right one wherever the
    surface has a grain direction. A trunk's plates run up it; nothing about
    a trunk is isotropic.
    """
    values = rng.random(count)
    index = (np.arange(size) * count // size) % count
    strip = values[index]
    return np.repeat(strip[None, :], size, axis=0) if vertical else \
        np.repeat(strip[:, None], size, axis=1)


def posterise(field, steps):
    """Snap a field to `steps` evenly spaced values.

    Cheap, and it is what keeps the palette a *list*. A cellular partition is
    already flat within a patch, but two neighbouring patches at 0.61 and 0.63
    are a gradient with extra steps -- posterising makes them either the same
    colour or visibly different ones, which is the decision the look wants
    made.
    """
    steps = max(int(steps), 2)
    return np.clip(np.floor(field * steps), 0, steps - 1) / (steps - 1.0)


def tint(value, low, high):
    """A value field mapped between two colours."""
    low = np.array(low, dtype=np.float64)
    high = np.array(high, dtype=np.float64)
    return low[None, None, :] + value[:, :, None] * (high - low)[None, None, :]


# --- the surfaces -------------------------------------------------------------
#
# Each returns one colour map. There is no normal, height or roughness map in
# this file on purpose: those are three ways of faking surface relief, and the
# relief here is supposed to be the geometry.

def ground(rng):
    """Dry forest floor: flat patches of dark earth, a few of them stonier.

    **Warm soil brown**, which is the owner's call and the right one. The first
    version was a cold mauve, reasoning that an unlit surface at night takes
    its colour from the sky -- true of a photograph and wrong for this. The
    reference is a lit poster: its ground is warm earth that the fire then
    makes warmer, and the cold only arrives in the shadows between the trees.
    A ground already grey-violet has nowhere to go when the firelight reaches
    it, which is why the clearing read as slate.

    Two scales of patch: big ones that give the clearing its broad lighter and
    darker areas, and small ones scattered over them so the ground has some
    grain to it without having any *texture*.
    """
    broad = posterise(cellular(SIZE, 5, rng), 4)
    fine = posterise(cellular(SIZE, 13, rng), 3)

    value = np.clip(0.30 + 0.52 * broad + 0.18 * fine, 0.0, 1.0)
    colour = tint(value, (0.20, 0.13, 0.085), (0.56, 0.40, 0.26))

    # A handful of the fine patches are stone rather than earth -- greyer and
    # cooler. Picking them by threshold keeps them as whole patches, which is
    # what makes them read as stones instead of as lighter dirt.
    stony = (fine > 0.98).astype(np.float64)
    grey = tint(np.clip(value + 0.2, 0, 1), (0.27, 0.25, 0.25), (0.56, 0.53, 0.52))
    return colour * (1.0 - stony[:, :, None]) + grey * stony[:, :, None]


def bark(rng):
    """Pine bark: vertical plates, four values, nothing else.

    Bands rather than patches because bark has a direction, and the trunk it
    goes on is a six-sided cylinder whose own facets already run the same way.
    The two agreeing is most of why it reads as bark at all.
    """
    plates = posterise(bands(SIZE, 14, rng), 4)
    breaks = posterise(cellular(SIZE, 6, rng), 3)

    value = np.clip(0.22 + 0.58 * plates + 0.20 * breaks, 0.0, 1.0)
    return tint(value, (0.14, 0.09, 0.07), (0.44, 0.29, 0.20))


def canvas(rng):
    """Tent cloth: three values, and they are nearly the same value.

    The quietest map in the file. Two big flat panels catching firelight will
    band badly if they are one exact colour, and this is the least that fixes
    it -- the tent's red and its pale band are the *material's* BaseColor, so
    a map that carried colour of its own would fight both.
    """
    value = posterise(cellular(SIZE, 4, rng), 3)
    return tint(np.clip(0.78 + 0.22 * value, 0.0, 1.0),
                (0.80, 0.79, 0.78), (1.0, 0.99, 0.98))


def fabric(rng):
    """Chair seats, pack, bedroll: a coarser patchwork than the tent's.

    Coarser because these are small objects seen close, and a panel the size
    of a chair seat wants two or three visible values across it or it is a
    rectangle of paint.
    """
    value = posterise(cellular(SIZE, 8, rng), 4)
    return tint(np.clip(0.62 + 0.38 * value, 0.0, 1.0),
                (0.55, 0.55, 0.58), (1.0, 1.0, 1.0))


def stone(rng):
    """The fire ring and the scattered pebbles: pale grey facets.

    Pale, because in the reference the ring is the one thing near the fire that
    is *not* warm-coloured, and that contrast is what makes the ring read as a
    ring rather than as more firelight.
    """
    value = posterise(cellular(SIZE, 9, rng), 4)
    return tint(np.clip(0.30 + 0.62 * value, 0.0, 1.0),
                (0.34, 0.33, 0.36), (0.84, 0.83, 0.82))


def needle(rng):
    """The canopy: four greens, and they are close together.

    A conifer at this poly count is eight flat facets, and what sells it is
    that each facet is a single value. This map exists only so a tier two
    metres across does not read as a sheet of card -- it is the least
    aggressive surface in the file and it should stay that way.
    """
    value = posterise(cellular(SIZE, 7, rng), 4)
    return tint(np.clip(0.55 + 0.45 * value, 0.0, 1.0),
                (0.62, 0.72, 0.66), (1.0, 1.0, 0.95))


def metal(rng):
    """Chair frames, the lantern, the pot: dark, with a couple of worn steps.

    Enough variation that a thin black bar in front of a fire is not one solid
    silhouette, and no more. Nothing here is trying to look like steel; it is
    trying to look like a *drawing* of steel, which is what the rest of the
    scene looks like.
    """
    value = posterise(bands(SIZE, 9, rng), 3)
    patch = posterise(cellular(SIZE, 6, rng), 2)

    return tint(np.clip(0.45 + 0.35 * value + 0.20 * patch, 0.0, 1.0),
                (0.42, 0.42, 0.46), (0.95, 0.95, 1.0))


SURFACES = (
    ("camp_ground", ground),
    ("camp_bark", bark),
    ("camp_canvas", canvas),
    ("camp_fabric", fabric),
    ("camp_stone", stone),
    ("camp_needle", needle),
    ("camp_metal", metal),
)


# --- the sprites --------------------------------------------------------------
#
# These two are not surfaces, and the flat-patch rule does not apply to them:
# a particle here is a piece of *light*, not a piece of an object. Light is the
# one thing in a scene like this that genuinely is smooth, and posterising a
# glow gives visible rings.

def flame_sprite():
    """The ember and the heat shimmer over the fire: a soft upright glow.

    Additive, so **the alpha does the work and the colour is nearly white** --
    the emitter's gradient decides what colour a flame is, and a sprite that
    arrives pre-tinted orange multiplies that decision by itself.

    Upright rather than round, because a tongue of flame is taller than it is
    wide and a round sprite piles up into a ball of light.

    The flame's actual *shape* in this scene is modelled geometry -- faceted
    shards, in `make_camp_models.py`. This is only what rises off it.
    """
    size = 128
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64)
    nx = (xs - (size - 1) / 2.0) / (size / 2.0)
    ny = (ys - (size - 1) / 2.0) / (size / 2.0)

    radius = np.sqrt((nx / 0.66) ** 2 + ny ** 2)

    alpha = np.clip(1.0 - radius, 0.0, 1.0) ** 1.8
    core = np.clip(1.0 - radius * 1.9, 0.0, 1.0) ** 2.0

    rgb = np.stack([np.full((size, size), 1.0),
                    0.86 + 0.14 * core,
                    0.70 + 0.30 * core], axis=-1)
    return np.concatenate([rgb, alpha[:, :, None]], axis=-1)


def spark_sprite():
    """A firefly: a small round point with a soft halo.

    Two falloffs added rather than one -- a tight core so it reads as a point
    at distance, and a wide faint halo so it does not alias into a single
    flickering pixel when it is far away.
    """
    size = 64
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64)
    nx = (xs - (size - 1) / 2.0) / (size / 2.0)
    ny = (ys - (size - 1) / 2.0) / (size / 2.0)
    radius = np.sqrt(nx * nx + ny * ny)

    core = np.clip(1.0 - radius * 3.4, 0.0, 1.0) ** 1.6
    halo = np.clip(1.0 - radius, 0.0, 1.0) ** 3.0
    alpha = np.clip(core + halo * 0.35, 0.0, 1.0)

    rgb = np.ones((size, size, 3))
    return np.concatenate([rgb, alpha[:, :, None]], axis=-1)


# --- the grade ----------------------------------------------------------------

def write_lut(path):
    """The camp's grade, as a recipe rather than a baked table.

    The courtyard's LUT is *warm with lifted shadows*, which is the end of a
    working day. This scene is a fire in a cold wood, and what it needs is the
    opposite treatment at the two ends: the shadows pulled toward blue and the
    highlights left warm, so the only warm thing in the frame is the thing that
    is actually on fire.

    A recipe rather than a `.cube` because somebody should be able to open it
    and see what it does -- ENGINE-NOTES 7v, and the whole argument for
    `.rvlut` existing.
    """
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join([
        "LutRecipe: 1",
        # **Warm.** The first version graded cool, on the argument that the
        # fire supplies all the warmth a night frame can take -- true of a
        # photograph and wrong for this. A camp fire scene is *about* being
        # warm, and pushing the whole image toward it makes the firelight feel
        # like it reaches further than the nine metres it actually does.
        "Temperature: 0.2",
        "Tint: 0.02",
        # Still a little violet in the lift, so the shadows keep some of the
        # night in them and the warmth is not uniform across the whole range.
        "Lift: [0.016, 0.008, 0.026]",
        "Gamma: [1, 1.0, 1.02]",
        # And warmer again in the gain, so the brightest things -- which are
        # the things the fire is lighting -- are the warmest.
        "Gain: [1.10, 1.0, 0.88]",
        "Contrast: 1.12",
        # Saturation *up*, not down. The reference is a poster, not a
        # photograph, and its colours are frankly unrealistic on purpose --
        # which is the same decision this whole file is making.
        "Saturation: 1.14",
        "Size: 33",
    ]) + "\n", encoding="utf-8")
    return write_meta(path, "ColorLut")


# --- main ---------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets", default=str(ASSETS))
    args = parser.parse_args()

    assets = pathlib.Path(args.assets)
    materials = assets / "materials"
    textures = assets / "textures"

    # The realism-era maps, which this file used to write. Removed here rather
    # than by hand, so a checkout that has them does not keep them around as
    # assets nothing references.
    stale = 0
    for name, _ in SURFACES:
        for suffix in ("normal", "roughness", "height"):
            for path in (materials / "{0}_{1}.png".format(name, suffix),
                         materials / "{0}_{1}.png.meta".format(name, suffix)):
                if path.exists():
                    path.unlink()
                    stale += 1

    total = 0
    handles = {}

    for name, builder in SURFACES:
        rng = np.random.default_rng(SEED ^ fnv1a(name))
        path = materials / "{0}_color.png".format(name)
        total += png(path, builder(rng))
        handles[name + "_color"] = write_meta(path, "Texture")
        sys.stdout.write("  {0:<14} colour, flat patches\n".format(name))

    for name, image in (("camp_flame", flame_sprite()),
                        ("camp_spark", spark_sprite())):
        path = textures / (name + ".png")
        total += png(path, image)
        handles[name] = write_meta(path, "Texture")
        sys.stdout.write("  {0:<14} sprite, RGBA\n".format(name))

    handles["camp_lut"] = write_lut(assets / "post" / "camp.rvlut")
    sys.stdout.write("  {0:<14} grade recipe\n".format("camp.rvlut"))

    sys.stdout.write("\n{0} images, {1:.1f} KB".format(
        len(SURFACES) + 2, total / 1024.0))
    if stale:
        sys.stdout.write(", and {0} realism-era file(s) removed".format(stale))
    sys.stdout.write("\n\nhandles:\n")
    for name in sorted(handles):
        sys.stdout.write("  {0:<28} {1}\n".format(name, handles[name]))


if __name__ == "__main__":
    main()
