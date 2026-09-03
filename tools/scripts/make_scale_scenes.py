"""Stress scenes at a range of object counts, for measuring where the renderer's
wall actually is.

**One scene is a number and a set of them is an answer.** Roadmap 8.3 defers
GPU-driven rendering on the grounds that "the renderer draws a thousand in
1.9 ms and is nowhere near the wall" -- which is a claim about a curve, made
from one point on it. These scenes are the rest of the curve.

Everything except the object count is held fixed: the same four meshes, the same
camera, the same sun with the same cascades, the same volume of world. So a
difference between two of them is the count and nothing else.
"""
import argparse
import io
import math
import pathlib
import random

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENES = ROOT / "SampleProject" / "assets" / "scenes"

# The four meshes stress.rage already uses, so nothing new has to be imported
# and the comparison against it is direct.
CUBE = 8241982477996916736
SPHERE = 8241982477996916737
PLANE = 8241982477996916738
CONE = 8241982477996916739

SHAPES = (CUBE, SPHERE, CONE)


def entity(eid, tag, body):
    return f"""  - EntityID: {eid}
    TagComponent:
      Tag: {tag}
{body}"""


def transform(position, rotation=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0)):
    return f"""    TransformComponent:
      Position: [{position[0]:.3f}, {position[1]:.3f}, {position[2]:.3f}]
      Rotation: [{rotation[0]:.4f}, {rotation[1]:.4f}, {rotation[2]:.4f}]
      Scale: [{scale[0]:.3f}, {scale[1]:.3f}, {scale[2]:.3f}]"""


def mesh(handle, colour, roughness=0.6):
    return f"""    MeshComponent:
      Static: true
      Mesh: {handle}
      Material:
        BaseColor: [{colour[0]:.3f}, {colour[1]:.3f}, {colour[2]:.3f}, 1]
        Emissive: [0, 0, 0, 1]
        Metallic: 0
        Roughness: {roughness}
        Occlusion: 1"""


def build(count, side):
    """`count` objects spread over a `side` x `side` field.

    The field grows with the count so the *density* stays put. Packing ten
    times as many objects into the same volume would change how many land in
    the frustum and in each cascade, and then a measurement is comparing two
    things at once.
    """
    rng = random.Random(20260821)
    out = [
        "Scene: Scale",
        "Version: 6",
        "Environment:",
        "  AmbientColor: [0.42, 0.47, 0.58]",
        "  AmbientIntensity: 0.12",
        "  Sky: 2",
        "  SkyHorizon: [0.52, 0.6, 0.72]",
        "  SkyZenith: [0.18, 0.31, 0.62]",
        "  SkyGround: [0.16, 0.15, 0.14]",
        "  SkyIntensity: 1",
        "  SkyRotation: 0",
        "Entities:",
    ]

    # The camera sits back far enough to see a good fraction of the field --
    # a fixed camera over a growing field would put a shrinking share of the
    # objects on screen, and the curve would be measuring the frustum.
    eye = (0.0, side * 0.32, side * 0.55)
    pitch = -math.atan2(eye[1] - 2.0, eye[2])
    out.append(entity(11400714819323198485, "Scene Camera",
                      transform(eye, (pitch, 0.0, 0.0)) + """
    CameraComponent:
      ViewRank: 0
      FixedAspectRatio: false
      ProjectionType: Perspective
      PerspectiveFOV: 60
      PerspectiveNearClip: 0.3
      PerspectiveFarClip: 2000
      OrthographicScale: 10
      OrthographicNearClip: -1
      OrthographicFarClip: 1
      PostProfile: 0"""))

    out.append(entity(15755400384260043839, "Sun",
                      transform((0, 0, 0), (-0.959931, -0.523599, 0.0)) + """
    LightComponent:
      Type: Directional
      Color: [0.55, 0.58, 0.65]
      Intensity: 1.6
      Range: 10
      InnerCone: 20
      OuterCone: 30
      CastShadows: true"""))

    out.append(entity(4354685564936845354, "Ground",
                      transform((0, -0.5, 0), scale=(side * 1.2, 1, side * 1.2)) +
                      "\n" + mesh(PLANE, (0.14, 0.14, 0.16), 0.85)))

    for i in range(count):
        x = rng.uniform(-side * 0.5, side * 0.5)
        z = rng.uniform(-side * 0.5, side * 0.5)
        size = rng.uniform(0.5, 1.4)
        shape = SHAPES[i % len(SHAPES)]
        colour = (rng.uniform(0.2, 0.9), rng.uniform(0.2, 0.9), rng.uniform(0.2, 0.9))
        out.append(entity(1000000 + i, f"Object {i}",
                          transform((x, size * 0.5, z),
                                    (0.0, rng.uniform(0.0, 6.283), 0.0),
                                    (size, size, size)) +
                          "\n" + mesh(shape, colour)))

    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--counts", default="1000,5000,20000,60000,120000")
    args = parser.parse_args()

    for text in args.counts.split(","):
        count = int(text)
        # Density held at roughly one object per 6 square metres.
        side = max(60.0, math.sqrt(count * 6.0))
        path = SCENES / f"scale_{count}.rage"
        io.open(path, "w", encoding="utf-8", newline="").write(build(count, side))
        print(f"  {path.name:22s} {count:7d} objects over {side:.0f} x {side:.0f} m")


if __name__ == "__main__":
    main()
