#!/usr/bin/env python3
"""Build showroom-mark85: the delivery bay again, with the Mark 85 in it.

    build/bin/Release/rvimport/rvimport.exe models/mark85/mark85.fbx \\
        --out=tools/scripts/data/showroom_mark85_figure.yaml
    python tools/scripts/make_showroom_mark85_scene.py

**The third scene out of one room.** `make_showroom_scene.py` builds the bay
and takes a `Subject`; the car is one, the glTF suit in `showroom2.rage` is
another, and this is the third. Nothing about the room is described here -- see
that file's Subject class for why it cannot be, and `make_showroom2_scene.py`
for the other half of the pair this file is closest to.

**Where this subject differs from the glTF one, and it is the whole of the
work.** `iron_man.glb` arrived nearly finished: one material carrying a base
colour, a metallic-roughness pair, a normal map and an emissive mask keyed
exactly to the parts that glow. This model arrives as six materials with *no
maps at all* -- flat Lambert colours and roughness zero, which is what an FBX
exported out of a Substance workflow contains, because the maps are baked
beside it and assigned in the material editor rather than written into the
file.

The maps are all there: eighteen of them, 4096 square, named after the material
they belong to. So this file does what the car's generator does with its livery
-- reattaches them by name, and says out loud that it is the *scene* making a
claim about the model rather than the importer guessing. An importer that went
looking for unreferenced images in a folder and bound them by hope would be
wrong far more often than it was right.
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import make_showroom_scene as showroom                          # noqa: E402
from make_demo_scene import write_material                      # noqa: E402

ROOT = showroom.ROOT
ASSETS = showroom.ASSETS
MODEL_DIR = ASSETS / "models" / "mark85"
FIGURE_SUBTREE = ROOT / "tools" / "scripts" / "data" / "showroom_mark85_figure.yaml"

MODEL = "models/mark85/mark85.fbx"

CREDIT = ('"Iron Man Mark 85 (rigged)" by its Sketchfab author, used here '
          'under the terms it was published with. See '
          'assets/models/mark85/mark85.fbx.ATTRIBUTION.md.')

# --- how big it is ------------------------------------------------------------
#
# **The importer already did the unit conversion**, which is the difference
# between this and the glTF suit. `FbxImporter` asks ufbx for a right-handed
# Y-up metre space on load (`target_unit_meters = 1`), so an FBX arrives in the
# engine's own units however its exporter measured. The mesh nodes still carry
# the artist's own scale of 10, and that is theirs to keep.
#
# So the only number here is the height wanted, and the measured height it is
# divided by. Both are metres; a scale of 1 would be the model at whatever the
# artist built it as.
# Both read off the importer, which now reports them: it prints an imported
# model's size and where its origin sits inside it, precisely so that a scene
# placing one does not have to recover the numbers by rendering it and counting
# pixels -- which is exactly how this file's first three attempts were written.
#
#     'mark85' measures 0.0393 x 0.0666 x 0.0134,
#              its origin 0.0001 above its own floor
#
# So it is a twentieth of a unit tall and stands on its own origin, and the
# only thing to decide is how tall it should be. Two metres, the same as the
# suit in showroom2 and for the same reason.
MODEL_HEIGHT = 0.0666
MODEL_FLOOR = -0.0001
FIGURE_HEIGHT = 2.0

FIGURE_SCALE = FIGURE_HEIGHT / MODEL_HEIGHT
FIGURE_LIFT = -MODEL_FLOOR * FIGURE_SCALE

# The same three-quarter turn the glTF suit stands at, and for the same reason:
# square to the camera is a passport photograph.
FIGURE_YAW = -18.0

# --- what the model does not say ----------------------------------------------
#
# **All six materials arrive flat.** Base colour a single Lambert value,
# metallic 0, roughness 0 -- and `FbxImporter` is right not to improve on that:
# it deliberately refuses to fabricate PBR out of a Phong shininess, because a
# roughness guessed from an exponent is a number nobody can debug six months
# later. What the file says is what the file says.
#
# What the file does not say is sitting next to it. Every map below is one of
# the eighteen PNGs in the model's own folder, matched to its material by the
# name the artist gave it.
MARK85_MAPS = {
    "Silver Part": "Silver_Part",
    "Gold Part":   "Gold_Part",
    "Red Part":    "Red_Part",
    "Arc Reactor": "Arc_Reactor",
    "Lights":      "Lights",
}

# The suffixes Substance wrote, truncated as its exporter truncates them.
MAP_SUFFIXES = {
    "BaseColor": ("BaseColor", "BaseColo", "BaseCo"),
    "Metallic":  ("Metallic", "Metall"),
    "Roughness": ("Roughness", "Roughnes", "Roughn"),
    "Normal":    ("Normal",),
}

# --- and what it says wrongly -------------------------------------------------
#
# **The metallic maps are 0.97 and the owner's answer is about half of that**,
# which is the same argument the glTF suit's 0.55 settled: a pure conductor has
# no diffuse term, so under a ceiling-sized source in a charcoal room the red
# and the gold collapse to a near-black mirror. The map still does the work of
# saying *where* the metal is; this decides how much of one it is.
METALLIC = 0.55

# The roughness maps are the artist's and are kept whole -- 0.13 at the polished
# panels to 0.35 at the scuffs, which is the variation that makes the suit read
# as something that has been worn rather than rendered. A factor of 1 is the
# map exactly.
ROUGHNESS = 1.0

# --- the glow -----------------------------------------------------------------
#
# **Two whole materials rather than a mask, and that is the easier case.** The
# glTF suit is one material over three meshes with an emissive map picking out
# 28,012 texels; here the artist separated the lit parts into their own
# materials -- `Lights` for the eyes, the palms and the chest ring, `Arc
# Reactor` for the core -- so the emissive is a property of the material and
# needs no mask at all.
#
# `Lights` already carries the colour: its Lambert base is (0, 0.559, 1), the
# cyan the suit glows. So the emissive is that colour raised, rather than a
# number invented here.
LIGHTS_GLOW = (2.0, 11.0, 19.0)

# The reactor is the hotter of the two and closer to white at the core, which
# is what a source that bright looks like through a tone mapper.
REACTOR_GLOW = (13.0, 16.0, 20.0)

GLOW_COLOUR = "[0.35, 0.72, 1]"


def texture_handle(stem):
    """An imported texture's handle, read from the `.meta` the registry wrote.

    **Not `handle_for(name)`**, which is what the room's own generated textures
    use. Those are minted by the script that draws them, from the file name; an
    imported asset's handle is minted by the asset registry when it first sees
    the file, and the only place it exists is the sidecar. Computing one here
    would produce a number that resolves to nothing.
    """
    for candidate in MODEL_DIR.glob("*.png.meta"):
        if candidate.name.lower().startswith(stem.lower() + ".png"):
            text = candidate.read_text(encoding="utf-8")
            return int(text.split("Handle:")[1].split("\n")[0])
    return None


def find_map(part, kind):
    """The PNG for one material and one map kind, by the artist's own naming."""
    for suffix in MAP_SUFFIXES[kind]:
        stem = f"For_Substance_Low_Polly_{part}_{suffix}"
        handle = texture_handle(stem)
        if handle is not None:
            return handle
    return None


def mark85_materials():
    """Six materials, re-authored beside the model's own.

    Returns {old handle: new handle}, which the subtree splice rewrites -- the
    same arrangement the car uses, and for the same reason: rvimport rewrites
    the `.rmat`s it made every time it runs, so an edit to one is a change
    waiting to be overwritten.
    """
    directory = ASSETS / "materials"
    remap = {}

    for source in sorted(MODEL_DIR.glob("mark85_*.rmat")):
        meta = source.with_name(source.name + ".meta")
        if not meta.exists():
            continue

        old = int(meta.read_text(encoding="utf-8").split("Handle:")[1].split("\n")[0])
        name = source.stem.split("_", 2)[-1]          # "Silver Part", "Lights", ...

        maps = {}
        part = MARK85_MAPS.get(name)
        if part:
            for kind in ("BaseColor", "Metallic", "Roughness", "Normal"):
                handle = find_map(part, kind)
                if handle is not None:
                    maps[kind] = handle

        # The base colour is white wherever a map supplies it, so the texture
        # is the whole of the colour; where there is none, the model's own
        # Lambert value is kept.
        base = (1, 1, 1, 1)
        emissive = (0, 0, 0, 1)
        metallic = METALLIC
        roughness = ROUGHNESS
        specular = 0.5

        if "BaseColor" not in maps:
            for line in source.read_text(encoding="utf-8").split("\n"):
                if line.startswith("BaseColor: ["):
                    base = tuple(float(v) for v in
                                 line.split("[", 1)[1].rstrip("]").split(","))

        if name == "Lights":
            emissive = LIGHTS_GLOW + (1,)
            # An emitter is not metal, and a rough one is not an emitter
            # either -- the surface is a diffuser over a light.
            metallic, roughness = 0.0, 0.35
        elif name == "Arc Reactor":
            # The housing behind the lens above. Never in frame on this model,
            # so it is lit only enough that an open chest -- or a camera that
            # gets inside one -- does not find a dead grey shell.
            emissive = tuple(v * 0.25 for v in REACTOR_GLOW) + (1,)
            metallic, roughness = 0.0, 0.30
        elif name == "Glass":
            # **`Glass` is the reactor's lens, and it is the only thing on the
            # chest you can see.** Named as though it were the visor, and it is
            # not: tinting each of the six materials a flat colour and
            # rendering once put cyan on the chest triangle and nowhere else.
            # `Arc Reactor` -- the obvious candidate, and the one this file
            # lit first -- never appears at all: it is the housing *behind*
            # the lens, and the lens is opaque.
            #
            # So the glow belongs here. Treated as a dark coated pane, which
            # is what the name suggested, it rendered as the dull grey patch
            # on the chest that started this.
            emissive = REACTOR_GLOW + (1,)
            base = (1, 1, 1, 1)
            metallic, roughness, specular = 0.0, 0.25, 0.5

        target = directory / f"showroom_mark85_{name.lower().replace(' ', '_')}.rmat"
        remap[old] = write_material(
            target, maps, tiling=(1, 1), height_scale=0.0,
            metallic=metallic, roughness=roughness, base_color=base,
            emissive=emissive, specular=specular)

    return remap


# --- where the light comes from -----------------------------------------------
#
# Positioned on the figure rather than measured out of it, unlike the glTF
# suit's -- that model's emissive mask gave exact coordinates for every lit
# texel, and this one's lit parts are whole meshes whose bounds are the
# meshes. Chest height and palm height on a two-metre figure are the numbers
# they would have been anyway.
REACTOR_AT = (0.0, 1.40, 0.22)
PALM_LEFT_AT = (-0.34, 0.92, 0.06)
PALM_RIGHT_AT = (0.34, 0.92, 0.06)


def turned(point):
    return showroom.rotate_y(point, FIGURE_YAW)


MARK85 = showroom.Subject(
    key="showroom-mark85",
    label="mark85",
    title="The showroom, with the Mark 85 in it",
    credit=CREDIT,
    model=MODEL,
    subtree=FIGURE_SUBTREE,
    root_tag="mark85",
    materials=mark85_materials,

    root_scale=(FIGURE_SCALE, FIGURE_SCALE, FIGURE_SCALE),
    root_position=(0.0, FIGURE_LIFT, 0.0),
    root_rotation=(0.0, FIGURE_YAW, 0.0),

    # The same orbit the other suit stands in, because it is the same subject
    # at the same height in the same room.
    target=(0.0, 1.05, 0.0),
    pitch=5.0,
    distance=5.4,
    min_distance=3.2,
    max_distance=9.0,

    lamps=(
        ("Arc Reactor Glow", "Point",
         turned(REACTOR_AT), turned((REACTOR_AT[0], REACTOR_AT[1], 3.0)),
         GLOW_COLOUR, 4.0, 30, 60),
        ("Repulsor Left", "Spot",
         turned(PALM_LEFT_AT), turned((PALM_LEFT_AT[0], -0.6, PALM_LEFT_AT[2])),
         GLOW_COLOUR, 2.6, 18, 52),
        ("Repulsor Right", "Spot",
         turned(PALM_RIGHT_AT), turned((PALM_RIGHT_AT[0], -0.6, PALM_RIGHT_AT[2])),
         GLOW_COLOUR, 2.6, 18, 52),
    ),

    lights=("SuitLights", dict(
        SuitRoot="'mark85'",
        # The lit meshes by name, which this model makes easy: the artist put
        # them in their own materials and rvimport names a part after the
        # material it wears.
        GlowParts="'Lights,Arc Reactor'",
        CoreLamps="'Arc Reactor Glow'",
        HandLamps="'Repulsor Left,Repulsor Right'",
        LabelName="'Lights Label'",
        CoreIntensity=22,
        HandIntensity=14,
        # Raised from the material's resting glow. No mask to cancel here --
        # the whole mesh is the emitter -- so this is the emitted colour
        # directly, unlike showroom2's.
        PoweredEmissive="'18 24 30'",
        StartOn="false")),
)


def main():
    showroom.main(MARK85)


if __name__ == "__main__":
    main()
