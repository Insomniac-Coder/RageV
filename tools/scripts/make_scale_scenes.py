"""Stress scenes at a range of object counts, for measuring where the renderer's
wall actually is.

**One scene is a number and a set of them is an answer.** Roadmap 8.3 defers
GPU-driven rendering on the grounds that "the renderer draws a thousand in
1.9 ms and is nowhere near the wall" -- which is a claim about a curve, made
from one point on it. These scenes are the rest of the curve.

Everything except the object count is held fixed: the same four meshes, the same
camera, the same sun with the same cascades, the same volume of world. So a
difference between two of them is the count and nothing else.

**And a moving twin of each** (RT2-2c, `scale_<n>_moving.rage`): the same scene
with one object in ten spinning (not static, a Rotator), so a still/moving pair
measures what motion costs -- the ray-tracing structure's instances and the
transforms that change -- with nothing else different.
"""
import argparse
import io
import math
import pathlib
import random
import re

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


def mesh(handle, colour, roughness=0.6, static=True):
    return f"""    MeshComponent:
      Static: {'true' if static else 'false'}
      Mesh: {handle}
      Material:
        BaseColor: [{colour[0]:.3f}, {colour[1]:.3f}, {colour[2]:.3f}, 1]
        Emissive: [0, 0, 0, 1]
        Metallic: 0
        Roughness: {roughness}
        Occlusion: 1"""


# One object in this many spins in the moving twin.
MOVING_EVERY = 10

SPIN = """
    NativeScriptComponent:
      Script: Rotator
      Fields:
        Speed: 0.6"""


def build(count, side, moving=False):
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
        spins = moving and i % MOVING_EVERY == 0
        out.append(entity(1000000 + i, f"Object {i}",
                          transform((x, size * 0.5, z),
                                    (0.0, rng.uniform(0.0, 6.283), 0.0),
                                    (size, size, size)) +
                          "\n" + mesh(shape, colour, static=not spins) + (SPIN if spins else "")))

    return "\n".join(out) + "\n"


# --- scenes that look like content (RT2-2c) ---------------------------------
#
# **The cubes measure the count and nothing else; these measure what a real
# scene adds to it.** Imported hierarchies (the showroom's car: 337 nodes, 151
# meshes, each with its own textured material), hundreds of distinct meshes
# (the garage's 141 and the camp's props beside the car's), emissive fittings
# (the garage's light panels and the camp fire's flames) and, in the moving
# twin, a share of the cars turning. Copied out of the shipped scenes by tag
# rather than re-authored, so they are exactly what those scenes draw: a block
# is one garage, two cars and a camp, repeated on a grid until the count is
# reached. Everything but the meshes is left behind -- the fire's light, sound,
# script and particles -- because lights at scale are RT2-2d's scene.

# The garage is about 48 x 55 m (its irradiance volume's half-extents are 24
# and 27.5 about the root's z = 14), so blocks are spaced wider than that and
# the cars and the camp stand outside it, in the gaps.
BLOCK_PITCH_X = 72.0
BLOCK_PITCH_Z = 80.0
# One car in this many turns in the moving twin.
CAR_TURNS_EVERY = 4


def load_entities(path):
    """The entity blocks of a scene file, in file order, by id."""
    text = io.open(path, encoding="utf-8").read()
    blocks = re.split(r"(?=^  - EntityID:)", text, flags=re.M)[1:]
    return {int(re.search(r"EntityID: (\d+)", b).group(1)): b.rstrip() for b in blocks}


def subtree(entities, tag):
    """The root tagged `tag` and every descendant, parents before children."""
    root = next(i for i, b in entities.items()
                if re.search(r"^      Tag: " + re.escape(tag) + r"\s*$", b, re.M)
                and "RelationshipComponent" not in b)
    children = {}
    for i, b in entities.items():
        parent = re.search(r"^      Parent: (\d+)", b, re.M)
        if parent:
            children.setdefault(int(parent.group(1)), []).append(i)
    order, stack = [], [root]
    while stack:
        node = stack.pop()
        order.append(node)
        stack.extend(reversed(children.get(node, [])))
    return order


def keep_meshes(block):
    """A copied block with only tag, transform, relationship and mesh."""
    kept = []
    for part in re.split(r"(?=^    \w+Component:)", block, flags=re.M):
        name = re.match(r"\s*(\w+Component):", part)
        if not name or name.group(1) in ("TagComponent", "TransformComponent",
                                          "RelationshipComponent", "MeshComponent"):
            kept.append(part.rstrip())
    return "\n".join(kept)


class Stamper:
    """Copies subtrees into a new scene under fresh, distinct entity ids."""

    def __init__(self):
        self.next_id = 7_000_000_000_000_000_000
        self.meshes = 0

    def stamp(self, entities, order, position, yaw, moving):
        """The subtree under a new placement node at `position`, turned by `yaw`.

        The copied root keeps its own rotation and scale -- an imported model's
        root is often what stands a Z-up file upright -- and only loses its
        position, which the placement node now gives it. A moving copy spins
        its placement node, so the whole hierarchy turns about its own spot.
        """
        place = self.next_id
        ids = {old: self.next_id + 1 + n for n, old in enumerate(order)}
        self.next_id += len(order) + 1
        out = [entity(place, "Placement", transform(position, (0.0, yaw, 0.0)) + (SPIN if moving else ""))]
        for n, old in enumerate(order):
            block = keep_meshes(entities[old])
            block = block.replace(f"EntityID: {old}", f"EntityID: {ids[old]}", 1)
            parent = re.search(r"Parent: (\d+)", block)
            if parent:
                block = block.replace(parent.group(0), f"Parent: {ids[int(parent.group(1))]}")
            block = re.sub(r"Static: (true|false)", f"Static: {'false' if moving else 'true'}", block)
            if n == 0:
                block = re.sub(r"Position: \[[^\]]*\]", "Position: [0, 0, 0]", block, count=1)
                block = block.replace(f"EntityID: {ids[old]}\n",
                                      f"EntityID: {ids[old]}\n    RelationshipComponent:\n      Parent: {place}\n", 1)
            self.meshes += block.count("MeshComponent:")
            out.append(block)
        return out


def build_content(count, moving=False):
    showroom = load_entities(SCENES / "showroom.rage")
    camp = load_entities(SCENES / "camp.rage")
    car = subtree(showroom, "porsche_992_gt3_r")
    garage = subtree(showroom, "underground_garage_pbr")
    camp_parts = [(subtree(camp, tag), offset) for tag, offset in
                  (("Fire", (0.0, 0.0, 0.0)), ("Tent", (3.05, 0.0, -2.5)), ("Van", (-5.9, 0.0, -3.3)))]
    per_block = (sum("MeshComponent" in showroom[i] for i in garage)
                 + 2 * sum("MeshComponent" in showroom[i] for i in car)
                 + sum("MeshComponent" in camp[i] for parts, _ in camp_parts for i in parts))
    blocks = max(1, round(count / per_block))
    across = math.ceil(math.sqrt(blocks))
    side = across * max(BLOCK_PITCH_X, BLOCK_PITCH_Z)

    out = [
        "Scene: Scale content",
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
      PerspectiveFarClip: 4000
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
                      transform((0, -0.05, 0), scale=(side * 1.2, 1, side * 1.2)) +
                      "\n" + mesh(PLANE, (0.14, 0.14, 0.16), 0.85)))

    stamper = Stamper()
    cars = 0
    for b in range(blocks):
        x = (b % across - (across - 1) * 0.5) * BLOCK_PITCH_X
        z = (b // across - (across - 1) * 0.5) * BLOCK_PITCH_Z
        out += stamper.stamp(showroom, garage, (x, 0.0, z + 14.0), 0.0, False)
        for side_x, yaw in ((-5.0, 0.0), (5.0, math.pi)):
            turns = moving and cars % CAR_TURNS_EVERY == 0
            out += stamper.stamp(showroom, car, (x + side_x, 0.0, z - 21.0), yaw, turns)
            cars += 1
        for parts, (ox, oy, oz) in camp_parts:
            out += stamper.stamp(camp, parts, (x + 33.0 + ox, oy, z - 4.0 + oz), 0.0, False)

    text = "\n".join(out) + "\n"
    return text, stamper.meshes, blocks, cars


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--counts", default="1000,5000,20000,60000,120000")
    parser.add_argument("--content", default="20000,60000,120000",
                        help="mesh counts of the content-like scenes (scale_content_<n>); empty for none")
    args = parser.parse_args()

    for text in [t for t in args.content.split(",") if t]:
        count = int(text)
        for moving in (False, True):
            path = SCENES / (f"scale_content_{count}_moving.rage" if moving else f"scale_content_{count}.rage")
            scene, meshes, blocks, cars = build_content(count, moving)
            io.open(path, "w", encoding="utf-8", newline="").write(scene)
            print(f"  {path.name:36s} {meshes:7d} meshes in {blocks} blocks, {cars} cars"
                  + (f", {(cars + CAR_TURNS_EVERY - 1) // CAR_TURNS_EVERY} turning" if moving else ""))

    for text in [t for t in args.counts.split(",") if t]:
        count = int(text)
        # Density held at roughly one object per 6 square metres.
        side = max(60.0, math.sqrt(count * 6.0))
        for moving in (False, True):
            path = SCENES / (f"scale_{count}_moving.rage" if moving else f"scale_{count}.rage")
            io.open(path, "w", encoding="utf-8", newline="").write(build(count, side, moving))
            print(f"  {path.name:28s} {count:7d} objects over {side:.0f} x {side:.0f} m"
                  + (f", {(count + MOVING_EVERY - 1) // MOVING_EVERY} spinning" if moving else ""))


if __name__ == "__main__":
    main()
