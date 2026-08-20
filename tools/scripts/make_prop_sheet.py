#!/usr/bin/env python3
"""Lay every camp prop out on a plain lit floor, so its shape can be judged.

    python tools/scripts/make_prop_sheet.py
    RageVRuntime --scene=scenes/prop_sheet.rage --screenshot=sheet.png

**Why this exists, in the owner's words:** *"instead of directly placing them
in the scene create a temp dummy scene to analyse how it looks and then place
it in the scene."*

Quite right, and three renders were wasted proving it. A prop was being judged
through firelight, depth of field, motion blur and a night grade -- so a chair
with a leg modelled inside out and a chair that is simply badly proportioned
look identical, and neither can be told from a chair that is fine but in
shadow. **A model is reviewed under flat light on a plain floor or it is not
being reviewed.**

So: one row of props, evenly spaced, on a neutral floor, under a plain white
key and a soft fill, with every post effect off and no grade. The camera is
side-on and level, because a silhouette is what a low-poly prop *is* and a
three-quarter view flatters it.

A metre grid of pale posts runs behind the row. Scale is the thing that goes
wrong most often and is hardest to see against nothing.
"""

import argparse
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import postprofile                                       # noqa: E402
from make_demo_scene import Scene, write_material        # noqa: E402
import make_camp_models                                  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "SampleProject" / "assets"

PRIMITIVE_BASE = 0x7261676556000000
CUBE, SPHERE, PLANE, CYLINDER, QUAD = (PRIMITIVE_BASE + i for i in range(5))

SPACING = 2.2


def prop_handles():
    """Every camp prop's handle, read from the `.meta` the registry minted.

    Read rather than pasted: this file exists to be run after a model changes,
    and a table of handles copied by hand is a table that goes stale exactly
    when it matters.
    """
    handles = {}
    directory = ASSETS / "models" / "camp"

    for name, _, _, _ in make_camp_models.PROPS:
        meta = directory / (name + ".fbx.meta")
        if not meta.exists():
            continue
        for line in meta.read_text(encoding="utf-8").splitlines():
            if line.startswith("Handle:"):
                handles[name] = int(line.split(":", 1)[1].strip())
                break

    return handles


def build(profile, mat, handles):
    s = Scene()

    names = [name for name, _, _, _ in make_camp_models.PROPS if name in handles]
    width = SPACING * max(len(names) - 1, 1)

    # Side on and level. A three-quarter view flatters a shape; a silhouette
    # is what a low-poly prop actually is.
    # Far enough back that the whole row fits. The first sheet put the camera
    # at fifteen metres and cropped the two tallest props out of frame
    # entirely -- a review sheet that does not show everything is worse than
    # none, because it looks like it did.
    #
    # Half the visible width is about 0.68 x the distance at this FOV and
    # aspect, so the distance follows from the row rather than being guessed.
    distance = max(width * 0.78, 12.0) + 6.0
    s.entity("Sheet Camera", position=(width * 0.5, 3.2, distance),
             rotation=(math.radians(-5.0), 0, 0))
    s.block("CameraComponent", [
        ("ViewRank", 0), ("FixedAspectRatio", "false"),
        ("ProjectionType", "Perspective"), ("PerspectiveFOV", 42),
        ("PerspectiveNearClip", 0.05), ("PerspectiveFarClip", 200),
        ("OrthographicScale", 10), ("OrthographicNearClip", -1),
        ("OrthographicFarClip", 1),
        ("PostProfile", profile),
    ])

    s.entity("Floor", position=(width * 0.5, 0, 0), scale=(60, 1, 60))
    s.mesh(PLANE, mat["floor"])

    # A metre grid behind the row. Scale is what goes wrong most often and is
    # invisible against nothing.
    for i in range(0, 13):
        s.entity(f"Rule {i}", position=(i * 1.0 - 2.0, 0.5, -3.0),
                 scale=(0.02, 1.0, 0.02))
        s.mesh(CUBE, mat["rule"])

    for index, name in enumerate(names):
        x = index * SPACING
        s.entity(f"Prop {name}", position=(x, 0, 0))
        s.mesh(handles[name], mat["prop"])

    # A plain white key and a soft blue fill from the other side, which is the
    # least opinionated lighting there is: enough to read a form, not enough to
    # flatter or hide one.
    s.entity("Key", rotation=(math.radians(-38), math.radians(28), 0))
    s.block("LightComponent", [
        ("Type", "Directional"), ("Color", "[1, 0.98, 0.94]"), ("Intensity", 2.6),
        ("Range", 60), ("InnerCone", 20), ("OuterCone", 30),
    ])

    s.entity("Fill", rotation=(math.radians(-22), math.radians(-140), 0))
    s.block("LightComponent", [
        ("Type", "Directional"), ("Color", "[0.62, 0.7, 0.88]"), ("Intensity", 0.8),
        ("Range", 60), ("InnerCone", 20), ("OuterCone", 30),
    ])

    return s.text(), names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default=str(ASSETS / "scenes" / "prop_sheet.rage"))
    args = parser.parse_args()

    materials = ASSETS / "materials"
    mat = {
        # Flat and mid-grey. A textured floor under a prop being judged for
        # its shape is a distraction with a texture on it.
        "floor": write_material(materials / "sheet_floor.rmat", {}, tiling=(1, 1),
                                roughness=0.95, base_color=(0.17, 0.17, 0.19, 1)),
        "rule": write_material(materials / "sheet_rule.rmat", {}, tiling=(1, 1),
                               roughness=0.9, base_color=(0.62, 0.62, 0.66, 1)),
        # **One material for every prop, and it is white.** The point of the
        # sheet is the geometry; a prop wearing its scene colour is a prop
        # whose shape is being judged through a paint job.
        "prop": write_material(materials / "sheet_prop.rmat", {}, tiling=(1, 1),
                               roughness=0.82, base_color=(0.78, 0.78, 0.8, 1)),
    }

    # Everything off. A model review under bloom and a grade is a review of the
    # bloom and the grade.
    profile = postprofile.write_named(ASSETS / "post" / "sheet.rvpostprofile", {
        "Exposure": 1.0,
        "BloomEnabled": False,
        "AutoExposure": False,
        "DepthOfField": False,
        "MotionBlur": False,
        "AmbientOcclusion": True,
        "AoRadius": 0.4,
        "AoIntensity": 0.7,
        "GlobalIllumination": False,
        "VignetteIntensity": 0.0,
        "ChromaticAberration": 0.0,
        "FilmGrain": 0.0,
    })

    handles = prop_handles()
    scene, names = build(profile, mat, handles)

    header = [
        "Scene: Prop sheet",
        "Environment:",
        "  Ambient: [0.09, 0.1, 0.12]",
        "  AmbientIntensity: 1",
        "  Sky: Gradient",
        "  SkyHorizon: [0.2, 0.22, 0.26]",
        "  SkyZenith: [0.11, 0.13, 0.18]",
        "  SkyGround: [0.09, 0.09, 0.1]",
        "  SkyIntensity: 1",
        "  SkyRotation: 0",
        "Entities:",
    ]

    path = pathlib.Path(args.output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(chr(10).join(header) + chr(10) + scene + chr(10),
                    encoding="utf-8")

    print(f"{path}: {len(names)} props on the row")
    print("  " + ", ".join(names))
    missing = [n for n, _, _, _ in make_camp_models.PROPS if n not in handles]
    if missing:
        print(f"  no handle yet (run the engine once): {', '.join(missing)}")


if __name__ == "__main__":
    main()
