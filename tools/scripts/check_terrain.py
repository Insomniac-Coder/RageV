#!/usr/bin/env python3
"""Terrain (8.4, ENGINE-NOTES 7ap, 7aq, 7ar, 7as): what the pixels can say.

The unit claims -- the serializer, the triangle-exact sampling, the chunk
builder, the level rule, HeightAt, the height-field body -- are in
scenetest. This is the rest: what a terrain looks like from a stated
camera, on both backends, and whether the level-of-detail seams hold.

Seven fixtures, all from make_terrain.py, all with the camera and the
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

4. **The paint is the picture (7aq).** A flat 129 grid painted layer 0
   (a flat red `.rmat`) on the left third, layer 1 (a flat blue one) on the
   right third *at half intensity*, a linear blend between, and a strip
   along the top of the right third unpainted; camera straight down, sun
   straight down. Four claims, each region's frame columns and rows derived
   from the camera: the left region is red (red over blue by more than two
   to one and a hundred levels, in display space) and the right blue by the
   same; the two are within ten percent of each other's brightness -- the
   normalisation claim, since the right was painted at half; the middle
   column has red and blue within twenty percent -- the blend; and the
   unpainted strip, inside the blue region, is red -- the layer-0 rule,
   measured where the paint around it says otherwise. On both
   backends, and on Vulkan with the heap on and off, so all three material
   paths the layered shader has are the ones measured. (Falsified by
   swapping the weight channels in the shader -- left and right trade
   places -- and by dropping the normalisation: the right goes half as
   bright and the unpainted strip black.)

5. **The brush sculpts what it says, and the file keeps it (7ar).** The
   `brush` fixture is the ridge's heights under the ridge's camera, red
   layer 0 and blue layer 1, unpainted. The *editor* is run with
   `--brush=raise,-30,20,10,1,2`: one two-second full-strength stroke of a
   ten-metre brush at (-30, 20) on the plain, through the tool's own begin,
   step and end, saved to the asset. The runtime then renders the saved
   asset. The bump's height is stated -- a quarter of Height per second at
   the centre, the kernel's fall-off toward the rim, on top of the ridge's
   own tail -- so the row its highest point projects to is derived, and
   the top of the region that changed against the unsculpted frame must
   land on it within a few pixels. Then, on a fresh fixture,
   `--brush=paint,-30,20,10,1,2,1`: layer 1 painted where the plain was
   red, and the pixel the stroke's centre projects to reads blue.
   (Falsified by inverting the raise -- the changed region's top drops to
   the plain, a hole and not a hill -- and by painting layer 0: red stays.)

6. **From under the ground the terrain is invisible (7ap).** The `under`
   fixture: the ridge under a bright sky, the camera three metres under its
   plain, ten across from a chunk seam, looking away from the ridge and up.
   The surface's back faces cull, and the skirts -- wound both ways so a
   crack is filled from either side, which is what made each seam a wall
   hanging in the sky from under -- are drawn only while the camera is above
   the surface at its own (x, z). So every pixel of the frame is sky, on both
   backends. (Falsified by forcing the skirts on: the seams' skirts cross the
   frame -- thousands of pixels that are not sky.)

7. **A brush shape is the mask, and a pattern is on the ground (7as).** The
   `stamp` fixture: the layers fixture's flat grid and straight-down camera,
   unpainted, red layer 0 and blue layer 1. The *editor* paints layer 1
   through `shape=ridge` -- the mask's crest runs along its own x, and the
   image says so: it reads 0.94 half way out along x, and 0.000 nine tenths
   of the way out along z. So the pixel at (0.5 R, 0) must read blue and the
   one at (0, 0.9 R) red; at `angle=90` the two swap, which no radial kernel
   can do. (The negative probe is where the mask is *zero*, not merely small:
   7ar's rule that a step which means to move moves at least one unit turns a
   weight of 0.03 into visible blue over two seconds.) Then, on a fresh fixture, `pattern=dunes scale=16` with the
   plain disc, centred four metres off the origin: one crest per tile, at
   x = 0, so the ground at x = 0 reads blue and the trough eight metres away
   -- half a tile, and the same distance from the brush's centre -- stays red. (Falsified by
   ignoring the angle: the turned stroke paints what the unturned one did.
   And by sampling the pattern in the brush's frame instead of the ground's:
   the crest follows the cursor and the trough goes blue.)

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
MAX_BRUSH_ROW_ERROR_PX = 6       # the sculpted bump's top against the row its peak projects to
BRUSH_STROKE = (-30.0, 20.0, 10.0, 1.0, 2.0)   # x, z, radius, strength, seconds
MIN_LAYER_RATIO = 2.0            # the dominant channel over the other, in a painted region ...
MIN_LAYER_GAP = 100.0            # ... and by this many levels, in display space
MAX_BRIGHTNESS_MISMATCH = 0.10   # left vs right, after the normalisation lifts the right
MAX_BLEND_MISMATCH = 0.20        # red vs blue in the middle of the blend
MAX_UNDER_PIXELS = 0             # pixels that are not sky, seen from under the ground
STAMP_RADIUS = 16.0              # metres, the shape and pattern claims' brush
# Where the ridge mask is read, as fractions of the radius. Along its crest at
# a half it is 0.94; across it at nine tenths it is 0.000 -- exactly nothing,
# which is the point: 7ar's rule that a paint step which means to move moves at
# least one unit turns any non-zero weight into paint if it is held long
# enough, so the negative probe has to be where the mask is zero, not merely
# small (at seven tenths across it is 0.03, and two seconds of that is visibly
# blue -- which is how this was found).
STAMP_ALONG = 0.5
STAMP_ACROSS = 0.9
STAMP_WINDOW = 5                 # pixels either side of a probed point


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


def project_column(camera, pitch, point, width, height, fov_degrees):
    """The frame column a world point lands on, from a camera at `camera`
    with rotation (pitch, 0, 0), for a frame `width` by `height` with a
    vertical field of view. The pair of project_row: the same depth, the
    sideways offset against the horizontal half-extent."""
    cx, cy, cz = camera
    px, py, pz = point
    d = (px - cx, py - cy, pz - cz)
    forward = (0.0, math.sin(pitch), -math.cos(pitch))
    depth = sum(a * b for a, b in zip(d, forward))
    aspect = width / height
    ndc_x = (d[0] / depth) / (math.tan(math.radians(fov_degrees) * 0.5) * aspect)
    return (ndc_x + 1.0) * 0.5 * width


def down_pixel(mt, width, height, world_x, world_z):
    """The pixel a world point lands on under the straight-down camera the
    layers and stamp fixtures share: the frame is the ground, to scale."""
    half_height = mt.LAYERS_CAMERA_HEIGHT * math.tan(math.radians(mt.LAYERS_CAMERA_FOV_DEGREES) * 0.5)
    half_width = half_height * (width / height)
    column = (world_x / half_width * 0.5 + 0.5) * width
    row = (world_z / half_height * 0.5 + 0.5) * height
    return int(round(column)), int(round(row))


def brush_weight(distance, radius, hardness):
    """TerrainBrush::Weight, restated: 1 inside hardness * radius, then
    1 - smoothstep to the rim, 0 beyond (ENGINE-NOTES 7ar)."""
    if radius <= 0.0 or distance >= radius:
        return 0.0
    t = distance / radius
    if t <= hardness:
        return 1.0
    u = (t - hardness) / max(1.0 - hardness, 1e-6)
    return 1.0 - u * u * (3.0 - 2.0 * u)


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


def layer_columns(mt, width, first_sample, last_sample):
    """The frame columns a run of weight-map sample columns lands on, from a
    camera straight down at LAYERS_CAMERA_HEIGHT with the stated field of
    view. Column x of the frame sees world x = (x / width - 0.5) * 2 *
    half_width, half_width = height * tan(fov / 2) * aspect; sample i sits
    at world x = (i / (R - 1) - 0.5) * Size."""
    aspect = 16.0 / 9.0
    half_width = mt.LAYERS_CAMERA_HEIGHT * math.tan(math.radians(mt.LAYERS_CAMERA_FOV_DEGREES) * 0.5) * aspect
    def column(sample):
        world_x = (sample / (mt.LAYERS_RESOLUTION - 1) - 0.5) * mt.LAYERS_SIZE
        return int(round((world_x / half_width * 0.5 + 0.5) * width))
    return column(first_sample), column(last_sample)


def layer_rows(mt, height, first_sample, last_sample):
    """The same for rows: the frame's top is -z, sample 0's row."""
    half_height = mt.LAYERS_CAMERA_HEIGHT * math.tan(math.radians(mt.LAYERS_CAMERA_FOV_DEGREES) * 0.5)
    def row(sample):
        world_z = (sample / (mt.LAYERS_RESOLUTION - 1) - 0.5) * mt.LAYERS_SIZE
        return int(round((world_z / half_height * 0.5 + 0.5) * height))
    return row(first_sample), row(last_sample)


def check_layers(image, mt, label, failures):
    """The four claims of fixture 4 on one frame."""
    height, width = image.shape[:2]
    inset = 4   # samples in from every edge of a region, clear of the blend and the seams
    R = mt.LAYERS_RESOLUTION - 1
    l0, l1 = layer_columns(mt, width, inset, mt.LAYERS_LEFT_COLUMN - inset)
    r0, r1 = layer_columns(mt, width, mt.LAYERS_RIGHT_COLUMN + inset, R - inset)
    m0, m1 = layer_columns(mt, width, (mt.LAYERS_LEFT_COLUMN + mt.LAYERS_RIGHT_COLUMN) // 2 - 1,
                           (mt.LAYERS_LEFT_COLUMN + mt.LAYERS_RIGHT_COLUMN) // 2 + 1)
    # Rows: the painted claims below the unpainted strip; the strip itself,
    # whose first rows are above the frame (the terrain is taller than the
    # view), so it is clamped to what the frame shows.
    p0, p1 = layer_rows(mt, height, mt.LAYERS_UNPAINTED_ROWS + inset, R - inset)
    u0, u1 = layer_rows(mt, height, inset, mt.LAYERS_UNPAINTED_ROWS - inset)
    p0, p1, u0, u1 = (min(max(v, 0), height) for v in (p0, p1, u0, u1))

    left = image[p0:p1, l0:l1].reshape(-1, 3).mean(axis=0)
    right = image[p0:p1, r0:r1].reshape(-1, 3).mean(axis=0)
    middle = image[p0:p1, m0:m1].reshape(-1, 3).mean(axis=0)
    strip = image[u0:u1, r0:r1].reshape(-1, 3).mean(axis=0)
    print(f"{label}: left {left.round(1)}, right {right.round(1)}, middle {middle.round(1)}, "
          f"unpainted (in the right third) {strip.round(1)}")

    def dominates(colour, channel, other):
        return colour[channel] >= MIN_LAYER_RATIO * max(colour[other], 1.0) and \
               colour[channel] - colour[other] >= MIN_LAYER_GAP

    if not dominates(left, 0, 2):
        failures.append(f"{label}: the left region is not layer 0's red ({left.round(1)})")
    if not dominates(right, 2, 0):
        failures.append(f"{label}: the right region is not layer 1's blue ({right.round(1)})")
    lb, rb = left.sum(), right.sum()
    if abs(lb - rb) > MAX_BRIGHTNESS_MISMATCH * max(lb, rb):
        failures.append(f"{label}: the half-intensity right ({rb:.0f}) is not lifted to the left's "
                        f"brightness ({lb:.0f}); the weights are not being normalised")
    if abs(middle[0] - middle[2]) > MAX_BLEND_MISMATCH * max(middle[0], middle[2]):
        failures.append(f"{label}: the middle of the blend is not an even mix ({middle.round(1)})")
    if not dominates(strip, 0, 2) or strip.sum() < 0.5 * lb:
        failures.append(f"{label}: the unpainted strip inside the blue region is not layer 0 "
                        f"({strip.round(1)})")


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

    # --- 6. from under the ground: nothing but sky (7ap) ---------------------
    for backend in ("vulkan", "opengl"):
        image = shoot(exe, backend, "terrain_under", shots / f"{backend}-under.png")
        stray = int(np.count_nonzero(~is_sky(image, sky_colour(image))))
        print(f"{backend}: from under the ground, {stray} pixels are not sky")
        if stray > MAX_UNDER_PIXELS:
            failures.append(f"{backend}: from under the ground {stray} pixels are not sky -- "
                            f"the seams' skirts, or something else, are drawn from below")

    # --- 7. shapes and patterns (7as): the mask is the brush, the pattern is
    # on the ground ------------------------------------------------------------
    editor7 = root / "build" / "bin" / args.config / "RageVEditor" / "RageVEditor.exe"
    if not editor7.exists():
        print(f"FAIL: {editor7} does not exist; build {args.config} first")
        sys.exit(1)

    def regenerate7():
        subprocess.run([sys.executable, str(root / "tools" / "scripts" / "make_terrain.py")],
                       check=True, capture_output=True)

    def stamp_paint(spec, image):
        code, log = run(editor7, [f"--project={root / 'SampleProject'}", "--rhi=vulkan",
                                  "--scene=scenes/terrain_stamp.rage", "--select=Stamp",
                                  f"--brush={spec}", "--width=1280", "--height=720",
                                  f"--screenshot={image}"])
        if code != 0 or "--brush:" not in log or "saved" not in log:
            print(f"FAIL: the editor did not apply and save --brush={spec} (exit {code})")
            print(log[-2000:])
            sys.exit(1)

    def patch_of(image, world_x, world_z):
        height, width = image.shape[:2]
        column, row = down_pixel(mt, width, height, world_x, world_z)
        w = STAMP_WINDOW
        return image[max(row - w, 0):row + w, max(column - w, 0):column + w].reshape(-1, 3).mean(axis=0)

    def is_blue(colour):
        return colour[2] > 1.5 * max(colour[0], 1.0)

    def is_red(colour):
        return colour[0] > 1.5 * max(colour[2], 1.0)

    on = STAMP_RADIUS * STAMP_ALONG
    off = STAMP_RADIUS * STAMP_ACROSS
    for angle, along, across in ((0, (on, 0.0), (0.0, off)),
                                 (90, (0.0, on), (off, 0.0))):
        stamp_paint(f"paint,0,0,{STAMP_RADIUS:g},1,2,layer=1,shape=ridge,angle={angle}",
                    shots / f"editor-stamp-ridge{angle}.png")
        image = shoot(exe, "vulkan", "terrain_stamp", shots / f"vulkan-stamp-ridge{angle}.png")
        regenerate7()
        crest = patch_of(image, along[0], along[1])
        flank = patch_of(image, across[0], across[1])
        print(f"stamp ridge at {angle} deg: along the crest {crest.round(1)}, across it {flank.round(1)}")
        if not is_blue(crest):
            failures.append(f"stamp ridge at {angle} deg: the crest ({along}) is not painted ({crest.round(1)})")
        if not is_red(flank):
            failures.append(f"stamp ridge at {angle} deg: across it ({across}) is painted ({flank.round(1)})")

    # Centred four metres off the origin on purpose: the pattern is meant to
    # live on the *ground*, so the crest must stay at x = 0 and not follow the
    # brush. A pattern sampled in the brush's own frame would put the crest at
    # x = 4 and read half-lit at both probes, which is the falsification.
    stamp_paint(f"paint,4,0,{STAMP_RADIUS * 1.5:g},1,2,layer=1,pattern=dunes,scale=16",
                shots / "editor-stamp-dunes.png")
    image = shoot(exe, "vulkan", "terrain_stamp", shots / "vulkan-stamp-dunes.png")
    regenerate7()
    crest = patch_of(image, 0.0, 0.0)
    trough = patch_of(image, 8.0, 0.0)
    print(f"stamp dunes: the crest at x = 0 {crest.round(1)}, the trough at x = 8 {trough.round(1)}")
    if not is_blue(crest):
        failures.append(f"stamp dunes: the crest is not painted ({crest.round(1)})")
    if not is_red(trough):
        failures.append(f"stamp dunes: the trough half a tile away is painted ({trough.round(1)})")

    # --- 5. the brush (7ar): the editor sculpts, the runtime renders the file --
    editor = root / "build" / "bin" / args.config / "RageVEditor" / "RageVEditor.exe"
    if not editor.exists():
        print(f"FAIL: {editor} does not exist; build {args.config} first")
        sys.exit(1)

    def regenerate():
        subprocess.run([sys.executable, str(root / "tools" / "scripts" / "make_terrain.py")],
                       check=True, capture_output=True)

    def editor_brush(spec, image):
        code, log = run(editor, [f"--project={root / 'SampleProject'}", "--rhi=vulkan",
                                 "--scene=scenes/terrain_brush.rage", "--select=Brush",
                                 f"--brush={spec}", "--width=1280", "--height=720",
                                 f"--screenshot={image}"])
        if code != 0 or "--brush:" not in log or "saved" not in log:
            print(f"FAIL: the editor did not apply and save --brush={spec} (exit {code})")
            print(log[-2000:])
            sys.exit(1)

    bx, bz, radius, strength, seconds = BRUSH_STROKE
    hardness = 0.5   # the tool's default, which --brush does not change
    plain_of = mt.ridge_height_metres

    before = shoot(exe, "vulkan", "terrain_brush", shots / "vulkan-brush-before.png")
    editor_brush(f"raise,{bx:g},{bz:g},{radius:g},{strength:g},{seconds:g}",
                 shots / "editor-brush-raise.png")
    after = shoot(exe, "vulkan", "terrain_brush", shots / "vulkan-brush-raise.png")
    regenerate()

    height, width = after.shape[:2]
    # The bump's surface is stated: the ridge's own height at each z plus the
    # raise -- a quarter of Height per second at full weight -- times the
    # kernel's weight at that distance from the centre. Rows do not depend on
    # x, so the profile along the stroke's centre column is enough; the
    # highest-projecting point of it is where the changed region must begin.
    lift = strength * mt.RIDGE_HEIGHT * 0.25 * seconds
    derived = min(project_row(mt.RIDGE_CAMERA_POSITION, mt.RIDGE_CAMERA_PITCH,
                              (bx, plain_of(z) + lift * brush_weight(abs(z - bz), radius, hardness), z),
                              height, mt.RIDGE_CAMERA_FOV_DEGREES)
                  for z in np.linspace(bz - radius, bz + radius, 401))
    changed = np.abs(after - before).max(axis=2) > 12
    rows = np.nonzero(changed.any(axis=1))[0]
    found = int(rows.min()) if len(rows) else height
    print(f"brush raise: the sculpt changes the frame from row {found}; the bump's peak projects to {derived:.1f} "
          f"({int(changed.sum())} pixels changed)")
    if abs(found - derived) > MAX_BRUSH_ROW_ERROR_PX:
        failures.append(f"brush raise: the changed region begins at row {found}, "
                        f"the stated bump projects to {derived:.1f}")

    editor_brush(f"paint,{bx:g},{bz:g},{radius:g},{strength:g},{seconds:g},1",
                 shots / "editor-brush-paint.png")
    painted = shoot(exe, "vulkan", "terrain_brush", shots / "vulkan-brush-paint.png")
    regenerate()
    centre = (bx, plain_of(bz), bz)
    row = int(round(project_row(mt.RIDGE_CAMERA_POSITION, mt.RIDGE_CAMERA_PITCH, centre, height,
                                mt.RIDGE_CAMERA_FOV_DEGREES)))
    column = int(round(project_column(mt.RIDGE_CAMERA_POSITION, mt.RIDGE_CAMERA_PITCH, centre,
                                      width, height, mt.RIDGE_CAMERA_FOV_DEGREES)))
    window = painted[max(row - 6, 0):row + 6, max(column - 6, 0):column + 6].reshape(-1, 3).mean(axis=0)
    was = before[max(row - 6, 0):row + 6, max(column - 6, 0):column + 6].reshape(-1, 3).mean(axis=0)
    print(f"brush paint: the stroke's centre projects to ({column}, {row}); it read {was.round(1)} and reads "
          f"{window.round(1)}")
    if window[2] < 2.0 * max(window[0], 1.0) or was[0] < 2.0 * max(was[2], 1.0):
        failures.append(f"brush paint: the painted centre is not layer 1's blue ({window.round(1)}) "
                        f"where it was red ({was.round(1)})")

    # --- 4. the paint (7aq): both backends, and both material paths on Vulkan --
    for label, backend, extra in (("vulkan", "vulkan", ("--bindless=on",)),
                                  ("vulkan-bound", "vulkan", ("--bindless=off",)),
                                  ("opengl", "opengl", ())):
        image = shoot(exe, backend, "terrain_layers", shots / f"{label}-layers.png", extra)
        check_layers(image, mt, label, failures)

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: the ridge stands where the heights say on both backends, its shadow lands "
          "on the plain under maps and rays alike, the level seams open no holes, the "
          "paint is the picture on every material path, the brush sculpts and paints "
          "what it says and the file keeps it, from under the ground there is only sky, "
          "and a mask paints its own shape while a pattern stays on the ground")


if __name__ == "__main__":
    main()
