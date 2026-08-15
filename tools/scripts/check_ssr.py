#!/usr/bin/env python3
"""SSR (9.7), on a bright block standing on a dark mirror floor.

Both backends:

1. **Off is off, to the byte.**
2. **The mirror shows the block.** The floor region directly below the
   block's base -- where its reflection lands -- gains brightness with SSR
   on. On a black metal floor with no lights, that brightness can only be
   the reflection.
3. **A rough floor shows less of it.** The same region on a roughness-0.7
   floor gains less than the mirror did: the trace fades toward roughness
   1 and the resolve blurs what it finds.
4. **The floor away from the block does not light up** -- a ray that finds
   nothing must leave the pixel alone, or every reflective surface glows.
5. **A frame reproduces**, and both backends agree on the reflection's
   position to within a couple of rows -- which is where a wrong Y
   convention in the reflected uv shows first. ENGINE-NOTES 7ad.

Usage:
    python tools/scripts/check_ssr.py [--config Release]
"""

import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

BACKENDS = ("vulkan", "opengl")
FRAME = 30

# Mean-level gains. The mirror bound is deliberately modest -- what matters
# is that it is clearly nonzero and clearly larger than the rough one.
MIN_MIRROR_GAIN = 6.0
MAX_EMPTY_DRIFT = 1.0
BACKEND_ROW_TOLERANCE = 3


def run(exe, args):
    result = subprocess.run([str(exe), *args], cwd=exe.parent,
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAIL: {' '.join(args)} exited {result.returncode}")
        print(result.stdout[-2000:], result.stderr[-2000:])
        sys.exit(1)


def shoot(exe, backend, path):
    run(exe, [f"--rhi={backend}", "--scene=scenes/ssr_mirror.rage",
              "--frame-time=0.0166", f"--screenshot-frame={FRAME}",
              f"--screenshot={path}", "--aa=none"])
    if not pathlib.Path(path).exists():
        print(f"FAIL: {path} was never written -- the scene probably did not load")
        sys.exit(1)
    return np.asarray(Image.open(path).convert("RGB")).astype(float).mean(axis=2)


def block_base(image):
    """The lowest bright row of the block itself, found on the off frame:
    the block is the only bright thing in it."""
    bright = image > 60
    rows = np.nonzero(bright.any(axis=1))[0]
    return int(rows.max())


def regions(image):
    height, width = image.shape
    base = block_base(image)
    cols = slice(int(width * 0.38), int(width * 0.62))
    # The reflection lands below the base line, in the block's columns.
    reflection = (slice(base + 4, min(base + int(height * 0.18), height)), cols)
    # Open floor, far to the side, well below the horizon: nothing above it
    # to reflect but empty sky, which is dark.
    empty = (slice(int(height * 0.8), int(height * 0.95)),
             slice(int(width * 0.04), int(width * 0.2)))
    return reflection, empty


def reflection_centroid_row(image, region):
    patch = image[region]
    weights = np.clip(patch - patch.min(), 0, None)
    if weights.sum() <= 0:
        return float(region[0].start)
    rows = np.arange(patch.shape[0])[:, None]
    return float(region[0].start + (weights * rows).sum() / weights.sum())


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
    import make_ssr_scene
    import postprofile

    scenes = root / "SampleProject" / "assets" / "scenes"
    shots = root / "build" / "ssr"
    shots.mkdir(parents=True, exist_ok=True)
    scene = scenes / "ssr_mirror.rage"

    def stage(settings, roughness):
        handle = postprofile.write_beside(scene, { "BloomEnabled": False, **settings })
        scene.write_text(make_ssr_scene.build(handle, roughness))

    failures = []
    centroids = {}

    for backend in BACKENDS:
        stage({}, 0.0)
        absent = shoot(exe, backend, shots / f"{backend}-absent.png")
        stage({ "ScreenSpaceReflections": False }, 0.0)
        off = shoot(exe, backend, shots / f"{backend}-off.png")

        delta = int(np.abs(absent - off).max())
        print(f"{backend}: off vs never mentioned   max {delta}")
        if delta != 0:
            failures.append(f"{backend}: ScreenSpaceReflections: false changed the "
                            f"image (max {delta})")

        reflection, empty = regions(off)

        stage({ "ScreenSpaceReflections": True }, 0.0)
        mirror = shoot(exe, backend, shots / f"{backend}-mirror.png")
        stage({ "ScreenSpaceReflections": True }, 0.7)
        rough = shoot(exe, backend, shots / f"{backend}-rough.png")

        mirror_gain = mirror[reflection].mean() - off[reflection].mean()
        rough_gain = rough[reflection].mean() - off[reflection].mean()
        empty_drift = abs(mirror[empty].mean() - off[empty].mean())

        print(f"{backend}: reflection region gains {mirror_gain:.2f} (mirror), "
              f"{rough_gain:.2f} (rough)")
        print(f"{backend}: empty floor drifts {empty_drift:.3f}")

        if mirror_gain < MIN_MIRROR_GAIN:
            failures.append(f"{backend}: the mirror floor gained only "
                            f"{mirror_gain:.2f} levels under the block")
        if rough_gain >= mirror_gain:
            failures.append(f"{backend}: a rough floor reflected as much as a "
                            f"mirror ({rough_gain:.2f} vs {mirror_gain:.2f})")
        if empty_drift > MAX_EMPTY_DRIFT:
            failures.append(f"{backend}: the empty floor lit up by "
                            f"{empty_drift:.3f} levels")

        stage({ "ScreenSpaceReflections": True }, 0.0)
        again = shoot(exe, backend, shots / f"{backend}-again.png")
        repeat = int(np.abs(mirror - again).max())
        print(f"{backend}: frame {FRAME} twice       max {repeat}")
        if repeat != 0:
            failures.append(f"{backend}: the same frame rendered differently twice "
                            f"(max {repeat})")

        centroids[backend] = reflection_centroid_row(mirror - off, reflection)

    if len(centroids) == 2:
        rows = abs(centroids["vulkan"] - centroids["opengl"])
        print(f"reflection centroid row: vulkan {centroids['vulkan']:.1f}, "
              f"opengl {centroids['opengl']:.1f}  (differ by {rows:.1f})")
        if rows > BACKEND_ROW_TOLERANCE:
            failures.append(f"the backends put the reflection {rows:.1f} rows apart")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: off is off to the byte, the mirror shows the block, a rough floor "
          "shows less, empty floor stays dark, a frame reproduces, and both "
          "backends agree where the reflection lands")


if __name__ == "__main__":
    main()
