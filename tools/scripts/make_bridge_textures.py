#!/usr/bin/env python3
"""The Golden Gate's four surfaces, as full PBR map sets.

    python tools/scripts/make_bridge_textures.py
    python tools/scripts/make_bridge_models.py    # writes the .rmat that use them

Generated rather than acquired, for the reason `make_camp_textures.py` gives:
an asset in this repository needs a licence and a provenance, and a surface
this regular is cheaper to derive than to buy.

**What each surface actually is**, because the maps follow from that and not
from a look:

- **Painted steel.** Riveted plate, not sheet: the tower shafts and the truss
  are built up from plates lapped and driven with rivets on about a 100 mm
  pitch, and the seams between plates are what the eye counts to judge how big
  the thing is. The paint over them is a maintained gloss that chalks where it
  faces the weather -- so roughness is not one number, it is a slow field with
  the rivet heads picked out sharper because paint thins over a curve. The
  colour is International Orange, and its reflectance peaks at 600 nm, right
  where a sodium lamp emits.
- **Concrete.** Board-formed on the piers and pylons: horizontal form lines,
  the aggregate showing through where the surface has weathered, and the dark
  vertical streaks below every ledge that any concrete over water grows.
- **Asphalt.** Aggregate in bitumen, and the thing that makes a road read as a
  road is *not* the grain -- it is that the wheel tracks are polished smoother
  and darker than the crown between them. Two bands per lane, and the
  roughness map is where they live.
- **Lane paint.** Thermoplastic, laid thick enough to stand above the asphalt,
  worn thin down the middle of each line by the traffic that crosses it.

**Roughness is the map that decides whether this looks real.** A painted steel
box at one roughness is a plastic toy; what says "painted metal outdoors" is
gloss that varies broadly at low contrast with the rivets and the seams
breaking it up at high frequency.

**Normals are OpenGL convention** -- green up in texture space -- which is what
`UnpackNormal` in pbr_fragment.glsl reads.

**Seamless by construction.** Every feature is placed by modular arithmetic
over the tile and every noise field is a sum of wrapping sinusoids, so the
last column is the first column's neighbour because the numbers say so.

**And seamless is not the same as not-obviously-repeating**, which is the
mistake this file made first. A tile can have a perfect edge and still draw a
grid across a sixty-metre pier, because what the eye picks up is not the seam
-- it is the *content*. Anything in the tile bigger than a fraction of it
becomes a feature that appears once per repeat, in rows: a soft dark blob at
`base=2`, or worse, a gradient across the tile, is a pattern the moment the
surface is larger than one repeat.

So the rule here is **no low frequencies and no gradients**. Every field
starts at `base=8` or above, so nothing in the tile is bigger than an eighth
of it; the broad variation a real surface has is left to the geometry and the
lighting, which do not repeat. What stays in the texture is the detail that
is *supposed* to look the same everywhere -- aggregate, rivets, board lines,
grain -- because that is exactly the content a repeat cannot betray.
"""

import argparse
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from make_camp_textures import png, write_meta                    # noqa: E402
from make_showroom_textures import (wrapping_noise, normal_map,   # noqa: E402
                                    cavity)

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "SampleProject" / "assets"
SEED = 41


def rivet_field(size, span, pitch, radius, rows):
    """Rivet heads on a grid, as a height field in metres.

    `pitch` and `radius` are metres; `rows` places lines of them along the
    plate seams rather than over the whole face, because that is where rivets
    actually are -- a rivet in the middle of a plate is holding nothing.
    """
    height = np.zeros((size, size), np.float32)
    per = max(int(round(span / pitch)), 1)
    step = size / per
    r = radius * size / span

    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    for row in rows:
        cy = row * size
        for i in range(per):
            cx = (i + 0.5) * step
            dx = np.minimum(np.abs(x - cx), size - np.abs(x - cx))
            dy = np.minimum(np.abs(y - cy), size - np.abs(y - cy))
            d = np.sqrt(dx * dx + dy * dy) / r
            # A spherical cap, so the head is domed rather than a cylinder.
            height += np.clip(1.0 - d * d, 0.0, 1.0) ** 0.5 * radius
    return height


def seam_field(size, count, depth, width):
    """Horizontal plate laps: a step down, then back up, `count` times."""
    y = np.mgrid[0:size, 0:size][0].astype(np.float32) / size
    height = np.zeros((size, size), np.float32)
    for i in range(count):
        centre = (i + 0.5) / count
        d = np.abs(((y - centre + 0.5) % 1.0) - 0.5)
        height -= depth * np.clip(1.0 - d / width, 0.0, 1.0)
    return height


def painted_steel(size, rng):
    """International Orange over riveted plate.

    The span is 3 m of real surface across the tile, which is about one plate:
    at that scale a 20 mm rivet is seven texels at 1024 and the seams are
    where a plate actually laps.
    """
    span = 3.0

    seams = seam_field(size, 3, 0.006, 0.010)
    rivets = rivet_field(size, span, 0.11, 0.010,
                         rows=(0.166, 0.5, 0.833))
    # The plate is not flat, but its waviness stays *small*: a slow swell at
    # base=2 is one blob per tile, and one blob per tile is a grid.
    swell = (wrapping_noise(size, rng, 3, base=9) - 0.5) * 0.0022
    height = seams + rivets + swell

    # **The albedo is nearly uniform and that is correct.** Paint is paint;
    # what varies is how much has chalked, which lightens and desaturates it,
    # and the rust bloom that starts at a seam.
    chalk = wrapping_noise(size, rng, 4, base=10)
    chalk = np.clip((chalk - 0.46) * 1.9, 0.0, 1.0)
    rust = np.clip((wrapping_noise(size, rng, 5, base=14) - 0.68) * 6.0, 0.0, 1.0)
    rust *= np.clip(-seams / 0.006, 0.0, 1.0) * 0.8 + 0.2

    base = np.array([0.527, 0.037, 0.025], np.float32)
    chalked = np.array([0.560, 0.150, 0.120], np.float32)
    rusted = np.array([0.190, 0.055, 0.028], np.float32)

    colour = (base[None, None, :] * (1.0 - chalk[:, :, None])
              + chalked[None, None, :] * chalk[:, :, None])
    colour = colour * (1.0 - rust[:, :, None]) + rusted[None, None, :] * rust[:, :, None]

    # Paint thins over a rivet head, so the head reads a touch lighter.
    colour *= (1.0 + np.clip(rivets / 0.010, 0.0, 1.0)[:, :, None] * 0.10)

    # Gloss where the paint is sound, matt where it has chalked, and matt in
    # the seams where dirt collects.
    rough = 0.34 + 0.30 * chalk + 0.22 * rust
    rough += (wrapping_noise(size, rng, 3, base=11) - 0.5) * 0.10
    rough += np.clip(-seams / 0.006, 0.0, 1.0) * 0.14
    rough -= np.clip(rivets / 0.010, 0.0, 1.0) * 0.06

    return {
        "color": np.clip(colour, 0.0, 1.0),
        "normal": normal_map(height, 1.0, span),
        "roughness": np.clip(rough, 0.05, 0.95),
        "ao": cavity(height, max(size // 128, 2)),
    }


def board_concrete(size, rng):
    """Board-formed concrete, weathered, over 2 m of surface."""
    span = 2.0

    # **Five boards, not eight, and shallower.** At eight the seams are close
    # enough that a 60 m pier tiled across them reads as a diamond lattice
    # rather than as concrete -- the repeat announces itself, which is the one
    # thing a tiling texture must not do.
    boards = seam_field(size, 5, 0.0022, 0.012)
    # Aggregate showing where the skin has weathered off.
    grain = wrapping_noise(size, rng, 6, base=14)
    exposure = np.clip((wrapping_noise(size, rng, 3, base=9) - 0.5) * 2.2, 0.0, 1.0)
    height = boards + (grain - 0.5) * 0.0026 * (0.35 + exposure)

    # **Concrete is not white.** Weathered structural concrete sits around a
    # linear 0.28-0.32 -- it reads pale against dark water, not against the
    # sky, and at 0.455 under a 2.6-intensity sun the pylons blew out to
    # near-white and lost every step in them.
    pale = np.array([0.300, 0.294, 0.278], np.float32)
    dark = np.array([0.163, 0.160, 0.153], np.float32)

    # The staining. **Not a gradient down the tile**, which is what this was:
    # multiplying by `y` puts a light band at every repeat's top edge and a
    # dark one at its foot, and eight repeats up a pylon is eight stripes.
    # Fine, vertical, high-frequency streaking reads as weathering and cannot
    # betray the repeat.
    streak = wrapping_noise(size, rng, 4, base=12)
    streak = np.clip((streak - 0.52) * 2.6, 0.0, 1.0)

    mix = np.clip(0.26 * grain + 0.38 * streak + 0.18 * exposure, 0.0, 1.0)
    colour = pale[None, None, :] * (1.0 - mix[:, :, None]) + dark[None, None, :] * mix[:, :, None]

    rough = 0.74 + 0.16 * grain - 0.10 * streak
    return {
        "color": np.clip(colour, 0.0, 1.0),
        "normal": normal_map(height, 1.0, span),
        "roughness": np.clip(rough, 0.35, 0.98),
        "ao": cavity(height, max(size // 96, 2)),
    }


def asphalt(size, rng):
    """Aggregate in bitumen, with the wheel tracks polished into it.

    The tile spans 4 m across the road, which puts the two tracks of a lane
    inside it: a lane is 3.03 m and the tracks are about 1.8 m apart.
    """
    span = 4.0

    grain = wrapping_noise(size, rng, 7, base=18)
    coarse = wrapping_noise(size, rng, 4, base=11)
    height = (grain - 0.5) * 0.0055 + (coarse - 0.5) * 0.0016

    # **The wheel tracks are not in here, and that was a mistake to try.**
    # They are a feature of the *road* -- two bands per lane, at fixed
    # positions across a 3.03 m lane -- and a tiling texture has no idea where
    # it is on the deck. Baked in, they repeated across and along at four
    # metres and read as a chequer. They belong in the model, as a separate
    # strip of darker, smoother surface, or in a mask the shader can place.
    track = np.zeros((size, size), np.float32)

    dark = np.array([0.038, 0.038, 0.041], np.float32)
    pale = np.array([0.085, 0.083, 0.080], np.float32)
    colour = dark[None, None, :] + (pale - dark)[None, None, :] * grain[:, :, None]
    colour *= (1.0 - 0.30 * track)[:, :, None]

    rough = 0.90 - 0.30 * track + (grain - 0.5) * 0.10
    return {
        "color": np.clip(colour, 0.0, 1.0),
        "normal": normal_map(height * (1.0 - 0.55 * track), 1.0, span),
        "roughness": np.clip(rough, 0.30, 0.99),
        "ao": cavity(height, max(size // 160, 2)),
    }


def lane_paint(size, rng):
    """Thermoplastic, laid thick and worn thin where tyres cross it."""
    span = 1.0

    wear = wrapping_noise(size, rng, 5, base=13)
    thin = np.clip((wear - 0.44) * 1.9, 0.0, 1.0)
    height = (1.0 - thin) * 0.0022 + (wrapping_noise(size, rng, 6, base=20) - 0.5) * 0.0006

    white = np.array([0.740, 0.730, 0.700], np.float32)
    grey = np.array([0.300, 0.298, 0.292], np.float32)
    colour = white[None, None, :] * (1.0 - thin[:, :, None] * 0.75) \
        + grey[None, None, :] * (thin[:, :, None] * 0.75)

    rough = 0.62 + 0.28 * thin
    return {
        "color": np.clip(colour, 0.0, 1.0),
        "normal": normal_map(height, 1.0, span),
        "roughness": np.clip(rough, 0.35, 0.96),
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=str(ASSETS / "textures"))
    args = parser.parse_args(argv)

    directory = pathlib.Path(args.output)
    directory.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(SEED)

    # 1024 for the steel, which is on screen at every distance from a
    # kilometre to arm's length, and 512 for the rest. Resolution is not free
    # in a repository, and that is the question that decides it.
    surfaces = {
        "bridge_steel": painted_steel(1024, rng),
        "bridge_concrete": board_concrete(512, rng),
        "bridge_asphalt": asphalt(512, rng),
        "bridge_paint": lane_paint(256, rng),
    }

    total = 0
    for name, maps in surfaces.items():
        for suffix, image in maps.items():
            path = directory / "{0}_{1}.png".format(name, suffix)
            png(path, image)
            handle = write_meta(path, "Texture")
            total += path.stat().st_size
            print("{0:<32} {1:>10,} bytes  {2}".format(
                path.name, path.stat().st_size, handle))

    print("{0:<32} {1:>10,} bytes total".format("", total))


if __name__ == "__main__":
    main()
