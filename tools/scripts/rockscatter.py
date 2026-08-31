#!/usr/bin/env python3
"""Where the rocks go, read off the terrain rather than guessed at.

Imported by `make_bridge_scene.py`. Separate because placement is a
level-design decision with its own reasoning, and burying it in the scene
builder would hide both.

## The ground is classified, not assumed

Three earlier versions of this guessed at height and slope bands and were
wrong every time, because **none of those guesses had looked at the terrain**.
Measured, this heightfield says:

    slope   p50 0.10   p75 0.20   p90 0.42   p95 0.60   p99 1.74
    land    24.6% of the map, 37% of it within 120 m of water

So a band demanding slope > 0.72 -- which the first version used for cliffs --
selects the top three per cent of the map, and that is the *inland massifs*,
not the coast. Every cliff block went to a ridge behind the camera.

What separates a coast from an upland is **distance to water**, which no
height-and-slope pair can express. So the sea mask is dilated outwards and the
three classes fall out of the combination:

    beach     land, within  18 m of water, slope < 0.30, under  3.5 m
    surf      land, within  45 m of water, slope 0.10-0.65, under 7 m
which measure 0.72% and 0.93% of the map. Ground matching none of them
gets no rock, and that is most of the terrain -- real ground is bare between
the places a process put something.

## What goes where

The shore gets pebbles and mostly small stone with a few medium ones, which is
what water does to a beach: it sorts the material down to what it can roll.
The cliffs themselves are not dressed from here -- authored rock assets are
coming for those.

## Only one camera renders

The runtime opens the camera with the lowest `ViewRank`, which is the hero, so
the hero's view is the only one worth spending a budget for. Ranking is
suitability times visibility in full 3D, pitch included -- a stone 30 m above a
camera pitched nine degrees *down* is not in shot, and no horizontal test can
tell.
"""

import math

import numpy as np

# name -> (max distance to water, slope low, slope high, height low, height high)
CLASSES = {
    "beach": (18.0, 0.00, 0.30, 0.0, 3.5),
    "surf": (45.0, 0.10, 0.65, 0.0, 7.0),
}

# What each class is dressed with: meshes, scale band, how deep it sits as a
# fraction of its own size, stones per cluster.
#
# **The shore is small material and the cliff is large.** A beach of uniformly
# medium stones is a gravel pit, so the medium boulders are deliberately rare --
# one surf cluster in MEDIUM_IN gets them.
DRESSING = {
    "beach": (("pebble_a", "pebble_b", "pebble_c"), (0.09, 0.30), 0.44, 6),
    "surf": (("rock_a", "rock_b", "rock_c"), (0.30, 0.85), 0.36, 3),
}

# **Cliff faces were tried here and removed on 2026-08-31.** Panels large
# enough to cover a face read as flat patches stuck on a hillside -- the
# silhouette gave them away every time, because a panel's outline is a
# rectangle and a cliff's is not. Real scanned cliff assets are coming in
# their place. What survives from that attempt is worth keeping: the terrain
# slope limiter it forced into `make_bridge_seabed`, which fixed the
# corrugated coastline the panels were being asked to hide.
MEDIUM_IN = 4
MEDIUM_SCALE = (1.05, 2.05)

# Roughly how far apart cluster anchors are, in metres. Converted to a cell
# stride against the heightfield, so anchors land on classified ground rather
# than on a grid that happens to cross it.
SPACING = 30.0

# How many clusters of each class to keep, best first.
#
# **A cap, not a density.** The transform walk is this engine's ceiling and
# there are no mesh LODs (NEXT.md 5), so every stone placed is drawn at full
# detail from every distance and this is the only control there is. Measured:
# an early pass placed 490 and cost 5.1 ms a frame, 7.6 to 12.7 -- worth paying
# for stone in shot, worth nothing for the stone that pass put behind the
# camera.
BUDGET = {"beach": 26, "surf": 34}

# Nothing in here: rock standing in the roadway or growing through a pier is
# the one placement error that cannot be read as nature.
KEEP_CLEAR_X = 24.0
KEEP_CLEAR_Z = 1010.0

_CACHE = {}


def rock_random(x, z, salt):
    """A repeatable [0, 1) from a world position, so the scatter is stable.

    **Position-seeded, not sequence-seeded**, and that matters twice.
    Regenerating the scene must not reshuffle the rocks, or a screenshot
    compared with one from an hour ago is worthless; and changing how many
    rocks are placed must not move the ones that stay, which a running counter
    would do to every stone after the first change.
    """
    h = 0xCBF29CE484222325
    for v in (int(x * 8.0) & 0xFFFFF, int(z * 8.0) & 0xFFFFF, salt):
        h ^= v & 0xFFFFFFFFFFFFFFFF
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        h ^= h >> 29
    return ((h >> 20) & 0xFFFFFF) / 16777216.0


def survey(seabed):
    """Height, slope and distance-to-water for the whole field, computed once.

    **Distance to water is the measurement the earlier versions were missing.**
    Height and slope together cannot tell a sea cliff from an inland scarp, and
    this terrain has far more of the second than the first -- which is why every
    block went inland. Dilating the sea mask outward is cheap and, at 2.73 m a
    cell, exact enough to answer "is this the coast".
    """
    if "survey" in _CACHE:
        return _CACHE["survey"]

    height = seabed.heights_metres()
    n = height.shape[0]
    cell = seabed.SIZE / (n - 1)
    gz, gx = np.gradient(height, cell)
    slope = np.hypot(gx, gz)

    sea = height <= 0.0
    distance = np.full(height.shape, 1.0e9, dtype=np.float32)
    distance[sea] = 0.0
    front = sea.copy()
    # 60 steps is 164 m, past the farthest class boundary; the loop leaves
    # early once the front stops growing.
    for step in range(1, 61):
        grown = front.copy()
        grown[1:, :] |= front[:-1, :]
        grown[:-1, :] |= front[1:, :]
        grown[:, 1:] |= front[:, :-1]
        grown[:, :-1] |= front[:, 1:]
        fresh = grown & ~front
        if not fresh.any():
            break
        distance[fresh] = step * cell
        front = grown

    _CACHE["survey"] = (height, slope, distance, cell, n)
    return _CACHE["survey"]


def prominence(x, y, z, view):
    """How much the camera that actually renders can be expected to care.

    `view` is (position, forward, right, up, tan half-horizontal,
    tan half-vertical) for the hero -- the only camera the runtime opens, since
    it takes the lowest `ViewRank`. Weighting every camera in the scene pulls
    the budget towards ground nothing will draw.

    **This is a frustum test, not a heuristic, and it is the fourth attempt.**
    Slope alone put every block on the inland massifs. Distance put them behind
    the camera. A forward dot product put them 30 m *above* a camera pitched
    nine degrees down -- 55 degrees up, well outside a 55 degree lens, and
    still scoring because the horizontal terms outvoted the vertical one. A
    dot product answers "roughly that way"; only projecting the point into the
    camera's own basis answers "in shot", so that is what this does.

    The bounds are opened 15% past the real frustum on purpose: a cliff block
    just outside the edge of frame is still doing work, because it occludes and
    shadows what is inside it.
    """
    (px, py, pz), forward, right, up, tan_h, tan_v = view
    dx, dy, dz = x - px, y - py, z - pz
    ahead = dx * forward[0] + dy * forward[1] + dz * forward[2]
    if ahead <= 1.0:
        return 0.0

    across = dx * right[0] + dy * right[1] + dz * right[2]
    rise = dx * up[0] + dy * up[1] + dz * up[2]
    margin = 1.15
    if abs(across) > ahead * tan_h * margin or abs(rise) > ahead * tan_v * margin:
        return 0.0

    # In shot. Rank by nearness to the camera and to the axis of the frame:
    # 300 m of falloff is generous on purpose, since a headland 300 m off is
    # still the thing filling a third of the picture.
    d = math.sqrt(dx * dx + dy * dy + dz * dz)
    centred = 1.0 - 0.5 * max(abs(across) / (ahead * tan_h),
                              abs(rise) / (ahead * tan_v))
    return math.exp(-d / 300.0) * max(centred, 0.05)


def anchors(seabed, view):
    """Cluster anchors per class, ranked by how well the hero camera sees them."""
    height, slope, distance, cell, n = survey(seabed)
    stride = max(1, int(round(SPACING / cell)))
    half = seabed.SIZE * 0.5
    found = {name: [] for name in CLASSES}

    for iz in range(0, n, stride):
        for ix in range(0, n, stride):
            h = float(height[iz, ix])
            if h <= 0.0:
                continue

            x = ix * cell - half
            z = iz * cell - half
            if abs(x) < KEEP_CLEAR_X and abs(z) < KEEP_CLEAR_Z:
                continue

            g = float(slope[iz, ix])
            d = float(distance[iz, ix])

            for name, (dmax, smin, smax, hmin, hmax) in CLASSES.items():
                if d <= dmax and smin <= g < smax and hmin <= h <= hmax:
                    seen = prominence(x, h, z, view)
                    if seen > 0.0:
                        found[name].append((seen, x, z))
                    break

    return found


def place(scene, seabed, handle_for, mesh_handle_for, view):
    """Emit the entities. Returns how many stones were placed."""
    stone = handle_for("materials/rock_stone.rmat")
    shingle = handle_for("materials/rock_pebble.rmat")
    found = anchors(seabed, view)
    placed = 0

    # --- the loose stone ------------------------------------------------------
    for name in ("beach", "surf"):
        chosen = sorted(found[name], key=lambda row: -row[0])[:BUDGET[name]]
        meshes, (low, high), sink, per = DRESSING[name]
        material = shingle if name == "beach" else stone

        for index, (_, ax, az) in enumerate(chosen):
            # A few medium boulders through the surf, and only a few.
            span = MEDIUM_SCALE if (name == "surf" and index % MEDIUM_IN == 0) \
                else (low, high)

            # **A cluster, not a stone.** One dominant piece with satellites,
            # because rock arrives in groups. Evenly spaced single stones is
            # the tell that survives even a good jitter.
            for k in range(per):
                theta = rock_random(ax, az, 20 + k) * 2.0 * math.pi
                reach = 0.0 if k == 0 else (1.6 + 7.5 * rock_random(ax, az, 10 + k))
                if name == "beach":
                    reach *= 0.45
                elif name == "seacliff":
                    reach *= 1.7
                x = ax + math.cos(theta) * reach
                z = az + math.sin(theta) * reach
                ground = seabed.height_at(x, z)

                # **A 3:1 scale range, squared toward the small end, and the
                # satellites smaller again.** Ten per cent of variation reads
                # as one asset repeated; this is what makes nine meshes look
                # like ninety.
                t = rock_random(x, z, 30 + k)
                scale = span[0] + (span[1] - span[0]) * (t * t)
                if k > 0:
                    scale *= 0.36 + 0.44 * rock_random(x, z, 40 + k)

                mesh = meshes[int(rock_random(x, z, 50 + k) * len(meshes)) % len(meshes)]

                # Sunk by a fraction of its own size. A stone resting exactly
                # on the surface reads as dropped there; one emerging from the
                # ground reads as having always been there. The meshes carry a
                # flat sole for this, so what is buried is the sole.
                y = ground - scale * sink

                # Yaw freely, tilt only a little: a boulder that has come to
                # rest is nearly level, and one lying at forty degrees reads as
                # caught mid-fall.
                # Radians -- see the note on the cliff faces above.
                tilt = math.radians(15.0)
                rotation = ((rock_random(x, z, 60 + k) - 0.5) * tilt,
                            rock_random(x, z, 70 + k) * 2.0 * math.pi,
                            (rock_random(x, z, 80 + k) - 0.5) * tilt)

                scene.entity("Rock {0} {1} {2}".format(name, index, k),
                             position=(x, y, z), rotation=rotation,
                             scale=(scale, scale, scale))
                scene.mesh(mesh_handle_for(mesh + ".fbx"), material)
                placed += 1

    return placed
