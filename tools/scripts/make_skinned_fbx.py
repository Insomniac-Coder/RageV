#!/usr/bin/env python3
"""Write limb.gltf's twin as an ASCII FBX: the same rig in the other format.

    python tools/scripts/make_skinned_fbx.py

Writes limb.fbx beside limb.gltf, into both places the repository keeps its
models -- the project's assets and the editor's. From *the same
description* -- this imports `build_geometry` and `build_animation` out of
make_skinned_gltf.py rather than restating them, so the two files cannot drift
apart. That is the whole value of it, and it is the pattern make_fbx_fixture.py
established for static geometry: a skinned model in one format looks like
*something* and there is nothing to check it against, while two formats of one
description turn "is this right" into "do these agree".

**What the twin is for is the FBX-shaped failures**, none of which the glTF
path can have:

  * A bind matrix read transposed. FBX writes a cluster's matrix as sixteen
    doubles in column-major order; read as rows it is wrong only where the
    rotation is, so a fixture with no rotation in its bind pose would pass.
  * The rest pose taken from where the bone nodes currently sit rather than
    from the bind matrices. Both give the same answer here, deliberately --
    the file is saved at bind -- so the check compares them and can say so.
  * Euler curves read as if they were a quaternion track. FBX keys rotation as
    three euler channels in the node's own rotation order, and the importer
    bakes them through ufbx rather than reading them; the bend below is 60
    degrees about one axis, which is large enough that euler-versus-slerp
    interpolation between keys is a visible difference rather than a rounding
    one.

The rig is exactly the glTF's: a square-section post two metres tall, bone 0 at
the origin holding the bottom, bone 1 a metre up holding the top, weights
crossing over smoothly around y = 1, and a two-second clip that swings bone 1
sixty degrees and back.
"""

import argparse
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from make_skinned_gltf import build_geometry, build_animation   # noqa: E402

# FBX 7.x counts time in these per second. A round number would be easier to
# read and would also be a different file format.
KTIME = 46186158000

# The bit that says "linear", plus the two an exporter always sets with it:
# 0x4 interpolation-linear, 0x2000 time-independent, 0x4000 clamp-progressive.
# Linear matters -- the default is cubic, and a cubic curve is resampled on the
# way in, so the twin would stop agreeing with the glTF between its keys.
KEY_LINEAR = 0x4 | 0x2000 | 0x4000


def numbers(values, per_line=6, indent=12):
    out, line = [], []
    for value in values:
        line.append('{0:.9g}'.format(value))
        if len(line) == per_line:
            out.append(','.join(line))
            line = []
    if line:
        out.append(','.join(line))
    return (',\n' + ' ' * indent).join(out)


def translation_matrix(x, y, z):
    """Column-major, which is the order FBX writes a cluster's matrices in:
    elements 12, 13 and 14 are the translation."""
    return [1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            x, y, z, 1]


def cluster_block(ident, name, bone_index, joints, weights, bone_y):
    """One SubDeformer: which vertices this bone moves, and by how much.

    `Transform` is the mesh node's transform at bind time expressed in the
    bone's space, and `TransformLink` is the bone's own transform at bind time.
    The mesh node sits at the origin here, so the first is just the inverse of
    the second -- which is the same quantity glTF calls the inverse bind
    matrix, and the file states both because a real exporter does.
    """
    indexes, values = [], []
    for vertex, (joint, weight) in enumerate(zip(joints, weights)):
        share = sum(w for j, w in zip(joint, weight) if j == bone_index)
        if share > 0.0:
            indexes.append(vertex)
            values.append(share)

    return '''    Deformer: {ident}, "SubDeformer::{name}", "Cluster" {{
        Version: 100
        UserData: "", ""
        Indexes: *{count} {{
            a: {indexes}
        }}
        Weights: *{count} {{
            a: {weights}
        }}
        Transform: *16 {{
            a: {transform}
        }}
        TransformLink: *16 {{
            a: {link}
        }}
    }}
'''.format(ident=ident, name=name, count=len(indexes),
           indexes=numbers(indexes, 12),
           weights=numbers(values, 8),
           transform=numbers(translation_matrix(0.0, -bone_y, 0.0), 8),
           link=numbers(translation_matrix(0.0, bone_y, 0.0), 8))


def curve_block(ident, times, values):
    """One AnimationCurve: a channel of a node's Lcl Rotation, in degrees."""
    return '''    AnimationCurve: {ident}, "AnimCurve::", "" {{
        Default: {default:.9g}
        KeyVer: 4008
        KeyTime: *{count} {{
            a: {times}
        }}
        KeyValueFloat: *{count} {{
            a: {values}
        }}
        KeyAttrFlags: *1 {{
            a: {flags}
        }}
        KeyAttrDataFloat: *4 {{
            a: 0,0,0,0
        }}
        KeyAttrRefCount: *1 {{
            a: {count}
        }}
    }}
'''.format(ident=ident, default=values[0], count=len(times),
           times=numbers([int(round(t * KTIME)) for t in times], 4),
           values=numbers(values, 8), flags=KEY_LINEAR)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=2.0)
    parser.add_argument("--samples", type=int, default=17)
    # Both copies by default. limb.gltf lives in two places already -- the
    # project's assets and the editor's, which is what the tools stage beside
    # themselves -- and a generator that writes only one of them is how the two
    # come to disagree.
    parser.add_argument("--output", action="append", default=None,
                        help="where to write; repeatable. Defaults to both "
                             "copies the repository keeps.")
    args = parser.parse_args()

    outputs = args.output or [
        ROOT / "SampleProject" / "assets" / "models" / "limb.fbx",
        ROOT / "RageVEditor" / "assets" / "models" / "limb.fbx",
    ]

    positions, normals, texcoords, joints, weights, indices = build_geometry()
    times, rotations = build_animation(args.seconds, args.samples)

    # The glTF stores the bend as quaternions about +Z; FBX keys euler degrees
    # in the node's rotation order, which is XYZ here. A single-axis rotation is
    # the one case where the two are the same number, which is why the fixture
    # bends about one axis: any disagreement between the twins is then the
    # importer's and not the format's.
    degrees = []
    for x, y, z, w in rotations:
        degrees.append(math.degrees(2.0 * math.atan2(z, w)))

    vertices = []
    for point in positions:
        vertices.extend(point)

    # FBX marks the last corner of a face by storing its index complemented,
    # which is how the format ends a polygon without a separate count.
    polygons = []
    for i in range(0, len(indices), 3):
        polygons.extend(indices[i:i + 2])
        polygons.append(~indices[i + 2])

    corner_normals = []
    uvs = []
    for index in indices:
        corner_normals.extend(normals[index])
        u, v = texcoords[index]
        # FBX puts the UV origin at the bottom left and the engine's sampler
        # puts it at the top, so the importer flips V on the way in. Writing
        # the flipped value here is what makes the two formats' UVs agree
        # *after* import rather than before it.
        uvs.extend([u, 1.0 - v])

    body = '''; FBX 7.4.0 project file
; Generated by tools/scripts/make_skinned_fbx.py -- do not edit by hand.
; The twin of limb.gltf, from the same description. See the script.

FBXHeaderExtension:  {{
    FBXHeaderVersion: 1003
    FBXVersion: 7400
    Creator: "RageV skinned fixture generator"
}}
GlobalSettings:  {{
    Version: 1000
    Properties70:  {{
        P: "UpAxis", "int", "Integer", "",1
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",2
        P: "FrontAxisSign", "int", "Integer", "",1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        ; Centimetres per unit, so 100 is one unit per metre and the numbers
        ; below are metres -- the same trick fbxwrite.py uses.
        P: "UnitScaleFactor", "double", "Number", "",100
        P: "TimeSpanStart", "KTime", "Time", "",0
        P: "TimeSpanStop", "KTime", "Time", "",{stop}
    }}
}}

Definitions:  {{
    Version: 100
    Count: 11
    ObjectType: "Geometry" {{ Count: 1 }}
    ObjectType: "Model" {{ Count: 3 }}
    ObjectType: "Material" {{ Count: 1 }}
    ObjectType: "Deformer" {{ Count: 3 }}
    ObjectType: "AnimationStack" {{ Count: 1 }}
    ObjectType: "AnimationLayer" {{ Count: 1 }}
    ObjectType: "AnimationCurveNode" {{ Count: 1 }}
    ObjectType: "AnimationCurve" {{ Count: 3 }}
}}

Objects:  {{
    Geometry: 1100, "Geometry::LimbMesh", "Mesh" {{
        Vertices: *{vertex_count} {{
            a: {vertices}
        }}
        PolygonVertexIndex: *{polygon_count} {{
            a: {polygons}
        }}
        GeometryVersion: 124
        LayerElementNormal: 0 {{
            Version: 101
            Name: ""
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            Normals: *{normal_count} {{
                a: {normals}
            }}
        }}
        LayerElementUV: 0 {{
            Version: 101
            Name: "UVMap"
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            UV: *{uv_count} {{
                a: {uvs}
            }}
        }}
        LayerElementMaterial: 0 {{
            Version: 101
            Name: ""
            MappingInformationType: "AllSame"
            ReferenceInformationType: "IndexToDirect"
            Materials: *1 {{
                a: 0
            }}
        }}
        Layer: 0 {{
            Version: 100
            LayerElement:  {{
                Type: "LayerElementNormal"
                TypedIndex: 0
            }}
            LayerElement:  {{
                Type: "LayerElementUV"
                TypedIndex: 0
            }}
            LayerElement:  {{
                Type: "LayerElementMaterial"
                TypedIndex: 0
            }}
        }}
    }}
    Model: 2100, "Model::Limb", "Mesh" {{
        Version: 232
        Properties70:  {{
            P: "Lcl Translation", "Lcl Translation", "", "A",0,0,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }}
        Shading: T
        Culling: "CullingOff"
    }}
    Model: 2200, "Model::Bone0", "LimbNode" {{
        Version: 232
        Properties70:  {{
            P: "Lcl Translation", "Lcl Translation", "", "A",0,0,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }}
        Shading: T
        Culling: "CullingOff"
    }}
    Model: 2300, "Model::Bone1", "LimbNode" {{
        Version: 232
        Properties70:  {{
            ; A metre above bone 0, which is the number the whole rig is
            ; measured against.
            P: "Lcl Translation", "Lcl Translation", "", "A",0,1,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }}
        Shading: T
        Culling: "CullingOff"
    }}
    Material: 3100, "Material::LimbMaterial", "" {{
        Version: 102
        ShadingModel: "phong"
        MultiLayer: 0
        Properties70:  {{
            P: "DiffuseColor", "Color", "", "A",0.82,0.31,0.28
            P: "SpecularColor", "Color", "", "A",0.05,0.05,0.05
            P: "ShininessExponent", "Number", "", "A",8
        }}
    }}
    Deformer: 4100, "Deformer::LimbSkin", "Skin" {{
        Version: 101
        Link_DeformAcuracy: 50
        SkinningType: "Linear"
    }}
{clusters}    AnimationStack: 5100, "AnimStack::Bend", "" {{
        Properties70:  {{
            P: "LocalStart", "KTime", "Time", "",0
            P: "LocalStop", "KTime", "Time", "",{stop}
            P: "ReferenceStart", "KTime", "Time", "",0
            P: "ReferenceStop", "KTime", "Time", "",{stop}
        }}
    }}
    AnimationLayer: 5200, "AnimLayer::Base Layer", "" {{
    }}
    AnimationCurveNode: 5300, "AnimCurveNode::R", "" {{
        Properties70:  {{
            P: "d|X", "Number", "", "A",0
            P: "d|Y", "Number", "", "A",0
            P: "d|Z", "Number", "", "A",0
        }}
    }}
{curves}}}

Connections:  {{
    ; The mesh and its two bones hang off the scene root.
    C: "OO",2100,0
    C: "OO",2200,0
    C: "OO",2300,2200
    C: "OO",1100,2100
    C: "OO",3100,2100

    ; The skin deforms the *geometry*, and each cluster belongs to the skin
    ; and points at one bone.
    C: "OO",4100,1100
    C: "OO",4200,4100
    C: "OO",4300,4100
    C: "OO",2200,4200
    C: "OO",2300,4300

    ; The clip: a stack holds a layer, the layer holds the curve node, and the
    ; curve node drives one property of one bone.
    C: "OO",5200,5100
    C: "OO",5300,5200
    C: "OP",5300,2300, "Lcl Rotation"
    C: "OP",5400,5300, "d|X"
    C: "OP",5500,5300, "d|Y"
    C: "OP",5600,5300, "d|Z"
}}
'''.format(
        stop=int(round(args.seconds * KTIME)),
        vertex_count=len(vertices), vertices=numbers(vertices),
        polygon_count=len(polygons), polygons=numbers(polygons, 12),
        normal_count=len(corner_normals), normals=numbers(corner_normals),
        uv_count=len(uvs), uvs=numbers(uvs, 8),
        clusters=(cluster_block(4200, "Bone0", 0, joints, weights, 0.0) +
                  cluster_block(4300, "Bone1", 1, joints, weights, 1.0)),
        curves=(curve_block(5400, [0.0], [0.0]) +
                curve_block(5500, [0.0], [0.0]) +
                curve_block(5600, times, degrees)))

    for target in outputs:
        target = pathlib.Path(target)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(body, encoding="utf-8", newline="\n")
        print(f"{target}: {len(positions)} vertices, {len(indices) // 3} triangles, "
              f"2 bones, {len(times)} keys over {args.seconds:g}s")


if __name__ == "__main__":
    main()
