#!/usr/bin/env python3
"""Model the Golden Gate Bridge and export it as FBX.

    python tools/scripts/make_bridge_models.py
    python tools/scripts/check_models.py

**Stage 1 is the silhouette, and this file is currently only that.** Two
towers, the cable parabola, the deck. No truss members, no railings, no
suspender ropes, no lamp standards -- those come once the shape is approved,
because the cable curve is the bridge's outline and nothing built on top of a
wrong one is worth keeping.

**Every number here is metres above Mean Higher High Water**, and the
elevations are the part that goes wrong. The commonly quoted "220 ft above the
water" is the *navigational clearance to the underside of the stiffening
truss*, not the roadway: the roadway is one truss-depth higher, at 246 ft /
75.0 m. Building to 67 m sinks the whole deck 7.6 m and every proportion above
it with it. The three numbers have to agree:

    746 ft tower  -  500 ft tower-above-roadway  =  246 ft roadway
    246 ft roadway  -  25 ft truss depth         =  221 ft clearance

**The 12.5 ft (3.81 m) module.** Railing posts sit at 12.5 ft, truss verticals
at 25, suspenders at 50, lamp posts at 150 -- 1, 2, 4 and 12 times one number.
Laying the bridge out from it is what makes every later element land on the
same grid instead of beating against it.
"""

import argparse
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import fbxwrite  # noqa: E402

Math_min = min
NEWLINE = chr(10)


class MaterialSplit:
    """One mesh per material, dispatched by the `material=` on every call.

    **A MeshComponent carries one material, and a model's own are never used.**
    The FBX importer reads them -- it reports three -- and `Scene.cpp` then
    resolves the *component's* handle and falls back to the renderer's default
    when it is unset, so an FBX material never reaches a draw. Measured: a
    model exported with a pure green material rendered bit-identically to one
    exported orange.

    The engine's idiom is a `.rmat` asset per entity, which is what every
    other scene in this project does. So the bridge is split by material at
    export: the builders below say what each face is made of, this routes it
    into that material's own mesh, and each becomes an entity pointing at its
    own asset.
    """

    def __init__(self):
        self.meshes = {}

    def mesh(self, material):
        return self.meshes.setdefault(material, fbxwrite.Mesh())

    def __getattr__(self, name):
        def call(*args, material=MAT_STEEL, **kwargs):
            return getattr(self.mesh(material), name)(*args, **kwargs)
        return call

    def absorb(self, other):
        """Take everything in `other`, material by material."""
        for material, part in other.meshes.items():
            into = self.mesh(material)
            base = len(into.points)
            first = len(into.faces)
            into.points.extend(part.points)
            for index, face in enumerate(part.faces):
                into.faces.append(tuple(base + i for i in face))
                into.uv.append(part.uv[index])
                into.smooth.append(part.smooth[index])
                into.material.append(0)
            for a, b in part.pieces:
                into.pieces.append((a + first, b + first))

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "SampleProject" / "assets" / "models" / "bridge"


# --- the bridge, in metres above MHHW ----------------------------------------

MODULE = 3.81               # 12.5 ft -- the module everything else is a multiple of

HALF_SPAN = 640.1           # 4200 ft between the towers
PYLON_X = 983.0             # tower + 1125 ft side span
HALF_LENGTH = 1368.5        # 2 737 m of bridge in all, approaches included
APPROACH_BENT = 45.72       # 150 ft, twelve modules -- the lamp-post spacing
ROADWAY = 75.0              # 246 ft, the driving surface
TRUSS_DEPTH = 7.62          # 25 ft, top chord to bottom chord
TRUSS_HALF = 13.715         # 90 ft between truss centres -- also the cable plane
                            # and the tower-leg centres. One number, three uses.

TOWER_TOP = 227.4           # 746 ft to the cable centreline at the saddle
PIER_TOP = 13.0             # the pier stands ~13 m out of the water
# Where each pier's concrete stops. **-z is Marin, +z is San Francisco**, and
# the two ends of this bridge are nothing like each other: the north pier is
# on the mainland at Lime Point, its foundation 20 ft down; the south pier is
# 1 100 ft out in the strait with its foundation 100 ft down, which is what
# the divers went to. `make_bridge_seabed.py` shapes the floor to meet both.
NORTH_PIER_BASE = -21.0     # the coast was pulled back off the bridge, so
                            # this pier stands in the strait and has to reach
SOUTH_PIER_BASE = -31.0     # 100 ft below the water, 335 m off Fort Point

# The cable. y'' = w/H is the same either side of a tower, so ONE curvature
# constant governs the main span and both side spans -- only the vertex moves.
# Fitting a separate, flatter curve to the side spans is the usual mistake.
SAG = 143.26                # 470 ft at mid-span
CURVE = SAG / (HALF_SPAN ** 2)          # 3.4966e-4 per metre
MAIN_LOW = TOWER_TOP - SAG              # 84.12 m, which is 30 ft over the road
SIDE_VERTEX_X = 1478.8
SIDE_VERTEX_Y = -18.60

CABLE_RADIUS = 0.462        # 36 3/8 in diameter

# --- the stiffening truss, which used to be a slab ---------------------------
# Verticals every 25 ft, which is two of the 12.5 ft module; the chords sit
# at the top and bottom of the 25 ft depth. Members are square sections, which
# is what riveted plate girders are.
TRUSS_PANEL = MODULE * 2.0          # 7.62 m between verticals
CHORD_HALF = 0.55                   # half the chord's square section
WEB_HALF = 0.34                     # verticals and diagonals
DECK_HALF_WIDTH = 12.50             # curb to curb plus both sidewalks
DECK_SLAB = 0.42                    # the roadway plate's thickness

# Suspenders every 50 ft, one pair a side; 70 mm rope, drawn at 110 so it
# survives a 1920-wide frame from half a kilometre.
SUSPENDER_SPACING = MODULE * 4.0    # 15.24 m
SUSPENDER_RADIUS = 0.11
SUSPENDER_PAIR = 0.55               # the two ropes of a station, either side

# Railings: outer 1.22 m, posts on the module itself.
RAIL_HEIGHT = 1.22
RAIL_POST = MODULE
RAIL_HALF_WIDTH = 12.62

# Lamp standards: 150 ft apart, opposite rather than staggered, mounted at
# 7.09 m with a 1.52 m arm.
LAMP_SPACING = MODULE * 12.0        # 45.72 m
LAMP_HEIGHT = 7.09
LAMP_ARM = 1.52

# The four pylons at the ends of the side spans, and the arch that carries
# the roadway over Fort Point.
PYLON_HALF = (7.4, 9.2)             # transverse, longitudinal, at the base
PYLON_TOP = 92.0
ARCH_CENTRE = 1030.0                # z, over the fort
ARCH_HALF_SPAN = 48.0
ARCH_SPRING = 9.0                   # where its feet stand

# The materials every face picks from. **International Orange is not
# decoration**: its reflectance peaks at 600 nm, right where a sodium lamp
# emits, and that match is why this bridge looks right at night and most
# things do not.
MAT_STEEL = 0
MAT_CONCRETE = 1
MAT_ROAD = 2
MAT_PAINT = 3
MATERIALS = [
    ("InternationalOrange", (0.527, 0.037, 0.025)),
    ("Concrete", (0.42, 0.41, 0.38)),
    ("Asphalt", (0.055, 0.055, 0.058)),
    ("LanePaint", (0.72, 0.71, 0.68)),
]

# The carriageway: six lanes between the curbs, a median down the middle and
# a sidewalk outside each curb. Every number is the real one, and they are
# what the markings are laid out from rather than eyeballed onto the slab.
CARRIAGEWAY_HALF = 9.45         # 18.90 m curb to curb
MEDIAN_HALF = 0.35
SIDEWALK = 3.05                 # each side, outside the curb
CURB_HEIGHT = 0.26
LANE_MARK_HALF = 0.075
DASH_LENGTH = 3.0
DASH_PERIOD = 12.0

# Leg plan: transverse (across the bridge) x longitudinal (along it). The long
# axis is along the bridge, which is what makes the tower read as a slab from
# the deck and as a tower from the side.
LEG_BASE = (10.06, 16.46)   # 33 x 54 ft over plates
LEG_TOP = (3.35, 7.62)      # 11 x 25 ft -- exactly 3 x 7 of the 3'-6" cells

# Strut bands, as (bottom, top) elevation above MHHW. The four above the
# roadway are the portals. **The openings get shorter going up** -- 111, 90, 75,
# 69 ft -- while the bands between them get thinner. That accelerating rhythm
# is what makes the tower soar; four equal openings read as a ladder.
STRUTS = [
    (13.4, 20.0),           # strut 7, on the pier
    (46.6, 53.0),           # strut 6
    (70.1, 75.0),           # strut 5, just under the deck
    (108.8, 119.2),         # strut 4  -- opening 1 below it is the tallest
    (146.6, 157.0),         # strut 3
    (179.9, 187.8),         # strut 2
    (208.8, 216.7),         # strut 1  -- shafts run free above it to the saddles
]


def cable_y(x):
    """Cable centreline elevation at `x` metres from mid-span."""
    x = abs(x)
    if x <= HALF_SPAN:
        return MAIN_LOW + CURVE * x * x
    return SIDE_VERTEX_Y + CURVE * (x - SIDE_VERTEX_X) ** 2


# --- pieces -------------------------------------------------------------------

def cables(segments=96, sides=10):
    """The two main cables, as a chain of smooth bars along the parabola.

    Sampled rather than swept: a chain of struts is what this toolkit can build
    and, at 96 segments over 640 m, each joint turns by a fraction of a degree.
    The smoothing then averages across the joints, so the chain reads as one
    continuous cable rather than as sausages.

    **The slope breaks at the tower and must be left broken.** The cable is not
    tangent-continuous over the saddle -- it kinks -- so the main span and the
    side spans are sampled as separate runs and never blended across the join.
    """
    mesh = MaterialSplit()

    for side in (-TRUSS_HALF, TRUSS_HALF):
        for x0, x1 in ((-HALF_SPAN, HALF_SPAN),
                       (-PYLON_X, -HALF_SPAN),
                       (HALF_SPAN, PYLON_X)):
            count = max(4, int(segments * (x1 - x0) / (2 * HALF_SPAN)))
            previous = None
            for i in range(count + 1):
                x = x0 + (x1 - x0) * i / count
                point = (side, cable_y(x), x)
                if previous is not None:
                    mesh.strut(previous, point, CABLE_RADIUS, sides=sides,
                               uv=1.0, smooth=True, material=MAT_STEEL)
                previous = point

    suspenders(mesh)
    return mesh


def suspenders(mesh):
    """The vertical ropes, every 50 ft, cable down to the truss.

    **The single most characteristic thing about this bridge and it was not
    modelled at all.** Two towers and a curve are half a dozen suspension
    bridges; what makes a photograph of this one unmistakable is the comb of
    ropes hanging off the parabola, closing to nothing at mid-span and
    fanning out to the towers. Without them the cable reads as a wire and the
    deck reads as floating.

    Four rope legs at each station in the real structure -- two doubled
    pairs -- which is a pair to either side of the cable plane here. Drawn at
    110 mm rather than the true 70: at half a kilometre the true diameter is
    a third of a pixel, and a rope that vanishes is worse than one a hair
    thick.
    """
    top = ROADWAY - CHORD_HALF
    stations = int(PYLON_X / SUSPENDER_SPACING)
    for side in (-TRUSS_HALF, TRUSS_HALF):
        for i in range(-stations, stations + 1):
            z = i * SUSPENDER_SPACING
            # Nothing hangs from a saddle, and nothing hangs where the cable
            # has already come down to the deck.
            if abs(abs(z) - HALF_SPAN) < SUSPENDER_SPACING * 0.5:
                continue
            anchor = cable_y(z)
            if anchor - top < 2.0:
                continue
            for offset in (-SUSPENDER_PAIR, SUSPENDER_PAIR):
                mesh.strut((side, anchor, z + offset),
                           (side, top, z + offset),
                           SUSPENDER_RADIUS, sides=4, uv=1.0,
                           material=MAT_STEEL)


def deck():
    """The stiffening truss, the roadway it carries, and everything on it.

    **It was a solid slab, and a slab is the one thing this truss is not.**
    From every camera that matters you see *through* the Golden Gate's deck:
    the chords, the verticals on the 25 ft module and the diagonals between
    them are as much of its silhouette as the cable curve is, and painting a
    box orange does not get there. So: two side trusses of real members, the
    lateral bracing between them that the pier close-up looks straight up
    into, the roadway plate on top, the railings, and the 128 lamp standards.

    Cheap, because a member is a four-sided strut: the whole truss is about
    thirty thousand triangles, against the two million the terrain draws.
    """
    mesh = MaterialSplit()

    top = ROADWAY - CHORD_HALF
    bottom = ROADWAY - TRUSS_DEPTH + CHORD_HALF

    # --- the roadway ---------------------------------------------------------
    #
    # Asphalt, and its own material: the deck is the one dark thing in a
    # bridge that is otherwise all one colour, and at dusk it is what
    # separates the near truss from the far one.
    mesh.box((-CARRIAGEWAY_HALF, ROADWAY - DECK_SLAB, -HALF_LENGTH),
             (CARRIAGEWAY_HALF, ROADWAY, HALF_LENGTH), uv=8.0, material=MAT_ROAD)

    # The sidewalks outside the curbs, raised, and the curb itself. **The
    # deck is not one flat plane**: from the deck camera and from the tower
    # window the step from carriageway to walkway is the first thing that
    # says how wide the road is.
    for side in (-1.0, 1.0):
        inner = side * CARRIAGEWAY_HALF
        outer = side * DECK_HALF_WIDTH
        lo, hi = min(inner, outer), max(inner, outer)
        mesh.box((lo, ROADWAY - DECK_SLAB, -HALF_LENGTH),
                 (hi, ROADWAY + CURB_HEIGHT, HALF_LENGTH),
                 uv=2.4, material=MAT_CONCRETE)

    # The median. Six lanes with nothing between them is a motorway, not this.
    mesh.box((-MEDIAN_HALF, ROADWAY, -HALF_LENGTH),
             (MEDIAN_HALF, ROADWAY + 0.82, HALF_LENGTH),
             uv=2.4, material=MAT_CONCRETE)

    # --- the markings --------------------------------------------------------
    #
    # Three lanes a side between the median and the curb, so the boundaries
    # fall where the arithmetic puts them rather than where they look right.
    lane = (CARRIAGEWAY_HALF - MEDIAN_HALF) / 3.0
    paint_y = ROADWAY + 0.012
    for side in (-1.0, 1.0):
        for k in (1, 2):
            x = side * (MEDIAN_HALF + lane * k)
            count = int(2.0 * HALF_LENGTH / DASH_PERIOD)
            for i in range(count):
                z = -HALF_LENGTH + i * DASH_PERIOD
                mesh.box((x - LANE_MARK_HALF, ROADWAY, z),
                         (x + LANE_MARK_HALF, paint_y, z + DASH_LENGTH),
                         uv=1.0, material=MAT_PAINT)
        # The solid edge line, one box for the whole run.
        edge = side * (CARRIAGEWAY_HALF - 0.28)
        mesh.box((edge - LANE_MARK_HALF, ROADWAY, -HALF_LENGTH),
                 (edge + LANE_MARK_HALF, paint_y, HALF_LENGTH),
                 uv=8.0, material=MAT_PAINT)

    # --- the two side trusses ------------------------------------------------
    panels = int(round(2.0 * HALF_LENGTH / TRUSS_PANEL))
    for side in (-TRUSS_HALF, TRUSS_HALF):
        # The chords, one member each rather than one per panel: they are
        # continuous in the real truss and a chain of boxes would show its
        # joints along the most-looked-at line on the bridge.
        for y in (top, bottom):
            mesh.box((side - CHORD_HALF, y - CHORD_HALF, -HALF_LENGTH),
                     (side + CHORD_HALF, y + CHORD_HALF, HALF_LENGTH),
                     uv=6.0, material=MAT_STEEL)

        for i in range(panels + 1):
            z = -HALF_LENGTH + i * TRUSS_PANEL
            if z > HALF_LENGTH:
                break
            mesh.box((side - WEB_HALF, bottom, z - WEB_HALF),
                     (side + WEB_HALF, top, z + WEB_HALF),
                     uv=3.0, material=MAT_STEEL)

            # **Warren, so the diagonals alternate.** A truss whose diagonals
            # all lean the same way is a Pratt, and it reads as a fence; the
            # zig-zag is what the eye picks up from a kilometre away.
            if i >= panels:
                continue
            z1 = z + TRUSS_PANEL
            if i % 2 == 0:
                a, b = (side, bottom, z), (side, top, z1)
            else:
                a, b = (side, top, z), (side, bottom, z1)
            mesh.strut(a, b, WEB_HALF * 0.85, sides=4, uv=3.0,
                       material=MAT_STEEL)

    # --- across, between the two trusses -------------------------------------
    #
    # The floor beams every panel and a cross-brace every other one. Seen only
    # from underneath -- which is exactly where the pier camera stands.
    for i in range(panels + 1):
        z = -HALF_LENGTH + i * TRUSS_PANEL
        if z > HALF_LENGTH:
            break
        mesh.box((-TRUSS_HALF, bottom - CHORD_HALF * 0.6, z - WEB_HALF * 0.8),
                 (TRUSS_HALF, bottom + CHORD_HALF * 0.6, z + WEB_HALF * 0.8),
                 uv=4.0, material=MAT_STEEL)
        if i % 2 == 0 and i + 2 <= panels:
            z1 = z + TRUSS_PANEL * 2.0
            y = bottom
            mesh.strut((-TRUSS_HALF, y, z), (TRUSS_HALF, y, z1),
                       WEB_HALF * 0.6, sides=4, uv=3.0, material=MAT_STEEL)
            mesh.strut((TRUSS_HALF, y, z), (-TRUSS_HALF, y, z1),
                       WEB_HALF * 0.6, sides=4, uv=3.0, material=MAT_STEEL)

    # --- the railings --------------------------------------------------------
    #
    # **Pickets, not a rail on posts.** The outer railing is a close comb of
    # verticals with a rail top and bottom, and from the sidewalk it is most
    # of what you see. At 1.5 m the comb reads without putting a picket in
    # every pixel of a distant shot -- the true spacing is about five inches,
    # which would be twenty thousand of them a side and nothing but aliasing
    # from any camera that is not standing on the deck.
    base = ROADWAY + CURB_HEIGHT
    pickets = int(2.0 * HALF_LENGTH / 1.5)
    posts = int(round(2.0 * HALF_LENGTH / RAIL_POST))
    for side in (-RAIL_HALF_WIDTH, RAIL_HALF_WIDTH):
        for y in (base + RAIL_HEIGHT - 0.14, base + RAIL_HEIGHT * 0.52):
            mesh.box((side - 0.08, y, -HALF_LENGTH),
                     (side + 0.08, y + 0.14, HALF_LENGTH),
                     uv=6.0, material=MAT_STEEL)
        for i in range(pickets + 1):
            z = -HALF_LENGTH + i * 1.5
            if z > HALF_LENGTH:
                break
            mesh.box((side - 0.035, base, z - 0.035),
                     (side + 0.035, base + RAIL_HEIGHT, z + 0.035),
                     uv=1.0, material=MAT_STEEL)
        # The heavier posts the comb hangs between, on the 12.5 ft module.
        for i in range(posts + 1):
            z = -HALF_LENGTH + i * RAIL_POST
            if z > HALF_LENGTH:
                break
            mesh.box((side - 0.09, base, z - 0.09),
                     (side + 0.09, base + RAIL_HEIGHT + 0.1, z + 0.09),
                     uv=1.0, material=MAT_STEEL)

    # The service pipe along the outside of each truss, which is the one
    # horizontal line on this bridge that is not structure.
    for side in (-1.0, 1.0):
        x = side * (TRUSS_HALF + 0.55)
        y = ROADWAY - 1.35
        mesh.strut((x, y, -HALF_LENGTH), (x, y, HALF_LENGTH), 0.42, sides=8,
                   uv=8.0, smooth=True, material=MAT_STEEL)
        # And the brackets that carry it off the truss, every other panel.
        brackets = int(2.0 * HALF_LENGTH / (TRUSS_PANEL * 2.0))
        for i in range(brackets + 1):
            z = -HALF_LENGTH + i * TRUSS_PANEL * 2.0
            if z > HALF_LENGTH:
                break
            mesh.strut((side * TRUSS_HALF, y + 0.55, z), (x, y, z),
                       0.10, sides=4, uv=1.0, material=MAT_STEEL)

    # --- the lamp standards --------------------------------------------------
    #
    # Opposite, not staggered: 128 of them at 150 ft, and the pairing is
    # visible down the whole roadway in every photograph of it.
    lamps = int(round(2.0 * HALF_LENGTH / LAMP_SPACING))
    for i in range(lamps + 1):
        z = -HALF_LENGTH + i * LAMP_SPACING
        if z > HALF_LENGTH:
            break
        for side in (-1.0, 1.0):
            x = side * (RAIL_HALF_WIDTH - 0.55)
            base = ROADWAY + CURB_HEIGHT
            # `cylinder` has no uv of its own -- it is the one primitive
            # here whose `uv` would land in `add`'s per-face list.
            mesh.cylinder(0.13, LAMP_HEIGHT, sides=6, taper=0.7,
                          base_y=base, offset=(x, 0.0, z),
                          material=MAT_STEEL)
            # **The arm curves.** A straight bracket reads as a sign post;
            # what says street lamp is the quarter circle from the shaft out
            # over the road, and three segments is enough to draw it.
            head = base + LAMP_HEIGHT
            curve = [(x, head - 0.35, z),
                     (x, head + 0.42, z),
                     (x - side * LAMP_ARM * 0.55, head + 0.78, z),
                     (x - side * LAMP_ARM, head + 0.80, z)]
            for a, b in zip(curve, curve[1:]):
                mesh.strut(a, b, 0.085, sides=6, uv=1.0, smooth=True,
                           material=MAT_STEEL)
            mesh.box((x - side * LAMP_ARM - 0.36, head + 0.56, z - 0.24),
                     (x - side * LAMP_ARM + 0.36, head + 0.80, z + 0.24),
                     uv=1.0, material=MAT_STEEL)

    # --- the approach bents --------------------------------------------------
    #
    # Concrete, and only where the deck is over land: over the water it is
    # suspended, and a column under the main span would be a different bridge.
    for sign in (-1.0, 1.0):
        z = sign * PYLON_X
        while abs(z) < HALF_LENGTH - 20.0:
            z += sign * APPROACH_BENT
            ground = ground_at(0.0, z)
            if ground is None or ground > ROADWAY - TRUSS_DEPTH - 3.0:
                continue
            for x in (-TRUSS_HALF * 0.55, TRUSS_HALF * 0.55):
                mesh.box((x - 1.6, ground - 4.0, z - 1.6),
                         (x + 1.6, ROADWAY - TRUSS_DEPTH, z + 1.6),
                         uv=2.0, material=MAT_CONCRETE)
    return mesh


def ground_at(x, z):
    """The seabed generator's land height, or None when it cannot be asked.

    Imported here rather than at the top because that module imports this one
    for the bridge's dimensions, and a cycle at import time would be a
    circular-import failure rather than a missing column.
    """
    try:
        import make_bridge_seabed
    except Exception:
        return None
    return make_bridge_seabed.height_at(x, z)


# --- fluted, chamfered sections ----------------------------------------------
#
# **The tower's shafts are not boxes and the difference is the whole
# building.** Every photograph of the Golden Gate's towers shows deep vertical
# reveals running the full height of each leg, corners cut back rather than
# square, and a projecting ledge at every setback. Drawn as tapered boxes they
# read as a pylon from a motorway bridge; drawn with the plan below they read
# as this one, and from any distance.
#
# So the leg is an extrusion of a *plan*, not a box: a rectangle with its
# corners chamfered and a row of reveals cut into each face. Both ends of a
# section use the same plan at different sizes, so the flutes taper with the
# shaft and stay in line all the way up.

def fluted_edge(start, end, inward, count, depth):
    """Points from `start` towards `end`, with `count` reveals cut into the
    face. `inward` points into the section. `start` is included, `end` is not:
    the next edge begins there."""
    (ax, az), (bx, bz) = start, end
    ix, iz = inward
    points = [(ax, az)]
    if count <= 0:
        return points

    # Each reveal is a quarter of its slot wide, centred in it, so the flat
    # between two of them is the same width as the reveal itself.
    for i in range(count):
        t0 = (i + 0.30) / count
        t1 = (i + 0.70) / count
        for t, sunk in ((t0, False), (t0, True), (t1, True), (t1, False)):
            x = ax + (bx - ax) * t
            z = az + (bz - az) * t
            if sunk:
                x += ix * depth
                z += iz * depth
            points.append((x, z))
    return points


def leg_plan(half_x, half_z, chamfer_frac=0.22, flutes=(3, 5), depth_frac=0.11):
    """The leg's plan, counter-clockwise in the order `prism` uses.

    `flutes` is (across the bridge, along it): the faces you see from the deck
    are the narrow ones and carry fewer.
    """
    c = min(half_x, half_z) * chamfer_frac
    dx = half_x * depth_frac
    dz = half_z * depth_frac
    plan = []
    plan += fluted_edge((-half_x + c, -half_z), (half_x - c, -half_z),
                        (0.0, 1.0), flutes[0], dz)
    plan += fluted_edge((half_x, -half_z + c), (half_x, half_z - c),
                        (-1.0, 0.0), flutes[1], dx)
    plan += fluted_edge((half_x - c, half_z), (-half_x + c, half_z),
                        (0.0, -1.0), flutes[0], dz)
    plan += fluted_edge((-half_x, half_z - c), (-half_x, -half_z + c),
                        (1.0, 0.0), flutes[1], dx)
    return plan


def extrude_plan(mesh, plan_low, plan_high, y0, y1, offset, material,
                 cap_low=True, cap_high=True):
    """A plan swept from y0 to y1, tapering from `plan_low` to `plan_high`.

    Wound exactly as `prism` winds: the sides are (bottom i, top i, top i+1,
    bottom i+1) and the caps are fans, the lower one in plan order and the
    upper one reversed. A face wound the other way is not merely invisible
    under backface culling -- it is lit from behind.

    The caps fan from an added centre point rather than from a corner,
    because a fluted plan is concave and a corner fan would cut triangles
    across the reveals.
    """
    n = len(plan_low)
    points = [(x, y0, z) for x, z in plan_low]
    points += [(x, y1, z) for x, z in plan_high]
    faces = [(i, n + i, n + (i + 1) % n, (i + 1) % n) for i in range(n)]

    if cap_low:
        centre = len(points)
        points.append((0.0, y0, 0.0))
        faces += [(centre, (i + 1) % n, i) for i in range(n)]
    if cap_high:
        centre = len(points)
        points.append((0.0, y1, 0.0))
        faces += [(centre, n + i, n + (i + 1) % n) for i in range(n)]

    return mesh.add(points, faces, offset=offset, material=material)


def coffered_band(mesh, x0, x1, y0, y1, z, depth, fins, material):
    """A strut face with a row of vertical fins across it.

    The horizontal bands between the tower's openings are not flat plate:
    they are coffered, and the stripe of light and shade that makes is what
    separates one band from the next at a kilometre.
    """
    span = x1 - x0
    for i in range(fins):
        t0 = x0 + span * (i + 0.22) / fins
        t1 = x0 + span * (i + 0.78) / fins
        for face in (-1.0, 1.0):
            mesh.box((t0, y0 + 0.45, z + face * depth * 0.5),
                     (t1, y1 - 0.45, z + face * (depth * 0.5 + 0.34)),
                     uv=1.0, material=material)


def leg_half(elevation):
    """Half the leg's plan at a height, as (transverse, longitudinal).

    **By elevation and never by index.** The first version tapered the shafts
    across the *list of levels* -- one eighth of the taper per section -- while
    the struts sized themselves from their height above the pier. The two
    disagree everywhere: eight sections spread over 214 m are nowhere near
    evenly spaced, so a strut at 20 m was cut for a leg width the leg only
    reaches at 40. That is what left the bars standing proud of the shafts at
    the bottom and shy of them at the top, and it read as sloppy modelling
    rather than as two functions answering the same question differently.

    One function, asked by both, and the disagreement cannot come back.
    """
    span = max(TOWER_TOP - PIER_TOP, 1e-6)
    t = min(max((elevation - PIER_TOP) / span, 0.0), 1.0)
    return (0.5 * (LEG_BASE[0] + (LEG_TOP[0] - LEG_BASE[0]) * t),
            0.5 * (LEG_BASE[1] + (LEG_TOP[1] - LEG_BASE[1]) * t))


def tower(z, pier_base):
    """One tower: two stepped shafts and the seven struts between them.

    `pier_base` is where the concrete stops, in metres relative to the water.
    """
    mesh = MaterialSplit()

    # The piers the shafts stand on, carried below the waterline.
    #
    # **They have to continue under the water, not stop at it.** A pier that
    # ends exactly at y=0 is a tower balanced on a mirror; what says the water
    # is water is seeing the concrete carry on into it.
    #
    # **And the two do not stop at the same depth**, which is the single
    # biggest asymmetry in the real bridge and was flat before the seabed
    # existed to show it. The south pier stands 1 100 ft out in the strait on
    # a foundation 100 ft below the water; the north pier is on the mainland
    # at Lime Point with its foundation 20 ft down. A shared -9 m left the
    # south one hanging over a 30 m hole the moment the floor arrived.
    for x in (-TRUSS_HALF, TRUSS_HALF):
        mesh.box((x - LEG_BASE[0] * 0.62, pier_base, z - LEG_BASE[1] * 0.62),
                 (x + LEG_BASE[0] * 0.62, PIER_TOP, z + LEG_BASE[1] * 0.62),
                 uv=3.0, material=MAT_CONCRETE)

    # The shafts, one section per strut band, narrowing at each. A single
    # smooth taper from base to top is the wrong shape: the real tower steps,
    # and the steps are the Art Deco.
    levels = [PIER_TOP] + [top for _, top in STRUTS] + [TOWER_TOP]
    for x in (-TRUSS_HALF, TRUSS_HALF):
        for i in range(len(levels) - 1):
            low, high = levels[i], levels[i + 1]
            extrude_plan(mesh, leg_plan(*leg_half(low)),
                         leg_plan(*leg_half(high)), low, high,
                         (x, 0.0, z), MAT_STEEL)

            # The cornice at the setback: a short flared band that catches
            # the light and says the shaft has just stepped in. Without them
            # the steps read as a modelling seam rather than as the Art Deco
            # they are.
            if i + 1 < len(levels) - 1:
                wide = [(px * 1.055, pz * 1.055)
                        for px, pz in leg_plan(*leg_half(high))]
                extrude_plan(mesh, wide, wide, high - 1.15, high,
                             (x, 0.0, z), MAT_STEEL)

    # The struts, spanning between the shafts.
    #
    # **The first one starts at the pier, not at its surveyed elevation.** The
    # schedule puts strut 7 at 13.4 m and the pier top at 13.0, and those four
    # hundred millimetres are a slot of daylight straight through the tower at
    # its widest, most-looked-at point. The real structure has no such gap: the
    # base member sits on the pier. Where a surveyed number and a closed
    # structure disagree at this scale, the structure wins.
    for index, (low, high) in enumerate(STRUTS):
        bottom = PIER_TOP if index == 0 else low

        # Sized from the leg at its own height, and a hair narrower, so the
        # bar dies into the shaft instead of standing proud of it -- which is
        # what a coplanar face looks like once the two are lit differently.
        depth = leg_half(high)[1] * 1.94

        # **The roadway passes between the legs, so a strut at deck level
        # cannot cross it.** Strut 5 is surveyed at 70.1-75.0 m and the deck
        # occupies 67.4-75.0: drawn full width it comes up through the
        # carriageway, which is what it did -- a red block lying across three
        # lanes. Where a band overlaps the deck it is drawn only outboard of
        # the curb, tying each leg to the truss and leaving the road clear.
        crosses_deck = (high > ROADWAY - TRUSS_DEPTH
                        and bottom < ROADWAY + 0.5)
        if crosses_deck:
            for side in (-1.0, 1.0):
                inner = side * CARRIAGEWAY_HALF
                outer = side * TRUSS_HALF
                mesh.box((min(inner, outer), bottom, z - depth * 0.5),
                         (max(inner, outer), high, z + depth * 0.5),
                         uv=2.0, material=MAT_STEEL)
        else:
            mesh.box((-TRUSS_HALF, bottom, z - depth * 0.5),
                     (TRUSS_HALF, high, z + depth * 0.5), uv=3.0,
                     material=MAT_STEEL)

        # The band's face, coffered, and the ledges that cap it top and
        # bottom. Only on the deep bands -- a 4 m strut has no room for it.
        if high - bottom > 5.0 and not crosses_deck:
            # **Inside the gap between the legs, not 0.9 of the way into
            # one.** The fins ran to `TRUSS_HALF - 0.9 * leg_half`, which is
            # half a metre *past* the leg's inner face, so every band pushed
            # its coffering through the shaft either side of it.
            widest = max(leg_half(bottom)[0], leg_half(high)[0])
            inner = TRUSS_HALF - widest - 0.40
            coffered_band(mesh, -inner, inner, bottom, high, z, depth,
                          max(6, int((2.0 * inner) / 1.6)), MAT_STEEL)
            for y in (bottom, high):
                mesh.box((-TRUSS_HALF - 0.35, y - 0.55, z - depth * 0.5 - 0.5),
                         (TRUSS_HALF + 0.35, y + 0.55, z + depth * 0.5 + 0.5),
                         uv=2.0, material=MAT_STEEL)

    # --- the top ------------------------------------------------------------
    #
    # The cable does not stop at the shaft: it runs over a saddle in a housing
    # that stands proud of the tower top, and there is a railed platform round
    # it. Both are in every photograph taken from the deck looking up.
    for x in (-TRUSS_HALF, TRUSS_HALF):
        top_half = leg_half(TOWER_TOP)
        mesh.box((x - top_half[0] * 1.12, TOWER_TOP - 1.0,
                  z - top_half[1] * 1.12),
                 (x + top_half[0] * 1.12, TOWER_TOP + 0.6,
                  z + top_half[1] * 1.12), uv=2.0, material=MAT_STEEL)
        mesh.box((x - CABLE_RADIUS * 2.4, TOWER_TOP + 0.6, z - top_half[1] * 0.8),
                 (x + CABLE_RADIUS * 2.4, TOWER_TOP + 2.1, z + top_half[1] * 0.8),
                 uv=1.0, material=MAT_STEEL)
        for post in range(8):
            t = -1.0 + 2.0 * post / 7.0
            pz = z + t * top_half[1] * 1.05
            mesh.box((x - top_half[0] * 1.12, TOWER_TOP + 0.6, pz - 0.09),
                     (x - top_half[0] * 0.95, TOWER_TOP + 1.7, pz + 0.09),
                     uv=1.0, material=MAT_STEEL)
            mesh.box((x + top_half[0] * 0.95, TOWER_TOP + 0.6, pz - 0.09),
                     (x + top_half[0] * 1.12, TOWER_TOP + 1.7, pz + 0.09),
                     uv=1.0, material=MAT_STEEL)

    # --- the corner gussets, which are the "arch" ---------------------------
    #
    # **The openings are rectangular.** Every photograph of this tower looks
    # arched and none of it is: what curves the corner is a bracket plate
    # across it, and four of them per opening is the whole effect. Modelling
    # the opening as an arch instead gets the silhouette wrong in a way that
    # is hard to name and impossible to miss.
    for index in range(len(STRUTS) - 1):
        bottom = STRUTS[index][1]
        top = STRUTS[index + 1][0]
        if top - bottom < 6.0:
            continue
        # Nothing decorates the opening the road runs through.
        if bottom < ROADWAY + 0.5 and top > ROADWAY - TRUSS_DEPTH:
            continue
        gusset = Math_min(3.4, (top - bottom) * 0.24)
        depth = leg_half(bottom)[1] * 1.7
        for x in (-TRUSS_HALF, TRUSS_HALF):
            inner = x - (1.0 if x > 0 else -1.0) * leg_half(bottom)[0]
            step = -gusset if x > 0 else gusset
            # Lower corner, then upper: wound counter-clockwise seen from +z.
            mesh.panel([(inner, bottom, z - depth * 0.5),
                        (inner + step, bottom, z - depth * 0.5),
                        (inner, bottom + gusset, z - depth * 0.5)],
                       depth, material=MAT_STEEL)
            mesh.panel([(inner, top - gusset, z - depth * 0.5),
                        (inner + step, top, z - depth * 0.5),
                        (inner, top, z - depth * 0.5)],
                       depth, material=MAT_STEEL)

    # --- the cross bracing under the roadway --------------------------------
    #
    # Two crossed diagonals in each opening below the deck, which is the
    # detail the pier close-up is looking straight at.
    for index in range(len(STRUTS) - 1):
        bottom = STRUTS[index][1]
        top = STRUTS[index + 1][0]
        if top > ROADWAY - TRUSS_DEPTH or top - bottom < 10.0:
            continue
        left = -TRUSS_HALF + leg_half(bottom)[0] * 0.5
        right = TRUSS_HALF - leg_half(bottom)[0] * 0.5
        mesh.strut((left, bottom, z), (right, top, z), 0.62, sides=4,
                   uv=2.0, material=MAT_STEEL)
        mesh.strut((right, bottom, z), (left, top, z), 0.62, sides=4,
                   uv=2.0, material=MAT_STEEL)

    return mesh


def pylons(mesh):
    """The four concrete pylons at the ends of the side spans.

    The roadway runs *between* each pair, and they are the only thing on the
    bridge taller than the deck that is not a tower -- which is what makes
    the side spans read as side spans rather than as more approach.
    """
    for z in (-PYLON_X, PYLON_X):
        ground = ground_at(0.0, z)
        foot = (ground - 8.0) if ground is not None else -6.0
        for x in (-(TRUSS_HALF + PYLON_HALF[0] + 1.2),
                  TRUSS_HALF + PYLON_HALF[0] + 1.2):
            # **Stepped, not a slab.** These are concrete towers that batter
            # inwards as they rise, and drawn as one extruded rectangle a
            # pylon reads as a blank wall standing beside the bridge -- which
            # is what it did. Four stages, each a little smaller than the one
            # under it, with the setback showing.
            stages = ((0.00, 1.00), (0.42, 0.86), (0.70, 0.74), (0.90, 0.66))
            for k, (start, scale) in enumerate(stages):
                low = foot + (PYLON_TOP - foot) * start
                high = (foot + (PYLON_TOP - foot) * stages[k + 1][0]
                        if k + 1 < len(stages) else PYLON_TOP)
                mesh.box((x - PYLON_HALF[0] * scale, low,
                          z - PYLON_HALF[1] * scale),
                         (x + PYLON_HALF[0] * scale, high,
                          z + PYLON_HALF[1] * scale),
                         uv=2.4, material=MAT_CONCRETE)
                # The cornice at each setback.
                if k + 1 < len(stages):
                    mesh.box((x - PYLON_HALF[0] * scale * 1.06, high - 1.1,
                              z - PYLON_HALF[1] * scale * 1.06),
                             (x + PYLON_HALF[0] * scale * 1.06, high,
                              z + PYLON_HALF[1] * scale * 1.06),
                             uv=2.0, material=MAT_CONCRETE)

        # The portal beam over the road, which is what you drive under.
        mesh.box((-(TRUSS_HALF + PYLON_HALF[0] * 2.0), ROADWAY + 12.0,
                  z - PYLON_HALF[1] * 0.62),
                 (TRUSS_HALF + PYLON_HALF[0] * 2.0, PYLON_TOP * 0.94,
                  z + PYLON_HALF[1] * 0.62),
                 uv=4.0, material=MAT_CONCRETE)


def fort_point_arch(mesh, segments=30):
    """The steel arch that carries the roadway over Fort Point, and the fort.

    **The one piece of this bridge that is not a suspension bridge, and it is
    on one side only.** Strauss would not demolish the 1861 fort, so the south
    approach steps over it on a 320 ft arch. It is the first thing in frame
    from the sea wall -- which is where half the photographs of the Golden
    Gate are taken from -- and it is the asymmetry that tells you which end of
    the bridge you are looking at.

    Two ribs, each a chord of paired chains rather than one bar, laced between
    like the real riveted box it is; spandrel columns at every panel; cross
    bracing between the ribs; and a springing block at each foot, because an
    arch that arrives at the ground as a thin tube reads as a wire.
    """
    crown = ROADWAY - TRUSS_DEPTH - 1.2
    rise = crown - ARCH_SPRING

    def arc(t):
        return (ARCH_CENTRE + t * ARCH_HALF_SPAN,
                ARCH_SPRING + rise * (1.0 - t * t))

    # The fort. Low, square and massive: a masonry casemate with its parade
    # wall, sitting inside the arch's span so the arch clears it -- which is
    # the whole reason the arch is there.
    #
    # **Stood on the ground it is actually on.** Built from y = 0 it was
    # buried to its roof in the Fort Point terrace, which rises to about
    # eighteen metres there: a fort you cannot see is not a reason for an
    # arch.
    ground = ground_at(0.0, ARCH_CENTRE)
    base = (ground - 1.5) if ground is not None else 0.0
    mesh.box((-27.0, base, ARCH_CENTRE - 26.0),
             (27.0, base + 11.5, ARCH_CENTRE + 26.0),
             uv=2.4, material=MAT_CONCRETE)
    mesh.box((-29.0, base + 11.5, ARCH_CENTRE - 28.0),
             (29.0, base + 13.4, ARCH_CENTRE + 28.0),
             uv=2.4, material=MAT_CONCRETE)

    for side in (-TRUSS_HALF, TRUSS_HALF):
        # The rib: two chords a metre apart with lacing between, which is what
        # a built-up arch member is. One tube would be a pipe.
        for chord in (-0.85, 0.85):
            previous = None
            for i in range(segments + 1):
                t = -1.0 + 2.0 * i / segments
                z, y = arc(t)
                point = (side + chord, y, z)
                if previous is not None:
                    mesh.strut(previous, point, 0.42, sides=6, uv=2.0,
                               smooth=True, material=MAT_STEEL)
                previous = point

        for i in range(segments):
            t0 = -1.0 + 2.0 * i / segments
            t1 = -1.0 + 2.0 * (i + 1) / segments
            z0, y0 = arc(t0)
            z1, y1 = arc(t1)
            # The lacing, alternating, between the rib's two chords.
            a = (side - 0.85, y0, z0) if i % 2 == 0 else (side + 0.85, y0, z0)
            b = (side + 0.85, y1, z1) if i % 2 == 0 else (side - 0.85, y1, z1)
            mesh.strut(a, b, 0.16, sides=4, uv=1.0, material=MAT_STEEL)

            # The spandrel column up to the truss, at every panel.
            if 0 < i < segments:
                mesh.box((side - 0.40, y0, z0 - 0.40),
                         (side + 0.40, ROADWAY - TRUSS_DEPTH, z0 + 0.40),
                         uv=2.0, material=MAT_STEEL)

        # The springing block: the arch does not meet the ground, it lands on
        # a concrete skewback.
        z0, y0 = arc(-1.0)
        z1, y1 = arc(1.0)
        for zf in (z0, z1):
            mesh.box((side - 2.2, 0.0, zf - 3.0),
                     (side + 2.2, ARCH_SPRING + 1.5, zf + 3.0),
                     uv=3.0, material=MAT_CONCRETE)

    # And the bracing between the two ribs, which is what stops them being
    # two arches standing next to each other.
    for i in range(2, segments - 1, 3):
        t0 = -1.0 + 2.0 * i / segments
        t1 = -1.0 + 2.0 * (i + 1) / segments
        z0, y0 = arc(t0)
        z1, y1 = arc(t1)
        mesh.strut((-TRUSS_HALF, y0, z0), (TRUSS_HALF, y1, z1), 0.20,
                   sides=4, uv=1.0, material=MAT_STEEL)
        mesh.strut((TRUSS_HALF, y0, z0), (-TRUSS_HALF, y1, z1), 0.20,
                   sides=4, uv=1.0, material=MAT_STEEL)


def towers():
    mesh = MaterialSplit()
    pylons(mesh)
    fort_point_arch(mesh)
    for z, pier_base in ((-HALF_SPAN, NORTH_PIER_BASE),
                         (HALF_SPAN, SOUTH_PIER_BASE)):
        mesh.absorb(tower(z, pier_base))
    return mesh


# The sea is not here. It was a 4.8 km box in the first draft of this file and
# is now a WaterComponent: a rectangle whose length and breadth are authored on
# the entity, so it is dragged to size in the inspector rather than regenerated
# and reimported. Renderer/Water builds its grid.


# --- driver -------------------------------------------------------------------

# **What each file is, and why there are five.** The three that existed carry
# the steel; the roadway and the concrete are their own, because a material is
# an entity here and a MeshComponent holds exactly one.
PARTS = [
    ("bridge_towers", towers, MAT_STEEL),
    ("bridge_deck", deck, MAT_STEEL),
    ("bridge_cables", cables, MAT_STEEL),
]
SHARED = [
    ("bridge_road", MAT_ROAD),
    ("bridge_piers", MAT_CONCRETE),
    ("bridge_paint", MAT_PAINT),
]


def split_of(material, mesh):
    """A one-material split holding an existing mesh, so `absorb` can take it."""
    split = MaterialSplit()
    split.meshes[material] = mesh
    return split


def fnv1a(data):
    """The registry's SourceHash, so a checkout does not resave the asset."""
    h = 0xCBF29CE484222325
    for byte in data:
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def handle_for(name):
    h = 0xCBF29CE484222325
    for byte in name.encode("utf-8"):
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h or 0x7261676556444D4F


def ensure_meta(path, kind):
    """Mint the `.meta` beside a generated asset if the registry has not.

    **Only when it is missing.** The three parts that already exist carry
    handles the registry minted, and the scene reads them: writing a new one
    over the top would change the handle, and every entity pointing at the old
    one would quietly draw nothing.
    """
    meta = path.with_name(path.name + ".meta")
    if meta.exists():
        return handle_for(path.name)
    lines = ["Handle: {0}".format(handle_for(path.name)),
             "Type: " + kind,
             "SourceHash: {0}".format(fnv1a(path.read_bytes()))]
    meta.write_text(NEWLINE.join(lines), encoding="utf-8", newline=NEWLINE)
    return handle_for(path.name)


def write_materials():
    """The three `.rmat`s the parts wear.

    International Orange is not decoration: its reflectance peaks at 600 nm,
    right where a sodium lamp emits, and that match is why this bridge looks
    right at night and most things do not.
    """
    out = ROOT / "SampleProject" / "assets" / "materials"
    out.mkdir(parents=True, exist_ok=True)
    written = {}

    # **The maps do the work; the constants under them are only what a map
    # multiplies.** BaseColor stays white where a colour map is bound, so the
    # texture's own values reach the shader unscaled -- tinting a mapped
    # albedo is how a surface ends up darker than anything that was ever
    # measured. Roughness and Occlusion are the same: 1, and the map decides.
    #
    # `tiling` is metres of surface per repeat, expressed as repeats per unit
    # of the model's uv. The model is authored in metres and its uv scales
    # are the `uv=` on each primitive, so these are the numbers that put a
    # rivet at 110 mm and a plate seam at a metre.
    # NormalScale over 1 on the steel and the concrete: the relief in those
    # maps is real millimetres, and at the distances this bridge is looked
    # at from, honest millimetres disappear. Pushing the slope is the cheap
    # half of what a close-up wants and costs nothing at range.
    for name, texture, colour, rough, tiling, height_scale, normal_scale in (
            ("bridge_orange", "bridge_steel", (0.527, 0.037, 0.025), 0.42,
             (1.0, 1.0), 0.006, 1.6),
            ("bridge_concrete", "bridge_concrete", (0.42, 0.41, 0.38), 0.78,
             (1.0, 1.0), 0.004, 1.8),
            ("bridge_asphalt", "bridge_asphalt", (0.055, 0.055, 0.058), 0.86,
             (2.4, 2.4), 0.0, 1.2),
            ("bridge_paint", "bridge_paint", (0.72, 0.71, 0.68), 0.70,
             (1.0, 1.0), 0.0, 1.0)):
        maps = {}
        for key, suffix in (("BaseColor", "color"), ("Normal", "normal"),
                            ("Roughness", "roughness"), ("Occlusion", "ao")):
            png = ROOT / "SampleProject" / "assets" / "textures" / \
                "{0}_{1}.png".format(texture, suffix)
            if png.exists():
                maps[key] = handle_for(png.name)

        path = out / (name + ".rmat")
        lines = [
            "Material: " + name,
            "BaseColor: [1, 1, 1, 1]" if "BaseColor" in maps
            else "BaseColor: [{0:g}, {1:g}, {2:g}, 1]".format(*colour),
            "Emissive: [0, 0, 0, 1]",
            "Metallic: 0",
            "Roughness: {0:g}".format(1.0 if "Roughness" in maps else rough),
            "Occlusion: 1",
            "NormalScale: {0:g}".format(normal_scale),
            "Specular: 0.5",
            "HeightScale: {0:g}".format(height_scale),
            "Tiling: [{0:g}, {1:g}]".format(*tiling),
            "UvOffset: [0, 0]",
        ]
        if maps:
            lines.append("Maps:")
            for key in ("BaseColor", "Normal", "Roughness", "Occlusion"):
                if key in maps:
                    lines.append("  {0}: {1}".format(key, maps[key]))
        body = NEWLINE.join(lines) + NEWLINE
        path.write_text(body, encoding="utf-8", newline=NEWLINE)
        key = "materials/" + name + ".rmat"
        meta = ["Handle: {0}".format(handle_for(key)),
                "Type: Material",
                "SourceHash: {0}".format(fnv1a(path.read_bytes()))]
        path.with_name(path.name + ".meta").write_text(
            NEWLINE.join(meta), encoding="utf-8", newline=NEWLINE)
        written[name] = handle_for(key)
    return written


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)

    OUT.mkdir(parents=True, exist_ok=True)
    total = 0

    materials = write_materials()
    print("  materials  " + "  ".join(
        "{0} {1}".format(k, v) for k, v in sorted(materials.items())))

    leftovers = MaterialSplit()
    written = []
    for name, build, material in PARTS:
        split = build()
        for other, part in split.meshes.items():
            if other != material:
                leftovers.absorb(split_of(other, part))
        written.append((name, split.mesh(material)))

    for name, material in SHARED:
        written.append((name, leftovers.mesh(material)))

    for name, mesh in written:
        path = OUT / (name + ".fbx")
        points, faces, _ = fbxwrite.write(path, mesh, name)
        ensure_meta(path, "Mesh")
        low = [min(p[i] for p in mesh.points) for i in range(3)] if mesh.points else [0, 0, 0]
        high = [max(p[i] for p in mesh.points) for i in range(3)] if mesh.points else [0, 0, 0]
        print("  {0:16} {1:6} verts {2:6} faces   "
              "{3:.1f} x {4:.1f} x {5:.1f} m".format(
                  name, points, faces,
                  high[0] - low[0], high[1] - low[1], high[2] - low[2]))
        total += faces

    print("{0} parts, {1} faces in all, in {2}".format(
        len(written), total, OUT))

    # The three numbers that have to agree, printed so a wrong one is loud.
    print("\n  roadway {0:.1f} m   truss soffit {1:.1f} m   "
          "cable low point {2:.1f} m ({3:.1f} m over the road)".format(
              ROADWAY, ROADWAY - TRUSS_DEPTH, cable_y(0.0),
              cable_y(0.0) - ROADWAY))
    print("  cable at the saddle {0:.1f} m   sag {1:.1f} m   "
          "ratio 1:{2:.2f}".format(
              cable_y(HALF_SPAN), SAG, 2 * HALF_SPAN / SAG))


if __name__ == "__main__":
    main()
