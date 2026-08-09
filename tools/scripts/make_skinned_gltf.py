#!/usr/bin/env python3
"""Write a minimal skinned glTF: a two-bone limb that bends.

The importer needs something to import and the renderer something to draw, and
downloading a character to test against makes the test depend on a file nobody
can regenerate -- the exact problem the sky.hdr generator exists to fix. This
emits one small self-contained .gltf whose every number is chosen here, so a
wrong bind matrix or a swapped weight is a discrepancy against something known
rather than against a model somebody exported once.

    python tools/scripts/make_skinned_gltf.py \
        --output SampleProject/assets/models/limb.gltf

The shape is a square-section post two metres tall, ringed every quarter metre
so the bend has somewhere to happen. Bone 0 holds the bottom, bone 1 the top,
and the weights cross over around y = 1 -- which is the whole point: a rigid
assignment would hinge and a smooth one bends, and telling those apart is how
you know the weights arrived.

Deliberately not centred on the origin and not a cube: a symmetric shape at the
origin hides axis swaps and sign errors, which are the two mistakes an importer
actually makes.
"""

import argparse
import base64
import json
import math
import struct

RINGS = 9          # cross-sections along the post, including both ends
HALF = 0.15        # half-width of the square section
HEIGHT = 2.0

# Where the weight crosses from the lower bone to the upper one, and how wide
# the blend is. A blend narrower than a ring spacing would quantise to a hinge.
BLEND_CENTRE = 1.0
BLEND_WIDTH = 0.8


def build_geometry():
    """Positions, normals, texcoords, joints and weights for the post."""
    # Four corners of the section, with their outward normals. Each corner is
    # duplicated per face so the edges stay sharp -- a shared corner vertex
    # would average two perpendicular normals and light the post like a tube.
    faces = [
        ((-HALF, -HALF), (HALF, -HALF), (0.0, -1.0)),   # -Z
        ((HALF, -HALF), (HALF, HALF), (1.0, 0.0)),      # +X
        ((HALF, HALF), (-HALF, HALF), (0.0, 1.0)),      # +Z
        ((-HALF, HALF), (-HALF, -HALF), (-1.0, 0.0)),   # -X
    ]

    positions, normals, texcoords, joints, weights = [], [], [], [], []
    indices = []

    for face_index, (start, end, normal) in enumerate(faces):
        base = len(positions)

        for ring in range(RINGS):
            y = HEIGHT * ring / (RINGS - 1)

            # The upper bone's share. Clamped, so the ends are fully owned.
            upper = (y - BLEND_CENTRE) / BLEND_WIDTH + 0.5
            upper = min(1.0, max(0.0, upper))
            # Smoothstep, so the bend is round rather than conical.
            upper = upper * upper * (3.0 - 2.0 * upper)

            for corner, (x, z) in enumerate((start, end)):
                positions.append((x, y, z))
                normals.append((normal[0], 0.0, normal[1]))
                texcoords.append((float(corner), y / HEIGHT))
                joints.append((0, 1, 0, 0))
                weights.append((1.0 - upper, upper, 0.0, 0.0))

        # Two triangles per quad between consecutive rings.
        for ring in range(RINGS - 1):
            a = base + ring * 2
            b = a + 1
            c = a + 2
            d = a + 3
            # Counter-clockwise seen from outside, matching the engine's
            # front-face convention and the winding check in scenetest.
            indices += [a, c, b, b, c, d]

    # Caps, so the post is closed and back-face culling has nothing to reveal.
    for y, normal, flip in ((0.0, (0.0, -1.0, 0.0), True), (HEIGHT, (0.0, 1.0, 0.0), False)):
        base = len(positions)
        upper = 0.0 if y == 0.0 else 1.0

        for x, z in ((-HALF, -HALF), (HALF, -HALF), (HALF, HALF), (-HALF, HALF)):
            positions.append((x, y, z))
            normals.append(normal)
            texcoords.append((0.5, 0.5))
            joints.append((0, 1, 0, 0))
            weights.append((1.0 - upper, upper, 0.0, 0.0))

        quad = [base, base + 1, base + 2, base, base + 2, base + 3]
        indices += list(reversed(quad)) if flip else quad

    return positions, normals, texcoords, joints, weights, indices


def build_animation(seconds, samples):
    """Bone 1 bending from straight to ninety degrees and back."""
    times, rotations = [], []

    for i in range(samples):
        t = seconds * i / (samples - 1)
        # A full there-and-back, so a looping clip meets itself at the ends and
        # a seam in the sampler would be visible as a jolt.
        phase = math.sin(2.0 * math.pi * i / (samples - 1))
        angle = math.radians(60.0) * phase

        half = angle * 0.5
        # About +Z, so the post leans along -X: an axis a viewer can name.
        times.append(t)
        rotations.append((0.0, 0.0, math.sin(half), math.cos(half)))

    return times, rotations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=2.0)
    parser.add_argument("--samples", type=int, default=17)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    positions, normals, texcoords, joints, weights, indices = build_geometry()
    times, rotations = build_animation(args.seconds, args.samples)

    # The joints, as a chain: bone 0 at the origin, bone 1 a metre up. The
    # inverse bind matrix is the inverse of each joint's rest transform in mesh
    # space -- here just a translation, so the inverse is the negation.
    #
    # Written out in full rather than as an identity, because an importer that
    # ignores the accessor entirely still looks right when every matrix is the
    # identity, and this is the file that has to catch that.
    inverse_binds = [
        # bone 0 at y = 0
        (1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1),
        # bone 1 at y = 1, so mesh space to bone space subtracts a metre
        (1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, -1.0, 0, 1),
    ]

    blob = bytearray()
    views = []
    accessors = []

    def add(data, fmt, count, kind, component, target=None, minmax=None):
        """Appends one accessor's worth of data and returns its index."""
        # Accessors must start on a multiple of their component size.
        while len(blob) % 4 != 0:
            blob.append(0)

        offset = len(blob)
        for item in data:
            blob.extend(struct.pack(fmt, *item) if isinstance(item, tuple)
                        else struct.pack(fmt, item))

        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(blob) - offset}
        if target is not None:
            view["target"] = target
        views.append(view)

        accessor = {
            "bufferView": len(views) - 1,
            "componentType": component,
            "count": count,
            "type": kind,
        }
        if minmax:
            accessor["min"], accessor["max"] = minmax
        accessors.append(accessor)
        return len(accessors) - 1

    FLOAT, UBYTE, USHORT = 5126, 5121, 5123
    ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963

    # POSITION requires min and max; the spec says so and some loaders rely on
    # it for bounds.
    lo = [min(p[i] for p in positions) for i in range(3)]
    hi = [max(p[i] for p in positions) for i in range(3)]

    position_accessor = add(positions, "<3f", len(positions), "VEC3", FLOAT,
                            ARRAY_BUFFER, (lo, hi))
    normal_accessor = add(normals, "<3f", len(normals), "VEC3", FLOAT, ARRAY_BUFFER)
    texcoord_accessor = add(texcoords, "<2f", len(texcoords), "VEC2", FLOAT, ARRAY_BUFFER)
    joint_accessor = add(joints, "<4B", len(joints), "VEC4", UBYTE, ARRAY_BUFFER)
    weight_accessor = add(weights, "<4f", len(weights), "VEC4", FLOAT, ARRAY_BUFFER)
    index_accessor = add(indices, "<H", len(indices), "SCALAR", USHORT, ELEMENT_ARRAY_BUFFER)

    bind_accessor = add(inverse_binds, "<16f", len(inverse_binds), "MAT4", FLOAT)
    time_accessor = add(times, "<f", len(times), "SCALAR", FLOAT,
                        None, ([min(times)], [max(times)]))
    rotation_accessor = add(rotations, "<4f", len(rotations), "VEC4", FLOAT)

    gltf = {
        "asset": {
            "version": "2.0",
            "generator": "RageV tools/scripts/make_skinned_gltf.py",
        },
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            # The skinned mesh. Deliberately not a child of the joints: a
            # skinned node's own transform is ignored for skinning, and parenting
            # it under a joint is the classic way to apply the root twice.
            {"name": "Limb", "mesh": 0, "skin": 0},
            {"name": "Bone0", "children": [2], "translation": [0.0, 0.0, 0.0]},
            {"name": "Bone1", "translation": [0.0, 1.0, 0.0]},
        ],
        "meshes": [{
            "name": "LimbMesh",
            "primitives": [{
                "attributes": {
                    "POSITION": position_accessor,
                    "NORMAL": normal_accessor,
                    "TEXCOORD_0": texcoord_accessor,
                    "JOINTS_0": joint_accessor,
                    "WEIGHTS_0": weight_accessor,
                },
                "indices": index_accessor,
                "material": 0,
            }],
        }],
        "skins": [{
            "name": "LimbSkin",
            "inverseBindMatrices": bind_accessor,
            "skeleton": 1,
            "joints": [1, 2],
        }],
        "animations": [{
            "name": "Bend",
            "samplers": [{
                "input": time_accessor,
                "output": rotation_accessor,
                "interpolation": "LINEAR",
            }],
            "channels": [{
                "sampler": 0,
                "target": {"node": 2, "path": "rotation"},
            }],
        }],
        "materials": [{
            "name": "LimbMaterial",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.82, 0.31, 0.28, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.55,
            },
        }],
        "bufferViews": views,
        "accessors": accessors,
        "buffers": [{
            "byteLength": len(blob),
            "uri": "data:application/octet-stream;base64," +
                   base64.b64encode(bytes(blob)).decode("ascii"),
        }],
    }

    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(gltf, handle, indent=1)
        handle.write("\n")

    print(f"{args.output}: {len(positions)} vertices, {len(indices) // 3} triangles, "
          f"2 bones, {len(times)} keys over {args.seconds:g}s")


if __name__ == "__main__":
    main()
