#!/usr/bin/env python3
"""A fixture for measuring ray-traced shadows against cascaded ones (8.12).

A grey floor under a low sun that shines *toward* the camera, so every
shadow falls forward onto the floor where the frame can see it. Two boxes:
one near, whose shadow both methods can draw and should draw alike; one far
past `ShadowDistance`, tall enough that its shadow reaches from beyond the
cascades' end back toward the camera. The maps have nothing recorded past
their distance and draw no shadow there; a ray does not have a distance.
ENGINE-NOTES 7am.

The camera and the sun are stated here and read back by check_ray_shadows,
which projects the boxes' footprints itself to find the regions to measure.

    python tools/scripts/make_ray_shadow_scene.py
"""

import make_motion_scene as base

CAMERA_POSITION = (0.0, 8.0, 18.0)
CAMERA_PITCH = -0.35          # radians, looking down toward the floor
CAMERA_FOV_DEGREES = 60.0     # vertical

# The sun travels toward +z (toward the camera) and 30 degrees down.
SUN_ROTATION = (-0.5236, 3.14159, 0.0)
SUN_ELEVATION_DEGREES = 30.0

NEAR_BOX_CENTRE = (-3.0, 1.5, 4.0)
NEAR_BOX_SCALE = (1.5, 3.0, 1.5)
# A wall, really: past the cascades' reach and tall enough that its shadow
# comes back toward the camera across the ground the maps do not cover.
FAR_BOX_CENTRE = (0.0, 30.0, -150.0)
FAR_BOX_SCALE = (30.0, 60.0, 30.0)


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
        "      Static: true",
        f"      Mesh: {base.PRIMITIVE_BASE + base.CUBE}",
        "      Material:",
        f"        BaseColor: {base.vec(list(base_rgb) + [1])}",
        "        Emissive: [0, 0, 0, 1]",
        "        Metallic: 0",
        "        Roughness: 1",
        "        Occlusion: 1",
    ]


def _sun(next_id):
    return [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        "      Tag: Sun",
        "    TransformComponent:",
        "      Position: [0, 20, 0]",
        f"      Rotation: {base.vec(SUN_ROTATION)}",
        "      Scale: [1, 1, 1]",
        "    LightComponent:",
        "      Type: Directional",
        "      Color: [1, 0.96, 0.9]",
        "      Intensity: 3",
        "      Range: 60",
        "      InnerCone: 20",
        "      OuterCone: 30",
    ]


def build(profile):
    next_id = base._ids()
    lines = base._header("Ray shadow boxes")
    lines += base._camera(next_id, CAMERA_POSITION, rotation=(CAMERA_PITCH, 0, 0),
                          profile=profile)
    lines += _sun(next_id)
    # A big flat floor, mid grey and rough, so a shadow on it is the absence
    # of the sun and nothing else: ambient is zero and the sky is near black.
    lines += _box(next_id, "Floor", (0, -0.1, -80), (600, 0.2, 600), (0.6, 0.6, 0.6))
    lines += _box(next_id, "Near box", NEAR_BOX_CENTRE, NEAR_BOX_SCALE, (0.7, 0.3, 0.2))
    lines += _box(next_id, "Far box", FAR_BOX_CENTRE, FAR_BOX_SCALE, (0.2, 0.3, 0.7))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    import pathlib
    import sys
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    import postprofile
    root = pathlib.Path(__file__).resolve().parents[2]
    scene = root / "SampleProject" / "assets" / "scenes" / "ray_shadows.rage"
    handle = postprofile.write_beside(scene, {"BloomEnabled": False})
    scene.write_text(build(handle))
    print(f"wrote {scene}")
