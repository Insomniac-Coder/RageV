"""The object-count curve: what the renderer costs as a scene grows.

Roadmap 8.3 defers GPU-driven rendering on one measurement -- "the renderer
draws a thousand in 1.9 ms and is nowhere near the wall" -- which is a claim
about a curve made from a single point on it. This walks the curve.

Run it before and after any change that claims to make scale cheaper. Every
number it prints comes from the engine's own benchmark; nothing here times
anything itself.
"""
import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "build" / "bin" / "Release" / "RageVRuntime" / "RageVRuntime.exe"

PATTERNS = {
    "frame": r"frame\s+mean\s+([0-9.]+) ms",
    "shadow_cpu": r"shadow maps\s+([0-9.]+)\s+([0-9.]+)",
    "graph_cpu": r"render graph\s+([0-9.]+)\s+([0-9.]+)",
    "accounted": r"accounted\s+([0-9.]+)\s+([0-9.]+)",
    "unaccounted": r"unaccounted\s+([0-9.]+)",
    "gpu": r"whole frame \(GPU\)\s+([0-9.]+) ms",
    "draws": r"([0-9]+) mesh draws, ([0-9]+) culled",
}


def run(scene, frames, width, height):
    if not RUNTIME.exists():
        sys.exit(f"no runtime at {RUNTIME}; build Release first")

    # **--vsync=off is not optional here.** With it on, every number is the
    # display's refresh and the benchmark says so in as many words; measuring
    # a CPU-bound renderer against a 240 Hz cap reports the cap.
    command = [str(RUNTIME), "--project=" + str(ROOT / "SampleProject"),
               "--scene=Scenes/" + scene, "--rhi=vulkan", "--render-defaults=on",
               "--vsync=off", f"--width={width}", f"--height={height}",
               f"--benchmark={frames}"]

    output = subprocess.run(command, cwd=RUNTIME.parent, capture_output=True,
                            text=True, timeout=600).stdout

    found = {}
    for name, pattern in PATTERNS.items():
        match = re.search(pattern, output)
        if match:
            found[name] = match.groups()
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--counts", default="1000,5000,20000,60000")
    parser.add_argument("--frames", type=int, default=200)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    args = parser.parse_args()

    print(f"{'objects':>8}  {'frame':>8}  {'FPS':>6}  {'GPU':>7}  "
          f"{'shadow':>7}  {'graph':>7}  {'other':>7}  {'draws':>6}  {'culled':>8}")
    print("-" * 78)

    for text in args.counts.split(","):
        count = int(text)
        found = run(f"scale_{count}.rage", args.frames, args.width, args.height)
        if "frame" not in found:
            print(f"{count:>8}  -- no benchmark output")
            continue

        frame = float(found["frame"][0])
        gpu = float(found.get("gpu", ("0",))[0])
        shadow = float(found.get("shadow_cpu", ("0", "0"))[0])
        graph = float(found.get("graph_cpu", ("0", "0"))[0])
        other = float(found.get("unaccounted", ("0",))[0])
        draws, culled = found.get("draws", ("0", "0"))

        print(f"{count:>8}  {frame:>7.2f}ms  {1000.0/frame:>6.0f}  {gpu:>6.2f}ms  "
              f"{shadow:>6.2f}ms  {graph:>6.2f}ms  {other:>6.2f}ms  "
              f"{draws:>6}  {culled:>8}")


if __name__ == "__main__":
    main()
