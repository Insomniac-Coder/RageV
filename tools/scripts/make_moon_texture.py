#!/usr/bin/env python3
"""The moon, as an RGBA disc for the sky shader to sample.

    python tools/scripts/make_moon_texture.py

**Why a texture and not the analytic disc.** `sky.rvshader` can already draw a
limb-darkened disc on the moon direction, and that is the right shape for a
*sun* -- a featureless blown-out circle. It cannot be a moon: what makes a
moon read as the moon is the maria, the dark basalt patches everyone has
looked at their whole life. No shading model gets you there from a flat disc.

**And not a procedural one either.** The first version of this script drew the
maria as soft ellipses with scattered craters. It read as mould on a
ping-pong ball, which is roughly what any from-scratch moon does: the pattern
is the one thing about the moon that people know by heart, so anything
approximate is instantly wrong. Owner's call, and the right one -- use a
photograph.

**Source: NASA's Scientific Visualization Studio, public domain.** A single
frame from the Moon Phase and Libration series, which is rendered from
Lunar Reconnaissance Orbiter data -- so it is a real elevation-and-albedo
model of the near side, not an artist's impression. Frame 0311 of the 2025
hourly set is 13 January 2025 ~22:00 UTC, essentially full. See
`moon.png.ATTRIBUTION.md` beside the written texture.

**What this script does to it.** The source is a JPEG of a lit disc on black.
The shader wants the disc to fill the texture exactly (it maps the disc's
angular radius straight onto the UV square) and needs alpha to say where the
moon stops, so:

  1. find the disc from the source's own luminance,
  2. crop square to it, so the moon's limb touches the texture edge,
  3. resample to `--size`,
  4. write alpha as a circle with a texel of feather -- at the angular sizes
     this is drawn a hard edge crawls under camera motion.

Deterministic: same input, same output, no seeds involved.
"""

import argparse
import io
import pathlib
import sys
import urllib.request

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from make_camp_textures import png, write_meta  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_OUT = ROOT / "SampleProject" / "assets" / "textures"

# Moon Phase and Libration 2025, hourly frame 0311 -- 13 Jan 2025, full.
SOURCE_URL = ("https://svs.gsfc.nasa.gov/vis/a000000/a005400/a005415/"
              "frames/730x730_1x1_30p/moon.0311.jpg")

ATTRIBUTION = """# moon.png

The full moon, for the night sky's moon disc (`Environment.MoonTexture`).

Rendered by NASA's Scientific Visualization Studio from **Lunar
Reconnaissance Orbiter** elevation and albedo data -- so the maria and the
crater rays are the real near side, not an approximation. Frame 0311 of the
2025 hourly *Moon Phase and Libration* set: 13 January 2025, ~22:00 UTC,
essentially full phase.

Source:
<https://svs.gsfc.nasa.gov/5415/>

Frame:
<{url}>

Licence: **Public domain.** NASA content is not subject to copyright; the
SVS asks only that the work be credited, which is what this file is for.

> Credit: NASA's Scientific Visualization Studio

Reprocessed by `tools/scripts/make_moon_texture.py`, which crops the disc to
the texture edge and adds the alpha channel the sky shader masks with. The
pixels are otherwise NASA's.

This file is not named `.meta` because that extension belongs to the asset
registry, which rewrites what it owns.
"""


def load_source(path, url):
    """The NASA frame, as float RGB in 0..1."""
    from PIL import Image

    if path:
        raw = pathlib.Path(path).read_bytes()
    else:
        print("fetching {0}".format(url))
        with urllib.request.urlopen(url, timeout=60) as response:
            raw = response.read()
        print("  {0} bytes".format(len(raw)))

    image = Image.open(io.BytesIO(raw)).convert("RGB")
    return np.asarray(image).astype(np.float32) / 255.0


def disc_bounds(rgb):
    """Centre and radius of the lit disc, from the source's own luminance.

    Measured rather than assumed: the frame is nominally centred, but the
    moon's apparent size changes through the year (perigee to apogee is about
    14%), so a hard-coded radius would crop a supermoon and leave a border on
    a micromoon.
    """
    lum = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    lit = lum > 0.06                      # well above JPEG mush, well below limb
    if not lit.any():
        raise SystemExit("no disc found in the source image")

    ys, xs = np.where(lit)
    cx = 0.5 * (float(xs.min()) + float(xs.max()))
    cy = 0.5 * (float(ys.min()) + float(ys.max()))
    radius = 0.5 * max(float(xs.max() - xs.min()), float(ys.max() - ys.min()))
    return cx, cy, radius


def build(rgb, size):
    from PIL import Image

    cx, cy, radius = disc_bounds(rgb)

    # Crop square to the disc. A hair of margin so the limb's own falloff is
    # not clipped by an off-by-one in the bounds above.
    half = radius * 1.005
    left, top = cx - half, cy - half
    box = (left, top, left + 2.0 * half, top + 2.0 * half)

    source = Image.fromarray((np.clip(rgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8))
    cropped = source.resize((size, size), Image.LANCZOS, box=box)
    out = np.asarray(cropped).astype(np.float32) / 255.0

    # Alpha: the disc, with a texel of feather.
    axis = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    xx, yy = np.meshgrid(axis, axis)
    r = np.sqrt(xx * xx + yy * yy)
    alpha = np.clip((1.0 - r) / (2.0 / size), 0.0, 1.0)

    # Black outside, so a filtered tap near the rim cannot drag the sky's
    # colour into the disc or the disc's into the sky.
    out = out * alpha[:, :, None]
    return np.concatenate([out, alpha[:, :, None]], axis=-1)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--size", type=int, default=512,
                        help="texels a side (default 512; the disc is drawn "
                             "about a degree across, so more is waste)")
    parser.add_argument("--source", default=None,
                        help="a local copy of the NASA frame; downloaded when "
                             "not given")
    parser.add_argument("--url", default=SOURCE_URL)
    parser.add_argument("--output", default=str(DEFAULT_OUT))
    args = parser.parse_args(argv)

    directory = pathlib.Path(args.output)
    directory.mkdir(parents=True, exist_ok=True)

    rgb = load_source(args.source, args.url)
    path = directory / "moon.png"
    png(path, build(rgb, args.size))
    handle = write_meta(path, "Texture")

    attribution = pathlib.Path(str(path) + ".ATTRIBUTION.md")
    attribution.write_text(ATTRIBUTION.format(url=args.url), encoding="utf-8")

    print("wrote {0} ({1}x{1} RGBA)".format(path, args.size))
    print("wrote {0}".format(attribution.name))
    print("  handle {0}".format(handle))
    print("  point the scene's Environment MoonTexture at that handle")


if __name__ == "__main__":
    main()
