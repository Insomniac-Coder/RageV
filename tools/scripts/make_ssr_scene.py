#!/usr/bin/env python3
"""A bright block on a mirror floor, for measuring SSR.

The floor is a metal at roughness `roughness` -- 0 is a mirror -- and *dark*
(black base colour, no emissive), so anything that appears on it below the
block's base line got there by reflection. The block is emissive and bright,
which gives the reflection something unmistakable to carry. The camera is
low and looking along the floor so the reflected block lands well inside the
frame. ENGINE-NOTES 7ad.

    python tools/scripts/make_ssr_scene.py
"""

import make_motion_scene as base


def _material_block(next_id, tag, position, scale, base_rgb, emissive_rgb,
                    metallic, roughness):
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


def main():
    import pathlib
    root = pathlib.Path(__file__).resolve().parents[2]
    out = root / "SampleProject" / "assets" / "scenes" / "ssr_mirror.rage"
    out.write_text(build(None))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
