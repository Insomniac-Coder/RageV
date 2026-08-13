#!/usr/bin/env python3
"""Writes a textured glTF cube, and the PNGs it references.

Every model in the repository was untextured, which is precisely why texture
maps could be dropped on save for as long as they were: nothing in the tree
exercised the path. This makes the fixture that does.

The textures are generated rather than downloaded so the check is reproducible
and the repository stays small -- and, more usefully, so each map is a *pattern
that can be recognised in a screenshot*: base colour is a two-colour check,
metallic-roughness sweeps roughness along one axis, and the normal map is a
dimpled grid. A flat texture would prove the binding and nothing about whether
the right map reached the right sampler.

Usage:
    python tools/scripts/make_textured_model.py SampleProject/assets/models
"""

import base64
import json
import os
import struct
import sys
import zlib


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


def base_colour(x, y):
    # A check, so a wrong UV or a swapped axis is visible rather than plausible.
    return (200, 60, 60) if ((x // 16) + (y // 16)) % 2 else (240, 235, 225)


def metallic_roughness(x, y):
    # glTF packing: roughness in green, metallic in blue. Roughness sweeps left
    # to right, so the direction of the gradient is checkable in a render.
    return (0, int(255 * x / 127), 40)


def normal_map(x, y):
    # Dimples. Tangent space, so flat is (128, 128, 255).
    import math
    u = (x % 32) / 32.0 - 0.5
    v = (y % 32) / 32.0 - 0.5
    r = math.sqrt(u * u + v * v)
    scale = max(0.0, 1.0 - r * 3.0)
    return (int(128 + 90 * u * scale), int(128 + 90 * v * scale), 255)


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(target, exist_ok=True)

    maps = {
        "textured_basecolor.png": base_colour,
        "textured_mr.png": metallic_roughness,
        "textured_normal.png": normal_map,
    }
    for name, fn in maps.items():
        path = os.path.join(target, name)
        with open(path, "wb") as handle:
            handle.write(png(128, 128, fn))
        print("wrote", path)

    # A unit cube: 24 vertices so each face has its own normal and UVs.
    positions, normals, uvs, indices = [], [], [], []
    faces = [
        ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
        ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
        ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
        ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
        ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
        ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ]
    for axis, right, up in faces:
        base = len(positions)
        for sx, sy, u, v in ((-1, -1, 0, 1), (1, -1, 1, 1), (1, 1, 1, 0), (-1, 1, 0, 0)):
            positions.append([
                (axis[i] + right[i] * sx + up[i] * sy) * 0.5 for i in range(3)
            ])
            normals.append(list(axis))
            uvs.append([u, v])
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]

    blob = bytearray()
    views, accessors = [], []

    def add(values, count, kind, component, target_hint):
        offset = len(blob)
        for value in values:
            blob.extend(struct.pack("<" + ("f" * len(value)), *value)
                        if isinstance(value, list) else struct.pack("<H", value))
        views.append({"buffer": 0, "byteOffset": offset,
                      "byteLength": len(blob) - offset, "target": target_hint})
        accessor = {"bufferView": len(views) - 1, "componentType": component,
                    "count": count, "type": kind}
        if kind == "VEC3" and component == 5126:
            accessor["min"] = [min(v[i] for v in values) for i in range(3)]
            accessor["max"] = [max(v[i] for v in values) for i in range(3)]
        accessors.append(accessor)
        return len(accessors) - 1

    p = add(positions, len(positions), "VEC3", 5126, 34962)
    n = add(normals, len(normals), "VEC3", 5126, 34962)
    t = add(uvs, len(uvs), "VEC2", 5126, 34962)
    i = add(indices, len(indices), "SCALAR", 5123, 34963)

    gltf = {
        "asset": {"version": "2.0", "generator": "make_textured_model.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "TexturedCube"}],
        "meshes": [{"name": "TexturedCube", "primitives": [
            {"attributes": {"POSITION": p, "NORMAL": n, "TEXCOORD_0": t},
             "indices": i, "material": 0}]}],
        "materials": [{
            "name": "Textured",
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0},
                "metallicRoughnessTexture": {"index": 1},
                "metallicFactor": 1.0,
                "roughnessFactor": 1.0,
            },
            "normalTexture": {"index": 2},
        }],
        "textures": [{"source": 0}, {"source": 1}, {"source": 2}],
        "images": [{"uri": name} for name in maps],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(blob),
                     "uri": "data:application/octet-stream;base64,"
                            + base64.b64encode(bytes(blob)).decode()}],
    }

    path = os.path.join(target, "textured.gltf")
    with open(path, "w") as handle:
        json.dump(gltf, handle, indent=1)
    print("wrote", path)


if __name__ == "__main__":
    main()
