"""RT2-2e: GI scenes where the bounce carries energy, and leak fixtures that can see a sky.

    python tools/scripts/make_gi_bright.py

**Why these exist.** The GI fixtures (`gi_corner` and kin) are lit by one sun
with a black sky and no ambient, and their bounce is worth under one display
level -- too little for any GI change to be judged on. The leak fixtures
(`irradiance_leak*`) are sealed rooms with the same black sky and zero ambient,
so the two known leaks at bake hits -- the sky's slot-0 irradiance and the flat
ambient, both added with no visibility (C-N3) -- have nothing to leak. Rule 5:
no shipped scene is edited, so these are new scenes.

  - `gi_room_bright.rage`: a closed white room, 8 x 3 x 8 m, one red wall, a
    window in the south wall. A strong sun lands a patch on the floor through
    the window and a bright sky shows through it; the camera looks at the back
    of the room, which no direct light reaches. Nearly everything it sees is
    bounce.
  - `irradiance_leak*_sky.rage`: each leak fixture with a grey sky and a
    non-zero ambient, otherwise byte for byte the original.
  - `showroom_daylit.rage`: the garage under a midday sky with a sun, its own
    lights as they are -- daylight comes in wherever the model is open.
"""
import io
import math
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from make_scale_scenes import entity, transform, mesh, CUBE  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENES = ROOT / "SampleProject" / "assets" / "scenes"

WHITE = (0.78, 0.78, 0.76)
RED = (0.75, 0.08, 0.06)

ROOM_X, ROOM_Y, ROOM_Z = 8.0, 3.0, 8.0
WALL = 0.2


def slab(eid, tag, centre, size, colour):
    return entity(eid, tag, transform(centre, scale=size) + "\n" + mesh(CUBE, colour, 0.9))


def bright_room():
    out = [
        "Scene: GI bright room",
        "Version: 6",
        "Environment:",
        "  AmbientColor: [0, 0, 0]",
        "  AmbientIntensity: 0",
        "  Sky: 2",
        "  SkyHorizon: [0.75, 0.82, 0.95]",
        "  SkyZenith: [0.35, 0.55, 0.95]",
        "  SkyGround: [0.35, 0.33, 0.3]",
        "  SkyIntensity: 3",
        "  SkyRotation: 0",
        "Entities:",
    ]
    hx, hz = ROOM_X * 0.5, ROOM_Z * 0.5
    eid = 9_100_000
    parts = [
        ("Floor", (0.0, -WALL * 0.5, 0.0), (ROOM_X + 2 * WALL, WALL, ROOM_Z + 2 * WALL), WHITE),
        ("Ceiling", (0.0, ROOM_Y + WALL * 0.5, 0.0), (ROOM_X + 2 * WALL, WALL, ROOM_Z + 2 * WALL), WHITE),
        ("North wall", (0.0, ROOM_Y * 0.5, -hz - WALL * 0.5), (ROOM_X, ROOM_Y, WALL), WHITE),
        ("West wall (red)", (-hx - WALL * 0.5, ROOM_Y * 0.5, 0.0), (WALL, ROOM_Y, ROOM_Z), RED),
        ("East wall", (hx + WALL * 0.5, ROOM_Y * 0.5, 0.0), (WALL, ROOM_Y, ROOM_Z), WHITE),
    ]
    # The south wall with a window 3 m wide and 1.6 m tall, sill at 0.8 m,
    # offset toward the red wall so the floor patch lands near it.
    window_x0, window_x1, sill, head = -2.5, 0.5, 0.8, 2.4
    z = hz + WALL * 0.5
    parts += [
        ("South wall, left of the window", ((-hx + window_x0) * 0.5, ROOM_Y * 0.5, z),
         (window_x0 + hx, ROOM_Y, WALL), WHITE),
        ("South wall, right of the window", ((window_x1 + hx) * 0.5, ROOM_Y * 0.5, z),
         (hx - window_x1, ROOM_Y, WALL), WHITE),
        ("South wall, under the window", ((window_x0 + window_x1) * 0.5, sill * 0.5, z),
         (window_x1 - window_x0, sill, WALL), WHITE),
        ("South wall, over the window", ((window_x0 + window_x1) * 0.5, (head + ROOM_Y) * 0.5, z),
         (window_x1 - window_x0, ROOM_Y - head, WALL), WHITE),
    ]
    for tag, centre, size, colour in parts:
        out.append(slab(eid, tag, centre, size, colour))
        eid += 1
    # A low block to catch light and shadow in the bounce.
    out.append(slab(eid, "Block", (1.5, 0.45, -1.0), (1.2, 0.9, 1.2), WHITE))
    eid += 1

    # The sun from the south, low enough to reach well into the room.
    out.append(entity(eid, "Sun", transform((0, 0, 0), (-0.55, 0.25, 0.0)) + """
    LightComponent:
      Type: Directional
      Color: [1, 0.95, 0.88]
      Intensity: 6
      Range: 10
      InnerCone: 20
      OuterCone: 30
      CastShadows: true"""))
    eid += 1
    # Inside, by the window, looking at the back of the room.
    out.append(entity(11400714819323198485, "Scene Camera",
                      transform((1.5, 1.6, hz - 0.6), (-0.12, 0.35, 0.0)) + """
    CameraComponent:
      ViewRank: 0
      FixedAspectRatio: false
      ProjectionType: Perspective
      PerspectiveFOV: 75
      PerspectiveNearClip: 0.05
      PerspectiveFarClip: 200
      OrthographicScale: 10
      OrthographicNearClip: -1
      OrthographicFarClip: 1
      PostProfile: 0"""))
    return "\n".join(out) + "\n"


SKY_ENVIRONMENT = """Environment:
  AmbientColor: [0.5, 0.55, 0.65]
  AmbientIntensity: 0.25
  Sky: 2
  SkyHorizon: [0.7, 0.75, 0.85]
  SkyZenith: [0.4, 0.55, 0.85]
  SkyGround: [0.3, 0.3, 0.3]
  SkyIntensity: 1
  SkyRotation: 0
Entities:"""


def leak_with_sky(source):
    text = io.open(source, encoding="utf-8-sig").read()
    swapped, n = re.subn(r"Environment:.*?\nEntities:", SKY_ENVIRONMENT, text, count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f"no Environment block in {source}")
    first = swapped.splitlines()[0]
    return swapped.replace(first, first + " (grey sky, ambient 0.25)", 1)


DAY_ENVIRONMENT = """Environment:
  AmbientColor: [0, 0, 0]
  AmbientIntensity: 0
  Sky: 2
  SkyHorizon: [0.75, 0.82, 0.95]
  SkyZenith: [0.35, 0.55, 0.95]
  SkyGround: [0.35, 0.33, 0.3]
  SkyIntensity: 3
  SkyRotation: 0
Entities:"""

DAY_SUN = """  - EntityID: 9200000000000000001
    TagComponent:
      Tag: Daylight sun
    TransformComponent:
      Position: [0, 0, 0]
      Rotation: [-0.75, 0.6, 0]
      Scale: [1, 1, 1]
    LightComponent:
      Type: Directional
      Color: [1, 0.95, 0.88]
      Intensity: 6
      Range: 10
      InnerCone: 20
      OuterCone: 30
      CastShadows: true
      Mobility: Realtime
"""


def daylit_garage():
    text = io.open(SCENES / "showroom.rage", encoding="utf-8-sig").read()
    swapped, n = re.subn(r"Environment:.*?\nEntities:", DAY_ENVIRONMENT, text, count=1, flags=re.S)
    if n != 1:
        raise SystemExit("no Environment block in showroom.rage")
    swapped = swapped.replace("Scene: The showroom", "Scene: The showroom, daylit", 1)
    return swapped.rstrip("\n") + "\n" + DAY_SUN


def main():
    path = SCENES / "showroom_daylit.rage"
    io.open(path, "w", encoding="utf-8", newline="").write(daylit_garage())
    print(f"  {path.name}")
    path = SCENES / "gi_room_bright.rage"
    io.open(path, "w", encoding="utf-8", newline="").write(bright_room())
    print(f"  {path.name}")
    for source in sorted(SCENES.glob("irradiance_leak*.rage")):
        if source.stem.endswith("_sky"):
            continue
        target = source.with_name(source.stem + "_sky.rage")
        io.open(target, "w", encoding="utf-8", newline="").write(leak_with_sky(source))
        print(f"  {target.name}")


if __name__ == "__main__":
    main()
