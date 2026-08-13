#!/usr/bin/env python3
"""Reads a render of parity_frame.rage and says whether the tangent frame
told the truth.

The fixture (make_parity_fixture.py) states the answer: the left plane
carries a normal map tilted 45 degrees toward +u = +X and the light shines
from +X, so a truthful backend lights it fully (N.L = 1.0) and it renders
BRIGHTER than the flat control on the right (N.L = 0.707). A backend whose
frame is rotated 180 degrees about the geometric normal -- what a flipped
dFdy does to the screen-derivative construction -- tilts the same normal
AWAY from the light (N.L = 0.0) and the left plane goes nearly black.

The two outcomes differ by an order of magnitude, so the verdict is an
ordering with a wide dead zone, not a tuned threshold. Anything landing in
the dead zone means the render is not this fixture (wrong scene, wrong
camera, wrong size) and the check refuses to answer rather than guessing.

Expects the 800x640 overhead framing the fixture's camera produces (640 is
the smallest window dimension the engine allows, for width and height both).

Usage:
    python tools/scripts/check_tangent_frame.py <render.png> [more.png ...]

Exit code 0 when every render is CORRECT, 1 otherwise.
"""

import sys

from PIL import Image

# Central patches well inside each plane at 800x640: the planes project to
# roughly x [104, 361] and [439, 696], y [191, 449] (camera at height 6,
# vertical FOV 45).
TILT_BOX = (140, 230, 330, 410)     # left plane: the tilted normal map
CONTROL_BOX = (470, 230, 660, 410)  # right plane: no maps at all


def mean_luma(image, box):
    region = image.convert("RGB").crop(box)
    pixels = list(region.getdata())
    return sum(0.2126 * r + 0.7152 * g + 0.0722 * b for r, g, b in pixels) / len(pixels)


def main(paths):
    if not paths:
        print(__doc__)
        return 1

    failed = False
    for path in paths:
        image = Image.open(path)
        if image.size != (800, 640):
            print(f"{path}: FAIL -- expected an 800x640 render of "
                  f"parity_frame.rage, got {image.size[0]}x{image.size[1]}")
            failed = True
            continue

        tilt = mean_luma(image, TILT_BOX)
        control = mean_luma(image, CONTROL_BOX)
        ratio = tilt / max(control, 1e-6)

        if ratio > 1.1:
            verdict = "CORRECT -- tilted plane brighter than control"
        elif ratio < 0.5:
            verdict = "FLIPPED -- the frame is rotated 180 degrees; " \
                      "the tilt faces away from the light"
            failed = True
        else:
            verdict = "FAIL -- ambiguous; is this really parity_frame.rage " \
                      "at 640x480?"
            failed = True

        print(f"{path}: tilt {tilt:.1f} control {control:.1f} "
              f"ratio {ratio:.2f}: {verdict}")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
