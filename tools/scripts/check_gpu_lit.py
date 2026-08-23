#!/usr/bin/env python3
"""Parity between the draw-submission paths, on a scene with transparency.

The lit pass has four ways to issue the same frame: the GPU-driven indirect
path (`--gpu-lit=on`, the default where bindless exists), the CPU walk
(`--gpu-lit=off`), the meshlet front end (`--meshlets=on`), and the CPU walk
with the front-to-back run reorder off (`--depth-sort=off`).  They are four
spellings of one picture, and this asserts they agree -- on the showroom,
because parity is only worth asserting on a scene that mixes everything:
opaque geometry nearer and farther than blended geometry, several distinct
blended meshes, and a post chain (depth of field, screen-space passes) that
reads what the lit pass wrote.

Two bugs shipped because nothing asserted this, and both hid in plain sight
(HANDOFF, "the gpu-lit defect"):

  1. The run reorder sorted by nearest member and ignored the transparent
     bit, so every opaque run farther than the car's glass was handed to the
     OIT pass: no depth write, so depth of field blurred the walls as "far";
     no g-buffer, so nothing screen-space saw them.  The two paths disagreed
     by 1.6M pixels and the *CPU* side was the wrong one -- while the three
     "references" (cpu, meshlets, bound) all shared the bug and agreed with
     each other, which is exactly why parity across the family is the check
     and no member of it is the oracle.

  2. The blended table's indirect loop stepped by the bare draw-command size
     where the cull writes 24-byte SlotCommand rows, so only the first
     blended mesh ever drew on the GPU path: a windscreen and no headlamp
     glass behind it.

Tolerance is ±2 levels, because the weighted-blend accumulation order between
slots differs between the paths by construction (atomicAdd placement on the
GPU, submission order on the CPU) and float addition is not associative --
measured at 373 of 4M pixels differing by exactly 1 level.  Both bugs sit
three orders of magnitude past it.

Vulkan only: the GPU-driven lit path requires bindless, which OpenGL never
has.  Run from the repository root:

    python tools/scripts/check_gpu_lit.py [--config Release]
"""

import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

# The paths against the default, as (name, extra flags). The default render
# carries no extra flag and is the reference each is compared to.
VARIANTS = [
    ("gpu-lit-off", ["--gpu-lit=off"]),
    ("meshlets", ["--meshlets=on"]),
    ("depth-sort-off", ["--gpu-lit=off", "--depth-sort=off"]),
]

# ±2 levels absorbs the OIT accumulation-order noise; the bugs this exists
# for measured max 215 and a missing mesh respectively.
MAX_DELTA = 2
# And no more than 1% of pixels may need even that much: a real defect moves
# whole surfaces, not scattered rounding.
MAX_FRACTION = 0.01

# The bottom of the frame is the showroom's own UI -- the credit bar and the
# LIGHTS ON button -- and the button's hover highlight follows the *real*
# mouse cursor, so whichever run the pointer happens to sit over renders a
# bright button and a clean 200-level "regression" in the corner. Measured
# doing exactly that. The lit paths this check compares own everything above
# the bar, so the bar is cropped rather than fought.
UI_BAR_FRACTION = 0.07


def render(runtime_dir, project, extra, out):
    result = subprocess.run(
        [os.path.join(runtime_dir, "RageVRuntime.exe"), "--render-defaults=on",
         "--project=" + project, "--scene=scenes/showroom.rage", "--rhi=vulkan",
         # No AA: temporal history would compare two convergences rather than
         # two submissions. Time pinned like every check in this directory.
         "--aa=none", "--validation=on", "--width=1280", "--height=800",
         "--frame-time=0.016666", "--screenshot-frame=60",
         "--screenshot=" + out, *extra],
        cwd=runtime_dir, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"FAIL: {extra} exited {result.returncode}\n"
                         f"{result.stdout}\n{result.stderr}")
    if "[Vulkan]" in result.stdout:
        raise SystemExit(f"FAIL: validation messages under {extra}\n" + result.stdout)
    return np.asarray(Image.open(out).convert("RGB")).astype(int)


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()

    runtime_dir = os.path.join(root, "build", "bin", args.config, "RageVRuntime")
    project = os.path.join(root, "SampleProject")

    failures = 0
    with tempfile.TemporaryDirectory() as temp:
        reference = render(runtime_dir, project, [],
                           os.path.join(temp, "default.png"))
        scene_rows = int(reference.shape[0] * (1.0 - UI_BAR_FRACTION))
        reference = reference[:scene_rows]
        for name, extra in VARIANTS:
            image = render(runtime_dir, project, extra,
                           os.path.join(temp, name + ".png"))[:scene_rows]
            delta = np.abs(image - reference).max(axis=2)
            differing = int((delta > 0).sum())
            fraction = differing / delta.size
            worst = int(delta.max())
            ok = worst <= MAX_DELTA and fraction <= MAX_FRACTION
            print(f"{'pass' if ok else 'FAIL'}  {name}: {differing} px differ "
                  f"({fraction:.4%}), max {worst} levels")
            failures += 0 if ok else 1

    if failures:
        print(f"{failures} check(s) failed")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
