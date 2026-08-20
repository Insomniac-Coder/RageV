#!/usr/bin/env python3
"""What ENGINE-NOTES 7y has to be true for: auto exposure.

Five claims. The first is the one that protects everything else in this
repository, and the middle two are a pair for the same reason every other pair
in this suite is one.

**1. Off is exactly off.** Auto exposure defaults to false, and a profile with
it false must render the *same bytes* as one that has never heard of the
feature. That is what lets `check_smaa`, `check_color_grading`,
`check_taa_*` and `check_lens_effects` keep their recorded thresholds without a
line of change. The shader branches on the flag rather than multiplying by a
buffer that holds 1 -- the multiply happens to be exact in IEEE, but a branch
is a claim a person can check.

**2. It meters.** Two scenes, one ten times brighter than the other. With auto
exposure off they render at very different brightnesses, which is the control:
it says the two scenes really are different. With it on they land close
together, because that is the entire job.

**3. And it does not simply flatten everything.** The mirror of the above, and
the reason 2 alone is not enough: an exposure that always produced mid grey
would pass "the two scenes converge" perfectly. So the *brighter scene must
still be at least slightly brighter* -- metering is not a normaliser.

**4. It reproduces.** The adaptation is driven by the frame time the loop hands
down, which `--frame-time` already pins, so frame 30 rendered twice is the same
picture. This is the property the roadmap warned about: without it, every
screenshot comparison in this repository becomes a measurement of whatever the
adaptation happened to be doing.

**5. The manual slider still does something.** With auto exposure on, Exposure
stops being the exposure and becomes exposure *compensation*. If it did
nothing, a scene the metering reads wrong would have no fix but switching the
feature off.

Usage:
    python tools/scripts/check_auto_exposure.py [--config Release]
"""

import argparse
import math
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "scripts"))

import make_aa_scene  # noqa: E402
import postprofile  # noqa: E402

BACKENDS = ("vulkan", "opengl")

FRAME = 30

# Off has to be off to the byte. A tolerance here would accept exactly the
# silent drift this exists to prevent.
OFF_MAX_DIFF = 0

# The two scenes, as a multiplier on the emissive slab. A factor of 10 is over
# three stops, which no amount of measurement noise accounts for.
DARK = 0.06
BRIGHT = 0.6

# Uncorrected, the two must differ by at least this many levels -- the control
# that says the scenes are genuinely different before anything is claimed about
# closing the gap.
MIN_UNCORRECTED_GAP = 40.0

# Metered, they must come within this. Not zero: the metering answers with the
# scene's *average*, and these two scenes do not have the same distribution.
MAX_METERED_GAP = 12.0

# But the brighter one must still read brighter. A normaliser would collapse
# this to zero, and would pass the claim above with room to spare.
MIN_ORDER_GAP = 0.20


def build(emissive, profile):
    """One emissive slab filling the frame, at a chosen brightness.

    Deliberately not the AA scene: that one is fixed at 0.6 emissive, and the
    whole measurement here is what happens when the scene's brightness changes.
    """
    ids = iter(range(1, 1 << 62))

    def next_id():
        return (next(ids) * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF

    lines = [
        "Scene: Auto exposure",
        "Version: 6",
        "Environment:",
        "  AmbientColor: [0, 0, 0]",
        "  AmbientIntensity: 0",
        "  Sky: 1",
        # The sky scales with the slab, so the *whole frame* changes brightness
        # rather than a rectangle in the middle of a constant background --
        # otherwise the sky anchors the histogram and the metering has half a
        # scene to work from.
        f"  SkyHorizon: [{emissive * 0.1:g}, {emissive * 0.1:g}, {emissive * 0.12:g}]",
        f"  SkyZenith: [{emissive * 0.1:g}, {emissive * 0.1:g}, {emissive * 0.12:g}]",
        f"  SkyGround: [{emissive * 0.1:g}, {emissive * 0.1:g}, {emissive * 0.12:g}]",
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
        f"  - EntityID: {next_id()}",
        "    TagComponent:",
        "      Tag: Slab",
        "    TransformComponent:",
        "      Position: [0, 0, 0]",
        "      Rotation: [0, 0, 0]",
        "      Scale: [40, 1.6, 0.01]",
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

    exe = ROOT / "build" / "bin" / args.config / "RageVRuntime" / "RageVRuntime.exe"

    # The runtime loads `assets/` beside its exe, which the build copies there
    # (ENGINE-NOTES 7be, hole 3): a shader edited but not built is measured as
    # the last build's shader, and nothing says so.
    import rvcheck
    rvcheck.require_current_shaders(ROOT, args.config)
    if not exe.exists():
        print(f"FAIL: {exe} does not exist; build {args.config} first")
        sys.exit(1)

    assets = ROOT / "SampleProject" / "assets"
    scenes = assets / "scenes"
    shots = ROOT / "build" / "exposure"
    shots.mkdir(parents=True, exist_ok=True)

    # Bloom off, for the reason every other check turns it off: it moves light
    # between pixels, and this measures how much light there is.
    base = {"BloomEnabled": False}

    profiles = {
        # A profile that has never mentioned the feature, for the byte test.
        "untouched": postprofile.write_named(
            assets / "post" / "ae_untouched.rvpostprofile", dict(base)),
        "off": postprofile.write_named(
            assets / "post" / "ae_off.rvpostprofile",
            dict(base, AutoExposure=False)),
        "on": postprofile.write_named(
            assets / "post" / "ae_on.rvpostprofile",
            dict(base, AutoExposure=True)),
        # Same, plus a stop of compensation.
        "on_pushed": postprofile.write_named(
            assets / "post" / "ae_on_pushed.rvpostprofile",
            dict(base, AutoExposure=True, Exposure=2.0)),
    }

    failures = []

    def mean_of(frame):
        return float(frame.mean())

    for backend in BACKENDS:
        print(f"--- {backend}")

        def render(profile, emissive, tag, frame=FRAME):
            scene = scenes / f"ae_{tag}.rage"
            scene.write_text(build(emissive, profiles[profile]))
            return shoot(exe, backend, shots / f"{backend}-{tag}.png",
                         f"scenes/{scene.name}", frame)

        # --- 1. off is exactly off --------------------------------------------
        off_bright = render("off", BRIGHT, "off-bright")
        untouched = render("untouched", BRIGHT, "untouched-bright")

        off_diff = int(np.abs(off_bright - untouched).max())
        print(f"  auto exposure false vs never mentioned   max {off_diff}")

        if off_diff > OFF_MAX_DIFF:
            failures.append(
                f"{backend}: with auto exposure off the frame differs by "
                f"{off_diff}/255 from one whose profile does not mention it. Off "
                f"has to be off to the byte, or every recorded threshold in this "
                f"repository has moved underneath it")

        # --- 2 and 3. it meters, without flattening ----------------------------
        off_dark = render("off", DARK, "off-dark")
        on_dark = render("on", DARK, "on-dark")
        on_bright = render("on", BRIGHT, "on-bright")

        uncorrected = mean_of(off_bright) - mean_of(off_dark)
        metered = mean_of(on_bright) - mean_of(on_dark)

        print(f"  uncorrected the two scenes differ by {uncorrected:6.1f} levels")
        print(f"  metered                              {metered:6.1f} levels")

        if uncorrected < MIN_UNCORRECTED_GAP:
            failures.append(
                f"{backend}: with auto exposure off the dark and bright scenes "
                f"differ by only {uncorrected:.1f} levels. The control is broken "
                f"-- the two scenes are not actually different, so nothing below "
                f"can be concluded from them converging")

        if abs(metered) > MAX_METERED_GAP:
            failures.append(
                f"{backend}: metered, the dark and bright scenes still differ by "
                f"{metered:.1f} levels against {uncorrected:.1f} uncorrected. Auto "
                f"exposure is not reaching the tone mapping pass, or the histogram "
                f"is not measuring the scene it was handed")

        if metered < MIN_ORDER_GAP:
            failures.append(
                f"{backend}: metered, the brighter scene is not brighter "
                f"({metered:+.2f} levels). Auto exposure has become a normaliser "
                f"-- every scene rendered to the same average is not exposure, it "
                f"is the loss of the thing exposure is for")

        # --- 4. and it reproduces ---------------------------------------------
        again = render("on", BRIGHT, "on-bright-again")
        repeat = int(np.abs(on_bright - again).max())
        print(f"  frame {FRAME} twice with metering: max {repeat}")

        if repeat > 0:
            failures.append(
                f"{backend}: two renders of frame {FRAME} with auto exposure "
                f"differ by {repeat}/255. The adaptation is reading something "
                f"other than the frame time the loop hands down -- a clock, most "
                f"likely -- which makes every screenshot comparison in this "
                f"repository unreliable the moment metering is switched on")

        # --- 5. the slider is compensation, not decoration ---------------------
        pushed = render("on_pushed", BRIGHT, "on-pushed")
        lift = mean_of(pushed) - mean_of(on_bright)
        print(f"  a stop of compensation lifts it by {lift:.1f} levels")

        if lift <= 1.0:
            failures.append(
                f"{backend}: with auto exposure on, doubling Exposure changed the "
                f"picture by {lift:.1f} levels. The manual slider is meant to "
                f"become exposure compensation, not to stop working -- a scene "
                f"the metering reads wrong would have no fix but turning the "
                f"feature off")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: off is off to the byte, metering closes a three-stop gap without "
          "flattening it, a metered frame reproduces, and the manual slider "
          "still compensates")


if __name__ == "__main__":
    main()
