#!/usr/bin/env python3
"""Terrain fixtures: `.rvterrain` heightfields, and scenes that stand on them.

The asset (ENGINE-NOTES 7ap) is a square grid of 16-bit heights, 2^n + 1 a
side, in a small binary file:

    char[4] "RVTR", uint32 version 1, uint32 resolution, uint32 layers 0,
    uint32 reserved[4], then resolution*resolution little-endian uint16.

Three generators, each written beside a `.meta` whose handle is a stable
hash of the name and whose SourceHash is the registry's own FNV-1a of the
bytes, so a checkout does not resave anything:

- `hills`  -- value noise summed over octaves, for the sample scene and for
              looking at. Seeded, so the same file every run.
- `ridge`  -- an analytic Gaussian ridge along x across the middle of the
              grid, on a low plain: the *known* shape check_terrain reads
              back, whose silhouette row it derives from the camera and the
              ridge's height rather than finding in the frame.
- `flat`   -- one height everywhere.

- `cliff`  -- a slope rising away from the camera to a crest at the far edge,
              rough per sample: the level-of-detail seams face the camera and
              a crack in one opens onto the sky, which check_terrain counts.

And three scenes: `scenes/terrain.rage` (the hills, a sun, a camera, a couple
of crates resting on the ground), `scenes/terrain_ridge.rage` (the ridge under
the stated camera check_terrain projects) and `scenes/terrain_cliff.rage`.

    python tools/scripts/make_terrain.py            # everything into SampleProject
    python tools/scripts/make_terrain.py --png in.png out.rvterrain   # from a 16-bit PNG
"""

import argparse
import math
import pathlib
import struct
import sys

import numpy as np

import make_motion_scene as base

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "SampleProject" / "assets"

MAGIC = b"RVTR"
VERSION = 1

# --- the ridge, stated for check_terrain -----------------------------------
RIDGE_RESOLUTION = 257
RIDGE_SIZE = 128.0          # metres a side
RIDGE_HEIGHT = 40.0         # metres at a full sample
RIDGE_BASE = 0.10           # the plain, as a fraction of RIDGE_HEIGHT
RIDGE_PEAK = 0.75           # the crest, as a fraction of RIDGE_HEIGHT
RIDGE_SIGMA_METRES = 6.0    # Gaussian width across z
RIDGE_CAMERA_POSITION = (0.0, 22.0, 60.0)
RIDGE_CAMERA_PITCH = -0.30  # radians; looks down the z axis at the ridge
RIDGE_CAMERA_FOV_DEGREES = 60.0
# The sun travels toward +z -- toward the camera -- and 45 degrees down, so
# the ridge's shadow lands on the near plain, in front of the camera.
RIDGE_SUN_ROTATION = (-0.7854, 3.14159, 0.0)
RIDGE_SUN_ELEVATION_DEGREES = 45.0

# The cliff: a slope rising away from the camera to a crest at the far edge
# with nothing behind it but sky, rough at the sample scale so a coarser
# level of detail errs by metres. Level seams face the camera; a crack in
# one is a ray that goes under the surface and out into the sky, which is
# what check_terrain counts as a hole -- zero with skirts, some without.
CLIFF_RESOLUTION = 257
CLIFF_SIZE = 128.0
CLIFF_HEIGHT = 40.0
CLIFF_NOISE = 0.025         # +- as a fraction of CLIFF_HEIGHT, per sample
CLIFF_CAMERA_POSITION = (0.0, 12.0, 170.0)
CLIFF_CAMERA_PITCH = -0.03
CLIFF_SKY = (0.30, 0.50, 0.90)

HILLS_RESOLUTION = 513
HILLS_SIZE = 256.0
HILLS_HEIGHT = 30.0
# SampleProject/assets/materials/soil.rmat, by its .meta handle.
SOIL_MATERIAL = 2253873544424874864


def handle_for(name):
    """A stable 64-bit id from a name. FNV-1a, the same as make_demo_scene's."""
    h = 0xCBF29CE484222325
    for byte in name.encode("utf-8"):
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h or 0x7261676556444D4F


def fnv1a_bytes(data):
    """The registry's SourceHash: FNV-1a over the file's bytes."""
    h = 0xCBF29CE484222325
    for byte in data:
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def encode(heights):
    """`heights` is a square uint16 array, 2^n + 1 a side."""
    heights = np.ascontiguousarray(heights, dtype="<u2")
    resolution = heights.shape[0]
    assert heights.shape == (resolution, resolution), heights.shape
    quads = resolution - 1
    assert 33 <= resolution <= 4097 and quads & (quads - 1) == 0, resolution
    header = MAGIC + struct.pack("<IIIIIII", VERSION, resolution, 0, 0, 0, 0, 0)
    return header + heights.tobytes()


def write_terrain(path, heights, name):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    data = encode(heights)
    path.write_bytes(data)
    handle = handle_for(name)
    path.with_name(path.name + ".meta").write_text(
        f"Handle: {handle}\nType: Terrain\nSourceHash: {fnv1a_bytes(data)}",
        encoding="utf-8", newline="\n")
    return handle


def to_u16(unit):
    return np.clip(np.rint(np.asarray(unit, dtype=np.float64) * 65535.0), 0, 65535).astype(np.uint16)


# --- generators --------------------------------------------------------------

def value_noise(resolution, cells, rng):
    """Smooth noise: a random lattice `cells` a side, bicubic-ish (smoothstep)
    interpolated up to `resolution`. Plain and dependency-free."""
    lattice = rng.random((cells + 1, cells + 1))
    xs = np.linspace(0.0, cells, resolution)
    x0 = np.minimum(np.floor(xs).astype(int), cells - 1)
    fx = xs - x0
    fx = fx * fx * (3.0 - 2.0 * fx)
    # rows then columns
    a = lattice[x0, :] * (1.0 - fx)[:, None] + lattice[x0 + 1, :] * fx[:, None]
    b = a[:, x0] * (1.0 - fx)[None, :] + a[:, x0 + 1] * fx[None, :]
    return b


def hills(resolution=HILLS_RESOLUTION, seed=7):
    rng = np.random.default_rng(seed)
    total = np.zeros((resolution, resolution))
    amplitude, cells, weight = 1.0, 4, 0.0
    for _ in range(6):
        total += amplitude * value_noise(resolution, cells, rng)
        weight += amplitude
        amplitude *= 0.5
        cells *= 2
    total /= weight
    # A gentle bowl toward the middle so the camera at the origin sits low.
    ys, xs = np.mgrid[0:resolution, 0:resolution] / (resolution - 1)
    bowl = 0.15 * ((xs - 0.5) ** 2 + (ys - 0.5) ** 2) * 4.0
    unit = np.clip(0.15 + 0.7 * total + bowl, 0.0, 1.0)
    return to_u16(unit)


def ridge(resolution=RIDGE_RESOLUTION, size=RIDGE_SIZE):
    """A Gaussian ridge along x at z = 0 (the middle row), on a plain."""
    zs = (np.arange(resolution) / (resolution - 1) - 0.5) * size   # metres
    profile = RIDGE_BASE + (RIDGE_PEAK - RIDGE_BASE) * np.exp(-(zs ** 2) / (2.0 * RIDGE_SIGMA_METRES ** 2))
    unit = np.repeat(profile[:, None], resolution, axis=1)          # rows are z
    return to_u16(unit)


def ridge_height_metres(z_metres):
    """The ridge's height at a world z, in metres, for the check."""
    unit = RIDGE_BASE + (RIDGE_PEAK - RIDGE_BASE) * math.exp(-(z_metres ** 2) / (2.0 * RIDGE_SIGMA_METRES ** 2))
    return unit * RIDGE_HEIGHT


def cliff(resolution=CLIFF_RESOLUTION, seed=11):
    """A plane from 0.2 at the near edge (+z, the last row) to 0.8 at the far
    edge (-z, the first row), plus noise per *column* -- ridges running away
    from the camera. Rough along a seam that crosses the frame, so a coarser
    level's edge misses the finer one by metres; smooth behind it, so a ray
    through the crack stays under the rising surface and comes out in the
    sky. Noise per sample would fill its own cracks with the next bump."""
    rng = np.random.default_rng(seed)
    rows = np.arange(resolution) / (resolution - 1)          # 0 at the far edge
    plane = 0.8 - 0.6 * rows
    ridges = rng.uniform(-CLIFF_NOISE, CLIFF_NOISE, resolution)
    unit = plane[:, None] + ridges[None, :]
    return to_u16(np.clip(unit, 0.0, 1.0))


def flat(resolution=65, unit=0.5):
    return to_u16(np.full((resolution, resolution), unit))


def from_png(path):
    from PIL import Image
    image = Image.open(path)
    if image.mode == "I;16":
        array = np.asarray(image, dtype=np.uint16)
    elif image.mode in ("I", "F"):
        array = np.clip(np.asarray(image, dtype=np.float64), 0, 65535).astype(np.uint16)
    else:
        array = (np.asarray(image.convert("L"), dtype=np.uint16) * 257).astype(np.uint16)
    if array.shape[0] != array.shape[1]:
        sys.exit(f"heightmap must be square, got {array.shape}")
    return array


# --- scenes ------------------------------------------------------------------

def _terrain_entity(next_id, tag, handle, size, height, material=None,
                    texture_scale=4.0, position=(0, 0, 0)):
    lines = [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        f"      Tag: {tag}",
        "    TransformComponent:",
        f"      Position: {base.vec(position)}",
        "      Rotation: [0, 0, 0]",
        "      Scale: [1, 1, 1]",
        "    TerrainComponent:",
        f"      Terrain: {handle}",
        f"      Size: {size:g}",
        f"      Height: {height:g}",
    ]
    if material is not None:
        lines.append(f"      Material: {material}")
    lines += [
        f"      TextureScale: {texture_scale:g}",
        "      Collision: true",
    ]
    return lines


def _sun(next_id, rotation, intensity=3.0):
    return [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        "      Tag: Sun",
        "    TransformComponent:",
        "      Position: [0, 50, 0]",
        f"      Rotation: {base.vec(rotation)}",
        "      Scale: [1, 1, 1]",
        "    LightComponent:",
        "      Type: Directional",
        "      Color: [1, 0.96, 0.9]",
        f"      Intensity: {intensity:g}",
        "      Range: 60",
        "      InnerCone: 20",
        "      OuterCone: 30",
        "      CastShadows: true",
    ]


def _crate(next_id, tag, position, scale=(1, 1, 1), dynamic=True):
    lines = [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        f"      Tag: {tag}",
        "    TransformComponent:",
        f"      Position: {base.vec(position)}",
        "      Rotation: [0, 0, 0]",
        f"      Scale: {base.vec(scale)}",
        "    MeshComponent:",
        f"      Mesh: {base.PRIMITIVE_BASE + base.CUBE}",
        "      Material:",
        "        BaseColor: [0.7, 0.45, 0.25, 1]",
        "        Emissive: [0, 0, 0, 1]",
        "        Metallic: 0",
        "        Roughness: 0.8",
        "        Occlusion: 1",
    ]
    if dynamic:
        lines += [
            "    RigidBodyComponent:",
            "      Type: Dynamic",
            "      Mass: 1",
            "    ColliderComponent:",
            "      Shape: Box",
            f"      HalfExtents: {base.vec([s * 0.5 for s in scale])}",
        ]
    return lines


def build_hills_scene(handle, profile=None):
    next_id = base._ids()
    lines = base._header("Terrain hills", sky_rgb=(0.45, 0.6, 0.8))
    lines += base._camera(next_id, (0.0, 34.0, 118.0), rotation=(-0.28, 0, 0), profile=profile)
    lines += _sun(next_id, (-0.9, 0.6, 0.0))
    # The demo's soil, whose own tiling is 6 repeats per uv unit: at 12 metres
    # per uv unit that is one repeat every two metres.
    lines += _terrain_entity(next_id, "Terrain", handle, HILLS_SIZE, HILLS_HEIGHT,
                             material=SOIL_MATERIAL, texture_scale=12.0)
    lines += _crate(next_id, "Crate A", (3.0, 25.0, 10.0))
    lines += _crate(next_id, "Crate B", (-4.0, 27.0, 6.0), (1.5, 1.5, 1.5))
    return "\n".join(lines) + "\n"


def build_ridge_scene(handle, profile=None):
    next_id = base._ids()
    lines = base._header("Terrain ridge", sky_rgb=(0.02, 0.02, 0.025))
    lines += base._camera(next_id, RIDGE_CAMERA_POSITION, rotation=(RIDGE_CAMERA_PITCH, 0, 0),
                          profile=profile)
    lines += _sun(next_id, RIDGE_SUN_ROTATION)
    lines += _terrain_entity(next_id, "Ridge", handle, RIDGE_SIZE, RIDGE_HEIGHT)
    return "\n".join(lines) + "\n"


def build_cliff_scene(handle, profile=None):
    next_id = base._ids()
    lines = base._header("Terrain cliff", sky_rgb=CLIFF_SKY)
    lines += base._camera(next_id, CLIFF_CAMERA_POSITION, rotation=(CLIFF_CAMERA_PITCH, 0, 0),
                          profile=profile)
    # Steep, so the rough slope shadows itself as little as possible: a
    # shadowed pixel and a sky pixel must stay tellable apart.
    lines += _sun(next_id, (-1.2, 0.0, 0.0))
    lines += _terrain_entity(next_id, "Cliff", handle, CLIFF_SIZE, CLIFF_HEIGHT)
    return "\n".join(lines) + "\n"


# Bloom off on the check fixtures: a bright sky bleeding over an edge is a
# row moved, and the exposure fixed so a frame's levels mean the same twice.
CHECK_PROFILE = { "BloomEnabled": False }


def write_fixture_scenes(scenes, hills_handle, ridge_handle, cliff_handle):
    """The three scenes and a post profile beside each, exactly as the check
    writes them, so a checkout and a check run agree to the byte."""
    import postprofile
    ridge = scenes / "terrain_ridge.rage"
    cliff = scenes / "terrain_cliff.rage"
    hills = scenes / "terrain.rage"
    write_scene(hills, build_hills_scene(hills_handle, postprofile.write_beside(hills, CHECK_PROFILE)))
    write_scene(ridge, build_ridge_scene(ridge_handle, postprofile.write_beside(ridge, CHECK_PROFILE)))
    write_scene(cliff, build_cliff_scene(cliff_handle, postprofile.write_beside(cliff, CHECK_PROFILE)))


def write_scene(path, text):
    path = pathlib.Path(path)
    path.write_text(text, encoding="utf-8", newline="\n")
    data = path.read_bytes()
    path.with_name(path.name + ".meta").write_text(
        f"Handle: {handle_for(path.name)}\nType: Scene\nSourceHash: {fnv1a_bytes(data)}",
        encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--png", nargs=2, metavar=("IN", "OUT"), help="convert a 16-bit PNG to .rvterrain")
    args = parser.parse_args()

    if args.png:
        source, out = args.png
        write_terrain(out, from_png(source), pathlib.Path(out).name)
        print(f"wrote {out}")
        return

    terrain_dir = ASSETS / "terrain"
    hills_handle = write_terrain(terrain_dir / "hills.rvterrain", hills(), "terrain/hills.rvterrain")
    ridge_handle = write_terrain(terrain_dir / "ridge.rvterrain", ridge(), "terrain/ridge.rvterrain")
    cliff_handle = write_terrain(terrain_dir / "cliff.rvterrain", cliff(), "terrain/cliff.rvterrain")

    scenes = ASSETS / "scenes"
    write_fixture_scenes(scenes, hills_handle, ridge_handle, cliff_handle)
    print(f"wrote {terrain_dir / 'hills.rvterrain'} ({HILLS_RESOLUTION}^2), "
          f"{terrain_dir / 'ridge.rvterrain'} ({RIDGE_RESOLUTION}^2), "
          f"{terrain_dir / 'cliff.rvterrain'} ({CLIFF_RESOLUTION}^2), and three scenes")


if __name__ == "__main__":
    main()
