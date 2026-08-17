#!/usr/bin/env python3
"""Terrain (8.4, ENGINE-NOTES 7ap): what the pixels can say.

The unit claims -- the serializer, the triangle-exact sampling, the chunk
builder, the level rule, HeightAt, the height-field body -- are in
scenetest. This is the rest: what a terrain looks like from a stated
camera, on both backends, and whether the level-of-detail seams hold.

Three fixtures, all from make_terrain.py, all with the camera and the
heights stated there so nothing below is found in a frame that could
instead be derived:

1. **The ridge stands where the heights say.** A Gaussian ridge along x at
   z = 0, crest 30 m, plain 4 m; the camera stated. The row where the ridge
   meets the sky, in the frame's centre column, is *derived* from the camera
   and the crest and compared with the row where the sky ends in the frame:
   within a few pixels, on both backends, and the two backends within one
   of each other.
2. **The ridge shadows the plain.** The sun travels toward the camera and 45
   degrees down, so the crest's shadow reaches z = 26 on the near plain. A
   band inside the shadow (z 17-23) is darker than a band past it (z 30-38)
   by tens of levels -- with the maps, and with `--raytracing=on`, and the
   two agree on how much.
3. **The seams do not open.** The cliff: a rough slope rising away from the
   camera to a crest at the far edge with only sky behind it. Its chunks draw
   at two levels of detail from this camera, so seams between levels cross
   the slope facing the camera. Without skirts a seam is a crack, and a ray
   through a crack on this slope goes under the surface and out into the
   sky. So: sky-coloured pixels not connected to the frame's border -- holes
   -- number zero. (Falsified by building the chunks with `skirts = false`
   in Terrain::Create: the holes appear.)

Falsified further by swapping the row-major read (x for z) in the builder:
the ridge turns ninety degrees and claim 1's row is nowhere near.

Usage:
    python tools/scripts/check_terrain.py [--config Release]
"""

import argparse
import math
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

FRAME = 30
MAX_SILHOUETTE_ERROR_PX = 4
MAX_BACKEND_ROW_DIFF_PX = 1
MIN_SHADOW_DARKENING = 25.0
MAX_MAPS_VS_RAYS_LEVELS = 12.0
MAX_HOLES = 0
SKY_TOLERANCE = 6


def run(exe, args):
    result = subprocess.run([str(exe), *args], cwd=exe.parent,
                            capture_output=True, text=True)
    return result.returncode, (result.stdout or "") + (result.stderr or "")


def shoot(exe, backend, scene, path, extra=()):
    code, log = run(exe, [f"--rhi={backend}", f"--scene=scenes/{scene}.rage",
                          "--frame-time=0.016666", f"--screenshot-frame={FRAME}",
                          f"--screenshot={path}", "--aa=none", *extra])
    if code != 0 or not pathlib.Path(path).exists():
        print(f"FAIL: {backend} {scene} exited {code} / no image")
        print(log[-2000:])
        sys.exit(1)
    return np.asarray(Image.open(path).convert("RGB")).astype(float)


def project_row(camera, pitch, point, height, fov_degrees):
    """The frame row a world point lands on, from a camera at `camera` with
    rotation (pitch, 0, 0): the same convention make_ssr_scene's mirror row
    uses. Derived, not found."""
    cx, cy, cz = camera
    px, py, pz = point
    d = (px - cx, py - cy, pz - cz)
    forward = (0.0, math.sin(pitch), -math.cos(pitch))
    up = (0.0, math.cos(pitch), math.sin(pitch))
    depth = sum(a * b for a, b in zip(d, forward))
    rise = sum(a * b for a, b in zip(d, up))
    ndc_y = (rise / depth) / math.tan(math.radians(fov_degrees) * 0.5)
    return (1.0 - ndc_y) * 0.5 * height


def sky_colour(image):
    """The sky as this frame renders it: the median of the top rows."""
    return np.median(image[:8].reshape(-1, 3), axis=0)


def is_sky(image, sky):
    return np.all(np.abs(image - sky[None, None, :]) <= SKY_TOLERANCE, axis=2)


def first_non_sky_row(image, columns):
    """Down the given columns, the first row that is not sky: the silhouette."""
    sky = sky_colour(image)
    mask = is_sky(image[:, columns], sky)
    # A row is still sky while more than half its sampled columns are.
    rows = np.where(mask.mean(axis=1) < 0.5)[0]
    return int(rows[0]) if len(rows) else image.shape[0]


def enclosed_sky_pixels(image):
    """Sky-coloured pixels not connected to the frame's border: holes."""
    sky = is_sky(image, sky_colour(image))
    h, w = sky.shape
    reached = np.zeros_like(sky)
    stack = []
    for x in range(w):
        for y in (0, h - 1):
            if sky[y, x] and not reached[y, x]:
                reached[y, x] = True
                stack.append((y, x))
    for y in range(h):
        for x in (0, w - 1):
            if sky[y, x] and not reached[y, x]:
                reached[y, x] = True
                stack.append((y, x))
    while stack:
        y, x = stack.pop()
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if 0 <= ny < h and 0 <= nx < w and sky[ny, nx] and not reached[ny, nx]:
                reached[ny, nx] = True
                stack.append((ny, nx))
    return int(np.count_nonzero(sky & ~reached))


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
    import make_terrain as mt

    # The fixtures, exactly as committed (the generator is deterministic).
    subprocess.run([sys.executable, str(root / "tools" / "scripts" / "make_terrain.py")],
                   check=True, capture_output=True)

    shots = root / "build" / "terrain"
    shots.mkdir(parents=True, exist_ok=True)

    failures = []

    # --- 1. the ridge's silhouette ------------------------------------------
    crest = (0.0, mt.ridge_height_metres(0.0), 0.0)
    rows = {}
    for backend in ("vulkan", "opengl"):
        image = shoot(exe, backend, "terrain_ridge", shots / f"{backend}-ridge.png")
        height, width = image.shape[:2]
        derived = project_row(mt.RIDGE_CAMERA_POSITION, mt.RIDGE_CAMERA_PITCH, crest, height,
                              mt.RIDGE_CAMERA_FOV_DEGREES)
        columns = slice(int(width * 0.47), int(width * 0.53))
        found = first_non_sky_row(image, columns)
        rows[backend] = found
        print(f"{backend}: the ridge meets the sky at row {found}; the crest projects to {derived:.1f}")
        if abs(found - derived) > MAX_SILHOUETTE_ERROR_PX:
            failures.append(f"{backend}: the ridge's silhouette is at row {found}, "
                            f"the heights say {derived:.1f}")
    if abs(rows["vulkan"] - rows["opengl"]) > MAX_BACKEND_ROW_DIFF_PX:
        failures.append(f"the backends disagree about the ridge by {abs(rows['vulkan'] - rows['opengl'])} rows")

    # --- 2. the shadow band, maps and rays -----------------------------------
    plain = mt.RIDGE_BASE * mt.RIDGE_HEIGHT
    # The crest's shadow lands (30 - 4) / tan(45) = 26 m toward the camera.
    shadow_end = crest[1] - plain
    darkening = {}
    for mode, extra in (("maps", ("--raytracing=off",)), ("rays", ("--raytracing=on",))):
        image = shoot(exe, "vulkan", "terrain_ridge", shots / f"vulkan-ridge-{mode}.png", extra)
        height, width = image.shape[:2]
        columns = slice(int(width * 0.40), int(width * 0.60))
        def band(z0, z1):
            r0 = project_row(mt.RIDGE_CAMERA_POSITION, mt.RIDGE_CAMERA_PITCH, (0.0, plain, z0), height,
                             mt.RIDGE_CAMERA_FOV_DEGREES)
            r1 = project_row(mt.RIDGE_CAMERA_POSITION, mt.RIDGE_CAMERA_PITCH, (0.0, plain, z1), height,
                             mt.RIDGE_CAMERA_FOV_DEGREES)
            lo, hi = sorted((int(round(r0)), int(round(r1))))
            return image[lo:hi, columns].mean()
        shadowed = band(17.0, 23.0)
        lit = band(shadow_end + 4.0, shadow_end + 12.0)
        darkening[mode] = lit - shadowed
        print(f"vulkan {mode}: the plain in the ridge's shadow reads {shadowed:.1f}, past it {lit:.1f}: "
              f"{lit - shadowed:.1f} levels darker")
        if lit - shadowed < MIN_SHADOW_DARKENING:
            failures.append(f"vulkan {mode}: the ridge's shadow darkens the plain by only "
                            f"{lit - shadowed:.1f} levels")
    if "rays" in darkening and darkening.get("maps") is not None:
        gap = abs(darkening["rays"] - darkening["maps"])
        if gap > MAX_MAPS_VS_RAYS_LEVELS:
            failures.append(f"maps and rays disagree about the shadow by {gap:.1f} levels")
        # A device without ray queries falls back to maps and the two are
        # then identical, which is the correct answer there too.

    # --- 3. the seams --------------------------------------------------------
    for backend in ("vulkan", "opengl"):
        image = shoot(exe, backend, "terrain_cliff", shots / f"{backend}-cliff.png")
        holes = enclosed_sky_pixels(image)
        print(f"{backend}: {holes} sky pixel(s) enclosed by the cliff")
        if holes > MAX_HOLES:
            failures.append(f"{backend}: the cliff shows {holes} hole(s) at its level seams")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: the ridge stands where the heights say on both backends, its shadow lands "
          "on the plain under maps and rays alike, and the level seams open no holes")


if __name__ == "__main__":
    main()
