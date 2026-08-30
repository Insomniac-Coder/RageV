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

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "SampleProject" / "assets" / "models" / "bridge"


# --- the bridge, in metres above MHHW ----------------------------------------

MODULE = 3.81               # 12.5 ft -- the module everything else is a multiple of

HALF_SPAN = 640.1           # 4200 ft between the towers
PYLON_X = 983.0             # tower + 1125 ft side span
ROADWAY = 75.0              # 246 ft, the driving surface
TRUSS_DEPTH = 7.62          # 25 ft, top chord to bottom chord
TRUSS_HALF = 13.715         # 90 ft between truss centres -- also the cable plane
                            # and the tower-leg centres. One number, three uses.

TOWER_TOP = 227.4           # 746 ft to the cable centreline at the saddle
PIER_TOP = 13.0             # the pier stands ~13 m out of the water

# The cable. y'' = w/H is the same either side of a tower, so ONE curvature
# constant governs the main span and both side spans -- only the vertex moves.
# Fitting a separate, flatter curve to the side spans is the usual mistake.
SAG = 143.26                # 470 ft at mid-span
CURVE = SAG / (HALF_SPAN ** 2)          # 3.4966e-4 per metre
MAIN_LOW = TOWER_TOP - SAG              # 84.12 m, which is 30 ft over the road
SIDE_VERTEX_X = 1478.8
SIDE_VERTEX_Y = -18.60

CABLE_RADIUS = 0.462        # 36 3/8 in diameter

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
    mesh = fbxwrite.Mesh()

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
                               uv=1.0, smooth=True)
                previous = point
    return mesh


def deck():
    """The stiffening truss as a solid slab, for the silhouette only.

    Real depth (7.62 m) and real width (27.43 m over the truss centres) so the
    proportion against the towers is right; the Warren members inside it are
    stage 3. A slab reads correctly from the chase camera and from the side,
    which is where this stage is judged.
    """
    mesh = fbxwrite.Mesh()
    mesh.box((-TRUSS_HALF, ROADWAY - TRUSS_DEPTH, -PYLON_X),
             (TRUSS_HALF, ROADWAY, PYLON_X), uv=4.0)
    return mesh


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


def tower(z):
    """One tower: two stepped shafts and the seven struts between them."""
    mesh = fbxwrite.Mesh()

    # The piers the shafts stand on, carried below the waterline.
    #
    # **They have to continue under the water, not stop at it.** A pier that
    # ends exactly at y=0 is a tower balanced on a mirror; what says the water
    # is water is seeing the concrete carry on into it. So each goes to -9 m,
    # under the surface at every state of tide this scene has.
    for x in (-TRUSS_HALF, TRUSS_HALF):
        mesh.box((x - LEG_BASE[0] * 0.62, -9.0, z - LEG_BASE[1] * 0.62),
                 (x + LEG_BASE[0] * 0.62, PIER_TOP, z + LEG_BASE[1] * 0.62),
                 uv=3.0)

    # The shafts, one section per strut band, narrowing at each. A single
    # smooth taper from base to top is the wrong shape: the real tower steps,
    # and the steps are the Art Deco.
    levels = [PIER_TOP] + [top for _, top in STRUTS] + [TOWER_TOP]
    for x in (-TRUSS_HALF, TRUSS_HALF):
        for i in range(len(levels) - 1):
            low, high = levels[i], levels[i + 1]
            mesh.prism(low, high, leg_half(low), leg_half(high),
                       offset=(x, 0.0, z), uv=3.0)

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
                 (TRUSS_HALF, high, z + depth * 0.5), uv=3.0)

    return mesh


def towers():
    mesh = fbxwrite.Mesh()
    for z in (-HALF_SPAN, HALF_SPAN):
        part = tower(z)
        base = len(mesh.points)
        mesh.points.extend(part.points)
        for index, face in enumerate(part.faces):
            mesh.faces.append(tuple(base + i for i in face))
            mesh.uv.append(part.uv[index])
            mesh.smooth.append(part.smooth[index])
            mesh.material.append(part.material[index])
        for first, last in part.pieces:
            mesh.pieces.append((first + len(mesh.faces) - len(part.faces),
                                last + len(mesh.faces) - len(part.faces)))
    return mesh


# The sea is not here. It was a 4.8 km box in the first draft of this file and
# is now a WaterComponent: a rectangle whose length and breadth are authored on
# the entity, so it is dragged to size in the inspector rather than regenerated
# and reimported. Renderer/Water builds its grid.


# --- driver -------------------------------------------------------------------

GREY = (0.55, 0.55, 0.55)

PARTS = [
    ("bridge_towers", towers, GREY),
    ("bridge_deck", deck, GREY),
    ("bridge_cables", cables, (0.62, 0.62, 0.62)),
]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)

    OUT.mkdir(parents=True, exist_ok=True)
    total = 0

    for name, build, colour in PARTS:
        mesh = build()
        points, faces, _ = fbxwrite.write(OUT / (name + ".fbx"), mesh, name,
                                          colour=colour)
        low = [min(p[i] for p in mesh.points) for i in range(3)]
        high = [max(p[i] for p in mesh.points) for i in range(3)]
        print("  {0:16} {1:6} verts {2:6} faces   "
              "{3:.1f} x {4:.1f} x {5:.1f} m".format(
                  name, points, faces,
                  high[0] - low[0], high[1] - low[1], high[2] - low[2]))
        total += faces

    print("{0} parts, {1} faces in all, in {2}".format(
        len(PARTS), total, OUT))

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
