#!/usr/bin/env python3
"""What ENGINE-NOTES 7w has to be true for: vignette, aberration, grain.

Four claims, and only the last one is about a picture.

**1. Off is exactly off.** All three default to zero, and a profile that has
not touched them must render the *same bytes* as one that has none of these
features at all. Not nearly the same: byte-identical. Every screenshot check
in this repository compares renders against recorded values, and an effect
that shifts one channel by one level while "disabled" would invalidate all of
them silently -- the images would still look right, and every threshold would
have moved underneath. So the shader branches past each effect rather than
computing a no-op, and this is what says so.

**2. Each one does something.** The mirror of the above: an effect skipped too
eagerly is a control that quietly does nothing, which is the failure the
branches could introduce.

**3. Grain animates.** It is noise; noise that does not move is a dirty lens.

**4. And grain reproduces.** This is the one that matters to everybody else.
Animated noise makes the frame a function of *when* it was drawn, which is
exactly the hazard 7r hit with TAA's jitter -- so grain is seeded from the
frame number rather than a clock. Frame 30 twice must be the same picture, and
frame 30 against frame 31 must not. Either alone is passable by a mistake:
a constant pattern passes "reproduces", and a clock passes "animates". Only
the pair together says *animated and deterministic*.

Usage:
    python tools/scripts/check_lens_effects.py [--config Release]
"""

import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

BACKENDS = ("vulkan", "opengl")

# Off has to be off to the byte. A tolerance here would accept precisely the
# silent drift this exists to prevent.
OFF_MAX_DIFF = 0

# The frame the reproducibility pair is taken at, and its neighbour. Both well
# past the point where the scene has settled.
FRAME = 30


def shoot(exe, backend, out, scene, frame=FRAME):
    subprocess.run(
        [str(exe), f"--backend={backend}", "--aa=none", "--validation=on",
         f"--scene={scene}", f"--screenshot={out}", f"--screenshot-frame={frame}",
         "--frame-time=0.016666"],
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
    import postprofile

    assets = root / "SampleProject" / "assets"
    scenes = assets / "scenes"
    shots = root / "build" / "lens"
    shots.mkdir(parents=True, exist_ok=True)

    # Bloom off throughout, for the reason the grading and AA checks turn it
    # off: it spreads a bright edge across the pixels being measured, and here
    # it would also mean the variants differ in what reaches the tone curve
    # rather than in what these effects do to it.
    base = {"BloomEnabled": False}

    variants = {
        "off":        dict(base),
        "vignette":   dict(base, VignetteIntensity=0.8, VignetteSmoothness=0.6),
        "aberration": dict(base, ChromaticAberration=0.02),
        "grain":      dict(base, FilmGrain=0.8, FilmGrainSize=2.0),
    }

    profiles = {
        name: postprofile.write_named(
            assets / "post" / f"lens_{name}.rvpostprofile", settings)
        for name, settings in variants.items()
    }

    failures = []

    for backend in BACKENDS:
        print(f"--- {backend}")

        frames = {}
        for name, handle in profiles.items():
            scene = scenes / f"lens_{name}.rage"
            scene.write_text(make_aa_scene.build(8.0, handle))
            frames[name] = shoot(exe, backend, shots / f"{backend}-{name}.png",
                                 f"scenes/{scene.name}")

        # --- 1. off is exactly off -----------------------------------------
        #
        # Against a scene whose profile names none of these keys at all, so
        # the comparison is "the feature exists but is zero" against "the
        # feature was never mentioned" -- which is what every older profile in
        # the project looks like.
        untouched = scenes / "lens_untouched.rage"
        untouched_handle = postprofile.write_named(
            assets / "post" / "lens_untouched.rvpostprofile", {"BloomEnabled": False})
        untouched.write_text(make_aa_scene.build(8.0, untouched_handle))
        plain = shoot(exe, backend, shots / f"{backend}-untouched.png",
                      f"scenes/{untouched.name}")

        off_diff = int(np.abs(frames["off"] - plain).max())
        print(f"  all three off vs untouched   max {off_diff}")

        if off_diff > OFF_MAX_DIFF:
            failures.append(
                f"{backend}: with vignette, aberration and grain all at zero the "
                f"frame differs by {off_diff}/255 from one whose profile does not "
                f"mention them. Off has to be off to the byte, or every recorded "
                f"threshold in this repository has moved underneath it")

        # --- 2. each one does something ------------------------------------
        for name in ("vignette", "aberration", "grain"):
            changed = int((np.abs(frames[name] - frames["off"]) > 0).sum())
            print(f"  {name:11s} changes {changed} subpixels")

            if changed == 0:
                failures.append(
                    f"{backend}: {name} at a large setting changed nothing at all. "
                    f"The effect is not reaching the shader, or is being branched "
                    f"past when it should not be")

        # The vignette has a direction, and "it changed pixels" would pass with
        # the sign inverted -- a bright ring instead of dark corners.
        corner = np.s_[:60, :60]
        centre_h, centre_w = frames["off"].shape[0] // 2, frames["off"].shape[1] // 2
        centre = np.s_[centre_h - 30:centre_h + 30, centre_w - 30:centre_w + 30]

        corner_drop = float(frames["off"][corner].mean() - frames["vignette"][corner].mean())
        centre_drop = float(frames["off"][centre].mean() - frames["vignette"][centre].mean())
        print(f"  vignette darkens the corner by {corner_drop:.1f} and the centre "
              f"by {centre_drop:.1f}")

        if corner_drop <= centre_drop:
            failures.append(
                f"{backend}: the vignette darkened the centre as much as the corner "
                f"({centre_drop:.1f} against {corner_drop:.1f}). It is inverted, or "
                f"the falloff is not a function of distance from the middle")

        # --- 3 and 4. grain animates, and reproduces -----------------------
        again = shoot(exe, backend, shots / f"{backend}-grain-again.png",
                      "scenes/lens_grain.rage")
        neighbour = shoot(exe, backend, shots / f"{backend}-grain-31.png",
                          "scenes/lens_grain.rage", frame=FRAME + 1)

        repeat_diff = int(np.abs(frames["grain"] - again).max())
        neighbour_diff = int((np.abs(frames["grain"] - neighbour) > 0).sum())
        print(f"  grain frame {FRAME} twice: max {repeat_diff}   "
              f"against frame {FRAME + 1}: {neighbour_diff} subpixels differ")

        if repeat_diff > 0:
            failures.append(
                f"{backend}: two renders of frame {FRAME} with grain differ by "
                f"{repeat_diff}/255. The noise is seeded from something that is not "
                f"the frame number -- a clock, most likely -- which makes every "
                f"screenshot comparison in this repository unreliable the moment "
                f"grain is switched on")

        if neighbour_diff == 0:
            failures.append(
                f"{backend}: frame {FRAME} and frame {FRAME + 1} have identical "
                f"grain. It is not animating, which is a dirty lens rather than "
                f"film grain")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: off is off to the byte, each effect lands, the vignette darkens "
          "the corner rather than the middle, and the grain animates without "
          "becoming irreproducible")


if __name__ == "__main__":
    main()
