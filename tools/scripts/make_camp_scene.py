#!/usr/bin/env python3
"""Build the camp scene: a forest clearing at night, and a camera that shows it.

    python tools/scripts/make_camp_scene.py

**The place.** A clearing in a pine forest, well after sunset. A ridge tent
pitched on the flat ground at one side. A fire burning down in a ring of
stones, with the wood for it stacked beside. Two folding chairs facing it, one
pushed back. A fallen trunk somebody has been sitting on. A pack and a bedroll
against the trunk. A fox that has come to the edge of the firelight and
stopped. Grass, stones and scrub scattered where the clearing gives way to the
trees.

Everything below is in that description, and the description is the argument
for it. The rule the courtyard scene set holds here: **nothing goes in because
it is new -- it goes in because the place would have one.**

| The clearing has | Which exercises |
|---|---|
| Sculpted ground | Terrain: heightfield, layered materials, LOD chunks |
| Sixty-odd pines, stones, tufts | FBX import, and instanced draws -- one mesh, many entities |
| A tent, chairs, a trunk, a pack | More FBX, and the flat-shaded low-poly look |
| A fox at the treeline | glTF import, skinning, an animation clip |
| A fire | Additive particles on curves, a point light that flickers, looping spatial audio |
| Fireflies | A second emitter, in world space, drifting |
| Night | A dark sky, a moon that is one dim directional light, and every post effect earning its place |
| A camera that moves | A script, four framed shots, and eased motion between them |

**Why a camera that moves.** A still frame of a scene is a screenshot; the
thing an engine has to show is that the scene holds up from more than one
angle. Four shots -- the establishing wide, the tent, low across the fire, and
a slow push toward the fox -- and the script eases between them, so the
demo introduces the place the way a film would rather than sitting in it.
"""

import argparse
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import postprofile                       # noqa: E402
from make_demo_scene import Scene, handle_for, vec, write_material, MAPS  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "SampleProject" / "assets"

PRIMITIVE_BASE = 0x7261676556000000
CUBE, SPHERE, PLANE, CYLINDER, QUAD = (PRIMITIVE_BASE + i for i in range(5))

# --- the props, by handle -----------------------------------------------------
#
# Minted by the registry from the `.meta` beside each `.fbx`, which is what
# lets the scene survive the models folder being reorganised.
PROP = {
    "chair": 16351542612800723582,
    "firewood": 370120828595532354,
    "grass": 907700210567673406,
    "log": 487663307951107120,
    "pack": 10534480071590883843,
    "pine": 14553671508285180132,
    "pine_small": 18067670757081584947,
    "rock": 3804336139711518416,
    "stump": 9064280038167848881,
    "tent_back": 10198568704574168710,
    "tent_side": 2184298104756860506,
    "mirror_frame": 15121154029924751065,
    "mirror_glass": 4253280667872139428,
    "pine_trunk": 8002729216336682092,
}

MESH_FOX = 11679010045657754579
MAT_FOX = 14845622431998451938
# The clearing, made for this scene: level where the camp is, lifting into
# forest at the edges. `hills` was tried first and put the tent inside a
# hillside -- a landscape has no flat part, and a camp needs one.
TERRAIN_CLEARING = 2908146728069828335

TEX_FLAME = 14156592944927634151
TEX_SKY = 17858281879166177050
SFX_FIRE = 5810587725358736489
CURVE_FLAME_SIZE = 4167849381279018650
CURVE_FLAME_ALPHA = 11461028735333202510
GRADIENT_EMBER = 14237806248374965933


def scatter(seed, count, inner, outer):
    """Points in a ring, from a fixed sequence.

    A hash rather than `random`: the scene has to regenerate identically or
    every run is a diff, and a seeded generator whose implementation can change
    between Python versions is not a guarantee.
    """
    points = []
    for i in range(count):
        h = (seed * 6364136223846793005 + i * 1442695040888963407) & 0xFFFFFFFF
        angle = (h % 10000) / 10000.0 * math.tau
        radius = inner + ((h >> 13) % 10000) / 10000.0 * (outer - inner)
        spin = ((h >> 7) % 10000) / 10000.0 * math.tau
        size = 0.82 + ((h >> 19) % 10000) / 10000.0 * 0.5
        points.append((math.cos(angle) * radius, math.sin(angle) * radius,
                       spin, size))
    return points


def build(profile_handle, mat):
    s = Scene()

    # --- the camera ----------------------------------------------------------
    #
    # Starts on the establishing wide. The script drives it from here; the
    # numbers below are shot 1 so that a still of frame zero is already a
    # composition rather than wherever the entity happened to be.
    s.entity("Camp Camera", position=(6.2, 3.1, 6.6),
             rotation=(math.radians(-17.0), math.radians(43.0), 0))
    s.block("CameraComponent", [
        ("ViewRank", 0), ("FixedAspectRatio", "false"),
        ("ProjectionType", "Perspective"), ("PerspectiveFOV", 46),
        ("PerspectiveNearClip", 0.05), ("PerspectiveFarClip", 500),
        ("OrthographicScale", 10), ("OrthographicNearClip", -1),
        ("OrthographicFarClip", 1),
        ("PostProfile", profile_handle),
    ])
    # The camera is what the cinematic script moves, so the script is on it.
    s.script("CampCamera", HoldSeconds=3.4, TravelSeconds=4.2)

    # --- the ground ----------------------------------------------------------
    #
    # A real heightfield rather than a plane, because the clearing is on a
    # hillside and the trees behind it need to be *above* the ones in front.
    # Dropped so the camp sits in a hollow of it; collision on, since this is
    # the surface everything in the scene stands on.
    s.entity("Ground", position=(0, -2.6, 0))
    s.block("TerrainComponent", [
        ("Terrain", TERRAIN_CLEARING),
        ("Size", 190), ("Height", 30),
        ("Material", mat["dirt"]), ("Layer1", mat["moss"]),
        ("Layer2", 0), ("Layer3", 0),
        ("TextureScale", 30), ("Collision", "true"),
    ])

    # --- the fire ------------------------------------------------------------
    #
    # The centre of the composition and the only real light in the scene, so
    # everything else is placed by where its light falls.

    s.entity("Fire Stones")
    for i, (x, z, spin, size) in enumerate(scatter(11, 9, 0.62, 0.78)):
        s.entity(f"Fire Stone {i}", parent="Fire Stones",
                 position=(x, 0.02, z), rotation=(0, spin, 0),
                 scale=(0.42 * size, 0.3 * size, 0.42 * size))
        s.mesh(PROP["rock"], mat["stone"])

    s.entity("Fire Logs", position=(0, 0.06, 0))
    for i, yaw in enumerate((0.4, 1.9, 3.3, 4.9)):
        s.entity(f"Fire Log {i}", parent="Fire Logs",
                 position=(math.cos(yaw) * 0.18, 0.04 + i * 0.03,
                           math.sin(yaw) * 0.18),
                 rotation=(math.radians(74), yaw, 0), scale=(0.5, 0.28, 0.5))
        s.mesh(PROP["log"], mat["bark"])

    # The flame. Additive, on the curves the courtyard authored, and the light
    # is on the same entity so a flicker moves both together.
    s.entity("Fire", position=(0, 0.22, 0))
    s.block("ParticleEmitterComponent", [
        ("Emit", "true"), ("Rate", 46), ("Burst", 0), ("Lifetime", 0.85),
        ("LifetimeJitter", 0.35), ("Direction", "[0, 1, 0]"), ("Spread", 16),
        ("Speed", 1.5), ("SpeedJitter", 0.5), ("Gravity", "[0, 1.1, 0]"),
        ("Drag", 1.4), ("SizeStart", 0.42), ("SizeEnd", 0.06),
        ("ColorStart", "[1, 0.86, 0.42, 1]"), ("ColorEnd", "[1, 0.3, 0.05, 0]"),
        ("Spin", 0.6), ("Facing", "Billboard"), ("Blend", "Additive"),
        ("Space", "Local"), ("Texture", TEX_FLAME), ("MaxParticles", 320),
        ("SimulateOnGpu", "false"),
        ("SizeCurve", CURVE_FLAME_SIZE), ("ColorGradient", GRADIENT_EMBER),
        ("AlphaCurve", CURVE_FLAME_ALPHA),
    ])
    s.block("LightComponent", [
        ("Type", "Point"), ("Color", "[1, 0.62, 0.3]"), ("Intensity", 34),
        ("Range", 18), ("InnerCone", 20), ("OuterCone", 30),
    ])
    s.block("AudioSourceComponent", [
        ("Clip", SFX_FIRE), ("Bus", "Sfx"), ("Volume", 0.5), ("Pitch", 1),
        ("Loop", "true"), ("PlayOnAwake", "true"), ("Stream", "false"),
        ("Spatial", "true"), ("MinDistance", 2.5), ("MaxDistance", 30),
    ])
    # The flicker, which is what stops the firelight reading as a lamp.
    s.script("Flicker", Amount=0.22, Rate=9.0, Base=34.0)

    s.entity("Firewood", position=(1.35, 0.0, -0.55), rotation=(0, 0.7, 0))
    s.mesh(PROP["firewood"], mat["bark"])

    # --- the tent ------------------------------------------------------------
    #
    # Pitched off to one side with its opening toward the fire, which is where
    # anybody would put it and also what keeps the dark doorway facing the
    # camera on two of the four shots.
    tent_yaw = math.radians(-28.0)
    s.entity("Tent", position=(-3.4, 0.0, -2.2), rotation=(0, tent_yaw, 0))

    for name, sign in (("Tent Left", -1.0), ("Tent Right", 1.0)):
        s.entity(name, parent="Tent",
                 position=(sign * 0.62, 0.0, 0.0),
                 rotation=(0, 0, math.radians(sign * 22.0)),
                 scale=(1, 1, 1))
        s.mesh(PROP["tent_side"], mat["canvas"])

    s.entity("Tent Back", parent="Tent", position=(0, 0.0, -1.5))
    s.mesh(PROP["tent_back"], mat["canvas_pale"])

    # --- the mirror ----------------------------------------------------------
    #
    # **The one prop where this engine's three reflection paths are visibly
    # different from each other**, which is why it is here at all: the probe
    # gives it the trees and the sky, screen-space reflections give it what is
    # on screen a frame late, and the traced form gives it what is *behind the
    # camera* as well. A scene with nothing polished in it has nothing to say
    # about any of them.
    #
    # Standing on its own kickstand beside the tent and turned toward the
    # fire, which is the whole difference between reflecting a canvas wall and
    # reflecting the thing the scene is about.
    mirror_yaw = math.radians(28.0)
    s.entity("Mirror", position=(-2.15, 0.0, 0.35), rotation=(0, mirror_yaw, 0))
    s.mesh(PROP["mirror_frame"], mat["bark"])

    s.entity("Mirror Glass", parent="Mirror", position=(0, 0, 0.042))
    s.mesh(PROP["mirror_glass"], mat["mirror"])

    # Its own probe, at the glass rather than at the camp's centre. A probe
    # captures from a point, and a mirror reflecting a cubemap taken three
    # metres away shows a room it is not standing in.
    s.entity("Mirror Probe", parent="Mirror", position=(0, 0.7, 0.1))
    s.block("ReflectionProbeComponent", [
        ("Update", "Realtime"), ("Resolution", 128), ("Influence", 8),
        ("NearClip", 0.05), ("FarClip", 80), ("Rate", "15Hz"),
        ("FacesPerFrame", 1),
    ])

    # --- what people sit on --------------------------------------------------

    for name, pos, yaw in (("Chair Near", (0.6, 0.0, 2.1), math.radians(-168.0)),
                           ("Chair Far", (2.5, 0.0, 0.9), math.radians(-104.0))):
        s.entity(name, position=pos, rotation=(0, yaw, 0))
        s.mesh(PROP["chair"], mat["fabric"])

    s.entity("Bench Log", position=(-1.9, 0.22, 1.5),
             rotation=(math.radians(90), math.radians(18.0), 0))
    s.mesh(PROP["log"], mat["bark"])

    s.entity("Pack", position=(-2.7, 0.0, 1.05), rotation=(0, math.radians(38), 0))
    s.mesh(PROP["pack"], mat["fabric"])

    s.entity("Stump", position=(1.9, 0.0, 2.4), rotation=(0, 0.8, 0))
    s.mesh(PROP["stump"], mat["bark"])

    # --- the fox -------------------------------------------------------------
    #
    # At the edge of the firelight, turned in. The only thing here that is
    # alive, and the whole of the skinning story.
    s.entity("Fox", position=(3.3, 0.0, 2.9), rotation=(0, math.radians(-142), 0),
             scale=(0.011, 0.011, 0.011))
    s.mesh(MESH_FOX, MAT_FOX)
    s.block("AnimatorComponent", [
        ("Clip", 0), ("Playing", "true"), ("Loop", "true"), ("Speed", 0.55),
        ("BlendTime", 0.4),
    ])

    # --- the forest ----------------------------------------------------------
    #
    # A ring of pines around the clearing, and a second sparser ring behind it
    # so the treeline has depth rather than being a fence. One mesh and one
    # material for all of them, which is the case instanced drawing is for.
    s.entity("Forest")
    # Canopy and trunk are separate entities because one FBX carries one
    # material here, and a green trunk is the detail that gives a low-poly
    # tree away.
    for i, (x, z, spin, size) in enumerate(scatter(3, 40, 8.5, 21.0)):
        s.entity(f"Pine {i}", parent="Forest", position=(x, -0.25, z),
                 rotation=(0, spin, 0), scale=(size, size * 1.05, size))
        s.mesh(PROP["pine"], mat["needle"])

        s.entity(f"Pine Trunk {i}", parent=f"Pine {i}")
        s.mesh(PROP["pine_trunk"], mat["trunk"])

    for i, (x, z, spin, size) in enumerate(scatter(7, 30, 5.8, 14.0)):
        s.entity(f"Sapling {i}", parent="Forest", position=(x, -0.2, z),
                 rotation=(0, spin, 0), scale=(size, size, size))
        s.mesh(PROP["pine_small"], mat["needle"])

        s.entity(f"Sapling Trunk {i}", parent=f"Sapling {i}",
                 scale=(0.8, 0.7, 0.8))
        s.mesh(PROP["pine_trunk"], mat["trunk"])

    # --- the ground cover ----------------------------------------------------
    #
    # Tufts and stones, scattered out from the clearing. They are what stops
    # the ground reading as a floor, which in the reference is most of why the
    # place looks like somewhere rather than like a stage.
    s.entity("Scatter")
    for i, (x, z, spin, size) in enumerate(scatter(23, 90, 1.6, 15.0)):
        s.entity(f"Tuft {i}", parent="Scatter", position=(x, 0.0, z),
                 rotation=(0, spin, 0), scale=(size, size * 1.3, size))
        s.mesh(PROP["grass"], mat["blade"])

    for i, (x, z, spin, size) in enumerate(scatter(41, 34, 2.2, 16.0)):
        s.entity(f"Stone {i}", parent="Scatter", position=(x, 0.0, z),
                 rotation=(0, spin, 0),
                 scale=(size * 0.5, size * 0.34, size * 0.5))
        s.mesh(PROP["rock"], mat["stone"])

    # --- the night -----------------------------------------------------------
    #
    # One dim directional light standing in for the moon: enough to separate
    # the trees from the sky and to give the ground a direction, and nowhere
    # near enough to compete with the fire. A night scene lit only by its fire
    # is a black rectangle with an orange dot in it.
    s.entity("Moon", rotation=(math.radians(-124), math.radians(31), 0))
    s.block("LightComponent", [
        ("Type", "Directional"), ("Color", "[0.42, 0.55, 0.9]"), ("Intensity", 0.09),
        ("Range", 60), ("InnerCone", 20), ("OuterCone", 30),
    ])

    # Fireflies: a second emitter, world space so they stay where they drift
    # rather than riding the entity, and slow enough to read as insects.
    s.entity("Fireflies", position=(0, 1.1, 0))
    s.block("ParticleEmitterComponent", [
        ("Emit", "true"), ("Rate", 7), ("Burst", 0), ("Lifetime", 5.5),
        ("LifetimeJitter", 0.5), ("Direction", "[0, 1, 0]"), ("Spread", 180),
        ("Speed", 0.35), ("SpeedJitter", 0.8), ("Gravity", "[0, 0.02, 0]"),
        ("Drag", 0.9), ("SizeStart", 0.05), ("SizeEnd", 0.05),
        ("ColorStart", "[0.85, 1, 0.5, 0]"), ("ColorEnd", "[0.7, 1, 0.35, 0]"),
        ("Spin", 0), ("Facing", "Billboard"), ("Blend", "Additive"),
        ("Space", "World"), ("Texture", TEX_FLAME), ("MaxParticles", 64),
        ("SimulateOnGpu", "false"),
        ("SizeCurve", 0), ("ColorGradient", 0), ("AlphaCurve", CURVE_FLAME_ALPHA),
    ])

    return s.text()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default=str(ASSETS / "scenes" / "camp.rage"))
    args = parser.parse_args()

    materials = ASSETS / "materials"
    mat = {
        # The ground the camp stands on, and the scrub on the tops. Soil maps,
        # tiled large: the terrain's own TextureScale multiplies these.
        "dirt": write_material(materials / "camp_dirt.rmat", MAPS["soil"],
                               tiling=(1.0, 1.0), height_scale=0.02,
                               base_color=(0.42, 0.33, 0.3, 1)),
        "moss": write_material(materials / "camp_moss.rmat", MAPS["soil"],
                               tiling=(1.0, 1.0), height_scale=0.01,
                               roughness=0.95, base_color=(0.3, 0.36, 0.24, 1)),

        # The props wear flat colours over the wood maps: low-poly is a look
        # that comes from the *shading*, and a photographic albedo on a
        # twelve-face tree fights it.
        "needle": write_material(materials / "camp_needle.rmat", {},
                                 tiling=(1, 1), roughness=0.92,
                                 base_color=(0.15, 0.3, 0.25, 1)),
        "trunk": write_material(materials / "camp_trunk.rmat", {},
                                tiling=(1, 1), roughness=0.95,
                                base_color=(0.3, 0.19, 0.12, 1)),
        "bark": write_material(materials / "camp_bark.rmat", MAPS["wood"],
                               tiling=(0.6, 0.6), height_scale=0.01,
                               roughness=0.88, base_color=(0.46, 0.32, 0.22, 1)),
        "stone": write_material(materials / "camp_stone.rmat", {},
                                tiling=(1, 1), roughness=0.8,
                                base_color=(0.35, 0.35, 0.4, 1)),
        "blade": write_material(materials / "camp_blade.rmat", {},
                                tiling=(1, 1), roughness=0.9,
                                base_color=(0.32, 0.6, 0.26, 1)),
        "canvas": write_material(materials / "camp_canvas.rmat", {},
                                 tiling=(1, 1), roughness=0.85,
                                 base_color=(0.78, 0.16, 0.18, 1)),
        "canvas_pale": write_material(materials / "camp_canvas_pale.rmat", {},
                                      tiling=(1, 1), roughness=0.85,
                                      base_color=(0.9, 0.88, 0.85, 1)),
        "fabric": write_material(materials / "camp_fabric.rmat", {},
                                 tiling=(1, 1), roughness=0.9,
                                 base_color=(0.22, 0.24, 0.42, 1)),

        # The glass. Metal at almost zero roughness, which is what a mirror
        # *is* -- silvered glass is a metal surface with a pane in front of it,
        # and modelling the pane separately would buy a refraction this
        # renderer does not do.
        #
        # Not pure white: a real mirror returns about 95 % and a perfect one
        # looks wrong beside surfaces that do not.
        "mirror": write_material(materials / "camp_mirror.rmat", {},
                                 tiling=(1, 1), metallic=1.0, roughness=0.03,
                                 base_color=(0.95, 0.96, 0.97, 1)),
    }

    # Night, and every effect chosen for the picture. Bloom carries the fire,
    # depth of field puts the treeline behind the camp, and the grade is left
    # to the LUT the courtyard authored -- one warm-shadowed look, which is
    # what a fire at night wants anyway.
    profile = postprofile.write_named(ASSETS / "post" / "camp.rvpostprofile", {
        # **Fixed, not automatic.** Auto exposure hunts for a mid-grey and
        # finds one in any scene, so a night scene comes back correctly
        # exposed and therefore not dark. The fire is the subject; it should
        # be the only thing near white in the frame.
        "Exposure": 1.0,
        "BloomEnabled": True,
        "BloomThreshold": 0.9,
        "BloomKnee": 0.55,
        "BloomIntensity": 0.11,
        "BloomClamp": 30.0,

        "DepthOfField": True,
        "FocusDistance": 9.0,
        "FocalLength": 46.0,
        "Aperture": 2.4,
        "MaxBokehRadius": 12.0,

        "AmbientOcclusion": True,
        "AoRadius": 0.6,
        "AoIntensity": 0.9,

        "GlobalIllumination": True,
        "GiRadius": 2.5,
        "GiQuality": "Low",
        "GiIntensity": 1.2,
        "GiDenoise": 0.9,

        "MotionBlur": True,
        "MotionBlurShutter": 0.4,

        "VignetteIntensity": 0.42,
        "VignetteSmoothness": 0.7,
        "ChromaticAberration": 0.0012,
        "FilmGrain": 0.16,
        "FilmGrainSize": 1.6,
    })

    scene = build(profile, mat)

    header = [
        "Scene: Camp",
        "Environment:",
        "  Ambient: [0.03, 0.04, 0.07]",
        "  AmbientIntensity: 1",
        # **A gradient, not the courtyard's panorama.** A cubemap sky is the
        # scene's image-based light as well as its backdrop, so a dusk HDR
        # lights every surface in the clearing with dusk -- which is exactly
        # what the trees came back as, and turning the moon down could never
        # have fixed it, because the moon was not what was lighting them.
        #
        # Three colours and a horizon, all nearly black. That is what leaves
        # the fire as the only light here, which is the composition.
        "  Sky: Gradient",
        "  SkyHorizon: [0.05, 0.07, 0.14]",
        "  SkyZenith: [0.012, 0.018, 0.045]",
        "  SkyGround: [0.01, 0.012, 0.02]",
        "  SkyIntensity: 1",
        "  SkyRotation: 0",
        "Entities:",
    ]

    path = pathlib.Path(args.output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(chr(10).join(header) + chr(10) + scene + chr(10),
                    encoding="utf-8")

    entities = scene.count("  - EntityID:")
    print(f"{path}: {entities} entities")
    print(f"  materials {', '.join(sorted(mat))}")
    print(f"  profile   {ASSETS / 'post' / 'camp.rvpostprofile'}")


if __name__ == "__main__":
    main()
