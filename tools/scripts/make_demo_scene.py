#!/usr/bin/env python3
"""Build the demo scene: a forge courtyard at dusk.

    python tools/scripts/make_demo_scene.py

**Why this is a generator and not a hand-edited `.rage`.**

The scene it replaces had thirty-four entities named things like "Sphere
(gold)", "Cube (dielectric)" and "Falling Box 3", laid out on a grid. Every
feature the engine had was in it and none of them were anywhere for a reason.
It read as a checklist because it *was* a checklist, and a 700-line YAML file
is exactly the shape that lets a checklist accumulate one entity at a time
with nobody able to see it happening.

So the scene is generated from sections that each state what they are for.
Adding a feature to the demo now means finding the section it belongs to and
saying why the courtyard would contain it -- or adding a section and making
that argument out loud. **The rule is that nothing goes in because it is new.
It goes in because the place would have one.**

---

**The place.** A small walled courtyard behind a forge, at the end of the day.
Three brick walls, a soil floor, a plinth at the back holding the piece the
smith is proudest of. A brazier still burning down on the left. Crates
stacked on the right, one of them not stacked very well. A fox that has
wandered in. The low sun comes over the open side, which is where the camera
is.

Every engine feature in the list below is in that description somewhere, and
that is the whole point of the description:

| The courtyard has | Which exercises |
|---|---|
| Brick walls, soil floor, wooden crates, a steel brazier | Textured PBR: albedo, normal, roughness, metallic, height, AO |
| A polished sphere on the plinth | Metals, image-based lighting, a realtime reflection probe |
| A low sun over the open side | Directional light, cascaded shadows |
| A brazier burning down | Additive particles with curves, a point light, looping spatial audio |
| A hanging lantern | Spot light and its shadows |
| A fox | glTF import, skinning, an animation clip |
| A badly stacked crate, and three more dropped at Play | Jolt rigid bodies, collision callbacks, impact sound and flash |
| The gate | A trigger volume |
| A plaque on the plinth | World-space text |
| A title card and a counter | Screen-space UI, a font, a bound button handler |
| Dusk | Every post effect: bloom, an authored LUT, vignette, aberration, grain, auto exposure, depth of field |

Anti-aliasing is **not** here, because it is not a property of the place: it
belongs to the project, and `SampleProject.rvproject` selects TAA.
ENGINE-NOTES 7s.
"""

import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "scripts"))

import postprofile  # noqa: E402

# --- the palette ------------------------------------------------------------
#
# Handles, not paths. Every one of these is minted by the asset registry and
# recorded in a `.meta` in version control, which is what makes a scene file
# survive a folder being reorganised.

PRIMITIVE_BASE = 0x7261676556000000
CUBE, SPHERE, PLANE, CYLINDER, QUAD = (PRIMITIVE_BASE + i for i in range(5))

MAT_STEEL = 13042967132365226213

# The map sets the tiled variants below are built from. A `.rmat` carries one
# `Tiling`, and tiling is a property of *how big the surface is* -- an eleven
# metre wall and a one-and-a-half metre plinth cannot share a number without
# one of them having bricks the size of a door. So the courtyard authors its
# own materials over the shared maps, which is what the maps being assets is
# for.
MAPS = {
    "brick": {"BaseColor": 5971349675159141683, "Normal": 2037669606796660249,
              "Occlusion": 6268745042894850009, "Roughness": 8365816227942354050,
              "Height": 997712195047113081},
    "soil": {"BaseColor": 17246187153222571135, "Normal": 12973099232739305617,
             "Roughness": 10909146574749930491, "Height": 12081873772315894805},
    "wood": {"BaseColor": 10613873159764570968, "Normal": 8374394024450062275,
             "Roughness": 14722946847724631757, "Height": 3600103724061119284},
}

MESH_FOX = 11679010045657754579
# The glTF import mints a material per primitive. Material 0 means "the
# model's own" to the *serializer* and white to the renderer, which is
# how the fox arrived with no fur.
MAT_FOX = 14845622431998451938
FONT = 12355000851502047045
TEX_FLAME = 14156592944927634151
TEX_SKY = 17858281879166177050

SFX_CHIME = 4876371458843502635
SFX_HUM = 11706423746662484290
SFX_IMPACT = 7622274306199633311

CURVE_FLAME_SIZE = 4167849381279018650
CURVE_FLAME_ALPHA = 11461028735333202510
GRADIENT_EMBER = 14237806248374965933

# The one colour decision, made once. A warm scene wants a cool accent for
# anything that is *not* part of it, and the UI is not part of it.
RED = "[0.9, 0.29, 0.32, 1]"
INK = "[0.04, 0.04, 0.06, 0.62]"


def handle_for(name):
    """A stable 64-bit id from a name. FNV-1a, same as postprofile's."""
    h = 0xCBF29CE484222325
    for byte in name.encode("utf-8"):
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h or 0x7261676556444D4F


def vec(*values):
    return "[" + ", ".join(f"{v:g}" for v in values) + "]"


class Scene:
    """Accumulates entities. Every one is named, and the name is its id.

    Deriving the id from the name rather than counting means regenerating the
    scene after inserting something in the middle does not renumber everything
    after it -- so the diff shows what changed rather than that the file was
    rewritten.
    """

    def __init__(self):
        self.lines = []
        self.used = {}

    def entity(self, name, parent=None, position=None, rotation=None, scale=None):
        eid = handle_for(name)
        if eid in self.used:
            raise SystemExit(f"'{name}' and '{self.used[eid]}' hash to the same id")
        self.used[eid] = name

        self.lines += [
            f"  - EntityID: {eid}",
            "    TagComponent:",
            f"      Tag: {name}",
        ]

        if position is not None or rotation is not None or scale is not None:
            self.lines += [
                "    TransformComponent:",
                f"      Position: {vec(*(position or (0, 0, 0)))}",
                f"      Rotation: {vec(*(rotation or (0, 0, 0)))}",
                f"      Scale: {vec(*(scale or (1, 1, 1)))}",
            ]

        if parent is not None:
            self.lines += [
                "    RelationshipComponent:",
                f"      Parent: {handle_for(parent)}",
            ]

        return eid

    def block(self, name, rows):
        self.lines.append(f"    {name}:")
        for key, value in rows:
            self.lines.append(f"      {key}: {value}")

    def mesh(self, mesh, material):
        """A mesh and a material *asset*. Material 0 means the model's own."""
        self.block("MeshComponent", [("Mesh", mesh), ("Material", material)])

    def mesh_inline(self, mesh, base, metallic, roughness, emissive=None):
        """A mesh whose material is written into the scene rather than shared.

        For the one-off. A `.rmat` is the right home for anything two objects
        use; inventing an asset for a single sphere is a file to keep track of
        for no reuse.
        """
        self.lines += [
            "    MeshComponent:",
            f"      Mesh: {mesh}",
            "      Material:",
            f"        BaseColor: {vec(*base)}",
            f"        Emissive: {vec(*(emissive or (0, 0, 0, 1)))}",
            f"        Metallic: {metallic:g}",
            f"        Roughness: {roughness:g}",
            "        Occlusion: 1",
        ]

    def static_body(self, shape, **kwargs):
        """A body that never moves. The walls, the floor, the plinth.

        Static rather than absent, because the crates have to land on
        something and the fox has to stand on it.
        """
        self.block("RigidBodyComponent", [
            ("Type", "Static"), ("Mass", 1), ("Friction", 0.6),
            ("Restitution", 0.05), ("LinearDamping", 0.05),
            ("AngularDamping", 0.05), ("GravityFactor", 1),
            ("FreezeRotation", "false"),
        ])
        self.collider(shape, **kwargs)

    def dynamic_body(self, shape, mass=8.0, **kwargs):
        self.block("RigidBodyComponent", [
            ("Type", "Dynamic"), ("Mass", mass), ("Friction", 0.55),
            ("Restitution", 0.12), ("LinearDamping", 0.05),
            ("AngularDamping", 0.08), ("GravityFactor", 1),
            ("FreezeRotation", "false"),
        ])
        self.collider(shape, **kwargs)

    def collider(self, shape, half=(0.5, 0.5, 0.5), radius=0.5, height=1.0,
                 offset=(0, 0, 0), trigger=False):
        self.block("ColliderComponent", [
            ("Shape", shape), ("HalfExtents", vec(*half)), ("Radius", radius),
            ("Height", height), ("Offset", vec(*offset)),
            ("IsTrigger", "true" if trigger else "false"),
        ])

    def script(self, name, **fields):
        self.lines += ["    NativeScriptComponent:", f"      Script: {name}"]
        if fields:
            self.lines.append("      Fields:")
            for key, value in fields.items():
                self.lines.append(f"        {key}: {value}")

    def text(self):
        return chr(10).join(self.lines)


def build(profile_handle, mat):
    s = Scene()

    # --- the camera ---------------------------------------------------------
    #
    # Standing in the open side of the courtyard, at eye height, looking
    # slightly down at the plinth. Not a three-quarter orbit view: a person
    # walked in here and stopped.
    s.entity("Courtyard Camera", position=(0.3, 1.62, 4.9),
             rotation=(math.radians(-4.0), math.radians(-2.0), 0))
    s.block("CameraComponent", [
        ("ViewRank", 0), ("FixedAspectRatio", "false"),
        ("ProjectionType", "Perspective"), ("PerspectiveFOV", 55),
        ("PerspectiveNearClip", 0.05), ("PerspectiveFarClip", 400),
        ("OrthographicScale", 10), ("OrthographicNearClip", -1),
        ("OrthographicFarClip", 1),
        # Where every phase-9 effect is switched on. ENGINE-NOTES 7s.
        ("PostProfile", profile_handle),
    ])

    # --- the ground and the walls -------------------------------------------
    #
    # Three walls and a floor. The fourth side is open and the camera is
    # standing in it, which is what makes this a courtyard rather than a room
    # -- and what lets the sun in.

    s.entity("Courtyard Floor", position=(0, 0, 0), scale=(30, 1, 30))
    s.mesh(PLANE, mat["floor"])
    s.static_body("Box", half=(15, 0.05, 15), offset=(0, -0.05, 0))

    walls = (
        # name, position, rotation about Y, length
        ("Back Wall", (0, 1.35, -5.4), 0.0, 11.0),
        ("Left Wall", (-5.2, 1.35, -0.9), math.pi / 2, 10.4),
        ("Right Wall", (5.2, 1.35, -0.9), math.pi / 2, 10.4),
    )
    for name, pos, yaw, length in walls:
        s.entity(name, position=pos, rotation=(0, yaw, 0),
                 scale=(length, 2.7, 0.4))
        s.mesh(CUBE, mat["wall"])
        s.static_body("Box", half=(0.5, 0.5, 0.5))

    # A course of brick along the top of the back wall, offset forward. Real
    # walls have a lip, and the lip is what catches the low sun and gives the
    # shadow cascades a hard horizontal edge to be judged on.
    s.entity("Back Wall Coping", position=(0, 2.78, -5.28), scale=(11.4, 0.22, 0.66))
    s.mesh(CUBE, mat["wall"])

    # --- the plinth and the piece on it -------------------------------------
    #
    # The reason the courtyard is arranged the way it is. Everything else
    # leads the eye here: the walls converge on it, the brazier lights it from
    # one side, the lantern from the other, and the depth of field is focused
    # on it.

    s.entity("Plinth", position=(0, 0.5, -2.6), scale=(1.5, 1.0, 1.5))
    s.mesh(CUBE, mat["plinth"])
    s.static_body("Box", half=(0.5, 0.5, 0.5))

    s.entity("Plinth Cap", position=(0, 1.05, -2.6), scale=(1.78, 0.12, 1.78))
    s.mesh(CUBE, MAT_STEEL)

    # Polished, so it is the object in the scene that shows the reflection
    # probe doing anything. A rough sphere here would prove nothing.
    s.entity("The Piece", position=(0, 1.62, -2.6), scale=(1.06, 1.06, 1.06))
    s.mesh_inline(SPHERE, (0.96, 0.82, 0.46, 1), metallic=1, roughness=0.13)
    s.script("Spinner")

    # Realtime, because the brazier flickers and a baked cube would hold a
    # still flame in the reflection while the real one moved.
    s.entity("Courtyard Probe", position=(0, 1.62, -2.6))
    s.block("ReflectionProbeComponent", [
        ("Update", "Realtime"), ("Resolution", 128), ("Influence", 26),
        ("NearClip", 0.05), ("FarClip", 60), ("FacesPerFrame", 1),
    ])

    # The plaque, which is what world-space text is *for* -- a label that
    # belongs to an object in the world rather than to the screen.
    s.entity("Plinth Plaque", position=(0, 0.62, -1.82),
             rotation=(math.radians(-10), 0, 0))
    s.block("WorldTextComponent", [
        ("Text", "no. 41  --  brass, unfinished"), ("Font", FONT), ("Size", 0.1),
        ("Color", "[0.86, 0.72, 0.5, 1]"), ("Align", "Center"),
        ("WrapWidth", 0), ("LineSpacing", 1), ("Billboard", "None"),
    ])

    # --- the brazier --------------------------------------------------------
    #
    # The warm side of the lighting, and the only thing in the courtyard that
    # is still moving on its own. Four features stacked on one prop, because
    # that is how they occur in a real scene: a fire is a light, a particle
    # system and a sound at the same time, and demonstrating them apart is
    # what made the old scene feel like a catalogue.

    s.entity("Brazier Stand", position=(-2.6, 0.42, -1.5), scale=(0.34, 0.85, 0.34))
    s.mesh(CYLINDER, mat["crate"])
    s.static_body("Box", half=(0.5, 0.5, 0.5))

    s.entity("Brazier Bowl", position=(-2.6, 0.95, -1.5), scale=(0.62, 0.26, 0.62))
    s.mesh(CYLINDER, MAT_STEEL)

    # **Not** parented to the bowl. A child inherits its parent's scale, and
    # the bowl is squashed to a third of its height -- which turned a column of
    # flame into a disc of it. Placed in the world instead.
    s.entity("Brazier Fire", position=(-2.6, 1.02, -1.5))
    s.block("ParticleEmitterComponent", [
        ("Emit", "true"), ("Rate", 64), ("Burst", 0), ("Lifetime", 0.95),
        ("LifetimeJitter", 0.3), ("Direction", "[0, 1, 0]"), ("Spread", 12),
        ("Speed", 0.95), ("SpeedJitter", 0.3), ("Gravity", "[0, 0.85, 0]"),
        ("Drag", 1.3), ("SizeStart", 0.19), ("SizeEnd", 0.05),
        ("ColorStart", "[1, 0.9, 0.48, 1]"), ("ColorEnd", "[0.55, 0.11, 0.03, 0]"),
        ("Spin", 40), ("Facing", "Billboard"), ("Blend", "Additive"),
        ("Space", "Local"), ("Texture", TEX_FLAME), ("MaxParticles", 512),
        # The CPU path, deliberately. The GPU one is exercised by its own
        # check; here the emitter is small and the point is what it looks
        # like, not how many of it there can be.
        ("SimulateOnGpu", "false"),
        ("SizeCurve", CURVE_FLAME_SIZE),
        ("ColorGradient", GRADIENT_EMBER),
        ("AlphaCurve", CURVE_FLAME_ALPHA),
    ])

    s.entity("Brazier Light", position=(-2.6, 1.3, -1.5))
    s.block("LightComponent", [
        ("Type", "Point"), ("Color", "[1, 0.68, 0.38]"), ("Intensity", 18),
        ("Range", 14), ("InnerCone", 20), ("OuterCone", 30),
    ])

    s.entity("Brazier Hum", position=(-2.6, 1.1, -1.5))
    s.block("AudioSourceComponent", [
        ("Clip", SFX_HUM), ("Bus", "Sfx"), ("Volume", 0.3), ("Pitch", 1),
        ("Loop", "true"), ("PlayOnAwake", "true"), ("Stream", "false"),
        # Spatial, so walking the camera past it is audibly a *place*. A
        # non-spatial fire is a menu sound.
        ("Spatial", "true"), ("MinDistance", 1.5), ("MaxDistance", 16),
    ])

    # --- the crates ---------------------------------------------------------
    #
    # Stacked by somebody in a hurry. The top one overhangs, which is why
    # pressing Play knocks it off -- physics that starts by doing something is
    # worth more than a pile that has to be poked.

    stack = (
        ("Crate A", (3.05, 0.45, -2.5), math.radians(3)),
        ("Crate B", (3.0, 1.34, -2.46), math.radians(-6)),
        ("Crate C", (3.9, 0.45, -1.7), math.radians(-18)),
    )
    for name, pos, yaw in stack:
        s.entity(name, position=pos, rotation=(0, yaw, 0), scale=(0.9, 0.9, 0.9))
        s.mesh(CUBE, mat["crate"])
        s.static_body("Box", half=(0.5, 0.5, 0.5))

    # One on the floor by the brazier. The right of the frame was a wall of
    # crates against an empty left, and a composition that is heavy on one
    # side reads as unfinished however good the lighting is.
    s.entity("Crate E", position=(-3.5, 0.4, -0.4), rotation=(0, math.radians(28), 0),
             scale=(0.8, 0.8, 0.8))
    s.mesh(CUBE, mat["crate"])
    s.static_body("Box", half=(0.5, 0.5, 0.5))

    # The one that is not going to stay there.
    s.entity("Crate D", position=(2.72, 2.16, -2.42),
             rotation=(0, math.radians(16), math.radians(-9)),
             scale=(0.82, 0.82, 0.82))
    s.mesh(CUBE, mat["crate"])
    s.dynamic_body("Box", mass=6.0, half=(0.5, 0.5, 0.5))
    s.script("ImpactSound")

    # And three more coming down from the roofline at Play, which is what
    # gives the contact callbacks something to report.
    for i in range(2):
        name = f"Falling Crate {i + 1}"
        s.entity(name,
                 position=(4.05 + i * 0.95, 3.4 + i * 0.7, -0.5 + i * 0.7),
                 rotation=(math.radians(9 + 14 * i), math.radians(24 * i), math.radians(6)),
                 scale=(0.7, 0.7, 0.7))
        s.mesh(CUBE, mat["crate"])
        s.dynamic_body("Box", mass=4.0, half=(0.5, 0.5, 0.5))
        s.script("ImpactFlash" if i == 1 else "ImpactSound")

    # --- the fox ------------------------------------------------------------
    #
    # The only thing here that is alive, and the whole of the skinning and
    # animation story. Off-centre and turned toward the plinth, so it reads as
    # having come to look at the same thing the camera has.

    s.entity("Fox", position=(-1.5, 0, -0.15), rotation=(0, math.radians(52), 0),
             scale=(0.0115, 0.0115, 0.0115))
    s.mesh(MESH_FOX, MAT_FOX)
    s.block("AnimatorComponent", [
        # Clip 2 is the walk. 0 is a survey that barely moves, which reads
        # as the animator not running.
        ("Clip", 2), ("Playing", "true"), ("Loop", "true"), ("Speed", 0.8),
        ("BlendTime", 0.35),
    ])

    # --- the light ----------------------------------------------------------

    # The sun, low and over the open side, so it rakes across the brick and
    # throws the plinth's shadow back at the wall. A high sun would light the
    # tops of things and leave the shadow cascades nothing to do.
    s.entity("Late Sun", position=(4, 7.5, 7),
             rotation=(math.radians(-27), math.radians(28), 0))
    s.block("LightComponent", [
        ("Type", "Directional"), ("Color", "[1, 0.79, 0.58]"), ("Intensity", 3.1),
        ("Range", 60), ("InnerCone", 20), ("OuterCone", 30),
    ])

    # A lantern over the plinth -- the light only, with nothing drawn for it.
    # A lit cube hanging in mid-air is an unexplained object, and an
    # unexplained object is the whole thing this scene is trying not to have.
    # Its job is the spot shadow, and in the composition it separates the
    # sphere from the wall behind it.
    s.entity("Lantern", position=(0, 2.3, -4.1))
    s.block("LightComponent", [
        ("Type", "Spot"), ("Color", "[1, 0.88, 0.72]"), ("Intensity", 42),
        ("Range", 18), ("InnerCone", 18), ("OuterCone", 34),
    ])

    # --- the gate -----------------------------------------------------------
    #
    # A trigger across the open side. Nothing blocks it; it is how the scene
    # knows something has come in, which is what a trigger volume is for.
    s.entity("Courtyard Gate", position=(0, 1.2, 2.6), scale=(1, 1, 1))
    s.static_body("Box", half=(4.6, 1.2, 0.4), trigger=True)
    s.script("TriggerZone")

    s.entity("Gate Chime", parent="Courtyard Gate")
    s.block("AudioSourceComponent", [
        ("Clip", SFX_CHIME), ("Bus", "Sfx"), ("Volume", 0.5), ("Pitch", 1),
        ("Loop", "false"), ("PlayOnAwake", "false"), ("Stream", "false"),
        ("Spatial", "true"), ("MinDistance", 2), ("MaxDistance", 22),
    ])

    # --- the screen ---------------------------------------------------------
    #
    # Screen space, and deliberately *not* diegetic: this is the engine talking
    # about itself, so it does not pretend to be part of the courtyard. Red on
    # near-black -- the house accent, and the one cool thing in a warm frame,
    # so it stays legible over any part of it.

    s.entity("HUD")
    s.block("UICanvasComponent", [
        ("ScaleMode", "ScaleWithScreen"), ("ReferenceResolution", "[1920, 1080]"),
        ("MatchWidthOrHeight", 0.5), ("SortOrder", 0),
    ])

    def rect(name, parent, anchor_min, anchor_max, offset_min, offset_max,
             order=0, blocks=False):
        s.entity(name, parent=parent)
        s.block("UIRectComponent", [
            ("AnchorMin", vec(*anchor_min)), ("AnchorMax", vec(*anchor_max)),
            ("OffsetMin", vec(*offset_min)), ("OffsetMax", vec(*offset_max)),
            ("SortOrder", order), ("Visible", "true"),
            # Only the buttons take the pointer. A backing panel that swallows
            # clicks is the classic UI bug and it is one field.
            ("BlocksPointer", "true" if blocks else "false"),
        ])

    def label(name, parent, text, size, colour, align="Center",
              inset=(0, 0), order=1):
        rect(name, parent, (0, 0), (1, 1), (inset[0], inset[1]),
             (-inset[0], -inset[1]), order)
        s.block("UITextComponent", [
            ("Text", text), ("Font", FONT), ("Size", size), ("Color", colour),
            ("Align", align), ("Wrap", "false"), ("LineSpacing", 1),
        ])

    # The banner, across the top.
    #
    # **The canvas origin is the top-left and Y grows downward** -- (0,0) is
    # the top-left corner of the parent and (1,1) the bottom-right. Anchoring a
    # top band at y = 1 with a negative offset is the mistake that puts the
    # title along the bottom of the window, which is where this first landed.
    rect("Banner", "HUD", (0, 0), (1, 0), (0, 0), (0, 104))
    s.block("UIImageComponent", [("Texture", 0), ("Color", INK)])

    label("Banner Title", "Banner", "RageV", 44, RED, align="Left",
          inset=(52, 28))
    label("Banner Subtitle", "Banner", "The courtyard  --  forge, dusk", 22,
          "[0.76, 0.76, 0.8, 1]", align="Right", inset=(52, 34))

    # --- two buttons, and they are two on purpose ---------------------------
    #
    # These are the *only* two ways a click reaches game code, and the engine
    # has both because they suit different shapes of game. Demonstrating one
    # would leave the other undemonstrated and nobody would know it was there.
    #
    #   Ring the bell -- the button carries the script and polls itself. The
    #     one to reach for when the handler lives on the button.
    #   Strike the anvil -- the button *names* a method on another entity. The
    #     one to reach for when a manager handles several buttons.
    #
    # Both count on screen, so both visibly do something rather than being
    # wired to a log line nobody reads.

    rect("Bell Button", "HUD", (0, 1), (0, 1), (52, -128), (356, -52),
         order=1, blocks=True)
    s.block("UIImageComponent", [("Texture", 0), ("Color", INK)])
    s.block("UIButtonComponent", [
        ("Interactable", "true"), ("NormalColor", "[0.82, 0.82, 0.86, 1]"),
        ("HoverColor", "[1, 1, 1, 1]"), ("PressedColor", "[0.55, 0.55, 0.6, 1]"),
    ])
    # Polls its own button, so it must not also be bound -- one click seen
    # twice is arithmetic rather than a bug, and it is confusing either way.
    s.script("ClickCounter", Caption="ring the bell", PollOwnButton="true")
    label("Bell Label", "Bell Button", "ring the bell", 24, RED, inset=(14, 12),
          order=2)

    # The tally the second button drives. A separate entity, which is the whole
    # point of the bound path: the handler does not live on the button.
    rect("Anvil Tally", "HUD", (0, 1), (0, 1), (376, -128), (680, -52), order=1)
    s.block("UIImageComponent", [("Texture", 0), ("Color", INK)])
    s.script("ClickCounter", Caption="struck", PollOwnButton="false")
    label("Anvil Tally Label", "Anvil Tally", "struck", 24,
          "[0.76, 0.76, 0.8, 1]", inset=(14, 12), order=2)

    rect("Anvil Button", "HUD", (0, 1), (0, 1), (700, -128), (1004, -52),
         order=1, blocks=True)
    s.block("UIImageComponent", [("Texture", 0), ("Color", INK)])
    s.block("UIButtonComponent", [
        ("Interactable", "true"), ("NormalColor", "[0.82, 0.82, 0.86, 1]"),
        ("HoverColor", "[1, 1, 1, 1]"), ("PressedColor", "[0.55, 0.55, 0.6, 1]"),
        ("OnClickTarget", handle_for("Anvil Tally")),
        ("OnClickMethod", "Count"),
    ])
    label("Anvil Label", "Anvil Button", "strike the anvil", 24, RED,
          inset=(14, 12), order=2)

    return s.text()


def write_material(path, maps, tiling, height_scale=0.03, metallic=0.0,
                   roughness=1.0):
    """A `.rmat` over shared maps, tiled for a surface of a known size.

    **Tiling is a property of the surface, not of the texture.** A brick map at
    the same tiling on an eleven-metre wall and a one-and-a-half-metre plinth
    gives one of them bricks the size of a door -- which is exactly how the
    first render of this courtyard looked, and the reason these exist rather
    than the scene sharing `brick.rmat` everywhere.

    The numbers below are chosen as *tiles per metre of surface*, which is the
    only way to reason about them that survives an object being resized.
    """
    handle = handle_for(path.name)
    path.parent.mkdir(parents=True, exist_ok=True)

    lines = [
        f"Material: {path.stem}",
        "BaseColor: [1, 1, 1, 1]",
        "Emissive: [0, 0, 0, 1]",
        f"Metallic: {metallic:g}",
        f"Roughness: {roughness:g}",
        "Occlusion: 1",
        "NormalScale: 1",
        "Specular: 0.5",
        f"HeightScale: {height_scale:g}",
        f"Tiling: [{tiling[0]:g}, {tiling[1]:g}]",
        "UvOffset: [0, 0]",
        "Maps:",
    ]
    for key, value in maps.items():
        lines.append(f"  {key}: {value}")

    path.write_text(chr(10).join(lines) + chr(10), encoding="utf-8")
    path.with_name(path.name + ".meta").write_text(
        chr(10).join([f"Handle: {handle}", "Type: Material", "SourceHash: 0"])
        + chr(10), encoding="utf-8")
    return handle


def write_lut(path):
    """The grade, authored here rather than imported from a `.cube`.

    A recipe rather than a baked table because it is *this project's* look and
    somebody should be able to open it and see what it does -- which is the
    whole argument for `.rvlut` existing. ENGINE-NOTES 7v.

    Warm, lifted shadows, a little desaturated: the end of a working day
    rather than a colour test.
    """
    handle = handle_for(path.name)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(chr(10).join([
        "LutRecipe: 1",
        "Temperature: 0.16",
        "Tint: -0.04",
        "Lift: [0.018, 0.012, 0.026]",
        "Gamma: [1, 0.99, 0.96]",
        "Gain: [1.04, 1, 0.94]",
        "Contrast: 1.08",
        "Saturation: 0.94",
        "Size: 33",
    ]) + chr(10), encoding="utf-8")

    path.with_name(path.name + ".meta").write_text(
        f"Handle: {handle}\nType: ColorLut\nSourceHash: 0\n", encoding="utf-8")
    return handle


def main():
    assets = ROOT / "SampleProject" / "assets"

    # Tiles per metre, and that is the unit to think in. Brick courses are
    # about 8 cm, so roughly three tiles a metre puts them at a believable
    # size; soil has no scale of its own and just needs to not repeat visibly;
    # a crate is small enough that one tile across it is right.
    materials = assets / "materials"
    mat = {
        "wall": write_material(materials / "courtyard_wall.rmat", MAPS["brick"],
                               tiling=(9.0, 2.4), height_scale=0.03),
        "plinth": write_material(materials / "courtyard_plinth.rmat", MAPS["brick"],
                                 tiling=(2.2, 1.6), height_scale=0.025),
        "floor": write_material(materials / "courtyard_floor.rmat", MAPS["soil"],
                                tiling=(16.0, 16.0), height_scale=0.04),
        "crate": write_material(materials / "courtyard_crate.rmat", MAPS["wood"],
                                tiling=(1.0, 1.0), height_scale=0.015),
    }

    lut = write_lut(assets / "post" / "courtyard.rvlut")

    # Every phase-9 effect, at settings chosen for the picture rather than to
    # prove they are switched on. Depth of field focused on the plinth; the
    # aperture is what decides how much of the courtyard goes with it.
    profile = postprofile.write_named(assets / "post" / "courtyard.rvpostprofile", {
        "Exposure": 1.0,
        "BloomEnabled": True,
        "BloomThreshold": 1.15,
        "BloomKnee": 0.6,
        "BloomIntensity": 0.055,
        "BloomClamp": 24.0,

        # On, so walking from the shaded side of the courtyard into the sun
        # does what an eye does. Slow, because a fast adaptation in a scene
        # this contrasty pumps.
        "AutoExposure": True,
        "AutoExposureKey": 0.17,
        "AutoExposureSpeed": 1.4,
        "AutoExposureLowPercent": 0.55,
        "AutoExposureHighPercent": 0.93,

        "ColorLut": lut,
        "ColorLutStrength": 0.85,

        # 50 mm at f/4: the plinth is sharp, the near ground and the far wall
        # are not, and a person can still tell what everything is.
        "DepthOfField": True,
        "FocusDistance": 7.5,
        "FocalLength": 50.0,
        "Aperture": 2.2,
        "MaxBokehRadius": 14.0,

        # Small numbers on all three. These are the effects that look like
        # settings the moment they are obvious.
        "ChromaticAberration": 0.0016,
        "VignetteIntensity": 0.34,
        "VignetteSmoothness": 0.55,
        "FilmGrain": 0.22,
        "FilmGrainSize": 2.0,
    })

    body = build(profile, mat)

    scene = assets / "scenes" / "demo.rage"
    scene.write_text(chr(10).join([
        "Scene: The courtyard",
        "Version: 6",
        "Environment:",
        # Cool ambient against the warm key. Without it the shadowed sides of
        # the brick go to a flat brown and the whole frame is one hue.
        "  AmbientColor: [0.34, 0.4, 0.52]",
        "  AmbientIntensity: 0.16",
        "  Sky: Cubemap",
        "  SkyHorizon: [0.52, 0.6, 0.72]",
        "  SkyZenith: [0.18, 0.31, 0.62]",
        "  SkyGround: [0.16, 0.15, 0.14]",
        "  SkyIntensity: 1",
        # Rotated so the panorama's bright quarter sits behind the open side,
        # agreeing with where the directional light is coming from. A sky that
        # disagrees with the sun is the single fastest way to make a lit scene
        # look wrong without anybody being able to say why.
        "  SkyRotation: 2.4",
        f"  SkyTexture: {TEX_SKY}",
        "Entities:",
        body,
    ]) + chr(10), encoding="utf-8")

    entities = body.count("  - EntityID:")
    print(f"{scene.relative_to(ROOT)}: {entities} entities")
    print(f"  materials {', '.join(sorted(mat))}")
    print(f"  grade   {assets / 'post' / 'courtyard.rvlut'}")
    print(f"  profile {assets / 'post' / 'courtyard.rvpostprofile'}")


if __name__ == "__main__":
    main()
