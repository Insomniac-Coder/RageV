#!/usr/bin/env python3
"""Every surface the showroom wears, as full PBR map sets, generated here.

    python tools/scripts/make_showroom_textures.py

The argument for generating rather than acquiring is the one
make_camp_textures.py already makes: an asset in this repository needs a
licence and a provenance, and a surface this regular is cheaper to derive than
to buy. **What is different here is the fidelity target.** The camp is
flat-shaded low-poly and wants one colour map per surface; a showroom is
manufactured panels under a large soft source, photographed, and it wants the
whole set -- albedo, normal, roughness, occlusion and height.

Four surfaces and two overlays:

  * **floor** (2048) -- polished charcoal porcelain, 600 mm tiles. Albedo,
    normal, roughness, occlusion and height. This is the hero surface: it is
    what the ceiling luminaire reflects in, and it is seen at a grazing angle
    across the whole frame, which is the one viewing condition that punishes
    every shortcut at once.
  * **wall** (2048) -- painted panel, 1.2 m joints, near white. Albedo, normal,
    roughness.
  * **concrete** (1024) -- the service bay behind. Dim, so it is smaller.
  * **panel** (1024) -- the luminaire itself. Its albedo doubles as its
    emissive map, so the mullions stay dark while the cells blow out.
  * **button** (120x30) -- the lights switch's plate, RGBA. Not a surface at
    all: it is drawn by the canvas, and the rule it follows is different (see
    the overlay section below).

**The roughness map is the one that decides whether this looks real.** A
polished floor at a *uniform* roughness is a mirror, and a mirror shows a
second car rather than a reflection of one. Every one of these varies broadly
and at low contrast, which is what a manufactured surface actually does and
what keeps a reflection legible without it becoming a copy.

**Normals are OpenGL convention** -- green points up in texture space -- which
is what `UnpackNormal` in pbr_fragment.glsl reads. It samples only XY and
rebuilds Z, so the blue channel is written for a human opening the file rather
than for the shader.

**Seamless by construction, not by blending.** Every grid line is placed by
modular arithmetic over the tile and every noise field is summed from
wrapping sinusoids, so the last column is the first column's neighbour because
the numbers say so.
"""

import argparse
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from make_camp_textures import png, write_meta                  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "SampleProject" / "assets"

# Reproducible: two runs produce the same bytes, so a diff means the recipe
# changed rather than that the file was rewritten.
SEED = 0x5307

# How many metres of surface one tile of each map covers. These are what turn a
# height field into a slope, and they are also what the scene's Tiling values
# have to agree with -- a map is only the right size on a wall if the two
# numbers were derived from one another.
FLOOR_SPAN = 9.0        # 15 tiles at 600 mm
WALL_SPAN = 4.8         # 4 panels at 1.2 m
CONCRETE_SPAN = 3.0
PANEL_SPAN = 8.0        # the luminaire is one tile across its whole width


# --- fields -------------------------------------------------------------------

def wrapping_noise(size, rng, octaves, base=4, gain=0.5):
    """Value noise that tiles, built from wrapping sinusoids.

    A lattice-interpolated noise tiles too, and is more work; this is a sum of
    products of sines whose frequencies are whole numbers of cycles across the
    tile, which wraps for the same reason a Fourier series does. It has no
    sharp features, which is right for everything it is used for here --
    mottling, polish variation, the slow swell in a floor that has been walked
    on.
    """
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    u = x / size * 2.0 * np.pi
    v = y / size * 2.0 * np.pi

    total = np.zeros((size, size), np.float32)
    amplitude = 1.0
    weight = 0.0

    for octave in range(octaves):
        frequency = base * (2 ** octave)
        for _ in range(3):
            # Random whole-number frequencies and a random phase: whole numbers
            # so it wraps, random so the result does not look like a plaid.
            fx = int(rng.integers(0, frequency + 1))
            fy = int(rng.integers(0, frequency + 1))
            phase = float(rng.uniform(0.0, 2.0 * np.pi))
            total += amplitude * np.sin(fx * u + fy * v + phase)

        weight += amplitude * 3.0
        amplitude *= gain

    return total / weight * 0.5 + 0.5


def grid(size, count, bevel, joint):
    """A tile grid's height field and its cell indices.

    Height is 1 over the face of a tile, falls through a bevel at its edge and
    sits at 0 in the joint. `bevel` and `joint` are fractions of one cell.
    """
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    step = size / count

    fx = np.minimum(x % step, step - (x % step))
    fy = np.minimum(y % step, step - (y % step))
    distance = np.minimum(fx, fy) / step          # 0 at a joint centre

    half = joint * 0.5
    height = np.clip((distance - half) / max(bevel, 1e-6), 0.0, 1.0)
    # Smoothstep, so the bevel is a rolled edge rather than a chamfer -- which
    # is what a fired tile has, and what makes the highlight along a joint a
    # line rather than a facet.
    height = height * height * (3.0 - 2.0 * height)

    column = np.minimum((x / step).astype(np.int32), count - 1)
    row = np.minimum((y / step).astype(np.int32), count - 1)

    return height, row, column


def normal_map(height, relief, span):
    """Tangent-space normal from a height field, OpenGL convention.

    **`relief` is in metres and `span` is how many metres of surface the tile
    covers**, which is the only pair of numbers that makes the slope right. A
    relief given as a fraction of the texture is the mistake worth naming: the
    first cut of this file used one, and a grout line 1.5 mm deep came out
    nine centimetres deep -- a 79-degree wall around every tile, which under a
    large soft source reads as a floor made of shallow boxes.

    Expressed this way the map is also resolution-independent by construction:
    doubling `size` halves the texel and doubles the gradient, and the two
    cancel.
    """
    size = height.shape[0]
    scale = relief * size / span

    # Central differences, wrapped, so the derivative is seamless too. Getting
    # this wrong leaves a one-texel line of wrong normals at the seam, which
    # under a large bright source is a visible crack.
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5 * scale
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5 * scale

    # Y is negated: texture rows run downwards and the convention has green
    # pointing up.
    nx, ny, nz = -dx, dy, np.ones_like(height)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)

    return np.stack([nx / length * 0.5 + 0.5,
                     ny / length * 0.5 + 0.5,
                     nz / length * 0.5 + 0.5], axis=-1)


def cavity(height, radius):
    """A cheap ambient occlusion: how far below its neighbourhood a texel sits.

    Blurring by successive rolls rather than by a convolution, because the only
    thing this needs is a wide low-frequency average and eight shifted copies
    give one that tiles.
    """
    blurred = np.zeros_like(height)
    for dx, dy in ((radius, 0), (-radius, 0), (0, radius), (0, -radius),
                   (radius, radius), (-radius, -radius),
                   (radius, -radius), (-radius, radius)):
        blurred += np.roll(np.roll(height, dy, axis=0), dx, axis=1)
    blurred /= 8.0

    return np.clip(1.0 - (blurred - height) * 2.2, 0.0, 1.0)


# --- surfaces -----------------------------------------------------------------

def floor(size, rng):
    """Polished charcoal porcelain, 600 mm tiles over a 9 m span."""
    count = 15
    height, row, column = grid(size, count, bevel=0.030, joint=0.016)

    # Per-tile value. Two percent, which is nothing on a swatch and is the
    # whole difference between a tiled floor and a painted one across nine
    # metres of it.
    tint = rng.uniform(0.053, 0.062, size=(count, count))[row, column]

    # The stone in the tile: a slow mottle, and it is per-tile rather than
    # continuous because a tile is cut from a slab and the pattern stops at
    # its edge.
    mottle = wrapping_noise(size, rng, octaves=4, base=count)
    value = tint * (0.86 + 0.28 * mottle)

    # The joint. Lighter than the tile and much rougher, which is the way round
    # a real dark floor goes and is what makes the grid visible in a reflection.
    joint = height < 0.5
    albedo = np.where(joint, 0.075 + 0.01 * mottle, value)
    colour = np.stack([albedo, albedo * 1.015, albedo * 1.07], axis=-1)

    # Roughness. Low over the face, high in the joint, and varying per tile by
    # more than the albedo does -- a tile a percent darker is invisible, a tile
    # three percent rougher visibly changes how the luminaire smears across it.
    # **0.14 to 0.22, not 0.08 to 0.14.** The polished version was a mirror in
    # everything but name: the car came back as a second car rather than as a
    # reflection, the luminaire's grid aliased into a dashed line at grazing
    # angles, and the traced reflection smeared vertically because there was
    # no roughness for it to converge into. Porcelain is polished, not
    # optically flat, and this is where the two part company.
    polish = rng.uniform(0.14, 0.22, size=(count, count))[row, column]
    walked = wrapping_noise(size, rng, octaves=3, base=2)
    rough = polish + (walked - 0.5) * 0.055
    rough = np.where(joint, 0.62, np.clip(rough, 0.03, 1.0))

    # Fine relief on the face as well as at the joints: the surface is polished,
    # not optically flat, and this is what breaks the reflection up at the
    # grazing angles the whole frame is seen at.
    grain = (wrapping_noise(size, rng, octaves=5, base=32) - 0.5) * 0.04
    relief = height + grain * (height > 0.5)

    return {
        "albedo": colour,
        # 1.5 mm of grout depth over nine metres of floor, which is what a
        # 600 mm porcelain tile actually sits proud of its joint by.
        "normal": normal_map(relief, 0.0015, FLOOR_SPAN),
        "rough": rough,
        "ao": cavity(height, max(2, size // 256)),
        # Parallax reads this. The joint is what gains from it: at a grazing
        # angle a grout line with real depth occludes, and a painted-on one
        # does not.
        "height": height * 0.86 + 0.14,
    }


def wall(size, rng):
    """Painted panel, 1.2 m joints over a 4.8 m span, near white."""
    count = 4
    height, row, column = grid(size, count, bevel=0.006, joint=0.004)

    panel = rng.uniform(0.845, 0.870, size=(count, count))[row, column]

    # Orange peel. Every sprayed surface has it, it is invisible in the albedo
    # and it is the entire reason a painted wall under a big soft light does
    # not read as a flat shaded polygon.
    peel = wrapping_noise(size, rng, octaves=4, base=24)

    value = panel + (peel - 0.5) * 0.008
    value = np.where(height < 0.5, 0.79, value)

    colour = np.stack([value, value, value * 1.004], axis=-1)

    # The joint carries the relief and the peel is a *sixteenth* of it: orange
    # peel is a tenth of a millimetre, and the only reason it is in the height
    # field at all is that it breaks the specular into something that moves.
    relief = height + peel * 0.06

    # **0.66, not 0.42.** These walls are black now, and a black wall at 0.42
    # is not matte paint -- it is a dark mirror, which is what put a smeared
    # orange streak of the luminaire across an otherwise empty wall. Matte
    # emulsion is around 0.7 and reflects the room as a wash rather than as an
    # image of it.
    rough = 0.66 + (peel - 0.5) * 0.10
    rough = np.where(height < 0.5, 0.78, rough)

    return {
        "albedo": colour,
        "normal": normal_map(relief, 0.002, WALL_SPAN),
        "rough": rough,
    }


def concrete(size, rng):
    """Sealed floor-to-ceiling concrete, for the service bay behind."""
    broad = wrapping_noise(size, rng, octaves=3, base=3)
    fine = wrapping_noise(size, rng, octaves=5, base=20)

    # Pitting: the small dark pinholes a poured surface has. Sparse, because
    # evenly distributed pitting reads as noise rather than as concrete.
    pits = (wrapping_noise(size, rng, octaves=2, base=48) > 0.80).astype(np.float32)

    value = 0.20 + broad * 0.055 + (fine - 0.5) * 0.03 - pits * 0.05
    colour = np.stack([value * 1.02, value, value * 0.97], axis=-1)

    relief = broad * 0.5 + fine * 0.5 - pits * 0.25
    rough = np.clip(0.74 + (fine - 0.5) * 0.16 + pits * 0.08, 0.0, 1.0)

    return {
        "albedo": colour,
        "normal": normal_map(relief, 0.004, CONCRETE_SPAN),
        "rough": rough,
    }


def panel(size):
    """The ceiling luminaire: white cells, dark mullions.

    Nothing random in it, and that is the point -- a light fitting is the one
    surface in a room that is *exactly* regular, and jitter in it reads as a
    defect rather than as texture.
    """
    count = 12
    # A thin frame and a crisp edge. The first cut used a wide bevel and a wide
    # joint, and every cell came out as a rounded blob -- a light fitting is
    # rectangles, and softening them is the one thing that stops it reading as
    # one.
    height, _, _ = grid(size, count, bevel=0.008, joint=0.038)

    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    step = size / count

    # The cell falls off slightly towards its own edge, the way a diffuser
    # does. Subtle, and it is the difference between a grid of white rectangles
    # and a grid of lit panels.
    ux = (x % step) / step - 0.5
    uy = (y % step) / step - 0.5
    diffuser = 1.0 - 0.05 * np.clip((ux * ux + uy * uy) * 4.0, 0.0, 1.0)

    # The mullion is 18% grey. Dark enough to read as a frame against a cell
    # that is blowing out, and light enough that it is not a hole in the
    # ceiling -- which is what the reference photograph has and what 5% grey
    # gave instead.
    value = height * diffuser + (1.0 - height) * 0.18

    # A hair cooler than neutral, which is what a 5000 K fitting is next to a
    # warm white wall.
    colour = np.stack([value * 0.982, value * 0.993, value], axis=-1)

    return {
        "albedo": colour,
        "normal": normal_map(height, 0.004, PANEL_SPAN),
        # The mullion is a satin extrusion and the cell is a matte diffuser.
        "rough": np.where(height < 0.5, 0.35, 0.92),
    }


# --- the overlay -------------------------------------------------------------
#
# **Authored at the size they are drawn**, which is a rule the room's maps do
# not follow and this one has to. UIRenderer's sampler is built with
# `MaxLod = 0` -- there are no mips on the UI path, because a glyph atlas must
# not have any -- so a UI image drawn smaller than it was authored is point
# sampled from the top level and a one-pixel border crawls. The canvas is
# 1920x1080 reference and these are the pixel sizes at that reference, so on a
# 1080p screen every texel lands on a pixel and anything larger magnifies,
# which bilinear does cleanly.

def rounded_rect(width, height, radius, inset=0.0):
    """Signed distance to a rounded rectangle, in pixels, negative inside."""
    y, x = np.mgrid[0:height, 0:width].astype(np.float32)

    # Texel centres. Off by half a pixel and the shape is asymmetric by one
    # texel on two of its four sides, which on a thirty-pixel-tall plate is a
    # visibly heavier bottom edge.
    dx = np.abs(x + 0.5 - width * 0.5) - (width * 0.5 - inset - radius)
    dy = np.abs(y + 0.5 - height * 0.5) - (height * 0.5 - inset - radius)

    outside = np.hypot(np.maximum(dx, 0.0), np.maximum(dy, 0.0))
    inside = np.minimum(np.maximum(dx, dy), 0.0)
    return outside + inside - radius


def button_plate(width, height):
    """The lights switch's background: a pill, white, with the shape in alpha.

    **White and nothing else, which is the whole design.** A UI Button
    multiplies its Normal, Hover and Pressed tints into whatever image is on
    the same entity, and multiplying white *is* the tint -- so grey at rest and
    white under the pointer are one image and three numbers in the scene, with
    no script involved and nothing to keep in step.

    Colour baked into the plate would fight that: a dark fill tinted by 0.55
    comes out near black rather than grey, and a lighter border tinted the same
    way inverts when the plate goes white. The alpha carries the shape, the
    scene carries the colour, and neither has an opinion about the other.
    """
    # Inset by a texel so the antialiased edge has somewhere to fall off. Run
    # flush to the image border and the outermost texel is left half covered,
    # which draws the pill with a hard clipped edge.
    distance = rounded_rect(width, height, radius=height * 0.5 - 1.0, inset=1.0)
    alpha = np.clip(0.5 - distance, 0.0, 1.0)

    return np.concatenate([np.ones((height, width, 3), np.float32),
                           alpha[..., None]], axis=-1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=str(ASSETS / "textures"))
    args = parser.parse_args()

    directory = pathlib.Path(args.output)
    directory.mkdir(parents=True, exist_ok=True)

    rng = np.random.default_rng(SEED)

    # 2048 where the surface fills the frame and is seen at a grazing angle,
    # 1024 where it is dim or blown out. Resolution is not free in a
    # repository, and these are the two questions that decide whether it buys
    # anything.
    surfaces = {
        "showroom_floor": floor(2048, rng),
        "showroom_wall": wall(2048, rng),
        "showroom_concrete": concrete(1024, rng),
        "showroom_panel": panel(1024),

        # One RGBA overlay rather than a map set, at the size it is drawn:
        # 120x30 canvas units, which make_showroom_scene.py has as
        # BUTTON_WIDTH and BUTTON_HEIGHT. The two files have to agree, for the
        # reason given above -- there are no mips on the UI path.
        "showroom_button": {"plate": button_plate(120, 30)},
    }

    total = 0
    for name, maps in surfaces.items():
        for suffix, image in maps.items():
            path = directory / f"{name}_{suffix}.png"
            png(path, image)
            handle = write_meta(path, "Texture")
            total += path.stat().st_size
            print(f"{path.name:<34} {path.stat().st_size:>10,} bytes  {handle}")

    print(f"{'':<34} {total:>10,} bytes total")


if __name__ == "__main__":
    main()
