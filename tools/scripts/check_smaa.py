#!/usr/bin/env python3
"""Anti-aliasing, measured rather than admired.

An anti-aliasing filter is easy to get subtly wrong in a way that still looks
better, because *any* blur looks better on a still frame. So this checks
three separate things, and only one of them is about quality.

**1. It must not touch what is not an edge.** Every pixel more than four
pixels from the edge must be bit-identical to `--aa=none`. Exact, not a
tolerance -- this is what separates a sharp filter from one quietly softening
the whole frame, and no amount of "it looks fine" substitutes for it.

**2. Both backends must agree.** This is the sharp end of the SMAA design:
after the flip that every fullscreen pass already does, the y axis still runs
opposite ways in sample space on Vulkan and OpenGL, and this filter is
direction-sensitive everywhere FXAA was symmetric. Getting it wrong leaves an
image that is still anti-aliased, with every edge smoothed towards the wrong
side by half a pixel. Only a cross-backend diff sees that.

**3. It must actually straighten edges.** The scene is a single straight edge
at a known angle (`make_aa_scene.py`), so the exact coverage of every pixel is
computable. The metric is the RMS error against it, in coverage units.

The measurement carries its own control. With no filter at all, every pixel is
wholly one side or the other, so the column sums are a staircase -- and the
deviation of a staircase from the line it approximates is the uniform
quantisation error, **1/sqrt(12) = 0.2887 px at any angle**. If the unfiltered
image does not measure that, the measurement is wrong and nothing below it
means anything.

Usage:
    python tools/scripts/check_smaa.py [--config Release]
"""

import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

MODES = ("none", "fxaa", "smaa")
BACKENDS = ("vulkan", "opengl")
ANGLES = (8, 45)

# Pixels this far from the edge are not edge pixels, and must come through
# untouched.
FLAT_DISTANCE = 4.0

# How much better than no filter at all SMAA has to be before this passes.
# Measured at 6x and 9x, so this is a floor rather than a target -- it exists
# to catch the filter silently stopping, not to grade it.
REQUIRED_GAIN = 2.5

# The two backends run different drivers and different rasterisers, so their
# coverage will not be bit-identical everywhere in general. On this scene it
# has been exactly equal; anything under this is still evidence the direction
# handling matches.
BACKEND_TOLERANCE = 0.002

# The unfiltered control, from theory.
QUANTISATION_RMS = 1.0 / np.sqrt(12.0)


def run(exe, args):
    result = subprocess.run([str(exe), *args], cwd=exe.parent,
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAIL: {' '.join(args)} exited {result.returncode}")
        print(result.stdout[-2000:], result.stderr[-2000:])
        sys.exit(1)
    return result.stdout + result.stderr


def luma(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64) @ [
        0.2126, 0.7152, 0.0722]


def coverage(image):
    """Per-pixel coverage by the bright side, from the two flat levels."""
    dark, bright = np.percentile(image, 0.5), np.percentile(image, 99.5)
    return (image - dark) / (bright - dark)


def fit_edge(unfiltered):
    """The edge as y = slope*x + intercept, from the unfiltered image.

    Column sums are exact regardless of filtering -- each is the number of
    covered rows in that column -- so this recovers the true line to well
    under a hundredth of a pixel.
    """
    columns = coverage(unfiltered).sum(axis=0)
    height, width = unfiltered.shape
    inside = (columns > 5) & (columns < height - 5)
    x = np.arange(width)[inside]
    gradient, offset = np.polyfit(x, columns[inside], 1)
    residual = np.sqrt(((columns[inside] - (gradient * x + offset)) ** 2).mean())
    # Column sums count *covered* rows from the bottom; the edge row is the
    # complement, and the half-pixel is the column's centre.
    return -gradient, height - offset + 0.5 * gradient, residual, inside.sum()


def ideal_coverage(rows, columns, slope, intercept, samples=1024):
    """Exactly how much of each pixel the half-plane covers."""
    t = (np.arange(samples) + 0.5) / samples
    xs = columns[:, None] + t[None, :]
    height_in_pixel = (slope * xs + intercept) - rows[:, None]
    return 1.0 - np.clip(height_in_pixel, 0.0, 1.0).mean(axis=1)


def edge_band(shape, slope, intercept, band):
    height, width = shape
    columns = np.arange(width)
    centre = slope * (columns + 0.5) + intercept
    first = np.floor(centre - band).astype(int)
    span = int(2 * band) + 1
    rows = (first[:, None] + np.arange(span)[None, :]).ravel()
    cols = np.repeat(columns, span)
    keep = (rows >= 0) & (rows < height)
    return rows[keep], cols[keep]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[2]
    exe = root / "build" / "bin" / args.config / "RageVRuntime" / "RageVRuntime.exe"
    if not exe.exists():
        print(f"FAIL: {exe} does not exist; build {args.config} first")
        sys.exit(1)

    sys.path.insert(0, str(root / "tools" / "scripts"))
    import make_aa_scene

    scenes = root / "SampleProject" / "assets" / "scenes"
    shots = root / "build" / "smaa-check"
    shots.mkdir(parents=True, exist_ok=True)

    failures = []
    results = {}

    for angle in ANGLES:
        # Rewritten every run: a check that depends on a file somebody
        # generated once is a check that silently measures the wrong scene.
        (scenes / f"aa_edge{angle}.rage").write_text(make_aa_scene.build(angle))

        for backend in BACKENDS:
            for mode in MODES:
                shot = shots / f"edge{angle}-{backend}-{mode}.png"
                run(exe, [f"--rhi={backend}", f"--aa={mode}",
                          f"--scene=scenes/aa_edge{angle}.rage",
                          "--frame-time=0.0166", f"--screenshot={shot}"])

            unfiltered = luma(shots / f"edge{angle}-{backend}-none.png")
            slope, intercept, staircase, columns = fit_edge(unfiltered)

            # The control. Only meaningful where the slope is irrational
            # enough for the staircase to be equidistributed -- at exactly 45
            # degrees the steps land on a perfect line and the figure is zero
            # by construction, which is a property of the angle and not of
            # the renderer.
            if angle != 45 and abs(staircase - QUANTISATION_RMS) > 0.01:
                failures.append(
                    f"{angle} deg {backend}: unfiltered staircase measured "
                    f"{staircase:.4f} px, and an unfiltered edge has to measure "
                    f"{QUANTISATION_RMS:.4f}. The measurement is wrong, so nothing "
                    f"else here is evidence")

            rows, cols = edge_band(unfiltered.shape, slope, intercept, band=2.0)
            want = ideal_coverage(rows, cols, slope, intercept)

            for mode in MODES:
                got = coverage(luma(shots / f"edge{angle}-{backend}-{mode}.png"))
                results[(angle, backend, mode)] = np.sqrt(
                    ((got[rows, cols] - want) ** 2).mean())

            # Claim 1, and only of SMAA: FXAA is a blur by design and is not
            # promised to leave flat regions alone.
            plain = np.asarray(Image.open(
                shots / f"edge{angle}-{backend}-none.png").convert("RGB")).astype(int)
            filtered = np.asarray(Image.open(
                shots / f"edge{angle}-{backend}-smaa.png").convert("RGB")).astype(int)

            height, width = unfiltered.shape
            centre = slope * (np.arange(width) + 0.5) + intercept
            distance = np.abs(np.arange(height)[:, None] + 0.5 - centre[None, :])
            flat = distance > FLAT_DISTANCE
            touched = int((np.abs(plain - filtered).max(axis=2)[flat] > 0).sum())
            if touched:
                failures.append(
                    f"{angle} deg {backend}: SMAA changed {touched} pixels more than "
                    f"{FLAT_DISTANCE:g} px from the edge. Anti-aliasing must not touch "
                    f"what is not an edge")

            print(f"{angle:>3} deg {backend:7s}  unfiltered staircase "
                  f"{staircase:.4f} px over {columns} columns")

    print()
    print(f"coverage error against the exact edge, RMS (lower is better)")
    print(f"{'':16s}" + "".join(f"{m:>10s}" for m in MODES))
    for angle in ANGLES:
        for backend in BACKENDS:
            row = [results[(angle, backend, m)] for m in MODES]
            gain = row[0] / row[-1] if row[-1] else float("inf")
            print(f"{angle:>3} deg {backend:8s}" + "".join(f"{v:10.4f}" for v in row)
                  + f"    SMAA is {gain:.1f}x better than none")

    for angle in ANGLES:
        # Claim 2.
        a = results[(angle, "vulkan", "smaa")]
        b = results[(angle, "opengl", "smaa")]
        if abs(a - b) > BACKEND_TOLERANCE:
            failures.append(
                f"{angle} deg: SMAA measures {a:.4f} on Vulkan and {b:.4f} on OpenGL. "
                f"The two backends disagree, which is what a wrong vertical direction "
                f"looks like -- the image is still anti-aliased, towards the wrong side")

        # Claim 3.
        gain = results[(angle, "vulkan", "none")] / results[(angle, "vulkan", "smaa")]
        if gain < REQUIRED_GAIN:
            failures.append(
                f"{angle} deg: SMAA is only {gain:.1f}x better than no filter at all, "
                f"and has to be at least {REQUIRED_GAIN}x")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: edges straightened on both backends, flat regions untouched, "
          "and the unfiltered control lands on 1/sqrt(12)")


if __name__ == "__main__":
    main()
