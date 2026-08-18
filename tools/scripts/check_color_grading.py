#!/usr/bin/env python3
"""The check ENGINE-NOTES 7t says decides whether 9.1 is correct.

Colour grading has one mistake that nothing about the picture reveals. A 3D
LUT is sampled at texel *centres*, so a colour c maps to

    uv = (c * (N - 1) + 0.5) / N

and not to c. The difference is half a texel -- about 1.5% of the range at
N = 32. Get it wrong and the image is still graded, still smooth, still
plausible; it is just not the grade that was authored, by a little,
everywhere. There is no artefact to notice and no frame to point at. It is
the same shape of defect as TAA's reprojection sign, which a static scene
could not show (7r): the failure hides inside a result that looks fine.

So this does not ask whether grading changes the picture. It asks:

**1. An identity LUT must be a no-op, exactly.** Every entry is its own
coordinate, so the graded frame has to be *byte-identical* to the ungraded
one. Any scale or offset mistake fails this, on either backend, and nothing
else catches it.

**2. A channel swap must land exactly.** Red and blue exchanged moves table
entries rather than blending them, so it is exact under trilinear
interpolation and the expected output is known to the bit. This separates
"the LUT is sampled at all" from "the LUT is sampled correctly" -- without
it, one failure wears two faces.

Usage:
    python tools/scripts/check_color_grading.py [--config Release]
"""

import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

BACKENDS = ("vulkan", "opengl")

# The identity has to be exact. Not "close": a tolerance here would accept
# precisely the half-texel error this exists to catch, which at N = 33 moves
# a mid-grey by about four levels out of 255.
IDENTITY_MAX_DIFF = 0

# The swap is exact in principle and goes through a 16-bit float table and an
# 8-bit output, so it is allowed the rounding of that path and nothing else.
SWAP_MAX_DIFF = 2


def shoot(exe, backend, out, scene, extra=()):
    """One frame of `scene` on `backend`, as an array."""
    subprocess.run(
        [str(exe), "--render-defaults=on", f"--backend={backend}", "--aa=none", "--validation=on",
         f"--scene={scene}", f"--screenshot={out}", "--screenshot-frame=30",
         "--frame-time=0.016666", *extra],
        check=True, capture_output=True, cwd=exe.parent)

    if not pathlib.Path(out).exists():
        print(f"FAIL: {backend} wrote no screenshot for {scene}")
        sys.exit(1)

    return np.asarray(Image.open(out).convert("RGB")).astype(np.int16)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[2]
    exe = root / "build" / "bin" / args.config / "RageVRuntime" / "RageVRuntime.exe"
    if not exe.exists():
        print(f"FAIL: {exe} does not exist; build {args.config} first")
        sys.exit(1)

    sys.path.insert(0, str(root / "tools" / "scripts"))
    import make_aa_scene
    import make_lut
    import postprofile

    assets = root / "SampleProject" / "assets"
    scenes = assets / "scenes"
    shots = root / "build" / "grading"
    shots.mkdir(parents=True, exist_ok=True)

    # The LUTs, and a profile for each variant. Bloom off throughout: it is
    # the same reason the AA checks turn it off, and here it would also mean
    # the three variants differ in what reaches the tone curve rather than in
    # what the LUT does to it.
    identity = make_lut.write(assets / "luts" / "identity.cube", "identity")
    swap = make_lut.write(assets / "luts" / "swap.cube", "swap")

    # A recipe, written the same way. Neutral, because the property being
    # tested is the one a `.cube` cannot express: a `.rvlut` at its defaults
    # bakes the identity table, so attaching one has to change nothing at all
    # -- through the bake, the upload and the sampler, not merely in the table
    # `CheckLutRecipe` compares on the CPU. ENGINE-NOTES 7v.
    recipe = assets / "luts" / "neutral.rvlut"
    recipe.parent.mkdir(parents=True, exist_ok=True)
    recipe.write_text(
        "LutRecipe: 1\nTemperature: 0\nTint: 0\nLift: [0, 0, 0]\n"
        "Gamma: [1, 1, 1]\nGain: [1, 1, 1]\nContrast: 1\nSaturation: 1\n"
        "Size: 33\n", encoding="utf-8")

    recipe_handle = postprofile.profile_handle(recipe.name)
    recipe.with_name(recipe.name + ".meta").write_text(
        f"Handle: {recipe_handle}\nType: ColorLut\nSourceHash: 0\n", encoding="utf-8")

    profiles = {
        "none": postprofile.write_named(
            assets / "post" / "grade_none.rvpostprofile",
            {"BloomEnabled": False}),
        "recipe": postprofile.write_named(
            assets / "post" / "grade_recipe.rvpostprofile",
            {"BloomEnabled": False, "ColorLut": recipe_handle,
             "ColorLutStrength": 1.0}),
        "identity": postprofile.write_named(
            assets / "post" / "grade_identity.rvpostprofile",
            {"BloomEnabled": False, "ColorLut": identity, "ColorLutStrength": 1.0}),
        "swap": postprofile.write_named(
            assets / "post" / "grade_swap.rvpostprofile",
            {"BloomEnabled": False, "ColorLut": swap, "ColorLutStrength": 1.0}),
    }

    # One scene per variant, identical but for which profile the camera names
    # -- so the only difference between the three frames is the grade.
    shots_by = {}
    failures = []

    for backend in BACKENDS:
        frames = {}
        for name, handle in profiles.items():
            scene = scenes / f"grade_{name}.rage"
            scene.write_text(make_aa_scene.build(8.0, handle))
            frames[name] = shoot(exe, backend, shots / f"{backend}-{name}.png",
                                 f"scenes/{scene.name}")

        plain = frames["none"]

        # --- 1. identity is a no-op ----------------------------------------
        identity_diff = int(np.abs(frames["identity"] - plain).max())
        changed = int((np.abs(frames["identity"] - plain) > 0).sum())
        print(f"--- {backend}")
        print(f"  identity vs no LUT   max {identity_diff:3d}   "
              f"{changed} differing subpixels")

        if identity_diff > IDENTITY_MAX_DIFF:
            failures.append(
                f"{backend}: an identity LUT changed the frame by up to "
                f"{identity_diff}/255. It must change nothing at all -- the "
                f"sampling scale is wrong, which grades every colour slightly "
                f"and looks like nothing")

        # --- 1b. a neutral recipe is a no-op too ----------------------------
        # The same bar as the identity `.cube`, and a different claim: that
        # one tests the *sampling*, this one tests the *bake*. A recipe whose
        # knobs are all at their defaults has to produce a table that grades
        # nothing, so that picking "New colour LUT..." never changes the
        # picture until somebody moves a knob.
        recipe_diff = int(np.abs(frames["recipe"] - plain).max())
        recipe_changed = int((np.abs(frames["recipe"] - plain) > 0).sum())
        print(f"  neutral recipe vs no LUT   max {recipe_diff:3d}   "
              f"{recipe_changed} differing subpixels")

        if recipe_diff > IDENTITY_MAX_DIFF:
            failures.append(
                f"{backend}: a .rvlut with every knob at its default changed the "
                f"frame by up to {recipe_diff}/255. A neutral recipe must bake "
                f"the identity table exactly -- something in the bake is not the "
                f"no-op it is supposed to be at its default value")

        # --- 2. the swap lands ---------------------------------------------
        # Expected: the ungraded frame with R and B exchanged. The LUT is
        # applied after the transfer function, so this compares encoded values
        # -- which is what a swap of encoded channels produces.
        expected = plain[:, :, ::-1]
        swap_diff = int(np.abs(frames["swap"] - expected).max())
        print(f"  swap vs R/B exchanged   max {swap_diff:3d}")

        if swap_diff > SWAP_MAX_DIFF:
            failures.append(
                f"{backend}: a red/blue swap LUT is off by up to {swap_diff}/255 "
                f"from the frame with its channels exchanged, and has to be "
                f"within {SWAP_MAX_DIFF}. The LUT is being sampled, and not "
                f"the way the table says")

        # A swap that changed nothing would pass the difference test above
        # only if the frame were grey everywhere. Said explicitly so the check
        # cannot be satisfied by a LUT that is never sampled at all.
        if int(np.abs(frames["swap"] - plain).max()) == 0:
            failures.append(
                f"{backend}: the swap LUT produced the ungraded frame, so the "
                f"LUT is not reaching the shader at all")

        shots_by[backend] = frames

    # The two backends have to agree: a LUT is a table lookup, and there is no
    # rasterisation in it to differ.
    for name in profiles:
        across = int(np.abs(shots_by["vulkan"][name] - shots_by["opengl"][name]).max())
        if across > SWAP_MAX_DIFF:
            failures.append(
                f"the backends disagree by {across}/255 on the '{name}' grade")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: an identity LUT is a no-op to the byte, a channel swap lands "
          "exactly, and both backends agree")


if __name__ == "__main__":
    main()
