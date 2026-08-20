#!/usr/bin/env python3
"""Front-to-back ordering: is it free, and is it worth anything?

Two claims, and they need different evidence.

**It must change nothing.** Reordering opaque, depth-tested draws cannot
alter the image, and it must not alter the batching either -- the whole design
of 7.8 is that instances are reordered *within* a batch, so the draw count is
the thing that proves grouping survived. Both are checked exactly: pixels
identical, draw count identical.

**It must be worth something.** Early-z only saves work when a scene overlaps
itself, so the win is entirely a property of the content. This reports the
gap on a scene built to have nothing but overdraw, which is the ceiling, and
leaves judging a real scene to whoever has one.

Usage:
    python tools/scripts/check_depth_sort.py [--config Release] [--rhi vulkan]
"""

import argparse
import pathlib
import re
import subprocess
import sys

# Reordering opaque draws is not an approximation of anything -- it either
# changes the image or it does not, and it must not.
EXACT = 0


def run(exe, args):
    result = subprocess.run([str(exe), "--render-defaults=on", *args], cwd=exe.parent,
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAIL: {' '.join(args)} exited {result.returncode}")
        print(result.stdout[-2000:], result.stderr[-2000:])
        sys.exit(1)
    return result.stdout + result.stderr


def draws_and_cost(output):
    """Draw counts, and the best available cost figure with its source.

    The render graph's own GPU span is the sharp instrument, but OpenGL does
    not always resolve its timestamps -- on the overdraw scene it reports
    "no GPU timings resolved" -- so the whole-frame mean is the fallback.
    That is a fair substitute here and only here: the scene is deliberately
    GPU bound, so the frame *is* the graph plus a constant.
    """
    draws = re.search(r"(\d+) mesh draws, (\d+) culled, (\d+) triangles", output)

    gpu = re.search(r"render graph\s+[\d.]+\s+([\d.]+)", output)
    if gpu:
        return draws.groups() if draws else None, float(gpu.group(1)), "graph GPU"

    frame = re.search(r"frame\s+mean\s+([\d.]+) ms", output)
    if frame:
        return draws.groups() if draws else None, float(frame.group(1)), "frame mean"

    return draws.groups() if draws else None, None, "none"


def compare(first, second):
    from PIL import Image

    a = Image.open(first).convert("RGB")
    b = Image.open(second).convert("RGB")
    if a.size != b.size:
        print(f"FAIL: {a.size} vs {b.size}")
        sys.exit(1)

    differing = 0
    worst = 0
    for pa, pb in zip(a.getdata(), b.getdata()):
        for x, y in zip(pa, pb):
            d = abs(x - y)
            if d:
                differing += 1
                worst = max(worst, d)
    return differing, worst, a.size[0] * a.size[1] * 3


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="Release")
    parser.add_argument("--rhi", default="vulkan")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[2]
    exe = root / "build" / "bin" / args.config / "RageVRuntime" / "RageVRuntime.exe"

    # The runtime loads `assets/` beside its exe, which the build copies there
    # (ENGINE-NOTES 7be, hole 3): a shader edited but not built is measured as
    # the last build's shader, and nothing says so.
    import rvcheck
    rvcheck.require_current_shaders(root, args.config)
    if not exe.exists():
        print(f"FAIL: {exe} does not exist; build {args.config} first")
        sys.exit(1)

    shots = root / "build" / "depth-sort-check"
    shots.mkdir(parents=True, exist_ok=True)

    # --- the image and the batching must not move -------------------------
    frames = {}
    counts = {}
    for state in ("on", "off"):
        shot = shots / f"{args.rhi}-{state}.png"
        run(exe, [f"--rhi={args.rhi}", f"--depth-sort={state}",
                  "--frame-time=0.0166", f"--screenshot={shot}"])
        frames[state] = shot

        out = run(exe, [f"--rhi={args.rhi}", f"--depth-sort={state}",
                        "--vsync=off", "--benchmark=200"])
        counts[state], _, _ = draws_and_cost(out)

    differing, worst, total = compare(frames["on"], frames["off"])
    print(f"backend            {args.rhi}")
    print(f"pixels             {differing} of {total} differ (max {worst})")
    print(f"draws  sorted      {counts['on']}")
    print(f"       unsorted    {counts['off']}")

    if differing > EXACT:
        print(f"FAIL: reordering opaque draws changed {differing} subpixels; it must "
              f"change none -- the depth test is what decides what is visible")
        sys.exit(1)

    if counts["on"] != counts["off"]:
        print("FAIL: the draw count moved, so sorting broke a batch -- instances must "
              "be reordered *within* a run, never across one")
        sys.exit(1)

    # --- and it has to buy something where overdraw exists -----------------
    scene = root / "SampleProject" / "assets" / "scenes" / "overdraw.rage"
    if not scene.exists():
        print("SKIP: no overdraw.rage; generate it with make_overdraw_scene.py")
        print("OK: identical pixels and identical batching")
        return

    gpu = {}
    source = "none"
    for state in ("on", "off"):
        out = run(exe, [f"--rhi={args.rhi}", "--scene=scenes/overdraw.rage",
                        f"--depth-sort={state}", "--vsync=off", "--benchmark=200"])
        _, gpu[state], source = draws_and_cost(out)

    if not gpu["on"] or not gpu["off"]:
        print("SKIP: no timing resolved for the overdraw scene; pixels and batching "
              "were still checked")
        return

    speedup = gpu["off"] / gpu["on"]
    print(f"overdraw scene     {gpu['on']:.2f} ms sorted, {gpu['off']:.2f} ms unsorted "
          f"({speedup:.0f}x, by {source})")

    # A floor rather than a target. The point is that the mechanism is
    # working at all: if early-z stopped rejecting -- a shader that writes
    # depth or discards would do it -- this collapses to 1x and the whole
    # feature silently becomes a sort that buys nothing.
    if speedup < 5.0:
        print(f"FAIL: only {speedup:.1f}x on a scene that is nothing but overdraw. "
              f"Early-z is not rejecting; check for discard or gl_FragDepth in the "
              f"opaque shaders")
        sys.exit(1)

    print("OK: identical pixels, identical batching, and early-z is doing its job")


if __name__ == "__main__":
    main()
