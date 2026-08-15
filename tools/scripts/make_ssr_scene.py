#!/usr/bin/env python3
"""Two fixtures for measuring SSR.

`build`: a bright block on a mirror floor. The floor is a metal at roughness
`roughness` -- 0 is a mirror -- and *dark* (black base colour, no emissive),
so anything that appears on it below the block's base line got there by
reflection. The block is emissive and bright, which gives the reflection
something unmistakable to carry. The camera is low and looking along the
floor so the reflected block lands well inside the frame. ENGINE-NOTES 7ad.

`build_sphere`: a mirror sphere with a bright slab standing to its *left*.
A floor's normal has no sideways component in view space, and it turns out
that is the one case where a wrong normal transform is invisible: reflect()
cannot tell a normal from its negation, and the wrong map and the right one
differed by exactly a negation whenever x was zero. A sphere's normals point
every way. The slab's reflection has to land on the sphere's *left* limb --
a transform that mirrors x puts it on the right. ENGINE-NOTES 7ae.

    python tools/scripts/make_ssr_scene.py
"""

import make_motion_scene as base


def _material_block(next_id, tag, position, scale, base_rgb, emissive_rgb,
                    metallic, roughness, primitive=None):
    if primitive is None:
        primitive = base.CUBE
    return [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        f"      Tag: {tag}",
        "    TransformComponent:",
        f"      Position: {base.vec(position)}",
        "      Rotation: [0, 0, 0]",
        f"      Scale: {base.vec(scale)}",
        "    MeshComponent:",
        f"      Mesh: {base.PRIMITIVE_BASE + primitive}",
        "      Material:",
        f"        BaseColor: {base.vec(list(base_rgb) + [1])}",
        f"        Emissive: {base.vec(list(emissive_rgb) + [1])}",
        f"        Metallic: {metallic:g}",
        f"        Roughness: {roughness:g}",
        "        Occlusion: 1",
    ]


def build(profile, roughness=0.0):
    next_id = base._ids()
    lines = base._header("SSR block on mirror")

    # Low and level: the reflection of the block appears *below* it on the
    # floor, and a low camera keeps that reflection large and on screen.
    lines += base._camera(next_id, (0, 1.2, 6.0), rotation=(-0.12, 0, 0),
                          profile=profile)

    # A dark metal floor. Nothing on it is lit -- no lights in the scene,
    # black ambient -- so its only colour is what it reflects.
    lines += _material_block(next_id, "Floor", (0, -0.1, 0), (30, 0.2, 30),
                             (0.02, 0.02, 0.02), (0, 0, 0), 1.0, roughness)

    # The bright block, standing on the floor.
    lines += _material_block(next_id, "Block", (0, 1.0, 0), (1.6, 2.0, 1.6),
                             (0, 0, 0), (2.4, 1.6, 0.4), 0.0, 1.0)

    return "\n".join(lines) + "\n"


# The sphere fixture's geometry, shared with the check that measures it. The
# camera sits level with the sphere's centre and looks straight at it, so
# the sphere is a disc at the centre of the frame whose radius follows from
# the distance and the vertical field of view alone: asin(r / d) against
# tan(fov / 2). Stated here so the check does not have to find the sphere in
# a picture where, with reflections off, it is as dark as the sky.
SPHERE_RADIUS = 1.0
SPHERE_DISTANCE = 6.0
SPHERE_CENTRE_Y = 1.0
CAMERA_FOV_DEGREES = 60.0


def sphere_screen_radius_fraction():
    """The sphere's on-screen radius as a fraction of the frame height."""
    import math
    angular = math.asin(SPHERE_RADIUS / SPHERE_DISTANCE)
    half_height = math.tan(math.radians(CAMERA_FOV_DEGREES) / 2.0)
    return math.tan(angular) / half_height / 2.0


BLOCK_EMISSIVE = (2.4, 1.6, 0.4)


def build_exact(profile, sky_rgb):
    """The exactness fixture (9.9): the mirror-floor scene under a *uniform*
    sky, so the probe's reflected radiance is one known colour everywhere.

    A metal floor's lighting is its specular term alone -- no diffuse to
    change with the sky -- and that term is `prefiltered * weight`, where
    SSR replaces `prefiltered` where it is confident. So a floor pixel that
    reflects the block with SSR on, under any sky, must equal the same
    pixel with SSR off under a sky the colour of the block: same weight,
    same occlusion, same F0, only the reflected radiance swapped. That
    equality is what "exact replacement" means, and what the check
    measures. ENGINE-NOTES 7af.

    The block is a black metal, so its lit colour is its emissive and
    nothing else: F0 is zero, so it reflects nothing, and a metal has no
    diffuse -- the sky cannot leak into the colour the floor is asked to
    reproduce.
    """
    next_id = base._ids()
    lines = base._header("SSR exactness", sky_rgb=sky_rgb)
    lines += base._camera(next_id, (0, 1.2, 6.0), rotation=(-0.12, 0, 0),
                          profile=profile)
    # A brighter metal than the mirror fixture's, so the weight -- and any
    # error in it -- is large enough to see.
    lines += _material_block(next_id, "Floor", (0, -0.1, 0), (30, 0.2, 30),
                             (0.5, 0.5, 0.5), (0, 0, 0), 1.0, 0.0)
    lines += _material_block(next_id, "Block", (0, 1.0, 0), (1.6, 2.0, 1.6),
                             (0, 0, 0), BLOCK_EMISSIVE, 1.0, 1.0)
    return "\n".join(lines) + "\n"


def build_sphere(profile):
    next_id = base._ids()
    lines = base._header("SSR sphere beside a slab")

    lines += base._camera(next_id, (0, SPHERE_CENTRE_Y, SPHERE_DISTANCE),
                          rotation=(0, 0, 0), profile=profile)

    # A bright mirror -- light metal, so the reflection carries most of what
    # it sees -- and nothing else lit anywhere: no lights, black ambient, a
    # near-black sky. Roughness 0 is clamped by the shader to its floor.
    lines += _material_block(next_id, "Sphere", (0, SPHERE_CENTRE_Y, 0),
                             (2 * SPHERE_RADIUS,) * 3,
                             (0.9, 0.9, 0.9), (0, 0, 0), 1.0, 0.0,
                             primitive=base.SPHERE)

    # The slab: emissive, tall, and long along z so a ray leaving the
    # sphere's left limb at any angle between "sideways" and "straight
    # back" still meets it. Its face toward the sphere is what reflects.
    lines += _material_block(next_id, "Slab", (-3.0, SPHERE_CENTRE_Y, -1.0),
                             (1.0, 3.0, 6.0), (0, 0, 0), (2.4, 1.6, 0.4),
                             0.0, 1.0)

    return "\n".join(lines) + "\n"


def main():
    import pathlib
    root = pathlib.Path(__file__).resolve().parents[2]
    scenes = root / "SampleProject" / "assets" / "scenes"
    out = scenes / "ssr_mirror.rage"
    out.write_text(build(None))
    print(f"wrote {out}")
    out = scenes / "ssr_sphere.rage"
    out.write_text(build_sphere(None))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
