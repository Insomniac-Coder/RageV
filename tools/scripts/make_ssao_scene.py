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


def main():
    import pathlib
    root = pathlib.Path(__file__).resolve().parents[2]
    out = root / "SampleProject" / "assets" / "scenes" / "ssao_box.rage"
    out.write_text(build(None))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
