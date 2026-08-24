#!/usr/bin/env python3
"""What the traced bounce's emitter list does with emissive it only partly, or
never, answers for.

Two claims, from the fixtures `make_emitter_scene.py` writes. Both were false
before the change they guard, and both were false *silently* -- the pictures
looked like lighting choices rather than like defects, which is why they lasted.

**1. A partly-lit surface emits the power it actually emits.** The list stands
a rectangle in for an emissive mesh and radiates it evenly, taking the
material's emissive scalar as the whole rectangle's radiance -- correct for a
panel that glows all over, and wrong by the ratio of lit to unlit area for one
whose emissive *map* is mostly dark. `emitter_holes` lights four cells of a
hundred and forty-four through its map; `emitter_uniform` is the same plane
emitting the same total power evenly, at a scalar low enough to fall out of the
list, so the bounce finds it by hemisphere sampling instead. Two estimators of
one physical light: they must land together. Before the map's mean was folded
in, the textured room was **thirty-six times** brighter.

**2. An emitter over there does not change a glow over here.** The bounce
removes a hit's emissive because a shadow ray already answered for it -- and it
used to do that whenever the list was non-empty *at all*, though membership is
filtered by a strength threshold, by a degenerate-rectangle drop and by the
sixteen cap. So a surface the list left out had its light subtracted by the
hemisphere term and added by nothing. `emitter_glow` is lit by a ceiling under
the threshold; `emitter_glow_plus` adds a bright emitter sealed inside a closed
box in the corner, whose light reaches nothing. The floors must match. Before,
the sealed box put the room in darkness.

The bands are wide on purpose and the reason is claim 1's: the two rooms are
measured through *different estimators*, and next-event estimation and
hemisphere sampling differ by a few per cent on the same light -- measured at
0.91 here. A band tight enough to call that a failure would fail on ray count,
on accumulation, and on a driver. What it must catch is a factor of thirty-six,
and it has three and a half stops of room before it would miss one.

Vulkan only: the traced bounce needs ray queries and bindless, which OpenGL
has neither of.

    python tools/scripts/make_emitter_scene.py
    python tools/scripts/check_emitters.py [--config Release]
"""

import argparse
import os
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

import rvcheck

# Claim 1. One is exact; 0.91 is what two estimators of one light measure.
# The failure this exists for is 36x, and 0.62 catches anything past a
# half-stop.
MEAN_BAND = (0.62, 1.6)

# Claim 2. The sealed box changes nothing at all, so this band is only as
# wide as the sampling noise between two runs of two scenes that differ by
# geometry the light never reaches.
GLOW_BAND = (0.94, 1.06)

# Claim 3. The same map's mean, by two entirely different pieces of code:
# decoded off the cooked mip chain, or scanned from the raw pixels. Block
# compression is lossy and the two will not agree to the byte, but a few per
# cent is all the room they need -- and the defect this exists for was 2.27x.
PATH_BAND = (0.93, 1.07)


def render(exe, scene, out, cooked=True):
    result = subprocess.run(
        [str(exe), "--render-defaults=on", "--raytracing=on", "--rt-gi=on",
         "--rhi=vulkan", f"--scene=scenes/{scene}.rage",
         *([] if cooked else ["--import-cache=off"]),
         # The bounce is what is being measured, so nothing temporal or
         # spatial may sit on top of it, and the frame is pinned late enough
         # for the accumulation to have settled.
         # **The import cache left ON, deliberately.** It is the path the
         # editor, the runtime and every packaged game take, and it is the
         # one where the emissive map's mean is read from a cooked mip chain
         # rather than from raw pixels -- two different pieces of code for
         # one number. Turning the cache off here would have checked only
         # the path nothing ships, and the cooked one was wrong by 2.27x on
         # this very fixture while the raw one was exact.
         #
         # Safe because the cache keys on the source's content: regenerate a
         # fixture and it re-cooks.
         "--aa=none", "--width=800", "--height=500",
         "--frame-time=0.0166", "--screenshot-frame=120", f"--screenshot={out}"],
        cwd=exe.parent, capture_output=True, text=True)

    if result.returncode != 0 or not os.path.exists(out):
        print(f"FAIL: {scene} did not render (exit {result.returncode})")
        print(result.stdout[-2000:], result.stderr[-2000:])
        sys.exit(1)
    if "[Vulkan]" in result.stdout:
        print(f"FAIL: validation messages rendering {scene}\n" + result.stdout)
        sys.exit(1)

    # The floor, which is the only thing in these rooms that the bounce lights
    # and the camera sees whole. Not the frame: the ceiling is in it, and a
    # ceiling that emits is not evidence about the light it sheds.
    image = np.asarray(Image.open(out).convert("RGB")).astype(float)
    return image[int(image.shape[0] * 0.55):, :].mean()


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()

    rvcheck.require_current_shaders(root, args.config)

    exe = root / "build" / "bin" / args.config / "RageVRuntime" / "RageVRuntime.exe"
    if not exe.exists():
        print(f"FAIL: {exe} does not exist; build {args.config} first")
        sys.exit(1)

    scenes = root / "SampleProject" / "assets" / "scenes"
    for name in ("emitter_holes", "emitter_uniform", "emitter_glow", "emitter_glow_plus"):
        if not (scenes / f"{name}.rage").exists():
            print(f"SKIP: no {name}.rage; generate with make_emitter_scene.py")
            return 0

    shots = root / "build" / "emitter-check"
    shots.mkdir(parents=True, exist_ok=True)

    floors = {name: render(exe, name, shots / f"{name}.png")
              for name in ("emitter_holes", "emitter_uniform",
                           "emitter_glow", "emitter_glow_plus")}
    raw_holes = render(exe, "emitter_holes", shots / "emitter_holes_raw.png", cooked=False)

    failures = 0

    # --- claim 1 -------------------------------------------------------------
    ratio = floors["emitter_holes"] / max(floors["emitter_uniform"], 1e-6)
    ok = MEAN_BAND[0] <= ratio <= MEAN_BAND[1]
    failures += 0 if ok else 1
    print(f"{'pass' if ok else 'FAIL'}  a partly-lit surface emits its own power: "
          f"textured {floors['emitter_holes']:.2f} against uniform "
          f"{floors['emitter_uniform']:.2f} of the same power, ratio {ratio:.2f} "
          f"(band {MEAN_BAND[0]}-{MEAN_BAND[1]})")
    if not ok:
        print("      A ratio far above the band means the emitter list is taking the "
              "material's scalar as the whole surface's radiance again -- the map's "
              "mean is not reaching AreaEmitter::Radiance (Scene.cpp's emitter walk, "
              "Material::GetEmissiveMean, TextureLoader::MeanColor). Far below it "
              "means the mean is being applied twice, or to the wrong map.")

    # --- claim 2 -------------------------------------------------------------
    ratio = floors["emitter_glow_plus"] / max(floors["emitter_glow"], 1e-6)
    ok = GLOW_BAND[0] <= ratio <= GLOW_BAND[1]
    failures += 0 if ok else 1
    print(f"{'pass' if ok else 'FAIL'}  a sealed emitter elsewhere changes nothing: "
          f"{floors['emitter_glow_plus']:.2f} against {floors['emitter_glow']:.2f}, "
          f"ratio {ratio:.2f} (band {GLOW_BAND[0]}-{GLOW_BAND[1]})")
    if not ok:
        print("      A ratio well below one means the bounce is subtracting emissive "
              "from surfaces the emitter list does not answer for: the per-instance "
              "RAY_INSTANCE_EMITTER flag is not reaching the shader (Scene.cpp sets "
              "AreaEmitter::Owner and passes the same id to RayShadows::AddInstance; "
              "Renderer3D::EndScene matches them onto the ray instance).")

    # --- claim 3 -------------------------------------------------------------
    #
    # **Two pieces of code compute this one number**, and only one of them
    # ships. The cooked path reads the map's average off a mip the cooker
    # already filtered; the raw path scans every texel. A check that turned
    # the import cache off tested the path nothing uses, and the shipping one
    # was 2.27x wrong on this very fixture -- exactly, and only, for a texture
    # whose size is not a power of two, because the cooker's box filter drops
    # an odd level's tail row.
    ratio = floors["emitter_holes"] / max(raw_holes, 1e-6)
    ok = PATH_BAND[0] <= ratio <= PATH_BAND[1]
    failures += 0 if ok else 1
    print(f"{'pass' if ok else 'FAIL'}  cooked and raw agree on the map's mean: "
          f"{floors['emitter_holes']:.2f} against {raw_holes:.2f}, ratio {ratio:.2f} "
          f"(band {PATH_BAND[0]}-{PATH_BAND[1]})")
    if not ok:
        print("      The two derivations of the emissive map's average disagree: "
              "TextureLoader::MeanOfCooked walks the cooked chain, MeanOfPixels "
              "scans the source. Suspect the level MeanOfCooked settles on -- it "
              "must be the deepest one reached by even halvings, because an odd "
              "level's reduction throws its tail away.")

    if failures:
        print(f"{failures} check(s) failed")
        return 1
    print("OK: emissive is counted once, and only where something answers for it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
