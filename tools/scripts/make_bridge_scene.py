#!/usr/bin/env python3
"""Build the Golden Gate demo scene.

    python tools/scripts/make_bridge_models.py
    (run the runtime or editor once, so the registry mints the .meta files)
    python tools/scripts/make_bridge_seabed.py
    python tools/scripts/make_bridge_scene.py

**Stage 1 was the silhouette, and it is still the geometry here.** What has
changed since is what the sea is looked at against. The water component grew
refraction, absorption, a measured depth ramp, caustics and contact foam --
and every one of those measures a distance to a bottom, over a bay that had
none, under a flat grey background that gave the surface nothing to reflect.
So the two things this file now writes, and the reason it takes flags:

- **A sky.** `Sky: Color` draws nothing and reflects nothing: the environment
  cube a `Color` background resolves to is literally black (`Skybox.cpp`),
  so every facet of the sea was mirroring one flat value and no shader could
  make that read as water. `Gradient` costs no asset and gives back a real
  environment, a real irradiance, and a horizon for the specular track to run
  along.
- **A seabed**, from `make_bridge_seabed.py`: the strait's real bathymetry
  and the headlands either side of it, to the surveyed numbers.

Both are switchable, because the point of building them is to look at the
difference: `--sky=flat|dusk|night`, `--seabed=bay|none`.

**Six cameras, and the hero is a flag.** They are entities now rather than a
promote-and-`git checkout` dance, and `--hero=<name>` decides which one gets
ViewRank 0, which is the one the runtime opens. Grazing shots from deck
height always read flat -- 0.85 m waves are sub-pixel in silhouette past
about 200 m -- so a downward pitch is what lets the sea show its shape at
all.

**Cameras that stand on land have no Y written here.** They are placed on
the terrain at generate time, so reshaping the ground moves them with it
instead of leaving them buried in a cliff or hanging over one.
"""

import argparse
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from make_demo_scene import Scene, vec  # noqa: E402
import make_bridge_models as bridge  # noqa: E402
import make_bridge_seabed as seabed  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "SampleProject" / "assets"
MODELS = ASSETS / "models" / "bridge"
SCENE = ASSETS / "scenes" / "GoldenGateDemo.rage"


def read_handle(path):
    """The registry's own handle for an asset, from the `.meta` beside it.

    Read and never pasted: the scene is regenerated whenever a model changes,
    and a handle copied by hand is a handle that goes stale silently -- the
    mesh simply stops being found and the entity draws nothing.
    """
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        if line.startswith("Handle:"):
            return line.split(":", 1)[1].strip()
    return None


def handles():
    found = {}
    for name in ("bridge_towers", "bridge_deck", "bridge_cables",
                 "bridge_road", "bridge_piers", "bridge_paint"):
        meta = MODELS / (name + ".fbx.meta")
        if not meta.exists():
            raise SystemExit(
                "no .meta for {0} -- run make_bridge_models.py, then open the "
                "project once in the runtime or the editor so the registry "
                "mints handles, then run this again.".format(name))
        found[name] = read_handle(meta)
    return found


# Grey, and the same grey for everything. A shape pass that tints the towers
# differently from the deck is a shape pass you can no longer read the
# silhouette off, because the eye follows the colour boundary instead of the
# outline.
# **Four components, not three.** `mesh_inline` writes the tuple straight
# into BaseColor, which is RGBA; a three-component one parses as a failure
# and the surface falls back to white -- which looked like a lighting
# problem and was a missing alpha.
STEEL = (0.55, 0.55, 0.55, 1)


# --- the backgrounds ---------------------------------------------------------
#
# Every colour here is **linear**, which is what the scene file stores and
# what reaches the target. The research quotes the night sky in sRGB hex --
# zenith #0C1620, horizon (city glow) #4A3520 -- and those are converted
# once, here, rather than each time somebody reads them.
#
# The gradient is `mix(horizon, zenith, |y|^0.45)`: the 0.45 means the
# horizon colour is spent within about ten degrees of the horizon, which is
# exactly the band the research calls the city glow, and exactly the band a
# sea reflects at grazing angles.
#
# **The ambient term falls when a real sky arrives**, and this is not a
# taste call. Sky irradiance is added *on top* of AmbientColor, so keeping a
# daylight fill of 0.14 under a dusk gradient lights every surface from a
# sky that is not there and flattens the whole frame.
SKIES = {
    # What stage 1 wore: a flat background to judge a silhouette against.
    # Kept so the difference the sky makes can be measured rather than
    # asserted.
    "flat": {
        "sky": "Color",
        "horizon": (0.62, 0.68, 0.76),
        "zenith": (0.28, 0.42, 0.66),
        "ground": (0.16, 0.18, 0.2),
        "ambient": (0.55, 0.62, 0.75),
        "ambient_intensity": 0.14,
        # The provisional sun: raking from the south-west, scaffolding for
        # judging shape.
        "sun": {"rotation": (-0.61, 0.9, 0.0), "color": (1.0, 0.97, 0.92),
                "intensity": 2.1},
    },
    # Late dusk, and the honest choice *until the lamps exist*. The target is
    # a night scene, but the 128 sodium lamps are build-order item 7: under a
    # true night sky with nothing lit, there is no picture to judge. Dusk
    # keeps a low sun in the sky, which is what the anisotropic Beckmann
    # track needs to show at all.
    "dusk": {
        "sky": "Gradient",
        "horizon": (0.290, 0.140, 0.062),
        "zenith": (0.020, 0.045, 0.105),
        "ground": (0.012, 0.013, 0.015),
        "ambient": (0.35, 0.42, 0.58),
        "ambient_intensity": 0.025,
        # **Low in the west, because that is where it goes.** The handoff
        # wanted the sun beyond the far end of the span so its track ran at
        # the lens; the hero looks south from the Marin cliff, and a sun in
        # the south is not a thing that happens here. West-south-west at 4.5
        # degrees is the real winter sunset through the Gate: the track runs
        # in diagonally from the right, and the towers get the side light
        # that separates them from the sky.
        "sun": {"elevation": 8.0, "bearing": 72.0,
                "color": (1.0, 0.55, 0.28), "intensity": 2.6},
    },
    # The target, previewed. Research numbers exactly; a moon stands in for
    # the sun, and the frame is meant to be nearly black until the lamps go
    # in.
    "night": {
        "sky": "Gradient",
        "horizon": (0.0666, 0.0358, 0.0154),   # #4A3520, the city glow
        "zenith": (0.0037, 0.0091, 0.0154),    # #0C1620, blue 4.2x red
        "ground": (0.004, 0.005, 0.006),
        "ambient": (0.20, 0.26, 0.40),
        "ambient_intensity": 0.012,
        "sun": {"elevation": 34.0, "bearing": 118.0,
                "color": (0.62, 0.70, 0.95), "intensity": 0.30},
    },
}


def sun_rotation(elevation, bearing):
    """Euler radians for a light standing `elevation` degrees over the
    horizon in the compass direction `bearing`.

    Bearing is measured the way this scene's axes run, not the way a compass
    does: **0 is south (+z), 90 is west (+x)**, 180 north, 270 east. So a
    setting sun is somewhere near 90 and the numbers read as the place.

    A light points down its own -Z, and the engine's forward is
    (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch)). Setting that to
    the negative of the direction the sun stands in falls out as pitch =
    -elevation, yaw = bearing, which is why there is no matrix here.
    """
    return (-math.radians(elevation), math.radians(bearing), 0.0)


# --- the cameras -------------------------------------------------------------
#
# A camera looks down its own -Z, so a camera at +Z with no rotation is
# looking back along the bridge towards -Z.
CAMERAS = {
    # The shot the demo is for: standing on the roadway looking down the
    # span, 1.4 m over the deck in the south side span, the near tower 310 m
    # ahead and the far one 1.6 km beyond it. It is also, at deck height, the
    # angle that reads flattest on water.
    "deck": {"tag": "Deck Camera",
             "position": (0.0, bridge.ROADWAY + 1.4, 950.0),
             "rotation": (0.0, 0.0, 0.0), "fov": 50},
    # Off the west side, level with the towers' upper third, looking east.
    # Yaw +90 degrees turns the camera's -Z towards -X.
    "profile": {"tag": "Profile Camera",
                "position": (2400.0, 150.0, 0.0),
                "rotation": (0.0, 1.5708, 0.0), "fov": 42},
    # **Owner-approved framing, 2026-08-30, moved to the side it exists on.**
    # Elevated three-quarter looking down the span: the downward pitch is
    # what lets the sea show its shape. The approved numbers put it 170 m up
    # on the *San Francisco* side, and once the land is the real land there
    # is no 170 m anywhere near Fort Point -- the Presidio bluff is about 74.
    # The viewpoint this shot is is Battery Spencer, on the Marin cliff, so
    # the shot is mirrored across the strait: same pitch, same height, same
    # three-quarter, yaw turned to look back down the span. Y is not written
    # here -- `build` stands it on the ground the terrain actually has.
    "headland": {"tag": "Headland Camera",
                 "position": (300.0, None, -1010.0),
                 "rotation": (-0.155, math.pi - 0.40, 0.0), "fov": 55,
                 "eye": 2.5, "sees": (0.0, 75.0, -640.1)},
    # The San Francisco bluff, kept because it is the approved shot's exact
    # numbers and the two are worth comparing. Stood on the ground for the
    # same reason.
    "bluff": {"tag": "Bluff Camera",
              "position": (45.0, None, 1150.0),
              "rotation": (-0.155, 0.088, 0.0), "fov": 55, "eye": 12.0},
    # **Owner-approved, 2026-08-30.** The pier close-up: the south tower's
    # near footing 107 m off, where the concrete carries under the surface.
    # The one frame that shows a contact-foam ring and bare open water at
    # once, which is where foam changes are judged.
    "pier": {"tag": "Pier Camera",
             "position": (70.0, 4.5, 705.0),
             "rotation": (0.05, 0.82, 0.0), "fov": 55},
    # Under the north tower at Lime Point, looking back across the strait.
    # The one place the water is genuinely shallow enough to see through --
    # the north pier stands on the shore, so the surf against it is the
    # contact-foam rule's own case.
    "lime": {"tag": "Lime Point Camera",
             "position": (185.0, None, -590.0),
             "rotation": (-0.02, 2.50, 0.0), "fov": 55, "eye": 5.5},
}

# Which camera is the hero by default. The deck shot is the demo's subject;
# it is not the shot that answers "does this read as water", and this
# generator exists at the moment to answer that.
DEFAULT_HERO = "headland"


def build(sky_name="dusk", seabed_name="bay", hero=DEFAULT_HERO, grounded=None):
    mesh = handles()
    sky = SKIES[sky_name]
    grounded = {} if grounded is None else grounded
    s = Scene()

    # --- the cameras --------------------------------------------------------
    #
    # All four, always, so a shot is picked rather than rebuilt. Only the
    # hero's rank matters -- the runtime opens the lowest -- but the others
    # keep a stable order so the editor's list does not shuffle.
    order = [hero] + [name for name in CAMERAS if name != hero]
    for rank, name in enumerate(order):
        camera = CAMERAS[name]
        position = camera["position"]
        if position[1] is None:
            # Stood on the land the terrain actually has. A typed-in Y is a
            # camera that ends up buried or hanging the next time the ground
            # is reshaped, and this one has already moved 130 m once.
            if "sees" in camera:
                y = seabed.clear_eye(position[0], position[2], camera["sees"],
                                     camera["eye"])
            else:
                y = max(seabed.height_at(position[0], position[2]), 0.0)                     + camera["eye"]
            position = (position[0], round(y, 2), position[2])
            grounded[name] = position[1]
        s.entity(camera["tag"], position=position,
                 rotation=camera["rotation"])
        s.block("CameraComponent", [
            ("ViewRank", rank),
            ("FixedAspectRatio", "false"),
            ("ProjectionType", "Perspective"),
            ("PerspectiveFOV", camera["fov"]),
            # **5 cm near against 8 km far.** Conventional depth could not
            # tell two surfaces apart at that ratio anywhere past the first
            # kilometre; the engine's depth is reversed, which is what makes
            # this pair spendable.
            ("PerspectiveNearClip", 0.05),
            ("PerspectiveFarClip", 8000),
            ("OrthographicScale", 10),
            ("OrthographicNearClip", -1),
            ("OrthographicFarClip", 1),
            ("PostProfile", 0),
        ])

    # --- the bridge ---------------------------------------------------------
    #
    # Four entities and no more. The transform walk is this engine's real
    # ceiling, and a silhouette made of one entity per member would spend that
    # budget before a single detail existed.
    # **Five entities, one material each.** Stage 1 wore flat grey
    # deliberately -- you cannot read a silhouette off a shape with a colour
    # boundary in it -- and that stage is over: the bridge is International
    # Orange, its piers and pylons are concrete, its roadway is asphalt.
    #
    # A MeshComponent holds one material and a model's own are never used
    # (Scene.cpp resolves the component's handle and falls back to the
    # renderer's default; an FBX exported with a green material rendered
    # bit-identically to one exported orange). So the parts are split by
    # material at export and each gets its own `.rmat`.
    steel = seabed.terrain.handle_for("materials/bridge_orange.rmat")
    concrete = seabed.terrain.handle_for("materials/bridge_concrete.rmat")
    asphalt = seabed.terrain.handle_for("materials/bridge_asphalt.rmat")
    paint = seabed.terrain.handle_for("materials/bridge_paint.rmat")
    for name, part, material in (
            ("Towers", "bridge_towers", steel),
            ("Deck", "bridge_deck", steel),
            ("Cables", "bridge_cables", steel),
            ("Roadway", "bridge_road", asphalt),
            ("Piers", "bridge_piers", concrete),
            ("Markings", "bridge_paint", paint),
    ):
        s.entity(name)
        s.mesh(mesh[part], material)

    # --- the floor of the strait --------------------------------------------
    #
    # A terrain, because a heightfield is what a seabed is: chunked, levelled
    # by distance, and traced at level 0 so the refraction ray and the drawn
    # surface are the same surface. `make_bridge_seabed.py` writes the asset
    # and owns every number in its shape; the scene only has to say where it
    # sits and how tall a full sample stands, and both come back from the
    # generator rather than being copied.
    if seabed_name != "none":
        floor = seabed.dimensions()
        silt, rock, sand, scrub = (seabed.terrain.handle_for(name) for name in (
            "materials/bay_silt.rmat", "materials/bay_rock.rmat",
            "materials/bay_sand.rmat", "materials/bay_scrub.rmat"))
        s.entity("Seabed", position=(0.0, floor["base"], 0.0))
        s.block("TerrainComponent", [
            ("Terrain", floor["handle"]),
            ("Size", "{0:g}".format(floor["size"])),
            ("Height", "{0:.4f}".format(floor["height"])),
            ("Material", silt),
            ("Layer1", rock),
            ("Layer2", sand),
            ("Layer3", scrub),
            ("TextureScale", "{0:g}".format(floor["texture_scale"])),
            # It *is* its own collider, and a bridge demo that lets you fall
            # through the headland is a demo with a hole in it.
            ("Collision", "true"),
        ])

    # --- the sea ------------------------------------------------------------
    #
    # A WaterComponent rather than a mesh: the strait is a rectangle and the
    # only things worth saying about it are how far it runs. There is no
    # material to give it -- the engine owns the one water material, so this
    # sea is the same sea every other scene gets.
    #
    # **Square, and the same 2 800 m as the terrain.** It used to be 2 200
    # across, which was fine over a bottomless bay and is not fine over a
    # floor: 300 m of seabed either side would stand in the open air as a dry
    # pit around the water, and the frame's far corners look straight at it.
    # The cost is honest -- 27% more quads, 871k at 3 m spacing -- and the
    # real fix is water levels of detail, which do not exist yet.
    #
    # **Spacing has to be finer than the waves, and by a lot.** A 24 m wave on
    # a 25 m grid has one vertex per crest: it cannot be represented at all and
    # aliases into noise. Four vertices per wavelength is the floor, so 3 m
    # here.
    s.entity("Sea", position=(0.0, 0.0, 0.0))
    s.block("WaterComponent", [
        ("Width", 2800),
        ("Length", 2800),
        ("Spacing", 3),
        ("TextureScale", 12),
        ("WaveHeight", 0.85),
        ("WaveLength", 48),
        ("Choppiness", 1.25),
        ("WaveSpeed", 1.4),
        ("WaveDirection", 38),
        ("Foam", 0.75),
        ("ShallowColor", vec(0.05, 0.16, 0.19)),
        ("DeepColor", vec(0.008, 0.026, 0.048)),
        ("GradientDepth", 12),
    ])

    # --- the light ----------------------------------------------------------
    #
    # One directional light, placed by the sky it belongs to. Still
    # provisional: the night setup is 128 sodium lamps plus four shadow
    # casters (build order item 7), and this is the one light that lets the
    # water's specular track exist in the meantime.
    lamp = sky["sun"]
    rotation = lamp.get("rotation")
    if rotation is None:
        rotation = sun_rotation(lamp["elevation"], lamp["bearing"])
    s.entity("Sun", rotation=rotation)
    s.block("LightComponent", [
        ("Type", "Directional"),
        ("Color", vec(*lamp["color"])),
        ("Intensity", lamp["intensity"]),
        ("Range", 10),
        ("InnerCone", 20),
        ("OuterCone", 44),
        ("CastShadows", "true"),
    ])

    return "\n".join([
        "Scene: Golden Gate at night -- stage 1, the silhouette",
        "Version: 6",
        "Environment:",
        "  AmbientColor: {0}".format(vec(*sky["ambient"])),
        "  AmbientIntensity: {0:g}".format(sky["ambient_intensity"]),
        "  Sky: {0}".format(sky["sky"]),
        "  SkyHorizon: {0}".format(vec(*sky["horizon"])),
        "  SkyZenith: {0}".format(vec(*sky["zenith"])),
        "  SkyGround: {0}".format(vec(*sky["ground"])),
        "  SkyIntensity: 1",
        "  SkyRotation: 0",
        "  SkyTexture: 0",
        "Entities:",
        s.text(),
        "",
    ])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sky", default="dusk", choices=sorted(SKIES),
                        help="what the sea has to reflect (default: dusk)")
    parser.add_argument("--seabed", default="bay", choices=("bay", "none"),
                        help="bay: the strait's real floor and its headlands; "
                             "none: the bottomless bay stage 1 had, for the A/B")
    parser.add_argument("--hero", default=DEFAULT_HERO, choices=sorted(CAMERAS),
                        help="which camera gets ViewRank 0")
    args = parser.parse_args(argv)

    SCENE.parent.mkdir(parents=True, exist_ok=True)
    grounded = {}
    with SCENE.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(build(args.sky, args.seabed, args.hero, grounded))

    print("wrote", SCENE)
    print("  sky {0}   seabed {1}   hero {2} ({3})".format(
        args.sky, args.seabed, args.hero, CAMERAS[args.hero]["tag"]))
    for name, y in sorted(grounded.items()):
        print("  {0:9s} stands on the ground at y = {1:.2f} m".format(name, y))
    print("  towers at z = +/-{0:.1f}   deck {1:.1f} to {2:.1f}   "
          "roadway {3:.1f} m".format(
              bridge.HALF_SPAN, -bridge.PYLON_X, bridge.PYLON_X, bridge.ROADWAY))


if __name__ == "__main__":
    main()
