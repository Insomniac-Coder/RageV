"""RT2-2d: the many-light scene, at 1k, 4k and 10k realtime lights.

    python tools/scripts/make_many_lights.py            # many_lights_{1000,4000,10000}
    python tools/scripts/bench_scale.py --scenes many_lights_1000 --arms rt,every

**What `lights256.rage` could not answer.** It is 256 shadow-casting points of
one size over an open floor -- the raster clustering benchmark. The questions
the light-selection work (RT2-12, RT2-12b) has to settle need more: thousands
of lights of mixed sizes and reaches, tubes, lights that move, glowing meshes
that are light sources too, and walls between them so a light's range crosses
into rooms it cannot see (the shadow ray, not the cull, has to say no).

**The scene.** A grid of roofless rooms, 10 m apart, walls on two sides of
each so every room is walled off from its neighbours. Each room holds four
Realtime lights, all casting shadows:
  - a ceiling point, 0.03 to 0.25 m in radius, reaching 6 to 16 m -- through
    the walls into the next rooms;
  - a tube (a spot with a length: 1.2 m, pointing down);
  - a wall spot, aimed into the room;
  - a small table lamp, 3 m of reach;
and a table to throw shadows. One room in four has a glowing panel (an
emissive mesh, so an area emitter). The `_moving` twin hangs one room in
eight's lights on a spinning pivot, so they orbit the room, and puts the
camera on a scripted circle round the grid; the still scene keeps both put.
"""
import argparse
import io
import math
import pathlib
import random
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from make_scale_scenes import entity, transform, CUBE, PLANE  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENES = ROOT / "SampleProject" / "assets" / "scenes"

ROOM = 10.0
WALL_HEIGHT = 3.0
LIGHTS_PER_ROOM = 4
MOVING_ROOM_EVERY = 8
PANEL_ROOM_EVERY = 4


def mesh(handle, colour, roughness=0.7, emissive=(0.0, 0.0, 0.0), static=True):
    return f"""    MeshComponent:
      Static: {'true' if static else 'false'}
      Mesh: {handle}
      Material:
        BaseColor: [{colour[0]:.3f}, {colour[1]:.3f}, {colour[2]:.3f}, 1]
        Emissive: [{emissive[0]:.3f}, {emissive[1]:.3f}, {emissive[2]:.3f}, 1]
        Metallic: 0
        Roughness: {roughness}
        Occlusion: 1"""


def light(kind, colour, intensity, reach, radius=0.0, length=0.0, inner=20.0, outer=30.0):
    lines = ["    LightComponent:",
             f"      Type: {kind}",
             f"      Color: [{colour[0]:.3f}, {colour[1]:.3f}, {colour[2]:.3f}]",
             f"      Intensity: {intensity:.3f}",
             f"      Range: {reach:.3f}",
             f"      SourceRadius: {radius:.3f}",
             f"      SourceLength: {length:.3f}",
             f"      InnerCone: {inner:g}",
             f"      OuterCone: {outer:g}",
             "      CastShadows: true",
             "      Mobility: Realtime",
             "      HybridRadius: 2"]
    return "\n".join(lines)


def spin(speed):
    return f"""
    NativeScriptComponent:
      Script: Rotator
      Fields:
        Speed: {speed}"""


def child(parent):
    return f"""    RelationshipComponent:
      Parent: {parent}
"""


def warm_or_cool(rng):
    return rng.choice([(1.0, 0.78, 0.52), (1.0, 0.9, 0.75), (0.75, 0.86, 1.0), (1.0, 0.62, 0.36)])


def build(count, moving):
    rng = random.Random(20261110 + count)
    rooms = math.ceil(count / LIGHTS_PER_ROOM)
    across = math.ceil(math.sqrt(rooms))
    side = across * ROOM
    out = [
        "Scene: Many lights",
        "Version: 6",
        "Environment:",
        "  AmbientColor: [0.06, 0.07, 0.1]",
        "  AmbientIntensity: 0.3",
        "  Sky: 2",
        "  SkyHorizon: [0.05, 0.05, 0.08]",
        "  SkyZenith: [0.01, 0.015, 0.035]",
        "  SkyGround: [0.02, 0.02, 0.02]",
        "  SkyIntensity: 1",
        "  SkyRotation: 0",
        "Entities:",
    ]
    eid = 5_000_000
    camera = """
    CameraComponent:
      ViewRank: 0
      FixedAspectRatio: false
      ProjectionType: Perspective
      PerspectiveFOV: 60
      PerspectiveNearClip: 0.1
      PerspectiveFarClip: 2000
      OrthographicScale: 10
      OrthographicNearClip: -1
      OrthographicFarClip: 1
      PostProfile: 0"""
    # Over the grid's near corner, looking across it. The moving twin hangs
    # the same camera on a pivot at the grid's centre that turns slowly: a
    # scripted path, the same every run.
    height = 18.0
    reach = min(side * 0.42, 120.0)
    pitch = -math.atan2(height, reach * 0.6)
    if moving:
        out.append(entity(11400714819323198484, "Camera path", transform((0.0, 0.0, 0.0)) + spin(0.05)))
        out.append(entity(11400714819323198485, "Scene Camera",
                          child(11400714819323198484) + transform((0.0, height, reach), (pitch, 0.0, 0.0))
                          + camera))
    else:
        out.append(entity(11400714819323198485, "Scene Camera",
                          transform((0.0, height, reach), (pitch, 0.0, 0.0)) + camera))
    out.append(entity(4354685564936845354, "Ground",
                      transform((0, -0.05, 0), scale=(side * 1.2, 1, side * 1.2)) + "\n"
                      + mesh(PLANE, (0.3, 0.3, 0.32), 0.8)))

    lights = 0
    emitters = 0
    for r in range(rooms):
        cx = (r % across - (across - 1) * 0.5) * ROOM
        cz = (r // across - (across - 1) * 0.5) * ROOM
        # Walls on the room's north and east sides, a gap for a door in each.
        for wx, wz, sx, sz in ((cx, cz - ROOM * 0.5, ROOM * 0.62, 0.2), (cx + ROOM * 0.5, cz, 0.2, ROOM * 0.62)):
            out.append(entity(eid, f"Wall {r}", transform((wx, WALL_HEIGHT * 0.5, wz), scale=(sx, WALL_HEIGHT, sz))
                              + "\n" + mesh(CUBE, (0.55, 0.53, 0.5), 0.9)))
            eid += 1
        out.append(entity(eid, f"Table {r}", transform((cx + rng.uniform(-2, 2), 0.4, cz + rng.uniform(-2, 2)),
                                                       (0.0, rng.uniform(0, 3.14), 0.0), (1.4, 0.8, 0.8))
                          + "\n" + mesh(CUBE, (0.45, 0.32, 0.22), 0.6)))
        eid += 1

        orbiting = moving and r % MOVING_ROOM_EVERY == 0
        parent = None
        if orbiting:
            parent = eid
            out.append(entity(eid, f"Room {r} lights", transform((cx, 0.0, cz)) + spin(0.8)))
            eid += 1
            origin = (0.0, 0.0, 0.0)
        else:
            origin = (cx, 0.0, cz)

        def place(local, rotation=(0.0, 0.0, 0.0)):
            position = (origin[0] + local[0], local[1], origin[2] + local[2])
            return (child(parent) if parent is not None else "") + transform(position, rotation)

        colour = warm_or_cool(rng)
        specs = [
            ("ceiling", place((rng.uniform(-2, 2), 2.7, rng.uniform(-2, 2))),
             light("Point", colour, rng.uniform(3.0, 8.0), rng.uniform(6.0, 16.0), radius=rng.uniform(0.03, 0.25))),
            ("tube", place((rng.uniform(-3, 3), 2.8, rng.uniform(-3, 3)), (-1.5708, rng.uniform(0, 3.14), 0.0)),
             light("Spot", (0.85, 0.92, 1.0), 6.0, 10.0, radius=0.03, length=1.2, inner=60, outer=88)),
            ("spot", place((-ROOM * 0.4, 2.4, rng.uniform(-3, 3)), (-0.6, -1.5708, 0.0)),
             light("Spot", colour, 10.0, 12.0, radius=0.05, inner=25, outer=40)),
            ("lamp", place((rng.uniform(-3, 3), 1.0, rng.uniform(-3, 3))),
             light("Point", (1.0, 0.8, 0.55), 1.0, 3.0, radius=0.05)),
        ]
        for name, placed, component in specs:
            if lights >= count:
                break
            out.append(entity(eid, f"Room {r} {name}", placed + "\n" + component))
            eid += 1
            lights += 1

        if r % PANEL_ROOM_EVERY == 0:
            out.append(entity(eid, f"Panel {r}", transform((cx, 2.9, cz + 2.0), scale=(1.6, 0.05, 0.4))
                              + "\n" + mesh(CUBE, (0.9, 0.9, 0.9), 0.5, emissive=(6.0, 5.4, 4.6))))
            eid += 1
            emitters += 1

    return "\n".join(out) + "\n", lights, rooms, emitters


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--counts", default="1000,4000,10000")
    args = parser.parse_args()
    for text in [t for t in args.counts.split(",") if t]:
        count = int(text)
        for moving in (False, True):
            path = SCENES / (f"many_lights_{count}_moving.rage" if moving else f"many_lights_{count}.rage")
            scene, lights, rooms, emitters = build(count, moving)
            io.open(path, "w", encoding="utf-8", newline="").write(scene)
            print(f"  {path.name:30s} {lights} lights in {rooms} rooms, {emitters} glowing panels"
                  + (f", {math.ceil(rooms / MOVING_ROOM_EVERY)} rooms' lights orbiting, camera on a path"
                     if moving else ""))


if __name__ == "__main__":
    main()
