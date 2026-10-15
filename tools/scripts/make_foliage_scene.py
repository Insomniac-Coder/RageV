"""An alpha-tested foliage scene, for the scale benchmark and RT2-38/RT2-39.

    python tools/scripts/make_foliage_scene.py            # 20000 meshes
    python tools/scripts/make_foliage_scene.py --count 60000

**What it is for.** Every other scale scene is opaque, and the ray-tracing
items that care about foliage -- opacity micromaps (RT2-38) and instanced,
wind-driven vegetation (RT2-39) -- need geometry whose silhouette comes from a
texture's alpha: a ray has to run the cutout test inside traversal for every
leaf card it crosses, which is the cost those items exist to cut. So: trees
(a bark trunk and a crown of crossed leaf cards, two meshes under one root)
and bushes (cards only), scattered at the cube scenes' density.

**Generated, not imported.** The leaf atlas is drawn here (leaf-shaped
ellipses in a cluster, alpha 1 inside and 0 outside, cut at 0.5 by a Masked
material), the cards and trunk are written as FBX by `fbxwrite`, and every
asset's `.meta` is minted with the registry's own handle arithmetic, as the
camp's generators do. The cards carry both windings, because a material has no
two-sided switch and a leaf seen from behind must not vanish.
"""
import argparse
import io
import math
import pathlib
import random
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fbxwrite  # noqa: E402
from make_camp_textures import png, write_meta  # noqa: E402
from make_scale_scenes import entity, transform, mesh, PLANE  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "SampleProject" / "assets"
MODELS = ASSETS / "models" / "foliage"
TEXTURE = ASSETS / "textures" / "foliage_leaves.png"
MATERIAL = ASSETS / "materials" / "foliage_leaves.rmat"
TRUNK_MATERIAL = ASSETS / "materials" / "camp_trunk.rmat"


def handle_of(path):
    return int(pathlib.Path(str(path) + ".meta").read_text(encoding="utf-8").split()[1])


def leaf_atlas(size=512, leaves=150, seed=20261015):
    """A cluster of leaves: colour in rgb, coverage in alpha."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:size, 0:size] / size
    rgba = np.zeros((size, size, 4))
    for _ in range(leaves):
        # Inside a rough disc, denser toward the middle.
        r = 0.42 * math.sqrt(rng.random())
        a = rng.random() * 2.0 * math.pi
        cx, cy = 0.5 + r * math.cos(a), 0.5 + r * math.sin(a)
        angle = rng.random() * math.pi
        length, width = rng.uniform(0.045, 0.075), rng.uniform(0.016, 0.026)
        dx, dy = x - cx, y - cy
        u = dx * math.cos(angle) + dy * math.sin(angle)
        v = -dx * math.sin(angle) + dy * math.cos(angle)
        inside = (u / length) ** 2 + (v / width) ** 2 <= 1.0
        shade = rng.uniform(0.75, 1.15)
        colour = np.array([0.16, 0.34, 0.09]) * shade
        # A paler midrib along the leaf's long axis.
        rib = inside & (np.abs(v) < width * 0.12)
        rgba[inside, :3] = colour
        rgba[rib, :3] = colour * 1.35
        rgba[inside, 3] = 1.0
    return rgba


def card(mesh, centre, width, height, yaw, tilt):
    """One leaf card, both windings, uv over the whole atlas."""
    c, s = math.cos(yaw), math.sin(yaw)
    ct, st = math.cos(tilt), math.sin(tilt)
    hw, hh = width * 0.5, height * 0.5
    corners = []
    for px, py in ((-hw, -hh), (hw, -hh), (hw, hh), (-hw, hh)):
        # Tilted about the card's own horizontal axis, then turned by yaw.
        lx, ly, lz = px, py * ct, py * st
        corners.append((centre[0] + lx * c + lz * s, centre[1] + ly, centre[2] - lx * s + lz * c))
    uv = ((0, 0), (1, 0), (1, 1), (0, 1))
    mesh.add(corners, [(0, 1, 2, 3)], uv=[uv])
    mesh.add(corners, [(3, 2, 1, 0)], uv=[tuple(reversed(uv))])


def crown(cards, radius, height, base, seed):
    rng = random.Random(seed)
    m = fbxwrite.Mesh()
    for i in range(cards):
        a = rng.random() * 2.0 * math.pi
        r = radius * math.sqrt(rng.random())
        y = base + rng.random() * height
        card(m, (r * math.cos(a), y, r * math.sin(a)), 1.6, 1.6,
             rng.random() * math.pi, rng.uniform(-0.5, 0.5))
    return m


def write_assets():
    TEXTURE.parent.mkdir(parents=True, exist_ok=True)
    png(TEXTURE, leaf_atlas())
    texture = write_meta(TEXTURE, "Texture")

    MATERIAL.write_text("\n".join([
        "Material: foliage_leaves",
        "BaseColor: [1, 1, 1, 1]",
        "Emissive: [0, 0, 0, 1]",
        "Metallic: 0",
        "Roughness: 0.8",
        "Occlusion: 1",
        "NormalScale: 1",
        "Specular: 0.4",
        "HeightScale: 0",
        "Blend: Masked",
        "AlphaCutoff: 0.5",
        "Tiling: [1, 1]",
        "UvOffset: [0, 0]",
        "Maps:",
        f"  BaseColor: {texture}",
    ]) + "\n", encoding="utf-8")
    material = write_meta(MATERIAL, "Material")

    MODELS.mkdir(parents=True, exist_ok=True)
    parts = {
        "foliage_tree_crown.fbx": crown(28, 1.8, 3.2, 2.4, 1),
        "foliage_bush.fbx": crown(10, 0.9, 0.9, 0.2, 2),
    }
    trunk = fbxwrite.Mesh()
    trunk.cylinder(0.16, 3.4, sides=8, taper=0.6)
    parts["foliage_tree_trunk.fbx"] = trunk
    handles = {}
    for name, m in parts.items():
        path = MODELS / name
        fbxwrite.write(path, m, path.stem)
        handles[name] = write_meta(path, "Mesh")
    return handles, material, handle_of(TRUNK_MATERIAL)


def mesh_ref(handle, material, static=True):
    return f"""    MeshComponent:
      Static: {'true' if static else 'false'}
      Mesh: {handle}
      Material: {material}"""


def build(count, handles, leaves, bark, seed=20261015):
    rng = random.Random(seed)
    # Trees are two meshes and bushes one; three in five plants are trees.
    plants = round(count / 1.6)
    side = max(60.0, math.sqrt(plants * 6.0))
    out = [
        "Scene: Scale foliage",
        "Version: 6",
        "Environment:",
        "  AmbientColor: [0.42, 0.47, 0.58]",
        "  AmbientIntensity: 0.12",
        "  Sky: 2",
        "  SkyHorizon: [0.52, 0.6, 0.72]",
        "  SkyZenith: [0.18, 0.31, 0.62]",
        "  SkyGround: [0.16, 0.15, 0.14]",
        "  SkyIntensity: 1",
        "  SkyRotation: 0",
        "Entities:",
    ]
    # Lower than the cube scenes' camera: foliage is about silhouettes
    # against each other, which a view from high above flattens away.
    eye = (0.0, 12.0, side * 0.5)
    out.append(entity(11400714819323198485, "Scene Camera",
                      transform(eye, (-0.12, 0.0, 0.0)) + """
    CameraComponent:
      ViewRank: 0
      FixedAspectRatio: false
      ProjectionType: Perspective
      PerspectiveFOV: 60
      PerspectiveNearClip: 0.3
      PerspectiveFarClip: 2000
      OrthographicScale: 10
      OrthographicNearClip: -1
      OrthographicFarClip: 1
      PostProfile: 0"""))
    out.append(entity(15755400384260043839, "Sun",
                      transform((0, 0, 0), (-0.959931, -0.523599, 0.0)) + """
    LightComponent:
      Type: Directional
      Color: [0.55, 0.58, 0.65]
      Intensity: 1.6
      Range: 10
      InnerCone: 20
      OuterCone: 30
      CastShadows: true"""))
    out.append(entity(4354685564936845354, "Ground",
                      transform((0, -0.05, 0), scale=(side * 1.2, 1, side * 1.2)) +
                      "\n" + mesh(PLANE, (0.12, 0.16, 0.09), 0.9)))

    eid = 3_000_000
    meshes = 0
    for i in range(plants):
        x = rng.uniform(-side * 0.5, side * 0.5)
        z = rng.uniform(-side * 0.5, side * 0.5)
        yaw = rng.uniform(0.0, 6.283)
        size = rng.uniform(0.8, 1.3)
        if i % 5 < 3:
            root = eid
            out.append(entity(root, f"Tree {i}", transform((x, 0.0, z), (0.0, yaw, 0.0), (size, size, size))))
            for name, material in (("foliage_tree_trunk.fbx", bark), ("foliage_tree_crown.fbx", leaves)):
                eid += 1
                out.append(entity(eid, f"Tree {i} {name[13:-4]}", f"""    RelationshipComponent:
      Parent: {root}
""" + transform((0.0, 0.0, 0.0)) + "\n" + mesh_ref(handles[name], material)))
                meshes += 1
            eid += 1
        else:
            out.append(entity(eid, f"Bush {i}", transform((x, 0.0, z), (0.0, yaw, 0.0), (size, size, size))
                              + "\n" + mesh_ref(handles["foliage_bush.fbx"], leaves)))
            eid += 1
            meshes += 1
    return "\n".join(out) + "\n", meshes, side


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=20000)
    args = parser.parse_args()
    handles, leaves, bark = write_assets()
    scene, meshes, side = build(args.count, handles, leaves, bark)
    path = ASSETS / "scenes" / f"scale_foliage_{args.count}.rage"
    io.open(path, "w", encoding="utf-8", newline="").write(scene)
    print(f"  {path.name}: {meshes} meshes over {side:.0f} x {side:.0f} m; "
          f"atlas, material and {len(handles)} models written")


if __name__ == "__main__":
    main()
