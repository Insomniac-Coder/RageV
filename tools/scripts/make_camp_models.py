#!/usr/bin/env python3
"""Model the camp's props and export them as FBX.

    python tools/scripts/make_camp_models.py

A forest camp at night, low-poly and flat-shaded. Everything here is built
from boxes, cones and tapered prisms, because that is what the look is: a
faceted tree reads as a tree at eight sides and stops reading as one the
moment it is smoothed.

Nine props, and the scene places many instances of most of them -- which is
also what puts the instanced draw path under load, since a hundred trees
sharing one mesh and one material is exactly the case 3.6 batches.

Sizes are metres. The camp is human-scale: the tent is 2.4 m across, a chair
is 0.9 m tall, and a pine is between five and nine.
"""

import argparse
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import fbxwrite  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]


# --- the props ----------------------------------------------------------------

def pine(tiers=5, height=6.4, radius=1.65):
    """A stack of short, wide, overlapping cones -- a skirted conifer.

    The first version stacked tall thin cones and read as a row of party hats.
    What makes the reference look like a conifer is the **step**: each tier is
    much wider than it is tall, and its base overhangs the tier below, so the
    silhouette is a staircase rather than a triangle.

    Each tier is also given a twist of its own, so the eight facets do not line
    up between tiers. Aligned facets read as a lathe-turned object; offset ones
    read as branches.

    Canopy only. The trunk is its own prop so it can be brown -- one FBX
    carries one material here, and a green trunk is the detail that gives the
    whole thing away.
    """
    mesh = fbxwrite.Mesh()

    base = height * 0.16
    span = height - base

    for tier in range(tiers):
        t = tier / max(tiers - 1, 1)

        # Wide at the bottom, small at the top, and *short* throughout: a
        # tier is about a third as tall as it is wide.
        tier_radius = radius * (1.0 - 0.66 * t)
        tier_height = tier_radius * 1.5

        # Overlapping: each sits only part of the way up, so the one above
        # covers its point and leaves the skirt showing.
        y = base + span * 0.62 * t

        mesh.cone(tier_radius, tier_height, sides=8, base_y=y,
                  yaw=0.39 * tier)

    return mesh


def pine_trunk():
    """The bit under the canopy. Short, because most of it is hidden."""
    mesh = fbxwrite.Mesh()
    mesh.cylinder(0.17, 1.15, sides=6, taper=0.72)
    return mesh


def tent():
    """A ridge tent: two sloped sides, a back wall, and a dark doorway.

    The doorway is a recess rather than a hole -- an open front would show the
    inside of the far side lit from outside, and at this size a dark inset
    reads as a tent you could crawl into.
    """
    mesh = fbxwrite.Mesh()

    # The two sloped sides, as very flat wedges meeting at the ridge.
    for sign in (-1.0, 1.0):
        mesh.prism(0.0, 1.55, (0.06, 1.5), (0.06, 1.5),
                   offset=(sign * 0.62, 0.0, 0.0), yaw=0.0,
                   scale=(1.0, 1.0, 1.0))

    return mesh


def tent_side():
    """One canvas panel, placed twice by the scene at opposite angles."""
    mesh = fbxwrite.Mesh()
    mesh.box((-0.02, 0.0, -1.5), (0.02, 1.72, 1.5))
    return mesh


def tent_back():
    """The triangular back wall."""
    mesh = fbxwrite.Mesh()
    points = [(-1.25, 0.0, 0.0), (1.25, 0.0, 0.0), (0.0, 1.62, 0.0),
              (-1.25, 0.0, 0.05), (1.25, 0.0, 0.05), (0.0, 1.62, 0.05)]
    faces = [(0, 2, 1), (3, 4, 5), (0, 1, 4, 3), (1, 2, 5, 4), (2, 0, 3, 5)]
    mesh.add(points, faces)
    return mesh


def chair():
    """A folding camp chair: four splayed legs, a seat and a back."""
    mesh = fbxwrite.Mesh()

    for x in (-0.26, 0.26):
        for z in (-0.24, 0.24):
            # Splayed, so the legs form an X from the side rather than a box.
            mesh.prism(0.0, 0.44, (0.03, 0.03), (0.02, 0.02),
                       offset=(x * 1.25, 0.0, z * 1.25))
            mesh.prism(0.44, 0.02, (0.02, 0.02), (0.02, 0.02),
                       offset=(x, 0.0, z))

    mesh.box((-0.3, 0.42, -0.28), (0.3, 0.47, 0.28))          # the seat
    mesh.prism(0.47, 0.5, (0.3, 0.05), (0.28, 0.05),
               offset=(0.0, 0.0, -0.24))                       # the back
    mesh.box((-0.31, 0.4, -0.3), (-0.26, 0.98, -0.2))          # arm posts
    mesh.box((0.26, 0.4, -0.3), (0.31, 0.98, -0.2))
    return mesh


def log():
    """A fallen trunk, eight-sided and slightly tapered."""
    mesh = fbxwrite.Mesh()
    mesh.cylinder(0.22, 2.6, sides=8, taper=0.86)
    return mesh


def stump():
    """A cut stump, for sitting on and for the axe."""
    mesh = fbxwrite.Mesh()
    mesh.cylinder(0.28, 0.42, sides=8, taper=0.88)
    return mesh


def rock():
    """A boulder: an eight-sided lump, squashed."""
    mesh = fbxwrite.Mesh()
    mesh.cone(0.36, 0.3, sides=7)
    mesh.cone(0.36, -0.16, sides=7)      # the underside, inverted
    return mesh


def grass():
    """A clump of splayed blades, each tapering to a point.

    The first version was three crossed strips and read as three little cones.
    What makes the reference a *plant* is that the blades **splay**: they leave
    a common root at different angles, from nearly upright to nearly
    horizontal, and each narrows along its length rather than being a ribbon of
    constant width.

    Seven blades -- six is symmetric enough to notice and eight is a palm.
    """
    mesh = fbxwrite.Mesh()

    # (lean from vertical, length, half width at the root)
    blades = (
        (0.10, 0.34, 0.030),
        (0.28, 0.30, 0.028),
        (0.62, 0.26, 0.026),
        (1.02, 0.30, 0.024),
        (0.44, 0.36, 0.030),
        (0.86, 0.24, 0.022),
        (0.18, 0.28, 0.026),
    )

    for i, (lean, length, width) in enumerate(blades):
        reach = math.sin(lean) * length
        rise = math.cos(lean) * length

        # A wedge: a rectangle at the root closing to a short edge at the tip,
        # so the blade comes to a point rather than a chisel end.
        points = [
            (-width, 0.0, -0.012), (width, 0.0, -0.012),
            (width, 0.0, 0.012), (-width, 0.0, 0.012),
            (reach - 0.004, rise, 0.0), (reach + 0.004, rise, 0.0),
        ]
        faces = [
            (0, 1, 2, 3),
            (0, 4, 5, 1),
            (2, 5, 4, 3),
            (1, 5, 2),
            (3, 4, 0),
        ]
        mesh.add(points, faces, yaw=i * 0.92)

    return mesh


def pack():
    """A rucksack leaning against something: a rounded box and a bedroll."""
    mesh = fbxwrite.Mesh()
    mesh.prism(0.0, 0.52, (0.19, 0.13), (0.16, 0.11))
    mesh.box((-0.2, 0.52, -0.14), (0.2, 0.62, 0.14))
    return mesh


def firewood():
    """A stack of split logs beside the fire."""
    mesh = fbxwrite.Mesh()
    for i, (x, z, yaw) in enumerate(((0.0, 0.0, 0.0), (0.18, 0.05, 0.35),
                                     (-0.16, 0.08, -0.28), (0.02, 0.2, 0.9))):
        mesh.cylinder(0.07, 0.62, sides=6, taper=0.95,
                      offset=(x, 0.07 + i * 0.005, z), yaw=yaw)
    return mesh


def mirror_frame():
    """A cheval mirror: a tall plank frame with a kickstand strut behind it.

    Standing rather than leaning, which is what the reference shows and what
    makes it useful here -- a mirror flat against a tent reflects the tent. On
    its own strut it can be angled at the fire, and what it reflects is the
    thing the scene is about.

    Nine boxes. The strut is a tapered prism so it reads as a prop rather than
    a stick.
    """
    mesh = fbxwrite.Mesh()
    w, h, t = 0.22, 1.28, 0.04

    mesh.box((-w - 0.055, 0.0, -t), (w + 0.055, 0.06, t))        # foot rail
    mesh.box((-w - 0.055, h, -t), (w + 0.055, h + 0.07, t))      # head rail
    mesh.box((-w - 0.055, 0.06, -t), (-w, h, t))                 # left stile
    mesh.box((w, 0.06, -t), (w + 0.055, h, t))                   # right stile
    mesh.box((-w, 0.06, -t), (w, h, -t + 0.014))                 # backing board

    # The kickstand: hinged near the top, splayed back to the ground.
    mesh.prism(0.0, 0.98, (0.03, 0.03), (0.022, 0.022),
               offset=(0.0, 0.16, -0.30))
    mesh.box((-0.035, 0.14, -0.34), (0.035, 0.2, -0.02))         # the foot of it
    mesh.box((-0.03, 1.08, -0.06), (0.03, 1.15, -0.02))          # the hinge block
    return mesh


def mirror_glass():
    """The pane. Its own mesh so it can carry its own material -- a mirror is
    a *material* (metal, nearly zero roughness) rather than a shape, and the
    frame around it is emphatically not one."""
    mesh = fbxwrite.Mesh()
    mesh.box((-0.22, 0.06, 0.0), (0.22, 1.28, 0.01))
    return mesh


PROPS = (
    # name, builder, colour, uv scale
    ("pine", pine, (0.13, 0.34, 0.2), 0.5),
    ("pine_small", lambda: pine(tiers=4, height=4.2, radius=1.15),
     (0.16, 0.4, 0.24), 0.5),
    ("pine_trunk", pine_trunk, (0.34, 0.22, 0.14), 1.2),
    ("tent_side", tent_side, (0.82, 0.2, 0.22), 0.6),
    ("tent_back", tent_back, (0.9, 0.9, 0.88), 0.6),
    ("chair", chair, (0.24, 0.26, 0.4), 1.2),
    ("log", log, (0.4, 0.29, 0.21), 0.8),
    ("stump", stump, (0.42, 0.31, 0.22), 1.4),
    ("rock", rock, (0.34, 0.34, 0.39), 1.6),
    ("grass", grass, (0.36, 0.66, 0.24), 2.0),
    ("pack", pack, (0.46, 0.32, 0.2), 1.4),
    ("firewood", firewood, (0.44, 0.31, 0.22), 1.4),
    ("mirror_frame", mirror_frame, (0.5, 0.36, 0.24), 1.6),
    ("mirror_glass", mirror_glass, (0.9, 0.92, 0.95), 1.0),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output',
                        default=str(ROOT / 'SampleProject' / 'assets' / 'models' / 'camp'))
    args = parser.parse_args()

    directory = pathlib.Path(args.output)
    total_faces = 0

    for name, builder, colour, uv in PROPS:
        mesh = builder()
        points, faces, size = fbxwrite.write(
            directory / (name + '.fbx'), mesh, name.title().replace('_', ''),
            colour=colour, uv_scale=uv)
        total_faces += faces
        sys.stdout.write('  {0:<12} {1:4d} verts  {2:4d} faces  {3:6d} bytes\n'.format(
            name, points, faces, size))

    sys.stdout.write('{0} props, {1} faces in all, in {2}\n'.format(
        len(PROPS), total_faces, directory))


if __name__ == '__main__':
    main()
