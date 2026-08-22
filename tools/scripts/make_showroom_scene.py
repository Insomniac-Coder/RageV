#!/usr/bin/env python3
"""Build the showroom scene: one car, one room, one very large soft light.

    python tools/scripts/make_showroom_textures.py
    build/bin/Release/rvimport/rvimport.exe models/showroom/porsche_992_gt3_r.glb \\
        --out=tools/scripts/data/showroom_car.yaml
    python tools/scripts/make_showroom_scene.py

**The place.** A single-car delivery bay. White painted panel to left and
right, a charcoal porcelain floor polished enough to hold a reflection, and a
recessed luminaire filling most of the ceiling. Behind the car the room opens
through a wide portal into the service bay, which is concrete, much darker, and
lit only by its own downlights. A convex mirror over the portal, a charge post
beside it.

Everything below is in that description, and the description is the argument
for it -- the rule the courtyard set and the camp kept: **nothing goes in
because it is new, it goes in because the place would have one.**

| The room has | Which exercises |
|---|---|
| A polished floor | Ray-traced reflections, and a roughness map that keeps them from being a mirror |
| A luminaire filling the ceiling | Emissive materials, bloom, and nine lights standing in for an area source |
| A dark bay behind | Value separation, and the traced bounce reaching into it |
| A 490,000-triangle car | glTF import at a scale nothing else in this project reaches |
| A drag-to-orbit camera | The action map, and a script that owns the camera |

**The lighting, which is the whole scene.** The reference is a photograph of a
delivery bay and what makes it work is one thing: *the light source is enormous
and it is the ceiling*. Everything else follows from that.

  * **A big source wraps.** The car has no hard-edged shadow anywhere on it;
    every terminator is a long soft gradient down the flank, and what reads as
    "expensive" in a car photograph is almost entirely that gradient.
  * **A big source is a big reflection.** The panel grid runs the length of the
    roof, the bonnet and the floor, and those long rectangles are what tell you
    the paint is deep and the floor is polished. This is why the luminaire is
    an emissive *texture* with mullions in it rather than a white rectangle:
    the reflection has to have structure or it is a smear.
  * **A big source cannot be one light.** Nine point lights across the panel,
    because a single one puts a single hot specular dot on the bonnet and the
    illusion dies at that dot.
  * **And it still needs a shadow.** Nine soft fills alone leave the car
    floating. One spot from directly overhead casts the grounding shadow, and
    it is the only shadow caster in the scene -- the engine allows four local
    ones and the other three would each add a second contact shadow the eye
    reads as a second car.

**Value, which is the other half.** The reference is a *high key* frame with a
low key hole in the middle of it: bright ceiling, bright walls, dark floor,
dark portal. The car sits on the boundary. Take the dark bay away and the car's
roofline dissolves into the wall behind it -- which is what the first version
of this scene did, and why the portal is there at all.

**The camera orbits and the car does not turn.** See ShowroomCamera.cs; the
short version is that a turning car keeps its highlights welded to its own
panels, and that single cue is what makes a turntable look like a turntable.

**Ray tracing is on at the project level**, so on a device with ray queries the
reflections, the ambient occlusion and the bounce are all traced -- which is
what this scene is authored for. OpenGL and anything without ray queries fall
back to the screen-space forms, which are configured in the post profile below
and are what the greyed-out rows in Render Settings are about (7ao).
"""

import argparse
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import postprofile                                              # noqa: E402
from make_demo_scene import Scene, handle_for, vec, write_material  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "SampleProject" / "assets"
CAR_SUBTREE = ROOT / "tools" / "scripts" / "data" / "showroom_car.yaml"

SCENE_VERSION = 6

PRIMITIVE_BASE = 0x7261676556000000
CUBE, SPHERE, PLANE, CYLINDER, QUAD = (PRIMITIVE_BASE + i for i in range(5))

FONT = 12355000851502047045

# --- the room, in metres ------------------------------------------------------
#
# The car is 4.77 long and 2.05 wide and sits at the origin facing +Z, which is
# the way the model was authored -- so the room is built around the origin and
# the camera stands on +Z looking back down the axis at the nose.
HALF_WIDTH = 5.5           # x, wall face to wall face
BACK = -8.0                # the portal wall
FRONT = 9.5                # behind the camera; mostly out of frame
CEILING = 3.7

WALL_THICKNESS = 0.2

# The portal into the service bay, and the bay behind it.
PORTAL_HALF = 3.2
PORTAL_HEIGHT = 3.0
BAY_BACK = -14.5
BAY_CEILING = 3.1

# The luminaire. Not centred on the room: it is pushed back, so the brightest
# part of the ceiling is *behind* the car from the default camera and the car
# reads against it rather than under it.
PANEL_HALF_X = 3.9
# **Extended forward, over the camera as well as over the car.** A headlamp
# lens faces forward and slightly down, so what it reflects is the ceiling *in
# front of* the car -- and with the luminaire stopping level with the nose there
# was nothing there but dark ceiling. The lamps came out as open holes with the
# internals visible and no cover on them, which reads as a missing mesh and is
# not: the glass was there, correctly blended, reflecting black.
#
# Every showroom photograph has continuous lighting overhead for exactly this
# reason. The fill lights stay where they were, so this changes what the car
# *reflects* and not what lights it.
PANEL_Z = 0.6
PANEL_HALF_Z = 5.5
PANEL_Y = 3.66

# How many metres of surface one tile of each map covers. **These are
# make_showroom_textures.py's own numbers** and the two files have to agree --
# a map is only the right size on a wall if the tiling was derived from the
# span the height field was built against.
FLOOR_SPAN = 9.0
WALL_SPAN = 4.8
CONCRETE_SPAN = 3.0

# The orbit the camera starts on. ShowroomCamera.cs is given the same numbers
# and solves the same transform, so the first frame is the still this scene was
# framed as.
TARGET = (0.0, 0.72, 0.0)
START_YAW = 0.0
START_PITCH = 5.0
START_DISTANCE = 7.3

# --- the car's own lights -----------------------------------------------------
#
# Off in the file and switched on by the button in the bar; ShowroomLights.cs
# is given these same numbers, so the two cannot drift.
#
# **Where they are came out of the model rather than off a tape measure.** The
# glTF stores a min and a max on every POSITION accessor, so the world box of
# each material is a walk of the node transforms and eight corners -- which put
# the headlamp band at y 0.57 to 0.73 and z 1.64 to 1.86, and the tail lamps at
# y 0.64 to 0.73 and z -2.17 to -1.64. A light guessed at from a photograph of
# the car would be a few centimetres out and would light the inside of the
# wing.
LAMP_Y = 0.66
LAMP_X = 0.60

# Just clear of the lens on each end, so the cone starts outside the bodywork.
# Inside it, the first thing a beam lights is the back of the panel it came
# from.
HEADLAMP_Z = 1.92
TAILLAMP_Z = -2.30

# Dipped, not on full: aimed at the floor four metres ahead, which is about
# eight degrees down. A beam thrown flat travels the length of the room and
# lands on the front wall as two bright discs, and the pool on the floor
# between the car and the camera is the whole reason to have them.
HEADLAMP_TARGET = (LAMP_X, 0.0, 6.2)
TAILLAMP_TARGET = (LAMP_X, 0.25, -8.5)

FRONT_INTENSITY = 88
REAR_INTENSITY = 30

# What the lamp elements themselves give off, which is a separate thing from
# the lights above -- emissive lights nothing in this engine, it only makes the
# surface bright and feeds bloom. Both are needed: one is the lamp being on and
# the other is the lamp doing something.
#
# Well above 1, like the luminaire, and for the same reason: bloom thresholds
# at 1.05 and the tone mapper is compressing everything near white, so a
# surface meant to read as a source has to sit clear of both or it comes back
# the same grey as the housing around it.
FRONT_EMISSIVE = (19, 21, 24)
REAR_EMISSIVE = (16, 0.7, 0.35)

CREDIT = ('"2024 Porsche 992 GT3 R" (https://skfb.ly/pMLAu) by Dave Love '
          'SketchFab is licensed under Creative Commons Attribution '
          '(http://creativecommons.org/licenses/by/4.0/).')


def font_metrics(path):
    """A baked font's advances, kerning pairs and line height, in em units.

    The `.rvfont` is a text file and this reads the three numbers a layout
    needs, so the generator can size a box to the text that will go in it. The
    alternative is a guess at an average character width, and a guess is what
    puts a plate three quarters of the way along a line.
    """
    advances, kerning, line_height = {}, {}, 1.0
    section, code = None, None

    for raw in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        row = raw.strip()
        if row == "Glyphs:":
            section = "glyphs"
        elif row == "Kerning:":
            section = "kerning"
        elif row.startswith("LineHeight:"):
            line_height = float(row.split(":")[1])
        elif section == "glyphs":
            if row.startswith("- C:"):
                code = int(row.split(":")[1])
            elif row.startswith("Advance:") and code is not None:
                advances[code] = float(row.split(":")[1])
        elif section == "kerning" and row.startswith("- ["):
            left, right, amount = row[3:].rstrip("]").split(",")
            kerning[(int(left), int(right))] = float(amount)

    return advances, kerning, line_height


FONT_ADVANCES, FONT_KERNING, FONT_LINE_HEIGHT = font_metrics(
    ASSETS / "Fonts" / "roboto.rvfont")


def text_width(text, size):
    """How wide one line of it draws, in canvas units.

    The same sum UI::TextLayout does: advances plus the kerning of each pair,
    scaled by the em size. A character the face has no glyph for contributes
    nothing, which is also what the layout does.
    """
    total, previous = 0.0, 0
    for character in text:
        code = ord(character)
        if code not in FONT_ADVANCES:
            continue
        if previous:
            total += FONT_KERNING.get((previous, code), 0.0)
        total += FONT_ADVANCES[code]
        previous = code
    return total * size


def line_height(size):
    """The height of one line box, which is what a rectangle has to hold."""
    return FONT_LINE_HEIGHT * size


def texture(name):
    """A generated texture's handle, which is FNV-1a of its file name.

    make_camp_textures.write_meta mints them that way, so the scene can name
    one without reading a sidecar -- and a regenerated texture keeps its
    handle, which is what stops a re-run breaking every material.
    """
    return handle_for(name)


def orbit_transform(yaw_degrees, pitch_degrees, distance):
    """Where the camera stands, and the euler angles that aim it at the target.

    The same solve ShowroomCamera.cs does at runtime. Written here as well
    because the scene has to *be* the framed shot before anything runs -- the
    editor's Game view before Play, a `--screenshot` of frame 0, and the
    thumbnail in the content browser all read the transform and never the
    script.
    """
    yaw = math.radians(yaw_degrees)
    pitch = math.radians(pitch_degrees)

    ground = distance * math.cos(pitch)
    position = (TARGET[0] + ground * math.sin(yaw),
                TARGET[1] + distance * math.sin(pitch),
                TARGET[2] + ground * math.cos(yaw))

    return position, aim(position, TARGET)


def aim(position, target):
    """Euler XYZ that points an entity's -Z axis from `position` at `target`.

    The engine takes a light's and a camera's direction from the entity's own
    -Z (Scene.cpp), and composes euler angles through Math::FromEuler -- so
    this is that composition inverted, and the two have to stay in step. A
    third opinion about which way -Z points is exactly the kind of thing that
    is wrong by a sign for months.
    """
    dx = target[0] - position[0]
    dy = target[1] - position[1]
    dz = target[2] - position[2]

    length = math.sqrt(dx * dx + dy * dy + dz * dz) or 1.0
    dx, dy, dz = dx / length, dy / length, dz / length

    return (math.asin(max(-1.0, min(1.0, dy))), math.atan2(-dx, -dz), 0.0)


def materials():
    """The room's surfaces, one `.rmat` per surface *size*.

    Tiling is a property of the surface and not of the texture -- the argument
    write_material makes -- so a seventeen-metre side wall and a nine-metre
    portal pier cannot share one material even though they wear one map set.
    """
    directory = ASSETS / "materials"

    floor_maps = {
        "BaseColor": texture("showroom_floor_albedo.png"),
        "Normal": texture("showroom_floor_normal.png"),
        "Roughness": texture("showroom_floor_rough.png"),
        "Occlusion": texture("showroom_floor_ao.png"),
        "Height": texture("showroom_floor_height.png"),
    }
    wall_maps = {
        "BaseColor": texture("showroom_wall_albedo.png"),
        "Normal": texture("showroom_wall_normal.png"),
        "Roughness": texture("showroom_wall_rough.png"),
    }
    concrete_maps = {
        "BaseColor": texture("showroom_concrete_albedo.png"),
        "Normal": texture("showroom_concrete_normal.png"),
        "Roughness": texture("showroom_concrete_rough.png"),
    }
    panel_maps = {
        "BaseColor": texture("showroom_panel_albedo.png"),
        "Emissive": texture("showroom_panel_albedo.png"),
        "Normal": texture("showroom_panel_normal.png"),
        "Roughness": texture("showroom_panel_rough.png"),
    }

    depth = FRONT - BACK
    width = HALF_WIDTH * 2.0

    return {
        # **Parallax on, and only here.** The grout line is the one edge in the
        # room seen at a grazing angle across the whole frame, which is the
        # single case where a height map earns its cost -- the floor is what
        # the eye reads the room's depth from.
        "floor": write_material(
            directory / "showroom_floor.rmat", floor_maps,
            tiling=(width / FLOOR_SPAN, depth / FLOOR_SPAN),
            height_scale=0.008, metallic=0.0, roughness=1.0),

        # **Black, on the owner's direction, and it changes the room.** A
        # white box lights a white car by wrapping it in its own colour, which
        # is flattering and is also why the first render had no edges in it --
        # the flanks and the wall behind them were the same value. Black side
        # walls turn the room into a photographic studio instead: the ceiling
        # is the source, the floor bounces, and the two long sides return
        # nothing, so the car keeps a dark edge down each flank and its shape
        # reads.
        #
        # The same map set, so the panel joints still catch a line. Rougher
        # than the white paint, because black paint at the white's roughness
        # becomes a mirror of the luminaire and stops being black.
        "wall_side": write_material(
            directory / "showroom_wall_side.rmat", wall_maps,
            tiling=(depth / WALL_SPAN, CEILING / WALL_SPAN),
            height_scale=0.0, roughness=1.0,
            base_color=(0.030, 0.031, 0.034, 1)),

        # **Charcoal, on the owner's direction: everything still white goes
        # dark except the luminaire itself.** What is left is a black room with
        # one lit ceiling in it, which is a photographic studio rather than a
        # showroom -- and it is the stronger frame, because the only bright
        # things left are the source and the car.
        #
        # Not pure black. A room with nothing above 0.02 in it has no form at
        # all: the portal's reveal, the panel joints and the corner where two
        # walls meet all stop existing, and the eye reads the result as fog
        # rather than as darkness.
        "wall_back": write_material(
            directory / "showroom_wall_back.rmat", wall_maps,
            tiling=(width / WALL_SPAN, CEILING / WALL_SPAN),
            height_scale=0.0, roughness=1.0,
            base_color=(0.026, 0.027, 0.030, 1)),

        # The ceiling goes with the walls. A hair lighter than they are, so the
        # luminaire is set *into* a surface rather than floating in a void --
        # the reveal around it needs something to be a reveal of.
        "ceiling": write_material(
            directory / "showroom_ceiling.rmat", wall_maps,
            tiling=(width / WALL_SPAN, depth / WALL_SPAN),
            height_scale=0.0, roughness=1.0,
            base_color=(0.034, 0.035, 0.038, 1)),

        # **Emissive well above one, and that is not an abuse of the field.**
        # Bloom thresholds at 1.05, and a surface meant to read as a light has
        # to sit clear of it or the tone mapper -- which is compressing
        # everything near white -- brings it back to the same grey as the
        # ceiling it is set into.
        #
        # A third off what it first was, on the owner's direction. The fitting
        # and its output came down together: they are one light, and dimming
        # only the number that lights the room leaves a ceiling still glowing
        # like the old one.
        "panel": write_material(
            directory / "showroom_panel.rmat", panel_maps,
            tiling=(1.0, 1.0), height_scale=0.0, roughness=1.0,
            base_color=(1, 1, 1, 1), emissive=(4.7, 4.75, 4.95, 1)),

        "concrete": write_material(
            directory / "showroom_concrete.rmat", concrete_maps,
            tiling=(6.4 / CONCRETE_SPAN, BAY_CEILING / CONCRETE_SPAN),
            height_scale=0.0, roughness=1.0,
            base_color=(0.62, 0.62, 0.63, 1)),

        "concrete_floor": write_material(
            directory / "showroom_bayfloor.rmat", concrete_maps,
            tiling=(6.4 / CONCRETE_SPAN, 6.4 / CONCRETE_SPAN),
            height_scale=0.0, roughness=1.0,
            base_color=(0.42, 0.42, 0.43, 1)),

        # Anodised dark grey: the luminaire's reveal, the skirting, the charge
        # post. Satin rather than matte, so it catches a line from the panel
        # and reads as metal.
        "trim": write_material(
            directory / "showroom_trim.rmat", {}, tiling=(1, 1),
            height_scale=0.0, metallic=0.55, roughness=0.34,
            base_color=(0.085, 0.088, 0.095, 1)),

        # The convex mirror over the portal, and the only true mirror in the
        # room.
        "chrome": write_material(
            directory / "showroom_chrome.rmat", {}, tiling=(1, 1),
            height_scale=0.0, metallic=1.0, roughness=0.055,
            base_color=(0.93, 0.94, 0.96, 1)),

        # A downlight's lens. Emissive but nothing like the panel: these are
        # small, and matching the panel's value would put four more suns in the
        # darkest part of the frame.
        "downlight": write_material(
            directory / "showroom_downlight.rmat", {}, tiling=(1, 1),
            height_scale=0.0, roughness=0.8,
            base_color=(1, 1, 1, 1), emissive=(2.2, 2.0, 1.7, 1)),
    }


def build(profile, mat):
    s = Scene()

    # --- the camera ---------------------------------------------------------
    position, rotation = orbit_transform(START_YAW, START_PITCH, START_DISTANCE)

    # **40 degrees vertical, which is wider than a car photographer would
    # use and is the right answer here.** A long lens flatters a car and shows
    # nothing else; this frame has to hold the luminaire above the car and the
    # portal behind it, because those are what light it and what separate it.
    # At 40 the panel enters the top of the frame just behind the roofline,
    # which is the composition the reference has.
    s.entity("Showroom Camera", position=position, rotation=rotation)
    s.block("CameraComponent", [
        ("ViewRank", 0), ("FixedAspectRatio", "false"),
        ("ProjectionType", "Perspective"), ("PerspectiveFOV", 40),
        ("PerspectiveNearClip", 0.05), ("PerspectiveFarClip", 200),
        ("OrthographicScale", 10), ("OrthographicNearClip", -1),
        ("OrthographicFarClip", 1),
        ("PostProfile", profile),
    ])
    s.managed_script("ShowroomCamera",
                     TargetX=TARGET[0], TargetY=TARGET[1], TargetZ=TARGET[2],
                     Yaw=START_YAW, Pitch=START_PITCH,
                     MinPitch=0.5, MaxPitch=32.0,
                     Distance=START_DISTANCE, MinDistance=4.6, MaxDistance=11.0)

    # --- the shell ----------------------------------------------------------
    depth = FRONT - BACK
    middle = (FRONT + BACK) * 0.5

    s.entity("Floor", position=(0, 0, middle), scale=(HALF_WIDTH * 2, 1, depth))
    s.mesh(PLANE, mat["floor"])
    s.static_body("Box", half=(HALF_WIDTH, 0.05, depth * 0.5), offset=(0, -0.05, 0))

    # Flipped, so its normal points down. A rotation has determinant one, so
    # the winding survives and only the normal turns over -- which is what a
    # ceiling made from a ground plane needs and what mirroring the scale would
    # get wrong.
    s.entity("Ceiling", position=(0, CEILING, middle),
             rotation=(math.pi, 0, 0), scale=(HALF_WIDTH * 2, 1, depth))
    s.mesh(PLANE, mat["ceiling"])

    for name, x in (("Left Wall", -1.0), ("Right Wall", 1.0)):
        s.entity(name,
                 position=(x * (HALF_WIDTH + WALL_THICKNESS * 0.5),
                           CEILING * 0.5, middle),
                 rotation=(0, math.pi / 2, 0),
                 scale=(depth, CEILING, WALL_THICKNESS))
        s.mesh(CUBE, mat["wall_side"])
        s.static_body("Box", half=(0.5, 0.5, 0.5))

    # Behind the camera. In frame only at the widest orbit angles, and it is
    # what stops the room being open to a void when the camera swings round.
    s.entity("Front Wall", position=(0, CEILING * 0.5, FRONT + WALL_THICKNESS * 0.5),
             scale=(HALF_WIDTH * 2 + WALL_THICKNESS * 2, CEILING, WALL_THICKNESS))
    s.mesh(CUBE, mat["wall_back"])

    # --- the portal ---------------------------------------------------------
    #
    # Two piers and a header rather than one wall with a hole, because the
    # engine has no boolean and a wall with a hole is three boxes however it is
    # described. The gap is what the whole frame is built around: it is the one
    # dark shape in a bright room and it is what the car's roofline reads
    # against.
    pier = (HALF_WIDTH - PORTAL_HALF)
    for name, side in (("Portal Pier Left", -1.0), ("Portal Pier Right", 1.0)):
        s.entity(name,
                 position=(side * (PORTAL_HALF + pier * 0.5), CEILING * 0.5,
                           BACK - WALL_THICKNESS * 0.5),
                 scale=(pier, CEILING, WALL_THICKNESS))
        s.mesh(CUBE, mat["wall_back"])
        s.static_body("Box", half=(0.5, 0.5, 0.5))

    s.entity("Portal Header",
             position=(0, (PORTAL_HEIGHT + CEILING) * 0.5, BACK - WALL_THICKNESS * 0.5),
             scale=(PORTAL_HALF * 2, CEILING - PORTAL_HEIGHT, WALL_THICKNESS))
    s.mesh(CUBE, mat["wall_back"])

    # The reveal: the portal's own edge, in the same dark trim as the
    # luminaire's. A doorway cut straight through painted panel has no edge and
    # reads as a printed rectangle rather than as an opening.
    for name, pos, scale in (
            ("Portal Reveal Left", (-PORTAL_HALF, PORTAL_HEIGHT * 0.5, BACK),
             (0.09, PORTAL_HEIGHT, 0.26)),
            ("Portal Reveal Right", (PORTAL_HALF, PORTAL_HEIGHT * 0.5, BACK),
             (0.09, PORTAL_HEIGHT, 0.26)),
            ("Portal Reveal Head", (0, PORTAL_HEIGHT, BACK),
             (PORTAL_HALF * 2 + 0.18, 0.09, 0.26))):
        s.entity(name, position=pos, scale=scale)
        s.mesh(CUBE, mat["trim"])

    # Skirting where the painted panel meets the floor. Two centimetres of dark
    # trim, and it is the difference between a wall standing on a floor and two
    # planes meeting at a line.
    for name, pos, scale in (
            ("Skirting Left", (-HALF_WIDTH + 0.03, 0.055, middle), (0.06, 0.11, depth)),
            ("Skirting Right", (HALF_WIDTH - 0.03, 0.055, middle), (0.06, 0.11, depth)),
            ("Skirting Back Left",
             (-(PORTAL_HALF + pier * 0.5), 0.055, BACK + 0.03), (pier, 0.11, 0.06)),
            ("Skirting Back Right",
             (PORTAL_HALF + pier * 0.5, 0.055, BACK + 0.03), (pier, 0.11, 0.06))):
        s.entity(name, position=pos, scale=scale)
        s.mesh(CUBE, mat["trim"])

    # --- the luminaire ------------------------------------------------------
    #
    # One emissive plane and a reveal around it. The grid is in the *texture*
    # rather than in geometry, and that is the load-bearing decision: what the
    # car's paint and the floor reflect is a pattern of long bright rectangles,
    # and a texture gives that at one draw where geometry would give it at
    # twenty-two and alias in the reflection.
    s.entity("Luminaire", position=(0, PANEL_Y, PANEL_Z),
             rotation=(math.pi, 0, 0),
             scale=(PANEL_HALF_X * 2, 1, PANEL_HALF_Z * 2))
    s.mesh(PLANE, mat["panel"])

    reveal = 0.055
    for name, pos, scale in (
            ("Luminaire Reveal Left",
             (-PANEL_HALF_X, PANEL_Y + 0.02, PANEL_Z), (0.10, 0.09, PANEL_HALF_Z * 2)),
            ("Luminaire Reveal Right",
             (PANEL_HALF_X, PANEL_Y + 0.02, PANEL_Z), (0.10, 0.09, PANEL_HALF_Z * 2)),
            ("Luminaire Reveal Front",
             (0, PANEL_Y + 0.02, PANEL_Z + PANEL_HALF_Z), (PANEL_HALF_X * 2 + 0.2, 0.09, 0.10)),
            ("Luminaire Reveal Back",
             (0, PANEL_Y + 0.02, PANEL_Z - PANEL_HALF_Z), (PANEL_HALF_X * 2 + 0.2, 0.09, 0.10))):
        s.entity(name, position=pos, scale=scale)
        s.mesh(CUBE, mat["trim"])
    del reveal

    # --- the light ----------------------------------------------------------
    #
    # **Nine fills standing in for one area source.** The engine has
    # directional, point and spot and no rect light, and a showroom is lit by a
    # rectangle four metres a side. One point light in the middle of the
    # luminaire puts one hot specular dot on the bonnet and the whole thing
    # dies at that dot; nine spread across it give a specular that runs the
    # length of a panel, which is what a big source does and the only part of
    # it the eye actually checks.
    #
    # None of them casts a shadow. Nine shadowed lights would be nine offset
    # copies of the car on the floor -- and the engine allows four local
    # casters anyway, so the choice is which one, not whether.
    fill_x = (-2.6, 0.0, 2.6)
    fill_z = (PANEL_Z - 2.7, PANEL_Z, PANEL_Z + 2.7)
    for row, z in enumerate(fill_z):
        for column, x in enumerate(fill_x):
            s.entity(f"Panel Light {row}{column}", position=(x, PANEL_Y - 0.25, z))
            s.block("LightComponent", [
                ("Type", "Point"),
                # 5000 K: a hair cool, which is what a delivery bay is lit at
                # and what keeps white paint reading as white rather than as
                # cream.
                ("Color", "[0.93, 0.96, 1]"), ("Intensity", 14.7),
                # Range is the composition, as the camp's fire says. Eleven
                # metres reaches the floor and the far wall and stops before
                # the service bay, which has to stay dark.
                ("Range", 11), ("InnerCone", 20), ("OuterCone", 30),
                ("CastShadows", "false"),
            ])

    # The grounding shadow, and the only shadow in the room. Straight down from
    # the middle of the luminaire: a key that is off to one side throws the
    # shadow out from under the car and the car stops sitting on the floor.
    s.entity("Key Light", position=(0, PANEL_Y - 0.2, 0.15),
             rotation=(math.radians(-90), 0, 0))
    s.block("LightComponent", [
        ("Type", "Spot"), ("Color", "[0.95, 0.97, 1]"), ("Intensity", 46),
        ("Range", 9), ("InnerCone", 34), ("OuterCone", 62),
        ("CastShadows", "true"),
    ])

    # Two kickers from behind, low and wide, aimed across the car's shoulders.
    # They do one job: put a line of light along the top of each flank so the
    # roof separates from the wall behind it. Without them the car's silhouette
    # against white panel is a grey shape on a white shape.
    for name, x in (("Kicker Left", -4.2), ("Kicker Right", 4.2)):
        origin = (x, 2.55, -5.4)
        s.entity(name, position=origin, rotation=aim(origin, (0.0, 1.0, 0.4)))
        s.block("LightComponent", [
            ("Type", "Spot"), ("Color", "[0.88, 0.92, 1]"), ("Intensity", 26),
            ("Range", 13), ("InnerCone", 16), ("OuterCone", 40),
            ("CastShadows", "false"),
        ])

    # --- the car's own lamps ------------------------------------------------
    #
    # Four spots, all at zero intensity in the file. ShowroomLights.cs raises
    # them when the switch in the bar is pressed and puts them back when it is
    # pressed again -- so the scene as saved is the still it was framed as, and
    # the lights are something the viewer turns on.
    #
    # **Intensity rather than deleting and spawning them**, because a light
    # that exists at zero costs a row in a storage buffer and nothing else: the
    # renderer caps nothing and the term it feeds multiplies out. Spawning
    # would mean a script that knows how to build a light, which is a scene's
    # job and not a script's.
    #
    # None of them casts a shadow. The engine allows four local casters and the
    # key light overhead is the one that matters -- it is what puts the car on
    # the floor. A headlamp caster would buy the splitter's shadow inside its
    # own pool, which is a detail nobody looks for, at the price of the shadow
    # that holds the whole picture together.
    for name, x in (("Headlamp Beam Left", -LAMP_X), ("Headlamp Beam Right", LAMP_X)):
        origin = (x, LAMP_Y, HEADLAMP_Z)
        target = (x, HEADLAMP_TARGET[1], HEADLAMP_TARGET[2])
        s.entity(name, position=origin, rotation=aim(origin, target))
        s.block("LightComponent", [
            # A cool white LED, which is what this car actually has and what
            # separates the beam from the 5000 K room around it.
            ("Type", "Spot"), ("Color", "[0.86, 0.91, 1]"), ("Intensity", 0),
            # Eight metres reaches the pool and dies before the front wall at
            # 9.5, which is behind the camera and should stay dark.
            ("Range", 8), ("InnerCone", 15), ("OuterCone", 30),
            ("CastShadows", "false"),
        ])

    # Backwards, into the portal. **The best thing the tail lights do is not on
    # the car**: the bay behind is the darkest thing in the frame, and a red
    # wash across its concrete is the only colour anywhere in a room built out
    # of white, charcoal and black.
    for name, x in (("Tail Glow Left", -LAMP_X), ("Tail Glow Right", LAMP_X)):
        origin = (x, LAMP_Y + 0.04, TAILLAMP_Z)
        target = (x, TAILLAMP_TARGET[1], TAILLAMP_TARGET[2])
        s.entity(name, position=origin, rotation=aim(origin, target))
        s.block("LightComponent", [
            # Not pure red. A light with nothing at all in two channels lands
            # on grey concrete as a flat clipped patch with no shading in it,
            # because there is only one channel left to shade with.
            ("Type", "Spot"), ("Color", "[1, 0.1, 0.045]"), ("Intensity", 0),
            ("Range", 7), ("InnerCone", 22), ("OuterCone", 46),
            ("CastShadows", "false"),
        ])

    # --- the service bay ----------------------------------------------------
    #
    # Dark, and it is dark *on purpose*: it is the only low value in a high key
    # frame and it is what the car's roofline reads against. It is also a real
    # room rather than a black card -- the downlights, the far wall and the
    # floor are all there, because a traced reflection off the bonnet looks
    # into it and a black card reflects as a hole.
    bay_middle = (BACK + BAY_BACK) * 0.5
    bay_depth = BACK - BAY_BACK

    s.entity("Bay Floor", position=(0, 0.0, bay_middle),
             scale=(PORTAL_HALF * 2 + 1.6, 1, bay_depth))
    s.mesh(PLANE, mat["concrete_floor"])

    s.entity("Bay Ceiling", position=(0, BAY_CEILING, bay_middle),
             rotation=(math.pi, 0, 0), scale=(PORTAL_HALF * 2 + 1.6, 1, bay_depth))
    s.mesh(PLANE, mat["concrete"])

    s.entity("Bay Back Wall", position=(0, BAY_CEILING * 0.5, BAY_BACK),
             scale=(PORTAL_HALF * 2 + 1.6, BAY_CEILING, WALL_THICKNESS))
    s.mesh(CUBE, mat["concrete"])

    for name, side in (("Bay Wall Left", -1.0), ("Bay Wall Right", 1.0)):
        s.entity(name, position=(side * (PORTAL_HALF + 0.8), BAY_CEILING * 0.5, bay_middle),
                 rotation=(0, math.pi / 2, 0),
                 scale=(bay_depth, BAY_CEILING, WALL_THICKNESS))
        s.mesh(CUBE, mat["concrete"])

    # Four recessed downlights. Each is a lens you can see and a light that
    # reaches about as far as the lens suggests -- which is the pairing that
    # makes a light fitting believable and the thing a scene most often gets
    # wrong in one direction or the other.
    for index, (x, z) in enumerate(((-1.7, -9.6), (1.7, -9.6),
                                    (-1.7, -12.4), (1.7, -12.4))):
        s.entity(f"Bay Downlight {index}", position=(x, BAY_CEILING - 0.02, z),
                 rotation=(math.pi, 0, 0), scale=(0.34, 1, 0.34))
        s.mesh(PLANE, mat["downlight"])

        s.entity(f"Bay Downlight Light {index}", position=(x, BAY_CEILING - 0.15, z))
        s.block("LightComponent", [
            ("Type", "Point"), ("Color", "[1, 0.94, 0.84]"), ("Intensity", 4.5),
            ("Range", 6), ("InnerCone", 20), ("OuterCone", 30),
            ("CastShadows", "false"),
        ])

    # --- what a delivery bay has in it --------------------------------------
    #
    # Two things, and no more. The room's subject is the car and every object
    # added to it is a thing competing for the eye -- but a room with *nothing*
    # in it is a render rather than a place, and these are the two a bay
    # actually has on that wall.
    #
    # The convex mirror over the portal: a driver reversing out cannot see
    # across the showroom floor, so there is always one. It also happens to be
    # the only thing in the room that shows you the room.
    s.entity("Convex Mirror", position=(0, PORTAL_HEIGHT + 0.34, BACK + 0.06),
             scale=(0.62, 0.62, 0.28))
    s.mesh(SPHERE, mat["chrome"])

    s.entity("Convex Mirror Rim", position=(0, PORTAL_HEIGHT + 0.34, BACK - 0.01),
             scale=(0.68, 0.68, 0.1))
    s.mesh(CYLINDER, mat["trim"])

    # And the charge post, because a 2024 delivery bay has one whatever is
    # parked in it.
    s.entity("Charge Post", position=(-4.35, 0.62, BACK + 0.22),
             scale=(0.30, 1.24, 0.17))
    s.mesh(CUBE, mat["trim"])

    s.entity("Charge Post Lens", position=(-4.35, 1.02, BACK + 0.31),
             scale=(0.17, 0.035, 0.02))
    s.mesh(CUBE, mat["downlight"])

    # --- the reflection probe -----------------------------------------------
    #
    # Baked, not realtime: nothing in this room moves except the camera, and a
    # realtime probe would re-render six faces a second to arrive at the same
    # cube map every time.
    #
    # Placed at the car's own centre and at its own height. A probe is a single
    # point and everything reflecting from it is guessing parallax, so the
    # place to put it is where the thing that matters most is -- and here that
    # is unambiguous.
    # **Above the roofline, not at the car's centre**, and that was a real
    # defect rather than a tuning choice. A probe is a camera: one *inside* the
    # car photographs the inside of the car, so every surface reflecting
    # through it reflected a black cabin. On Vulkan that was hidden, because
    # traced reflections replace the probe wherever a surface is glossy enough
    # to earn a ray -- so the glass looked right there and was dead on OpenGL,
    # which has no ray queries and nothing else to fall back to. Screen-space
    # reflections cannot cover it either: a blended fragment writes no
    # g-buffer, so SSR has no surface to trace from.
    #
    # 1.55 m clears the roof and the wing, so the capture is the room with the
    # car below it -- which is what the car should be reflecting.
    #
    # 512 rather than 256: glass at roughness 0.04 samples the sharpest mip,
    # and at 256 the luminaire's grid arrives as a smear. Baked, so this is
    # six renders once and nothing per frame.
    s.entity("Showroom Probe", position=(0, 1.55, 0))
    s.block("ReflectionProbeComponent", [
        ("Update", "Baked"), ("Resolution", 512), ("Influence", 22),
        ("NearClip", 0.05), ("FarClip", 60), ("Rate", "15Hz"),
        ("FacesPerFrame", 1),
    ])

    return s


# --- the overlay --------------------------------------------------------------
#
# Canvas units, at the 1920x1080 reference the canvas scales from.
BAR_HEIGHT = 44
CREDIT_SIZE = 17

BUTTON_WIDTH = 120
BUTTON_HEIGHT = 30
BUTTON_INSET = 14              # from the right edge of the bar
BUTTON_LABEL_SIZE = 14


def overlay_ui(s):
    """The attribution, the bar it sits on, and the lights switch at its end.

    **On the canvas rather than on a plaque in the room**, which is the
    opposite of the choice the courtyard's label made and is right for the
    opposite reason: a plaque is a thing in the world that belongs to an object
    in it, and a licence notice belongs to the *picture*. It also has to stay
    legible from every angle the orbit reaches, which nothing standing on the
    floor can promise.
    """
    s.entity("Showroom Canvas")
    s.block("UICanvasComponent", [
        ("ScaleMode", "ScaleWithScreen"), ("ReferenceResolution", "[1920, 1080]"),
        ("MatchWidthOrHeight", 0.5), ("SortOrder", 0),
    ])

    # **A bar rather than a plate sized to the text**, on the owner's
    # direction, and running the full width is what makes it work: the orbit
    # swings the notice across a black floor, a charcoal wall and the
    # luminaire's reflection, and text alone loses one of those three every
    # time round. A band that runs edge to edge reads as part of the frame --
    # the way a broadcast lower third does -- rather than as a label stuck on
    # the picture.
    #
    # Anchored to the bottom edge; y runs down the screen, so 1 is the bottom.
    s.entity("Credit Bar", parent="Showroom Canvas")
    s.block("UIRectComponent", [
        ("AnchorMin", "[0, 1]"), ("AnchorMax", "[1, 1]"),
        ("OffsetMin", f"[0, {-BAR_HEIGHT}]"), ("OffsetMax", "[0, 0]"),
        ("SortOrder", 0), ("Visible", "true"), ("BlocksPointer", "false"),
    ])
    s.block("UIImageComponent", [
        ("Texture", 0),
        # Not quite opaque. At 1.0 it is a letterbox and the frame stops at it;
        # at 0.85 the brightest thing the room has -- the luminaire reflected
        # in the polished floor -- comes through at 15%, which is far below the
        # text and still enough that the bar reads as laid over a photograph
        # rather than cut out of it.
        ("Color", "[0, 0, 0, 0.85]"),
    ])

    # Centred in the whole bar, not in the space left over beside the switch.
    # The notice is the bar's subject and the switch is at its end; centring
    # the text on the remaining room would put it visibly off the middle of the
    # frame, which is the sort of asymmetry that reads as a mistake.
    #
    # The vertical inset is the arithmetic that centres it: text is drawn from
    # the top of its rectangle downwards with no vertical alignment of its own,
    # so the box is the thing that has to be centred.
    top = round((BAR_HEIGHT - line_height(CREDIT_SIZE)) * 0.5)

    s.entity("Credit Text", parent="Credit Bar")
    s.block("UIRectComponent", [
        ("AnchorMin", "[0, 0]"), ("AnchorMax", "[1, 1]"),
        ("OffsetMin", f"[0, {top}]"), ("OffsetMax", "[0, 0]"),
        ("SortOrder", 1), ("Visible", "true"), ("BlocksPointer", "false"),
    ])
    s.block("UITextComponent", [
        # **Single-quoted, and it has to be.** The notice opens with a double
        # quote around the model's title, which YAML reads as the start of a
        # quoted scalar -- so written bare it parses as the title and then
        # fails on the rest of the line. The scene did exactly that: `error at
        # line 757, column 38: unexpected scalar`, which points at the URL and
        # not at the quote that caused it. Nothing in the notice is a single
        # quote, so this needs no escaping.
        ("Text", "'" + CREDIT + "'"),
        ("Font", FONT),
        # 17 at a 1080 reference: small, and still above the size the atlas was
        # baked sharp at. **A licence notice that cannot be read is not a
        # licence notice**, which is the whole reason this is not smaller. At
        # this size the line measures 1267 units and the bar is 1920 of them,
        # so it clears the switch at the far end by a couple of hundred at 16:9
        # and by rather less at 4:3 -- which is the aspect ratio to check if
        # either number ever moves.
        ("Size", CREDIT_SIZE),
        # Held off pure white and off full opacity, which it can now afford to
        # be: it is read against a known black rather than against whatever the
        # orbit happened to put behind it.
        ("Color", "[0.82, 0.83, 0.86, 0.86]"),
        ("Align", "Center"), ("Wrap", "false"), ("LineSpacing", 1),
    ])

    # --- the lights switch --------------------------------------------------
    #
    # At the far right of the bar and inside it, so it belongs to the same band
    # as the notice instead of floating over the picture on its own.
    #
    # **Its whole look is one image and the button's three tints.** The plate
    # texture is white with the pill shape in its alpha, so Normal at 0.55
    # draws grey and Hover at 1.0 draws white, and no script is involved in
    # either -- the canvas multiplies the tint into the image every frame. What
    # a script *is* needed for is the label, because the tint reaches the image
    # on the button and nothing else, children included.
    label_top = round((BUTTON_HEIGHT - line_height(BUTTON_LABEL_SIZE)) * 0.5)
    button_top = round((BAR_HEIGHT - BUTTON_HEIGHT) * 0.5)

    s.entity("Lights Button", parent="Credit Bar")
    s.block("UIRectComponent", [
        ("AnchorMin", "[1, 0]"), ("AnchorMax", "[1, 1]"),
        ("OffsetMin", f"[{-(BUTTON_WIDTH + BUTTON_INSET)}, {button_top}]"),
        ("OffsetMax", f"[{-BUTTON_INSET}, {-button_top}]"),
        ("SortOrder", 2), ("Visible", "true"),
        # Redundant -- a live button takes the pointer whatever this says -- and
        # written out anyway, because what it guarantees is that a click on the
        # switch does not also spin the car.
        ("BlocksPointer", "true"),
    ])
    s.block("UIImageComponent", [
        ("Texture", texture("showroom_button_plate.png")),
        # White: the plate's colour comes entirely from the tints below.
        ("Color", "[1, 1, 1, 1]"),
    ])
    s.block("UIButtonComponent", [
        ("Interactable", "true"),
        ("NormalColor", "[0.55, 0.56, 0.58, 1]"),
        ("HoverColor", "[1, 1, 1, 1]"),
        # A hair below the hover rather than back down to grey. A press that
        # darkens past the resting colour reads as the switch turning itself
        # off under the finger.
        ("PressedColor", "[0.86, 0.87, 0.89, 1]"),
        # Empty: the script is on the button itself, which is the usual shape.
        ("OnClickTarget", 0),
        ("OnClickMethod", "Toggle"),
    ])
    s.managed_script(
        "ShowroomLights",
        CarRoot="'porsche_992_gt3_r'",
        FrontParts="'EXT_Emissive_Light_Front,ST_FRONT_'",
        RearParts="'EXT_Emissive_Light_Rear,EXT_Glass_Emissive_Rear'",
        FrontLamps="'Headlamp Beam Left,Headlamp Beam Right'",
        RearLamps="'Tail Glow Left,Tail Glow Right'",
        LabelName="'Lights Label'",
        FrontEmissive=" ".join(f"{v:g}" for v in FRONT_EMISSIVE),
        RearEmissive=" ".join(f"{v:g}" for v in REAR_EMISSIVE),
        FrontIntensity=FRONT_INTENSITY,
        RearIntensity=REAR_INTENSITY,
        StartOn="false")

    s.entity("Lights Label", parent="Lights Button")
    s.block("UIRectComponent", [
        ("AnchorMin", "[0, 0]"), ("AnchorMax", "[1, 1]"),
        ("OffsetMin", f"[0, {label_top}]"), ("OffsetMax", "[0, 0]"),
        ("SortOrder", 3), ("Visible", "true"), ("BlocksPointer", "false"),
    ])
    s.block("UITextComponent", [
        # What the press will do, not what the lights currently are. Both
        # readings of a one-word switch are defensible and this is the one a
        # person acts on -- and the beams themselves announce which state it is
        # in far more loudly than a label could.
        ("Text", "LIGHTS ON"),
        ("Font", FONT), ("Size", BUTTON_LABEL_SIZE),
        # White on the grey plate. The script takes it to black on hover, when
        # the plate underneath has gone white.
        ("Color", "[0.97, 0.97, 0.98, 1]"),
        ("Align", "Center"), ("Wrap", "false"), ("LineSpacing", 1),
    ])


# --- what the model does not say ---------------------------------------------
#
# **Forty-five of the car's eighty-nine materials carry no maps at all.** That
# is the upload rather than the importer: the glTF gives them a
# `baseColorFactor` of [0.8, 0.8, 0.8], `metallicFactor` 0 and `roughnessFactor`
# 0.958, and nothing else. The forty-four that *do* have maps -- the livery, the
# decals, the glass, the tyres -- import correctly and are left alone.
#
# Among the forty-five is `EXT_Carpaint_Inst`, which is the entire body of the
# car. A flat mid grey at roughness 0.96 is chalk: it has no specular to catch
# the luminaire, so under a big source it goes to a white silhouette with no
# shading in it, which is exactly how it rendered.
#
# So **the model supplies the geometry and the livery, and the showroom
# supplies the paint.** Every entry below is a physical description of what the
# part is actually made of, and the values are the ordinary ones -- car paint is
# a dielectric at about 0.2 roughness, a machined rim is a metal at 0.3, an
# anodised caliper is neither.
#
# The overrides keep whatever maps the original had. Eight of these do carry a
# normal map even without a colour one, and throwing that away to fix the
# colour would trade one loss for another.
CAR_PAINT = (0.82, 0.83, 0.85, 1)

# **The livery, which is in the download and not in the model.**
#
# The Sketchfab page shows this car in the yellow E-TECK #91 Manthey wrap. The
# `.glb` does not: `EXT_Carpaint_Inst` carries a `baseColorFactor` of
# [0.8, 0.8, 0.8] and no texture at all, so the body imported as flat grey and
# rendered as a white silhouette -- correctly, from what the file said.
#
# The wrap is in the archive beside the model, as a 4096-square `decals.png`
# that nothing in the glTF references. The viewer on the website applies it
# through Sketchfab's own material editor, and that assignment is what the
# glTF export dropped.
#
# So it is reattached here, by name, to the one material that is the car's
# body. This is the scene making a claim about the model rather than the
# importer guessing: an importer that went looking for unreferenced images in
# a folder and bound them to materials by hope would be wrong far more often
# than it was right.
CAR_LIVERY = "porsche_992_gt3_r_livery.png"

CAR_MATERIALS = (
    # (matches, base colour, metallic, roughness)
    #
    # The body, wearing the livery above -- so its base colour is white and
    # the texture is the whole of the colour. Roughness 0.18 is a clearcoat:
    # low enough that the luminaire draws a long soft rectangle down the flank,
    # which is the single cue that says "this is painted metal", and not so low
    # that it becomes a mirror.
    (("EXT_Carpaint_Inst",), (1, 1, 1, 1), 0.0, 0.18),

    # Wheels: a dark satin alloy, which is what a GT3 R runs.
    (("EXT_RIM",), (0.185, 0.19, 0.20, 1), 0.85, 0.30),

    # Brake calipers. Anodised, so barely metallic and quite matte -- a
    # chrome-red caliper is a toy car.
    (("EXT_CALIPER",), (0.40, 0.045, 0.035, 1), 0.15, 0.32),

    # Exhausts, which run hot enough to colour.
    (("EXT_EXALTS",), (0.30, 0.275, 0.255, 1), 1.0, 0.38),

    # Carbon, inside and out. Nearly black and *not* rough: the weave under
    # lacquer is glossy, and matte carbon reads as painted plastic.
    (("CHASSIS_KEVLAR", "INT_Carbon", "INT_Steer_Carbon", "INT_CARBON_BAGE"),
     (0.034, 0.035, 0.039, 1), 0.05, 0.28),

    # Radiators, mounts, the fuel filler: machined and blasted metal.
    (("EXT_Mechanics", "EXT_RADIATOR", "EXT_TAMP_FUEL", "INT_METAL_PARTS"),
     (0.155, 0.16, 0.165, 1), 0.75, 0.40),

    # The cabin's black plastic: dash, switchgear, the wheel's rim.
    (("INT_ELECTRONICS", "INT_Racelogic", "INT_SAS", "INT_DASH_PANEL",
      "INT_PLASTIC_BLACK", "STEER_PLASTIC", "INT_BORRACHAS"),
     (0.028, 0.028, 0.030, 1), 0.0, 0.62),

    # Seat and roll-cage padding. Alcantara has no specular worth the name.
    (("SPARCO_FOAM", "SEAT_ALCANTARA"), (0.046, 0.046, 0.049, 1), 0.0, 0.92),

    # Hoses and looms.
    (("INT_CABLES", "INT_MANG_AR", "INT_MANGUEIRAS"),
     (0.022, 0.022, 0.024, 1), 0.0, 0.78),

    # An occlusion shell and a Blender default that came along for the ride.
    # Both want to disappear rather than to be anything.
    (("INT_Occlusion_Inst", "WorldGridMaterial"),
     (0.020, 0.020, 0.022, 1), 0.0, 0.90),
)


# --- and what the model says wrongly ------------------------------------------
#
# **Every material in this model has roughness 0.9577.** Not the untextured
# forty-five -- *all* of them, including the windscreen, the side glass and the
# headlamp lenses. It is whatever the uploader's exporter wrote once and applied
# everywhere.
#
# At 0.96 a dielectric's specular lobe is spread so wide that its highlight has
# no peak at all, which is visually indistinguishable from having no specular:
# the glass came out transparent and completely dead, reading as an empty hole
# rather than as a pane. Glass is one of the smoothest things there is -- 0.03
# to 0.06 -- and that difference is the entire look of it.
#
# Separate from CAR_MATERIALS because these keep every map they came with. The
# maps are the artist's own work and are right; only the scalars beside them are
# wrong, so only the scalars are replaced.
CAR_SURFACES = (
    # (matches, roughness, metallic, specular)
    #
    # **Specular is the fourth number and it is the one that decides whether
    # glass reads as glass.** F0 = 0.08 * Specular, so the engine's 0.5 default
    # is the 4% a bare dielectric reflects -- correct, and at 4% the reflection
    # of even a very bright ceiling is a faint veil rather than a highlight.
    # Real automotive glazing is not bare: a windscreen is laminated and coated,
    # and a race screen is anti-glare treated on top of that. 1.5 is 12%, which
    # is what those coatings actually do and what makes the luminaire appear in
    # the screen instead of merely tinting it.
    #
    # Window glass, inside and out. 0.04: a windscreen reflects the luminaire as
    # a sheet with a hard edge, which is the cue that says "there is a surface
    # here" before anything behind it is read.
    (("EXT_Windows", "INT_Windows", "INT_Windshield", "INT_Glass"), 0.04, 0.0, 1.5),

    # Lamp lenses. A hair rougher than window glass, because a lens is moulded
    # rather than float, and rougher still would lose the point.
    (("EXT_Glass_Emissive",), 0.06, 0.0, 1.7),

    # The bonnet outlet's grille and the roof vent. Perforated sheet over an
    # opening, and on this car it is under a clear cover -- so it wants the
    # glass treatment rather than the 0.96 the model gave everything.
    (("EXT_Grid",), 0.07, 0.0, 1.4),

    # **The lamp internals, and they carry the whole look of the lamp on this
    # model.** Tinting each candidate material a different colour and rendering
    # once showed why nothing worked before: there is *no lens mesh over the
    # headlamp aperture*. `EXT_Glass_Emissive_Front` never appears there --
    # the opening is open, and what fills it is the reflector and the LED
    # elements and nothing in front of them.
    #
    # So the glass has to come from these. A real headlamp reflector is a
    # vacuum-aluminised shell -- a mirror, not a grey plastic bowl -- and at
    # 0.08 and nearly full metalness it throws the luminaire back as a sharp
    # highlight across the whole unit. That is the cue the eye reads as "there
    # is a clear cover over this", and it is the only one available without
    # adding geometry the model does not have.
    (("EXT_Emissive_Light", "AUX_LIGHT"), 0.22, 0.62, 1.6),

    # The wheel-blur discs, which are transparent and want no highlight of
    # their own -- they are a fake, and a fake that catches the light announces
    # itself.
    (("EXT_RIM_BLUR",), 0.85, 0.0, 0.3),
)


def car_materials():
    """Re-materialise the parts the model left as flat grey.

    Returns {old handle: new handle}, which the subtree splice rewrites.

    **Rewriting the reference rather than the imported `.rmat`** is what makes
    this survive: rvimport regenerates those files from the glTF every time it
    runs, so an edit to one is a change waiting to be overwritten. A new
    material beside them, and a handle swapped on the way into the scene, is
    owned by this generator and cannot be clobbered.
    """
    source = ASSETS / "models" / "showroom"
    directory = ASSETS / "materials"
    remap = {}

    def read_scalar_colour(text):
        for line in text.split("\n"):
            if line.startswith("BaseColor: ["):
                return tuple(float(v) for v in
                             line.split("[", 1)[1].rstrip("]").split(","))
        return (1, 1, 1, 1)

    def read_uv(text):
        tiling, offset = (1, 1), (0, 0)
        for line in text.split("\n"):
            if line.startswith("Tiling: ["):
                tiling = tuple(float(v) for v in
                               line.split("[", 1)[1].rstrip("]").split(","))
            elif line.startswith("UvOffset: ["):
                offset = tuple(float(v) for v in
                               line.split("[", 1)[1].rstrip("]").split(","))
        return tiling, offset

    for path in sorted(source.glob("porsche_992_gt3_r_*.rmat")):
        text = path.read_text(encoding="utf-8")

        head, _, maps = text.partition("Maps:")
        name = path.stem.split("_", 5)[-1]

        # Two kinds of override, and which one applies is decided by whether
        # the model gave the material any maps at all.
        textured = "BaseColor:" in maps

        if textured:
            # The maps are the artist's own and are kept; only the scalars
            # beside them are replaced, and only where they are wrong.
            surface = next((entry for entry in CAR_SURFACES
                            if any(name.startswith(match) for match in entry[0])), None)
            if surface is None:
                continue

            _, roughness, metallic, specular = surface
            base = read_scalar_colour(text)
        else:
            override = next((entry for entry in CAR_MATERIALS
                             if any(name.startswith(match) for match in entry[0])), None)
            if override is None:
                continue

            _, base, metallic, roughness = override
            specular = 0.5

        meta = path.with_name(path.name + ".meta")
        if not meta.exists():
            continue
        old = int(meta.read_text(encoding="utf-8").split("Handle:")[1].split("\n")[0])

        # Whatever maps the original had -- eight of these carry a normal map
        # with no colour map, and dropping it to fix the colour trades one loss
        # for another.
        kept = {}
        for line in maps.strip().split("\n"):
            if ":" in line:
                key, value = line.split(":", 1)
                kept[key.strip()] = value.strip()

        tiling, uv_offset = read_uv(text)

        if name.startswith("EXT_Carpaint_Inst"):
            kept["BaseColor"] = texture(CAR_LIVERY)

            # **V mirrored, because the atlas and the mesh disagree about which
            # way it runs.** The body's TEXCOORD_0 has V from 1.007 to 1.995 --
            # offset by a whole tile, which repeat sampling absorbs -- and the
            # panels then land on the atlas upside down: the nose came out
            # wearing the sill's "PORSCHE MANTHEY" and the red bar from the
            # bottom of the sheet.
            #
            # The reason is that this image was never referenced by the glTF,
            # so nothing ever made it agree with glTF's top-left origin. The
            # shader computes `uv * Tiling + UvOffset`, so a tile of -1 and an
            # offset of 1 is the mirror, expressed in the material rather than
            # by rewriting the artist's file.
            tiling, uv_offset = (1, -1), (0, 1)

        target = directory / f"showroom_car_{name.lower()}.rmat"
        remap[old] = write_material(target, kept, tiling=tiling, uv_offset=uv_offset,
                                    height_scale=0.0, metallic=metallic,
                                    roughness=roughness, base_color=base,
                                    specular=specular,
                                    blend="Blend: Blend" in text)

    return remap


def car_root_id():
    """The subtree's own root entity, which is the car as one object.

    Read from the committed import rather than written down here, because it is
    rvimport's number and not this script's -- and a copy of somebody else's id
    is a copy that goes stale silently.
    """
    for line in CAR_SUBTREE.read_text(encoding="utf-8").split("\n"):
        if line.strip().startswith("- EntityID:"):
            return int(line.split(":")[1])

    raise SystemExit(f"{CAR_SUBTREE.relative_to(ROOT)} has no entities in it")


def car_subtree(remap):
    """The imported car, as rvimport wrote it.

    **Read from a file rather than generated, and that is the point.** A model
    with eighty-nine materials is not one entity -- the importer splits it into
    a primitive per (mesh, material) pair and the asset manager turns that into
    three hundred and thirty-seven of them, each with a mesh handle and a
    `.rmat` that only exist once something has imported the model. This script
    cannot mint those and should not try.

    So the import runs once, its output is committed, and regenerating the room
    does not renumber three hundred entities that did not change.
    """
    if not CAR_SUBTREE.exists():
        raise SystemExit(
            f"{CAR_SUBTREE.relative_to(ROOT)} is missing. Run:\n"
            "  build/bin/Release/rvimport/rvimport.exe "
            "models/showroom/porsche_992_gt3_r.glb "
            "--out=tools/scripts/data/showroom_car.yaml")

    lines = CAR_SUBTREE.read_text(encoding="utf-8").split("\n")

    # The document's own header, which the showroom writes for itself.
    start = lines.index("Entities:") + 1
    kept = [line for line in lines[start:] if line.strip()]

    total = sum(1 for line in kept if "Material: " in line)
    swapped = 0

    for index, line in enumerate(kept):
        stripped = line.strip()
        if not stripped.startswith("Material: "):
            continue

        handle = int(stripped.split(": ", 1)[1])
        if handle in remap:
            kept[index] = line.replace(str(handle), str(remap[handle]))
            swapped += 1

    print(f"  car       {swapped} of {total} draws re-authored, "
          f"from {len(remap)} overridden materials")

    return "\n".join(kept)


def write_lut(path):
    """The grade: clean, neutral, and barely there.

    The courtyard's grade is warm and lifted because it is the end of a working
    day. This one is a *product photograph*, and a product photograph's grade
    is the one nobody notices: whites stay white, blacks stay closed, and the
    only real move is a couple of points of contrast to keep the mid greys off
    each other.

    The one deliberate touch is a hair of blue in the lift. A charcoal floor
    under a 5000 K source goes slightly green at the bottom of the curve, and
    two hundredths of blue is what takes it out.
    """
    handle = handle_for(path.name)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join([
        "LutRecipe: 1",
        "Temperature: -0.03",
        "Tint: 0.01",
        "Lift: [0.004, 0.006, 0.014]",
        "Gamma: [1, 1, 1.01]",
        "Gain: [1, 1, 1.01]",
        "Contrast: 1.06",
        "Saturation: 0.98",
        "Size: 33",
    ]) + "\n", encoding="utf-8")

    path.with_name(path.name + ".meta").write_text(
        f"Handle: {handle}\nType: ColorLut\nSourceHash: 0\n", encoding="utf-8")
    return handle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=str(ASSETS / "scenes" / "showroom.rage"))
    args = parser.parse_args()

    lut = write_lut(ASSETS / "post" / "showroom.rvlut")

    profile = postprofile.write_named(ASSETS / "post" / "showroom.rvpostprofile", {
        # **0.45, and the reason is the car and not the room.** Every other
        # scene here sits at 1.0 because every other scene is lit by a sun at
        # an intensity chosen against it. This one is lit from two metres away
        # by a ceiling, and a white car that close to a source that size
        # receives several times what a sunlit surface does -- so at 1.0 the
        # paint clipped to a flat white silhouette with no shading anywhere on
        # it, which is what "the car has no textures" actually looked like.
        #
        # Exposure rather than the lights, deliberately: the *relationship*
        # between the luminaire, the paint and the floor is the picture and it
        # was already right. This is the stop, not the lighting.
        "Exposure": 0.45,

        # **Bloom, and the headlamps are what it is set for now.** The
        # luminaire used to be the only thing above threshold and this was at
        # 0.045 to keep it to a soft halo rather than a light leak. Lit
        # headlamps are a much smaller, much brighter source, and at that
        # setting they read as flat white shapes with no glow at all.
        #
        # **The lever was the lamps, not this number.** The prefilter's
        # contribution is `brightness - threshold`, so a source at 19 throws
        # roughly six times the halo of the ceiling at 4.7 whatever the
        # intensity is -- which is why raising the emissive gets a headlamp
        # glow without turning the ceiling into fog. This went to 0.12 on top
        # of that, which is the amount the tail lamps needed to show a red
        # halo on black bodywork; the ceiling at 0.12 is still a halo and not
        # a leak.
        #
        # The knee is high so the near-white wall next to the luminaire stays
        # out of the effect. The clamp is what any one source may bleed at:
        # 28 clears the front lamps at 24 and nothing else in the room is
        # within a factor of five of it.
        "BloomEnabled": True,
        # **2.28 is the owner's, set in the editor, and this line exists so a
        # regeneration keeps it.** The generator rewrites this file wholesale,
        # so a value tuned by hand in the inspector survives exactly until
        # somebody runs the script -- which is a way to lose an afternoon's
        # taste to a command that looks like it only rebuilds the room.
        "BloomThreshold": 2.28,
        "BloomKnee": 0.7,
        "BloomIntensity": 0.12,
        "BloomClamp": 28.0,

        # **Auto exposure off, and this is the one setting worth arguing
        # about.** Every other scene in this project has it on, because every
        # other scene has a camera that walks between a dark place and a bright
        # one. This camera orbits one object under a fixed light -- so the only
        # thing auto exposure can do here is pump the whole frame as the dark
        # portal swings in and out of view, which is precisely the artefact a
        # product photograph must not have.
        "AutoExposure": False,
        "AutoExposureKey": 0.17,
        "AutoExposureSpeed": 1.4,
        "AutoExposureLowPercent": 0.55,
        "AutoExposureHighPercent": 0.93,

        "ColorLut": lut,
        "ColorLutStrength": 0.9,

        # **Focused on the car, by naming it.** This used to be a hand-set
        # distance equal to the orbit's starting radius, and the comment here
        # admitted the flaw it had: "it does drift if the wheel is used". It
        # drifted badly. At the closest zoom the plane stayed at 7.3 m with the
        # camera at 4.6, which put the *entire car* in front of the near limit
        # -- the nose at six pixels of blur -- and the answer to "why does it
        # go soft when I lean in" was that nothing was ever going to move it.
        #
        # Target mode names the subject and solves both numbers from it every
        # frame: the distance from where the car is, the aperture from how deep
        # it is. The aperture below is what Manual mode falls back to and what
        # the solve overrides; it is left at the value it was tuned to by eye,
        # because the solve independently arrives at f/8.1 at this distance and
        # a number that agrees is worth keeping visible.
        "DepthOfField": True,
        "Focus": "Target",
        "FocusTarget": car_root_id(),

        # **Not 1, and this is the aesthetic choice rather than the correct
        # one.** Full coverage keeps the whole car sharp end to end, which at
        # the closest orbit needs f/29 -- physically right, and it flattens the
        # picture: no fall-off anywhere, which is the one thing separating a
        # photograph of a car from an elevation drawing of one.
        #
        # 0.55 keeps the near half of the car crisp and lets the tail soften,
        # which is what a car photographer shooting a three-quarter view
        # actually does. It also holds the aperture inside the range where the
        # bokeh still has a shape.
        # 0.684, the owner's, for the reason BloomThreshold above gives.
        "SubjectCoverage": 0.684,

        "FocusDistance": START_DISTANCE,
        "FocalLength": 60.0,
        # **f/8, not f/4.5.** At the wider stop the back wall came back as a
        # smear and the near floor went with it -- which is what a portrait
        # lens does at three metres and not what a room shot wants. Stopped
        # down the whole car is sharp and only the far concrete softens, which
        # is the amount a real showroom photograph has.
        "Aperture": 8.0,
        "MaxBokehRadius": 6.0,

        # **Off, unlike every other scene here.** There is nothing in this room
        # that moves, so the only thing motion blur can blur is the orbit
        # itself -- and a car that smears while you drag round it is the exact
        # opposite of what the drag is for.
        "MotionBlur": False,
        "MotionBlurShutter": 0.5,

        # Contact shadowing. Small radius: what it is for here is the line
        # where a tyre meets the floor and the grout the wheels sit across, and
        # a wide AO in a white room reads as dirt.
        "AmbientOcclusion": True,
        "AoRadius": 0.4,
        "AoIntensity": 0.85,

        # The screen-space fallbacks. On a device with ray queries the project
        # turns the traced forms on and these grey out (7ao); on OpenGL they
        # are what the floor's reflection actually is, so they are tuned for
        # that rather than left at defaults.
        "ScreenSpaceReflections": True,
        "SsrMaxDistance": 30.0,
        "SsrThickness": 0.35,
        "SsrIntensity": 1.0,

        # **The bounce is what makes a white room look like one.** Nine lights
        # on the ceiling light the top of everything; what lights the underside
        # of the mirrors and the inside of the arches is the wall opposite
        # throwing it back. The radius is the room, near enough -- a bounce
        # allowed to travel further is the defect ENGINE-NOTES records against
        # 8.12, where an unbounded one cost 3.2 ms and changed no pixels.
        "GlobalIllumination": True,
        "GiRadius": 8.0,
        "GiQuality": "Medium",
        "GiIntensity": 1.0,
        "GiDenoise": 0.9,

        # Barely there, all three. A product photograph is clean; these are
        # here to stop the image reading as computer-generated, which is a
        # different job from being visible.
        "ChromaticAberration": 0.0008,
        "VignetteIntensity": 0.20,
        "VignetteSmoothness": 0.62,
        "FilmGrain": 0.09,
        "FilmGrainSize": 1.6,
    })

    mat = materials()
    s = build(profile, mat)
    overlay_ui(s)

    body = s.text() + "\n" + car_subtree(car_materials())

    scene = pathlib.Path(args.output)
    scene.parent.mkdir(parents=True, exist_ok=True)
    scene.write_text("\n".join([
        "Scene: The showroom",
        f"Version: {SCENE_VERSION}",
        "Environment:",
        # **Almost nothing, and deliberately.** Ambient light is the engine's
        # stand-in for everything it is not simulating, and in a closed room
        # lit by a source this large the bounce is real and comes from the
        # global illumination. Ambient on top of that is a flat lift that
        # takes the shadows out of the wheel arches -- which is where the
        # first version of this scene lost the car's form.
        "  AmbientColor: [0.5, 0.54, 0.62]",
        "  AmbientIntensity: 0.04",
        # No sky. There is no window in this room, and a procedural gradient
        # behind the walls would only ever be seen through a gap that should
        # not exist -- so a flat colour is both correct and a way of finding
        # out if a gap does.
        "  Sky: Color",
        "  SkyHorizon: [0.08, 0.085, 0.095]",
        "  SkyZenith: [0.08, 0.085, 0.095]",
        "  SkyGround: [0.06, 0.06, 0.065]",
        "  SkyIntensity: 1",
        "  SkyRotation: 0",
        "  SkyTexture: 0",
        "Entities:",
        body,
    ]) + "\n", encoding="utf-8")

    entities = body.count("  - EntityID:")
    print(f"{scene.relative_to(ROOT)}: {entities} entities")
    print(f"  materials {', '.join(sorted(mat))}")
    print(f"  grade     {ASSETS / 'post' / 'showroom.rvlut'}")
    print(f"  profile   {ASSETS / 'post' / 'showroom.rvpostprofile'}")


if __name__ == "__main__":
    main()
