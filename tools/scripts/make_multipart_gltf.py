#!/usr/bin/env python3
"""Write a two-primitive, two-material glTF: the smallest model that is a tree.

    python tools/scripts/make_multipart_gltf.py

Writes SampleProject/assets/models/multipart.gltf.

**What it is for.** A model with more than one material is not one entity. The
importer splits it into a primitive per (mesh, material) pair, and only the
*first* wears the model file's own handle -- the rest are `modelHandle + 1 + i`,
minted when the model is instantiated. A saved scene stores those numbers, so
they have to resolve on a cold load or reopening a scene loses every part of an
imported model except one piece.

Every model already in this project is single-primitive, so nothing could
exercise that. The showroom's car is the opposite problem: a hundred and fifty
primitives in thirty-six megabytes, which is a minute of parsing and not
something a test suite should do on every run.

So: two boxes, two materials, one node. Small enough to parse in microseconds
and different enough between the two primitives that a check can tell which one
it was handed -- the second has twice the triangles of the first, which no
amount of returning the wrong mesh can fake.
"""

import argparse
import base64
import json
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[2]


def box(low, high, split):
    """A box's corners and its faces, `split` of them in the second group.

    Returns (positions, normals, indices_a, indices_b) with the two index sets
    addressing one shared vertex buffer.
    """
    x0, y0, z0 = low
    x1, y1, z1 = high

    faces = [
        # corners counter-clockwise seen from outside, and the face's normal
        (((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)), (0, 0, 1)),
        (((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)), (0, 0, -1)),
        (((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)), (-1, 0, 0)),
        (((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)), (1, 0, 0)),
        (((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)), (0, 1, 0)),
        (((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)), (0, -1, 0)),
    ]

    positions, normals = [], []
    groups = [[], []]

    for index, (corners, normal) in enumerate(faces):
        base = len(positions)
        for corner in corners:
            positions.append(corner)
            normals.append(normal)

        # Each face is its own four vertices, so the normals stay hard.
        group = groups[1] if index >= len(faces) - split else groups[0]
        group += [base, base + 1, base + 2, base, base + 2, base + 3]

    return positions, normals, groups[0], groups[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        default=str(ROOT / "SampleProject" / "assets" / "models" / "multipart.gltf"))
    args = parser.parse_args()

    # Four faces on the first material, two on the second: 8 triangles and 4.
    # Deliberately unequal, so "which primitive is this" has an answer.
    positions, normals, first, second = box((-0.4, 0.0, -0.25), (0.4, 0.6, 0.25),
                                            split=2)

    blob = bytearray()
    views, accessors = [], []

    def add(data, fmt, kind, component, target=None, minmax=None):
        while len(blob) % 4:
            blob.append(0)

        offset = len(blob)
        for item in data:
            blob.extend(struct.pack(fmt, *item) if isinstance(item, tuple)
                        else struct.pack(fmt, item))

        views.append({"buffer": 0, "byteOffset": offset,
                      "byteLength": len(blob) - offset,
                      **({"target": target} if target else {})})

        accessor = {"bufferView": len(views) - 1, "componentType": component,
                    "count": len(data), "type": kind}
        if minmax:
            accessor["min"], accessor["max"] = minmax
        accessors.append(accessor)
        return len(accessors) - 1

    FLOAT, USHORT = 5126, 5123
    ARRAY, ELEMENTS = 34962, 34963

    lo = [min(p[i] for p in positions) for i in range(3)]
    hi = [max(p[i] for p in positions) for i in range(3)]

    position = add(positions, "<3f", "VEC3", FLOAT, ARRAY, (lo, hi))
    normal = add(normals, "<3f", "VEC3", FLOAT, ARRAY)
    index_a = add(first, "<H", "SCALAR", USHORT, ELEMENTS)
    index_b = add(second, "<H", "SCALAR", USHORT, ELEMENTS)

    gltf = {
        "asset": {"version": "2.0",
                  "generator": "RageV tools/scripts/make_multipart_gltf.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Multipart", "mesh": 0}],
        "meshes": [{
            "name": "MultipartMesh",
            "primitives": [
                {"attributes": {"POSITION": position, "NORMAL": normal},
                 "indices": index_a, "material": 0},
                {"attributes": {"POSITION": position, "NORMAL": normal},
                 "indices": index_b, "material": 1},
            ],
        }],
        "materials": [
            {"name": "Sides", "pbrMetallicRoughness": {
                "baseColorFactor": [0.72, 0.24, 0.18, 1.0],
                "metallicFactor": 0.0, "roughnessFactor": 0.7}},
            {"name": "Caps", "pbrMetallicRoughness": {
                "baseColorFactor": [0.18, 0.42, 0.68, 1.0],
                "metallicFactor": 0.0, "roughnessFactor": 0.35}},
        ],
        "bufferViews": views,
        "accessors": accessors,
        "buffers": [{"byteLength": len(blob),
                     "uri": "data:application/octet-stream;base64," +
                            base64.b64encode(bytes(blob)).decode("ascii")}],
    }

    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with open(output, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(gltf, handle, indent=1)
        handle.write("\n")

    print(f"{output.relative_to(ROOT)}: {len(positions)} vertices, "
          f"{len(first) // 3} + {len(second) // 3} triangles, 2 materials")


if __name__ == "__main__":
    main()
