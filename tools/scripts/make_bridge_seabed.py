#!/usr/bin/env python3
"""The floor of the Golden Gate, and the headlands it runs between.

    python tools/scripts/make_bridge_models.py     # the piers moved: rerun it
    python tools/scripts/make_bridge_seabed.py
    python tools/scripts/make_bridge_scene.py

**Why a seabed is water work and not level design.** Everything the water
shader grew measures a distance to the bottom: screen-space and traced
refraction bend a ray that has to *hit* something, Beer-Lambert absorption
integrates over a thickness, the shallow/deep colour ramp reads that
thickness in metres, the caustic webs are masked by it, and contact foam
rings geometry where it goes to zero. Over a bottomless bay every one of them
takes its "nothing there" branch, which is exactly the flat, fake sea the
owner called out.

**Everything here is the real strait, to the numbers that could be found.**
The first draft was a symmetric shelf with the towers standing in matched
8 m of water, and it was wrong in the way that matters: the Golden Gate is
violently asymmetric, and the asymmetry is the whole character of the place.

    | measured                            | source                       |
    |-------------------------------------|------------------------------|
    | south pier 1 100 ft off Fort Point  | USNI Proceedings, Apr 1935   |
    | south foundation 100 ft below water | same; divers worked at 110ft |
    | north pier on the mainland,         | same -- Lime Point is the    |
    |   foundation 20 ft below water      |   north shore of the strait  |
    | Lime Point cliff 400 ft sheer       | Fort Baker post history      |
    | channel scoured to 113 m in bedrock | USGS multibeam, 2004-05      |
    | sand waves 30 ft tall, 700 ft long  | USGS / KQED, under the span  |
    | Hawk Hill 280 m, 1.5 km NW          | Marin Headlands              |
    | Presidio bluff about 74 m           | topo, south side             |

**Axes, because two of them are counter-intuitive.** `-z` is north (Marin),
`+z` is south (San Francisco) -- the deck camera stands in the south side
span. `+x` is west (the Pacific), `-x` is east (the bay), which the profile
camera fixes by standing "off the west side" at `+x`. So the strait itself
runs along **x**, the bridge crosses it along **z**, and the tidal scour and
its sand-wave fields are features of x while the shore profile is a feature
of z.

**What each shape is doing:**

- **The cross-strait profile** is a sounding list, not a formula: 0 m at
  Lime Point, plunging to the 113 m scour pool nearer the Marin side -- the
  current hugs that shore -- then a long ramp up the San Francisco side
  through 30 m at the south pier to the Fort Point shore. The two piers are
  then standing on their own foundations rather than on one invented shelf.
- **The shorelines** are curves in x, so the water ends against a coast with
  Kirby Cove and Horseshoe Bay bitten out of the Marin side, Point Cavallo
  and a west point standing out of it, and Fort Point jutting from a San
  Francisco shore that falls away south-west towards Baker Beach and runs
  flat and sandy east along Crissy Field.
- **The sand waves** are the real ones: 220 m from crest to crest across the
  current, up to 9 m tall, strongest where the floor is 40-70 m down, and
  asymmetric like dunes rather than sinusoidal.
- **The Marin cliff** is the 400 ft one, and the headland behind it is cut
  by the gulches that give those hills their silhouette. Getting that
  outline right matters more than anything else on land: it is the far edge
  of the hero shot.

The shore is where the field crosses zero. Nothing sets it separately, so it
cannot drift out of agreement with the floor.
"""

import argparse
import math
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import make_terrain as terrain  # noqa: E402
import terrainlib as tl  # noqa: E402
import make_bridge_models as bridge  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "SampleProject" / "assets"
TERRAIN_DIR = ASSETS / "terrain"
MATERIALS = ASSETS / "materials"

# --- the grid ----------------------------------------------------------------
#
# Square, because a terrain is: `Size` is one number. 2 800 m matches the
# water body, and the scene widens the water to the same 2 800 so every
# square metre of floor has sea over it -- a floor wider than its water is a
# dry pit around the bay, which is worse than no floor at all.
#
# 1 025 samples is 2.73 m between them. 513 was tried first and the Marin
# gulches came out as smears: the cliff is the far edge of the hero shot and
# a 5.5 m sample cannot hold a 400 ft drop with a gully in it.
SIZE = 2800.0
RESOLUTION = 1025

NORTH_TOWER = -bridge.HALF_SPAN      # -640.1
# **The Marin waterline, and it is not at the tower.** The survey puts the
# north pier on the mainland at Lime Point, and that is what this had: the
# shore ran to the tower and the land carried on out under the side span.
# It reads wrong -- the headland reaches under the deck almost to the first
# bent -- so the coast is pulled 165 m back off the bridge and the north
# tower stands in water like the south one. A deliberate departure from the
# survey, made on the look, and the pier depth below follows it.
NORTH_SHORE_Z = NORTH_TOWER - 165.0
SOUTH_TOWER = bridge.HALF_SPAN       # +640.1, 335 m off Fort Point
FORT_POINT = SOUTH_TOWER + 335.0     # 975.1 -- 1 100 ft, the surveyed number

# The cross-strait sounding profile at the bridge, on the x = 0 line:
# (metres along z, metres of water). Read north to south. The scour pool sits
# nearer Marin because that is the side the ebb current hugs, and the long
# ramp on the San Francisco side is what leaves the south pier at 30 m and
# still lets the shore be shallow.
SOUNDINGS = [
    (NORTH_SHORE_Z, 0.0),      # the Marin waterline
    (-740.0,       10.0),
    (-680.0,       16.0),
    (NORTH_TOWER,  18.0),      # the north pier, now standing in the strait
    (-600.0,       26.0),
    (-540.0,       63.0),
    (-460.0,      100.0),
    (-400.0,      113.0),      # the scoured bedrock channel
    (-300.0,      106.0),
    (-150.0,       93.0),
    (0.0,          79.0),
    (200.0,        65.0),
    (400.0,        48.0),
    (SOUTH_TOWER,  30.5),      # 100 ft, the south pier's foundation
    (780.0,        16.0),
    (880.0,         7.0),
    (FORT_POINT,    0.0),
]

# Sand waves: 700 ft crest to crest, up to 30 ft tall, across the current.
SAND_WAVE_LENGTH = 220.0
SAND_WAVE_HEIGHT = 4.5          # half-amplitude; 9 m crest to trough
SAND_WAVE_BAND = (26.0, 44.0, 72.0, 100.0)   # fades in, holds, fades out

# Marin. The cliff is the measured 400 ft; the ridge behind it climbs towards
# Hawk Hill (280 m), which is 1.5 km away and so off the edge of this field.
LIME_POINT_CLIFF = 78.0
# The massifs behind it, placed in world (x, z) with a height and a radius,
# because *where* they are is the whole point: the bridge crosses the low
# saddle at x = 0 and the big ground stands off to the west. Hawk Hill's
# 280 m is 1.5 km north-west, so only its shoulder is inside this field.
# **Lower than the survey, on the owner's eye.** Hawk Hill really is 280 m
# and these were built to it, but the near headland then fills the hero frame
# as one black mass and the bridge is a detail in front of it. The far shore
# -- low, long, and articulated enough to read its own relief -- is what the
# near one should look like, so the whole Marin group comes down about a
# third and the relief on top of it goes up.
MARIN_MASSIFS = (
    (880.0, -1330.0, 162.0, 800.0),     # the Hawk Hill shoulder, north-west
    (1390.0, -960.0, 138.0, 640.0),     # the ridge running out to Point Bonita
    # Lower and broader than it was: at 208 m this nose stood over the
    # span and took the eye off it, which is the one thing the near
    # ground in this shot must not do.
    (330.0, -985.0, 118.0, 360.0),      # the Battery Spencer nose, over the point
    (-980.0, -1210.0, 112.0, 580.0),    # the ridge above Fort Baker, east
)
# The valley the approach follows, north of Lime Point: its floor climbs
# from the waterline to the deck's underside, and where it gets there is
# where the roadway lands. The truss soffit is at 67.4 m, so 68 puts the
# ground just into the deck at the end of the run and nowhere before it.
ROAD_LANDS_AT = 68.0
# The far end of the field, so the ramp only reaches the soffit where
# the deck stops -- the same landfall the San Francisco approach makes,
# where the ground comes up to the road at its last metre and not
# eighty metres before it.
ROAD_LANDFALL_Z = -1400.0

# San Francisco: Fort Point sits on a low terrace under a bluff that reaches
# the Presidio's 74 m, and the coast west of the bridge stands on the Baker
# Beach cliffs.
FORT_TERRACE = 12.0
PRESIDIO_BLUFF = 34.0
PRESIDIO_RIDGE = 28.0

# The floor never goes below this, and the entity sits at it: heights are
# unsigned, measured up from the transform.
BASE = -125.0

# Metres per repeat of the layers' uv, before each material's own tiling
# multiplies on top. 48 against the soil maps' 6 is an 8 m repeat -- coarse
# enough not to shimmer across a 2.8 km field, fine enough that the caustics
# have something to break on.
TEXTURE_SCALE = 48.0


def smoothstep(x, edge0, edge1):
    """Hermite from 0 at `edge0` to 1 at `edge1`, either order."""
    t = np.clip((np.asarray(x, dtype=np.float64) - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def bump(x, centre, half_width):
    """A smooth 0 -> 1 -> 0 over `centre` +/- `half_width`. Raised cosine, so
    a cove blends into the coast either side of it instead of ending."""
    t = np.clip((np.asarray(x, dtype=np.float64) - centre) / half_width, -1.0, 1.0)
    return np.cos(t * (math.pi * 0.5)) ** 2




def north_shore(x):
    """Where the Marin coast crosses sea level, per column of x.

    Lime Point is a promontory at the bridge -- which is why the bridge lands
    there -- so the coast falls back either side of it, then Kirby Cove bites
    into the west side and Horseshoe Bay, deeper and rounder, into the east.
    Point Cavallo stands back out beyond the bay.
    """
    # **The coast falls back fast, and that is not decoration.** Lime Point
    # is a point: the shore runs away north-west from it towards Kirby Cove
    # within a couple of hundred metres. Draw it as a gentle 130 m recession
    # and the strait's west side becomes a plateau standing between every
    # viewpoint on the headland and the bridge -- which is exactly what put
    # three attempts at the hero shot behind their own hillside.
    z = NORTH_SHORE_Z - 265.0 * smoothstep(np.abs(x), 30.0, 430.0)
    z -= 165.0 * bump(x, 800.0, 270.0)        # Kirby Cove, west
    z -= 340.0 * bump(x, -520.0, 360.0)       # Horseshoe Bay, east
    z += 95.0 * bump(x, -960.0, 200.0)        # Point Cavallo
    z += 78.0 * bump(x, 1260.0, 210.0)        # the west point past the cove
    return z


def south_shore(x):
    """Where the San Francisco coast crosses sea level.

    Fort Point juts; east of it the shore runs almost straight along the
    Crissy Field strand; west of it the coast peels away south towards Baker
    Beach, which is why the strait opens out on that side.
    """
    z = FORT_POINT + np.zeros_like(np.asarray(x, dtype=np.float64))
    z -= 34.0 * bump(x, -25.0, 165.0)         # Fort Point itself
    z += 55.0 * smoothstep(x, 0.0, -900.0)    # the strand east
    z += 300.0 * smoothstep(x, 150.0, 1400.0)  # falling away west
    return z


def heights_metres(seed=11):
    """The field in metres, world Y, as a (RESOLUTION, RESOLUTION) array
    indexed [z][x] -- the order TerrainData::At reads."""
    axis = np.linspace(-SIZE * 0.5, SIZE * 0.5, RESOLUTION)
    x = axis[None, :]
    z = axis[:, None]

    north = north_shore(axis)[None, :]
    south = south_shore(axis)[None, :]

    # --- the water --------------------------------------------------------
    #
    # The soundings are a profile between two *fixed* shores; the real ones
    # move with x. So the sample's position is expressed as a fraction of the
    # strait's width where it stands and the profile read at the equivalent
    # place on the surveyed line. The floor then reaches exactly zero at the
    # coast wherever the coast happens to be, which is what stops the
    # waterline and the bathymetry from disagreeing.
    span = np.maximum(south - north, 1.0)
    across = np.clip((z - north) / span, 0.0, 1.0)
    line = NORTH_SHORE_Z + across * (FORT_POINT - NORTH_SHORE_Z)
    depth = np.interp(line, [p[0] for p in SOUNDINGS], [p[1] for p in SOUNDINGS])

    # The scour is the narrows'. Going west it opens onto the ebb-tidal delta
    # and going east into the bay, and the bay side shoals much faster -- so
    # the same profile is pressed up towards the surface, harder to the east.
    west = np.clip(x / (SIZE * 0.5), 0.0, 1.0)
    east = np.clip(-x / (SIZE * 0.5), 0.0, 1.0)
    depth *= 1.0 - 0.36 * west ** 1.4 - 0.62 * east ** 1.2

    # Presidio Shoal, east of the bridge on the San Francisco side.
    depth -= 20.0 * bump(x, -1210.0, 470.0) * bump(z, 520.0, 430.0)

    # Sand waves, across the current. Dunes are not sinusoids -- the lee side
    # is steep -- so a second harmonic skews them, and the crest lines are
    # bent by a long wave along z rather than being drawn with a ruler.
    phase = (2.0 * math.pi * x / SAND_WAVE_LENGTH
             + 0.55 * np.sin(2.0 * math.pi * z / 940.0))
    dunes = SAND_WAVE_HEIGHT * (np.sin(phase) + 0.32 * np.sin(2.0 * phase))
    dunes *= (smoothstep(depth, SAND_WAVE_BAND[0], SAND_WAVE_BAND[1])
              * smoothstep(depth, SAND_WAVE_BAND[3], SAND_WAVE_BAND[2]))

    rng = np.random.default_rng(seed)
    lumps = 2.2 * tl.fbm(RESOLUTION, rng, (11, 29, 67))

    floor = -depth + (dunes + lumps * smoothstep(depth, 3.0, 14.0))

    # --- the land ---------------------------------------------------------
    #
    # Marin first, and it is a cliff: the 400 ft at Lime Point in the first
    # 150 m, the Battery Spencer shoulder on top of that, then the climb
    # towards Hawk Hill. The gulches are what makes the outline read as the
    # Headlands rather than as a ramp -- they run down the fall line, so they
    # are cut in x and deepen with distance from the shore.
    inland_n = np.maximum(north - z, 0.0)

    # **The high ground is beside the bridge, not on it.** Hawk Hill's 280 m
    # is 1.5 km north-*west*; what the roadway actually crosses at Lime Point
    # is a saddle. Stacking height on "distance inland" -- which is what the
    # first version did -- puts a 280 m hill directly on the alignment and
    # buries the road, and no cut deep enough to clear it is a thing that
    # exists. So the massifs are placed where they are, and the axis is left
    # as the low crossing the bridge was built for.
    marin = np.zeros_like(x + z)
    for cx, cz, height, radius in MARIN_MASSIFS:
        r = np.hypot((x - cx) / radius, (z - cz) / radius)
        marin = np.maximum(marin, height * np.clip(1.0 - r * r, 0.0, 1.0) ** 1.35)

    # The coastal cliff: the measured 400 ft, following the shore rather than
    # the massifs, and dying away into Kirby Cove and Horseshoe Bay where the
    # land comes down to a beach.
    cliff = LIME_POINT_CLIFF * (1.0 - 0.55 * bump(x, 800.0, 320.0)
                                - 0.72 * bump(x, -520.0, 400.0))
    # **Over 100 m, which is 50 degrees.** Two failure modes either side of
    # this number. Spread the 400 ft over 165 m and it is a 36-degree
    # hillside -- a viewpoint on top of a hillside cannot see the water at
    # its foot, which is what put the first Battery Spencer camera 70 m in
    # the air to clear its own ground. Squeeze it into 62 and it is a 63
    # degree wall that reads as cut stone rather than as a headland. 100 m
    # of run keeps the sight line open and lets the relief below sit on a
    # face instead of a facade.
    marin = np.maximum(marin, np.maximum(cliff, 0.0)
                       * smoothstep(inland_n, 0.0, 100.0))

    # The gulches. A plain cosine in x combs the hillside into corduroy;
    # jittering its phase with a slow noise is what makes them read as
    # drainage rather than as a pattern.
    jitter = 210.0 * tl.fbm(RESOLUTION, rng, (3, 7))
    gulch = (0.5 + 0.5 * np.cos(2.0 * math.pi * (x + jitter - 140.0) / 590.0)) ** 2
    marin -= 58.0 * gulch * smoothstep(inland_n, 45.0, 470.0)

    # Three scales of relief, held off the last few metres of the shore so
    # the coastline the sea meets stays the curve the sines drew.
    marin += 26.0 * tl.fbm(RESOLUTION, rng, (7, 17, 41, 97)) \
        * smoothstep(inland_n, 15.0, 260.0)

    # **The road flies over a valley; it does not run through a trench.**
    # A flat-floored cut at 62 m put a kilometre of carriageway inside a
    # canyon, which is the one thing a suspension bridge must never look
    # like. What the alignment actually follows is a draw: the ground stays
    # under the deck from the shore inland and climbs to meet it at the end
    # of the approach, so the roadway is on bents over open ground the whole
    # way and makes landfall once, where the deck stops.
    #
    # The floor is a ramp from the waterline at Lime Point to the soffit at
    # the deck's last bent, and the corridor is 500 m across with soft
    # sides, so it reads as a valley between the spurs rather than as a
    # channel cut for it. **Applied after the erosion, below**, or the talus
    # fills the approach back in.
    grade = ROAD_LANDS_AT * smoothstep(z, NORTH_SHORE_Z, ROAD_LANDFALL_Z)
    corridor = smoothstep(np.abs(x), 260.0, 60.0) * np.where(z < north, 1.0, 0.0)

    # San Francisco: a low terrace at the fort, the bluff above it, the
    # Presidio ridge behind -- shaped so the ground reaches the deck's soffit
    # at the end of the approach, which is where the roadway has to land.
    inland_s = np.maximum(z - south, 0.0)
    sf = (FORT_TERRACE * smoothstep(inland_s, 0.0, 70.0)
          + PRESIDIO_BLUFF * smoothstep(inland_s, 55.0, 250.0)
          + PRESIDIO_RIDGE * smoothstep(inland_s, 230.0, 420.0))
    sf *= 1.0 - 0.78 * smoothstep(x, -220.0, -780.0)
    sf += 20.0 * smoothstep(x, 260.0, 950.0) * smoothstep(inland_s, 0.0, 95.0)
    sf += 8.0 * tl.fbm(RESOLUTION, rng, (9, 23, 53)) \
        * smoothstep(inland_s, 15.0, 180.0)

    # A last, fine roughness over all of the land: the scale that says how
    # far away a hillside is, and the one a viewer misses without being able
    # to name it.
    land = np.where(z < north, marin, sf)
    land += 3.0 * tl.fbm(RESOLUTION, rng, (149, 331), 0.6)

    # --- erosion ------------------------------------------------------------
    #
    # **This is the step that decides whether the land reads as land.** Every
    # shape above it is a formula -- domes, sines, noise -- and a formula
    # makes a *shape*, not a place. What the eye recognises is the record of
    # water and gravity having been at work: slopes planed to one angle,
    # ridges left sharp between them, and hollows joined into a branching set
    # of draws that all run to the sea. None of that can be drawn directly;
    # it has to be *run*.
    #
    # Repose at 42 degrees rather than a scree slope's 33: these headlands
    # are rock, and a 33-degree talus angle would take the Lime Point cliff
    # apart -- which is also, conveniently, the "too sharp" complaint about
    # the same cliff softening from 50 to about 45.
    cell = SIZE / (RESOLUTION - 1)

    # Talus first, to plane the slopes and sharpen the ridges between them.
    # 42 degrees, not a scree slope's 33: these headlands are rock, and a
    # scree angle takes the Lime Point cliff apart into a mound.
    land = tl.thermal_erode(land, cell, iterations=60, repose_degrees=42.0)

    # Then the water. This is the step that makes it a landscape: the
    # dendritic valley network and the fans at its mouths come from material
    # actually being carried, and nothing else produces either.
    land = tl.hydraulic_erode(land, cell, droplets=260000, steps=56, seed=19,
                              capacity=3.4, erode_rate=0.34, deposit_rate=0.3)

    # A second, shorter talus pass to settle what the water undercut: real
    # slopes fail after a channel cuts their toe, and without it the banks
    # stand at angles nothing could hold.
    land = tl.thermal_erode(land, cell, iterations=24, repose_degrees=44.0)

    # Bedding, on the steep ground only, tilted a little because perfectly
    # level strata are their own tell.
    steep = smoothstep(tl.slope(land, cell), 0.55, 1.05)
    land = tl.stratify(land, amount=3.6, thickness=17.0, mask=steep, tilt=0.004)

    # The approach's valley, cut after the ground has finished moving, and
    # then held up at the far end so the deck still lands on something.
    land = land + (np.minimum(land, grade) - land) * corridor
    hold = np.maximum(land, grade - 2.5)
    land = land + (hold - land) * corridor * smoothstep(z, -1150.0, -1330.0)
    return np.where((z < north) | (z > south), np.maximum(land, 0.15), floor)


def paint(height):
    """Layer weights: 0 silt, 1 rock, 2 sand, 3 scrub. The shader normalises,
    and an all-zero sample is layer 0 -- so these need not sum to one."""
    cell = SIZE / (RESOLUTION - 1)
    dz, dx = np.gradient(height, cell)
    slope = np.hypot(dx, dz)

    # **Rock takes a steep face outright, and nothing else is allowed on
    # it.** The terrain's uv is planar -- local metres over the texture scale
    # -- so on a face where the height runs and x and z barely move, every
    # mapped layer smears into vertical stripes. That is what the black teeth
    # down the Marin cliff were. Until the terrain shader can project
    # triplanar, the answer is that the layer covering those faces carries no
    # maps at all, and it has to cover them *completely*: leaving even a
    # fifth of a mapped layer on the cliff leaves a fifth of the streaks,
    # which is what the first attempt at this measured.
    rock = smoothstep(slope, 0.55, 1.0)     # 29 to 45 degrees
    # The band the eye reads depth against, and the beaches it runs up into.
    sand = smoothstep(height, -8.0, -1.0) * smoothstep(height, 16.0, 3.0) * (1.0 - rock)
    # The headland grass, which is most of what is above the cliffs.
    scrub = smoothstep(height, 2.0, 22.0) * (1.0 - rock) * (1.0 - sand)
    silt = np.clip(1.0 - rock - sand - scrub, 0.0, 1.0)
    return terrain.to_weights(silt, rock, sand, scrub)


def materials():
    """Four tints of the shared soil maps. Untextured colour under water
    gives the caustics nothing to break on and the eye nothing to read
    distance from, and above water it gives a 280 m hill no scale at all."""
    maps = terrain.SOIL_MAPS
    return (
        terrain.write_material(MATERIALS / "bay_silt.rmat", "materials/bay_silt.rmat",
                               (0.18, 0.15, 0.11), maps=maps, tiling=(6.0, 6.0)),
        # Mapped like the rest, now that the terrain shader projects from the
        # side on a steep face. Before that it had to be flat colour -- a
        # mapped layer on a cliff was a wall of vertical stripes -- and a
        # flat-colour cliff is a white slab, which is worse.
        terrain.write_material(MATERIALS / "bay_rock.rmat", "materials/bay_rock.rmat",
                               (0.17, 0.14, 0.12), maps=maps, tiling=(6.0, 6.0)),
        terrain.write_material(MATERIALS / "bay_sand.rmat", "materials/bay_sand.rmat",
                               (0.52, 0.45, 0.35), maps=maps, tiling=(6.0, 6.0)),
        # **Tawny, off the photographs.** The Headlands are gold-brown for
        # most of the year -- not the green a hill is reflexively painted,
        # and not the neutral drab this had before the references arrived.
        terrain.write_material(MATERIALS / "bay_scrub.rmat", "materials/bay_scrub.rmat",
                               (0.23, 0.18, 0.08), maps=maps, tiling=(6.0, 6.0)),
    )


_CACHE = {}


def height_at(x, z):
    """The ground at a world (x, z), in metres, bilinear over the samples.

    So a camera can be *stood on* the terrain instead of having its Y typed
    in and then drifting the next time the land is reshaped. Bilinear rather
    than nearest because the drawn surface is interpolated too, and a camera
    2 m over a nearest-sample answer can be underground on a cliff.
    """
    if "height" not in _CACHE:
        _CACHE["height"] = heights_metres()
    height = _CACHE["height"]

    cell = SIZE / (RESOLUTION - 1)
    fx = np.clip((x + SIZE * 0.5) / cell, 0.0, RESOLUTION - 1.001)
    fz = np.clip((z + SIZE * 0.5) / cell, 0.0, RESOLUTION - 1.001)
    ix, iz = int(fx), int(fz)
    tx, tz = fx - ix, fz - iz
    return float(
        height[iz, ix] * (1 - tx) * (1 - tz) + height[iz, ix + 1] * tx * (1 - tz)
        + height[iz + 1, ix] * (1 - tx) * tz + height[iz + 1, ix + 1] * tx * tz)


def clear_eye(x, z, target, eye=2.5, margin=6.0, samples=140):
    """Eye height at (x, z) that both stands on the ground and sees `target`.

    A viewpoint on a headland is only a viewpoint if the headland is not in
    the way, and "is it in the way" is a question about every metre between
    here and there -- not about the ground underfoot. Hand-placing a camera
    behind a 122 m cliff and typing a height is how the first three attempts
    at this shot came out as a hillside filling the frame.

    Returns the ground plus `eye`, raised if that is not enough to clear the
    terrain by `margin` all the way to `target` (a world x, y, z). A large
    lift means the position is wrong, not that the camera should fly, so the
    caller is given the number to look at.
    """
    # Over water the "ground" is the seabed, and an eye 2 m over a seabed
    # 40 m down is underwater. The surface is the floor for anything afloat.
    ground = max(height_at(x, z), 0.0)
    needed = ground + eye
    tx, ty, tz = target
    for i in range(1, samples):
        t = i / float(samples)
        here = height_at(x + (tx - x) * t, z + (tz - z) * t)
        # The eye that puts the sight line over this sample. The margin is
        # scaled by (1 - t) so it is a clearance over the *obstruction* and
        # not a demand that the line arrive above the target -- which it
        # cannot, and which sent the first version to 852 m.
        needed = max(needed, (here - ty * t) / (1.0 - t) + margin)
    return needed


def dimensions():
    """What the scene has to write into the component: the asset's handle and
    the two numbers that turn its unsigned samples back into metres.

    Recomputed rather than remembered. A `Height` copied by hand is a scene
    whose terrain is the right shape at the wrong scale -- the seabed would
    sit metres out and nothing would say so.
    """
    height = heights_metres()
    return {"handle": terrain.handle_for("terrain/bay.rvterrain"),
            "size": SIZE,
            "height": float(height.max()) - BASE,
            "base": BASE,
            "texture_scale": TEXTURE_SCALE}


def build():
    """Writes the asset. Returns (handle, span, heights)."""
    height = heights_metres()
    span = float(height.max()) - BASE
    unit = np.clip((height - BASE) / span, 0.0, 1.0)
    handle = terrain.write_terrain(TERRAIN_DIR / "bay.rvterrain",
                                   terrain.to_u16(unit),
                                   "terrain/bay.rvterrain",
                                   weights=paint(height))
    return handle, span, height


def report(height):
    """The numbers a change here can silently break, checked rather than
    hoped for: both piers have to be standing on the floor, and the floor
    has to reach the depth the survey says."""
    axis = np.linspace(-SIZE * 0.5, SIZE * 0.5, RESOLUTION)

    def at(px, pz):
        return float(height[int(np.argmin(np.abs(axis - pz))),
                            int(np.argmin(np.abs(axis - px)))])

    for tag, pz, base in (("north (Lime Point)", NORTH_TOWER, bridge.NORTH_PIER_BASE),
                          ("south (in the strait)", SOUTH_TOWER, bridge.SOUTH_PIER_BASE)):
        floor = at(0.0, pz)
        print("  {0:22s} floor {1:7.2f} m   pier foot {2:6.2f} m   {3}".format(
            tag, floor, base,
            "footed" if floor > base else "STANDING PROUD OF THE FLOOR"))

    print("  deepest {0:.1f} m   highest {1:.1f} m".format(
        float(height.min()), float(height.max())))
    print("  Fort Point shore z {0:+.1f}   Lime Point shore z {1:+.1f}".format(
        float(south_shore(0.0)), float(north_shore(0.0))))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)

    names = materials()
    print("materials  silt {0}  rock {1}  sand {2}  scrub {3}".format(*names))

    handle, span, height = build()
    print("bay.rvterrain  handle {0}  base {1:g}  height {2:.2f}  "
          "{3} samples".format(handle, BASE, span, RESOLUTION))
    report(height)


if __name__ == "__main__":
    main()
