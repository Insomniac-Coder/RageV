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

**And "correct" is two different pictures, which is the subtlest thing here.**
A pixel half covered by a surface reflecting 0.60 and half by one reflecting
0.05 really did receive 0.325 of the light, and the tone curve belongs on
*that*. SSAA can produce it, because it averages the linear scene before tone
mapping. FXAA and SMAA cannot: they run on the finished image, by necessity —
they threshold on perceived brightness, which only exists after the curve — so
the best they can do is the same coverage interpolated between two *display*
values. Those are different numbers, and the curve is concave, so each mode
scores badly on the other's yardstick. Measured both ways below; each mode is
judged against the one it can actually reach, and the other is printed so the
gap stays visible rather than becoming an argument.

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

MODES = ("none", "fxaa", "smaa", "ssaa", "msaa", "taa")
BACKENDS = ("vulkan", "opengl")
# Four regimes, and each one reaches code the others do not.
#
#   8   a shallow edge: long horizontal runs, the orthogonal path
#   43  just under the diagonal: the north-edge diagonal search
#   47  just over it: the same edge leaning the other way, which is where the
#       *west*-edge diagonal runs, and is otherwise never exercised
#   77  steep: long vertical runs, the orthogonal path transposed
#
# 45 itself is deliberately absent. An edge there advances exactly one row per
# column, so the staircase lands on a perfect line and the control reads zero,
# and every supersample grid is symmetric about it. A degenerate case is the
# worst possible thing to build an acceptance test on: it does not fail, it
# reports a flattering number -- which it did, and the wrong conclusion about
# diagonals it produced survived into three documents.
ANGLES = (8, 43, 47, 77)

# Pixels this far from the edge are not edge pixels, and must come through
# untouched.
FLAT_DISTANCE = 4.0

# How much better than no filter at all SMAA has to be before this passes.
#
# A floor to catch the filter silently stopping, not a grade. It was 1.4 while
# SMAA had no diagonal pass and managed only 1.6x on a near-diagonal edge; with
# 7.14 the worst of the four angles is 3.4x, so the floor goes back up. Leaving
# it at 1.4 would have let the diagonal pass regress all the way out again
# without this failing.
REQUIRED_GAIN = 2.5

# The same kind of floor for TAA, against the linear-space ideal.
#
# The floor is here to catch the history being *dropped* -- not blended in, or
# rejected everywhere -- rather than to grade the filter. So it belongs below
# what the shipped settings actually reach and above what no accumulation at
# all reaches, and both of those are measured rather than guessed.
#
# **1.5 was measured at a feedback the engine no longer ships.** It was set
# when the default was 0.9; the default became 0.6 when TAA was finally
# checked in motion, because 0.9 is worse than no filter at all on moving
# content. Static quality is the other side of that trade, and this check --
# which is entirely static -- was not re-run. It has been failing at two of
# four angles ever since, on a build that is working exactly as intended.
# That is the mistake ENGINE-NOTES 7r names in its own words: "a threshold is
# only worth having at the setting it actually runs at."
#
# Re-measured on this scene, both backends, linear-light coverage error
# against the 4x reference, as the ratio of no filter to TAA:
#
#              8 deg   43 deg   47 deg   77 deg
#   f = 0.0     1.00     0.68     0.66     0.59      no accumulation at all
#   f = 0.6     1.64     1.44     1.47     1.64      what ships
#   f = 0.9      -        -        -       5.11      the old default
#
# 1.25 sits clear of both sides: every angle passes at the shipped default
# with margin, and every angle fails with the accumulation switched off --
# which is the defect this exists to catch, and the run that proves the guard
# can still fail.
TAA_REQUIRED_GAIN = 1.25

# The two backends run different drivers and different rasterisers, so their
# coverage will not be bit-identical everywhere in general. On this scene the
# post filters have been exactly equal; anything under this is still evidence
# the direction handling matches.
BACKEND_TOLERANCE = 0.002

# MSAA gets its own, wider, and the reason is not sloppiness: *where the
# coverage samples sit inside a pixel is not specified identically* by the two
# APIs. Vulkan defines standard sample locations; OpenGL leaves them to the
# implementation. Two correct implementations can therefore disagree slightly
# on a partly covered pixel, and demanding they match would be demanding
# something neither API promises.
MSAA_BACKEND_TOLERANCE = 0.010

# The unfiltered control, from theory.
QUANTISATION_RMS = 1.0 / np.sqrt(12.0)


def aces(x):
    """Narkowicz's ACES approximation — exactly what tonemap.rvshader applies."""
    a, b, c, d, e = 2.51, 0.03, 2.43, 0.59, 0.14
    return np.clip((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0)


def to_display(linear):
    return aces(linear) ** (1.0 / 2.2)


def to_linear(display):
    """The inverse, by bisection. ACES has no closed-form inverse worth writing."""
    low = np.zeros_like(display)
    high = np.full_like(display, 64.0)
    for _ in range(60):
        middle = 0.5 * (low + high)
        below = to_display(middle) < display
        low = np.where(below, middle, low)
        high = np.where(below, high, middle)
    return 0.5 * (low + high)


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
    import postprofile

    scenes = root / "SampleProject" / "assets" / "scenes"
    shots = root / "build" / "smaa-check"
    shots.mkdir(parents=True, exist_ok=True)

    failures = []
    results = {}

    for angle in ANGLES:
        # Rewritten every run: a check that depends on a file somebody
        # generated once is a check that silently measures the wrong scene.
        scene = scenes / f"aa_edge{angle}.rage"
        profile = postprofile.write_beside(scene, { "BloomEnabled": False })
        scene.write_text(make_aa_scene.build(angle, profile))

        for backend in BACKENDS:
            for mode in MODES:
                shot = shots / f"edge{angle}-{backend}-{mode}.png"
                run(exe, [f"--rhi={backend}", f"--aa={mode}",
                          f"--scene=scenes/aa_edge{angle}.rage",
                          "--frame-time=0.0166", f"--screenshot={shot}"])

            unfiltered = luma(shots / f"edge{angle}-{backend}-none.png")
            slope, intercept, staircase, columns = fit_edge(unfiltered)

            # The control.
            #
            # 43 rather than 45 for the near-diagonal case, and the reason is
            # worth keeping: at *exactly* 45 degrees the edge advances one row
            # per column, so the staircase lands on a perfect line and this
            # reads 0.0000 -- a property of the angle, not of the renderer.
            # Every supersample grid is symmetric about that diagonal too, so
            # it is the one angle where supersampling can measure as buying
            # nothing. A degenerate case is the worst possible thing to build
            # an acceptance test on.
            if abs(staircase - QUANTISATION_RMS) > 0.01:
                failures.append(
                    f"{angle} deg {backend}: unfiltered staircase measured "
                    f"{staircase:.4f} px, and an unfiltered edge has to measure "
                    f"{QUANTISATION_RMS:.4f}. The measurement is wrong, so nothing "
                    f"else here is evidence")

            # Wide enough to hold the whole transition. A steep edge crosses
            # several rows within one column, and a fixed band would measure
            # the middle of the ramp and call the ends untouched.
            band = 2.0 + 0.5 * abs(slope)
            rows, cols = edge_band(unfiltered.shape, slope, intercept, band)
            want = ideal_coverage(rows, cols, slope, intercept)

            # The same coverage, resolved the way SSAA resolves it: mixed in
            # linear light and then tone mapped, rather than mixed after.
            dark = np.percentile(unfiltered, 0.5) / 255.0
            bright = np.percentile(unfiltered, 99.5) / 255.0
            low, high = to_linear(np.array([dark, bright]))
            want_linear = ((to_display(low + want * (high - low)) - dark)
                           / (bright - dark))

            for mode in MODES:
                got = coverage(luma(shots / f"edge{angle}-{backend}-{mode}.png"))[rows, cols]
                results[(angle, backend, mode)] = np.sqrt(((got - want) ** 2).mean())
                results[(angle, backend, mode, "linear")] = np.sqrt(
                    ((got - want_linear) ** 2).mean())

            # Claim 1, and only of SMAA: FXAA is a blur by design and is not
            # promised to leave flat regions alone.
            plain = np.asarray(Image.open(
                shots / f"edge{angle}-{backend}-none.png").convert("RGB")).astype(int)
            filtered = np.asarray(Image.open(
                shots / f"edge{angle}-{backend}-smaa.png").convert("RGB")).astype(int)

            height, width = unfiltered.shape
            centre = slope * (np.arange(width) + 0.5) + intercept
            distance = np.abs(np.arange(height)[:, None] + 0.5 - centre[None, :])
            flat = distance > FLAT_DISTANCE * max(1.0, abs(slope))
            touched = int((np.abs(plain - filtered).max(axis=2)[flat] > 0).sum())
            if touched:
                failures.append(
                    f"{angle} deg {backend}: SMAA changed {touched} pixels more than "
                    f"{FLAT_DISTANCE:g} px from the edge. Anti-aliasing must not touch "
                    f"what is not an edge")

            print(f"{angle:>3} deg {backend:7s}  unfiltered staircase "
                  f"{staircase:.4f} px over {columns} columns")

    for space, label in (("", "mixed after the tone curve — what a post filter can reach"),
                         ("linear", "mixed in linear light — what supersampling reaches")):
        print()
        print(f"coverage error, RMS, {label}")
        print(f"{'':16s}" + "".join(f"{m:>10s}" for m in MODES))
        for angle in ANGLES:
            for backend in BACKENDS:
                key = lambda m: (angle, backend, m) if not space else (angle, backend, m, space)
                row = [results[key(m)] for m in MODES]
                print(f"{angle:>3} deg {backend:8s}" + "".join(f"{v:10.4f}" for v in row))

    for angle in ANGLES:
        # Claim 2.
        a = results[(angle, "vulkan", "smaa")]
        b = results[(angle, "opengl", "smaa")]
        if abs(a - b) > BACKEND_TOLERANCE:
            failures.append(
                f"{angle} deg: SMAA measures {a:.4f} on Vulkan and {b:.4f} on OpenGL. "
                f"The two backends disagree, which is what a wrong vertical direction "
                f"looks like -- the image is still anti-aliased, towards the wrong side")

        # Claim 3, each mode against the ideal it can actually reach.
        gain = results[(angle, "vulkan", "none")] / results[(angle, "vulkan", "smaa")]
        if gain < REQUIRED_GAIN:
            failures.append(
                f"{angle} deg: SMAA is only {gain:.1f}x better than no filter at all, "
                f"and has to be at least {REQUIRED_GAIN}x")

        ssaa = (results[(angle, "vulkan", "none", "linear")]
                / results[(angle, "vulkan", "ssaa", "linear")])
        if ssaa < 1.5:
            failures.append(
                f"{angle} deg: SSAA is only {ssaa:.1f}x better than no filter at all "
                f"against the linear-space ideal, and 2x supersampling has to be at "
                f"least 1.5x. Either the scene target is not actually larger or the "
                f"resolve is not averaging its footprint")

        a = results[(angle, "vulkan", "ssaa", "linear")]
        b = results[(angle, "opengl", "ssaa", "linear")]
        if abs(a - b) > BACKEND_TOLERANCE:
            failures.append(
                f"{angle} deg: SSAA measures {a:.4f} on Vulkan and {b:.4f} on OpenGL")

        # MSAA, on the same linear-space yardstick: it resolves before the
        # tone curve exactly as supersampling does.
        msaa = (results[(angle, "vulkan", "none", "linear")]
                / results[(angle, "vulkan", "msaa", "linear")])
        if msaa < 2.0:
            failures.append(
                f"{angle} deg: MSAA is only {msaa:.1f}x better than no filter at all. "
                f"Either the scene target is not multisampled or a pipeline's sample "
                f"count does not match it -- which is undefined behaviour rather than "
                f"an error, so nothing else will say so")

        a = results[(angle, "vulkan", "msaa", "linear")]
        b = results[(angle, "opengl", "msaa", "linear")]
        if abs(a - b) > MSAA_BACKEND_TOLERANCE:
            failures.append(
                f"{angle} deg: MSAA measures {a:.4f} on Vulkan and {b:.4f} on OpenGL, "
                f"further apart than sample-position freedom explains")

        # TAA, on the linear yardstick too: it accumulates before the tone
        # curve, so it is reaching for the same ideal SSAA and MSAA are.
        #
        # **And this number flatters it, which is the honest caveat.** The
        # scene is static, so thirty frames of accumulation converge on
        # something close to an eight-sample supersample for the price of one
        # -- which is TAA at its very best and the case nobody complains
        # about. Its actual failure mode is ghosting under motion, which no
        # still frame can show; check_taa_motion.py is where that lives. What
        # this proves is narrower and still worth proving: that the history is
        # being accumulated at all, and in the right space.
        taa = (results[(angle, "vulkan", "none", "linear")]
               / results[(angle, "vulkan", "taa", "linear")])
        if taa < TAA_REQUIRED_GAIN:
            failures.append(
                f"{angle} deg: TAA is only {taa:.1f}x better than no filter at all "
                f"against the linear-space ideal, and thirty frames of accumulation on "
                f"a static scene has to be at least {TAA_REQUIRED_GAIN}x. Either the "
                f"history is not being blended in, or it is being rejected everywhere")

        a = results[(angle, "vulkan", "taa", "linear")]
        b = results[(angle, "opengl", "taa", "linear")]
        if abs(a - b) > BACKEND_TOLERANCE:
            failures.append(
                f"{angle} deg: TAA measures {a:.4f} on Vulkan and {b:.4f} on OpenGL. "
                f"The reprojection reads the history with a vertical direction that "
                f"differs between the backends, and this is where getting it wrong shows")

    # --- the case a single straight edge is not ------------------------------
    #
    # Every measurement above is one long straight edge, which is the *easy*
    # case: the runs are long and the pattern is unambiguous. What breaks a
    # morphological filter is a silhouette that turns every few pixels, and
    # there is no formula for the right answer to that.
    #
    # So the reference is rendered rather than computed: SSAA at 4x, which is
    # sixteen samples a pixel and the most correct image this engine can
    # produce. It resolves in linear light, so a post filter cannot reach it
    # exactly -- but every post filter is handicapped identically, so ranking
    # them against it is fair even though the absolute numbers are not.
    fan = scenes / "aa_fan.rage"
    fan_profile = postprofile.write_beside(fan, { "BloomEnabled": False })
    fan.write_text(make_aa_scene.build_fan(12, fan_profile))

    fan = {}
    run(exe, ["--rhi=vulkan", "--aa=ssaa", "--ssaa=4", "--scene=scenes/aa_fan.rage",
              "--frame-time=0.0166", f"--screenshot={shots / 'fan-reference.png'}"])
    reference = np.asarray(Image.open(shots / "fan-reference.png").convert("RGB")
                           ).astype(np.float64)

    for mode in MODES:
        shot = shots / f"fan-{mode}.png"
        run(exe, ["--rhi=vulkan", f"--aa={mode}", "--scene=scenes/aa_fan.rage",
                  "--frame-time=0.0166", f"--screenshot={shot}"])
        got = np.asarray(Image.open(shot).convert("RGB")).astype(np.float64)
        fan[mode] = np.sqrt(((got - reference) ** 2).mean())

    print()
    print("a fan of thin bars, against a 4x supersampled reference (RMS, /255)")
    print("".join(f"{m:>10s}" for m in MODES))
    print("".join(f"{fan[m]:10.3f}" for m in MODES))

    # The claim this scene exists to test: on content that turns constantly,
    # reconstructing the edge still beats guessing at it. Without the diagonal
    # pass SMAA was *worse* than FXAA here, which is exactly the failure a
    # single straight edge cannot show.
    if fan["smaa"] >= fan["fxaa"]:
        failures.append(
            f"on small features SMAA scores {fan['smaa']:.3f} against FXAA's "
            f"{fan['fxaa']:.3f}. Reconstructing an edge has to beat guessing at "
            f"one even when the silhouette turns every few pixels -- that is what "
            f"the diagonal pass is for")

    if fan["msaa"] >= fan["ssaa"]:
        failures.append(
            f"on small features MSAA scores {fan['msaa']:.3f} against SSAA 2x's "
            f"{fan['ssaa']:.3f}. Four coverage samples should beat four shaded ones "
            f"on geometry, since the reference is a supersampled render and MSAA is "
            f"resolving in the same space")

    if fan["smaa"] >= fan["none"]:
        failures.append(
            f"on small features SMAA scores {fan['smaa']:.3f} against {fan['none']:.3f} "
            f"for no filter at all, so it is making this content worse")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: edges straightened on both backends, flat regions untouched, "
          "and the unfiltered control lands on 1/sqrt(12)")


if __name__ == "__main__":
    main()
