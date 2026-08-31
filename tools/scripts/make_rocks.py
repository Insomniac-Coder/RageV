#!/usr/bin/env python3
"""Rocks, pebbles and cliff blocks for the Golden Gate scene.

    python tools/scripts/make_rocks.py

**Why geometry and not more texture.** The hero camera stands a few metres
from the Marin headland, and at that range a 1K map tiled every 2 m is past
its own resolution: one texel covers several pixels and the cliff reads as
smooth clay whatever the shader does with it. A terrain heightfield also
cannot be vertical -- at 2.73 m between samples the steepest face it can make
is a slope -- so the cliffs are ramps wearing a stretched planar uv. Both are
resolution problems and neither has a shader answer. Real rock in front of
them does have one: it brings its own silhouette, its own uv, and its own
texel density at the distance it is actually seen from.

## How the shapes are made

An icosphere, subdivided, with every vertex pushed along its own direction by

    r = 1 + a few broad lobes + fine grain

**The lobes are what make it a rock rather than a lumpy ball.** A handful of
`max(0, dot(direction, axis)) ** sharpness` terms at random axes produce broad
flattish faces meeting at edges, which is what a fractured stone is. Noise
alone gives a potato: bumpy everywhere and featureless, because it has no
scale larger than its own frequency. The fine octave on top is the grain that
keeps the flats from looking machined.

`sharpness` is the dial between the two readings. Low values give the rounded,
water-worn stone that belongs on a beach; high values give the angular freshly
split blocks that belong at the foot of a cliff.

**Cliff blocks are the same generator, anisotropically scaled and bedded.**
Squashing one axis and adding a strong vertical-frequency term gives
horizontal bedding planes, which is what the serpentine and chert of the Marin
Headlands actually do, and what tells the eye "cliff" rather than "big rock".

## Fidelity

Subdivision 3 is 1 280 triangles and 4 is 5 120. These are seen from tens of
metres rather than hundreds, so they get the higher counts: the whole set below
is about the size of the bridge deck, against a terrain that draws two million
triangles a frame.

**There are no mesh LODs in this engine yet** (NEXT.md item 5), so whatever is
authored is drawn at every distance. That is the argument for a moderate number
of good stones rather than a field of cheap ones.

Smooth-shaded, and that is load-bearing: `smooth_normals` averages across faces
within 60 degrees and leaves the rest hard, so a displaced icosphere comes out
as curved surfaces meeting at real edges rather than as visible facets.
"""

import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import fbxwrite  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "SampleProject" / "assets" / "models" / "rocks"
MATERIALS = ROOT / "SampleProject" / "assets" / "materials"
TEXTURES = ROOT / "SampleProject" / "assets" / "textures"


# --- deterministic noise ------------------------------------------------------

def _hash3(x, y, z, seed):
    """One value in [0, 1) from a lattice point.

    FNV-1a over the coordinates, which is the mixing every other generator in
    this toolkit uses and needs no dependency. Deterministic, so a seed names
    a stone: regenerating the assets cannot quietly reshape the scene.
    """
    h = 0xCBF29CE484222325 ^ ((seed * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF)
    for v in (x, y, z):
        h ^= (v + 0x8000) & 0xFFFF
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return ((h >> 24) & 0xFFFFFF) / 16777216.0


def _smooth(t):
    return t * t * (3.0 - 2.0 * t)


def value_noise(p, seed):
    """Trilinear value noise at a 3D point, smoothstep interpolated."""
    xi, yi, zi = math.floor(p[0]), math.floor(p[1]), math.floor(p[2])
    fx, fy, fz = _smooth(p[0] - xi), _smooth(p[1] - yi), _smooth(p[2] - zi)
    out = 0.0
    for dz in (0, 1):
        wz = fz if dz else 1.0 - fz
        for dy in (0, 1):
            wy = fy if dy else 1.0 - fy
            for dx in (0, 1):
                wx = fx if dx else 1.0 - fx
                out += wx * wy * wz * _hash3(int(xi) + dx, int(yi) + dy,
                                             int(zi) + dz, seed)
    return out


# --- the base sphere ----------------------------------------------------------

def icosphere(subdivisions):
    """A geodesic sphere: an icosahedron with every triangle split four ways.

    Chosen over a uv sphere because its triangles are near-equilateral
    everywhere. A uv sphere crowds hundreds of slivers at each pole, and a
    displaced pole then shows a pinch that no amount of smoothing hides.
    """
    t = (1.0 + math.sqrt(5.0)) / 2.0
    raw = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0),
           (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
           (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    points = []
    for x, y, z in raw:
        length = math.sqrt(x * x + y * y + z * z)
        points.append((x / length, y / length, z / length))

    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
             (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
             (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
             (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]

    for _ in range(subdivisions):
        midpoints = {}

        def middle(a, b):
            key = (a, b) if a < b else (b, a)
            if key not in midpoints:
                pa, pb = points[a], points[b]
                m = [(pa[i] + pb[i]) * 0.5 for i in range(3)]
                length = math.sqrt(sum(c * c for c in m))
                points.append((m[0] / length, m[1] / length, m[2] / length))
                midpoints[key] = len(points) - 1
            return midpoints[key]

        split = []
        for a, b, c in faces:
            ab, bc, ca = middle(a, b), middle(b, c), middle(c, a)
            split += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = split

    return points, faces


# --- the stone -----------------------------------------------------------------

def stone(seed, subdivisions=3, lobes=6, sharpness=2.4, lobe_amount=0.30,
          grain=0.12, grain_frequency=3.4, scale=(1.0, 1.0, 1.0),
          bedding=0.0, sole=None):
    """One stone as (points, faces). Every parameter is a shape decision.

    `lobe_amount` and `sharpness` set how faceted it is, `grain` the fine
    surface, `bedding` adds horizontal strata, and `sole` cuts a flat bottom so
    the thing sits on the ground instead of balancing on a curve.
    """
    points, faces = icosphere(subdivisions)

    axes = []
    for k in range(lobes):
        # Directions from the same hash, so a seed reproduces a stone exactly.
        u = _hash3(k, 1, 0, seed) * 2.0 - 1.0
        phi = _hash3(k, 2, 0, seed) * 2.0 * math.pi
        r = math.sqrt(max(0.0, 1.0 - u * u))
        axes.append((r * math.cos(phi), u, r * math.sin(phi),
                     0.55 + 0.45 * _hash3(k, 3, 0, seed)))

    shaped = []
    for x, y, z in points:
        radius = 1.0
        for ax, ay, az, weight in axes:
            d = x * ax + y * ay + z * az
            if d > 0.0:
                radius += lobe_amount * weight * (d ** sharpness)

        # Grain, sampled on the *direction* so it does not swim when the stone
        # is scaled anisotropically below.
        radius += grain * (value_noise((x * grain_frequency + 8.0,
                                        y * grain_frequency + 8.0,
                                        z * grain_frequency + 8.0), seed) - 0.5)

        # Bedding: a vertical-only ripple. It reads as strata because it is
        # coherent horizontally and busy vertically, which is the one thing
        # that separates sedimentary rock from a boulder.
        if bedding > 0.0:
            radius += bedding * (value_noise((x * 0.7 + 3.0, y * 9.0 + 3.0,
                                              z * 0.7 + 3.0), seed + 17) - 0.5)

        shaped.append((x * radius * scale[0],
                       y * radius * scale[1],
                       z * radius * scale[2]))

    if sole is not None:
        # **A sole, not a slice.** Vertices below the plane are lifted onto it
        # rather than clipped, so the mesh stays closed. An open bottom is a
        # hole the shadow pass draws straight through, and check_models would
        # rightly refuse it.
        shaped = [(x, max(y, sole), z) for x, y, z in shaped]

    return shaped, faces


# --- the catalogue -------------------------------------------------------------
#
# **Three families, and the difference between them is not size.** A pebble is
# a rounded stone, a boulder is an angular one, and a cliff block is a bedded
# slab -- so they differ in `sharpness` and `bedding` first and in scale second.
# Three sizes of one shape reads as three copies of one shape, which is the
# thing that gives scattered geometry away.
#
# name, subdiv, and the shape arguments.
CATALOGUE = [
    # Beach pebbles: water-worn, so low sharpness and a strong sole -- a pebble
    # on sand is half buried, and one balanced on its curve reads as floating.
    ("pebble_a", 2, dict(seed=101, lobes=5, sharpness=1.5, lobe_amount=0.22,
                         grain=0.10, scale=(1.00, 0.62, 0.86), sole=-0.34)),
    ("pebble_b", 2, dict(seed=202, lobes=6, sharpness=1.3, lobe_amount=0.26,
                         grain=0.09, scale=(0.88, 0.55, 1.00), sole=-0.30)),
    ("pebble_c", 2, dict(seed=303, lobes=4, sharpness=1.8, lobe_amount=0.20,
                         grain=0.12, scale=(1.00, 0.70, 0.78), sole=-0.36)),

    # Shore boulders: angular, still worn. These sit at the waterline where
    # the headland sheds into the strait.
    ("rock_a", 3, dict(seed=411, lobes=7, sharpness=2.6, lobe_amount=0.32,
                       grain=0.13, scale=(1.00, 0.78, 0.92), sole=-0.52)),
    ("rock_b", 3, dict(seed=522, lobes=6, sharpness=3.2, lobe_amount=0.36,
                       grain=0.11, scale=(0.90, 0.86, 1.00), sole=-0.48)),
    ("rock_c", 3, dict(seed=633, lobes=8, sharpness=2.2, lobe_amount=0.28,
                       grain=0.15, scale=(1.00, 0.66, 0.88), sole=-0.55)),

    # **No cliff family.** Blocks big enough to cover a face read as patches
    # stuck on a hillside, and panels read worse -- a rectangle's silhouette is
    # not a cliff's. Authored rock is coming for that job instead.
]

# Metres of surface per texture repeat. **Not one number for all three.** A
# pebble the size of a fist wearing a 2 m tile shows a fifth of one texel's
# worth of pattern and comes out flat; a cliff block wearing a pebble's tiling
# turns into gravel. Each family is mapped at the scale it is actually seen at.
UV_METRES = {"pebble": 0.35, "rock": 1.10}


def build(name, subdivisions, arguments):
    points, faces = stone(subdivisions=subdivisions, **arguments)
    mesh = fbxwrite.Mesh()
    family = name.split("_")[0]
    uv = fbxwrite.box_uv(points, faces, UV_METRES[family])
    mesh.add(points, faces, uv=uv, smooth=True)
    return mesh


# --- assets --------------------------------------------------------------------

def fnv1a(data):
    h = 0xCBF29CE484222325
    for byte in data:
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def handle_for(name):
    return fnv1a(name.encode("utf-8")) or 0x7261676556444D4F


def ensure_meta(path, kind):
    """Mint the sidecar if the registry has not. Never overwrite one: the
    handle would change and every entity pointing at it would draw nothing."""
    meta = path.with_name(path.name + ".meta")
    if meta.exists():
        return
    meta.write_text("Handle: {0}\nType: {1}\nSourceHash: {2}".format(
        handle_for(path.name), kind, fnv1a(path.read_bytes())),
        encoding="utf-8", newline="\n")


def write_materials():
    """Two materials for the whole set, and that is deliberate.

    Rock050 for anything that reads as cut stone, Gravel032 for the pebbles --
    a beach pebble is a rounded, lighter, wetter thing than a cliff face and
    painting both with one map is most of why scattered rock looks placed.

    White base colour, because the maps carry the colour. The terrain layers
    were tinted to a linear albedo of 0.017 and came out darker than charcoal;
    that mistake is not worth repeating here.
    """
    written = {}
    for name, texture, rough, height in (("rock_stone", "acg_rock", 1.0, 0.05),
                                         ("rock_pebble", "acg_gravel", 1.0, 0.04)):
        maps = {}
        for key, suffix in (("BaseColor", "color"), ("Normal", "normal"),
                            ("Roughness", "roughness"), ("Occlusion", "ao"),
                            ("Height", "height")):
            for extension in (".jpg", ".png"):
                file = TEXTURES / "{0}_{1}{2}".format(texture, suffix, extension)
                if file.exists():
                    maps[key] = handle_for(file.name)
                    break

        lines = ["Material: " + name,
                 "BaseColor: [1, 1, 1, 1]",
                 "Emissive: [0, 0, 0, 1]",
                 "Metallic: 0",
                 "Roughness: {0:g}".format(rough),
                 "Occlusion: 1",
                 # Pushed past 1 for the same reason the bridge does it: the
                 # relief in these maps is real millimetres and honest
                 # millimetres disappear at the range a headland is seen from.
                 "NormalScale: 1.5",
                 "Specular: 0.5",
                 "HeightScale: {0:g}".format(height),
                 "Tiling: [1, 1]",
                 "UvOffset: [0, 0]",
                 "Maps:"]
        for key in ("BaseColor", "Normal", "Roughness", "Occlusion", "Height"):
            if key in maps:
                lines.append("  {0}: {1}".format(key, maps[key]))

        path = MATERIALS / (name + ".rmat")
        path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        key = "materials/" + name + ".rmat"
        path.with_name(path.name + ".meta").write_text(
            "Handle: {0}\nType: Material\nSourceHash: {1}".format(
                handle_for(key), fnv1a(path.read_bytes())),
            encoding="utf-8", newline="\n")
        written[name] = handle_for(key)
    return written


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    materials = write_materials()
    print("  materials  " + "  ".join("{0} {1}".format(k, v)
                                      for k, v in sorted(materials.items())))

    total = 0
    for name, subdivisions, arguments in CATALOGUE:
        mesh = build(name, subdivisions, arguments)
        path = OUT / (name + ".fbx")
        points, faces, _ = fbxwrite.write(path, mesh, name)
        ensure_meta(path, "Mesh")
        low = [min(p[i] for p in mesh.points) for i in range(3)]
        high = [max(p[i] for p in mesh.points) for i in range(3)]
        print("  {0:10} {1:6} verts {2:6} faces   {3:.2f} x {4:.2f} x {5:.2f} m".format(
            name, points, faces, high[0] - low[0], high[1] - low[1], high[2] - low[2]))
        total += faces

    print("{0} assets, {1} faces in all, in {2}".format(
        len(CATALOGUE), total, OUT))


if __name__ == "__main__":
    main()
