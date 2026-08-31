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

        # --- the three parallel-to-`faces` lists (7.7) ------------------------
        #
        # **Parallel arrays rather than a Face object, and that is a choice.**
        # A face is a tuple of point indices everywhere in this file and in
        # every generator that reads `mesh.faces`; wrapping it would rewrite
        # all of them to gain nothing. These three say what the face wears.
        #
        # `uv`: per-corner (u, v) for that face, or None to fall back to the
        # planar projection `write` has always used. None is the default and
        # is what keeps every model authored before this byte-identical.
        self.uv = []

        # `smooth`: whether this face's corners average their normal with the
        # neighbours they share a point with. Off by default -- see the module
        # docstring on why flat is the right default for a low-poly prop and
        # the wrong one for a cable.
        self.smooth = []

        # `material`: which entry of `write`'s material list shades it. Zero
        # everywhere is the single-material file this wrote before.
        self.material = []

    def add(self, points, faces, offset=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0),
            yaw=0.0, uv=None, smooth=False, material=0):
        """Append a piece, placed. Returns the index the piece started at.

        `uv` is a list parallel to `faces`, each entry a tuple of (u, v) per
        corner of that face -- **per corner and not per point**, because a
        cylinder's seam needs u=0 and u=1 at the same position in space, and a
        cube's corner needs a different (u, v) on each of the three faces
        meeting there. Storing them per point cannot express either.
        """
        base = len(self.points)
        cos, sin = math.cos(yaw), math.sin(yaw)

        for x, y, z in points:
            x, y, z = x * scale[0], y * scale[1], z * scale[2]
            self.points.append((x * cos + z * sin + offset[0],
                                y + offset[1],
                                -x * sin + z * cos + offset[2]))

        first = len(self.faces)
        for index, face in enumerate(faces):
            self.faces.append(tuple(base + i for i in face))
            self.uv.append(tuple(uv[index]) if uv is not None else None)
            self.smooth.append(smooth)
            self.material.append(material)
        self.pieces.append((first, len(self.faces)))
        return base

    # --- primitives -----------------------------------------------------------

    def box(self, low, high, uv=None, **place):
        x0, y0, z0 = low
        x1, y1, z1 = high
        points = [
            (x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
            (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1),
        ]
        faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
                 (3, 7, 6, 2), (0, 4, 7, 3), (1, 2, 6, 5)]
        return self.add(points, faces, uv=box_uv(points, faces, uv), **place)

    def chamfered_box(self, low, high, chamfer, uv=None, **place):
        """A box with all twelve arrises cut back by `chamfer`.

        **Nothing this size has a sharp arris.** Concrete is cast against
        formwork with a chamfer strip nailed into every internal corner -- 20
        to 25 mm is the standard section -- because an unprotected arrise
        spalls off the moment the forms are struck. Seventy years of salt air
        then rounds what the strip left. So a sharp edge on a pier is not a
        simplification, it is a shape the object has never had.

        And it reads, which is the point: a sharp edge gives the eye one line
        where two faces meet, and those two faces are lit as a pair at every
        sun angle. A chamfer inserts a *third* face on the bisector, which is
        lit as neither -- so the arrise becomes a band of real width that goes
        bright against a dark face or dark against a bright one, and turns
        over as the sun moves. That band is most of what separates cast
        concrete from a shaded polygon.

        26 faces against `box`'s 6, so this is for pieces that are large and
        near -- piers, pylons, curbs, the fort -- and emphatically not for a
        railing picket, of which there are 3 648.

        **Flat by default gives a cast chamfer; `smooth=True` gives a
        weathered one.** The chamfer face sits 45 degrees off each neighbour,
        which is inside `smooth_normals`' 60-degree crease, so marking the
        piece smooth blends all three into a rounded fillet instead. Both are
        real finishes and the call site chooses.

        **The winding is decided, not written down.** Twelve edge quads and
        eight corner triangles is exactly the sort of hand-authored table that
        ships inside out -- which is the defect `check_models` exists for, and
        which cost this toolkit its trees once already. The solid is convex
        and contains its own centre, so a face is outward exactly when its
        normal agrees with the direction from that centre to the face, and
        that is tested here per face rather than trusted.
        """
        x0, y0, z0 = low
        x1, y1, z1 = high
        centre = ((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5)

        # A third of the shortest side, never more. At half a side the
        # chamfers off opposite arrises meet, the face between them vanishes,
        # and past that the solid turns through itself -- silently, because
        # every face is still wound the way this builds it.
        c = min(chamfer, (x1 - x0) / 3.0, (y1 - y0) / 3.0, (z1 - z0) / 3.0)
        if c <= 1e-6:
            return self.box(low, high, uv=uv, **place)

        # Three vertices per corner, one per face meeting there: it keeps the
        # box's full extent along its own axis and is pulled in by `c` along
        # the other two. Twenty-four in all, and the corner's three are the
        # corner triangle.
        points = []
        index = {}
        for sx in (-1, 1):
            for sy in (-1, 1):
                for sz in (-1, 1):
                    corner = [x1 if sx > 0 else x0,
                              y1 if sy > 0 else y0,
                              z1 if sz > 0 else z0]
                    for axis in range(3):
                        p = list(corner)
                        for other in range(3):
                            if other != axis:
                                p[other] -= c * (sx, sy, sz)[other]
                        index[(sx, sy, sz, axis)] = len(points)
                        points.append(tuple(p))

        def vertex(signs, axis):
            return index[(signs[0], signs[1], signs[2], axis)]

        faces = []

        # The six shrunken faces.
        for axis in range(3):
            u_axis, v_axis = [(1, 2), (0, 2), (0, 1)][axis]
            for s in (-1, 1):
                quad = []
                for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                    signs = [0, 0, 0]
                    signs[axis], signs[u_axis], signs[v_axis] = s, su, sv
                    quad.append(vertex(signs, axis))
                faces.append(tuple(quad))

        # The twelve edge bands. An arrise is shared by two faces; the band
        # bridges their two strips along the third axis.
        for a in range(3):
            for b in range(a + 1, 3):
                along = 3 - a - b
                for sa in (-1, 1):
                    for sb in (-1, 1):
                        quad = []
                        for face_axis, order in ((a, (-1, 1)), (b, (1, -1))):
                            for s in order:
                                signs = [0, 0, 0]
                                signs[a], signs[b], signs[along] = sa, sb, s
                                quad.append(vertex(signs, face_axis))
                        faces.append(tuple(quad))

        # The eight corner triangles.
        for sx in (-1, 1):
            for sy in (-1, 1):
                for sz in (-1, 1):
                    faces.append(tuple(vertex((sx, sy, sz), axis)
                                       for axis in range(3)))

        outward = []
        for face in faces:
            normal = face_normal(points, face)
            mid = [sum(points[i][k] for i in face) / len(face)
                   for k in range(3)]
            away = [mid[k] - centre[k] for k in range(3)]
            if sum(normal[k] * away[k] for k in range(3)) < 0.0:
                face = tuple(reversed(face))
            outward.append(face)

        return self.add(points, outward, uv=box_uv(points, outward, uv),
                        **place)

    def prism(self, low, high, near, far, uv=None, **place):
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
        return self.add(points, faces, uv=box_uv(points, faces, uv), **place)

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


    def strut(self, a, b, radius, sides=4, taper=1.0, uv=None, **place):
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

        # **u runs around, v runs along, both in metres.** This is the mapping
        # the main cable's wrapping needs -- fine bands across the bar and no
        # stretch along it -- and it is the one the world-space fallback cannot
        # express at any scale.
        #
        # The seam takes `i + 1`, never `j`. `j` wraps to zero on the last
        # face, which sends u from nearly-one back to zero across a single
        # quad and mirrors the whole texture into it. That backwards ribbon is
        # the classic cylinder-unwrap bug and it is one modulo away at all
        # times.
        texture = None
        if uv is not None:
            circumference = 2.0 * math.pi * radius
            span = length / uv
            texture = []
            for i in range(sides):
                u0 = circumference * i / sides / uv
                u1 = circumference * (i + 1) / sides / uv
                texture.append(((u0, 0.0), (u0, span), (u1, span), (u1, 0.0)))
            for i in range(1, sides - 1):
                # The caps, radially: they are small, rarely seen on a bar, and
                # a disc has no natural u.
                ring = [(math.cos(2.0 * math.pi * k / sides) * radius / uv,
                         math.sin(2.0 * math.pi * k / sides) * radius / uv)
                        for k in range(sides)]
                texture.append((ring[0], ring[i], ring[i + 1]))
                texture.append((ring[0], ring[i + 1], ring[i]))

        return self.add(points, faces, uv=texture, **place)

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


def box_uv(points, faces, scale):
    """Planar (u, v) per corner, taken in the plane each face actually lies in.

    **Metres per UV unit, not a multiplier.** `uv=2.0` means the texture
    repeats every two metres, on every face of every prop, whatever size the
    piece is -- which is what texel density means and what a multiplier cannot
    give you. Passing None returns None, and the caller falls back to the old
    world-space projection.

    The two axes are chosen by dropping the one the face's normal is most
    aligned with, so a wall is mapped in x/y and a floor in x/z rather than
    both being squashed into the same pair. This is the same choice a
    triplanar shader makes per fragment, made once here per face.
    """
    if scale is None:
        return None

    out = []
    for face in faces:
        normal = face_normal(points, face)
        axis = max(range(3), key=lambda i: abs(normal[i]))
        # **v is up wherever there is an up.** The x-facing case used to be
        # (1, 2), which makes u = world y: a texture with any direction in it
        # -- board lines, pour joints, dirt runs -- came out rotated a quarter
        # turn on the two faces normal to x, so a pylon had horizontal form
        # lines on one pair of sides and vertical on the other. (2, 1) puts
        # world y in v on both wall cases, and leaves the floor case alone
        # because a floor has no up to get wrong.
        u_axis, v_axis = (2, 1) if axis == 0 else (0, 2) if axis == 1 else (0, 1)
        out.append(tuple((points[c][u_axis] / scale, points[c][v_axis] / scale)
                         for c in face))
    return out


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


def smooth_normals(mesh, crease=60.0):
    """A normal per face corner, averaged across the faces that agree.

    **The crease angle is what makes one rule serve a cylinder and its caps.**
    A face marked smooth averages its normal with every face that shares that
    corner's point, is also smooth, and points within `crease` of it. The side
    faces of a cylinder are a few degrees apart and average into a round
    surface; the cap is ninety degrees away and is excluded, so the rim stays a
    hard edge. Marking the piece and setting one angle replaces per-face
    smoothing groups, which is what a modelling tool would make you author by
    hand.

    Adjacency is per *point index*, and `Mesh.add` gives every piece its own
    points, so a smooth cylinder standing on a smooth box never bleeds into it.
    Two pieces that should share a surface must be one piece.
    """
    points, faces = mesh.points, mesh.faces
    normals = [face_normal(points, face) for face in faces]
    limit = math.cos(math.radians(crease))

    # Point index -> the faces using it. Built once; a prop has thousands of
    # corners and the naive search is quadratic in the piece.
    sharing = {}
    for index, face in enumerate(faces):
        for corner in face:
            sharing.setdefault(corner, []).append(index)

    out = []
    for index, face in enumerate(faces):
        own = normals[index]
        for corner in face:
            if not mesh.smooth[index]:
                out.append(own)
                continue

            x = y = z = 0.0
            for other in sharing[corner]:
                if not mesh.smooth[other]:
                    continue
                n = normals[other]
                if own[0] * n[0] + own[1] * n[1] + own[2] * n[2] < limit:
                    continue
                x, y, z = x + n[0], y + n[1], z + n[2]

            length = math.sqrt(x * x + y * y + z * z)
            # Every contributor cancelling is possible in principle and would
            # leave a zero vector, which shades as black. The face's own normal
            # is the honest fallback.
            out.append((x / length, y / length, z / length)
                       if length > 1e-9 else own)
    return out


def write(path, mesh, name, colour=(0.7, 0.7, 0.7), uv_scale=1.0,
          crease=60.0, materials=None):
    """One mesh, one node, and one Phong material unless `materials` says more.

    `materials` is a list of (name, (r, g, b)); a face's `material` index picks
    from it. Omitted, the file is exactly the single-material one this wrote
    before -- `colour` names it and every face wears it.
    """
    points, faces = mesh.points, mesh.faces

    vertices = []
    for point in points:
        vertices.extend(point)

    indices = []
    for face in faces:
        indices.extend(face[:-1])
        indices.append(~face[-1])

    normals = []
    for normal in smooth_normals(mesh, crease):
        normals.extend(normal)

    # A piece's own (u, v) where it gave one, and the old planar projection off
    # the world position where it did not. The fallback is what keeps every
    # prop authored before parametric UVs existed byte-identical: those wear
    # flat colours and a tiling detail map at most, so a real unwrap would be
    # work nothing reads.
    uvs, uv_indices = [], []
    for index, face in enumerate(faces):
        supplied = mesh.uv[index]
        for corner_index, corner in enumerate(face):
            uv_indices.append(len(uvs) // 2)
            if supplied is not None:
                uvs.extend(supplied[corner_index])
            else:
                x, y, z = points[corner]
                uvs.extend([(x + z) * uv_scale, y * uv_scale])

    # --- the materials, and the layer that says which face wears which -------
    #
    # **AllSame stays AllSame for a one-material file.** ufbx reads ByPolygon
    # with a single entry perfectly well, but the mapping type is part of the
    # bytes, and every model authored before this must come out unchanged.
    table = list(materials) if materials else [(name + 'Surface', colour)]

    if len(table) > 1:
        material_layer = (
            'MappingInformationType: "ByPolygon"\n'
            '            ReferenceInformationType: "IndexToDirect"\n'
            '            Materials: *{0} {{\n'
            '                a: {1}\n'
            '            }}').format(
                len(faces), _numbers(mesh.material, 16))
    else:
        material_layer = (
            'MappingInformationType: "AllSame"\n'
            '            ReferenceInformationType: "IndexToDirect"\n'
            '            Materials: *1 {\n'
            '                a: 0\n'
            '            }')

    material_objects = '\n'.join(
        '''    Material: {id}, "Material::{label}", "" {{
        Version: 102
        ShadingModel: "phong"
        MultiLayer: 0
        Properties70:  {{
            P: "DiffuseColor", "Color", "", "A",{r:g},{g:g},{b:g}
            P: "SpecularColor", "Color", "", "A",0.05,0.05,0.05
            P: "ShininessExponent", "Number", "", "A",8
        }}
    }}'''.format(id=3100 + index, label=label,
                 r=tint[0], g=tint[1], b=tint[2])
        for index, (label, tint) in enumerate(table))

    # **Order is the material index.** FBX has no per-material id in the layer
    # -- the integers above index the materials *in the order they are
    # connected to the model*, so these connections are the table.
    material_connections = '\n'.join(
        '    C: "OO",{0},2100'.format(3100 + index)
        for index in range(len(table)))

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
        Count: {material_count}
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
            {material_layer}
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
{material_objects}
}}

Connections:  {{
    C: "OO",2100,0
    C: "OO",1100,2100
{material_connections}
}}
'''.format(
        generator=pathlib.Path(path).stem and 'make_camp_models.py',
        name=name,
        vertex_count=len(vertices), vertices=_numbers(vertices),
        index_count=len(indices), indices=_numbers(indices, 8),
        normal_count=len(normals), normals=_numbers(normals),
        uv_count=len(uvs), uvs=_numbers(uvs, 8),
        uv_index_count=len(uv_indices), uv_indices=_numbers(uv_indices, 8),
        material_count=len(table), material_layer=material_layer,
        material_objects=material_objects,
        material_connections=material_connections)

    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)

    # **Newline='\\n', and this is a bug fix, not a preference.** Python's text
    # mode translates to the platform's line ending, so the same generator run
    # on Windows produced files 285 bytes larger than the committed ones -- the
    # same model, every line a byte longer. Git treats `.fbx` as binary and
    # cannot normalise that away, so merely *re-running* a generator showed
    # forty modified files and no diff to explain them.
    with path.open('w', encoding='utf-8', newline='\n') as handle:
        handle.write(text)
    return len(points), len(faces), len(text)
