#!/usr/bin/env python3
"""The fixture for check_graph.py: one cube, and a graph that moves it.

Two scenes that differ in exactly one thing -- whether the cube carries the
generated script -- so the only explanation for a pixel changing between them
is that the script ran. Nothing else in frame moves: no particles, no
animation, a fixed camera, a still light.

The graph is deliberately the smallest one that *does* something: On Create,
then Set Field. The v1 node set has no variables, so a graph cannot integrate
and "move it a little each frame" is not expressible; a one-shot set is, and
it is enough to answer the only question this check asks -- **did the C# a
graph generated actually run inside the engine?**
"""

import io
import pathlib

import make_motion_scene as base

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENES = ROOT / "SampleProject" / "assets" / "scenes"
GRAPHS = ROOT / "SampleProject" / "assets" / "graphs"

CAMERA_POSITION = (0.0, 1.6, 6.0)
CAMERA_ROTATION = (0.0, 0.0, 0.0)

# Where the script puts it. Far enough that the two frames cannot be confused
# for noise, and still inside the camera's view so the difference is *visible*
# rather than merely off screen -- a cube that left the frame would pass a
# pixel comparison for the wrong reason.
MOVED_TO = (2.2, 1.0, 0.0)
RESTING_AT = (-2.2, 1.0, 0.0)


def _scene(name, script):
    next_id = base._ids()
    lines = base._header(name, sky_rgb=(0.05, 0.05, 0.06))
    lines += base._camera(next_id, CAMERA_POSITION, rotation=CAMERA_ROTATION)

    lines += [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        "      Tag: Sun",
        "    TransformComponent:",
        "      Position: [0, 8, 0]",
        "      Rotation: [-0.9, 0.5, 0]",
        "      Scale: [1, 1, 1]",
        "    LightComponent:",
        "      Type: Directional",
        "      Color: [1, 1, 1]",
        "      Intensity: 3",
        "      Range: 60",
        "      InnerCone: 20",
        "      OuterCone: 30",
        "      CastShadows: false",
    ]

    cube = [
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        "      Tag: Mover",
        "    TransformComponent:",
        f"      Position: {base.vec(RESTING_AT)}",
        "      Rotation: [0, 0, 0]",
        "      Scale: [1.4, 1.4, 1.4]",
        "    MeshComponent:",
        f"      Mesh: {base.PRIMITIVE_BASE + base.CUBE}",
        "      Material:",
        "        BaseColor: [0.85, 0.2, 0.2, 1]",
        "        Emissive: [0, 0, 0, 1]",
        "        Metallic: 0",
        "        Roughness: 0.9",
        "        Occlusion: 1",
    ]

    # The one difference between the two scenes.
    if script:
        cube += [
            "    ManagedScriptComponent:",
            f"      Script: {script}",
        ]

    lines += cube
    return "\n".join(lines) + "\n"


# The graph. Written as the serializer writes one -- id order, ids never
# reused -- so a save from the editor produces the same file and this fixture
# does not turn into a diff every time somebody opens it.
MOVER_GRAPH = """ScriptGraph: 1
NextNodeId: 4
NextLinkId: 3
Nodes:
  - Id: 1
    Type: OnCreate
    Pos: [-260, -40]
  - Id: 2
    Type: SetField
    Pos: [40, -40]
    Text: TransformComponent.Position
  - Id: 3
    Type: LiteralString
    Pos: [-260, 140]
    Text: "{0} {1} {2}"
Links:
  - Id: 1
    From: [1, 0]
    To: [2, 0]
  - Id: 2
    From: [3, 0]
    To: [2, 1]
""".format(*MOVED_TO)


def main():
    SCENES.mkdir(parents=True, exist_ok=True)
    GRAPHS.mkdir(parents=True, exist_ok=True)

    for name, script in (("graph_moved", "Mover"), ("graph_still", None)):
        path = SCENES / f"{name}.rage"
        io.open(path, "w", encoding="utf-8", newline="\n").write(_scene(name, script))
        print(f"wrote {path}")

    path = GRAPHS / "Mover.rvgraph"
    io.open(path, "w", encoding="utf-8", newline="\n").write(MOVER_GRAPH)
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
