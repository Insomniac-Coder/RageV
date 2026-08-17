#!/usr/bin/env python3
"""Terrain fixtures: `.rvterrain` heightfields, and scenes that stand on them.

The asset (ENGINE-NOTES 7ap, 7aq) is a square grid of 16-bit heights, 2^n + 1
a side, in a small binary file, with the paint after it when there is any:

    char[4] "RVTR", uint32 version 1, uint32 resolution, uint32 layers (0 or 4),
    uint32 reserved[4], then resolution*resolution little-endian uint16 heights,
    then -- when layers is 4 -- resolution*resolution*4 bytes: one RGBA weight
    per sample, one channel per layer, on the same row-major grid.

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
- `layers` -- a flat 129 grid painted for the layered-material claim (7aq):
              layer 0 on the left third, layer 1 on the right third at *half*
              intensity, a linear blend across the middle, and a strip along
              the top of the *right* third left unpainted -- where the paint
              around it says blue and the rule says layer 0. Under a camera looking
              straight down and a sun straight down, with a flat red `.rmat`
              as layer 0 and a flat blue one as layer 1, so the frame's colour
              *is* the weight map -- and where it is not, something is wrong.

- `brush`  -- the ridge's heights again under their own handle, unpainted, so
              the brush check (7ar) can sculpt and paint a copy that the ridge
              claims never see. Its scene stands red layer 0 and blue layer 1
              on it under the ridge's camera and sun.

And six scenes: `scenes/terrain.rage` (the hills, painted by slope and height
with three layers -- soil, a mossy soil on the flats, a sandy one in the low
ground -- a sun, a camera, a couple of crates resting on the ground),
`scenes/terrain_ridge.rage` (the ridge under the stated camera check_terrain
projects), `scenes/terrain_cliff.rage`, `scenes/terrain_layers.rage`,
`scenes/terrain_brush.rage`, and `scenes/terrain_under.rage` (the ridge again,
under a bright sky, with the camera three metres *under* its plain, looking
away from the ridge and up: from under the ground the terrain is invisible --
the surface culls away and the skirts are not drawn -- so every pixel is sky,
where they used to be the seams' skirts hanging as walls).

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

# Under the ridge's plain (4 m): three metres down, ten across from the x = 0
# seam so that seam's skirt is not edge-on, forty out along z where the
# ridge's Gaussian tail is nothing; facing +z (away from the ridge) and 0.4
# rad up, so the frame holds the seams' skirts if they are drawn and nothing
# but sky if they are not. A bright sky, so a shadowed skirt still differs.
UNDER_CAMERA_POSITION = (10.0, 1.0, 40.0)
UNDER_CAMERA_ROTATION = (0.4, 3.14159, 0.0)
UNDER_SKY = (0.30, 0.50, 0.90)

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
# SampleProject/assets/materials/soil.rmat, by its .meta handle, and its
# maps (make_demo_scene's palette), which the moss and sand tints share.
SOIL_MATERIAL = 2253873544424874864
SOIL_MAPS = {"BaseColor": 17246187153222571135, "Normal": 12973099232739305617,
             "Roughness": 10909146574749930491, "Height": 12081873772315894805}

# The layered fixture (7aq), stated for check_terrain.
LAYERS_RESOLUTION = 129
LAYERS_SIZE = 64.0
LAYERS_HEIGHT = 1.0
LAYERS_CAMERA_HEIGHT = 50.0
LAYERS_CAMERA_FOV_DEGREES = 60.0
# The paint, in sample columns of the 129: layer 0 up to LEFT, layer 1 from
# RIGHT, a linear blend between; and rows below UNPAINTED_ROWS of the *right*
# third carry no paint at all -- a patch that must read as layer 0 inside a
# region painted layer 1, so the claim cannot pass by measuring the wrong rows.
LAYERS_LEFT_COLUMN = 43
LAYERS_RIGHT_COLUMN = 86
LAYERS_UNPAINTED_ROWS = 20
LAYERS_RIGHT_WEIGHT = 127          # half intensity: what the normalisation lifts
LAYERS_RED = (0.80, 0.08, 0.08)
LAYERS_BLUE = (0.08, 0.08, 0.80)


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


def encode(heights, weights=None):
    """`heights` is a square uint16 array, 2^n + 1 a side; `weights`, when
    given, a (resolution, resolution, 4) uint8 array of layer weights."""
    heights = np.ascontiguousarray(heights, dtype="<u2")
    resolution = heights.shape[0]
    assert heights.shape == (resolution, resolution), heights.shape
    quads = resolution - 1
    assert 33 <= resolution <= 4097 and quads & (quads - 1) == 0, resolution
    layers = 0
    tail = b""
    if weights is not None:
        weights = np.ascontiguousarray(weights, dtype=np.uint8)
        assert weights.shape == (resolution, resolution, 4), weights.shape
        layers = 4
        tail = weights.tobytes()
    header = MAGIC + struct.pack("<IIIIIII", VERSION, resolution, layers, 0, 0, 0, 0)
    return header + heights.tobytes() + tail


def write_terrain(path, heights, name, weights=None):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    data = encode(heights, weights)
    path.write_bytes(data)
    handle = handle_for(name)
    path.with_name(path.name + ".meta").write_text(
        f"Handle: {handle}\nType: Terrain\nSourceHash: {fnv1a_bytes(data)}",
        encoding="utf-8", newline="\n")
    return handle


def to_u16(unit):
    return np.clip(np.rint(np.asarray(unit, dtype=np.float64) * 65535.0), 0, 65535).astype(np.uint16)


def to_weights(*channels):
    """Up to four (resolution, resolution) arrays in [0, 1] to the RGBA8
    weight map; missing channels are zero. Not normalised here: the shader
    normalises, and the layered fixture relies on that."""
    resolution = channels[0].shape[0]
    out = np.zeros((resolution, resolution, 4), dtype=np.uint8)
    for i, channel in enumerate(channels):
        out[:, :, i] = np.clip(np.rint(np.asarray(channel, dtype=np.float64) * 255.0), 0, 255).astype(np.uint8)
    return out


def write_material(path, name, base_color, maps=None, tiling=(1.0, 1.0),
                   roughness=1.0, height_scale=0.0):
    """A `.rmat` beside a `.meta` whose SourceHash is the registry's own, so a
    check run leaves it clean. Untextured unless `maps` names the handles."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f"Material: {path.stem}",
        f"BaseColor: [{base_color[0]:g}, {base_color[1]:g}, {base_color[2]:g}, 1]",
        "Emissive: [0, 0, 0, 1]",
        "Metallic: 0",
        f"Roughness: {roughness:g}",
        "Occlusion: 1",
        "NormalScale: 1",
        "Specular: 0.5",
        f"HeightScale: {height_scale:g}",
        f"Tiling: [{tiling[0]:g}, {tiling[1]:g}]",
        "UvOffset: [0, 0]",
    ]
    if maps:
        lines.append("Maps:")
        for key, value in maps.items():
            lines.append(f"  {key}: {value}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    data = path.read_bytes()
    handle = handle_for(name)
    path.with_name(path.name + ".meta").write_text(
        f"Handle: {handle}\nType: Material\nSourceHash: {fnv1a_bytes(data)}",
        encoding="utf-8", newline="\n")
    return handle


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


def hills_weights(heights, size=HILLS_SIZE, height=HILLS_HEIGHT):
    """The hills' paint, from their own shape: a mossy soil where the ground
    is flat, a sandy one where it is low, plain soil for the rest. Rules, not
    a brush -- the brush is stage 3 -- but they make the layers visible from
    the demo camera and give the shader three layers to blend."""
    unit = heights.astype(np.float64) / 65535.0
    metres = unit * height
    cell = size / (heights.shape[0] - 1)
    dz, dx = np.gradient(metres, cell)
    slope = np.sqrt(dx * dx + dz * dz)                 # rise over run
    flatness = np.clip((0.35 - slope) / 0.25, 0.0, 1.0)  # 1 on the flat, 0 past ~19 degrees
    low = np.clip((0.30 - unit) / 0.10, 0.0, 1.0)      # 1 in the bowl, 0 above 30 %
    sand = low
    moss = flatness * (1.0 - sand)
    soil = np.clip(1.0 - moss - sand, 0.0, 1.0)
    return to_weights(soil, moss, sand)


def layers(resolution=LAYERS_RESOLUTION):
    """The layered fixture: flat heights, and the stated paint (7aq)."""
    heights = flat(resolution, 0.5)
    xs = np.arange(resolution)
    t = np.clip((xs - LAYERS_LEFT_COLUMN) / float(LAYERS_RIGHT_COLUMN - LAYERS_LEFT_COLUMN), 0.0, 1.0)
    red = np.where(xs <= LAYERS_LEFT_COLUMN, 1.0, np.where(xs >= LAYERS_RIGHT_COLUMN, 0.0, 1.0 - t))
    blue = np.where(xs >= LAYERS_RIGHT_COLUMN, LAYERS_RIGHT_WEIGHT / 255.0,
                    np.where(xs <= LAYERS_LEFT_COLUMN, 0.0, t))
    red = np.repeat(red[None, :], resolution, axis=0)
    blue = np.repeat(blue[None, :], resolution, axis=0)
    # The unpainted strip: the first rows (z from -Size/2, the top of a
    # frame looking down) of the right third, every channel zero.
    red[:LAYERS_UNPAINTED_ROWS, LAYERS_RIGHT_COLUMN:] = 0.0
    blue[:LAYERS_UNPAINTED_ROWS, LAYERS_RIGHT_COLUMN:] = 0.0
    return heights, to_weights(red, blue)


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
                    texture_scale=4.0, position=(0, 0, 0), layers=()):
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
    for i, layer in enumerate(layers):
        if layer is not None:
            lines.append(f"      Layer{i + 1}: {layer}")
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


def build_hills_scene(handle, profile=None, moss=None, sand=None):
    next_id = base._ids()
    lines = base._header("Terrain hills", sky_rgb=(0.45, 0.6, 0.8))
    lines += base._camera(next_id, (0.0, 34.0, 118.0), rotation=(-0.28, 0, 0), profile=profile)
    lines += _sun(next_id, (-0.9, 0.6, 0.0))
    # The demo's soil, whose own tiling is 6 repeats per uv unit: at 12 metres
    # per uv unit that is one repeat every two metres. Layer 0; the moss and
    # the sand are the same maps under a tint, in the weights hills_weights
    # painted from the slope and the height.
    lines += _terrain_entity(next_id, "Terrain", handle, HILLS_SIZE, HILLS_HEIGHT,
                             material=SOIL_MATERIAL, texture_scale=12.0,
                             layers=(moss, sand))
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


def build_layers_scene(handle, red, blue, profile=None):
    """Straight down at a flat painted terrain, lit straight down: the frame
    is the weight map, and every claim about it is about the shader."""
    next_id = base._ids()
    lines = base._header("Terrain layers", sky_rgb=(0.02, 0.02, 0.025))
    lines += base._camera(next_id, (0.0, LAYERS_CAMERA_HEIGHT, 0.0),
                          rotation=(-math.pi / 2.0, 0, 0), profile=profile)
    lines += _sun(next_id, (-math.pi / 2.0, 0.0, 0.0))
    lines += _terrain_entity(next_id, "Layers", handle, LAYERS_SIZE, LAYERS_HEIGHT,
                             material=red, layers=(blue,))
    return "\n".join(lines) + "\n"


def build_brush_scene(handle, red, blue, profile=None):
    """The ridge's geometry under the ridge's camera, red where nothing is
    painted and blue where the brush paints layer 1: what `--brush` sculpts
    and check_terrain measures (7ar)."""
    next_id = base._ids()
    lines = base._header("Terrain brush", sky_rgb=(0.02, 0.02, 0.025))
    lines += base._camera(next_id, RIDGE_CAMERA_POSITION, rotation=(RIDGE_CAMERA_PITCH, 0, 0),
                          profile=profile)
    lines += _sun(next_id, RIDGE_SUN_ROTATION)
    lines += _terrain_entity(next_id, "Brush", handle, RIDGE_SIZE, RIDGE_HEIGHT,
                             material=red, layers=(blue,))
    return "\n".join(lines) + "\n"


def build_under_scene(handle, profile=None):
    """The ridge from three metres under its plain (7ap, the skirts drawn
    only from above the ground): sky, and nothing else."""
    next_id = base._ids()
    lines = base._header("Terrain under", sky_rgb=UNDER_SKY)
    lines += base._camera(next_id, UNDER_CAMERA_POSITION, rotation=UNDER_CAMERA_ROTATION,
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


def write_fixture_scenes(scenes, hills_handle, ridge_handle, cliff_handle, layers_handle,
                         brush_handle, materials):
    """The six scenes and a post profile beside each, exactly as the check
    writes them, so a checkout and a check run agree to the byte."""
    import postprofile
    ridge = scenes / "terrain_ridge.rage"
    cliff = scenes / "terrain_cliff.rage"
    hills = scenes / "terrain.rage"
    painted = scenes / "terrain_layers.rage"
    brush = scenes / "terrain_brush.rage"
    under = scenes / "terrain_under.rage"
    write_scene(under, build_under_scene(ridge_handle, postprofile.write_beside(under, CHECK_PROFILE)))
    write_scene(brush, build_brush_scene(brush_handle, materials["red"], materials["blue"],
                                         postprofile.write_beside(brush, CHECK_PROFILE)))
    write_scene(hills, build_hills_scene(hills_handle, postprofile.write_beside(hills, CHECK_PROFILE),
                                         moss=materials["moss"], sand=materials["sand"]))
    write_scene(ridge, build_ridge_scene(ridge_handle, postprofile.write_beside(ridge, CHECK_PROFILE)))
    write_scene(cliff, build_cliff_scene(cliff_handle, postprofile.write_beside(cliff, CHECK_PROFILE)))
    write_scene(painted, build_layers_scene(layers_handle, materials["red"], materials["blue"],
                                            postprofile.write_beside(painted, CHECK_PROFILE)))


def write_materials(materials_dir):
    """The layers the fixtures name: two flat colours for the claim, and the
    hills' moss and sand -- the demo soil's maps under a tint, tiled as it
    is, so the three layers differ in colour and nothing else."""
    return {
        "red": write_material(materials_dir / "terrain_layer_red.rmat",
                              "materials/terrain_layer_red.rmat", LAYERS_RED),
        "blue": write_material(materials_dir / "terrain_layer_blue.rmat",
                               "materials/terrain_layer_blue.rmat", LAYERS_BLUE),
        "moss": write_material(materials_dir / "terrain_moss.rmat",
                               "materials/terrain_moss.rmat", (0.55, 0.72, 0.38),
                               maps=SOIL_MAPS, tiling=(6.0, 6.0), height_scale=0.05),
        "sand": write_material(materials_dir / "terrain_sand.rmat",
                               "materials/terrain_sand.rmat", (0.92, 0.84, 0.62),
                               maps=SOIL_MAPS, tiling=(6.0, 6.0), height_scale=0.05),
    }


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
    hill_heights = hills()
    hills_handle = write_terrain(terrain_dir / "hills.rvterrain", hill_heights, "terrain/hills.rvterrain",
                                 weights=hills_weights(hill_heights))
    ridge_handle = write_terrain(terrain_dir / "ridge.rvterrain", ridge(), "terrain/ridge.rvterrain")
    cliff_handle = write_terrain(terrain_dir / "cliff.rvterrain", cliff(), "terrain/cliff.rvterrain")
    layer_heights, layer_weights = layers()
    layers_handle = write_terrain(terrain_dir / "layers.rvterrain", layer_heights,
                                  "terrain/layers.rvterrain", weights=layer_weights)
    brush_handle = write_terrain(terrain_dir / "brush.rvterrain", ridge(), "terrain/brush.rvterrain")

    materials = write_materials(ASSETS / "materials")
    scenes = ASSETS / "scenes"
    write_fixture_scenes(scenes, hills_handle, ridge_handle, cliff_handle, layers_handle,
                         brush_handle, materials)
    print(f"wrote {terrain_dir / 'hills.rvterrain'} ({HILLS_RESOLUTION}^2, painted), "
          f"{terrain_dir / 'ridge.rvterrain'} ({RIDGE_RESOLUTION}^2), "
          f"{terrain_dir / 'cliff.rvterrain'} ({CLIFF_RESOLUTION}^2), "
          f"{terrain_dir / 'layers.rvterrain'} ({LAYERS_RESOLUTION}^2, painted), "
          f"{terrain_dir / 'brush.rvterrain'} ({RIDGE_RESOLUTION}^2), "
          f"four materials and six scenes")


if __name__ == "__main__":
    main()
