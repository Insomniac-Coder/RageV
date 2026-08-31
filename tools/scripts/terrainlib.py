#!/usr/bin/env python3
"""Terrain generation: the operators that turn a noise field into a place.

`make_terrain.py` writes `.rvterrain` files and owns the format. This owns
the *shaping*, because that is a separate job and every scene wants it: the
bridge demo's headlands, and anything after it.

**Why this exists.** A heightfield built from formulas -- domes, sines,
summed noise -- makes a shape, and a shape is not a landscape. What the eye
recognises as ground is the record of two processes having run over it:
water moving material downhill into branching channels, and gravity pulling
anything steeper than the material can stand down to the angle it can. Those
cannot be drawn; they have to be simulated. Every other terrain tool worth
using is built around that fact, and the operators below are the ones they
all have:

- `hydraulic_erode` -- droplets that pick up, carry and drop sediment. The
  single highest-value operator here: it cuts the dendritic valley networks
  and lays the fans at their mouths, and nothing else produces either.
- `thermal_erode` -- talus slippage to an angle of repose. Planes slopes,
  sharpens the ridges between them, piles debris at the bottom.
- `ridged` / `billow` / `domain_warp` -- noise bases with crests instead of
  blobs, and the coordinate warp that stops ridge lines running in a lattice.
- `stratify` -- horizontal rock banding, on the steep ground only, which is
  what makes a cliff read as rock rather than as a smooth surface.
- `flow_accumulation`, `curvature`, `slope` -- the masks the *painting* wants:
  material should follow the landform, not be sprinkled over it. Rock on the
  convex, exposed ridges; soil and scrub in the concave hollows; the damp
  green in the draws where the water actually goes.

Everything is vectorised over the whole grid or over a whole batch of
droplets, so a 1025 field takes seconds rather than minutes. Everything is
seeded, so the same call gives the same terrain on every machine.
"""

import math

import numpy as np


# --- noise -------------------------------------------------------------------

def value_noise(resolution, cells, rng):
    """Smooth noise in [0, 1]: a random lattice `cells` a side, smoothstep
    interpolated up to `resolution`. Plain and dependency-free."""
    lattice = rng.random((cells + 1, cells + 1))
    xs = np.linspace(0.0, cells, resolution)
    x0 = np.minimum(np.floor(xs).astype(int), cells - 1)
    fx = xs - x0
    fx = fx * fx * (3.0 - 2.0 * fx)
    a = lattice[x0, :] * (1.0 - fx)[:, None] + lattice[x0 + 1, :] * fx[:, None]
    b = a[:, x0] * (1.0 - fx)[None, :] + a[:, x0 + 1] * fx[None, :]
    return b


def fbm(resolution, rng, octaves, gain=0.5):
    """Summed value noise, coarsest first, in about [-1, 1].

    `octaves` are lattice cell counts across the whole field, so over a
    2 800 m terrain 7 is a 400 m feature and 197 is a 14 m one.
    """
    total = np.zeros((resolution, resolution))
    amplitude, weight = 1.0, 0.0
    for cells in octaves:
        total += amplitude * (value_noise(resolution, cells, rng) - 0.5) * 2.0
        weight += amplitude
        amplitude *= gain
    return total / max(weight, 1e-6)


def ridged(resolution, rng, octaves, gain=0.5, sharpness=1.7):
    """Ridged multifractal noise, in [-1, 1].

    **Smooth value noise makes blobs, and blobs are what reads as fake.**
    Folding each octave about its midpoint -- 1 - |2n - 1| -- turns its
    smooth maxima into creases; a power sharpens them; and multiplying each
    octave by the last leaves detail on the ridges and takes it out of the
    hollows, which is where erosion puts it too. A hill built from this has
    crests and draws before a single grain has been moved.
    """
    total = np.zeros((resolution, resolution))
    amplitude, weight = 1.0, 0.0
    carry = np.ones((resolution, resolution))
    for cells in octaves:
        n = value_noise(resolution, cells, rng)
        r = np.clip(1.0 - np.abs(2.0 * n - 1.0), 0.0, 1.0) ** sharpness
        total += amplitude * r * carry
        carry = np.clip(0.35 + 0.65 * r, 0.0, 1.0)
        weight += amplitude
        amplitude *= gain
    return (total / max(weight, 1e-6)) * 2.0 - 1.0


def domain_warp(field, rng, strength_cells, octaves=(5, 13)):
    """Resample `field` at coordinates pushed about by low-frequency noise.

    Noise summed straight gives ridges that run in a grid-aligned lattice --
    the same fault that made the first ocean read as corrugated iron. Warping
    the *coordinates* before evaluating bends every ridge line into something
    that interlocks, and it costs one resample.

    `strength_cells` is how far the warp pushes, in samples.
    """
    resolution = field.shape[0]
    wx = fbm(resolution, rng, octaves) * strength_cells
    wz = fbm(resolution, rng, octaves) * strength_cells

    zz, xx = np.meshgrid(np.arange(resolution, dtype=np.float64),
                         np.arange(resolution, dtype=np.float64), indexing="ij")
    return _sample_bilinear(field, xx + wx, zz + wz)


def _sample_bilinear(field, x, z):
    """`field` at fractional (x, z), clamped at the edges."""
    resolution = field.shape[0]
    x = np.clip(x, 0.0, resolution - 1.001)
    z = np.clip(z, 0.0, resolution - 1.001)
    x0 = x.astype(np.int32)
    z0 = z.astype(np.int32)
    tx = x - x0
    tz = z - z0
    return ((field[z0, x0] * (1 - tx) + field[z0, x0 + 1] * tx) * (1 - tz)
            + (field[z0 + 1, x0] * (1 - tx) + field[z0 + 1, x0 + 1] * tx) * tz)


# --- erosion -----------------------------------------------------------------

def thermal_erode(height, cell, iterations=70, repose_degrees=38.0, rate=0.55,
                  mask=None):
    """Talus slippage: material steeper than the angle of repose slides.

    The cheapest erosion there is and the one that does most for a
    silhouette. It planes slopes to a characteristic angle, sharpens the
    ridges between them, and piles the debris at the bottom -- the difference
    between a hill and a heap.

    `repose_degrees` is the material: about 33 for loose scree, 40-45 for
    the rock a sea cliff is made of. Erode a rock headland at a scree angle
    and it collapses into a mound.

    `mask` in [0, 1] scales how much each sample is allowed to move, so a
    coastline or a road corridor can be held still.
    """
    talus = math.tan(math.radians(repose_degrees)) * cell
    shifts = ((1, 0), (-1, 0), (0, 1), (0, -1))
    for _ in range(iterations):
        excess = []
        total = np.zeros_like(height)
        for dz, dx in shifts:
            drop = height - np.roll(np.roll(height, -dz, 0), -dx, 1)
            e = np.maximum(drop - talus, 0.0)
            excess.append(e)
            total += e
        moved = rate * 0.25 * total
        if mask is not None:
            moved = moved * mask
        height = height - moved
        safe = np.where(total > 1e-9, total, 1.0)
        for (dz, dx), e in zip(shifts, excess):
            height = height + np.roll(np.roll(moved * e / safe, dz, 0), dx, 1)
    return height


def hydraulic_erode(height, cell, droplets=300000, steps=64, seed=7,
                    inertia=0.06, capacity=3.2, min_slope=0.02,
                    erode_rate=0.35, deposit_rate=0.28, evaporate=0.02,
                    gravity=6.0, max_speed=6.0, max_capacity=8.0,
                    max_change=0.5, mask=None):
    """Droplet erosion with sediment transport.

    **This is the operator that makes terrain look eroded**, and there is no
    substitute for it: valleys that branch, join and run to the sea, and fans
    of deposit where they flatten out, are the signature of material actually
    being carried. Blurring, noise and thermal slippage cannot fake any of it.

    Each droplet is dropped at random, follows the surface downhill with a
    little inertia, and carries sediment up to a capacity set by its speed,
    its water and the slope it is on. Over capacity it drops the difference;
    under it, it cuts. Every droplet in the batch is stepped at once -- the
    whole thing is numpy, so three hundred thousand of them over a 1025 grid
    is seconds, not minutes.

    The three numbers that matter: `capacity` (how much a droplet can carry,
    so how deep the channels cut), `erode_rate` against `deposit_rate` (a
    landscape that only cuts is a badland; the fans come from depositing),
    and `steps` (how far a droplet travels, so how long the channels are).

    **The three limiters are not decoration; without them this diverges.**
    Cutting a cell deepens it, which steepens the drop into it, which raises
    the next droplet's capacity, which cuts it deeper: left alone the loop
    runs to 1e33 in thirty steps, and it was measured doing exactly that.
    `max_speed` stops the velocity term compounding, `max_capacity` caps what
    a droplet may carry however fast it is going, and `max_change` says no
    single droplet-step moves more than half a metre of ground -- which is
    also just true.

    `mask` in [0, 1] scales the change applied at each sample.
    """
    resolution = height.shape[0]
    h = height.astype(np.float64).copy()
    rng = np.random.default_rng(seed)

    px = rng.uniform(1.0, resolution - 2.0, droplets)
    pz = rng.uniform(1.0, resolution - 2.0, droplets)
    dx = np.zeros(droplets)
    dz = np.zeros(droplets)
    speed = np.ones(droplets)
    water = np.ones(droplets)
    sediment = np.zeros(droplets)
    alive = np.ones(droplets, dtype=bool)

    def gradient_at(x, z):
        """Height and its gradient, bilinear over the cell, in metres per
        sample. The gradient is the *cell's* plane, not a difference of
        neighbouring samples: a droplet lives between samples."""
        x0 = x.astype(np.int32)
        z0 = z.astype(np.int32)
        tx = x - x0
        tz = z - z0
        h00 = h[z0, x0]
        h10 = h[z0, x0 + 1]
        h01 = h[z0 + 1, x0]
        h11 = h[z0 + 1, x0 + 1]
        gx = (h10 - h00) * (1 - tz) + (h11 - h01) * tz
        gz = (h01 - h00) * (1 - tx) + (h11 - h10) * tx
        value = ((h00 * (1 - tx) + h10 * tx) * (1 - tz)
                 + (h01 * (1 - tx) + h11 * tx) * tz)
        return value, gx, gz, x0, z0, tx, tz

    cells = resolution * resolution

    def scatter(x0, z0, tx, tz, amount):
        """Spread a change over the four samples of the cell, by area.

        A droplet that dumps its whole load on one sample makes a spike, and
        a field of spikes is what an unfiltered erosion looks like.

        `bincount`, not `np.add.at`: hundreds of thousands of droplets times
        tens of steps is tens of millions of scattered adds, and `add.at`
        does them one at a time. Same arithmetic, most of a minute saved.
        """
        flat = (z0.astype(np.int64) * resolution + x0)
        target = h.reshape(-1)
        target += np.bincount(flat, weights=amount * (1 - tx) * (1 - tz), minlength=cells)
        target += np.bincount(flat + 1, weights=amount * tx * (1 - tz), minlength=cells)
        target += np.bincount(flat + resolution, weights=amount * (1 - tx) * tz, minlength=cells)
        target += np.bincount(flat + resolution + 1, weights=amount * tx * tz, minlength=cells)

    for _ in range(steps):
        if not alive.any():
            break

        value, gx, gz, x0, z0, tx, tz = gradient_at(px, pz)

        # Direction: last heading, plus the slope. Pure gradient descent
        # traps a droplet in every dimple; a little inertia carries it out
        # and is what lets channels run.
        dx = dx * inertia - gx * (1.0 - inertia)
        dz = dz * inertia - gz * (1.0 - inertia)
        length = np.sqrt(dx * dx + dz * dz)
        moving = length > 1e-8
        dx = np.where(moving, dx / np.where(moving, length, 1.0), 0.0)
        dz = np.where(moving, dz / np.where(moving, length, 1.0), 0.0)

        nx = px + dx
        nz = pz + dz
        inside = (nx >= 1.0) & (nx <= resolution - 2.0) & \
                 (nz >= 1.0) & (nz <= resolution - 2.0) & moving
        alive = alive & inside
        if not alive.any():
            break

        nx = np.clip(nx, 1.0, resolution - 2.0)
        nz = np.clip(nz, 1.0, resolution - 2.0)
        new_value = _sample_bilinear(h, nx, nz)
        drop = value - new_value          # positive going downhill

        # Capacity rises with the slope it is running down, its speed and how
        # much water is left. The floor stops a droplet on flat ground from
        # dropping everything at once and pockmarking the plain.
        cap = np.minimum(np.maximum(drop, min_slope) * speed * water * capacity,
                         max_capacity)

        over = sediment > cap
        change = np.where(
            over,
            (sediment - cap) * deposit_rate,                  # deposit
            -np.minimum((cap - sediment) * erode_rate, np.maximum(drop, 0.0)))
        # Uphill: it cannot cut, only fill the hole it is climbing out of.
        change = np.where(drop < 0.0, np.minimum(sediment, -drop), change)

        change = np.clip(np.where(alive, change, 0.0), -max_change, max_change)
        if mask is not None:
            change = change * _sample_bilinear(mask, px, pz)

        scatter(x0, z0, tx, tz, change)
        sediment = sediment - change

        speed = np.minimum(np.sqrt(np.maximum(speed * speed + drop * gravity, 0.0)),
                           max_speed)
        water = water * (1.0 - evaporate)
        px, pz = nx, nz

    return h.astype(height.dtype)


# --- the masks painting wants ------------------------------------------------

def slope(height, cell):
    """|grad h|, dimensionless: 1.0 is 45 degrees."""
    dz, dx = np.gradient(height, cell)
    return np.hypot(dx, dz)


def curvature(height, cell):
    """The Laplacian, scaled: positive on convex ground (ridges, noses),
    negative in concave ground (hollows, draws).

    The mask nobody thinks of and every painted terrain needs. Rock is
    exposed where the ground is convex and being stripped; soil and scrub
    collect where it is concave and receiving. Painting by height and slope
    alone gives contour-following bands, which is the other way a terrain
    announces that it was generated.
    """
    lap = (np.roll(height, 1, 0) + np.roll(height, -1, 0)
           + np.roll(height, 1, 1) + np.roll(height, -1, 1) - 4.0 * height)
    return lap / (cell * cell)


def flow_accumulation(height, iterations=48):
    """How much water passes through each sample, roughly, in units of the
    rain that falls on one.

    Multiple-flow-direction: every sample sends its water to all eight lower
    neighbours in proportion to the drop towards each, advected a fixed
    number of steps. Not a solved drainage network -- there is no depression
    filling and no ordering -- but it lands water in the draws in the right
    proportions, which is all a paint mask or a wetness term needs.
    """
    shifts = ((1, 0), (-1, 0), (0, 1), (0, -1),
              (1, 1), (1, -1), (-1, 1), (-1, -1))
    drops = []
    total = np.zeros_like(height)
    for dz, dx in shifts:
        drop = np.maximum(height - np.roll(np.roll(height, -dz, 0), -dx, 1), 0.0)
        drops.append(drop)
        total += drop
    safe = np.where(total > 1e-9, total, 1.0)
    share = [d / safe for d in drops]
    stays = np.where(total > 1e-9, 0.0, 1.0)

    water = np.ones_like(height)
    collected = np.zeros_like(height)
    for _ in range(iterations):
        moved = np.zeros_like(height)
        for (dz, dx), w in zip(shifts, share):
            moved += np.roll(np.roll(water * w, dz, 0), dx, 1)
        water = moved + water * stays
        collected += water
    return collected / max(iterations, 1)


# --- rock --------------------------------------------------------------------

def stratify(height, amount, thickness, mask=None, tilt=0.0):
    """Horizontal rock banding: sedimentary strata, stepped into the face.

    A cliff cut out of smooth noise is a smooth cliff, and no rock is smooth
    at a hundred metres. Quantising the height into bands and pushing each
    one out a little gives the stepped ledge-and-riser profile that says
    "rock" from any distance -- and because it is a function of height, the
    bands run level across the whole face and wrap round a spur exactly as
    real bedding does.

    `tilt` dips the beds, in metres of height per sample of x, because
    perfectly level bedding is its own tell.
    """
    resolution = height.shape[0]
    x = np.arange(resolution, dtype=np.float64)[None, :]
    level = height + tilt * x
    band = level / max(thickness, 1e-6)
    # A sawtooth, softened: the riser is steeper than the ledge.
    frac = band - np.floor(band)
    step = frac * frac * (3.0 - 2.0 * frac)
    offset = (step - 0.5) * amount
    return height + (offset if mask is None else offset * mask)
