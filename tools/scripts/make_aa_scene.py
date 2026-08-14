#!/usr/bin/env python3
"""Write a scene that is one straight edge, so aliasing can be measured.

"Does it look smoother" is not a measurement, and it is how an
anti-aliasing filter that quietly blurs the whole frame gets shipped. This
builds the scene that makes the question numeric.

The scene is a single large emissive slab, rotated about the view axis, in
front of a flat sky. Every column of the image is therefore sky above and
slab below with exactly one transition between, and the sum of a column is
an affine function of where that transition sits. **For a straight edge, that
sum is linear in x.** How far it departs from a line *is* the staircase, in
pixels.

Emissive, unlit, with a flat sky and no bloom, because the measurement wants
two constant levels and nothing else: a gradient across either region would
be indistinguishable from an edge in the wrong place.

The control comes free. With no anti-aliasing at all, every pixel is wholly
one side or the other, so the column sums are a staircase -- and the
deviation of a staircase from the line it approximates is the uniform
quantisation error, **1/sqrt(12) = 0.289 px, whatever the angle**. A
measurement of the unfiltered image that does not land near 0.289 is
measuring something other than what it thinks.

    python tools/scripts/make_aa_scene.py --angle 8 \
        --output SampleProject/assets/scenes/aa_edge8.rage

    RageVRuntime.exe --scene=scenes/aa_edge8.rage --aa=smaa --screenshot=out.png
"""

import argparse
import math
import pathlib

PRIMITIVE_BASE = 0x7261676556000000
CUBE, SPHERE, PLANE, CYLINDER, QUAD = range(5)

# Where the camera sits, and the slab's half-length. The slab has to run off
# every side of the frame at any rotation, or its own ends become edges too.
CAMERA_Z = 4.0
HALF_LENGTH = 40.0
HALF_DEPTH = 20.0


def vec(values):
    return "[" + ", ".join(f"{v:g}" for v in values) + "]"


def build(angle_degrees):
    ids = iter(range(1, 1 << 62))

    def next_id():
        return (next(ids) * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF

    theta = math.radians(angle_degrees)

    # The slab's top edge starts at local y = +HALF_DEPTH. Rotating by theta
    # and translating by the negative of where that lands puts the edge line
    # exactly through the origin -- which projects to the centre of the frame,
    # so the edge crosses the middle at every angle rather than sliding off.
    position = (HALF_DEPTH * math.sin(theta), -HALF_DEPTH * math.cos(theta), 0.0)

    out = []
    out.append(f"Scene: AA edge {angle_degrees:g} degrees\n")
    out.append("Version: 5\n")
    out.append("Environment:\n")
    # Ambient off and the three sky colours equal: a flat background, and
    # nothing reaching the slab that could vary across it.
    out.append("  AmbientColor: [0, 0, 0]\n")
    out.append("  AmbientIntensity: 0\n")
    out.append("  Sky: 1\n")
    out.append("  SkyHorizon: [0.05, 0.05, 0.06]\n")
    out.append("  SkyZenith: [0.05, 0.05, 0.06]\n")
    out.append("  SkyGround: [0.05, 0.05, 0.06]\n")
    out.append("  SkyIntensity: 1\n")
    out.append("  SkyRotation: 0\n")
    out.append("  Exposure: 1\n")
    # Bloom would spread the bright side across the edge, which is precisely
    # the signal being measured.
    out.append("  BloomEnabled: false\n")
    out.append("  ShadowsEnabled: false\n")
    out.append("  AntiAliasing: 0\n")
    out.append("Entities:\n")

    out.append(f"  - EntityID: {next_id()}\n")
    out.append("    TagComponent:\n      Tag: Scene Camera\n")
    out.append(
        "    TransformComponent:\n"
        f"      Position: {vec([0, 0, CAMERA_Z])}\n"
        "      Rotation: [0, 0, 0]\n"
        "      Scale: [1, 1, 1]\n"
    )
    out.append(
        "    CameraComponent:\n"
        "      ViewRank: 0\n"
        "      FixedAspectRatio: false\n"
        "      ProjectionType: Perspective\n"
        "      PerspectiveFOV: 60\n"
        "      PerspectiveNearClip: 0.05\n"
        "      PerspectiveFarClip: 400\n"
        "      OrthographicScale: 10\n"
        "      OrthographicNearClip: -1\n"
        "      OrthographicFarClip: 1\n"
    )

    # No light: the slab is emissive and the sky is flat, so shading would only
    # add a gradient for the measurement to mistake for an edge.
    out.append(f"  - EntityID: {next_id()}\n")
    out.append("    TagComponent:\n      Tag: Edge\n")
    out.append(
        "    TransformComponent:\n"
        f"      Position: {vec(position)}\n"
        f"      Rotation: {vec([0, 0, theta])}\n"
        f"      Scale: {vec([HALF_LENGTH * 2, HALF_DEPTH * 2, 0.01])}\n"
    )
    # Black base colour so nothing is reflected or lit: the emissive term is
    # the whole of it, and it is the same from every angle.
    out.append(
        "    MeshComponent:\n"
        f"      Mesh: {PRIMITIVE_BASE + CUBE}\n"
        "      Material:\n"
        "        BaseColor: [0, 0, 0, 1]\n"
        "        Emissive: [0.6, 0.6, 0.6, 1]\n"
        "        Metallic: 0\n"
        "        Roughness: 1\n"
        "        Occlusion: 1\n"
    )

    return "".join(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--angle", type=float, default=8.0,
                        help="degrees the edge is rotated from horizontal")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    path = pathlib.Path(args.output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(build(args.angle))
    print(f"{path}: one edge at {args.angle:g} degrees")


if __name__ == "__main__":
    main()
