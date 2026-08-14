#!/usr/bin/env python3
"""Scenes that move, because a still one cannot fail the way TAA fails.

Every anti-aliasing check in this repository holds the camera and the scene
still. That is the case TAA is *best* at -- thirty jittered samples of an
unchanging image converge on a supersampled one -- and it is the case nobody
complains about. ENGINE-NOTES 7r said so before any of TAA was built: "Its
only real failure mode is motion... That check does not exist yet and TAA is
not finished until it does."

Two scenes, because there are two different things a still frame cannot see.

**falling** -- a bright block dropping through a dark frame, camera fixed.
Screen motion is almost purely **vertical**, which is the axis in doubt: the
velocity attachment is a difference of clip coordinates, whose y runs the
same way on both backends, while the resolve's v does not. The sign is
negated on the backend that flips, by the same argument every other
fullscreen pass uses -- and that argument has never been tested, because a
static scene has zero velocity everywhere and any sign multiplied by zero
looks correct.

**panning** -- the camera yaws, everything moves **horizontally**, and the
sky comes into it. The sky reports no motion of its own (it is infinitely far
away, so it has none) but it does sweep across the screen when the camera
turns, and a filter told otherwise reprojects it onto itself.

Both are deterministic, which is the whole reason they are built this way
rather than driven from a clock. The block falls under the fixed-step
solver; the camera turns from `Rotator`, whose OnTick is a fixed step too.
With `--frame-time` pinned, frame 30 is frame 30 on any machine -- the same
property `--screenshot-frame` has always depended on.

Emissive and unlit, with a flat sky, no bloom and no shadows, for the reason
make_aa_scene.py gives: the measurement wants a bright thing on a dark
ground and nothing else varying across either.

    python tools/scripts/make_motion_scene.py --kind falling \
        --output SampleProject/assets/scenes/motion_fall.rage
"""

import argparse
import pathlib

PRIMITIVE_BASE = 0x7261676556000000
CUBE, SPHERE, PLANE, CYLINDER, QUAD = range(5)

# Far enough back that the block crosses a good part of the frame, near
# enough that its screen-space speed is tens of pixels a frame rather than
# ones -- the jitter is half a pixel, and the motion has to be unmistakably
# larger than that or the two are not separable.
CAMERA_Z = 4.0


def vec(values):
    return "[" + ", ".join(f"{v:g}" for v in values) + "]"


def _ids():
    counter = iter(range(1, 1 << 62))
    return lambda: (next(counter) * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF


def _header(name, sky_rgb=(0.02, 0.02, 0.025)):
    # A colon here is a second map value on the line and yaml-cpp refuses the
    # whole file for it -- which the runtime then reports and exits 0 from, so
    # the only symptom is a screenshot that never appears.
    assert ":" not in name, "a scene name with a colon in it is not loadable"

    return [
        f"Scene: {name}",
        "Version: 5",
        "Environment:",
        "  AmbientColor: [0, 0, 0]",
        "  AmbientIntensity: 0",
        "  Sky: 1",
        f"  SkyHorizon: {vec(sky_rgb)}",
        f"  SkyZenith: {vec(sky_rgb)}",
        f"  SkyGround: {vec(sky_rgb)}",
        "  SkyIntensity: 1",
        "  SkyRotation: 0",
        "  Exposure: 1",
        # Bloom would spread the block's edges across the very pixels a smear
        # would occupy, which is the signal being measured.
        "  BloomEnabled: false",
        "  ShadowsEnabled: false",
        "  AntiAliasing: 0",
        "Entities:",
    ]


def _camera(next_id, position, rotation=(0, 0, 0), script=None, speed=None):
    lines = [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        "      Tag: Scene Camera",
        "    TransformComponent:",
        f"      Position: {vec(position)}",
        f"      Rotation: {vec(rotation)}",
        "      Scale: [1, 1, 1]",
        "    CameraComponent:",
        "      ViewRank: 0",
        "      FixedAspectRatio: false",
        "      ProjectionType: Perspective",
        "      PerspectiveFOV: 60",
        "      PerspectiveNearClip: 0.05",
        "      PerspectiveFarClip: 400",
        "      OrthographicScale: 10",
        "      OrthographicNearClip: -1",
        "      OrthographicFarClip: 1",
    ]
    if script:
        lines += [
            "    NativeScriptComponent:",
            f"      Script: {script}",
        ]
        if speed is not None:
            lines += [
                "      Fields:",
                f"        Speed: {speed:g}",
            ]
    return lines


def _emissive_block(next_id, tag, position, scale, colour, dynamic=False):
    lines = [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        f"      Tag: {tag}",
        "    TransformComponent:",
        f"      Position: {vec(position)}",
        "      Rotation: [0, 0, 0]",
        f"      Scale: {vec(scale)}",
        "    MeshComponent:",
        f"      Mesh: {PRIMITIVE_BASE + CUBE}",
        "      Material:",
        "        BaseColor: [0, 0, 0, 1]",
        f"        Emissive: {vec(list(colour) + [1])}",
        "        Metallic: 0",
        "        Roughness: 1",
        "        Occlusion: 1",
    ]
    if dynamic:
        lines += [
            "    RigidBodyComponent:",
            "      Type: Dynamic",
            "      Mass: 1",
            "      Friction: 0.4",
            "      Restitution: 0",
            # No damping: the fall is then exactly 0.5*g*t^2, so where the
            # block is at frame N is arithmetic rather than a simulation
            # anybody has to trust.
            "      LinearDamping: 0",
            "      AngularDamping: 0",
            "      GravityFactor: 1",
            # Translation only. A tumbling block would rotate its own
            # silhouette between frames, which is a second kind of motion
            # mixed into the one being measured.
            "      FreezeRotation: true",
            "    ColliderComponent:",
            "      Shape: Box",
            "      HalfExtents: [0.5, 0.5, 0.5]",
            "      Radius: 0.5",
            "      Height: 1",
            "      Offset: [0, 0, 0]",
            "      IsTrigger: false",
        ]
    return lines


def build_falling():
    """A block falling through a dark frame, camera fixed.

    Motion is vertical in screen space, which is the axis whose sign has
    never been checked. Nothing is below it to land on, so it accelerates
    cleanly off the bottom of the frame and never bounces -- a bounce would
    put a discontinuity in the motion right where the measurement wants a
    smooth one.
    """
    next_id = _ids()
    lines = _header("Motion falling block")

    lines += _camera(next_id, (0, 0, CAMERA_Z))

    # Starts above the frame and is near the middle by frame 30 at 60 Hz:
    # 0.5 * 9.81 * 0.5^2 is about 1.2 m of fall. The check locates it rather
    # than trusting this arithmetic, but it wants the block on screen.
    # A *textured* falling patch, not one uniform block, and this is the whole
    # difference between a scene that can test the reprojection and one that
    # cannot.
    #
    # Measured on a single bright block against a dark sky: no ghost at all,
    # on either backend, with any reprojection. The neighbourhood clamp
    # rejects wrong-place history whenever the neighbourhood is uniform --
    # history from the wrong side of a flat bright block is outside the box
    # and gets clipped, and history from the wrong side of flat background is
    # too. The clamp masks precisely the mistake being looked for.
    #
    # A patch of small cells at different brightnesses has no uniform
    # neighbourhood anywhere, so history fetched a few pixels off is still
    # *locally plausible*, survives the clamp, and lands in the image. Which
    # is what makes the sign observable.
    cells = 5
    for row in range(cells):
        for col in range(cells):
            # Brightnesses that do not repeat along either axis, so a fetch
            # displaced in x is as wrong as one displaced in y.
            level = 0.18 + 0.62 * (((row * 7 + col * 3) % 11) / 10.0)
            lines += _emissive_block(
                next_id, f"Fall {row}-{col}",
                (1.4 + (col - 2) * 0.11, 1.25 + (row - 2) * 0.11, 0.0),
                (0.05, 0.05, 0.05), (level, level, level), dynamic=True)

    # A still block beside it, as the control *inside the same frame*: a
    # filter that smears the moving one and leaves this one alone is doing
    # its job, and one that blurs both is simply blurring.
    for row in range(cells):
        for col in range(cells):
            level = 0.18 + 0.62 * (((row * 7 + col * 3) % 11) / 10.0)
            lines += _emissive_block(
                next_id, f"Still {row}-{col}",
                (-1.4 + (col - 2) * 0.11, 0.0 + (row - 2) * 0.11, 0.0),
                (0.05, 0.05, 0.05), (level, level, level))

    return chr(10).join(lines) + chr(10)


def build_panning(speed=0.6):
    """The camera yaws, so everything sweeps horizontally -- including the sky.

    `Rotator` is the sample project's own script and turns about Y from
    OnTick, which is the fixed step, so the angle at frame N is exactly
    speed * N / fixed_hz.

    The blocks are spread across the turn so there is always something with
    real geometry in frame, and the sky is a gradient rather than flat here:
    a sky that is one colour cannot show a smear no matter how wrongly it is
    reprojected.
    """
    next_id = _ids()
    lines = _header("Motion panning camera", sky_rgb=(0.02, 0.02, 0.025))

    # A **cubemap** sky, and it has to be one.
    #
    # The obvious choice is the gradient, and it cannot work: the gradient is
    # a function of the direction's y alone, so it is rotationally symmetric
    # about the axis `Rotator` turns. Yawing the camera through a gradient sky
    # changes nothing on screen, so there is no sky motion to smear and no
    # measurement to make -- the first version of this scene measured 0.08 RMS
    # against 0.14 and neither number meant anything.
    #
    # The sample project's panorama has structure in every direction, so a
    # yaw moves real detail across the frame.
    for i, line in enumerate(lines):
        if line.startswith("  Sky:"):
            lines[i] = "  Sky: 2"
    lines.insert(lines.index("Entities:"),
                 "  SkyTexture: 17858281879166177050")

    lines += _camera(next_id, (0, 0, 0), script="Rotator", speed=speed)

    # Ringed around the camera so the turn always has something in view.
    import math
    for i in range(8):
        angle = 2.0 * math.pi * i / 8.0
        lines += _emissive_block(
            next_id, f"Post {i}",
            (5.0 * math.sin(angle), 0.0, -5.0 * math.cos(angle)),
            (0.4, 1.6, 0.4), (0.8, 0.8, 0.8))

    return chr(10).join(lines) + chr(10)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kind", choices=("falling", "panning"), default="falling")
    parser.add_argument("--speed", type=float, default=0.6,
                        help="radians per second of camera yaw, for --kind panning")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    text = build_falling() if args.kind == "falling" else build_panning(args.speed)
    path = pathlib.Path(args.output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    print(f"wrote {path} ({args.kind})")


if __name__ == "__main__":
    main()
