#!/usr/bin/env python3
"""Motion blur (9.5), measured on the scene that can show it.

The falling-block fixture check_taa_motion built is exactly the instrument:
one textured patch falling 16 px a frame, one identical patch standing still,
both in one frame under --frame-time. Four properties, each passable alone by
a specific failure and only meaningful together:

1. **Off is off, to the byte.** A profile that says MotionBlur: false renders
   the same bytes as one that never mentions it.
2. **Zero velocity is a no-op, to the byte.** With the blur ON, the still
   patch's pixels are identical to the off frame: every tile near it is
   still, the gather early-outs into a copy, and an RGBA16F copy of an
   RGBA16F image is exact. A blur that softens what does not move fails
   here and nowhere else.
3. **The smear lies along the motion and scales with the shutter.** The
   falling patch's vertical extent grows under the blur, by about the
   per-frame motion times the shutter, and doubling the shutter roughly
   doubles the growth. Horizontal extent stays put -- a smear off-axis is a
   sign error, and this fixture is the one place a vertical sign shows.
4. **A frame reproduces.** The same shot twice is byte-identical -- the
   dither is seeded by pixel, never by frame, and this is what holds
   --screenshot-frame's meaning. ENGINE-NOTES 7ab.

Anti-aliasing is forced off for every shot: this measures the blur, not the
blur convolved with whichever filter the project defaults to.

Usage:
    python tools/scripts/check_motion_blur.py [--config Release]
"""

import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

BACKENDS = ("vulkan", "opengl")
FRAME = 30

# The block falls ~16 px a frame by frame 30; at a 0.5 shutter the smear adds
# roughly 8 px of vertical extent. Bounds are generous because the extent is
# measured off a thresholded image, but the *ratio* between two shutters is
# tight: it cancels everything but the scaling being tested.
MIN_GROWTH_HALF = 3.0        # px of extra vertical extent at shutter 0.5
MAX_GROWTH_HALF = 14.0       # ...and no more than this: measured 6 (97 -> 103), and
                             # a smear at twice its length is a shutter bug the ratio
                             # claim below cannot see, because it scales both (7ba)
RATIO_LOW, RATIO_HIGH = 1.4, 2.8   # extent growth, shutter 1.0 over 0.5


def run(exe, args):
    result = subprocess.run([str(exe), "--render-defaults=on", *args], cwd=exe.parent,
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAIL: {' '.join(args)} exited {result.returncode}")
        print(result.stdout[-2000:], result.stderr[-2000:])
        sys.exit(1)


def shoot(exe, backend, path):
    run(exe, [f"--rhi={backend}", "--scene=scenes/motion_fall.rage",
              "--frame-time=0.0166", f"--screenshot-frame={FRAME}",
              f"--screenshot={path}", "--aa=none"])
    if not pathlib.Path(path).exists():
        print(f"FAIL: {path} was never written -- the scene probably did not load")
        sys.exit(1)
    return np.asarray(Image.open(path).convert("RGB")).astype(float)


def halves(image):
    """Moving patch is right of centre, still patch left -- the fixture's
    layout, relied on the same way check_taa_motion relies on it."""
    width = image.shape[1]
    return image[:, width // 2:], image[:, : width // 2]


def extent(image, axis):
    """Size of the bright region along one axis, in pixels."""
    bright = image.mean(axis=2) > 40
    coords = np.nonzero(bright)[axis]
    if coords.size == 0:
        return 0.0
    return float(coords.max() - coords.min() + 1)


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
    import postprofile

    scenes = root / "SampleProject" / "assets" / "scenes"
    shots = root / "build" / "motion-blur"
    shots.mkdir(parents=True, exist_ok=True)

    scene = scenes / "motion_fall.rage"

    # The scene names one profile; each variant rewrites that profile between
    # runs, and every run is a fresh process, so the edit always lands.
    def profile(settings):
        handle = postprofile.write_beside(scene, { "BloomEnabled": False, **settings })
        scene.write_text(make_motion_scene.build_falling(handle))

    failures = []

    for backend in BACKENDS:
        # 1. Off is off, to the byte.
        profile({})
        absent = shoot(exe, backend, shots / f"{backend}-absent.png")
        profile({ "MotionBlur": False })
        explicit = shoot(exe, backend, shots / f"{backend}-off.png")

        delta = int(np.abs(absent - explicit).max())
        print(f"{backend}: off vs never mentioned    max {delta}")
        if delta != 0:
            failures.append(f"{backend}: MotionBlur: false changed the image (max {delta})")

        # 2. With the blur on, the still half is untouched to the byte.
        profile({ "MotionBlur": True, "MotionBlurShutter": 0.5 })
        half = shoot(exe, backend, shots / f"{backend}-on.png")

        still_delta = int(np.abs(halves(half)[1] - halves(explicit)[1]).max())
        print(f"{backend}: still patch under the blur  max {still_delta}")
        if still_delta != 0:
            failures.append(f"{backend}: the blur touched pixels nothing moved "
                            f"near (max {still_delta})")

        # 3. The smear is vertical and scales with the shutter.
        profile({ "MotionBlur": True, "MotionBlurShutter": 1.0 })
        full = shoot(exe, backend, shots / f"{backend}-full.png")

        base_v = extent(halves(explicit)[0], 0)
        half_v = extent(halves(half)[0], 0)
        full_v = extent(halves(full)[0], 0)
        base_h = extent(halves(explicit)[0], 1)
        half_h = extent(halves(half)[0], 1)

        growth_half = half_v - base_v
        growth_full = full_v - base_v
        ratio = growth_full / max(growth_half, 1.0e-3)

        print(f"{backend}: vertical extent {base_v:.0f} -> {half_v:.0f} -> {full_v:.0f}"
              f"  (growth ratio {ratio:.2f})")
        print(f"{backend}: horizontal extent {base_h:.0f} -> {half_h:.0f}")

        if growth_half < MIN_GROWTH_HALF:
            failures.append(f"{backend}: a 0.5 shutter grew the smear by only "
                            f"{growth_half:.1f} px")
        if growth_half > MAX_GROWTH_HALF:
            failures.append(f"{backend}: a 0.5 shutter grew the smear by {growth_half:.1f} px "
                            f"(ceiling {MAX_GROWTH_HALF}) -- a shutter this long is a bug "
                            f"the ratio claim cannot see, because it scales both sides")
        if not (RATIO_LOW <= ratio <= RATIO_HIGH):
            failures.append(f"{backend}: doubling the shutter scaled the smear "
                            f"by {ratio:.2f}, expected about 2")
        if abs(half_h - base_h) > 4.0:
            failures.append(f"{backend}: the smear moved {abs(half_h - base_h):.1f} px "
                            "off the motion axis")

        # 4. A frame reproduces.
        profile({ "MotionBlur": True, "MotionBlurShutter": 0.5 })
        again = shoot(exe, backend, shots / f"{backend}-again.png")
        repeat = int(np.abs(half - again).max())
        print(f"{backend}: frame {FRAME} twice        max {repeat}")
        if repeat != 0:
            failures.append(f"{backend}: the same frame rendered differently twice "
                            f"(max {repeat})")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: off is off to the byte, stillness is untouched to the byte, the "
          "smear lies along the motion and scales with the shutter, and a frame "
          "reproduces")


if __name__ == "__main__":
    main()
