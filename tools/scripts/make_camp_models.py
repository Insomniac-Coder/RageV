#!/usr/bin/env python3
"""Model the camp's props and export them as FBX.

    python tools/scripts/make_camp_models.py
    python tools/scripts/check_models.py

A forest camp at night, low-poly and flat-shaded. Everything here is built from
boxes, cones, tapered prisms, bars between two points and thickened polygons,
because that is what the look is: a faceted tree reads as a tree at eight sides
and stops reading as one the moment it is smoothed.

**Sizes are metres, and they are the part that goes wrong.** Every prop below
is written against a fixed human scale -- a folding chair is 0.95 m to the top
of its back and 0.45 m to its seat, a ridge tent is 2.6 m across and 1.9 m
tall, a bench log is knee height. The first version of this file was judged by
eye inside the night scene, and produced a chair half its proper height, a
stump too small to sit on and grass too short to see. A prop is checked on its
own, against a metre grid, or it is not being checked.

**One FBX carries one material.** So anything two-coloured is two props: the
canopy and the trunk, the tent and its band, the chair's frame and its seat,
the mirror and its glass. That is not a limitation worth working around -- a
green trunk is the single detail that gives a low-poly tree away.
"""

import argparse
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import fbxwrite  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]


# --- the forest ---------------------------------------------------------------

def pine(tiers=6, height=7.4, radius=1.55):
    """A stack of short, wide, overlapping cones -- a skirted conifer.

    Three things make the reference read as a conifer rather than as a row of
    party hats, and all three are in the arithmetic below.

    **The step.** Each tier is much wider than it is tall and its base overhangs
    the tier below, so the silhouette is a staircase rather than a triangle.

    **The crown is pointier than the skirts.** A tier's height is `r * (1.35 +
    0.9t)`, so the bottom tiers are squat plates and the top one is a spire.
    Constant proportions give a cone made of cones, which is a bush.

    **The facets do not line up.** Each tier gets a twist of its own, so its
    eight edges fall between the edges of the one below. Aligned facets read as
    a lathe-turned object; offset ones read as branches.

    The apex lands exactly at `height`, which is what makes the argument mean
    what it says -- the first version's `height` was a number the tree was
    built from and then overshot by two metres.
    """
    mesh = fbxwrite.Mesh()

    # Where the canopy starts. Below this is trunk, and the trunk being visible
    # is most of what separates a conifer from a shrub.
    canopy = height * 0.22
    crown_radius = radius * 0.30
    crown_height = crown_radius * 2.25

    span = height - crown_height - canopy

    for tier in range(tiers):
        t = tier / max(tiers - 1, 1)

        tier_radius = radius * (1.0 - 0.70 * t)
        tier_height = tier_radius * (1.35 + 0.90 * t)
        y = canopy + span * t

        mesh.cone(tier_radius, tier_height, sides=8, base_y=y, yaw=0.39 * tier)

    return mesh


def pine_trunk(height=2.0, radius=0.20):
    """The bit under the canopy. Six-sided, because a round trunk under a
    faceted canopy is the join that looks wrong."""
    mesh = fbxwrite.Mesh()
    mesh.cylinder(radius, height, sides=6, taper=0.7)
    return mesh


# --- the tent -----------------------------------------------------------------
#
# A ridge tent, 2.6 m across the base, 1.9 m at the ridge and 3.0 m long. Its
# three materials are three props: the red canvas, the pale band around the
# bottom, and the dark opening.

TENT_HALF = 1.3        # half the base width
TENT_RIDGE = 1.9       # height at the ridge
TENT_LONG = 1.5        # half the length
TENT_BAND = 0.52       # how far up the pale band reaches
TENT_SKIN = 0.05


def _tent_x(y):
    """Half-width of the tent at height `y` -- the slope, solved once."""
    return TENT_HALF * (1.0 - y / TENT_RIDGE)


def tent():
    """The red canvas: both sloped sides above the band, and the two gables."""
    mesh = fbxwrite.Mesh()
    band_x = _tent_x(TENT_BAND)

    for sign in (-1.0, 1.0):
        # Counter-clockwise seen from outside, which for the left slope means
        # the opposite order from the right one. Getting this wrong is not a
        # culled face, it is a wall lit from inside.
        low_out = (sign * band_x, TENT_BAND, sign * TENT_LONG)
        low_in = (sign * band_x, TENT_BAND, -sign * TENT_LONG)
        top_out = (0.0, TENT_RIDGE, sign * TENT_LONG)
        top_in = (0.0, TENT_RIDGE, -sign * TENT_LONG)
        mesh.panel([low_in, low_out, top_out, top_in], TENT_SKIN)

    # The gables above the band: a triangle at each end.
    for sign in (-1.0, 1.0):
        z = sign * TENT_LONG
        corners = [(-sign * band_x, TENT_BAND, z), (sign * band_x, TENT_BAND, z),
                   (0.0, TENT_RIDGE, z)]
        mesh.panel(corners, TENT_SKIN)

    return mesh


def tent_band():
    """The pale band around the bottom, and the lower part of both gables."""
    mesh = fbxwrite.Mesh()
    band_x = _tent_x(TENT_BAND)

    for sign in (-1.0, 1.0):
        foot_out = (sign * TENT_HALF, 0.0, sign * TENT_LONG)
        foot_in = (sign * TENT_HALF, 0.0, -sign * TENT_LONG)
        band_out = (sign * band_x, TENT_BAND, sign * TENT_LONG)
        band_in = (sign * band_x, TENT_BAND, -sign * TENT_LONG)
        mesh.panel([foot_in, foot_out, band_out, band_in], TENT_SKIN)

    for sign in (-1.0, 1.0):
        z = sign * TENT_LONG
        corners = [(-sign * TENT_HALF, 0.0, z), (sign * TENT_HALF, 0.0, z),
                   (sign * band_x, TENT_BAND, z), (-sign * band_x, TENT_BAND, z)]
        mesh.panel(corners, TENT_SKIN)

    return mesh


def tent_door():
    """The opening: a dark trapezoid standing just proud of the front gable.

    A recess rather than a hole. An actual hole would show the inside of the
    far wall lit from outside, and at this size a dark inset reads as a tent
    you could crawl into -- which is all it has to do.
    """
    mesh = fbxwrite.Mesh()
    z = TENT_LONG + 0.02
    top = 1.24
    mesh.panel([(-0.52, 0.0, z), (0.52, 0.0, z),
                (_tent_x(top) * 0.62, top, z), (-_tent_x(top) * 0.62, top, z)],
               0.03)
    return mesh


def guy_line():
    """One guy rope and its peg, modelled from the ridge end to the ground.

    Four of these is the difference between a tent and a red wedge sitting on
    the floor. They are also the only thing in the scene thin enough to test
    that a 2 cm bar survives the anti-aliasing.
    """
    mesh = fbxwrite.Mesh()
    mesh.strut((0.0, 1.88, 0.0), (0.0, 0.06, 1.15), 0.012, sides=4)
    mesh.strut((0.0, 0.14, 1.15), (0.0, -0.04, 1.19), 0.022, sides=4)
    return mesh


# --- the fire -----------------------------------------------------------------

def flame_outer():
    """The flame, as geometry: a cluster of faceted shards.

    **The fire is modelled, not simulated.** A particle system alone gives a
    soft orange smudge, and the reference's fire is emphatically hard-edged --
    angular tongues with visible facets, which is what makes it belong to the
    same drawing as the trees. The particles that remain are embers rising off
    it, which is what particles are actually good at.

    Six shards, each a squashed few-sided cone, leaning outward and twisted so
    no two silhouettes agree. The tallest is in the middle; a cluster with its
    tallest at the edge reads as a fire falling over.
    """
    mesh = fbxwrite.Mesh()

    # (x, z, radius, height, yaw, squash across the lean)
    # Wider than the first cut, which was 0.53 m across and 0.95 m tall and
    # read as a torch. A camp fire is roughly as wide as it is high -- the
    # reference's is wider -- and a narrow flame over a wide ring of stones
    # looks like something stuck in the ground rather than something burning.
    shards = (
        (0.00, 0.00, 0.28, 0.86, 0.0, 1.0),
        (0.19, 0.09, 0.21, 0.58, 0.7, 0.85),
        (-0.21, 0.06, 0.23, 0.66, 2.1, 0.9),
        (0.06, -0.22, 0.20, 0.50, 3.4, 0.8),
        (-0.09, -0.19, 0.17, 0.42, 4.6, 0.85),
        (0.24, -0.06, 0.15, 0.34, 5.5, 0.8),
    )

    for x, z, radius, height, yaw, squash in shards:
        mesh.cone(radius, height, sides=5, yaw=yaw,
                  offset=(x, 0.0, z), scale=(1.0, 1.0, squash))

    return mesh


def flame_inner():
    """The hot core: three shorter, brighter shards inside the outer ones.

    Two colours of flame rather than one, because the reference has a pale
    yellow heart inside the orange and that is what stops it reading as a
    traffic cone. Shorter, so the outer shards are what makes the silhouette.
    """
    mesh = fbxwrite.Mesh()
    for x, z, radius, height, yaw in ((0.0, 0.0, 0.155, 0.56, 0.4),
                                      (0.10, 0.04, 0.10, 0.36, 2.6),
                                      (-0.10, -0.03, 0.095, 0.30, 4.4)):
        mesh.cone(radius, height, sides=5, yaw=yaw, offset=(x, 0.02, z))
    return mesh


def fire_logs():
    """The wood that is actually burning: leaned in, crossed, and untidy.

    **A fire is not a woodpile.** The first version put a neat criss-cross
    stack under the flame -- three logs one way, two the other, all parallel
    and all the same length -- and it read as a pallet with a fire on top.
    Nobody builds a fire like that and nothing that has been burning for an
    hour stays like that.

    What a burning fire actually is: a few logs **leaning inward** so their
    ends meet over the hottest point, one or two lying across the base at
    whatever angle they fell to, and the lengths all different. Not one bar
    here is parallel to another, and that is the entire point.
    """
    mesh = fbxwrite.Mesh()

    # Leaning in, meeting over the middle. The feet are not evenly spaced round
    # the ring and the tops do not meet at one point -- they lean past each
    # other, which is what stops it reading as a tepee diagram.
    leans = (
        (0.44, 0.35, 0.62, 0.078, 0.09),    # (foot radius, foot yaw, top height, thickness, top offset yaw)
        (0.40, 2.05, 0.54, 0.072, 2.4),
        (0.46, 3.55, 0.58, 0.080, 3.2),
        (0.38, 5.10, 0.46, 0.068, 4.9),
    )
    for radius, yaw, top, thick, top_yaw in leans:
        foot = (math.cos(yaw) * radius, 0.05, math.sin(yaw) * radius)
        crown = (math.cos(top_yaw) * 0.10, top, math.sin(top_yaw) * 0.10)
        mesh.strut(foot, crown, thick, sides=5, taper=0.85)

    # Two lying across the base, at the angles they happened to end up at.
    mesh.strut((-0.42, 0.075, 0.14), (0.36, 0.075, -0.06), 0.075, sides=5,
               taper=0.9)
    mesh.strut((0.16, 0.075, 0.40), (-0.20, 0.075, -0.42), 0.066, sides=5,
               taper=0.88)

    # A short burnt-through end, fallen off the rest.
    mesh.strut((0.30, 0.06, 0.30), (0.52, 0.06, 0.44), 0.055, sides=5,
               taper=0.8)
    return mesh


def firewood():
    """The *spare* wood, stacked beside the fire and not in it.

    This one is allowed to be tidy, because a stack of cut wood waiting its
    turn is tidy -- somebody stacked it. It belongs a metre away from the ring,
    not under the flame.
    """
    mesh = fbxwrite.Mesh()

    for x in (-0.17, 0.0, 0.17):
        mesh.strut((x, 0.09, -0.32), (x, 0.09, 0.32), 0.088, sides=6,
                   taper=0.94)
    for z in (-0.09, 0.09):
        mesh.strut((-0.28, 0.26, z), (0.28, 0.26, z), 0.085, sides=6,
                   taper=0.94)
    return mesh


def log_split():
    """One split billet, for the ones lying loose around the ring."""
    mesh = fbxwrite.Mesh()
    mesh.strut((-0.26, 0.075, 0.0), (0.26, 0.075, 0.0), 0.075, sides=5,
               taper=0.92)
    return mesh


# --- what people sit on -------------------------------------------------------

CHAIR_WIDE = 0.30       # half the width
CHAIR_SEAT = 0.45       # seat height
CHAIR_TOP = 0.95        # top of the back

# The chair, as the six points everything else is derived from. Written out
# because the first version guessed at them and produced a shape that was not
# any chair: bars that crossed in the wrong place, a seat rail with nothing
# under its back edge, and a back post that leaned out of a joint that did not
# exist.
CHAIR_FRONT_FOOT = 0.28      # z of the front feet
CHAIR_BACK_FOOT = -0.30      # z of the back feet
CHAIR_SEAT_FRONT = 0.26      # z of the seat's front rail
CHAIR_SEAT_BACK = -0.24      # z of the seat's back rail, and the X's crossing
CHAIR_BACK_TOP = -0.34       # z of the top of the back


def chair_frame():
    """A folding camp chair's frame: an X on each side, and five rails.

    **The X is the chair, and it has to actually cross.** Each side is a bar
    from the front foot back and up to the *back* of the seat, and a second bar
    from the back foot forward and up to the *front* of the seat. Those two
    cross halfway, which is the hinge a folding chair folds about and the shape
    the eye reads even in silhouette.

    From the back of the seat the frame carries on upward as the back post,
    with a bend at the seat -- not a straight bar from the floor to the
    headrest. That bend is why a camp chair's back leans and its legs do not.

    Bars between two points rather than rotated cylinders, because the two
    points are the thing that is actually known.
    """
    mesh = fbxwrite.Mesh()
    bar = 0.019

    for sign in (-1.0, 1.0):
        x = sign * CHAIR_WIDE

        # Front foot, back and up to the seat's back corner.
        mesh.strut((x, 0.0, CHAIR_FRONT_FOOT),
                   (x, CHAIR_SEAT, CHAIR_SEAT_BACK), bar, sides=4)
        # The back post, bending outward from that same corner.
        mesh.strut((x, CHAIR_SEAT, CHAIR_SEAT_BACK),
                   (x * 0.95, CHAIR_TOP, CHAIR_BACK_TOP), bar, sides=4)
        # Back foot, forward and up to the seat's front corner -- and this is
        # the bar that crosses the first one.
        mesh.strut((x, 0.0, CHAIR_BACK_FOOT),
                   (x, CHAIR_SEAT, CHAIR_SEAT_FRONT), bar, sides=4)

        # The armrest and the post that holds its front end up.
        mesh.strut((x, 0.68, CHAIR_SEAT_FRONT - 0.02),
                   (x * 0.96, 0.72, CHAIR_BACK_TOP + 0.06), 0.016, sides=4)
        mesh.strut((x, CHAIR_SEAT, CHAIR_SEAT_FRONT),
                   (x, 0.68, CHAIR_SEAT_FRONT - 0.02), 0.015, sides=4)

    # The rails that tie the two sides together. The seat needs one at each
    # edge or the cloth has nothing to hang from at the back.
    for y, z, radius in ((0.03, CHAIR_FRONT_FOOT, bar),
                         (0.03, CHAIR_BACK_FOOT, bar),
                         (CHAIR_SEAT, CHAIR_SEAT_FRONT, bar),
                         (CHAIR_SEAT, CHAIR_SEAT_BACK, bar)):
        mesh.strut((-CHAIR_WIDE, y, z), (CHAIR_WIDE, y, z), radius, sides=4)

    mesh.strut((-CHAIR_WIDE * 0.95, CHAIR_TOP, CHAIR_BACK_TOP),
               (CHAIR_WIDE * 0.95, CHAIR_TOP, CHAIR_BACK_TOP), bar, sides=4)

    # Feet, so the bars do not end in a point on the ground.
    for x in (-CHAIR_WIDE, CHAIR_WIDE):
        for z in (CHAIR_FRONT_FOOT, CHAIR_BACK_FOOT):
            mesh.box((x - 0.03, 0.0, z - 0.032), (x + 0.03, 0.045, z + 0.032))

    return mesh


def chair_seat():
    """The sling: a seat that dips toward the back, and a leaning back rest.

    Slung, not flat. A camp chair's seat is a piece of cloth under load and it
    is always lower at the back than the front, which is a four-centimetre
    difference that does most of the work of making it look sat-in.

    Both panels hang between rails the frame actually has, which is the part
    that was wrong before -- cloth stretched to a rail that is not there reads
    as a floating rectangle, and that is exactly what it looked like.
    """
    mesh = fbxwrite.Mesh()

    mesh.panel([(-0.27, CHAIR_SEAT + 0.015, CHAIR_SEAT_FRONT),
                (0.27, CHAIR_SEAT + 0.015, CHAIR_SEAT_FRONT),
                (0.27, CHAIR_SEAT - 0.025, CHAIR_SEAT_BACK),
                (-0.27, CHAIR_SEAT - 0.025, CHAIR_SEAT_BACK)], 0.03)

    mesh.panel([(-0.27, CHAIR_SEAT + 0.02, CHAIR_SEAT_BACK - 0.015),
                (0.27, CHAIR_SEAT + 0.02, CHAIR_SEAT_BACK - 0.015),
                (0.26, CHAIR_TOP - 0.02, CHAIR_BACK_TOP - 0.01),
                (-0.26, CHAIR_TOP - 0.02, CHAIR_BACK_TOP - 0.01)], 0.03)
    return mesh


def log():
    """A fallen trunk somebody has been sitting on. Knee height, lying down.

    Modelled lying rather than standing, so the scene does not have to rotate
    it ninety degrees and then guess how far to lift it -- the first version
    was a standing cylinder and read as a column.
    """
    mesh = fbxwrite.Mesh()
    mesh.strut((-1.25, 0.27, 0.0), (1.25, 0.27, 0.06), 0.27, sides=8,
               taper=0.88)
    # A cut branch stub, which is what stops it reading as a pipe.
    mesh.strut((0.3, 0.34, 0.02), (0.52, 0.52, 0.2), 0.055, sides=5, taper=0.8)
    return mesh


def stump():
    """A cut stump: something to sit on, or to stand a mug on.

    0.45 m tall and 0.68 m across -- a stump you could actually sit on. The
    first was 0.42 across and read as a tin can.
    """
    mesh = fbxwrite.Mesh()
    mesh.cylinder(0.34, 0.45, sides=8, taper=0.9)
    # Roots flaring at the *base*, because a cut stump is not a cylinder at the
    # ground -- it is a cylinder that spreads into the earth. Low and wide: at
    # 0.2 m these were nearly half the stump's height and read as flaps stuck
    # to its side.
    for yaw in (0.6, 2.4, 4.3, 5.4):
        mesh.cone(0.11, 0.11, sides=5,
                  offset=(math.cos(yaw) * 0.30, 0.0, math.sin(yaw) * 0.30))
    return mesh


# --- the ground cover ---------------------------------------------------------

def rock(radius=0.42, top=0.34, bottom=0.18, sides=7):
    """A boulder: two cones back to back, so it has a top and an underside.

    A negative height would move the apex down and leave the winding pointing
    up, which is inside out -- `flip` does both, and check_models found exactly
    that here the first time.
    """
    mesh = fbxwrite.Mesh()
    mesh.cone(radius, top, sides=sides)
    mesh.cone(radius, bottom, sides=sides, flip=True)
    return mesh


def grass(scale=1.0, lean_bias=0.0, twist=0.72, count=9):
    """A clump of splayed blades, each tapering to a point.

    What makes the reference a *plant* is that the blades **splay**: they leave
    a common root at different angles, from nearly upright to nearly
    horizontal, and each narrows along its length rather than being a ribbon of
    constant width.

    Nine blades at 0.4 m rather than seven at 0.3 m. The tufts in the reference
    stand about as high as a stump is wide and they are the brightest colour in
    the frame after the fire; at the first size they were three centimetres of
    dark green and simply did not appear.

    Parameterised because **one tuft repeated four hundred times is a pattern,
    not ground cover.** `lean_bias` opens the clump out toward a low sprawl and
    `twist` changes which way the blades face, so the variants are genuinely
    different silhouettes rather than the same one at three sizes -- which the
    eye picks out immediately at this density.
    """
    mesh = fbxwrite.Mesh()

    # (lean from vertical, length, half width at the root)
    blades = (
        (0.08, 0.46, 0.042),
        (0.26, 0.40, 0.038),
        (0.58, 0.34, 0.036),
        (0.98, 0.30, 0.032),
        (0.40, 0.44, 0.040),
        (0.82, 0.28, 0.030),
        (0.16, 0.38, 0.036),
        (0.68, 0.32, 0.034),
        (1.15, 0.24, 0.028),
    )[:count]

    blades = tuple((lean + lean_bias, length * scale, width * scale)
                   for lean, length, width in blades)

    for i, (lean, length, width) in enumerate(blades):
        reach = math.sin(lean) * length
        rise = math.cos(lean) * length

        # A wedge: a rectangle at the root closing to a short edge at the tip,
        # so the blade comes to a point rather than a chisel end.
        points = [
            (-width, 0.0, -0.014), (width, 0.0, -0.014),
            (width, 0.0, 0.014), (-width, 0.0, 0.014),
            (reach - 0.005, rise, 0.0), (reach + 0.005, rise, 0.0),
        ]
        faces = [
            (0, 1, 2, 3),
            (0, 4, 5, 1),
            (2, 5, 4, 3),
            (1, 5, 2),
            (3, 4, 0),
        ]
        mesh.add(points, faces, yaw=i * twist)

    return mesh


# --- the kit ------------------------------------------------------------------

def pack():
    """A rucksack: a body that tapers, a top flap, a pocket and two straps."""
    mesh = fbxwrite.Mesh()

    mesh.prism(0.0, 0.52, (0.19, 0.13), (0.17, 0.115))
    mesh.prism(0.52, 0.62, (0.175, 0.12), (0.15, 0.10))       # the top flap
    mesh.box((-0.13, 0.10, 0.115), (0.13, 0.30, 0.16))        # the front pocket

    for x in (-0.10, 0.10):
        mesh.panel([(x - 0.035, 0.10, -0.135), (x + 0.035, 0.10, -0.135),
                    (x + 0.035, 0.50, -0.155), (x - 0.035, 0.50, -0.155)], 0.02)
    return mesh


def bedroll():
    """A rolled sleeping mat with two straps round it. Lies on its side."""
    mesh = fbxwrite.Mesh()
    mesh.strut((-0.34, 0.15, 0.0), (0.34, 0.15, 0.0), 0.15, sides=9)
    # The straps go *round* the roll, so they share its axis. Run across it and
    # they are two bars driven through the middle of the mat, which is what the
    # first version looked like: three lumps rather than a roll.
    for x in (-0.17, 0.17):
        mesh.strut((x - 0.022, 0.15, 0.0), (x + 0.022, 0.15, 0.0), 0.158,
                   sides=9)
    return mesh


def suitcase():
    """A hard case: a lid seam, two latches and a handle.

    The reference has one lying behind the fire with a bedroll on it, and it is
    the prop that most needs its *details* -- a plain brown box at this size
    reads as a crate.
    """
    mesh = fbxwrite.Mesh()

    mesh.box((-0.31, 0.0, -0.21), (0.31, 0.20, 0.21))          # the body
    mesh.box((-0.315, 0.20, -0.215), (0.315, 0.235, 0.215))    # the lid seam
    mesh.box((-0.30, 0.235, -0.20), (0.30, 0.40, 0.20))        # the lid

    for x in (-0.16, 0.16):
        mesh.box((x - 0.05, 0.16, 0.205), (x + 0.05, 0.28, 0.235))   # latches

    mesh.strut((-0.11, 0.40, 0.0), (-0.10, 0.47, 0.0), 0.018, sides=4)
    mesh.strut((0.10, 0.40, 0.0), (0.11, 0.47, 0.0), 0.018, sides=4)
    mesh.strut((-0.11, 0.47, 0.0), (0.11, 0.47, 0.0), 0.018, sides=4)
    return mesh


def cooler():
    """A cool box with a lid lip and a moulded handle on the end."""
    mesh = fbxwrite.Mesh()

    mesh.prism(0.0, 0.30, (0.30, 0.18), (0.285, 0.17))
    mesh.box((-0.315, 0.30, -0.195), (0.315, 0.36, 0.195))     # the lid
    mesh.strut((-0.315, 0.20, 0.0), (-0.37, 0.20, 0.0), 0.035, sides=4)
    mesh.strut((0.315, 0.20, 0.0), (0.37, 0.20, 0.0), 0.035, sides=4)
    return mesh


def lantern():
    """A camp lantern: a base, a cage of four posts, a top and a bail handle."""
    mesh = fbxwrite.Mesh()

    mesh.cylinder(0.075, 0.05, sides=6)
    mesh.cylinder(0.055, 0.03, sides=6, base_y=0.05)
    for x, z in ((-0.055, -0.055), (0.055, -0.055),
                 (0.055, 0.055), (-0.055, 0.055)):
        mesh.strut((x, 0.05, z), (x, 0.24, z), 0.008, sides=4)
    mesh.cylinder(0.075, 0.035, sides=6, base_y=0.24, taper=0.55)

    # The bail, as three bars -- a real arc would be twenty faces for a handle
    # nobody is going to look at.
    mesh.strut((-0.07, 0.26, 0.0), (-0.05, 0.34, 0.0), 0.008, sides=4)
    mesh.strut((0.07, 0.26, 0.0), (0.05, 0.34, 0.0), 0.008, sides=4)
    mesh.strut((-0.05, 0.34, 0.0), (0.05, 0.34, 0.0), 0.008, sides=4)
    return mesh


def lantern_glass():
    """The lit part. Its own mesh because it is the only emissive kit here."""
    mesh = fbxwrite.Mesh()
    mesh.cylinder(0.05, 0.185, sides=6, base_y=0.05)
    return mesh


def pot():
    """A cooking pot: a body, a rolled rim, and a handle over the top."""
    mesh = fbxwrite.Mesh()
    mesh.cylinder(0.115, 0.16, sides=8, taper=1.06)
    mesh.cylinder(0.128, 0.022, sides=8, base_y=0.16)
    mesh.strut((-0.12, 0.17, 0.0), (-0.09, 0.29, 0.0), 0.009, sides=4)
    mesh.strut((0.12, 0.17, 0.0), (0.09, 0.29, 0.0), 0.009, sides=4)
    mesh.strut((-0.09, 0.29, 0.0), (0.09, 0.29, 0.0), 0.009, sides=4)
    return mesh


def mug():
    """An enamel mug, for the top of the stump."""
    mesh = fbxwrite.Mesh()
    mesh.cylinder(0.045, 0.085, sides=7, taper=1.05)
    mesh.strut((0.045, 0.025, 0.0), (0.072, 0.045, 0.0), 0.008, sides=4)
    mesh.strut((0.072, 0.045, 0.0), (0.045, 0.068, 0.0), 0.008, sides=4)
    return mesh


def fork():
    """A toasting fork: a long bent wire with two tines.

    Bent rather than straight, because these are propped over the fire at an
    angle and a straight one reads as a dropped skewer.
    """
    mesh = fbxwrite.Mesh()
    mesh.strut((0.0, 0.0, 0.0), (0.0, 0.0, 0.62), 0.010, sides=4)
    mesh.strut((0.0, 0.0, 0.62), (0.0, 0.05, 0.86), 0.009, sides=4)
    for x in (-0.022, 0.022):
        mesh.strut((0.0, 0.05, 0.86), (x, 0.06, 0.95), 0.006, sides=4)
    # The grip, along the wire rather than across it. It was a standing
    # cylinder to begin with, which put a 14 cm post sticking straight up out
    # of the handle end -- visible immediately on the verification sheet and
    # not at all in the scene.
    mesh.strut((0.0, 0.0, -0.02), (0.0, 0.0, 0.13), 0.018, sides=5)
    return mesh


def marshmallow():
    """Two on a fork. A squat faceted barrel is exactly what one looks like."""
    mesh = fbxwrite.Mesh()
    for z in (0.0, 0.062):
        mesh.strut((0.0, 0.0, z), (0.0, 0.0, z + 0.046), 0.03, sides=6)
    return mesh


# --- the van ------------------------------------------------------------------
#
# 4.3 m long, 1.85 m wide and 1.9 m to the roof rack, which is a real minivan
# and by some way the largest thing in the camp. It matters that it is: the
# scene had nothing between a two-metre tent and a nine-metre tree, and a
# vehicle parked at the edge is what gives the clearing a reason to be a place
# somebody drove to.
#
# Three materials, so three meshes: the paint, the glass and the wheels.

VAN_HALF = 0.92        # half the body width
VAN_NOSE = 2.15        # z of the front bumper
VAN_TAIL = -2.15       # z of the rear
VAN_SILL = 0.42        # underside of the body
VAN_WAIST = 1.16       # where the panels stop and the windows start
VAN_ROOF = 1.78


def van_body():
    """The painted shell: sills, a short bonnet, a boxy greenhouse, a rack."""
    mesh = fbxwrite.Mesh()

    # The lower body, from the rear to the base of the windscreen.
    mesh.box((-VAN_HALF, VAN_SILL, VAN_TAIL), (VAN_HALF, VAN_WAIST, 1.28))
    # The bonnet, lower and shorter -- a minivan has one, a bus does not, and
    # that step is most of what tells them apart in silhouette.
    mesh.box((-VAN_HALF + 0.02, VAN_SILL, 1.28),
             (VAN_HALF - 0.02, 1.02, VAN_NOSE))

    # The roof, and the two pillars that are not glass.
    mesh.box((-VAN_HALF, VAN_ROOF - 0.08, VAN_TAIL),
             (VAN_HALF, VAN_ROOF, 1.30))
    for x in (-VAN_HALF, VAN_HALF - 0.07):
        mesh.box((x, VAN_WAIST, VAN_TAIL), (x + 0.07, VAN_ROOF, VAN_TAIL + 0.08))
        mesh.box((x, VAN_WAIST, 1.22), (x + 0.07, VAN_ROOF, 1.30))
        # The B pillar, which is what stops the side glass being one long slab.
        mesh.box((x, VAN_WAIST, -0.34), (x + 0.07, VAN_ROOF, -0.22))

    # The windscreen frame, raked forward from the bonnet to the roof.
    mesh.panel([(-VAN_HALF, 1.02, VAN_NOSE - 0.06),
                (VAN_HALF, 1.02, VAN_NOSE - 0.06),
                (VAN_HALF, VAN_ROOF, 1.30), (-VAN_HALF, VAN_ROOF, 1.30)], 0.06)

    # Bumpers.
    mesh.box((-VAN_HALF - 0.03, 0.36, VAN_NOSE - 0.04),
             (VAN_HALF + 0.03, 0.66, VAN_NOSE + 0.08))
    mesh.box((-VAN_HALF - 0.03, 0.36, VAN_TAIL - 0.08),
             (VAN_HALF + 0.03, 0.72, VAN_TAIL + 0.04))

    # The roof rack. A camping van has one and it is two bars and four feet.
    for x in (-0.62, 0.62):
        mesh.strut((x, VAN_ROOF + 0.10, -1.85), (x, VAN_ROOF + 0.10, 1.05),
                   0.035, sides=4)
        for z in (-1.7, 0.9):
            mesh.box((x - 0.05, VAN_ROOF - 0.01, z - 0.05),
                     (x + 0.05, VAN_ROOF + 0.10, z + 0.05))

    # Wheel arches, as a lip round each opening -- without them the wheels look
    # like they are parked beside the van rather than under it.
    for z in (1.45, -1.35):
        for x in (-VAN_HALF, VAN_HALF - 0.04):
            mesh.box((x, VAN_SILL - 0.06, z - 0.44), (x + 0.04, 0.86, z + 0.44))

    return mesh


def van_glass():
    """Windscreen, four side windows and the tailgate. Dark, and flat."""
    mesh = fbxwrite.Mesh()

    mesh.panel([(-0.86, 1.06, VAN_NOSE - 0.10), (0.86, 1.06, VAN_NOSE - 0.10),
                (0.86, VAN_ROOF - 0.06, 1.34), (-0.86, VAN_ROOF - 0.06, 1.34)],
               0.03)

    for sign in (-1.0, 1.0):
        x = sign * (VAN_HALF - 0.03)
        for near, far in ((-0.22, 1.20), (-1.98, -0.36)):
            # Wound so each side's outward face is the one facing out.
            corners = [(x, VAN_WAIST + 0.04, sign * near),
                       (x, VAN_WAIST + 0.04, sign * far),
                       (x, VAN_ROOF - 0.08, sign * far),
                       (x, VAN_ROOF - 0.08, sign * near)]
            mesh.panel(corners, 0.03)

    mesh.panel([(0.84, VAN_WAIST + 0.04, VAN_TAIL + 0.06),
                (-0.84, VAN_WAIST + 0.04, VAN_TAIL + 0.06),
                (-0.84, VAN_ROOF - 0.08, VAN_TAIL + 0.06),
                (0.84, VAN_ROOF - 0.08, VAN_TAIL + 0.06)], 0.03)
    return mesh


def van_wheels():
    """Four of them, as one mesh, because they share one material.

    Eight-sided and lying on their axle. A wheel is the one round thing on a
    vehicle this angular, and at eight sides it stays part of the same drawing.
    """
    mesh = fbxwrite.Mesh()
    for z in (1.45, -1.35):
        for sign in (-1.0, 1.0):
            x = sign * (VAN_HALF - 0.06)
            mesh.strut((x, 0.36, z), (x + sign * 0.20, 0.36, z), 0.36, sides=8)
    return mesh


# --- the mirror ---------------------------------------------------------------

def mirror_frame():
    """A cheval mirror: a plank frame with a kickstand strut behind it.

    Standing rather than leaning, which is what the reference for it shows and
    what makes it useful here -- a mirror flat against a tent reflects the
    tent. On its own strut it can be angled at the fire, and what it reflects is
    the thing the scene is about.
    """
    mesh = fbxwrite.Mesh()
    w, h, t = 0.26, 1.35, 0.045

    mesh.box((-w - 0.06, 0.0, -t), (w + 0.06, 0.07, t))          # foot rail
    mesh.box((-w - 0.06, h, -t), (w + 0.06, h + 0.08, t))        # head rail
    mesh.box((-w - 0.06, 0.07, -t), (-w, h, t))                  # left stile
    mesh.box((w, 0.07, -t), (w + 0.06, h, t))                    # right stile
    mesh.box((-w, 0.07, -t), (w, h, -t + 0.016))                 # backing board

    # The kickstand: hinged near the top, splayed back to the ground.
    mesh.strut((0.0, 1.14, -0.04), (0.0, 0.03, -0.44), 0.028, sides=4)
    mesh.box((-0.05, 0.0, -0.50), (0.05, 0.06, -0.36))           # its foot
    return mesh


def mirror_glass():
    """The pane. Its own mesh so it can carry its own material -- a mirror is
    a *material* (metal, nearly zero roughness) rather than a shape, and the
    frame around it is emphatically not one."""
    mesh = fbxwrite.Mesh()
    mesh.box((-0.26, 0.07, 0.0), (0.26, 1.35, 0.012))
    return mesh


# --- the animals --------------------------------------------------------------

def rabbit():
    """A rabbit at the edge of the firelight.

    The reference has three small animals watching the fire, and they are what
    make the camp feel visited rather than staged. Low enough poly that the
    ears are two thickened triangles, which at 30 cm tall is all they need to
    be.
    """
    mesh = fbxwrite.Mesh()

    mesh.prism(0.055, 0.20, (0.075, 0.125), (0.062, 0.10))       # the body
    mesh.prism(0.10, 0.20, (0.078, 0.075), (0.070, 0.065),
               offset=(0.0, 0.0, -0.085))                         # the haunch
    mesh.box((-0.05, 0.17, 0.055), (0.05, 0.28, 0.155))          # the head
    mesh.box((-0.032, 0.24, 0.145), (0.032, 0.27, 0.175))        # the muzzle

    for x in (-0.032, 0.032):
        mesh.panel([(x - 0.022, 0.27, 0.06), (x + 0.022, 0.27, 0.06),
                    (x + 0.016, 0.40, 0.03), (x - 0.016, 0.40, 0.03)], 0.014)

    mesh.cone(0.038, 0.055, sides=5, offset=(0.0, 0.155, -0.15), flip=True)
    for x, z in ((-0.055, 0.10), (0.055, 0.10), (-0.06, -0.09), (0.06, -0.09)):
        mesh.box((x - 0.026, 0.0, z - 0.035), (x + 0.026, 0.07, z + 0.035))
    return mesh


PROPS = (
    # name, builder, colour, uv scale
    #
    # The colour here is the FBX material's diffuse, which the importer reads
    # and which is what the prop looks like *before* the scene points a `.rmat`
    # at it. It is set anyway, so a prop dragged into an empty scene is not
    # white.
    # **Four tree shapes, not one scaled three ways.** A forest of one
    # silhouette at three sizes reads as a texture; what makes a treeline look
    # like trees is that the *proportions* differ -- a spire beside a squat
    # broad one beside a middling one. The scene scales these as well, but the
    # scaling is on top of real variety rather than instead of it.
    ("pine", pine, (0.11, 0.30, 0.24), 0.4),
    ("pine_tall", lambda: pine(tiers=7, height=9.4, radius=1.35),
     (0.10, 0.27, 0.22), 0.4),
    ("pine_wide", lambda: pine(tiers=5, height=5.8, radius=1.95),
     (0.13, 0.33, 0.25), 0.4),
    ("pine_small", lambda: pine(tiers=5, height=4.6, radius=1.05),
     (0.13, 0.34, 0.27), 0.5),
    ("pine_trunk", pine_trunk, (0.30, 0.19, 0.13), 1.0),

    ("tent", tent, (0.78, 0.16, 0.18), 0.5),
    ("tent_band", tent_band, (0.92, 0.90, 0.87), 0.6),
    ("tent_door", tent_door, (0.05, 0.04, 0.05), 0.6),
    ("guy_line", guy_line, (0.55, 0.5, 0.42), 1.0),

    ("flame_outer", flame_outer, (1.0, 0.45, 0.08), 1.2),
    ("flame_inner", flame_inner, (1.0, 0.86, 0.32), 1.6),
    ("fire_logs", fire_logs, (0.24, 0.17, 0.13), 1.2),
    ("firewood", firewood, (0.42, 0.30, 0.21), 1.2),
    ("log_split", log_split, (0.46, 0.34, 0.24), 1.4),

    ("chair_frame", chair_frame, (0.10, 0.10, 0.12), 1.6),
    ("chair_seat", chair_seat, (0.22, 0.24, 0.44), 1.4),
    ("log", log, (0.38, 0.28, 0.20), 0.7),
    ("stump", stump, (0.44, 0.32, 0.22), 1.2),

    # Four stones and three tufts, for the same reason as the four trees. These
    # two are the props the scene places most -- several hundred between them --
    # so a single shape is the one that would be noticed.
    ("rock", rock, (0.36, 0.36, 0.40), 1.2),
    ("rock_flat", lambda: rock(radius=0.52, top=0.17, bottom=0.09, sides=6),
     (0.34, 0.34, 0.38), 1.2),
    ("rock_tall", lambda: rock(radius=0.28, top=0.54, bottom=0.12, sides=6),
     (0.37, 0.37, 0.41), 1.2),
    ("pebble", lambda: rock(radius=0.15, top=0.10, bottom=0.05, sides=6),
     (0.38, 0.38, 0.42), 2.0),

    ("grass", grass, (0.34, 0.66, 0.22), 1.6),
    ("grass_tall", lambda: grass(scale=1.3, twist=0.51, count=7),
     (0.31, 0.62, 0.20), 1.6),
    ("grass_low", lambda: grass(scale=0.8, lean_bias=0.34, twist=0.93),
     (0.37, 0.70, 0.25), 1.6),

    ("pack", pack, (0.24, 0.22, 0.26), 1.4),
    ("bedroll", bedroll, (0.68, 0.24, 0.22), 1.4),
    ("suitcase", suitcase, (0.44, 0.30, 0.19), 1.4),
    ("cooler", cooler, (0.78, 0.80, 0.82), 1.2),
    ("lantern", lantern, (0.16, 0.17, 0.20), 2.0),
    ("lantern_glass", lantern_glass, (1.0, 0.92, 0.68), 2.0),
    ("pot", pot, (0.14, 0.14, 0.16), 2.0),
    ("mug", mug, (0.82, 0.84, 0.86), 2.4),
    ("fork", fork, (0.2, 0.2, 0.22), 2.0),
    ("marshmallow", marshmallow, (0.96, 0.94, 0.9), 2.4),

    ("van_body", van_body, (0.82, 0.80, 0.74), 0.5),
    ("van_glass", van_glass, (0.09, 0.11, 0.14), 0.8),
    ("van_wheels", van_wheels, (0.08, 0.08, 0.09), 1.4),

    ("mirror_frame", mirror_frame, (0.48, 0.34, 0.22), 1.4),
    ("mirror_glass", mirror_glass, (0.92, 0.94, 0.96), 1.0),

    ("rabbit", rabbit, (0.42, 0.36, 0.32), 2.0),
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

        # The bounding box, printed because scale is the thing that goes wrong
        # most often and the number is right here.
        low = tuple(min(p[i] for p in mesh.points) for i in range(3))
        high = tuple(max(p[i] for p in mesh.points) for i in range(3))
        sys.stdout.write(
            '  {0:<14} {1:4d} verts {2:4d} faces   {3:.2f} x {4:.2f} x {5:.2f} m\n'
            .format(name, points, faces, high[0] - low[0], high[1] - low[1],
                    high[2] - low[2]))

    sys.stdout.write('{0} props, {1} faces in all, in {2}\n'.format(
        len(PROPS), total_faces, directory))


if __name__ == '__main__':
    main()
