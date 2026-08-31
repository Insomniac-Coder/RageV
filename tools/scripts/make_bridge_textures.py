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


def seam_field(size, count, depth, width, rng=None):
    """Horizontal laps: a step down and back up, `count` times.

    Boards are not milled to one width and plates are not laid to one line.
    Given an `rng` each seam gets its own offset, depth and width, which is
    the difference between a surface and a sheet of ruled paper.
    """
    y = np.mgrid[0:size, 0:size][0].astype(np.float32) / size
    height = np.zeros((size, size), np.float32)
    for i in range(count):
        centre = (i + 0.5) / count
        scale, spread, shift = 1.0, 1.0, 0.0
        if rng is not None:
            scale = float(rng.uniform(0.55, 1.45))
            spread = float(rng.uniform(0.7, 1.35))
            shift = float(rng.uniform(-0.35, 0.35)) / count
        d = np.abs(((y - centre - shift + 0.5) % 1.0) - 0.5)
        height -= depth * scale * np.clip(1.0 - d / (width * spread), 0.0, 1.0)
    return height


def streaks(size, rng, density, length, sharpness=2.0):
    """Vertical dirt runs: thin, high-frequency, and therefore tile-safe.

    Weathering on a vertical surface runs *down*, and the runs are narrow.
    That narrowness is what lets them carry the "this has been outside for
    ninety years" reading without putting a low-frequency feature in the tile:
    a streak two texels wide repeated eight times across a pier is invisible
    as a repeat and unmistakable as grime.
    """
    # Narrow in x, long in y: a noise stretched along the run.
    fine = wrapping_noise(size, rng, 4, base=density)
    smear = np.zeros((size, size), np.float32)
    steps = max(int(size * length), 1)
    for i in range(steps):
        smear += np.roll(fine, i, axis=0) * (1.0 - i / steps)
    smear /= max(steps * 0.5, 1.0)
    return np.clip((smear - 0.45) * sharpness, 0.0, 1.0)


def stud_grid(size, span, pitch, radius, depth, rng=None, offset=(0.5, 0.5)):
    """Recessed circles on a grid -- form-tie holes, bolt heads.

    **Regular by nature, so it tiles honestly** -- the features a repeat
    cannot betray are the ones genuinely identical everywhere, and a form-tie
    plug is set out on the formwork's own grid.

    **But identical is not the same as regular, and a perfect grid is the
    thing that reads as graph paper.** Real plugs sit a few centimetres off
    where the drawing put them, some were never filled, and only a few have
    started to weep. So each one gets its own jitter, size and weight. All of
    it is inside the tile, so none of it costs a repeat -- what it buys is a
    field the eye reads as "many plugs" rather than "a pattern of plugs".

    Returns (field, stained) -- the second being the subset that has rusted,
    so a caller can hang the weep off those alone.
    """
    per = max(int(round(span / pitch)), 1)
    step = size / per
    r = max(radius * size / span, 1.5)
    if rng is None:
        rng = np.random.default_rng(1)

    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    field = np.zeros((size, size), np.float32)
    stained = np.zeros((size, size), np.float32)
    for j in range(per):
        for i in range(per):
            if rng.random() < 0.12:
                continue                        # never filled, or spalled away
            jitter = step * 0.16
            cx = (i + offset[0]) * step + float(rng.uniform(-jitter, jitter))
            cy = (j + offset[1]) * step + float(rng.uniform(-jitter, jitter))
            scale = float(rng.uniform(0.72, 1.28))
            weight = float(rng.uniform(0.55, 1.0))
            dx = np.minimum(np.abs(x - cx), size - np.abs(x - cx))
            dy = np.minimum(np.abs(y - cy), size - np.abs(y - cy))
            d = np.sqrt(dx * dx + dy * dy) / (r * scale)
            plug = np.clip(1.0 - d * d, 0.0, 1.0) ** 0.6 * weight
            field = np.maximum(field, plug)
            if rng.random() < 0.35:
                stained = np.maximum(stained, plug)
    return field * depth, stained * depth


def painted_steel(size, rng):
    """International Orange over riveted plate.

    The span is 3 m of real surface across the tile, which is about one plate:
    at that scale a 20 mm rivet is seven texels at 1024 and the seams are
    where a plate actually laps.
    """
    span = 3.0

    seams = seam_field(size, 3, 0.008, 0.009, rng)
    rivets = rivet_field(size, span, 0.11, 0.013,
                         rows=(0.166, 0.5, 0.833))
    # A second row of heads down the vertical plate edges: a lapped plate is
    # riveted on all four sides, and from close in that is what the eye reads.
    edges = np.transpose(rivet_field(size, span, 0.13, 0.011,
                                     rows=(0.25, 0.75)))
    # The plate is not flat, but its waviness stays *small*: a slow swell at
    # base=2 is one blob per tile, and one blob per tile is a grid.
    swell = (wrapping_noise(size, rng, 3, base=9) - 0.5) * 0.0022
    # **Orange peel.** Sprayed paint is not glass: it dries with a fine
    # dimpling about a millimetre across, and that dimpling is most of why a
    # painted surface scatters its highlight instead of mirroring it. Without
    # it the normal is flat between the rivets and the whole plate reads as
    # moulded plastic.
    peel = (wrapping_noise(size, rng, 2, base=52) - 0.5) * 0.00055
    height = seams + rivets + edges + swell + peel

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

    # **Plate to plate, not texel to texel.** The tile holds four strips of
    # plate between its seams, and on a real structure each was last painted
    # in a different year: a couple of percent of brightness between them is
    # what stops a large surface reading as one moulded piece. Four strips
    # inside the tile is a feature *of* the tile, so it costs no repeat.
    y = np.mgrid[0:size, 0:size][0].astype(np.float32)
    strip = np.floor(y / size * 4.0).astype(np.int32)
    ages = np.array([1.00, 0.94, 1.05, 0.97], np.float32)
    colour *= ages[strip][:, :, None]

    # Paint thins over a rivet head, so the head reads a touch lighter.
    colour *= (1.0 + np.clip((rivets + edges) / 0.012, 0.0, 1.0)[:, :, None] * 0.12)

    # Ninety years of salt air: dirt runs down from every seam.
    grime = streaks(size, rng, 26, 0.16, 2.4)
    colour *= (1.0 - grime[:, :, None] * 0.30)

    # **Semi-gloss, and this is the number that decides whether it is metal.**
    # It sat at 0.34 rising to 0.7 with the chalk, and at 0.7 there is no
    # highlight and no sky in the surface -- a dielectric with no specular
    # response is precisely what plastic looks like. Maintained bridge paint
    # is 0.25-0.35; chalked paint gets to 0.55 and no further, because paint
    # that rough has failed and would have been stripped.
    rough = 0.24 + 0.20 * chalk + 0.22 * rust + 0.16 * grime
    rough += (wrapping_noise(size, rng, 3, base=11) - 0.5) * 0.07
    rough += np.clip(-seams / 0.008, 0.0, 1.0) * 0.10
    # Paint pools slightly at a rivet's shoulder and thins over its crown, so
    # the head is the glossiest thing on the plate.
    rough -= np.clip((rivets + edges) / 0.012, 0.0, 1.0) * 0.09

    return {
        "color": np.clip(colour, 0.0, 1.0),
        "normal": normal_map(height, 1.0, span),
        "roughness": np.clip(rough, 0.14, 0.62),
        "ao": cavity(height, max(size // 128, 2)),
    }


def board_concrete(size, rng):
    """Board-formed concrete, weathered, over 2.4 m of surface.

    **The span matches the uv the model asks for**, so the 600 mm tie grid in
    here is 600 mm on the pier rather than 960: a texture designed at one
    scale and applied at another is a texture whose every real-world
    dimension is a lie.
    """
    span = 2.4

    # **Five boards, not eight, and shallower.** At eight the seams are close
    # enough that a 60 m pier tiled across them reads as a diamond lattice
    # rather than as concrete -- the repeat announces itself, which is the one
    # thing a tiling texture must not do.
    boards = seam_field(size, 4, 0.0018, 0.009, rng)

    # **Pour lines.** The horizontal joint where one lift of concrete met the
    # next: about 1.2 m apart on a pier this size, and the third feature that
    # is regular by nature and so tiles honestly. What shows is not the joint
    # itself -- it is a thin dark line of laitance along it, a slight ridge
    # where the form leaked, and the fact that the lift above was a different
    # batch from the lift below.
    lifts = 2
    yy = np.mgrid[0:size, 0:size][0].astype(np.float32) / size
    lift_index = np.floor(yy * lifts).astype(np.int32)
    # A pour line wanders: the form was levelled by eye and the concrete
    # found its own top. A slow waver along the joint, high enough in
    # frequency to stay tile-safe.
    waver = (wrapping_noise(size, rng, 2, base=9) - 0.5) * 0.016
    joint = np.zeros((size, size), np.float32)
    for i in range(lifts):
        centre = i / lifts
        d = np.abs(((yy - centre - waver + 0.5) % 1.0) - 0.5)
        joint = np.maximum(joint, np.clip(1.0 - d / 0.009, 0.0, 1.0))
    # The leak: a small ridge just under the joint, where grout ran.
    ledge = np.zeros((size, size), np.float32)
    for i in range(lifts):
        centre = i / lifts + 0.012
        d = np.abs(((yy - centre - waver + 0.5) % 1.0) - 0.5)
        ledge = np.maximum(ledge, np.clip(1.0 - d / 0.014, 0.0, 1.0))
    # Aggregate showing where the skin has weathered off.
    grain = wrapping_noise(size, rng, 6, base=14)
    exposure = np.clip((wrapping_noise(size, rng, 3, base=9) - 0.5) * 2.2, 0.0, 1.0)

    # **Form-tie holes.** The plugged holes left by the ties that held the
    # formwork together, on the formwork's own grid at about 600 mm. They are
    # the single feature that says "this was poured against boards" rather
    # than "this is a grey box", and because they are genuinely identical
    # everywhere they cost nothing in repeat.
    # 45 mm rather than the true 25: a plug that survives being mipped down
    # to a pier seen from four hundred metres is worth more than one that is
    # dimensionally exact and gone.
    ties, tie_stain = stud_grid(size, span, 0.60, 0.028, 1.0, rng)

    # Spalls: where the arris has chipped and the aggregate is proud.
    spall = np.clip((wrapping_noise(size, rng, 5, base=17) - 0.62) * 5.0, 0.0, 1.0)

    height = (boards
              - ties * 0.0045
              - joint * 0.0030
              + ledge * 0.0022
              + (grain - 0.5) * 0.0034 * (0.35 + exposure)
              - spall * 0.0038)

    # **Concrete is not white.** Weathered structural concrete sits around a
    # linear 0.28-0.32 -- it reads pale against dark water, not against the
    # sky, and at 0.455 under a 2.6-intensity sun the pylons blew out to
    # near-white and lost every step in them.
    pale = np.array([0.330, 0.322, 0.302], np.float32)
    dark = np.array([0.120, 0.117, 0.112], np.float32)

    # The staining. **Not a gradient down the tile**, which is what this was:
    # multiplying by `y` puts a light band at every repeat's top edge and a
    # dark one at its foot, and eight repeats up a pylon is eight stripes.
    # Fine, vertical, high-frequency streaking reads as weathering and cannot
    # betray the repeat.
    streak = streaks(size, rng, 18, 0.22, 1.7)
    # And the rust weep below each tie, which is the detail that dates a
    # concrete structure faster than anything else on it.
    # Only the plugs that have actually rusted weep, which is a third of
    # them: a stain under every single tie is the same graph paper again.
    weep = np.zeros((size, size), np.float32)
    for i in range(int(size * 0.09)):
        weep += np.roll(tie_stain, i, axis=0) * (1.0 - i / max(size * 0.09, 1))
    weep = np.clip(weep * 0.9, 0.0, 1.0)

    # **The albedo had a standard deviation of five values out of 255**, which
    # is a flat tan card however good the normal over it is. Every term here
    # is pulled up, and the grain -- which is the one that varies everywhere
    # -- carries most of it.
    # **The ties are a detail, not the pattern.** At 0.70 they were the
    # loudest thing in the map and 600 mm of them in each direction read as
    # brickwork -- a plug is a small dark dot, not a course.
    # **Noise over pattern.** The regular features are the vocabulary; the
    # grain is what stops them being the whole sentence. When the ties and
    # the joints carried the field it read as woven matting.
    mix = np.clip(0.88 * grain + 0.60 * streak + 0.34 * exposure
                  + 0.22 * ties + 0.45 * joint, 0.0, 1.0)
    colour = pale[None, None, :] * (1.0 - mix[:, :, None]) + dark[None, None, :] * mix[:, :, None]

    # One lift is not the next: a different batch, a different day, and two
    # or three percent between them. Two lifts *inside* the tile, so it is a
    # feature of the tile and costs no repeat.
    batches = np.array([1.000, 0.962], np.float32)
    colour *= batches[lift_index][:, :, None]

    # The weep is not grey, it is iron.
    stain = np.array([0.205, 0.108, 0.058], np.float32)
    colour = colour * (1.0 - weep[:, :, None] * 0.7) \
        + stain[None, None, :] * weep[:, :, None] * 0.7
    # Fresh aggregate at a spall is paler and sharper than the weathered skin.
    colour += spall[:, :, None] * 0.09

    rough = 0.66 + 0.22 * grain - 0.08 * streak + 0.16 * spall + 0.10 * joint
    return {
        "color": np.clip(colour, 0.0, 1.0),
        "normal": normal_map(height, 1.0, span),
        "roughness": np.clip(rough, 0.42, 0.96),
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
