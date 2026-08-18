#!/usr/bin/env python3
"""A white corner beside a red wall, for measuring colour bleed (7at).

Global illumination's one visible claim is that light picks up the colour of
what it bounced off. So the fixture is the smallest scene where that is the
*only* explanation for a red pixel: a white floor and a white wall meeting a
saturated red one, lit by a single white directional light, with no sky and no
ambient. Nothing in the frame is red but the red wall itself -- so any red on
the white surfaces arrived by bouncing, and its amount is what the check
measures.

Four views come out of one room:

- `gi_corner.rage` looks into the corner, with the red wall in frame: the
  white wall **near** it must redden when GI is on, and the same wall **far**
  along must redden far less -- bleed, rather than a global tint.
- `gi_detail.rage` is the corner view with a cluster of small cubes standing
  where the bounce is strongest: a receiver with occlusion and faces at many
  angles, which is a harder place for the ray-traced form's denoiser to settle
  on the right value than two flat walls. It was built for a check that the
  filter keeps the bounce's *structure*, and that check was abandoned --
  ENGINE-NOTES 7aw records why, and it is a finding about diffuse light rather
  than about this fixture.
- `gi_skylit.rage` is the corner view with the sun off and a grey sky, so every
  photon in the room is environment light and the probe already accounts for
  all of it. A GI term that adds the sky again shows here and nowhere else --
  ENGINE-NOTES 7bb.
- `gi_away.rage` is the same room with the camera turned until the red wall is
  **off screen**. The white wall is still lit by light bounced off it, so the
  screen-space form has nothing red to gather and must add none, while the
  traced form -- whose rays do not care what is in frame -- must still redden
  it. That is the difference the two forms exist to have, in one frame.

Emissive is not used here, unlike the SSAO and motion fixtures: a bounce is
light, and an unlit surface has none to give. The surfaces are ordinary
diffuse ones under one light, which is the situation GI is for.

    python tools/scripts/make_gi_scene.py
"""

import pathlib

import make_motion_scene as base
import postprofile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENES = ROOT / "SampleProject" / "assets" / "scenes"

# The camera looks into the corner from outside it, high enough to see the
# floor and both walls. Stated here because the check projects with it.
CAMERA_POSITION = (5.0, 3.2, 5.0)
CAMERA_ROTATION = (-0.30, 0.785, 0.0)
CAMERA_FOV_DEGREES = 60.0

# One white light from above and to the side, so the red wall is lit and the
# white wall beside it is lit -- both have to be lit for a bounce to exist.
SUN_ROTATION = (-0.95, 0.35, 0.0)
SUN_INTENSITY = 4.0

# Where the walls are, in metres. The red wall stands in the x = -4 plane;
# the white wall runs along z = -4 and meets it at the corner.
RED_X = -4.0
WHITE_Z = -4.0
WALL_HEIGHT = 4.0
WALL_LENGTH = 8.0

# A white slab standing in the room. Not measured: it is there so the scene
# has something between the walls to occlude and receive, which is what makes
# the near/far difference a bounce rather than a gradient.
AWAY_PANEL = (2.2, 1.1, 1.2)

# A cluster of small white cubes at assorted angles, standing on the floor
# where the bounce is strongest.
#
# **These were put here for a claim that did not survive** (ENGINE-NOTES 7aw).
# The idea was that a receiver covered in edges would give the indirect term
# structure at the scale the denoiser's neighbourhood clamp works at, so a
# filter that leaked across an edge could be caught. The cluster does show a
# hundred and thirty display levels between its brightest face and its dimmest
# -- and smearing the indirect buffer over seventeen texels changes the picture
# by 0.83 of a level, because that variation is one *smooth* field arriving on
# faces at different angles rather than a field with structure in it.
#
# What they are good for is claim 7: a receiver with occlusion and many
# orientations is a harder place to settle on the right value than two flat
# walls. And a moving-camera check of the clamp -- which is the only kind that
# can work -- will want a scene that disoccludes, which this one does.
DETAIL_CUBE = 0.7
DETAIL_CUBES = [
    ((-3.1, 0.35, -0.4), 0.42),
    ((-2.1, 0.35, -0.9), 0.95),
    ((-1.2, 0.35, -0.2), 0.18),
    ((-3.3, 0.35,  0.7), 1.21),
    ((-2.3, 0.35,  0.4), 0.63),
    ((-1.4, 0.35,  1.1), 1.05),
    ((-3.0, 0.35,  1.9), 0.30),
    ((-2.0, 0.35,  1.7), 0.78),
    ((-1.1, 0.35,  2.4), 1.34),
    ((-2.8, 1.05, -0.1), 0.55),
    ((-1.7, 1.05,  0.9), 1.12),
    ((-2.5, 1.05,  1.6), 0.24),
]


def _block(next_id, tag, position, scale, colour, rotation=(0, 0, 0)):
    """A plain diffuse box: base colour, no emissive, rough."""
    return [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        f"      Tag: {tag}",
        "    TransformComponent:",
        f"      Position: {base.vec(position)}",
        f"      Rotation: {base.vec(rotation)}",
        f"      Scale: {base.vec(scale)}",
        "    MeshComponent:",
        f"      Mesh: {base.PRIMITIVE_BASE + base.CUBE}",
        "      Material:",
        f"        BaseColor: [{colour[0]:g}, {colour[1]:g}, {colour[2]:g}, 1]",
        "        Emissive: [0, 0, 0, 1]",
        "        Metallic: 0",
        "        Roughness: 0.9",
        "        Occlusion: 1",
    ]


def build(profile, camera=None, rotation=None, detail=False, skylit=False):
    next_id = base._ids()
    # A black sky and no ambient: every photon in the frame comes from the
    # one light, so a red pixel on a white wall has exactly one explanation.
    # Black sky and no ambient for the bleed fixtures: every photon comes from
    # the one light, so a red pixel on a white wall has exactly one explanation.
    # `skylit` is the other room (ENGINE-NOTES 7bb): the sun off and a grey sky,
    # so every photon is *environment* light and a surface's correct
    # irradiance is the probe's -- which is what a GI term that added the sky
    # a second time could not leave alone.
    sky = (0.5, 0.5, 0.5) if skylit else (0.0, 0.0, 0.0)
    lines = base._header("GI corner", sky_rgb=sky)
    lines += base._camera(next_id, camera or CAMERA_POSITION,
                          rotation=rotation or CAMERA_ROTATION, profile=profile)

    lines += [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        "      Tag: Sun",
        "    TransformComponent:",
        "      Position: [0, 8, 0]",
        f"      Rotation: {base.vec(SUN_ROTATION)}",
        "      Scale: [1, 1, 1]",
        "    LightComponent:",
        "      Type: Directional",
        "      Color: [1, 1, 1]",
        f"      Intensity: {0 if skylit else SUN_INTENSITY:g}",
        "      Range: 60",
        "      InnerCone: 20",
        "      OuterCone: 30",
        "      CastShadows: true",
    ]

    # White floor, white wall, red wall.
    lines += _block(next_id, "Floor", (0, -0.1, 0), (16, 0.2, 16), (0.85, 0.85, 0.85))
    lines += _block(next_id, "WhiteWall", (0, WALL_HEIGHT * 0.5, WHITE_Z - 0.1),
                    (WALL_LENGTH * 2, WALL_HEIGHT, 0.2), (0.85, 0.85, 0.85))
    lines += _block(next_id, "RedWall", (RED_X - 0.1, WALL_HEIGHT * 0.5, 0),
                    (0.2, WALL_HEIGHT, WALL_LENGTH * 2), (0.85, 0.03, 0.03))

    # The facing-away panel: its lit side is +z, away from the red wall.
    lines += _block(next_id, "AwayPanel", AWAY_PANEL, (2.2, 2.2, 0.2), (0.85, 0.85, 0.85))

    if detail:
        for index, (position, turn) in enumerate(DETAIL_CUBES):
            lines += _block(next_id, f"Detail{index}", position,
                            (DETAIL_CUBE, DETAIL_CUBE, DETAIL_CUBE),
                            (0.85, 0.85, 0.85), rotation=(0.0, turn, 0.0))

    return "\n".join(lines) + "\n"


# Bloom off, so a bright red wall does not bleed over the white one through
# the bloom chain and read as bounce; auto exposure off for the same reason
# every fixture turns it off -- two frames must mean the same thing.
CHECK_PROFILE = {"BloomEnabled": False}


# The second view: the same room, the camera turned until the red wall is *off
# screen* to the left. The white wall is still lit by light that bounced off
# it, so a screen-space gather has nothing red to find and a traced bounce
# does -- the whole difference between the two forms, in one frame.
AWAY_CAMERA_POSITION = (5.0, 3.2, 5.0)
AWAY_CAMERA_ROTATION = (-0.30, -0.25, 0.0)


def main():
    SCENES.mkdir(parents=True, exist_ok=True)
    scene = SCENES / "gi_corner.rage"
    text = build(postprofile.write_beside(scene, CHECK_PROFILE))
    scene.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {scene}")

    away = SCENES / "gi_away.rage"
    text = build(postprofile.write_beside(away, CHECK_PROFILE),
                 camera=AWAY_CAMERA_POSITION, rotation=AWAY_CAMERA_ROTATION)
    away.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {away}")

    # The third view: the corner again, with something for the bounce to land
    # on that has edges in it. See DETAIL_CUBES.
    detail = SCENES / "gi_detail.rage"
    text = build(postprofile.write_beside(detail, CHECK_PROFILE), detail=True)
    detail.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {detail}")

    # The fourth: the same room lit by nothing but a grey sky (7bb).
    skylit = SCENES / "gi_skylit.rage"
    text = build(postprofile.write_beside(skylit, CHECK_PROFILE), skylit=True)
    skylit.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {skylit}")


if __name__ == "__main__":
    main()
