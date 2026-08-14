#!/usr/bin/env python3
"""The check ENGINE-NOTES 7r said had to exist before TAA was finished.

Quoting it, written before any of TAA was built: "Its only real failure mode
is motion... That check does not exist yet and TAA is not finished until it
does."

Everything else here holds the scene still, which is the regime a temporal
filter is *best* in. This renders one frame of a scene where something is
moving and judges it against a 4x supersampled render of the same frame --
the same instant, sampled sixteen times a pixel, with no temporal component
at all. That is what the moving frame should look like.

The scene (make_motion_scene.py) has two identical textured patches: one
falling 16 px a frame, one perfectly still. Both in one frame, so "TAA is
better standing still" and "TAA is not worse moving" are measured against
the same lighting, the same reference and the same run.

**Textured, not a flat block, and that is not decoration.** The first
version of this used one uniform bright block and could not detect anything
at all: the neighbourhood clip rejects wrong-place history whenever the
neighbourhood is uniform, so it discards the evidence along with the
mistake. A patch of cells at differing brightness has no uniform
neighbourhood, so history fetched from the wrong place is locally plausible,
survives the clip, and shows up.

What this catches that nothing else can:

**1. A reprojection that goes the wrong way.** The velocity attachment is a
difference of clip coordinates, whose y axis runs the same way on both
backends, while the resolve's v does not -- so the y component is negated on
one of them. Inverting it measures 29.9 against 19.8 here. On a still scene
it measures nothing at all, because velocity is zero everywhere and either
sign times zero is zero.

**2. A feedback default tuned only for stillness.** Still content improves
monotonically with feedback; moving content bottoms out near 0.3 and is
worse than no filter by 0.9. The default was 0.9 until this existed.

Usage:
    python tools/scripts/check_taa_motion.py [--config Release]
"""

import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

BACKENDS = ("vulkan", "opengl")

# The frame to judge. Late enough that the accumulation has settled and the
# block is well inside the frame, early enough that it has not fallen out.
FRAME = 30

# How much better than no filter TAA has to be on the *still* patch. A floor
# to catch the accumulation having stopped, not a grade -- it measures around
# 3x at the default feedback.
REQUIRED_STILL_GAIN = 2.0

# And how much worse it is allowed to be on the *moving* patch.
#
# This is the reprojection guard, and the number is measured on both sides
# rather than chosen:
#
#   correct sign   1.01 / 1.02   (vulkan / opengl)
#   inverted sign  1.21 / 1.22
#
# 1.10 sits between them with roughly equal headroom either way.
#
# **It was 1.25 first, and that could not fail.** That figure came from
# measuring the inversion at a feedback of 0.9, where it costs 1.86x. The
# default is 0.6 now, less history is used, and a backwards reprojection is
# correspondingly cheaper -- so the guard written for the old default passed
# a build whose reprojection ran backwards. A threshold is only worth having
# at the setting it actually runs at.
ALLOWED_MOVING_LOSS = 1.10

# Both backends compute the same reprojection from the same velocities, so
# they should land in the same place. A vertical sign handled correctly on
# one and not the other shows up here rather than in the numbers above.
BACKEND_TOLERANCE = 0.35


def run(exe, args):
    result = subprocess.run([str(exe), *args], cwd=exe.parent,
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAIL: {' '.join(args)} exited {result.returncode}")
        print(result.stdout[-2000:], result.stderr[-2000:])
        sys.exit(1)
    return result.stdout + result.stderr


def shoot(exe, backend, path, extra=()):
    # A scene that fails to parse leaves the runtime exiting 0 with no
    # screenshot at all, so a missing file is a real failure and not a
    # tidying-up problem. Opening it below is what reports that.
    run(exe, [f"--rhi={backend}", "--scene=scenes/motion_fall.rage",
              "--frame-time=0.0166", f"--screenshot-frame={FRAME}",
              f"--screenshot={path}", *extra])

    if not pathlib.Path(path).exists():
        print(f"FAIL: {path} was never written -- the scene probably did not load")
        sys.exit(1)

    return np.asarray(Image.open(path).convert("RGB")).astype(float).mean(axis=2)


def patches(reference):
    """Where the moving and still patches are, found rather than assumed."""
    height, width = reference.shape
    columns = np.arange(width)[None, :]
    bright = reference > 40

    def box(mask, pad=25):
        ys, xs = np.nonzero(mask)
        return (slice(max(ys.min() - pad, 0), min(ys.max() + pad + 1, height)),
                slice(max(xs.min() - pad, 0), min(xs.max() + pad + 1, width)))

    # The generator puts the falling patch right of centre and the still one
    # left, far enough apart that a split down the middle separates them.
    return box(bright & (columns >= width // 2)), box(bright & (columns < width // 2))


def rms(image, reference, region):
    return float(np.sqrt(((image[region] - reference[region]) ** 2).mean()))


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
    import make_motion_scene

    scenes = root / "SampleProject" / "assets" / "scenes"
    shots = root / "build" / "taa-motion"
    shots.mkdir(parents=True, exist_ok=True)

    # Rewritten every run, like every other check here: one that depends on a
    # file somebody generated once is one that silently measures another scene.
    (scenes / "motion_fall.rage").write_text(make_motion_scene.build_falling())

    failures = []
    measured = {}

    for backend in BACKENDS:
        reference = shoot(exe, backend, shots / f"{backend}-ref.png",
                          ("--aa=ssaa", "--ssaa=4"))
        moving, still = patches(reference)

        plain = shoot(exe, backend, shots / f"{backend}-none.png", ("--aa=none",))
        temporal = shoot(exe, backend, shots / f"{backend}-taa.png", ("--aa=taa",))

        n_mov, n_sti = rms(plain, reference, moving), rms(plain, reference, still)
        t_mov, t_sti = rms(temporal, reference, moving), rms(temporal, reference, still)
        measured[backend] = (t_mov, t_sti)

        print(f"--- {backend}")
        print(f"  {'':10s} {'moving':>9s} {'still':>9s}")
        print(f"  {'no filter':10s} {n_mov:9.3f} {n_sti:9.3f}")
        print(f"  {'taa':10s} {t_mov:9.3f} {t_sti:9.3f}")

        gain = n_sti / max(t_sti, 1e-6)
        if gain < REQUIRED_STILL_GAIN:
            failures.append(
                f"{backend}: standing still TAA is only {gain:.1f}x better than no "
                f"filter, and has to be at least {REQUIRED_STILL_GAIN}x. The history "
                f"is not being accumulated")
        else:
            print(f"  still: {gain:.1f}x better than no filter")

        loss = t_mov / max(n_mov, 1e-6)
        if loss > ALLOWED_MOVING_LOSS:
            failures.append(
                f"{backend}: under motion TAA is {loss:.2f}x *worse* than no filter, "
                f"past the {ALLOWED_MOVING_LOSS} allowed. Either the history is being "
                f"fetched from somewhere that is not where the surface was -- an "
                f"inverted vertical sign measures 1.86x here -- or the feedback is "
                f"turned up far enough that stale history outweighs the new frame")
        else:
            print(f"  moving: {loss:.2f}x no filter")

    a, b = measured["vulkan"], measured["opengl"]
    for i, what in enumerate(("moving", "still")):
        if abs(a[i] - b[i]) > BACKEND_TOLERANCE:
            failures.append(
                f"{what}: TAA measures {a[i]:.3f} on Vulkan and {b[i]:.3f} on OpenGL. "
                f"The reprojection reads the history with a vertical direction that "
                f"differs between them, which is exactly what a still scene cannot see")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: TAA accumulates standing still and does not lose to no filter in "
          "motion, on both backends")


if __name__ == "__main__":
    main()
