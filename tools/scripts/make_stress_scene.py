#!/usr/bin/env python3
"""Write a scene big enough to measure.

The sample scene is twelve objects, which is not enough to tell a renderer's
cost from noise -- the whole of this engine's frame-time history is 4.1 ms on a
240 Hz panel, which was the panel.  This emits a scene with a controllable
number of meshes, drawn from a handful of distinct primitives so that draw
sorting and instancing have something to bite on, spread over an area larger
than the camera sees so that frustum culling does too.

    python tools/scripts/make_stress_scene.py --count 1000 \
        --output SampleProject/assets/scenes/stress.rage

Then, from the runtime's own directory:

    RageVRuntime.exe --scene=scenes/stress.rage --vsync=off --benchmark=600

Deliberately no physics bodies and no scripts: this measures rendering, and a
thousand Jolt bodies would measure something else at the same time.
"""

import argparse
import math
import random

# AssetManager::GetPrimitiveHandle -- BuiltinAssets::kPrimitiveBase + type.
PRIMITIVE_BASE = 0x7261676556000000
CUBE, SPHERE, PLANE, CYLINDER, QUAD = range(5)

# Every mesh is one of these, so identical draws exist to be batched. Four is
# enough for sorting to matter and few enough that instancing would collapse a
# thousand draws into four.
SHAPES = [CUBE, SPHERE, CYLINDER, CUBE]


def vec(values):
    return "[" + ", ".join(f"{v:g}" for v in values) + "]"


def transform(position, rotation, scale, indent):
    pad = " " * indent
    return (
        f"{pad}TransformComponent:\n"
        f"{pad}  Position: {vec(position)}\n"
        f"{pad}  Rotation: {vec(rotation)}\n"
        f"{pad}  Scale: {vec(scale)}\n"
    )


def mesh(shape, base_color, metallic, roughness, indent):
    pad = " " * indent
    return (
        f"{pad}MeshComponent:\n"
        f"{pad}  Static: true\n"
        f"{pad}  Mesh: {PRIMITIVE_BASE + shape}\n"
        f"{pad}  Material:\n"
        f"{pad}    BaseColor: {vec(base_color)}\n"
        f"{pad}    Emissive: [0, 0, 0, 1]\n"
        f"{pad}    Metallic: {metallic:g}\n"
        f"{pad}    Roughness: {roughness:g}\n"
        f"{pad}    Occlusion: 1\n"
    )


def build(count, extent, seed, lights, probe, light_reach):
    rng = random.Random(seed)
    ids = iter(range(1, 1 << 62))

    def next_id():
        # Deterministic but spread across the 64-bit space, because a UUID
        # collision with an imported asset would be a very confusing bug and
        # sequential small integers invite one.
        return (next(ids) * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF

    out = []
    out.append("Scene: Stress\n")
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

    # The camera sits low and looks along -Z, so roughly half the field is
    # behind it: culling has something to remove and the count is comparable
    # between runs.
    out.append(f"  - EntityID: {next_id()}\n")
    out.append("    TagComponent:\n      Tag: Scene Camera\n")
    out.append(transform([0, 6, extent * 0.55], [-0.22, 0, 0], [1, 1, 1], 4))
    out.append(
        "    CameraComponent:\n"
        "      ViewRank: 0\n"
        "      FixedAspectRatio: false\n"
        "      ProjectionType: Perspective\n"
        "      PerspectiveFOV: 60\n"
        "      PerspectiveNearClip: 0.05\n"
        "      PerspectiveFarClip: 2000\n"
        "      OrthographicScale: 10\n"
        "      OrthographicNearClip: -1\n"
        "      OrthographicFarClip: 1\n"
    )

    out.append(f"  - EntityID: {next_id()}\n")
    out.append("    TagComponent:\n      Tag: Ground\n")
    out.append(transform([0, -1, 0], [0, 0, 0], [extent * 2, 1, extent * 2], 4))
    out.append(mesh(PLANE, [0.14, 0.14, 0.16, 1], 0, 0.85, 4))

    out.append(f"  - EntityID: {next_id()}\n")
    out.append("    TagComponent:\n      Tag: Sun\n")
    out.append(transform([0, 0, 0], [-0.9599311, -0.5235988, 0], [1, 1, 1], 4))
    out.append(
        "    LightComponent:\n"
        "      Type: Directional\n"
        "      Color: [0.55, 0.58, 0.65]\n"
        "      Intensity: 1.6\n"
        "      Range: 10\n"
        "      InnerCone: 20\n"
        "      OuterCone: 30\n"
        "      CastShadows: true\n"
    )

    # No cap. There used to be one -- eight including the sun, because the
    # scene's uniform block declared arrays of eight -- so asking for more
    # measured a limit rather than a cost. Lights live in a storage buffer now
    # and a scene may have as many as it likes; only shadow *casters* are still
    # budgeted, at four spot maps and four point cubes.
    # Split a fixed budget between them, so a scene with fifty lights is lit
    # rather than blown out. Comparing a clustered frame against an unclustered
    # one needs an image with detail left in it.
    light_intensity = 90.0 * min(1.0, 8.0 / max(lights, 1))

    # How far each light reaches. The default is the whole field, which is the
    # honest default for a handful of lights and pathological for many: a light
    # that reaches everywhere lands in every cluster, and clustering then costs
    # an indirection and saves nothing. Real scenes are mostly small lights.
    light_range = extent if light_reach <= 0.0 else light_reach

    for i in range(lights):
        angle = 2.0 * math.pi * i / max(lights, 1)
        radius = extent * (0.30 if (i % 2) else 0.55)
        out.append(f"  - EntityID: {next_id()}\n")
        out.append(f"    TagComponent:\n      Tag: Point Light {i}\n")
        out.append(transform(
            [math.cos(angle) * radius, 5, math.sin(angle) * radius],
            [0, 0, 0], [1, 1, 1], 4))
        out.append(
            "    LightComponent:\n"
            "      Type: Point\n"
            f"      Color: [{0.6 + 0.4 * rng.random():.3g}, "
            f"{0.6 + 0.4 * rng.random():.3g}, {0.6 + 0.4 * rng.random():.3g}]\n"
            f"      Intensity: {light_intensity:g}\n"
            f"      Range: {light_range:g}\n"
            "      InnerCone: 20\n"
            "      OuterCone: 30\n"
            # Only the first two cast: four spot and four point maps exist, and
            # a shadow budget warning in the log is noise in a benchmark.
            f"      CastShadows: {'true' if i < 2 else 'false'}\n"
        )

    if probe:
        out.append(f"  - EntityID: {next_id()}\n")
        out.append("    TagComponent:\n      Tag: Reflection Probe\n")
        out.append(transform([0, 3, 0], [0, 0, 0], [1, 1, 1], 4))
        out.append(
            "    ReflectionProbeComponent:\n"
            "      Update: Realtime\n"
            "      Resolution: 128\n"
            f"      Influence: {extent * 4:g}\n"
            "      NearClip: 0.05\n"
            "      FarClip: 200\n"
            "      FacesPerFrame: 1\n"
        )

    # A square grid, jittered. Regular enough that the count is predictable and
    # irregular enough that the depth order is not already front-to-back --
    # which would be the one arrangement that makes sorting look free.
    side = int(math.ceil(math.sqrt(count)))
    placed = 0
    for row in range(side):
        for column in range(side):
            if placed >= count:
                break

            x = (column / max(side - 1, 1) - 0.5) * 2.0 * extent
            z = (row / max(side - 1, 1) - 0.5) * 2.0 * extent
            x += rng.uniform(-0.4, 0.4)
            z += rng.uniform(-0.4, 0.4)

            shape = SHAPES[placed % len(SHAPES)]
            scale = rng.uniform(0.5, 1.4)
            metallic = 1.0 if placed % 5 == 0 else 0.0
            roughness = rng.uniform(0.15, 0.9)

            out.append(f"  - EntityID: {next_id()}\n")
            out.append(f"    TagComponent:\n      Tag: Prop {placed}\n")
            out.append(transform([x, scale * 0.5, z],
                                 [0, rng.uniform(0, 6.28), 0],
                                 [scale, scale, scale], 4))
            out.append(mesh(shape,
                            [rng.uniform(0.1, 0.9), rng.uniform(0.1, 0.9),
                             rng.uniform(0.1, 0.9), 1],
                            metallic, roughness, 4))
            placed += 1

    return "".join(out), placed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, default=1000, help="mesh entities")
    parser.add_argument("--extent", type=float, default=60.0,
                        help="half the width of the field they are spread over")
    parser.add_argument("--lights", type=int, default=7, help="point lights")
    parser.add_argument("--light-reach", type=float, default=0.0,
                        help="how far each light reaches; 0 means the whole field, "
                             "which is what makes clustering useless")
    parser.add_argument("--no-probe", action="store_true", help="omit the reflection probe")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    text, placed = build(args.count, args.extent, args.seed, args.lights,
                         not args.no_probe, args.light_reach)

    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)

    print(f"{args.output}: {placed} meshes over {args.extent * 2:g} units")


if __name__ == "__main__":
    main()
