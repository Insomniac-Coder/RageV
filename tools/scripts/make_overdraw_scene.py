#!/usr/bin/env python3
"""Write a scene that is nothing but overdraw, to find the ceiling on 7.8.

Front-to-back sorting only ever buys what early-z rejection can save, and
early-z can only save shading a pixel that a nearer pixel will overwrite. So
the question "is depth sorting worth building" is really "how much does this
renderer spend shading pixels that get covered", and the honest way to bound
that is to build the worst scene anyone could hand it.

Every mesh here is a large cube centred on the camera's axis, spaced along
it, each one filling the view. Depth complexity is therefore the mesh count:
with `--count 200` the renderer is asked to shade every pixel two hundred
times if nothing rejects it, and once if the ordering is perfect.

A real scene never looks like this. That is the point -- if sorting cannot be
measured *here*, it cannot be measured anywhere, and 7.8 is answered.

    python tools/scripts/make_overdraw_scene.py --count 200 \
        --output SampleProject/assets/scenes/overdraw.rage

    RageVRuntime.exe --scene=scenes/overdraw.rage --vsync=off --benchmark=400
"""

import argparse

PRIMITIVE_BASE = 0x7261676556000000
CUBE, SPHERE, PLANE, CYLINDER, QUAD = range(5)


def vec(values):
    return "[" + ", ".join(f"{v:g}" for v in values) + "]"


def build(count, spacing):
    ids = iter(range(1, 1 << 62))

    def next_id():
        return (next(ids) * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF

    out = []
    out.append("Scene: Overdraw\n")
    out.append("Version: 5\n")
    out.append("Environment:\n")
    out.append("  AmbientColor: [0.42, 0.47, 0.58]\n")
    out.append("  AmbientIntensity: 0.12\n")
    out.append("  Sky: 2\n")
    out.append("  SkyHorizon: [0.52, 0.6, 0.72]\n")
    out.append("  SkyZenith: [0.18, 0.31, 0.62]\n")
    out.append("  SkyGround: [0.16, 0.15, 0.14]\n")
    out.append("  SkyIntensity: 1\n")
    out.append("  SkyRotation: 0\n")
    out.append("Entities:\n")

    out.append(f"  - EntityID: {next_id()}\n")
    out.append("    TagComponent:\n      Tag: Scene Camera\n")
    out.append(
        "    TransformComponent:\n"
        "      Position: [0, 0, 4]\n"
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
        "      PerspectiveFarClip: 4000\n"
        "      OrthographicScale: 10\n"
        "      OrthographicNearClip: -1\n"
        "      OrthographicFarClip: 1\n"
    )

    # One directional light, so shading costs something. A scene lit by
    # ambient alone would have a fragment shader cheap enough to hide the
    # very effect being measured.
    out.append(f"  - EntityID: {next_id()}\n")
    out.append("    TagComponent:\n      Tag: Sun\n")
    out.append(
        "    TransformComponent:\n"
        "      Position: [0, 10, 0]\n"
        "      Rotation: [-0.9, -0.6, 0]\n"
        "      Scale: [1, 1, 1]\n"
    )
    out.append(
        "    LightComponent:\n"
        "      Type: 0\n"
        "      Color: [1, 0.97, 0.92]\n"
        "      Intensity: 3\n"
        "      Range: 30\n"
        "      InnerCone: 20\n"
        "      OuterCone: 30\n"
        "      CastsShadows: false\n"
    )

    # Each slab is scaled so it still covers the view at its distance: the
    # frustum widens with depth, so a constant size would stop filling the
    # screen and the depth complexity would fall off with range.
    #
    # **Far to near, and the order is the fixture.** The unsorted case draws
    # in submission order, and submission order is this file's entity order
    # (the ECS iterates its sparse sets in insertion order since 10.2 --
    # under EnTT it was arbitrary, which hid this). Emitted near-to-far, the
    # "unsorted" case is already perfect and the check's early-z floor reads
    # 1.0x forever: measured 2026-08-24, 0.62 ms "unsorted" against 24.2 ms
    # once the order was reversed. Worst case must be a property the fixture
    # guarantees, not one an iteration order happens to provide.
    for i in reversed(range(count)):
        z = -spacing * (i + 1)
        distance = 4.0 - z
        # tan(30 degrees) either side of a 60 degree vertical field, widened
        # for aspect and then some.
        half = distance * 0.6 * 1.9
        out.append(f"  - EntityID: {next_id()}\n")
        out.append(f"    TagComponent:\n      Tag: Slab {i}\n")
        out.append(
            "    TransformComponent:\n"
            f"      Position: {vec([0, 0, z])}\n"
            "      Rotation: [0, 0, 0]\n"
            f"      Scale: {vec([half * 2, half * 2, 0.05])}\n"
        )
        # Cubes rather than quads: a cube is unambiguously solid from either
        # side, so nothing here depends on winding or on backface culling.
        out.append(
            "    MeshComponent:\n"
            "      Static: true\n"
            f"      Mesh: {PRIMITIVE_BASE + CUBE}\n"
            "      Material:\n"
            f"        BaseColor: {vec([0.6, 0.35 + 0.002 * i, 0.3, 1])}\n"
            "        Emissive: [0, 0, 0, 1]\n"
            "        Metallic: 0.1\n"
            "        Roughness: 0.6\n"
            "        Occlusion: 1\n"
        )

    return "".join(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=200)
    parser.add_argument("--spacing", type=float, default=2.0)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    import pathlib

    path = pathlib.Path(args.output)
    path.write_text(build(args.count, args.spacing))
    print(f"{path}: {args.count} full-screen slabs, depth complexity {args.count}")


if __name__ == "__main__":
    main()
