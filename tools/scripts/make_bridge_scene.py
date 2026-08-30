#!/usr/bin/env python3
"""Build the Golden Gate demo scene.

    python tools/scripts/make_bridge_models.py
    (run the runtime or editor once, so the registry mints the .meta files)
    python tools/scripts/make_bridge_scene.py

**Stage 1: the silhouette, and it is lit to be judged rather than to look
like the film.** The finished scene is a night one -- sodium lamps, black sky,
wet road -- and none of that is here yet, on purpose. You cannot judge a shape
in the dark. So this stage wears flat grey under a plain sky, because the only
question it asks is whether the proportions read as the Golden Gate, and
colour, contrast and mood are all ways of not answering it.

**Two cameras, because the shape has two questions.** The deck camera is the
shot the demo is actually for -- standing on the roadway looking down the
span, which is what the reference photograph is. The profile camera stands
2.2 km off the west side and answers the other one: the cable parabola is the
bridge's outline, and from the deck you are inside it and cannot see it.

Pick one with `--camera=<name>` on the runtime.
"""

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from make_demo_scene import Scene, vec  # noqa: E402
import make_bridge_models as bridge  # noqa: E402

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
    for name in ("bridge_towers", "bridge_deck", "bridge_cables"):
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
SEA = (0.035, 0.045, 0.062, 1)


def build():
    mesh = handles()
    s = Scene()

    # --- the cameras --------------------------------------------------------
    #
    # A camera looks down its own -Z, so a camera at +Z with no rotation is
    # looking back along the bridge towards -Z. The deck camera stands in the
    # south side span, 1.4 m over the roadway -- a driver's eye -- with the
    # near tower 310 m ahead and the far one 1.6 km beyond it.
    s.entity("Deck Camera", position=(0.0, bridge.ROADWAY + 1.4, 950.0))
    s.block("CameraComponent", [
        ("ViewRank", 0),
        ("FixedAspectRatio", "false"),
        ("ProjectionType", "Perspective"),
        ("PerspectiveFOV", 50),
        # **5 cm near against 8 km far.** Conventional depth could not tell two
        # surfaces apart at that ratio anywhere past the first kilometre; the
        # engine's depth is reversed, which is what makes this pair spendable.
        ("PerspectiveNearClip", 0.05),
        ("PerspectiveFarClip", 8000),
        ("OrthographicScale", 10),
        ("OrthographicNearClip", -1),
        ("OrthographicFarClip", 1),
        ("PostProfile", 0),
    ])

    # Off the west side, level with the towers' upper third, looking east. Yaw
    # +90 degrees turns the camera's -Z towards -X, which is where the bridge
    # is from here.
    s.entity("Profile Camera", position=(2400.0, 150.0, 0.0),
             rotation=(0.0, 1.5708, 0.0))
    s.block("CameraComponent", [
        ("ViewRank", 1),
        ("FixedAspectRatio", "false"),
        ("ProjectionType", "Perspective"),
        ("PerspectiveFOV", 42),
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
    for name, handle, tint in (
            ("Towers", mesh["bridge_towers"], STEEL),
            ("Deck", mesh["bridge_deck"], STEEL),
            ("Cables", mesh["bridge_cables"], STEEL),
    ):
        s.entity(name)
        s.mesh_inline(handle, tint, 0.0, 0.55)

    # --- the sea ------------------------------------------------------------
    #
    # A WaterComponent rather than a mesh: the strait is a rectangle and the
    # only things worth saying about it are how far it runs. There is no
    # material to give it -- the engine owns the one water material, so this
    # sea is the same sea every other scene gets.
    #
    # 25 m spacing is 40,000 quads over 5 km, which is coarse -- deliberately,
    # until the wave work says what resolution it actually needs. One number.
    s.entity("Sea", position=(0.0, 0.0, 0.0))
    #
    # **Spacing has to be finer than the waves, and by a lot.** A 24 m wave on
    # a 25 m grid has one vertex per crest: it cannot be represented at all and
    # aliases into noise. Four vertices per wavelength is the floor, so 6 m
    # here, and the body is smaller than the 5 km first drafted because 5 km at
    # 6 m is over a million quads. The honest fix for both is water LODs; until
    # then this is the trade, stated rather than hidden.
    s.block("WaterComponent", [
        ("Width", 2200),
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

    # --- light, entirely provisional ----------------------------------------
    #
    # One sun, raking from the south-west so the towers cast along the deck and
    # the cable curve is separated from the sky by shading rather than by
    # outline alone. This is scaffolding for judging shape; the night lighting
    # replaces it wholesale.
    s.entity("Sun", rotation=(-0.61, 0.9, 0.0))
    s.block("LightComponent", [
        ("Type", "Directional"),
        ("Color", vec(1.0, 0.97, 0.92)),
        ("Intensity", 2.1),
        ("Range", 10),
        ("InnerCone", 20),
        ("OuterCone", 44),
        ("CastShadows", "true"),
    ])

    return "\n".join([
        "Scene: Golden Gate at night -- stage 1, the silhouette",
        "Version: 6",
        "Environment:",
        "  AmbientColor: [0.55, 0.62, 0.75]",
        "  AmbientIntensity: 0.14",
        "  Sky: Color",
        "  SkyHorizon: [0.62, 0.68, 0.76]",
        "  SkyZenith: [0.28, 0.42, 0.66]",
        "  SkyGround: [0.16, 0.18, 0.2]",
        "  SkyIntensity: 1",
        "  SkyRotation: 0",
        "  SkyTexture: 0",
        "Entities:",
        s.text(),
        "",
    ])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)

    SCENE.parent.mkdir(parents=True, exist_ok=True)
    with SCENE.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(build())

    print("wrote", SCENE)
    print("  towers at z = +/-{0:.1f}   deck {1:.1f} to {2:.1f}   "
          "roadway {3:.1f} m".format(
              bridge.HALF_SPAN, -bridge.PYLON_X, bridge.PYLON_X, bridge.ROADWAY))
    print("  deck camera 310 m short of the near tower; "
          "profile camera 2.4 km off the west side")


if __name__ == "__main__":
    main()
