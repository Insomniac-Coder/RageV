#!/usr/bin/env python3
"""Writes the tangent-frame parity fixture: a scene whose correct render is
known before either backend draws it.

VK and GL disagree on normal-mapped surfaces (mean 14.7/255 on
material_closeup.rage). A backend diff cannot say which one is wrong -- two
backends can agree and both be wrong -- so this fixture states the answer:

  - The built-in Plane's UVs run +u along world +X (Mesh.cpp, AddQuadFace
    with right = (1,0,0)), so a tangent-space normal tilted 45 degrees
    toward +u MUST come out tilted toward world +X.
  - The light travels toward (-0.866, -0.5, 0): it shines from the +X
    side, 30 degrees up. L, toward the light, is (0.866, 0.5, 0).
  - Correct frame:  N = (0.707, 0.707, 0),  N.L = 0.97  -- nearly full.
  - Frame rotated 180 about the geometric normal (what a flipped dFdy
    does to the screen-derivative construction): N = (-0.707, 0.707, 0),
    N.L = 0 -- lit by ambient alone.
  - The flat control plane beside it: N = (0, 1, 0), N.L = 0.5.

The light is low and dim (intensity 1.5) on purpose: it puts both planes
on the near-linear part of the ACES curve, where the 1.9x linear gap
between 0.97 and 0.5 survives into the pixels. At 45 degrees and
intensity 3 both planes land on the shoulder and the gap compresses to a
few percent -- measured, not conjectured.

So the verdict is an ordering, not a threshold: a truthful backend renders
the tilted plane BRIGHTER than the control; a backend whose frame is
rotated renders it nearly black. The two cannot be confused.

Layout, at 800x640 with the overhead camera this writes: the tilted plane
is the LEFT half of the image, the control is the RIGHT half.
tools/scripts/check_tangent_frame.py measures a render and says which case
it sees.

Usage:
    python tools/scripts/make_parity_fixture.py
"""

import os
import struct
import sys
import zlib

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
MATERIALS = os.path.join(ROOT, "SampleProject", "assets", "materials")
SCENES = os.path.join(ROOT, "SampleProject", "assets", "scenes")

# Fixed handles, so the scene can reference the materials without a registry
# scan happening first. 7741... is this fixture's block; nothing else in the
# repository uses it.
H_NORMAL_PNG = 7741000000000000101
H_TILT_RMAT = 7741000000000000102
H_FLAT_RMAT = 7741000000000000103
H_SCENE = 7741000000000000104

PLANE_MESH = 8241982477996916738  # built-in Plane primitive


def png(width, height, pixel):
    """A PNG from a function of (x, y) -> (r, g, b). No dependencies."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter type: none
        for x in range(width):
            rows.extend(bytes(pixel(x, y)))

    def chunk(tag, data):
        out = struct.pack(">I", len(data)) + tag + data
        return out + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
            + chunk(b"IEND", b""))


def write(path, data):
    mode = "wb" if isinstance(data, bytes) else "w"
    with open(path, mode) as f:
        f.write(data)
    print(f"  {os.path.relpath(path, ROOT)}")


def meta(handle, asset_type):
    # SourceHash 0 is fine: the registry hashes the file on scan, sees a
    # change, and rewrites the sidecar keeping this handle.
    return f"Handle: {handle}\nType: {asset_type}\nSourceHash: 0"


def material(name, normal_map):
    maps = f"\nMaps:\n  Normal: {normal_map}" if normal_map else ""
    # Roughness 1, metallic 0: as diffuse as the BRDF gets, so the picture is
    # dominated by N.L -- the one number the fixture makes a statement about.
    return (f"Material: {name}\n"
            "BaseColor: [1, 1, 1, 1]\n"
            "Emissive: [0, 0, 0, 1]\n"
            "Metallic: 0\n"
            "Roughness: 1\n"
            "Occlusion: 1\n"
            "NormalScale: 1\n"
            "Specular: 0.5\n"
            "HeightScale: 0\n"
            "Tiling: [1, 1]\n"
            "UvOffset: [0, 0]" + maps)


# Every texel tilted 45 degrees toward +u: n = (0.707, 0, 0.707), stored
# as (n + 1) / 2. Constant on purpose -- any pattern would leave room to
# blame the pattern's orientation instead of the frame's.
def tilt_toward_u(x, y):
    return (218, 128, 218)


SCENE = """Scene: Tangent-frame parity
Version: 5
Environment:
  AmbientColor: [1, 1, 1]
  AmbientIntensity: 0.05
  Sky: 0
Entities:
  - EntityID: 7741000000000000201
    TagComponent:
      Tag: Overhead Camera
    TransformComponent:
      Position: [0, 6, 0]
      Rotation: [-1.5707963, 0, 0]
      Scale: [1, 1, 1]
    CameraComponent:
      ViewRank: 0
      FixedAspectRatio: false
      ProjectionType: Perspective
      PerspectiveFOV: 45
      PerspectiveNearClip: 0.01
      PerspectiveFarClip: 1000
      OrthographicScale: 10
      OrthographicNearClip: -1
      OrthographicFarClip: 1
  - EntityID: 7741000000000000202
    TagComponent:
      Tag: Light from +X, 30 degrees up
    TransformComponent:
      Position: [0, 4, 0]
      Rotation: [-0.5235988, 1.5707963, 0]
      Scale: [1, 1, 1]
    LightComponent:
      Type: Directional
      Color: [1, 1, 1]
      Intensity: 1.5
      Range: 20
      InnerCone: 12
      OuterCone: 25
      CastShadows: false
  - EntityID: 7741000000000000203
    TagComponent:
      Tag: Tilted normal map (left)
    TransformComponent:
      Position: [-1.3, 0, 0]
      Rotation: [0, 0, 0]
      Scale: [2, 1, 2]
    MeshComponent:
      Static: true
      Mesh: {plane}
      Material: {tilt}
  - EntityID: 7741000000000000204
    TagComponent:
      Tag: Flat control (right)
    TransformComponent:
      Position: [1.3, 0, 0]
      Rotation: [0, 0, 0]
      Scale: [2, 1, 2]
    MeshComponent:
      Static: true
      Mesh: {plane}
      Material: {flat}
""".format(plane=PLANE_MESH, tilt=H_TILT_RMAT, flat=H_FLAT_RMAT)


def main():
    print("Writing the tangent-frame parity fixture:")

    p = os.path.join(MATERIALS, "parity_tilt_normal.png")
    write(p, png(64, 64, tilt_toward_u))
    write(p + ".meta", meta(H_NORMAL_PNG, "Texture"))

    p = os.path.join(MATERIALS, "parity_tilt.rmat")
    write(p, material("parity_tilt", H_NORMAL_PNG))
    write(p + ".meta", meta(H_TILT_RMAT, "Material"))

    p = os.path.join(MATERIALS, "parity_flat.rmat")
    write(p, material("parity_flat", None))
    write(p + ".meta", meta(H_FLAT_RMAT, "Material"))

    p = os.path.join(SCENES, "parity_frame.rage")
    write(p, SCENE)
    write(p + ".meta", meta(H_SCENE, "Scene"))

    print("Stated answer: tilted plane (left) brighter than control (right).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
