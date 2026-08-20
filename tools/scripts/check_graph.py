#!/usr/bin/env python3
"""Visual scripting (8.10, ENGINE-NOTES 7bh): does a graph become a script that runs?

Everything before this proves the *text*. `scenetest` pins what the generator
emits and `dotnet build` pins that it compiles. Neither answers the only
question a user of this feature has, which is whether wiring two nodes together
makes the game do something.

So this drives the whole chain and looks at pixels:

    .rvgraph -> C# -> dotnet build -> the engine loads the assembly ->
    the script attaches by name -> it moves an entity -> the frame changes

Two scenes differing in exactly one thing -- whether the cube carries the
generated script. Nothing else in frame moves, so a pixel that differs between
them has one explanation.

**Claim 1.** With the script attached the cube is somewhere else. Measured as
the fraction of pixels that differ; the cube is 1.4 units across and moves 4.4
units across the middle of the frame, so this is not a subtle number and the
band is wide on purpose -- it is a check of "did it run", not of where exactly
a cube landed.

**Claim 2.** A graph that does not generate leaves no file behind. `Broken`
ships alongside precisely so that a run of the generator proves the refusal
every time, not only when somebody remembers to try it.

**The break** (`falsify.py graph-no-exec`) severs the graph's exec link, which
is the smallest edit that makes a valid-looking graph do nothing. Claim 1's
floor is what catches it: the two scenes become identical.

    python tools/scripts/check_graph.py
    python tools/scripts/check_graph.py --config Debug
"""

import argparse
import pathlib
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

import rvcheck

ROOT = pathlib.Path(__file__).resolve().parents[2]

# The cube is large and moves across the frame's middle, so "it ran" is tens of
# per cent of the image, not a fraction of one. The floor is well under what a
# move produces and far above what noise does; the ceiling only catches a
# fixture that has stopped being two nearly-identical scenes -- if half the
# frame changes, something other than the cube moved and the claim is no longer
# about the script.
MIN_MOVED_FRACTION = 0.02
MAX_MOVED_FRACTION = 0.45

# A channel difference below this is the renderer's own run-to-run noise, which
# ENGINE-NOTES 7bc measured at one or two levels on this hardware.
LEVEL = 8


def run(exe, args, cwd=None):
    result = subprocess.run([str(exe), *args], cwd=cwd or str(exe.parent),
                            capture_output=True, text=True)
    return result.returncode, (result.stdout or "") + (result.stderr or "")


def shoot(exe, scene, path, frame=45):
    code, log = run(exe, ["--render-defaults=on", "--rhi=vulkan",
                          f"--scene=scenes/{scene}.rage", "--aa=none",
                          "--frame-time=0.0166", f"--screenshot-frame={frame}",
                          f"--screenshot={path}"])
    if code != 0 or not pathlib.Path(path).exists():
        print(f"FAIL: {scene} exited {code} / no image")
        print(log[-2500:])
        sys.exit(1)
    return rvcheck.require_drawn(
        np.asarray(Image.open(path).convert("RGB")).astype(float), scene)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()

    exe = ROOT / "build" / "bin" / args.config / "RageVRuntime" / "RageVRuntime.exe"
    if not exe.exists():
        print(f"FAIL: {exe} does not exist; build {args.config} first")
        sys.exit(1)

    rvcheck.require_current_shaders(ROOT, args.config)

    shots = ROOT / "build" / "graph"
    shots.mkdir(parents=True, exist_ok=True)

    import make_graph_scene
    make_graph_scene.main()

    failures = []

    # --- 1: the graphs become C# ---------------------------------------------
    #
    # Through the runtime, not the editor: a check that needs a person to click
    # Build Scripts is a check nobody runs.
    generated = ROOT / "SampleProject" / "Scripts" / "Generated"
    stale = generated / "Mover.g.cs"
    if stale.exists():
        stale.unlink()

    code, log = run(exe, ["--render-defaults=on", "--rhi=vulkan",
                          "--generate-graphs=on", "--frame-time=0.0166",
                          "--screenshot-frame=1",
                          f"--screenshot={shots / 'generate.png'}"])
    if code != 0:
        print(f"FAIL: --generate-graphs exited {code}")
        print(log[-2500:])
        sys.exit(1)

    if not stale.exists():
        failures.append("the Mover graph did not generate a .g.cs at all")
    else:
        print(f"the generator wrote {stale.name}")

    # Every other graph in the project must have generated too. This is here
    # because a fixture went stale without anyone noticing: a rename left two
    # of them naming a node type that no longer existed, they stopped
    # generating, and nothing looked -- this check only ever asked about the
    # two graphs it owns. A project's graphs are its graphs; check them all.
    for graph in sorted((ROOT / "SampleProject" / "assets" / "graphs").glob("*.rvgraph")):
        if graph.stem == "Broken":
            continue
        if not (generated / f"{graph.stem}.g.cs").exists():
            failures.append(f"{graph.name} did not generate; every graph in the "
                            f"project except the deliberately broken one must")

    # Claim 2, and it costs nothing because Broken.rvgraph is already there:
    # a graph with errors must leave *no* file, because an empty class compiles,
    # attaches, runs and does nothing (7bf).
    if (generated / "Broken.g.cs").exists():
        failures.append("the Broken graph wrote a file; a graph with errors "
                        "must write none, or an empty script runs and does nothing")
    else:
        print("and the graph with errors wrote nothing, which is the point")

    # --- 2: the C# compiles ---------------------------------------------------
    dotnet = shutil.which("dotnet") or r"C:\Program Files\dotnet\dotnet.exe"
    csproj = ROOT / "SampleProject" / "Scripts" / "Sample.csproj"
    core = ROOT / "RageVScriptCore" / "bin" / args.config / "net8.0" / "RageV.ScriptCore.dll"
    if not core.exists():
        core = ROOT / "RageVScriptCore" / "bin" / "Release" / "net8.0" / "RageV.ScriptCore.dll"

    build = subprocess.run(
        [dotnet, "build", str(csproj), "-c", args.config, "--nologo",
         f"-p:RageVScriptCore={core}",
         f"-p:OutputPath={ROOT / 'SampleProject' / 'Scripts' / 'bin'}"],
        capture_output=True, text=True)
    if build.returncode != 0:
        print("FAIL: the generated C# did not compile")
        print((build.stdout or "")[-2500:])
        sys.exit(1)
    print("the generated C# compiles")

    # --- 3: and it runs -------------------------------------------------------
    moved = shoot(exe, "graph_moved", shots / "moved.png")
    still = shoot(exe, "graph_still", shots / "still.png")

    differing = float((np.abs(moved - still).max(axis=2) > LEVEL).mean())
    print(f"the script moved the cube across {differing * 100:.1f}% of the frame")

    if differing < MIN_MOVED_FRACTION:
        failures.append(
            f"the scripted scene is the same picture as the unscripted one "
            f"({differing * 100:.2f}% of pixels differ, wanted "
            f"{MIN_MOVED_FRACTION * 100:.0f}%) -- the graph generated and "
            f"compiled, but nothing it describes happened in the engine")
    if differing > MAX_MOVED_FRACTION:
        failures.append(
            f"{differing * 100:.1f}% of the frame differs (ceiling "
            f"{MAX_MOVED_FRACTION * 100:.0f}%) -- the two scenes are supposed to "
            f"differ by one cube, so something else is moving and this claim is "
            f"no longer about the script")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("\nOK: a graph becomes C#, the C# compiles, the engine loads it, and "
          "the script it describes moves an entity -- while a graph with errors "
          "writes no file at all")


if __name__ == "__main__":
    main()
