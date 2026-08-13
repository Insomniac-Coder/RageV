#!/usr/bin/env python3
"""Does the import cache change what the engine draws?

Cooking is lossy by design (7i), so the acceptance test cannot be "identical".
It has to be a *bounded* difference -- and a bound is only meaningful next to
the noise the same comparison produces when nothing changed. So this renders
the same scene three times:

    A   cooked      (--import-cache=on)
    A'  cooked      (again, identical flags)   <- the control
    B   from source (--import-cache=off)

and reports mean/max per subpixel for A-vs-A' and A-vs-B. The control is the
whole point: without it, "mean 5.2/255" is a number with nothing to compare
against, and the first run of this reported exactly that from a scene whose
physics had simply settled differently between two runs of *the same build*.

`--frame-time` is passed for the same reason it exists: the demo scene has
falling cubes and an animated fox, so anything driven by the clock makes two
captures of one build disagree, and the comparison then measures the scheduler
rather than the change under test.

Usage:
    python tools/scripts/check_import_cache.py [--config Release] [--rhi vulkan]
"""

import argparse
import pathlib
import subprocess
import sys

# Cooking a 4K map to BC quantizes it; 7.2e measured the cooked pak at mean
# 0.89/255 against loose on this asset set, under the 1.28/255 the two backends
# already disagree by. Anything past this is a defect in the cooker or in the
# cache, not the codec being lossy.
MEAN_LIMIT = 2.0


def render(exe, out, rhi, extra):
    args = [str(exe), f"--rhi={rhi}", f"--screenshot={out}",
            "--frame-time=0.0166", *extra]
    result = subprocess.run(args, cwd=exe.parent, capture_output=True, text=True)
    if result.returncode != 0 or not pathlib.Path(out).exists():
        print(f"FAIL: {' '.join(args)} exited {result.returncode}")
        print(result.stdout[-2000:], result.stderr[-2000:])
        sys.exit(1)
    return result.stdout


def compare(first, second):
    from PIL import Image

    a = Image.open(first).convert("RGB")
    b = Image.open(second).convert("RGB")
    if a.size != b.size:
        print(f"FAIL: {first} is {a.size}, {second} is {b.size}")
        sys.exit(1)

    total = 0
    worst = 0
    differing = 0
    count = a.size[0] * a.size[1] * 3

    for pa, pb in zip(a.getdata(), b.getdata()):
        for x, y in zip(pa, pb):
            d = abs(x - y)
            total += d
            if d > worst:
                worst = d
            if d:
                differing += 1

    return total / count, worst, differing, count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="Release")
    parser.add_argument("--rhi", default="vulkan")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[2]
    exe = root / "build" / "bin" / args.config / "RageVRuntime" / "RageVRuntime.exe"
    if not exe.exists():
        print(f"FAIL: {exe} does not exist; build the {args.config} configuration first")
        sys.exit(1)

    shots = root / "build" / "import-cache-check"
    shots.mkdir(parents=True, exist_ok=True)

    cooked = shots / f"cooked-{args.rhi}.png"
    again = shots / f"cooked-again-{args.rhi}.png"
    source = shots / f"source-{args.rhi}.png"

    log = render(exe, cooked, args.rhi, ["--import-cache=on"])
    render(exe, again, args.rhi, ["--import-cache=on"])
    render(exe, source, args.rhi, ["--import-cache=off"])

    # A run that read nothing cooked would compare two identical source
    # renders and pass while proving nothing at all.
    loaded = log.count("Loaded cooked texture") + log.count("Loaded cooked")
    if loaded == 0:
        print("FAIL: --import-cache=on loaded nothing cooked; the cache is not being "
              "consulted, so this comparison would pass without testing anything")
        sys.exit(1)

    noise_mean, noise_max, _, _ = compare(cooked, again)
    mean, worst, differing, count = compare(cooked, source)

    print(f"backend            {args.rhi}")
    print(f"cooked assets      {loaded}")
    print(f"control (run-run)  mean {noise_mean:.3f}/255  max {noise_max}")
    print(f"cooked vs source   mean {mean:.3f}/255  max {worst}  "
          f"differing {differing} of {count} ({100.0 * differing / count:.1f}%)")

    if noise_mean > MEAN_LIMIT:
        print(f"FAIL: two identical runs differ by {noise_mean:.3f}/255, which is more "
              f"than the {MEAN_LIMIT}/255 budget for the change itself -- the "
              f"comparison is measuring something other than cooking")
        sys.exit(1)

    if mean > MEAN_LIMIT:
        print(f"FAIL: cooked differs from source by mean {mean:.3f}/255, over the "
              f"{MEAN_LIMIT}/255 bound")
        sys.exit(1)

    print(f"OK: cooked is within {MEAN_LIMIT}/255 of source, on a control of "
          f"{noise_mean:.3f}/255")


if __name__ == "__main__":
    main()
