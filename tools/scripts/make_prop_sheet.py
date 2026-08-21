#!/usr/bin/env python3
"""Put a camp prop on a plain lit floor beside a ruler, so it can be judged.

    python tools/scripts/make_prop_sheet.py --each
    RageVRuntime --scene=scenes/_verify_chair_frame.rage --screenshot=chair.png
    python tools/scripts/make_prop_sheet.py --clean

**Why this exists, in the owner's words:** *"for each asset create a dummy
scene to verify whether the FBX/GLTF 3D object was created the right way, post
confirmation delete the dummy scene and only then use it in the new demo
scene."*

Quite right, and several renders were wasted proving it. A prop was being
judged through firelight, depth of field, motion blur and a night grade -- so a
chair with a leg modelled inside out and a chair that is simply badly
proportioned look identical, and neither can be told from a chair that is fine
but in shadow. **A model is reviewed under flat light on a plain floor or it is
not being reviewed.**

Three things make the verification scene able to answer the question:

  * **One prop.** Not a row of them. A row answers "do these look consistent",
    which is a different and later question, and it makes every prop small.
  * **A ruler.** A pole in ten-centimetre bands, sized to the prop. Scale is
    what goes wrong most often and is invisible against nothing -- a chair at
    half its height looks exactly like a chair.
  * **Nothing else.** A white material on everything, a plain floor, a plain
    key and fill, and every post effect off. A model review under bloom and a
    grade is a review of the bloom and the grade.

These scenes are scaffolding, and `--clean` removes them. A project full of
leftover verification scenes is a project nobody can find anything in.
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
SCENES = ASSETS / "scenes"

PRIMITIVE_BASE = 0x7261676556000000
CUBE, SPHERE, PLANE, CYLINDER, QUAD = (PRIMITIVE_BASE + i for i in range(5))

# The view. A little off axis and a little above: straight on hides whether the
# thing has a back, and a full three-quarter flatters a silhouette, which is
# what a low-poly prop mostly is.
VIEW_YAW = math.radians(24.0)
VIEW_PITCH = math.radians(-11.0)
VIEW_FOV = 40.0

# **`PerspectiveFOV` is the horizontal angle, not the vertical one**, and the
# first cut of this file assumed the opposite. On a 16:9 frame that is the
# difference between a 40 degree view and a 23 degree one, so every prop was
# computed to fit and then rendered a third too big, sitting on the bottom edge
# with its ruler out of shot.
#
# Solved here once, from the aspect the screenshots are actually taken at, and
# with a margin on top of it -- a review sheet that crops is worse than none,
# because it looks like it did not.
VIEW_ASPECT = 16.0 / 9.0
VIEW_MARGIN = 1.5


# Props that are only one object once they are put together, and the offsets
# that put them together.
#
# **A component can be built correctly and the assembly still be wrong**, and
# the chair proved it: its frame passed on its own sheet and its seat passed on
# its own, and the two side by side were a shape no chair has ever had. One
# FBX carries one material, so a two-coloured object is two files -- which
# means "check each asset on its own" is necessary and not sufficient.
ASSEMBLIES = {
    "chair": (("chair_frame", (0, 0, 0)), ("chair_seat", (0, 0, 0))),
    "tent": (("tent", (0, 0, 0)), ("tent_band", (0, 0, 0)),
             ("tent_door", (0, 0, 0))),
    "fire": (("flame_outer", (0, 0.18, 0)), ("flame_inner", (0, 0.18, 0)),
             ("fire_logs", (0, 0, 0))),
    "lamp": (("lantern", (0, 0, 0)), ("lantern_glass", (0, 0, 0))),
    "mirror": (("mirror_frame", (0, 0, 0)), ("mirror_glass", (0, 0, 0.030))),
    "toast": (("fork", (0, 0, 0)), ("marshmallow", (0, 0.05, 0.80))),
    "van": (("van_body", (0, 0, 0)), ("van_trim", (0, 0, 0)),
            ("van_roof_units", (0, 0, 0)), ("van_stripe_wide", (0, 0, 0)),
            ("van_stripe_thin", (0, 0, 0)), ("van_glass", (0, 0, 0)),
            ("van_lights", (0, 0, 0)), ("van_wheels", (0, 0, 0))),
}


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


def prop_bounds(name):
    """The prop's own bounding box, from the mesh the generator builds.

    Asked of the generator rather than of the FBX, because the generator is
    where the numbers came from -- and because a bounding box read back out of
    the file would be testing the writer rather than the model.
    """
    for other, builder, _, _ in make_camp_models.PROPS:
        if other != name:
            continue
        mesh = builder()
        low = tuple(min(p[i] for p in mesh.points) for i in range(3))
        high = tuple(max(p[i] for p in mesh.points) for i in range(3))
        return low, high
    raise SystemExit(f"no prop named '{name}'")


def look_at(centre, distance, yaw=VIEW_YAW, pitch=VIEW_PITCH):
    """Where a camera has to stand to have `centre` in the middle of frame.

    The engine's zero rotation looks down -Z, so forward is
    (-sin yaw cos pitch, sin pitch, -cos yaw cos pitch) and the position is the
    target walked back along it. Solved once here rather than guessed per
    scene, which is how the first sheet cropped its two tallest props.
    """
    forward = (-math.sin(yaw) * math.cos(pitch),
               math.sin(pitch),
               -math.cos(yaw) * math.cos(pitch))
    return tuple(centre[i] - forward[i] * distance for i in range(3))


def ruler(s, mat, x, height):
    """A pole in ten-centimetre bands, standing beside the prop.

    Bands rather than a scale bar on the floor, because the mistakes that
    actually happen are *height* mistakes -- a chair at 0.5 m, a stump at
    0.2 m, grass at 0.03 m. A pole answers those at a glance.
    """
    bands = max(int(math.ceil(height / 0.1)), 5)
    for i in range(bands):
        s.entity(f"Ruler {i}", position=(x, i * 0.1 + 0.05, 0.0),
                 scale=(0.035, 0.1, 0.035))
        s.mesh(CUBE, mat["rule_pale" if i % 2 else "rule_dark"])

    # A wider mark at every whole metre, so the count does not have to be done
    # band by band.
    for metre in range(1, bands // 10 + 1):
        s.entity(f"Metre {metre}", position=(x, metre * 1.0, 0.0),
                 scale=(0.11, 0.012, 0.11))
        s.mesh(CUBE, mat["rule_mark"])


def lights(s):
    """A plain white key and a soft blue fill from the other side.

    The least opinionated lighting there is: enough to read a form, not enough
    to flatter or hide one.
    """
    s.entity("Key", rotation=(math.radians(-38), math.radians(28), 0))
    s.block("LightComponent", [
        ("Type", "Directional"), ("Color", "[1, 0.98, 0.94]"), ("Intensity", 2.6),
        ("Range", 60), ("InnerCone", 20), ("OuterCone", 30),
    ])

    s.entity("Fill", rotation=(math.radians(-22), math.radians(-140), 0))
    s.block("LightComponent", [
        ("Type", "Directional"), ("Color", "[0.62, 0.7, 0.88]"), ("Intensity", 0.9),
        ("Range", 60), ("InnerCone", 20), ("OuterCone", 30),
    ])


def header(name):
    return [
        f"Scene: {name}",
        # Version 6, because a file without it is read as version 1 and has
        # every entity id replaced. Nothing here is parented so nothing broke,
        # but a verification scene that differs from a real one in how it
        # loads is not verifying the same thing.
        "Version: 6",
        "Environment:",
        "  Ambient: [0.16, 0.17, 0.2]",
        "  AmbientIntensity: 1",
        "  Sky: Gradient",
        "  SkyHorizon: [0.24, 0.26, 0.3]",
        "  SkyZenith: [0.14, 0.16, 0.22]",
        "  SkyGround: [0.1, 0.1, 0.11]",
        "  SkyIntensity: 1",
        "  SkyRotation: 0",
        "Entities:",
    ]


def assembly_bounds(parts):
    """The box round every part of an assembly, each in its placed position."""
    low = [1e9, 1e9, 1e9]
    high = [-1e9, -1e9, -1e9]
    for name, offset in parts:
        part_low, part_high = prop_bounds(name)
        for i in range(3):
            low[i] = min(low[i], part_low[i] + offset[i])
            high[i] = max(high[i], part_high[i] + offset[i])
    return tuple(low), tuple(high)


def build_single(name, handles, profile, mat, parts=None):
    """One prop -- or one assembly -- with a ruler on a plain floor."""
    s = Scene()

    parts = parts or ((name, (0, 0, 0)),)
    low, high = assembly_bounds(parts)
    size = tuple(high[i] - low[i] for i in range(3))

    # The ruler stands beside the prop and is a little taller than it, so what
    # has to fit in frame is both of them together.
    pole = max(size[1], 0.5) * 1.25
    ruler_x = high[0] + max(max(size) * 0.22, 0.12)

    tall = max(size[1], pole, 0.35)
    wide = max(ruler_x - low[0], max(size), 0.35)

    centre = ((low[0] + ruler_x) * 0.5, max(low[1], 0.0) + tall * 0.5, 0.0)

    half_v = math.atan(math.tan(math.radians(VIEW_FOV) * 0.5) / VIEW_ASPECT)
    half_h = math.radians(VIEW_FOV) * 0.5

    distance = max(tall * 0.5 * VIEW_MARGIN / math.tan(half_v),
                   wide * 0.5 * VIEW_MARGIN / math.tan(half_h),
                   0.6)

    position = look_at(centre, distance)
    s.entity("Verify Camera", position=position,
             rotation=(VIEW_PITCH, VIEW_YAW, 0))
    s.block("CameraComponent", [
        ("ViewRank", 0), ("FixedAspectRatio", "false"),
        ("ProjectionType", "Perspective"), ("PerspectiveFOV", VIEW_FOV),
        ("PerspectiveNearClip", 0.02), ("PerspectiveFarClip", 200),
        ("OrthographicScale", 10), ("OrthographicNearClip", -1),
        ("OrthographicFarClip", 1),
        ("PostProfile", profile),
    ])

    s.entity("Floor", position=(0, 0, 0), scale=(40, 1, 40))
    s.mesh(PLANE, mat["floor"])

    for part, offset in parts:
        s.entity(f"Prop {part}", position=offset)
        s.mesh(handles[part], mat["prop"])

    ruler(s, mat, ruler_x, pole)
    lights(s)

    return s.text()


def build_row(profile, mat, handles):
    """Every prop on one row, for comparing proportions between them.

    A second question from the one above, and worth keeping: a chair and a
    stump can each be right on their own sheet and still be wrong beside each
    other.
    """
    s = Scene()

    names = [name for name, _, _, _ in make_camp_models.PROPS if name in handles]
    spacing = 1.6
    width = spacing * max(len(names) - 1, 1)

    distance = max(width * 0.78, 12.0) + 6.0
    s.entity("Sheet Camera", position=(width * 0.5, 3.0, distance),
             rotation=(math.radians(-5.0), 0, 0))
    s.block("CameraComponent", [
        ("ViewRank", 0), ("FixedAspectRatio", "false"),
        ("ProjectionType", "Perspective"), ("PerspectiveFOV", 42),
        ("PerspectiveNearClip", 0.05), ("PerspectiveFarClip", 300),
        ("OrthographicScale", 10), ("OrthographicNearClip", -1),
        ("OrthographicFarClip", 1),
        ("PostProfile", profile),
    ])

    s.entity("Floor", position=(width * 0.5, 0, 0), scale=(120, 1, 120))
    s.mesh(PLANE, mat["floor"])

    for i in range(0, int(width) + 2):
        s.entity(f"Rule {i}", position=(i * 1.0 - 1.0, 0.5, -3.0),
                 scale=(0.02, 1.0, 0.02))
        s.mesh(CUBE, mat["rule_pale"])

    for index, name in enumerate(names):
        s.entity(f"Prop {name}", position=(index * spacing, 0, 0))
        s.mesh(handles[name], mat["prop"])

    lights(s)
    return s.text(), names


def materials():
    directory = ASSETS / "materials"
    return {
        # Flat and mid-grey. A textured floor under a prop being judged for
        # its shape is a distraction with a texture on it.
        "floor": write_material(directory / "sheet_floor.rmat", {}, tiling=(1, 1),
                                roughness=0.95, base_color=(0.19, 0.19, 0.21, 1)),
        "rule_pale": write_material(directory / "sheet_rule.rmat", {}, tiling=(1, 1),
                                    roughness=0.9, base_color=(0.72, 0.72, 0.75, 1)),
        "rule_dark": write_material(directory / "sheet_rule_dark.rmat", {},
                                    tiling=(1, 1), roughness=0.9,
                                    base_color=(0.24, 0.25, 0.3, 1)),
        "rule_mark": write_material(directory / "sheet_rule_mark.rmat", {},
                                    tiling=(1, 1), roughness=0.85,
                                    base_color=(0.86, 0.42, 0.3, 1)),
        # **One material for every prop, and it is white.** The point of the
        # sheet is the geometry; a prop wearing its scene colour is a prop
        # whose shape is being judged through a paint job.
        "prop": write_material(directory / "sheet_prop.rmat", {}, tiling=(1, 1),
                               roughness=0.82, base_color=(0.78, 0.78, 0.8, 1)),
    }


def profile():
    # Everything off.
    return postprofile.write_named(ASSETS / "post" / "sheet.rvpostprofile", {
        "Exposure": 1.0,
        "BloomEnabled": False,
        "AutoExposure": False,
        "DepthOfField": False,
        "MotionBlur": False,
        "AmbientOcclusion": True,
        "AoRadius": 0.3,
        "AoIntensity": 0.7,
        "GlobalIllumination": False,
        "VignetteIntensity": 0.0,
        "ChromaticAberration": 0.0,
        "FilmGrain": 0.0,
    })


def write(path, name, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(chr(10).join(header(name)) + chr(10) + text + chr(10),
                    encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", help="write a verification scene for one prop")
    parser.add_argument("--each", action="store_true",
                        help="write one verification scene per prop")
    parser.add_argument("--assemblies", action="store_true",
                        help="write a scene per multi-mesh assembly")
    parser.add_argument("--clean", action="store_true",
                        help="delete every verification scene")
    args = parser.parse_args()

    if args.clean:
        removed = 0
        for path in sorted(SCENES.glob("_verify_*.rage")):
            path.unlink()
            removed += 1
            meta = path.with_name(path.name + ".meta")
            if meta.exists():
                meta.unlink()
        print(f"removed {removed} verification scene(s)")
        return

    mat = materials()
    post = profile()
    handles = prop_handles()

    missing = [n for n, _, _, _ in make_camp_models.PROPS if n not in handles]
    if missing:
        print("no handle yet -- run the engine once so the registry mints them:")
        print("  " + ", ".join(missing))
        if args.only or args.each:
            return

    if args.assemblies:
        for name, parts in ASSEMBLIES.items():
            path = SCENES / f"_verify_{name}_assembly.rage"
            write(path, f"Verify {name}",
                  build_single(name, handles, post, mat, parts))
            low, high = assembly_bounds(parts)
            print(f"{path.name}   {high[0] - low[0]:.2f} x {high[1] - low[1]:.2f}"
                  f" x {high[2] - low[2]:.2f} m")
        return

    if args.only or args.each:
        names = [args.only] if args.only else [
            name for name, _, _, _ in make_camp_models.PROPS if name in handles]

        for name in names:
            parts = ASSEMBLIES.get(name)
            if parts is None and name not in handles:
                raise SystemExit(f"'{name}' has no handle yet")
            path = SCENES / f"_verify_{name}.rage"
            write(path, f"Verify {name}",
                  build_single(name, handles, post, mat, parts))
            low, high = assembly_bounds(parts or ((name, (0, 0, 0)),))
            print(f"{path.name}   {high[0] - low[0]:.2f} x {high[1] - low[1]:.2f}"
                  f" x {high[2] - low[2]:.2f} m")
        return

    scene, names = build_row(post, mat, handles)
    path = SCENES / "prop_sheet.rage"
    write(path, "Prop sheet", scene)
    print(f"{path}: {len(names)} props on the row")
    print("  " + ", ".join(names))


if __name__ == "__main__":
    main()
