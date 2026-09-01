#!/usr/bin/env python3
"""Does `make_bridge_scene.py` still reproduce the committed lighting?

WR-0 (docs/RENDERING-REVAMP.md). The bridge scene is hand-owned: commit
`dab692d` added `SourceRadius`/`Range`/`OuterCone` to 120 deck lamps and 8
tower floods, and 4 marine lights, *directly* in `GoldenGateDemo.rage` --
bypassing the generator, which never learned any of it. Regenerating over
that scene silently reverts the lighting, drops the marine lights, changes
`Scene::LightingHash`, and every run falls back to Realtime GI with nothing
but a log line to say why.

This never writes the scene file. It calls `make_bridge_scene.build()` for
its string return value and diffs the *LightComponent* blocks against
`git show HEAD:.../GoldenGateDemo.rage` as multisets -- order and EntityID
don't matter (the four marine lights get generator-hashed ids, not the
sequential ones they were hand-given; `Scene::LightingHash` never reads an
id, so that mismatch is cosmetic) -- but every field within a block must
appear the same number of times on both sides.

A field the generator does not yet know about (the next hand-edit) fails
this the same way the missing marine lights did: present in committed,
absent in generated.

Usage:
    python tools/scripts/check_bridge_roundtrip.py
"""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SCENE_PATH = "SampleProject/assets/scenes/GoldenGateDemo.rage"

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import make_bridge_scene  # noqa: E402


def light_blocks(text):
    """Every light entity's Position, Rotation and `LightComponent:` fields,
    sorted so field order can't cause a false mismatch -- the generator and
    the hand-edit are not obliged to write fields in the same order.

    **Position and Rotation, not just the LightComponent.** A light's own
    block never states where it is -- that lives in the sibling
    TransformComponent -- and `Scene::LightingHash` mixes both. A check
    that only reads the LightComponent block would pass a lamp moved to the
    wrong end of the bridge, and did on the first draft of this script."""
    lines = text.splitlines()
    blocks = []
    i = 0
    while i < len(lines):
        if lines[i].strip().startswith("- EntityID:"):
            j = i + 1
            component = None
            fields = []
            has_light = False
            while j < len(lines) and not lines[j].strip().startswith("- EntityID:"):
                stripped = lines[j].strip()
                if lines[j].startswith("    ") and not lines[j].startswith("      "):
                    component = stripped.rstrip(":")
                    has_light = has_light or component == "LightComponent"
                elif component == "LightComponent":
                    fields.append("Light." + stripped)
                elif component == "TransformComponent" and stripped.startswith(
                        ("Position:", "Rotation:")):
                    fields.append("Transform." + stripped)
                j += 1
            if has_light:
                blocks.append(tuple(sorted(fields)))
            i = j
        else:
            i += 1
    return blocks


def main():
    generated = make_bridge_scene.build(sky_name="night", seabed_name="bay",
                                         hero="headland")
    result = subprocess.run(["git", "show", "HEAD:" + SCENE_PATH], cwd=ROOT,
                             capture_output=True, text=True)
    if result.returncode != 0:
        print("FAIL: could not read", SCENE_PATH, "from HEAD:", result.stderr)
        sys.exit(1)
    committed = result.stdout

    generated_blocks = sorted(light_blocks(generated))
    committed_blocks = sorted(light_blocks(committed))

    only_generated = list(generated_blocks)
    only_committed = list(committed_blocks)
    for block in committed_blocks:
        if block in only_generated:
            only_generated.remove(block)
    for block in generated_blocks:
        if block in only_committed:
            only_committed.remove(block)

    if only_generated or only_committed:
        print("FAIL: {0} committed light(s) the generator does not "
              "reproduce, {1} generated light(s) not in the committed "
              "scene.".format(len(only_committed), len(only_generated)))
        for block in only_committed:
            print("  committed only:", ", ".join(block))
        for block in only_generated:
            print("  generated only:", ", ".join(block))
        sys.exit(1)

    print("PASS: {0} lights, generator and committed scene agree "
          "field for field.".format(len(committed_blocks)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
