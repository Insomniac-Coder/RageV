#!/usr/bin/env python3
"""Fixtures for what the traced bounce's emitter list does with emissive it
does not, or only partly, answer for.

Three rooms, each a sealed box with a white floor to read the bounce off and a
camera looking straight at that floor. They differ only in what glows.

**`emitter_holes`** — the ceiling is one plane whose emissive *texture* is
black with four bright cells out of a hundred and forty-four.

**`emitter_split`** — the same four cells, built as four separate small
emissive quads over an unlit ceiling. This is the ground truth: the emitter
list stands a rectangle in for a mesh and assumes the mesh glows evenly, which
is *exactly true* here, so nothing about the estimator is being approximated.

The pair answers one question: does a partly-lit surface emit the power it
actually emits? The list took the material's scalar as the whole rectangle's
radiance, so before that was fixed the textured room came out about
thirty-six times too bright -- lit as though all hundred and forty-four cells
were on. See docs/TEXEL-EMITTERS.md.

**`emitter_glow`** and **`emitter_glow_plus`** — a room lit by a *dim* ceiling
(emissive 0.6, under the list's `strength > 1` threshold, so the list holds
nothing for it) and, in the `_plus` variant, a second bright emitter sealed
inside a closed box in the corner where none of its light can reach the floor
being measured.

That pair answers a different question, and it is the one that had been
answered wrongly for as long as the list existed: **does an emitter over
*there* change how a glow over *here* contributes?** It did. The bounce
removed a hit's emissive whenever the list was non-empty at all -- so adding
the sealed box, whose light cannot reach anything, put the room in darkness by
deleting the dim ceiling's own contribution. The two floors must now match.

    python tools/scripts/make_emitter_scene.py
    python tools/scripts/check_emitters.py --config Release
"""

import argparse
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from make_camp_textures import png, write_meta                      # noqa: E402
from make_demo_scene import Scene, handle_for, vec, write_material  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "SampleProject" / "assets"

CUBE = 0x7261676556000000
PLANE = CUBE + 2

# The room. Wide enough that the ceiling's light falls off across the floor
# rather than flooding it, and shallow enough that the camera sees the whole
# floor at once.
HALF = 4.0
CEILING = 4.0

# The ceiling grid, and which of its cells are lit. Twelve by twelve is the
# showroom's own fitting, and four lit cells is the case that broke: a ratio
# of 1 in 36, which is a factor no tolerance band can absorb.
CELLS = 12
LIT = ((5, 2), (6, 2), (5, 3), (6, 3))   # (column, row)

# Emissive scalars. The lit surfaces sit well above the list's threshold so
# they are certainly in it; the glow sits under it so it is certainly not.
LIT_EMISSIVE = (6.0, 6.0, 6.0, 1)
# What fraction of the ceiling actually glows -- and therefore what the
# emitter list must multiply the scalar by. The whole point of the fixture.
LIT_FRACTION = len(LIT) / float(CELLS * CELLS)
GLOW_EMISSIVE = (0.6, 0.6, 0.6, 1)


def holes_texture(size):
    """Black with four lit cells, as a mask the material multiplies."""
    image = np.zeros((size, size, 3), dtype=np.float32)
    step = size // CELLS
    for column, row in LIT:
        image[row * step:(row + 1) * step, column * step:(column + 1) * step] = 1.0
    return image


def materials():
    directory = ASSETS / "materials"
    directory.mkdir(parents=True, exist_ok=True)

    holes = handle_for("emitter_holes_albedo.png")
    return {
        # White, matte, no maps: what the bounce lands on and what the check
        # measures. Nothing here glows.
        "floor": write_material(directory / "emitter_floor.rmat", {},
                                tiling=(1, 1), height_scale=0.0,
                                metallic=0.0, roughness=1.0,
                                base_color=(0.8, 0.8, 0.8, 1)),

        # The room's shell: dark, so the floor is lit by the ceiling and not
        # by six reflections of it.
        "shell": write_material(directory / "emitter_shell.rmat", {},
                                tiling=(1, 1), height_scale=0.0,
                                metallic=0.0, roughness=1.0,
                                base_color=(0.04, 0.04, 0.04, 1)),

        # The textured ceiling: the map is the **emissive only**, and the
        # albedo is a plain white the split room's ceiling also wears.
        #
        # A real fitting uses one map for both, as the showroom's does -- an
        # unlit cell neither emits nor reflects. It is separated here because
        # the two rooms must differ in *one* thing. Sharing the map makes the
        # textured ceiling black where it is unlit and the split room's grey,
        # and the floor then reads a difference in how much the ceiling
        # bounces back rather than a difference in what it emits: measured,
        # eighteen per cent of one, which is more than the band this check
        # wants to draw.
        "holes": write_material(directory / "emitter_holes.rmat",
                                {"Emissive": holes},
                                tiling=(1, 1), height_scale=0.0, roughness=1.0,
                                base_color=(1, 1, 1, 1), emissive=LIT_EMISSIVE),

        # The unlit part of both ceilings, so the only difference between the
        # rooms is which surface carries the emission.
        "ceiling": write_material(directory / "emitter_ceiling.rmat", {},
                                  tiling=(1, 1), height_scale=0.0,
                                  metallic=0.0, roughness=1.0,
                                  base_color=(1, 1, 1, 1)),

        # One lit cell, as its own surface and with no map at all -- the
        # ground truth's emitter is uniform over its whole rectangle, which
        # is the one case the list represents exactly.
        "cell": write_material(directory / "emitter_cell.rmat", {},
                               tiling=(1, 1), height_scale=0.0, roughness=1.0,
                               base_color=(1, 1, 1, 1), emissive=LIT_EMISSIVE),

        # **The ground truth for the textured ceiling**: the same plane, the
        # same size, emitting the same total power -- spread evenly instead
        # of concentrated in four cells. Its scalar is the textured room's
        # scalar times the fraction of cells that are lit, which is exactly
        # what folding the map's mean into the emitter's radiance is supposed
        # to arrive at.
        #
        # It is below the list's threshold, so the bounce finds it by
        # hemisphere sampling instead -- a *different estimator*, which is
        # what makes it worth comparing against. Two estimators of one
        # quantity agreeing is evidence; one estimator agreeing with itself
        # is not, which is the lesson the gpu-lit defect taught when three
        # "references" all shared a bug.
        "uniform": write_material(directory / "emitter_uniform.rmat", {},
                                  tiling=(1, 1), height_scale=0.0, roughness=1.0,
                                  base_color=(1, 1, 1, 1),
                                  emissive=tuple(LIT_EMISSIVE[i] * LIT_FRACTION
                                                 for i in range(3)) + (1,)),

        # Dim enough to stay out of the emitter list, bright enough to light
        # the room on its own.
        "glow": write_material(directory / "emitter_glow.rmat", {},
                               tiling=(1, 1), height_scale=0.0, roughness=1.0,
                               base_color=(1, 1, 1, 1), emissive=GLOW_EMISSIVE),
    }


def room(s, mat, ceiling):
    """The shell, the camera and the floor. `ceiling` adds what glows."""
    # Inside the room, near the front wall, looking down at the floor. The
    # walls are at +/- HALF, so a camera any further back is outside the box
    # photographing the unlit back of a wall -- which renders, correctly, as
    # a black frame.
    s.entity("Camera", position=(0, 2.2, HALF - 0.9), rotation=(-0.42, 0, 0))
    s.block("CameraComponent", [
        ("ViewRank", 0), ("FixedAspectRatio", "false"),
        ("ProjectionType", "Perspective"), ("PerspectiveFOV", 55),
        ("PerspectiveNearClip", 0.05), ("PerspectiveFarClip", 100),
        ("OrthographicScale", 10), ("OrthographicNearClip", -1),
        ("OrthographicFarClip", 1),
    ])

    s.entity("Floor", position=(0, 0, 0), scale=(HALF * 2, 1, HALF * 2))
    s.mesh(PLANE, mat["floor"])

    for name, position, scale in (
            ("Wall Left", (-HALF, CEILING * 0.5, 0), (0.2, CEILING, HALF * 2)),
            ("Wall Right", (HALF, CEILING * 0.5, 0), (0.2, CEILING, HALF * 2)),
            ("Wall Back", (0, CEILING * 0.5, -HALF), (HALF * 2, CEILING, 0.2)),
            ("Wall Front", (0, CEILING * 0.5, HALF), (HALF * 2, CEILING, 0.2))):
        s.entity(name, position=position, scale=scale)
        s.mesh(CUBE, mat["shell"])

    ceiling(s)


def sealed_box(s, mat):
    """A bright emitter inside a closed box, in the corner, lighting nothing.

    Six faces around it, because the point is that **no light escapes**: the
    claim is about an emitter's mere presence in the list, so any light that
    actually reached the floor would answer a different question.
    """
    cx, cz = -HALF + 1.0, -HALF + 1.0
    s.entity("Sealed Emitter", position=(cx, 1.0, cz), scale=(0.3, 0.02, 0.3))
    s.mesh(PLANE, mat["cell"])

    half = 0.6
    for name, offset, scale in (
            ("Box Top", (0, half, 0), (half * 2, 0.1, half * 2)),
            ("Box Bottom", (0, -half, 0), (half * 2, 0.1, half * 2)),
            ("Box Left", (-half, 0, 0), (0.1, half * 2, half * 2)),
            ("Box Right", (half, 0, 0), (0.1, half * 2, half * 2)),
            ("Box Back", (0, 0, -half), (half * 2, half * 2, 0.1)),
            ("Box Front", (0, 0, half), (half * 2, half * 2, 0.1))):
        s.entity(name, position=(cx + offset[0], 1.0 + offset[1], cz + offset[2]),
                 scale=scale)
        s.mesh(CUBE, mat["shell"])


SCENE_VERSION = 6

# **Nothing but the emitters.** No ambient, no sky: every photon on the floor
# came off something that glows, which is what makes the two rooms comparable
# by a single number and what makes a lost contribution show as darkness
# rather than as a small tint.
HEADER = [
    "Scene: Emitters",
    f"Version: {SCENE_VERSION}",
    "Environment:",
    "  AmbientColor: [0, 0, 0]",
    "  AmbientIntensity: 0",
    "  Sky: Color",
    "  SkyHorizon: [0, 0, 0]",
    "  SkyZenith: [0, 0, 0]",
    "  SkyGround: [0, 0, 0]",
    "  SkyIntensity: 0",
    "  SkyRotation: 0",
    "  SkyTexture: 0",
    "Entities:",
]


def scene(mat, kind):
    s = Scene()

    def textured(s):
        s.entity("Ceiling", position=(0, CEILING, 0), rotation=(3.14159265, 0, 0),
                 scale=(HALF * 2, 1, HALF * 2))
        s.mesh(PLANE, mat["holes"])

    def split(s):
        # The same four cells, at the same places and the same size, as their
        # own surfaces. An unlit ceiling behind them, so the room's geometry
        # and its bounce off the ceiling are identical to the textured one's.
        #
        # A **plane**, not a box: a box has thickness, and a cell hung at the
        # ceiling's own height ends up inside it -- sealed in, lighting the
        # inside of a lid. The room then renders black, which is a fixture
        # bug that looks exactly like the estimator losing all the light.
        s.entity("Ceiling", position=(0, CEILING, 0),
                 rotation=(3.14159265, 0, 0), scale=(HALF * 2, 1, HALF * 2))
        s.mesh(PLANE, mat["ceiling"])

        # A hair below the ceiling, so the two are not coplanar -- which is a
        # depth fight, and resolved by draw order rather than by geometry.
        cell = HALF * 2 / CELLS
        for index, (column, row) in enumerate(LIT):
            x = -HALF + (column + 0.5) * cell
            z = -HALF + (row + 0.5) * cell
            s.entity(f"Cell {index}", position=(x, CEILING - 0.02, z),
                     rotation=(3.14159265, 0, 0), scale=(cell, 1, cell))
            s.mesh(PLANE, mat["cell"])

    def uniform(s):
        s.entity("Ceiling", position=(0, CEILING, 0), rotation=(3.14159265, 0, 0),
                 scale=(HALF * 2, 1, HALF * 2))
        s.mesh(PLANE, mat["uniform"])

    def glow(s):
        s.entity("Ceiling", position=(0, CEILING, 0), rotation=(3.14159265, 0, 0),
                 scale=(HALF * 2, 1, HALF * 2))
        s.mesh(PLANE, mat["glow"])

    room(s, mat, {"holes": textured, "split": split, "uniform": uniform,
                  "glow": glow, "glow_plus": glow}[kind])

    if kind == "glow_plus":
        sealed_box(s, mat)

    return chr(10).join(HEADER + [s.text()]) + chr(10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=str(ASSETS / "scenes"))
    args = parser.parse_args()

    textures = ASSETS / "textures"
    textures.mkdir(parents=True, exist_ok=True)
    path = textures / "emitter_holes_albedo.png"
    png(path, holes_texture(768))
    write_meta(path, "Texture")
    print(f"{path.name}: {CELLS}x{CELLS} cells, {len(LIT)} lit")

    mat = materials()
    directory = pathlib.Path(args.output)
    directory.mkdir(parents=True, exist_ok=True)

    for kind in ("holes", "split", "uniform", "glow", "glow_plus"):
        out = directory / f"emitter_{kind}.rage"
        out.write_text(scene(mat, kind), encoding="utf-8")
        write_meta(out, "Scene")
        print(f"{out.name}")


if __name__ == "__main__":
    main()
