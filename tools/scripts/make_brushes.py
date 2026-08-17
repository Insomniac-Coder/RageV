#!/usr/bin/env python3
"""Brush masks: the greyscale shapes and patterns the terrain brush reads
(ENGINE-NOTES 7as).

A brush mask is an ordinary square greyscale PNG in
`RageVEditor/assets/brushes/`. The editor enumerates that folder through the
VFS and decodes each with stb_image -- the red channel is the value -- so a
user who drops a PNG in beside these has a new brush shape and a new
pattern, with no import step and no asset handle: a brush is a tool of the
editor, not content of a project.

Used two ways, both by the same image:

- as a **shape**, laid over the brush's square [-Radius, Radius]^2 and
  rotated by the brush's angle (and, following, by the stroke's direction),
  sampled bilinearly, zero outside;
- as a **pattern**, tiled over the *ground* every `PatternScale` metres, so
  the pattern stays where the world is and a stroke reveals it rather than
  dragging it along.

Every one of these is a landform or a ground texture -- something a terrain
is actually built out of -- not a decorative stamp. Fifteen, all seeded, all
128 a side, all committed:

  Shapes, for Raise, Lower and Flatten -- a few plain ones, a few with real
  structure in them, which is the spread every landscape tool ships:
    dome      a smooth round hill: the workhorse, softer than the disc
    soft      an irregular blob with an edge you cannot find: hides where a
              stroke stopped
    mid       the same blob, shorter fall-off, weather inside it -- where
              most sculpting actually happens
    hard      a flat-topped slab with a short steep edge: a bench, a shelf,
              a lava field
    wispy     filaments: torn, wind-blown ground, the complex end of the
              library
    branch    fewer, thicker forking limbs: spurs off a ridge, arms of a
              delta when lowered
    mountain  a peak with ridged spurs and a rough flank -- one press is a
              mountain, a few overlapping are a massif
    ridge     a long crest along the brush's x, tapering across: drag it and
              a range grows; turn it with Angle or let it follow the stroke
    mesa      a flat top with steep sides -- a plateau, or a building site
    crater    a raised rim around an empty middle: crater, caldera, berm;
              with Lower it is a moat or a quarry
    pad       a hard rounded rectangle, flat to its edge: what Flatten and
              Set Height want for a road, a yard or a foundation

  Patterns, tiled over the ground under any mode:
    erosion   gullies -- ridged noise stretched one way, so a slope gets
              runnels; the one worth Follow stroke
    rock      fine fractal detail for roughening a surface that reads too
              smooth
    veins     a connected network of ridgelines -- the veins a mountain range
              makes across a map; raise through it and the spines link up
              with basins between, which separate lumps never do
    dunes     wind ridges: one crest per tile, a clean sine, so a check can
              derive where the material lands

    python tools/scripts/make_brushes.py
"""

import pathlib

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
BRUSHES = ROOT / "RageVEditor" / "assets" / "brushes"

SIZE = 128
SEED = 20260817


def coords(size=SIZE):
    """(u, v) in [-1, 1] at texel centres, and the radius from the centre."""
    axis = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    u, v = np.meshgrid(axis, axis)
    return u, v, np.sqrt(u * u + v * v)


def smoothstep(edge0, edge1, x):
    # The edges may be arrays, so the guard against a zero-width band is
    # elementwise.
    t = np.clip((x - edge0) / np.maximum(np.asarray(edge1) - np.asarray(edge0), 1e-6), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def normalise(values):
    low, high = values.min(), values.max()
    return (values - low) / max(high - low, 1e-6)


def value_noise(size, cells, rng):
    """A random lattice `cells` a side, smoothstep-interpolated up, wrapping
    -- so a pattern tiles without a seam."""
    lattice = rng.random((cells, cells))
    xs = np.linspace(0.0, cells, size, endpoint=False)
    x0 = np.floor(xs).astype(int) % cells
    x1 = (x0 + 1) % cells
    fx = xs - np.floor(xs)
    fx = fx * fx * (3.0 - 2.0 * fx)
    rows = lattice[x0, :] * (1.0 - fx)[:, None] + lattice[x1, :] * fx[:, None]
    return rows[:, x0] * (1.0 - fx)[None, :] + rows[:, x1] * fx[None, :]


def fbm(size, cells, rng, octaves=4, ridged=False):
    """Fractal noise. `ridged` folds each octave about its middle -- the
    standard trick for crests and gullies rather than blobs."""
    total = np.zeros((size, size))
    amplitude, weight = 1.0, 0.0
    for octave in range(octaves):
        layer = value_noise(size, cells * 2 ** octave, rng)
        if ridged:
            layer = 1.0 - np.abs(layer * 2.0 - 1.0)
        total += layer * amplitude
        weight += amplitude
        amplitude *= 0.5
    return total / weight


def rounded_box(u, v, half, radius):
    """Distance to a rounded rectangle of half-extent `half` and corner
    `radius`: <= 0 inside."""
    dx = np.abs(u) - (half - radius)
    dz = np.abs(v) - (half - radius)
    outside = np.sqrt(np.maximum(dx, 0.0) ** 2 + np.maximum(dz, 0.0) ** 2)
    inside = np.minimum(np.maximum(dx, dz), 0.0)
    return outside + inside - radius


# --- the shapes ---------------------------------------------------------------

def dome():
    """A raised cosine: full at the centre, zero at the rim, no corner in the
    profile -- what a hill looks like from above."""
    _, _, r = coords()
    return np.where(r < 1.0, 0.5 + 0.5 * np.cos(np.pi * np.clip(r, 0.0, 1.0)), 0.0)


def mountain():
    """A dome roughened by ridged noise, with spurs running off the summit:
    one press raises a peak that already has flanks and gullies, which is the
    difference between a mountain and a bump."""
    rng = np.random.default_rng(SEED)
    u, v, r = coords()
    base = np.where(r < 1.0, np.cos(np.pi * 0.5 * np.clip(r, 0.0, 1.0)) ** 1.5, 0.0)
    rough = fbm(SIZE, 5, rng, octaves=5, ridged=True)
    # Spurs running off the summit: a few broad ones, warped by the noise so
    # they wander rather than radiating like a star, and only out on the
    # flanks -- a summit is a point, and ridges grow away from it.
    angle = np.arctan2(v, u)
    spurs = 1.0 - np.abs(np.sin(angle * 2.5 + rough * 2.0 + 0.7))
    detail = 0.62 + 0.28 * rough + 0.10 * spurs * smoothstep(0.15, 0.7, r)
    return np.clip(base * detail, 0.0, 1.0)


def ridge():
    """A crest along the brush's x, tapering across it and rounded off at the
    ends: dragged, it grows a range; rotated, it lies where you point it. The
    core is clean enough that a check can derive what it covers."""
    rng = np.random.default_rng(SEED + 1)
    u, v, _ = coords()
    across = 1.0 - smoothstep(0.25, 0.75, np.abs(v))
    along = 1.0 - smoothstep(0.75, 1.0, np.abs(u))
    rough = fbm(SIZE, 6, rng, octaves=4, ridged=True)
    return np.clip(across * along * (0.85 + 0.15 * rough), 0.0, 1.0)


def mesa():
    """Flat over most of its width, then a short steep fall: a plateau, or
    the pad a town sits on."""
    rng = np.random.default_rng(SEED + 2)
    u, v, _ = coords()
    d = rounded_box(u, v, 0.92, 0.35)
    edge = 1.0 - smoothstep(-0.22, 0.0, d)
    # The sides wander a little, so a mesa does not read as a stamped cookie.
    return np.clip(edge * (0.93 + 0.07 * fbm(SIZE, 4, rng)), 0.0, 1.0)


def crater():
    """A rim with nothing inside it. Raised it is a crater or a caldera;
    lowered it is a moat, a quarry bench or a ring ditch."""
    rng = np.random.default_rng(SEED + 3)
    _, _, r = coords()
    rim = np.exp(-((r - 0.72) ** 2) / (2.0 * 0.13 ** 2))
    return np.clip(rim * (0.9 + 0.1 * fbm(SIZE, 5, rng)) * (1.0 - smoothstep(0.92, 1.0, r)), 0.0, 1.0)


def pad():
    """Hard and flat to its edge: what Flatten and Set Height want when the
    thing being made is a road, a yard or a foundation."""
    u, v, _ = coords()
    return 1.0 - smoothstep(-0.03, 0.0, rounded_box(u, v, 0.9, 0.12))


def _organic_edge(rng, wobble, cells=3):
    """A disc whose boundary wanders: the radius plus a low-frequency noise,
    which is what makes a stamp read as ground rather than as a cookie
    cutter. Returns the signed distance to that boundary, <= 0 inside."""
    _, _, r = coords()
    return r + wobble * (fbm(SIZE, cells, rng, octaves=3) - 0.5) * 2.0 - 0.78


def soft():
    """A soft irregular blob: an edge you cannot see the end of. The one to
    reach for when a landform should not announce where the brush stopped."""
    rng = np.random.default_rng(SEED + 7)
    d = _organic_edge(rng, 0.22)
    return np.clip((1.0 - smoothstep(-0.55, 0.05, d)) ** 1.3, 0.0, 1.0)


def mid():
    """The same blob with a shorter fall-off and some weather inside it:
    between the soft one and the hard one, which is where most sculpting
    actually happens."""
    rng = np.random.default_rng(SEED + 8)
    d = _organic_edge(rng, 0.26, cells=4)
    body = 1.0 - smoothstep(-0.28, 0.02, d)
    return np.clip(body * (0.78 + 0.22 * fbm(SIZE, 6, rng, octaves=4)), 0.0, 1.0)


def hard():
    """A flat-topped irregular slab with a short steep edge -- a bench, a
    shelf, a lava field. Nearly a pad, but its outline is not a drawing."""
    rng = np.random.default_rng(SEED + 9)
    d = _organic_edge(rng, 0.20, cells=5)
    return np.clip((1.0 - smoothstep(-0.06, 0.0, d)) * (0.94 + 0.06 * fbm(SIZE, 8, rng)), 0.0, 1.0)


def wispy():
    """Filaments: ridged noise pushed to high contrast inside a soft disc.
    Raising through it leaves torn, wind-blown ground rather than a lump --
    the complex end of the library, and the one that stops a hand-sculpted
    hillside looking hand-sculpted."""
    rng = np.random.default_rng(SEED + 10)
    _, _, r = coords()
    coarse = fbm(SIZE, 5, rng, octaves=6, ridged=True)
    fine = fbm(SIZE, 13, rng, octaves=4, ridged=True)
    field = normalise(coarse * 0.6 + fine * 0.4)
    filaments = smoothstep(0.58, 0.93, field)
    glow = 0.18 * field
    return np.clip((filaments + glow) * (1.0 - smoothstep(0.15, 0.95, r)), 0.0, 1.0)


def branch():
    """Wispy's coarser cousin: fewer, thicker limbs that fork. Good for
    spurs running off a ridge, or the arms of a delta when it is lowered."""
    rng = np.random.default_rng(SEED + 11)
    _, _, r = coords()
    field = normalise(fbm(SIZE, 3, rng, octaves=5, ridged=True))
    limbs = smoothstep(0.66, 0.96, field)
    return np.clip((limbs + 0.22 * field ** 2) * (1.0 - smoothstep(0.2, 1.0, r)), 0.0, 1.0)


# --- the patterns -------------------------------------------------------------

def erosion():
    """Gullies: ridged noise squeezed hard across one axis, so lowering
    through it cuts runnels down a slope and raising through it leaves
    spurs. The mask worth Follow stroke."""
    rng = np.random.default_rng(SEED + 4)
    fine = fbm(SIZE, 24, rng, octaves=3, ridged=True)
    coarse = fbm(SIZE, 6, rng, octaves=3, ridged=True)
    # Stretch along x by averaging each row with its neighbours in x only.
    kernel = np.ones(9) / 9.0
    stretched = np.apply_along_axis(lambda row: np.convolve(np.tile(row, 3), kernel, mode="same")[SIZE:2 * SIZE],
                                    axis=1, arr=fine * 0.65 + coarse * 0.35)
    return np.clip(normalise(stretched) ** 1.4, 0.0, 1.0)


def rock():
    """Fine fractal detail, mean around a half: a pass of Raise through it
    roughens a surface that reads too smooth, and Lower through it pits
    one."""
    rng = np.random.default_rng(SEED + 5)
    return np.clip(normalise(fbm(SIZE, 16, rng, octaves=5)), 0.0, 1.0)


def veins():
    """A connected network of ridgelines -- the veins a mountain range makes
    across a map. The cell edges of a wrapped Voronoi diagram: every ridge
    meets its neighbours, so raising through this grows linked spines with
    basins between them rather than a field of separate lumps."""
    rng = np.random.default_rng(SEED + 6)
    points = rng.random((14, 2))
    axis = (np.arange(SIZE) + 0.5) / SIZE
    gx, gz = np.meshgrid(axis, axis)
    # Toroidal distance to every feature point, so the pattern tiles.
    distances = []
    for px, pz in points:
        dx = np.abs(gx - px)
        dz = np.abs(gz - pz)
        dx = np.minimum(dx, 1.0 - dx)
        dz = np.minimum(dz, 1.0 - dz)
        distances.append(np.sqrt(dx * dx + dz * dz))
    stacked = np.sort(np.stack(distances, axis=0), axis=0)
    # The seam between the two nearest cells: bright where they are close.
    edge = stacked[1] - stacked[0]
    ridge_lines = 1.0 - smoothstep(0.0, 0.075, edge)
    rough = fbm(SIZE, 8, rng, octaves=4, ridged=True)
    return np.clip(ridge_lines * (0.7 + 0.3 * rough), 0.0, 1.0)


def dunes():
    """Wind ridges: exactly one crest per tile, a clean raised cosine across
    x and constant along z. Tiled every S metres, the crest lands on x = 0
    (and every multiple of S) and the trough on x = S/2 -- which is what
    check_terrain's pattern claim derives its two pixels from."""
    axis = (np.arange(SIZE) + 0.5) / SIZE
    # meshgrid's first output varies across the columns, which is x -- the
    # crest has to run *down* the image, not across it.
    s, _ = np.meshgrid(axis, axis)
    return (0.5 + 0.5 * np.cos(2.0 * np.pi * s)) ** 1.5


MASKS = {
    "dome": dome,
    "soft": soft,
    "mid": mid,
    "hard": hard,
    "wispy": wispy,
    "branch": branch,
    "mountain": mountain,
    "ridge": ridge,
    "mesa": mesa,
    "crater": crater,
    "pad": pad,
    "erosion": erosion,
    "rock": rock,
    "veins": veins,
    "dunes": dunes,
}


def main():
    BRUSHES.mkdir(parents=True, exist_ok=True)
    for name, build in MASKS.items():
        values = np.clip(build(), 0.0, 1.0)
        image = Image.fromarray((values * 255.0 + 0.5).astype(np.uint8), mode="L")
        image.save(BRUSHES / f"{name}.png", optimize=True)
    print(f"wrote {len(MASKS)} brush masks ({SIZE}x{SIZE}) to {BRUSHES}")


if __name__ == "__main__":
    main()
