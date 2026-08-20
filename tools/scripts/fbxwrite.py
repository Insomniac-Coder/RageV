#!/usr/bin/env python3
"""A minimal ASCII FBX writer, shared by the model generators.

Three scripts needed the same forty lines of FBX boilerplate -- the import
fixture, the demo's anvil, and the camp's props -- and the third was where
copying it a third time stopped being acceptable.

**ASCII FBX 7.4 rather than binary.** It is the format's own text form, ufbx
reads it, and a generated asset somebody can open and read is worth more here
than a smaller file. `PolygonVertexIndex` uses FBX's convention where the last
index of a face is stored as its bitwise complement, which is how the format
marks a face boundary without a separate count.

**Flat shading by construction.** Every face gets its own normal, computed from
its own winding, and no vertex is shared between faces. That is what gives a
low-poly model the faceted look it is for -- smoothing a twelve-triangle tree
just makes it a blurry twelve-triangle tree.
"""

import math
import pathlib


class Mesh:
    """Points and faces, accumulated. Faces are triangles or quads."""

    def __init__(self):
        self.points = []
        self.faces = []

        # Where each piece's faces begin and end. A prop is a union of solids
        # rather than one hull, and the only way to ask "is this wound
        # outward" of a union is to ask it of each solid -- which is what
        # check_models does, and what lets it demand every face rather than a
        # fraction.
        self.pieces = []

    def add(self, points, faces, offset=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0),
            yaw=0.0):
        """Append a piece, placed. Returns the index the piece started at."""
        base = len(self.points)
        cos, sin = math.cos(yaw), math.sin(yaw)

        for x, y, z in points:
            x, y, z = x * scale[0], y * scale[1], z * scale[2]
            self.points.append((x * cos + z * sin + offset[0],
                                y + offset[1],
                                -x * sin + z * cos + offset[2]))

        first = len(self.faces)
        for face in faces:
            self.faces.append(tuple(base + i for i in face))
        self.pieces.append((first, len(self.faces)))
        return base

    # --- primitives -----------------------------------------------------------

    def box(self, low, high, **place):
        x0, y0, z0 = low
        x1, y1, z1 = high
        points = [
            (x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
            (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1),
        ]
        faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
                 (3, 7, 6, 2), (0, 4, 7, 3), (1, 2, 6, 5)]
        return self.add(points, faces, **place)

    def prism(self, low, high, near, far, **place):
        """A box whose far end is a different rectangle: a taper, a wedge, a
        tent. `near` and `far` are (half width, half depth) at each end."""
        (nx, nz), (fx, fz) = near, far
        y0, y1 = low, high
        points = [
            (-nx, y0, -nz), (nx, y0, -nz), (nx, y0, nz), (-nx, y0, nz),
            (-fx, y1, -fz), (fx, y1, -fz), (fx, y1, fz), (-fx, y1, fz),
        ]
        faces = [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1),
                 (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
        return self.add(points, faces, **place)

    def cone(self, radius, height, sides=8, base_y=0.0, flip=False, **place):
        """`flip` points it downward *and reverses the winding with it*.

        A negative `height` looks like it should do this and does not: it moves
        the apex below the base and leaves every face wound for a cone that
        points up, so the piece comes out inside out. check_models found
        exactly that in the rock, which was two cones with one of them negative.
        """
        points = [(0.0, base_y + height, 0.0)]
        for i in range(sides):
            angle = 2.0 * math.pi * i / sides
            points.append((math.cos(angle) * radius, base_y,
                           math.sin(angle) * radius))

        # **Counter-clockwise seen from outside.** Every normal in this file
        # is derived from the winding, so a face wound the other way is not
        # merely invisible under backface culling -- it is lit from behind.
        faces = []
        for i in range(sides):
            faces.append((0, 1 + (i + 1) % sides, 1 + i))
        # The underside, as a fan. Cheap, and never seen on a tree.
        for i in range(1, sides - 1):
            faces.append((1, 1 + i, 1 + i + 1))

        if flip:
            points = [(x, 2.0 * base_y - y, z) for x, y, z in points]
            faces = [tuple(reversed(face)) for face in faces]

        return self.add(points, faces, **place)

    def cylinder(self, radius, height, sides=8, taper=1.0, base_y=0.0, **place):
        points = []
        for i in range(sides):
            angle = 2.0 * math.pi * i / sides
            points.append((math.cos(angle) * radius, base_y,
                           math.sin(angle) * radius))
        for i in range(sides):
            angle = 2.0 * math.pi * i / sides
            points.append((math.cos(angle) * radius * taper, base_y + height,
                           math.sin(angle) * radius * taper))

        faces = []
        for i in range(sides):
            j = (i + 1) % sides
            faces.append((i, sides + i, sides + j, j))
        for i in range(1, sides - 1):
            # The caps were already right; only the sides were inverted, and
            # flipping these too was an over-correction the same three-line
            # check caught.
            faces.append((0, i, i + 1))                       # bottom, -Y
            faces.append((sides, sides + i + 1, sides + i))   # top, +Y
        return self.add(points, faces, **place)


    def strut(self, a, b, radius, sides=4, taper=1.0, **place):
        """A round bar from point `a` to point `b`.

        `cylinder` can only stand up. A camp chair is four bars that each run
        from a corner of the floor to the opposite corner of the seat, a
        tripod is three that meet in the air, and a guy line goes from a
        ridge to a peg -- none of which is expressible as "a cylinder, rotated"
        without doing this arithmetic at the call site every time.

        The cross-section is placed in a frame built around the axis, and the
        faces are wound exactly as `cylinder`'s are, because they are the same
        faces seen from a different basis. The one subtlety is the handedness:
        `cylinder` lays its ring out as (cos, 0, sin) about +Y, and X cross Z
        is *minus* Y -- so the second basis vector here is `s x u`, not
        `u x s`. Getting that backwards turns every bar inside out, and
        check_models is what says so.
        """
        axis = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        length = math.sqrt(sum(component * component for component in axis))
        if length < 1e-9:
            return len(self.points)

        u = tuple(component / length for component in axis)

        # Any vector not parallel to the axis will do to start from.
        seed = (0.0, 1.0, 0.0) if abs(u[1]) < 0.9 else (1.0, 0.0, 0.0)
        s = (seed[1] * u[2] - seed[2] * u[1],
             seed[2] * u[0] - seed[0] * u[2],
             seed[0] * u[1] - seed[1] * u[0])
        scale = math.sqrt(sum(component * component for component in s))
        s = tuple(component / scale for component in s)

        t = (s[1] * u[2] - s[2] * u[1],
             s[2] * u[0] - s[0] * u[2],
             s[0] * u[1] - s[1] * u[0])

        points = []
        for end, ring in ((a, radius), (b, radius * taper)):
            for i in range(sides):
                angle = 2.0 * math.pi * i / sides
                cos, sin = math.cos(angle) * ring, math.sin(angle) * ring
                points.append((end[0] + s[0] * cos + t[0] * sin,
                               end[1] + s[1] * cos + t[1] * sin,
                               end[2] + s[2] * cos + t[2] * sin))

        faces = []
        for i in range(sides):
            j = (i + 1) % sides
            faces.append((i, sides + i, sides + j, j))
        for i in range(1, sides - 1):
            faces.append((0, i, i + 1))                       # the `a` cap
            faces.append((sides, sides + i + 1, sides + i))    # the `b` cap

        return self.add(points, faces, **place)

    def panel(self, corners, thickness, **place):
        """A flat polygon given by its corners, thickened along its own normal.

        A tent's canvas, a gable, a chair's seat and a mirror's pane are all
        this: a shape defined by *where its corners are* rather than by a size
        and a rotation. Expressing one as a box plus two euler angles means
        solving for the angles, and the angles are not the thing anybody
        knows -- the corners are.

        Any number of corners from three up, because a tent has as many
        triangles in it as quads. The corners must be given counter-clockwise
        seen from the front, which is what decides which way the panel faces.
        """
        count = len(corners)
        a, b, c = corners[0], corners[1], corners[2]
        u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        n = (u[1] * v[2] - u[2] * v[1],
             u[2] * v[0] - u[0] * v[2],
             u[0] * v[1] - u[1] * v[0])
        length = max(math.sqrt(sum(component * component for component in n)), 1e-9)
        n = tuple(component / length * (thickness * 0.5) for component in n)

        points = [(p[0] - n[0], p[1] - n[1], p[2] - n[2]) for p in corners]
        points += [(p[0] + n[0], p[1] + n[1], p[2] + n[2]) for p in corners]

        faces = [tuple(reversed(range(count))),
                 tuple(range(count, count * 2))]
        for i in range(count):
            j = (i + 1) % count
            faces.append((i, j, count + j, count + i))

        return self.add(points, faces, **place)


def face_normal(points, face):
    a, b, c = points[face[0]], points[face[1]], points[face[2]]
    u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    n = (u[1] * v[2] - u[2] * v[1],
         u[2] * v[0] - u[0] * v[2],
         u[0] * v[1] - u[1] * v[0])
    length = max((n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5, 1e-9)
    return (n[0] / length, n[1] / length, n[2] / length)


def _numbers(values, per_line=6):
    out, line = [], []
    for value in values:
        line.append('{0:g}'.format(value))
        if len(line) == per_line:
            out.append(','.join(line))
            line = []
    if line:
        out.append(','.join(line))
    return (',\n' + ' ' * 12).join(out)


def write(path, mesh, name, colour=(0.7, 0.7, 0.7), uv_scale=1.0):
    """One mesh, one Phong material, one node."""
    points, faces = mesh.points, mesh.faces

    vertices = []
    for point in points:
        vertices.extend(point)

    indices = []
    for face in faces:
        indices.extend(face[:-1])
        indices.append(~face[-1])

    normals = []
    for face in faces:
        normal = face_normal(points, face)
        for _ in range(len(face)):
            normals.extend(normal)

    # Planar UVs off the world position. These props wear flat colours and a
    # tiling detail map at most, so a real unwrap would be work nothing reads.
    uvs, uv_indices = [], []
    for face in faces:
        for corner in face:
            x, y, z = points[corner]
            uv_indices.append(len(uvs) // 2)
            uvs.extend([(x + z) * uv_scale, y * uv_scale])

    text = '''; FBX 7.4.0 project file
; Generated by tools/scripts/{generator} -- do not edit by hand.

FBXHeaderExtension:  {{
    FBXHeaderVersion: 1003
    FBXVersion: 7400
    Creator: "RageV model generator"
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
        ; Centimetres per unit, so 100 is one unit per metre.
        P: "UnitScaleFactor", "double", "Number", "",100
    }}
}}

Definitions:  {{
    Version: 100
    Count: 3
    ObjectType: "Geometry" {{
        Count: 1
    }}
    ObjectType: "Model" {{
        Count: 1
    }}
    ObjectType: "Material" {{
        Count: 1
    }}
}}

Objects:  {{
    Geometry: 1100, "Geometry::{name}", "Mesh" {{
        Vertices: *{vertex_count} {{
            a: {vertices}
        }}
        PolygonVertexIndex: *{index_count} {{
            a: {indices}
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
            ReferenceInformationType: "IndexToDirect"
            UV: *{uv_count} {{
                a: {uvs}
            }}
            UVIndex: *{uv_index_count} {{
                a: {uv_indices}
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
    Model: 2100, "Model::{name}", "Mesh" {{
        Version: 232
        Properties70:  {{
            P: "Lcl Translation", "Lcl Translation", "", "A",0,0,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }}
        Shading: T
        Culling: "CullingOff"
    }}
    Material: 3100, "Material::{name}Surface", "" {{
        Version: 102
        ShadingModel: "phong"
        MultiLayer: 0
        Properties70:  {{
            P: "DiffuseColor", "Color", "", "A",{r:g},{g:g},{b:g}
            P: "SpecularColor", "Color", "", "A",0.05,0.05,0.05
            P: "ShininessExponent", "Number", "", "A",8
        }}
    }}
}}

Connections:  {{
    C: "OO",2100,0
    C: "OO",1100,2100
    C: "OO",3100,2100
}}
'''.format(
        generator=pathlib.Path(path).stem and 'make_camp_models.py',
        name=name,
        vertex_count=len(vertices), vertices=_numbers(vertices),
        index_count=len(indices), indices=_numbers(indices, 8),
        normal_count=len(normals), normals=_numbers(normals),
        uv_count=len(uvs), uvs=_numbers(uvs, 8),
        uv_index_count=len(uv_indices), uv_indices=_numbers(uv_indices, 8),
        r=colour[0], g=colour[1], b=colour[2])

    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')
    return len(points), len(faces), len(text)
