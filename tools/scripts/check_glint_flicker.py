#!/usr/bin/env python3
"""How many pixels blink, over a run of consecutive frames.

WR-13 (docs/RENDERING-REVAMP.md). The owner reported the bridge's cables,
suspender ropes and lamp standards flickering at distance -- the sub-pixel
glint problem that no coverage-based anti-aliasing can fix, because MSAA
supersamples *coverage* and a highlight smaller than a pixel is a *shading*
frequency.

**The metric is the blinking-pixel COUNT, not the worst pixel.** That is a
hard-learned number here: an earlier flicker session halved the worst pixel
(26 levels to 13) and the owner read the result as "0% improvement", because
what the eye picks up on a dark frame is how many points are twinkling, not
how far the loudest one swings.

A pixel counts as blinking when its luminance swings by more than `--threshold`
levels across the run *and* reverses direction at least twice -- a pixel that
ramps steadily is something moving, which is not what this is looking for.

The run must be at least 8 frames: TAA's jitter has an 8-frame period
(`TemporalJitterPhase: 8`), so a shorter window can sample one phase of the
cycle and report calm that is not there.

**Pin the clock with `--frame-time=0.000001`, never `0`.** Zero means the
wall clock (`Application.cpp`: a step of zero falls back to the measured
frame time), so the waves keep rolling and the tower beacons keep flashing,
and every crest and flash is counted here as a blink. The first numbers
taken with this script were taken that way, and the water's 15-65% was
motion. With the step pinned, a hard-shadowed frame blinks at 0.01%, which
is the metric's floor (film grain and dither are under two levels).

Usage:
    python tools/scripts/check_glint_flicker.py <dir>/base_*.png
    python tools/scripts/check_glint_flicker.py --region 0,250,1600,420 <files>

Captured with, from build/bin/Release/RageVRuntime:
    RageVRuntime.exe --project=<SampleProject> --scene=scenes/GoldenGateDemo.rage
        --rhi=vulkan --render-defaults=off --width=1600 --height=900
        --frame-time=0.000001 --vsync=off --screenshot-frame=240
        --screenshot-count=16 --screenshot=<dir>/f.png [--aa=none|msaa --msaa=4]
"""

import argparse
import glob
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow: python -m pip install pillow")

LUMA = (0.2126, 0.7152, 0.0722)


def load(paths, region):
    frames = []
    for path in paths:
        img = Image.open(path).convert("RGB")
        if region:
            img = img.crop(region)
        px = img.get_flattened_data()
        frames.append([LUMA[0] * p[0] + LUMA[1] * p[1] + LUMA[2] * p[2] for p in px])
    return frames, img.size


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+")
    parser.add_argument("--region", default=None,
                        help="x0,y0,x1,y1 to restrict the count to")
    parser.add_argument("--threshold", type=float, default=6.0,
                        help="luminance swing, in levels of 255, that counts")
    parser.add_argument("--label", default="")
    args = parser.parse_args()

    paths = []
    for pattern in args.files:
        paths.extend(sorted(glob.glob(pattern)) or [pattern])
    if len(paths) < 8:
        sys.exit("need at least 8 frames -- TAA's jitter period is 8")

    region = tuple(int(v) for v in args.region.split(",")) if args.region else None
    frames, size = load(paths, region)
    total = len(frames[0])

    blinking = 0
    swing_sum = 0.0
    worst = 0.0
    for i in range(total):
        series = [f[i] for f in frames]
        swing = max(series) - min(series)
        if swing < args.threshold:
            continue
        # Direction reversals: a steady ramp is motion, not a blink.
        reversals = 0
        previous = 0
        for a, b in zip(series, series[1:]):
            direction = (b > a) - (b < a)
            if direction and previous and direction != previous:
                reversals += 1
            if direction:
                previous = direction
        if reversals >= 2:
            blinking += 1
            swing_sum += swing
            worst = max(worst, swing)

    print("{0}{1} frames, {2} px examined{3}".format(
        args.label + ": " if args.label else "", len(paths), total,
        " (region {0})".format(region) if region else ""))
    print("  blinking pixels   {0:>7}   ({1:.3f}% of the region)".format(
        blinking, 100.0 * blinking / max(total, 1)))
    print("  mean swing        {0:>7.1f} levels".format(
        swing_sum / blinking if blinking else 0.0))
    print("  worst swing       {0:>7.1f} levels".format(worst))


if __name__ == "__main__":
    main()
