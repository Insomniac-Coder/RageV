#!/usr/bin/env python3
"""Every generated prop is solid, and wound the right way out.

    python tools/scripts/check_models.py

**Why this exists.** `fbxwrite.cone` and `fbxwrite.cylinder` shipped wound
inside out, and it was reported as *"the trees seem see through"* -- which is
exactly what an inverted winding looks like once backface culling has removed
the front faces. It was also most of why the camp's lighting looked wrong:
normals here are derived from the winding, so every tree, trunk, log and stone
in the scene was being lit **from behind**, and no amount of moving lights
could have fixed it.

`box` and `prism` were correct throughout, which is the tell that went unread
in three renders: the tent and the chairs were solid and shaded, and only the
round things were not.

The claim below is three lines against a known primitive, and it would have
caught it the first time.
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import fbxwrite            # noqa: E402
import make_camp_models    # noqa: E402


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def centre(points):
    n = float(len(points))
    return (sum(p[0] for p in points) / n,
            sum(p[1] for p in points) / n,
            sum(p[2] for p in points) / n)


def inverted_pieces(mesh):
    """Every piece whose faces do not all point away from its own middle.

    **Per piece, not per prop.** A prop is a union of solids -- a chair is four
    legs and a seat -- and a leg at the far corner has inner faces that
    correctly point toward the middle of the *chair*. Measuring against the
    assembly's centroid called that an inverted winding, which is a check
    failing rather than a chair.

    Against each piece's own middle the claim gets to be exact: a closed solid
    has every face pointing away from its centroid, so the answer is all of
    them or something is wrong. No per-prop floor to tune.
    """
    bad = []

    for index, (first, last) in enumerate(mesh.pieces):
        faces = mesh.faces[first:last]
        if not faces:
            continue

        used = set()
        for face in faces:
            used.update(face)
        middle = centre([mesh.points[i] for i in sorted(used)])

        outward = 0
        for face in faces:
            normal = fbxwrite.face_normal(mesh.points, face)
            corner = centre([mesh.points[i] for i in face])
            away = (corner[0] - middle[0], corner[1] - middle[1],
                    corner[2] - middle[2])
            if dot(normal, away) > 0.0:
                outward += 1

        if outward != len(faces):
            bad.append((index, outward, len(faces)))

    return bad


def main():
    failures = []

    # --- the primitives, against answers worked out by hand -------------------
    #
    # A four-sided cone and cylinder, where every normal is checkable on paper.
    # This is the claim that would have caught the defect.
    cone = fbxwrite.Mesh()
    cone.cone(1.0, 1.0, sides=4)
    side = fbxwrite.face_normal(cone.points, cone.faces[0])
    if not (side[0] > 0.0 and side[1] > 0.0 and side[2] > 0.0):
        failures.append(f"a cone's +X+Z side points {side}, not outward and up")

    base = fbxwrite.face_normal(cone.points, [f for f in cone.faces if len(f) == 3][-1])
    if base[1] > -0.9:
        failures.append(f"a cone's base points {base}, not down")

    tube = fbxwrite.Mesh()
    tube.cylinder(1.0, 1.0, sides=4)
    wall = fbxwrite.face_normal(tube.points, tube.faces[0])
    if not (wall[0] > 0.0 and wall[2] > 0.0 and abs(wall[1]) < 0.01):
        failures.append(f"a cylinder's +X+Z wall points {wall}, not straight out")

    caps = [f for f in tube.faces if len(f) == 3]
    bottom = fbxwrite.face_normal(tube.points, caps[0])
    top = fbxwrite.face_normal(tube.points, caps[-1])
    if bottom[1] > -0.9:
        failures.append(f"a cylinder's bottom cap points {bottom}, not down")
    if top[1] < 0.9:
        failures.append(f"a cylinder's top cap points {top}, not up")

    box = fbxwrite.Mesh()
    box.box((-1, 0, -1), (1, 1, 1))
    front = fbxwrite.face_normal(box.points, box.faces[0])
    if front[2] > -0.9:
        failures.append(f"a box's -Z face points {front}, not back")

    # `strut` is `cylinder` seen from a rotated basis, and the rotation is
    # exactly where it can go wrong: the ring's two axes have to come out
    # left-handed against the bar's direction, because that is what `cylinder`
    # does with X, Z and Y. Build one along +Y and it must agree with the
    # cylinder above face for face.
    bar = fbxwrite.Mesh()
    bar.strut((0, 0, 0), (0, 1, 0), 1.0, sides=4)
    if inverted_pieces(bar):
        failures.append("a strut along +Y is inside out, which means its two "
                        "ring axes came out right-handed against its own "
                        "direction")

    # And along an axis with no zero in it, which is the case the basis is
    # actually for -- a bar along +Y could be right by accident.
    slant = fbxwrite.Mesh()
    slant.strut((0, 0, 0), (1, 2, 3), 0.2, sides=5)
    if inverted_pieces(slant):
        failures.append("a strut along (1, 2, 3) is inside out")

    # `panel` decides which way it faces from the order of its corners, and
    # nothing else. Given four corners counter-clockwise in the XY plane seen
    # from +Z, its front face must point at +Z.
    plate = fbxwrite.Mesh()
    plate.panel([(-1, 0, 0), (1, 0, 0), (1, 1, 0), (-1, 1, 0)], 0.1)
    faces = [f for f in plate.faces if len(f) == 4]
    fronts = [fbxwrite.face_normal(plate.points, f) for f in faces]
    if not any(n[2] > 0.99 for n in fronts):
        failures.append("a panel wound counter-clockwise from +Z has no face "
                        f"pointing at +Z; it has {fronts}")
    if inverted_pieces(plate):
        failures.append("a four-corner panel is not a closed solid")

    # Three corners, because a tent gable is a triangle and the general case
    # is what the tent actually uses.
    gable = fbxwrite.Mesh()
    gable.panel([(-1, 0, 0), (1, 0, 0), (0, 1, 0)], 0.1)
    if inverted_pieces(gable):
        failures.append("a three-corner panel is not a closed solid")

    print("primitives: cone, cylinder, box, strut and panel all wound outward")

    # --- and every prop the camp actually uses --------------------------------
    #
    # Every piece of every prop, and every face of every piece. The blades of a
    # grass clump are open wedges rather than closed solids and are the one
    # stated exception -- a wedge has no back, so its root face points into the
    # ground by design.
    open_pieces = {"grass", "grass_tall", "grass_low"}

    for name, builder, _, _ in make_camp_models.PROPS:
        mesh = builder()

        if name in open_pieces:
            print(f"  {name:<14} {len(mesh.pieces)} open piece(s), not closed "
                  f"solids -- skipped, and said so")
            continue

        bad = inverted_pieces(mesh)
        print(f"  {name:<14} {len(mesh.pieces)} piece(s), "
              f"{len(mesh.faces)} faces   {'ok' if not bad else 'INVERTED'}")

        for index, outward, total in bad:
            failures.append(
                f"{name}: piece {index} has {total - outward} of {total} faces "
                f"pointing into itself, which is what an inverted winding looks "
                f"like -- and an inverted face is not merely culled, it is lit "
                f"from behind")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: every primitive is wound outward, and every prop is solid")


if __name__ == "__main__":
    main()
