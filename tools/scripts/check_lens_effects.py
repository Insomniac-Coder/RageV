#!/usr/bin/env python3
"""What ENGINE-NOTES 7w has to be true for: vignette, aberration, grain.

Six claims, and only the last two are about how the grain *looks*.

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

**5. Grain is clumps, and clumps have a size.** Another pair, for the same
reason as the one above. A field of blocks -- `hash(floor(coord / size))`,
which is what this used to be -- is piecewise constant, so half of all
adjacent pixels are *exactly* equal; a field of per-pixel white noise has no
correlation between neighbours at all. Asserting only the first passes white
noise, asserting only the second passes a blur. Together they say: continuous,
correlated at the grain size, and decorrelated well before twice it.

**6. Grain peaks in the midtones.** Film has nothing left to vary once nothing
is exposed or everything is, so the amplitude has to fall off at *both* ends
of the tone scale. This is the one that caught a real defect: the curve was
`1.0 - luma * 0.5`, which is largest at black, under a comment claiming it
kept the shadows clean. Nothing in the suite could see it, because the AA
scene these other checks use has two flat levels and no tone ramp at all --
so this one builds a scene that has one. ENGINE-NOTES 7x.

Usage:
    python tools/scripts/check_lens_effects.py [--config Release]
"""

import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "scripts"))

import make_aa_scene  # noqa: E402  -- needs the path above
import postprofile  # noqa: E402

BACKENDS = ("vulkan", "opengl")

# Off has to be off to the byte. A tolerance here would accept precisely the
# silent drift this exists to prevent.
OFF_MAX_DIFF = 0

# The frame the reproducibility pair is taken at, and its neighbour. Both well
# past the point where the scene has settled.
FRAME = 30

# Claim 5, the pair that says "clumps of a definite size".
#
# A block field is piecewise constant: with a 2-pixel cell exactly half of all
# horizontally adjacent pixels are inside the same cell and therefore
# identical. Smooth noise lands at 5.7% (two values quantised to 8 bits that
# happen to coincide), so 20% is nowhere near either and cannot be reached by
# rounding.
MAX_ADJACENT_IDENTICAL = 0.20

# And the correlations, measured at grain size 2: +0.507 at one pixel, +0.130
# at two, +0.001 at four. White noise is 0 at every lag and fails the first;
# anything blurred stays high at four and fails the second.
MIN_NEIGHBOUR_CORRELATION = 0.30
MAX_FAR_CORRELATION = 0.15

# Claim 6. The response curve is sqrt(4L(1-L)), so relative to its peak the
# bands below sit at 0.44 (L=0.05), 0.99 (L=0.44) and 0.48 (L=0.94). A margin
# of 1.3x is comfortably inside that and comfortably outside measurement noise
# -- and the curve this replaced ran 0.98, 0.78, 0.53, which fails the dark
# comparison outright.
MIDTONE_MARGIN = 1.3

# Emissive values whose *rendered* luma spreads across the whole tone scale
# once ACES and the transfer function have had them: roughly 0.05, 0.13, 0.24,
# 0.44, 0.64, 0.84, 0.94, 1.00. Chosen by inverting the curve rather than by
# spacing them evenly, because evenly spaced emitters land almost entirely in
# the top half.
RAMP_EMISSIVE = (0.005, 0.02, 0.05, 0.12, 0.25, 0.6, 1.5, 6.0)


def build_ramp(profile):
    """A scene with a tone ramp in it, which nothing else in the suite has.

    Eight emissive slabs stacked up the middle of the frame. The AA scene the
    other checks use is deliberately two flat levels -- that is what makes an
    edge measurable -- and two levels cannot show that an effect varies with
    brightness. A response curve that is upside down looks entirely normal on
    it.

    Kept small and central (y within +-1.2 of the middle, x running well off
    both sides) so that it does not matter whether the 60-degree field of view
    is measured vertically or horizontally, nor what aspect ratio the
    screenshot comes out at. Nothing here depends on where a slab lands: the
    check bins pixels by the luma it *measures* in the ungrained render, not
    by where it expected them to be.
    """
    ids = iter(range(1, 1 << 62))

    def next_id():
        return (next(ids) * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF

    lines = [
        "Scene: Tone ramp",
        "Version: 6",
        "Environment:",
        "  AmbientColor: [0, 0, 0]",
        "  AmbientIntensity: 0",
        "  Sky: 1",
        "  SkyHorizon: [0.05, 0.05, 0.06]",
        "  SkyZenith: [0.05, 0.05, 0.06]",
        "  SkyGround: [0.05, 0.05, 0.06]",
        "  SkyIntensity: 1",
        "  SkyRotation: 0",
        "Entities:",
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        "      Tag: Scene Camera",
        "    TransformComponent:",
        "      Position: [0, 0, 4]",
        "      Rotation: [0, 0, 0]",
        "      Scale: [1, 1, 1]",
        "    CameraComponent:",
        "      ViewRank: 0",
        "      FixedAspectRatio: false",
        "      ProjectionType: Perspective",
        "      PerspectiveFOV: 60",
        "      PerspectiveNearClip: 0.05",
        "      PerspectiveFarClip: 400",
        "      OrthographicScale: 10",
        "      OrthographicNearClip: -1",
        "      OrthographicFarClip: 1",
        f"      PostProfile: {profile}",
    ]

    # No light, and black base colour: the emissive term is the whole of each
    # slab, so its brightness is exactly the number written here.
    for i, emissive in enumerate(RAMP_EMISSIVE):
        y = (i - (len(RAMP_EMISSIVE) - 1) / 2.0) * 0.32
        lines += [
            f"  - EntityID: {next_id()}",
            "    TagComponent:",
            f"      Tag: Step {i}",
            "    TransformComponent:",
            f"      Position: [0, {y:g}, 0]",
            "      Rotation: [0, 0, 0]",
            "      Scale: [80, 0.3, 0.01]",
            "    MeshComponent:",
            f"      Mesh: {make_aa_scene.PRIMITIVE_BASE + make_aa_scene.CUBE}",
            "      Material:",
            "        BaseColor: [0, 0, 0, 1]",
            f"        Emissive: [{emissive:g}, {emissive:g}, {emissive:g}, 1]",
            "        Metallic: 0",
            "        Roughness: 1",
            "        Occlusion: 1",
        ]

    return chr(10).join(lines) + chr(10)


def luma_of(image):
    """Rec. 709 luma in 0..1, which is what the shader's response curve reads."""
    return (image * np.array([0.2126, 0.7152, 0.0722])).sum(axis=2) / 255.0


def grain_rms(delta, mask):
    """RMS of the grain over the masked pixels, across all three channels."""
    selected = delta[mask]
    return float(np.sqrt((selected.astype(np.float64) ** 2).mean()))


def shoot(exe, backend, out, scene, frame=FRAME):
    subprocess.run(
        [str(exe), "--render-defaults=on", f"--backend={backend}", "--aa=none", "--validation=on",
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

    root = ROOT
    exe = root / "build" / "bin" / args.config / "RageVRuntime" / "RageVRuntime.exe"
    if not exe.exists():
        print(f"FAIL: {exe} does not exist; build {args.config} first")
        sys.exit(1)

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

        # --- 5. clumps, and clumps of a size -------------------------------
        #
        # The grain itself, isolated: the same frame with and without it, so
        # what is left is the noise the shader added and nothing else.
        delta = (frames["grain"] - frames["off"]).astype(np.float64)

        # Only where grain is actually acting. Pairs that are both zero are
        # equal for a reason that has nothing to do with the field's shape,
        # and there are enough of them at the extremes of the tone scale to
        # swamp the measurement.
        live = (delta[:, :-1] != 0) | (delta[:, 1:] != 0)
        identical = float((delta[:, :-1][live] == delta[:, 1:][live]).mean())

        near = float(np.corrcoef(delta[:, :-1][live], delta[:, 1:][live])[0, 1])
        vertical = delta[:-1, :], delta[1:, :]
        live_v = (vertical[0] != 0) | (vertical[1] != 0)
        near_v = float(np.corrcoef(vertical[0][live_v],
                                   vertical[1][live_v])[0, 1])

        far_a, far_b = delta[:, :-4], delta[:, 4:]
        live_far = (far_a != 0) | (far_b != 0)
        far = float(np.corrcoef(far_a[live_far], far_b[live_far])[0, 1])

        print(f"  grain adjacent identical {identical * 100:.1f}%   "
              f"correlation at 1px {near:+.3f} (vertical {near_v:+.3f}), "
              f"at 4px {far:+.3f}")

        if identical > MAX_ADJACENT_IDENTICAL:
            failures.append(
                f"{backend}: {identical * 100:.0f}% of horizontally adjacent "
                f"pixels have exactly the same grain. The field is piecewise "
                f"constant -- a grid of squares rather than specks, which is "
                f"what `hash(floor(coord / size))` produces and what value "
                f"noise exists here to avoid")

        if near < MIN_NEIGHBOUR_CORRELATION:
            failures.append(
                f"{backend}: grain in adjacent pixels correlates {near:+.3f}, "
                f"so a speck is one pixel wide however the grain size is set. "
                f"That is per-pixel white noise, which reads as a broken sensor "
                f"rather than as film")

        if far > MAX_FAR_CORRELATION:
            failures.append(
                f"{backend}: grain still correlates {far:+.3f} four pixels "
                f"away at grain size 2. The clumps have no size -- the field "
                f"is closer to a blur than to a stock")

        # --- 6. and it peaks in the midtones -------------------------------
        ramp = scenes / "lens_ramp.rage"
        ramp.write_text(build_ramp(profiles["off"]))
        ramp_off = shoot(exe, backend, shots / f"{backend}-ramp-off.png",
                         f"scenes/{ramp.name}")

        ramp.write_text(build_ramp(profiles["grain"]))
        ramp_grain = shoot(exe, backend, shots / f"{backend}-ramp-grain.png",
                           f"scenes/{ramp.name}")

        # Binned by the luma of the *ungrained* render, so the bands are where
        # the renderer actually put them rather than where the scene meant to.
        luma = luma_of(ramp_off)
        ramp_delta = (ramp_grain - ramp_off).astype(np.float64)

        bands = {
            "shadow":  luma < 0.12,
            "midtone": (luma > 0.35) & (luma < 0.60),
            "highlight": luma > 0.90,
        }

        measured = {}
        for name, mask in bands.items():
            if int(mask.sum()) < 500:
                failures.append(
                    f"{backend}: the tone-ramp scene produced only "
                    f"{int(mask.sum())} {name} pixels, so the midtone claim was "
                    f"not measured. The ramp is not landing on screen, or the "
                    f"tone curve has moved out from under the emissive values "
                    f"chosen for it")
                measured[name] = None
                continue

            measured[name] = grain_rms(ramp_delta, mask)

        if all(v is not None for v in measured.values()):
            print(f"  grain by tone: shadow {measured['shadow']:.2f}   "
                  f"midtone {measured['midtone']:.2f}   "
                  f"highlight {measured['highlight']:.2f} levels rms")

            for end in ("shadow", "highlight"):
                if measured["midtone"] < measured[end] * MIDTONE_MARGIN:
                    failures.append(
                        f"{backend}: grain measures {measured[end]:.2f} levels "
                        f"in the {end}s against {measured['midtone']:.2f} in "
                        f"the midtones. Film has no variation left to show "
                        f"once nothing is exposed or everything is, so the "
                        f"amplitude has to fall off at both ends -- a curve "
                        f"that is loudest in the {end}s is the one that made "
                        f"this read as sensor noise")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: off is off to the byte, each effect lands, the vignette darkens "
          "the corner rather than the middle, the grain animates without "
          "becoming irreproducible, and it is clumps of a definite size that "
          "peak in the midtones")


if __name__ == "__main__":
    main()
