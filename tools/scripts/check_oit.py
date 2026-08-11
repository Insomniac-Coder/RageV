#!/usr/bin/env python3
"""Regression check for weighted-blended transparency.

Two bugs shipped in the weighted path and neither was visible in a still of
smoke, which is why this exists as a check rather than a screenshot somebody
remembers to look at.  Both are properties, not pixels, so this compares
against a render that is definitionally correct instead of a stored image:
the same particles drawn as sorted `Alpha`, which one CPU emitter gets right
by construction.

  1. Orientation.  The resolve is a single fullscreen pass reading a render
     target, so it owes the V flip that PostProcess::Dispatch explains.  It
     did not pay it, and Vulkan composited the transparency upside down --
     invisible against plumes that sit near the middle of frame.  Caught by
     comparing silhouettes with the sorted render.

  2. The weight still discriminates.  The depth weight saturated its clamp at
     every distance, which makes the resolve a flat average: correct-looking
     and completely unordered.  Caught by rendering red at 2 units over green
     at 20 over blue at 200 and requiring the near layer to actually win.

Needs the scenes from make_depth_scene.py.  Run from the repository root:

    python tools/scripts/make_depth_scene.py
    python tools/scripts/check_oit.py --config Debug
"""

import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

BACKGROUND = np.array([62, 62, 70])


def render(runtime_dir, project, scene, backend, out):
    result = subprocess.run(
        [os.path.join(runtime_dir, "RageVRuntime.exe"),
         "--project=" + project, "--scene=scenes/" + scene, "--rhi=" + backend,
         "--validation=on", "--screenshot=" + out],
        cwd=runtime_dir, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"FAIL: {scene} on {backend} exited {result.returncode}\n"
                         f"{result.stdout}\n{result.stderr}")
    if "[Vulkan]" in result.stdout:
        raise SystemExit(f"FAIL: validation messages rendering {scene} on {backend}\n"
                         + result.stdout)
    return np.asarray(Image.open(out).convert("RGB")).astype(int)


def coverage(image):
    """Where the particles are, as opposed to the cleared background."""
    return np.abs(image - BACKGROUND).max(axis=2) > 20


def centre(image, half=40):
    h, w, _ = image.shape
    return image[h // 2 - half:h // 2 + half,
                 w // 2 - half:w // 2 + half].reshape(-1, 3).mean(axis=0)


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="Debug")
    parser.add_argument("--backends", default="vulkan,opengl")
    parser.add_argument("--project", default=os.path.join(root, "SampleProject"))
    args = parser.parse_args()

    runtime_dir = os.path.join(root, "build", "bin", args.config, "RageVRuntime")
    if not os.path.isdir(runtime_dir):
        raise SystemExit(f"FAIL: no runtime at {runtime_dir} -- build {args.config} first")

    failures = 0
    checks = 0
    with tempfile.TemporaryDirectory() as tmp:
        for backend in args.backends.split(","):
            reference = render(runtime_dir, args.project, "particles_depth_alpha.rage",
                               backend, os.path.join(tmp, f"alpha_{backend}.png"))
            weighted = render(runtime_dir, args.project, "particles_depth_weighted.rage",
                              backend, os.path.join(tmp, f"weighted_{backend}.png"))

            mask_ref, mask_weighted = coverage(reference), coverage(weighted)

            # 1. Same pixels covered, the same way up.
            wrong = int((mask_ref ^ mask_weighted).sum())
            flipped = int((mask_ref ^ mask_weighted[::-1]).sum())
            checks += 1
            if wrong != 0:
                failures += 1
                how = "vertically flipped" if flipped == 0 else "misplaced"
                print(f"FAIL [{backend}] weighted transparency is {how}: "
                      f"{wrong} pixels differ from the sorted render")
            else:
                print(f"OK   [{backend}] weighted silhouette matches the sorted render")

            # 2. Near beats far. Red sits at 2 units, green at 20, blue at 200,
            #    so a weight that still discriminates puts them in that order.
            #    A saturated weight averages them and the centre goes grey.
            r, g, b = centre(weighted)
            checks += 1
            if not (r > g + 10 and g > b + 5):
                failures += 1
                print(f"FAIL [{backend}] the depth weight does not order by distance: "
                      f"centre is ({r:.0f},{g:.0f},{b:.0f}), wanted red > green > blue "
                      f"(a flat average reads grey)")
            else:
                print(f"OK   [{backend}] near layers outweigh far ones "
                      f"({r:.0f} > {g:.0f} > {b:.0f})")

    print(f"\n{checks - failures}/{checks} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
