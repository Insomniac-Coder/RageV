#!/usr/bin/env python3
"""A fixture for local-light and skinned ray-traced shadows (8.12 stage 2).

No sun. A grey floor seen from almost straight above, and three lights that
each own a patch of it: a spot on the left throwing a box's shadow, a point
light on the right throwing another box's, and a low spot in the middle
throwing the running fox's shadow back across the floor. Each light's cone
or range stops short of the other two patches, so a region on the floor is
shadowed by one light and one caster, and the traced and mapped frames can
be compared region by region. ENGINE-NOTES 7an.

The fox runs in place under a fixed step, so frame 30 and frame 45 are two
different poses of a caster whose transform did not move -- which is the
distinction between a structure refit from the pose and one built once from
the bind pose.

The camera and the geometry are stated here and read back by
check_ray_shadows, which fixes its regions from them.

    python tools/scripts/make_ray_shadow_local_scene.py
"""

import math

import make_motion_scene as base

# Almost straight down: the shadows lie on the floor and nothing stands
# between them and the camera. Not exactly -pi/2, which is where a look-at
# loses its up vector.
CAMERA_POSITION = (0.0, 15.0, -1.0)
CAMERA_PITCH = -1.5
CAMERA_FOV_DEGREES = 60.0

FLOOR_HALF = 30.0

# The spot on the left: above and in front of its box, aimed at it, so the
# shadow falls behind the box (toward -z) and its cone (40 degrees) reaches
# nowhere near the middle or the right.
SPOT_POSITION = (-9.0, 6.0, 4.0)
SPOT_BOX_CENTRE = (-9.0, 1.0, 0.0)
SPOT_BOX_SCALE = (1.5, 2.0, 1.5)
SPOT_OUTER_DEGREES = 40.0
SPOT_INNER_DEGREES = 30.0

# The point light on the right, over its box the same way; a range that
# reaches its own shadow and not the fox's patch.
POINT_POSITION = (9.0, 5.0, 4.0)
POINT_BOX_CENTRE = (9.0, 1.0, 0.0)
POINT_BOX_SCALE = (1.5, 2.0, 1.5)
POINT_RANGE = 13.0

# A lid over each local light: a slab *beyond* the light from the floor,
# which shadows nothing under either method -- a map's frustum starts at the
# light, and a ray toward the light stops there. It is in the scene so that
# a ray that did not stop would show it: with tMax ignored the whole patch
# under each light goes dark. One unit above its light and wide, so every
# ray from the measured region that overshot would meet it; placed where the
# camera's view of the shadows does not pass through it (its image lands on
# floor nearer the camera than the shadow).
SPOT_LID_CENTRE = (-9.0, 7.0, 4.0)
POINT_LID_CENTRE = (9.0, 6.0, 4.0)
LID_SCALE = (8.0, 0.2, 8.0)

# Three more casting spots, aimed straight down at floor the camera does not
# see, so the scene has five spot lights asking to cast: one over the maps'
# budget of four. Under maps the fifth is dropped with a warning; under rays
# every one is assigned and nothing warns. Off-screen so the frame is the
# same either way and only the log differs.
BUDGET_SPOTS = ((-25.0, 6.0, 0.0), (25.0, 6.0, 0.0), (0.0, 6.0, -20.0))

# The fox's spot: low and in front, so the shadow is long and the light sees
# the fox side-on -- the legs' silhouette is what changes pose to pose.
FOX_SPOT_POSITION = (0.0, 3.0, 6.0)
FOX_SPOT_AIM = (0.0, 0.6, 0.0)
FOX_SPOT_OUTER_DEGREES = 30.0
FOX_SPOT_INNER_DEGREES = 22.0
FOX_POSITION = (0.0, 0.0, 0.0)
FOX_YAW = math.pi / 2          # facing +x: side-on to a light at +z
FOX_SCALE = 0.02
FOX_MESH = 11679010045657754579  # fox.glb, the sample project's own
FOX_CLIP = 2                     # Run


def _pitch_toward(source, target):
    """The x rotation that aims a light at `target`, yaw zero: forward is -z
    tilted down by the elevation angle."""
    dx, dy, dz = (t - s for s, t in zip(source, target))
    assert abs(dx) < 1e-6, "aim straight down the -z line; a yaw is not derived here"
    return -math.atan2(-dy, -dz)


def _box(next_id, tag, position, scale, base_rgb):
    return [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        f"      Tag: {tag}",
        "    TransformComponent:",
        f"      Position: {base.vec(position)}",
        "      Rotation: [0, 0, 0]",
        f"      Scale: {base.vec(scale)}",
        "    MeshComponent:",
        f"      Mesh: {base.PRIMITIVE_BASE + base.CUBE}",
        "      Material:",
        f"        BaseColor: {base.vec(list(base_rgb) + [1])}",
        "        Emissive: [0, 0, 0, 1]",
        "        Metallic: 0",
        "        Roughness: 1",
        "        Occlusion: 1",
    ]


def _light(next_id, tag, kind, position, rotation, intensity, range_, inner, outer):
    return [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        f"      Tag: {tag}",
        "    TransformComponent:",
        f"      Position: {base.vec(position)}",
        f"      Rotation: {base.vec(rotation)}",
        "      Scale: [1, 1, 1]",
        "    LightComponent:",
        f"      Type: {kind}",
        "      Color: [1, 1, 1]",
        f"      Intensity: {intensity:g}",
        f"      Range: {range_:g}",
        f"      InnerCone: {inner:g}",
        f"      OuterCone: {outer:g}",
        "      CastShadows: true",
    ]


def _fox(next_id):
    return [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        "      Tag: Fox",
        "    TransformComponent:",
        f"      Position: {base.vec(FOX_POSITION)}",
        f"      Rotation: {base.vec((0, FOX_YAW, 0))}",
        f"      Scale: {base.vec((FOX_SCALE,) * 3)}",
        "    MeshComponent:",
        f"      Mesh: {FOX_MESH}",
        "      Material: 0",
        "    AnimatorComponent:",
        f"      Clip: {FOX_CLIP}",
        "      Playing: true",
        "      Loop: true",
        "      Speed: 1",
    ]


def build(profile):
    next_id = base._ids()
    lines = base._header("Ray shadow locals")
    lines += base._camera(next_id, CAMERA_POSITION, rotation=(CAMERA_PITCH, 0, 0),
                          profile=profile)
    lines += _box(next_id, "Floor", (0, -0.1, 0), (2 * FLOOR_HALF, 0.2, 2 * FLOOR_HALF),
                  (0.6, 0.6, 0.6))

    lines += _light(next_id, "Spot", "Spot", SPOT_POSITION,
                    (_pitch_toward(SPOT_POSITION, SPOT_BOX_CENTRE), 0, 0),
                    intensity=60, range_=20,
                    inner=SPOT_INNER_DEGREES, outer=SPOT_OUTER_DEGREES)
    lines += _box(next_id, "Spot box", SPOT_BOX_CENTRE, SPOT_BOX_SCALE, (0.7, 0.3, 0.2))
    lines += _box(next_id, "Spot lid", SPOT_LID_CENTRE, LID_SCALE, (0.5, 0.5, 0.5))

    lines += _light(next_id, "Point", "Point", POINT_POSITION, (0, 0, 0),
                    intensity=100, range_=POINT_RANGE, inner=20, outer=30)
    lines += _box(next_id, "Point box", POINT_BOX_CENTRE, POINT_BOX_SCALE, (0.2, 0.3, 0.7))
    lines += _box(next_id, "Point lid", POINT_LID_CENTRE, LID_SCALE, (0.5, 0.5, 0.5))

    lines += _light(next_id, "Fox spot", "Spot", FOX_SPOT_POSITION,
                    (_pitch_toward(FOX_SPOT_POSITION, FOX_SPOT_AIM), 0, 0),
                    intensity=40, range_=20,
                    inner=FOX_SPOT_INNER_DEGREES, outer=FOX_SPOT_OUTER_DEGREES)
    lines += _fox(next_id)

    # **Last in the file, and that is load-bearing.** The spot shadow budget
    # is four (ShadowMap::kMaxLocal) and this scene asks for five, so one spot
    # lights without casting -- and the one dropped must be an off-screen
    # filler, never the spot over the box or the one on the fox, or the mapped
    # frame this check measures against loses a shadow the traced frame still
    # has.
    #
    # These were *first* in the file while the registry walked lights newest
    # first, which is the order EnTT gave. The purpose-built ECS walks
    # creation order instead (ECS.h, View::Iterator), so first became first,
    # the fillers took the budget, and the light left without a map was the
    # **fox** spot. The traced fox shadow was then measured against a mapped
    # fox shadow that no longer existed: IoU 0.358, the mapped mask a strict
    # subset of the traced one, and not one traced pixel changed.
    #
    # The comment this replaces promised that a changed order "would say
    # which light lost its map". It did not -- the warning in Scene.cpp does
    # not name the light -- which is why this cost a diagnosis, not a glance.
    for i, position in enumerate(BUDGET_SPOTS):
        lines += _light(next_id, f"Budget spot {i + 1}", "Spot", position,
                        (-math.pi / 2 + 0.01, 0, 0),
                        intensity=20, range_=10, inner=20, outer=30)

    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    import pathlib
    import sys
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    import postprofile
    root = pathlib.Path(__file__).resolve().parents[2]
    scene = root / "SampleProject" / "assets" / "scenes" / "ray_shadows_local.rage"
    handle = postprofile.write_beside(scene, {"BloomEnabled": False})
    scene.write_text(build(handle))
    print(f"wrote {scene}")
