#!/usr/bin/env python3
"""Build showroom2: the same delivery bay, with a figure standing in it.

    build/bin/Release/rvimport/rvimport.exe models/showroom2/iron_man.glb \\
        --out=tools/scripts/data/showroom2_figure.yaml
    python tools/scripts/make_showroom2_scene.py

**The room is not described here, and that is the point.** Every wall, light,
material, switch and post setting comes from `make_showroom_scene.py` -- this
file is a `Subject` and nothing else. A replica made by copying that file would
be a replica on the day it was made and a divergence from the first edit
afterwards; a replica made by calling it cannot drift, and the room's own
generator proves it by still writing `showroom.rage` byte for byte.

What a figure changes, and it is a short list:

| | Why |
|---|---|
| The orbit comes in to 5.4 m | The room was framed round a 4.77 m car. A two-metre figure at 7.3 m is a person at the far end of a hall. |
| The subject is scaled and stood on the floor | The model is 2307 units tall with its soles 2 mm below its own origin. |
| Three lamps instead of four | A suit has a reactor and two palms where a car has headlamps and tail lamps. |
| One material instead of eighty-nine | And it needs almost nothing done to it -- see below. |

**The model is unusually well made, and the honest thing is to say so.** The
car needed forty-five materials invented for it because the upload carried no
maps. This one carries a base colour, a metallic-roughness pair, a normal map
and an emissive map, all at 4096 square and all authored: the metallic channel
is 255 over 99.8% of the surface -- the armour *is* metal and the file says so
-- and the roughness runs 0.10 to 0.70, polished panels against battle scuffs.
Nothing here re-materialises it. Two scalars are corrected and the emissive is
raised, and that is the whole of it.

**The emissive is already keyed exactly where it needs to be.** The base colour
`[188, 188, 188]` appears on 28,012 texels and on no others, and the emissive
map lights precisely those: the eye slits, the palms, the chest reactor, the
boot jets and two helmet temples. So the areas that "share the same base
colour" are not something this file has to go looking for -- the artist already
separated them, and the shader multiplies the material's emissive by that mask,
so raising one number lights those texels and nothing else.
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import make_showroom_scene as showroom                          # noqa: E402
from make_demo_scene import write_material                      # noqa: E402

ROOT = showroom.ROOT
ASSETS = showroom.ASSETS
FIGURE_SUBTREE = ROOT / "tools" / "scripts" / "data" / "showroom2_figure.yaml"

MODEL = "models/showroom2/iron_man.glb"

CREDIT = ('"Iron Man" (https://skfb.ly/6TKyK) by Grant Riley is licensed under '
          'Creative Commons Attribution-NonCommercial '
          '(http://creativecommons.org/licenses/by-nc/4.0/).')

# --- how big it is, and where the floor is ------------------------------------
#
# **Read off the glTF's own accessors rather than measured in a viewer.** Every
# POSITION accessor carries a min and a max, so the model's box is exact and
# free: it is 1062.3 x 501.8 x 2307.4 of whatever unit the exporter used, with
# the long axis vertical and the soles at -2.15.
#
# Two metres, which is the suit and not the man inside it. The figure then
# stands 0.6 m taller than the car it replaced and reads at about half the
# frame height at the default orbit -- the same share of the picture the car
# had across its own long axis.
MODEL_HEIGHT = 2307.35522437
MODEL_FLOOR = -2.15332007
FIGURE_HEIGHT = 2.0

FIGURE_SCALE = FIGURE_HEIGHT / MODEL_HEIGHT

# **Lifted by its own two millimetres.** The model's lowest vertex is below its
# origin, so placed at y = 0 the soles sink into the polished floor -- which is
# 2 mm nobody would see directly and which the contact shadow and the floor's
# reflection both would.
FIGURE_LIFT = -MODEL_FLOOR * FIGURE_SCALE

# **He stands up on his own now, and for a while he did not.** The glTF puts a
# single -90 degree X on its `Sketchfab_model` node -- the ordinary Z-up to
# Y-up conversion -- and rvimport used to write the node *below* it with a
# +90 that cancelled it exactly, so the figure arrived in the exporter's own
# Z-up frame and lay on his back with his head toward the camera.
#
# That was never the importer inventing a rotation: `AssetManager::
# InstantiateModel` wrote each node's local transform and then parented the
# nodes with `Scene::SetParent`, whose default is to preserve a child's
# *world* placement -- right for dragging a row in the hierarchy panel, and
# exactly wrong for a hierarchy that is already local. Every child's transform
# was rewritten as the inverse of its parent's, so the whole model composed to
# the identity. Fixed at the source; this file used to carry a -90 to undo it
# and no longer needs to.

# **Turned eighteen degrees, which is the only piece of staging here.** Square
# to the camera is a passport photograph: both shoulders the same width, both
# arms the same length, and no read of depth anywhere. Eighteen is enough to
# separate the shoulders and still keep the chest reactor facing the default
# frame -- and the camera orbits, so dead-on is one drag away.
FIGURE_YAW = -18.0

# --- what the model says wrongly ----------------------------------------------
#
# Two numbers, and both are the exporter's rather than the artist's.
#
# **The normal scale is a tenth of the emissive strength**, exactly:
# `normalTexture.scale` is 0.32152478940888196 and
# `KHR_materials_emissive_strength` is 3.2152478940888196. One of those is a
# real authored value and the other is it divided by ten, written into a field
# that has nothing to do with it. At 0.32 the shader flattens every panel line,
# rivet and vent to a third of its depth -- the map carries real relief, a mean
# xy deviation of 0.17 -- so it goes back to 1.
NORMAL_SCALE = 1.0

# **The roughness factor is kept**, unlike the car's. 0.797 multiplied into a
# map that runs 0.10 to 0.70 gives a surface between 0.08 and 0.56: mirror-
# polished on the clean panels, satin where the suit is scuffed. That is a
# physically ordinary range for lacquered metal and it is what the luminaire
# needs in order to draw a long soft rectangle down the arm -- which is the
# cue that says "this is painted metal" and is the same argument the car's
# 0.18 clearcoat makes.
ROUGHNESS = 0.7969548

# --- the metallic factor, which is the one value argued down from the model ---
#
# **The map says 1.0 and the picture said otherwise** (owner, at 0.55 against a
# ladder of 0.75 / 0.55 / 0.35). The metallic channel is 255 over 99.8% of the
# surface, so at a factor of 1 the whole suit is a pure conductor -- and a pure
# conductor has *no diffuse term at all*. In a room whose walls are charcoal
# and whose one source is straight overhead, the only thing left to see is what
# the armour reflects, which is a dark room: the red and the gold went to a
# near-black mirror with a few highlights on it, which is physically right and
# is not what this suit looks like.
#
# 0.55 puts the base colour map back in the picture. The armour keeps a metal's
# tinted specular and gains enough diffuse for the red to be red and the gold to
# be gold under a source this soft -- which is also the honest description of
# the material: a hero suit is lacquered over metal, not bare plate.
#
# **Where the map is 0 this changes nothing**, and that is why it is safe: the
# metallic map's only zeros are exactly the texels the emissive map lights, so
# the reactor, the eyes and the palms were dielectric at 1.0 and still are.
METALLIC = 0.55

# --- the glow -----------------------------------------------------------------
#
# **The mask is a stencil, not a colour, so its tint is cancelled here.** Every
# lit texel in the emissive map is the same `#95BBFF`, which is (0.3005,
# 0.4969, 1.0) once the sRGB image is decoded. The shader computes
# `EmissiveColor * emissiveMap`, so a neutral scalar would come out in the
# map's own hue at a ratio of 1 : 1.7 : 3.3 -- a deep blue whose actual colour
# is decided by an artist's swatch and stated nowhere.
#
# Dividing it out means the number below *is* the emitted radiance, and can be
# read as one. 11 / 14 / 20 is a blue-white: luminance 13.8, which sits in the
# band the room's own sources occupy (the ceiling at 4.7, the car's headlamps
# at 19-24) and well clear of the profile's 2.28 bloom threshold, so the
# reactor and the eyes carry a halo. The blue channel at 20 stays under the
# 28 bloom clamp with room for the powered-up level SuitLights raises it to.
MASK_LINEAR = (0.3005, 0.4969, 1.0)
GLOW = (11.0, 14.0, 20.0)

# And what it goes to when the switch in the bar powers the suit up. A third
# brighter, which is a visible step on the surfaces themselves rather than only
# in the pools the lamps throw -- and 26 in the blue keeps the brightest
# channel under the profile's bloom clamp of 28, which is where the halo stops
# growing and starts flattening.
POWERED = (16.0, 20.0, 26.0)


def unmask(colour):
    """The `Emissive` a material needs in order to emit `colour`.

    The mask cancels: `EmissiveColor * emissiveMap` with every lit texel at the
    same value means the map is a constant, and dividing it out leaves the
    factor that produces the radiance asked for.
    """
    return tuple(colour[i] / MASK_LINEAR[i] for i in range(3))


EMISSIVE = unmask(GLOW) + (1.0,)

# --- where the light actually comes from --------------------------------------
#
# **Found in the model, the way the car's headlamps were.** Each vertex's uv
# was looked up in the emissive map and the lit ones clustered, which puts the
# reactor, the palms, the eyes, the boot jets and the helmet temples at known
# coordinates instead of at a guess off a photograph. In the model's own frame,
# which is Z-up with -Y forward:
#
#     n=184 x2  (+-191.3,   41.4,   13.0)  boot jets, at the soles
#     n= 34     (   5.7, -202.6, 1799.3)  the arc reactor
#     n= 24     (   0.0, -140.9, 2151.3)  the eye slits, 115 mm across
#     n= 22/21  (+- 93.9,   10.0, 2159.6)  the helmet temples
#     n=  9 x2  (+-438.9,  -39.7, 1194.6)  the palms, arms at the sides
#
# Three of those get a light. The eyes and the temples do not: a lamp in front
# of a face lights the face, which is the one thing a glowing eye must not do.
# The boot jets do not either -- they sit 13 mm off a polished floor and their
# own reflection in it is already the pool a light there would try to draw.
REACTOR_LOCAL = (5.7, -202.6, 1799.3)
PALM_LEFT_LOCAL = (-438.9, -39.8, 1194.6)
PALM_RIGHT_LOCAL = (438.9, -39.6, 1194.5)

# Clear of the bodywork. Inside it, the first thing a light does is light the
# back of the panel it came from -- the same reason the car's headlamp cones
# start 6 cm ahead of the lens.
REACTOR_STANDOFF = 0.03
PALM_DROP = 0.05

# A repulsor is not white. The glow above is the emitted colour and these are
# the same hue, so the pool on the floor belongs to the thing that cast it.
GLOW_COLOUR = "[0.55, 0.72, 1]"


def to_world(local, offset=(0.0, 0.0, 0.0)):
    """A point in the model's frame, in metres, where the scene will see it.

    The subtree's own transform chain takes the exporter's Z-up, -Y-forward
    space to the engine's Y-up, +Z-forward one -- so height is the model's Z
    and the front of the chest is its -Y. The offset is applied in the world
    frame, before the figure's yaw, so "3 cm in front of the chest" stays in
    front of the chest however the figure is turned.
    """
    position = (local[0] * FIGURE_SCALE + offset[0],
                local[2] * FIGURE_SCALE + FIGURE_LIFT + offset[1],
                -local[1] * FIGURE_SCALE + offset[2])
    return showroom.rotate_y(position, FIGURE_YAW)


def suit_material():
    """One `.rmat` beside the room's own, and one handle swapped on the way in.

    **Written here rather than edited in place**, for the reason the car's
    overrides give: rvimport regenerates `iron_man_0_default.rmat` from the
    glTF every time it runs, so an edit to that file is a change waiting to be
    overwritten. A material this generator owns cannot be clobbered by an
    import.

    Every map the model came with is kept -- there is no reason to drop any of
    them, which is the difference between this subject and the car.
    """
    source = ASSETS / "models" / "showroom2" / "iron_man_0_default.rmat"
    if not source.exists():
        raise SystemExit(
            f"{source.relative_to(ROOT)} is missing. Run:\n"
            "  build/bin/Release/rvimport/rvimport.exe "
            f"{MODEL} --out={FIGURE_SUBTREE.relative_to(ROOT).as_posix()}")

    text = source.read_text(encoding="utf-8")
    _, _, maps = text.partition("Maps:")

    kept = {}
    for line in maps.strip().split("\n"):
        if ":" in line:
            key, value = line.split(":", 1)
            kept[key.strip()] = value.strip()

    meta = source.with_name(source.name + ".meta")
    old = int(meta.read_text(encoding="utf-8").split("Handle:")[1].split("\n")[0])

    new = write_material(
        ASSETS / "materials" / "showroom2_suit.rmat", kept, tiling=(1, 1),
        height_scale=0.0, metallic=METALLIC, roughness=ROUGHNESS,
        base_color=(1, 1, 1, 1), emissive=EMISSIVE, specular=0.5,
        normal_scale=NORMAL_SCALE)

    return {old: new}


IRON_MAN = showroom.Subject(
    key="showroom2",
    label="figure",
    title="The showroom, with a suit in it",
    credit=CREDIT,
    model=MODEL,
    subtree=FIGURE_SUBTREE,
    root_tag="iron_man",
    materials=suit_material,

    root_scale=(FIGURE_SCALE, FIGURE_SCALE, FIGURE_SCALE),
    root_position=(0.0, FIGURE_LIFT, 0.0),
    root_rotation=(0.0, FIGURE_YAW, 0.0),

    # **The orbit, and the only thing about the room that moves.** The car was
    # framed from 7.3 m at a 40 degree lens, which covers 5.3 m of height at
    # the subject -- and the car uses that across its length rather than its
    # height. A standing figure has only height to spend, so the camera comes
    # in to 5.4 m, where 3.9 m of coverage puts two metres of suit across half
    # the frame.
    #
    # The target rises with it. 1.05 is the chest, and at 5 degrees above it
    # the camera stands at 1.52 m -- eye level with the figure, which is the
    # height a person is photographed from and is nothing like the height a
    # car is.
    target=(0.0, 1.05, 0.0),
    pitch=5.0,
    distance=5.4,
    min_distance=3.2,
    max_distance=9.0,

    lamps=(
        # The reactor: a point source, because a chest emitter has no aim. It
        # is what puts blue under the jaw, along the inside of both arms and
        # on the floor between the feet -- the parts of a standing figure the
        # ceiling cannot reach.
        ("Arc Reactor Glow", "Point",
         to_world(REACTOR_LOCAL, (0.0, 0.0, REACTOR_STANDOFF)),
         to_world(REACTOR_LOCAL, (0.0, 0.0, 3.0)),
         GLOW_COLOUR, 4.0, 30, 60),

        # The palms: aimed straight down, because they are. Two crisp pools
        # either side of the feet read as repulsors idling; the same light
        # spread over the room reads as a blue fill nobody switched on.
        ("Repulsor Left", "Spot",
         to_world(PALM_LEFT_LOCAL, (0.0, -PALM_DROP, 0.0)),
         to_world((PALM_LEFT_LOCAL[0], PALM_LEFT_LOCAL[1], MODEL_FLOOR),
                  (0.0, -0.6, 0.0)),
         GLOW_COLOUR, 2.6, 18, 52),
        ("Repulsor Right", "Spot",
         to_world(PALM_RIGHT_LOCAL, (0.0, -PALM_DROP, 0.0)),
         to_world((PALM_RIGHT_LOCAL[0], PALM_RIGHT_LOCAL[1], MODEL_FLOOR),
                  (0.0, -0.6, 0.0)),
         GLOW_COLOUR, 2.6, 18, 52),
    ),

    lights=("SuitLights", dict(
        SuitRoot="'iron_man'",
        GlowParts="'Object_'",
        CoreLamps="'Arc Reactor Glow'",
        HandLamps="'Repulsor Left,Repulsor Right'",
        LabelName="'Lights Label'",
        CoreIntensity=22,
        HandIntensity=14,
        # In the material's factor space, not in emitted radiance: the
        # script writes an entity override that the shader multiplies by the
        # same mask, so the tint has to be cancelled here too.
        PoweredEmissive="'" + " ".join(f"{v:g}" for v in unmask(POWERED)) + "'",
        StartOn="false")),
)


def main():
    showroom.main(IRON_MAN)


if __name__ == "__main__":
    main()
