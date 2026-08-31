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
PYLON_HALF = (5.4, 6.8)             # transverse, longitudinal
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
MATERIALS = [
    ("InternationalOrange", (0.527, 0.037, 0.025)),
    ("Concrete", (0.42, 0.41, 0.38)),
    ("Asphalt", (0.055, 0.055, 0.058)),
]

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
    mesh.box((-DECK_HALF_WIDTH, ROADWAY - DECK_SLAB, -HALF_LENGTH),
             (DECK_HALF_WIDTH, ROADWAY, HALF_LENGTH), uv=8.0, material=MAT_ROAD)

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
    posts = int(round(2.0 * HALF_LENGTH / RAIL_POST))
    for side in (-RAIL_HALF_WIDTH, RAIL_HALF_WIDTH):
        mesh.box((side - 0.09, ROADWAY + RAIL_HEIGHT - 0.16, -HALF_LENGTH),
                 (side + 0.09, ROADWAY + RAIL_HEIGHT, HALF_LENGTH),
                 uv=6.0, material=MAT_STEEL)
        for i in range(posts + 1):
            z = -HALF_LENGTH + i * RAIL_POST
            if z > HALF_LENGTH:
                break
            mesh.box((side - 0.07, ROADWAY, z - 0.07),
                     (side + 0.07, ROADWAY + RAIL_HEIGHT, z + 0.07),
                     uv=1.0, material=MAT_STEEL)

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
            x = side * (RAIL_HALF_WIDTH + 0.32)
            base = ROADWAY + RAIL_HEIGHT * 0.2
            # `cylinder` has no uv of its own -- it is the one primitive
            # here whose `uv` would land in `add`'s per-face list.
            mesh.cylinder(0.13, LAMP_HEIGHT, sides=6, taper=0.7,
                          base_y=base, offset=(x, 0.0, z),
                          material=MAT_STEEL)
            head = base + LAMP_HEIGHT
            mesh.strut((x, head, z), (x - side * LAMP_ARM, head + 0.28, z),
                       0.09, sides=4, uv=1.0, material=MAT_STEEL)
            mesh.box((x - side * LAMP_ARM - 0.30, head + 0.06, z - 0.22),
                     (x - side * LAMP_ARM + 0.30, head + 0.32, z + 0.22),
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
            mesh.prism(low, high, leg_half(low), leg_half(high),
                       offset=(x, 0.0, z), uv=3.0, material=MAT_STEEL)

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
        mesh.box((-TRUSS_HALF, bottom, z - depth * 0.5),
                 (TRUSS_HALF, high, z + depth * 0.5), uv=3.0,
                 material=MAT_STEEL)

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
        for x in (-(TRUSS_HALF + PYLON_HALF[0] + 1.2),
                  TRUSS_HALF + PYLON_HALF[0] + 1.2):
            mesh.box((x - PYLON_HALF[0], -6.0, z - PYLON_HALF[1]),
                     (x + PYLON_HALF[0], PYLON_TOP, z + PYLON_HALF[1]),
                     uv=6.0, material=MAT_CONCRETE)
        # The portal beam over the road, which is what you drive under.
        mesh.box((-(TRUSS_HALF + PYLON_HALF[0] * 2.0), ROADWAY + 11.0,
                  z - PYLON_HALF[1] * 0.7),
                 (TRUSS_HALF + PYLON_HALF[0] * 2.0, PYLON_TOP,
                  z + PYLON_HALF[1] * 0.7),
                 uv=4.0, material=MAT_CONCRETE)


def fort_point_arch(mesh, segments=22):
    """The steel arch that carries the roadway over Fort Point.

    **The one piece of this bridge that is not a suspension bridge.** Strauss
    would not demolish the fort, so the south approach steps over it on an
    arch -- and it is the first thing in frame from the sea wall, which is
    where half the photographs of the Golden Gate are taken from.
    """
    crown = ROADWAY - TRUSS_DEPTH - 1.2
    rise = crown - ARCH_SPRING
    for side in (-TRUSS_HALF, TRUSS_HALF):
        previous = None
        for i in range(segments + 1):
            t = -1.0 + 2.0 * i / segments
            z = ARCH_CENTRE + t * ARCH_HALF_SPAN
            y = ARCH_SPRING + rise * (1.0 - t * t)
            point = (side, y, z)
            if previous is not None:
                mesh.strut(previous, point, 0.85, sides=6, uv=2.0,
                           smooth=True, material=MAT_STEEL)
            previous = point

            # The spandrel columns up to the truss.
            if 0 < i < segments and i % 2 == 0:
                mesh.box((side - 0.34, y, z - 0.34),
                         (side + 0.34, ROADWAY - TRUSS_DEPTH, z + 0.34),
                         uv=1.0, material=MAT_STEEL)


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
    for name, colour, rough in (
            ("bridge_orange", (0.527, 0.037, 0.025), 0.42),
            ("bridge_concrete", (0.42, 0.41, 0.38), 0.78),
            ("bridge_asphalt", (0.055, 0.055, 0.058), 0.86)):
        path = out / (name + ".rmat")
        body = NEWLINE.join([
            "Material: " + name,
            "BaseColor: [{0:g}, {1:g}, {2:g}, 1]".format(*colour),
            "Emissive: [0, 0, 0, 1]",
            "Metallic: 0",
            "Roughness: {0:g}".format(rough),
            "Occlusion: 1",
            "NormalScale: 1",
            "Specular: 0.5",
            "HeightScale: 0",
            "Tiling: [1, 1]",
            "UvOffset: [0, 0]",
        ]) + NEWLINE
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
