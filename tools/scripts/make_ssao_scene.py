#!/usr/bin/env python3
"""A box on a floor, for measuring SSAO's restraint as well as its darkness.

Any ambient occlusion darkens a corner; the failure that ships is darkening
what is *open* -- self-occlusion speckle across flat floors, halos around
silhouettes. So the fixture has exactly two things: a seam where geometry
meets geometry, which must darken, and a wide flat floor far from anything,
which must not. ENGINE-NOTES 7ac.

Emissive and unlit, like the motion fixture and for the same reason: the
occlusion applies as a multiply on the lit image, so a uniformly bright
surface makes the multiply the *only* thing that varies -- any change the
check measures is AO and nothing else.

    python tools/scripts/make_ssao_scene.py
"""

import make_motion_scene as base


def build(profile):
    next_id = base._ids()
    lines = base._header("SSAO box on floor")

    # Above and back, pitched down at the box, so the contact seam runs
    # across the middle of the frame with open floor either side of it.
    lines += base._camera(next_id, (0, 2.2, 4.2), rotation=(-0.42, 0, 0),
                          profile=profile)

    # The floor: wide enough that its edges are off screen, so nothing in
    # the open region is near a silhouette.
    lines += base._emissive_block(next_id, "Floor", (0, -0.1, 0),
                                  (24, 0.2, 24), (0.5, 0.5, 0.5))

    # The box, seated exactly on it. The seam where the two meet is the
    # thing an occlusion pass exists to find.
    lines += base._emissive_block(next_id, "Box", (0, 0.8, 0),
                                  (1.6, 1.6, 1.6), (0.5, 0.5, 0.5))

    return "\n".join(lines) + "\n"


# The courtyard's brick wall material: a normal map and a height map over
# flat geometry. Read from its .meta so the fixture follows the asset.
def wall_material_handle(root):
    meta = root / "SampleProject" / "assets" / "materials" / "courtyard_wall.rmat.meta"
    for line in meta.read_text().splitlines():
        if line.startswith("Handle:"):
            return int(line.split(":", 1)[1])
    raise RuntimeError(f"no Handle in {meta}")


def build_wall(profile, material):
    """A normal-mapped brick wall seen along its length, nothing near it (9.8b).

    The occluders in a screen-space kernel are the depth buffer, which is
    flat here; the *shading* normal is not, because the bricks are a normal
    map over that flat depth. A hemisphere oriented by the shading normal
    dips its lower taps into the wall along every mortar line and the wall
    darkens itself into mottling -- which is what the demo's left wall did
    the day SSAO started reading the attachment. Seen at a grazing angle,
    which is also where the depth reconstruction is at its worst, so the
    fixture asks the hard question of both normals at once. Lit by a flat
    sky so the only thing that varies with AO on is AO. ENGINE-NOTES 7ae.
    """
    next_id = base._ids()
    lines = base._header("SSAO brick wall at a graze", sky_rgb=(0.6, 0.6, 0.6))
    # Looking down the wall from beside it: the wall fills the left of the
    # frame in a wedge, its normal 60-70 degrees from the view direction.
    lines += base._camera(next_id, (0, 1.5, 2.0), rotation=(0, 0, 0), profile=profile)
    lines += [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        "      Tag: Wall",
        "    TransformComponent:",
        "      Position: [-1.9, 1.5, -9]",
        "      Rotation: [0, 0, 0]",
        "      Scale: [0.4, 3, 24]",
        "    MeshComponent:",
        f"      Mesh: {base.PRIMITIVE_BASE + base.CUBE}",
        f"      Material: {material}",
    ]
    return "\n".join(lines) + "\n"


def main():
    import pathlib
    root = pathlib.Path(__file__).resolve().parents[2]
    scenes = root / "SampleProject" / "assets" / "scenes"
    out = scenes / "ssao_box.rage"
    out.write_text(build(None))
    print(f"wrote {out}")
    out = scenes / "ssao_wall.rage"
    out.write_text(build_wall(None, wall_material_handle(root)))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
