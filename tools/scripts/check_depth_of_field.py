#!/usr/bin/env python3
"""What ENGINE-NOTES 7z has to be true for: depth of field.

The scene is two groups of thin bars, one near and one far, scaled so they
subtend the *same angle* -- they occupy the same area of screen at the same
spatial frequency, so the only thing that differs between them is distance.
Without that, "the far one is blurrier" would be measuring perspective.

Five claims, and the second and third are a pair for the reason every pair in
this suite is one.

**1. Off is exactly off.** No passes are added, so a profile with depth of
field false must render the same bytes as one that has never heard of it.

**2. Focus sharpens, distance blurs.** With the focus on the near group, the
far group loses high-frequency detail and the near group keeps it.

**3. And it follows the focus, rather than just blurring the distance.** Move
the focus to the far group and the roles *reverse*. Claim 2 alone is passed
perfectly by a shader that blurs everything past a fixed distance and has
never read the focus setting -- which is a fog, not a lens.

**4. The aperture does what an aperture does.** f/16 blurs measurably less
than f/1.4. This is the claim that says the thin-lens maths is wired up rather
than a blur radius with a physical-sounding name.

**2b. And blurs by the right amount, not merely enough.** The far group under
near focus is banded above as well as below (57.5% measured, ceiling 66%), and
f/16 -- sub-pixel on this fixture -- must blur nothing. Both ceilings were
placed against a circle of confusion scaled three and ten times, and the
far-focus direction is *not* bounded, because it sits at the MaxRadius clamp
already and cannot tell a correct blur from a doubled one. ENGINE-NOTES 7ba.
An observation, not a claim: the in-focus group reads *more* detail with the
effect on than off (115%), which nobody has explained; it is not bounded
because a bound placed without understanding is a coin toss.

**5. The near group does not lose its edges.** The artefact that makes depth
of field read as a filter is a blurred background bleeding over a sharp
foreground: the subject grows a halo of the wall behind it. So the in-focus
group must stay within a few percent of the sharpness it has with the whole
effect switched off.

**6. And none of that depends on the anti-aliasing.** Claims 2 to 5 all read
the scene's depth, and MSAA and SSAA are the two modes that change what the
depth *is* -- multisampled for the first, larger than the frame for the
second. For a whole roadmap phase this check ran only at `--aa=none`, and MSAA
was meanwhile binding a 4x depth image to a `sampler2D`: every pixel came back
as the near plane and the entire frame was blurred edge to edge. Nothing here
noticed, because nothing here had ever rendered a frame with anti-aliasing on.
ENGINE-NOTES 7ai.

Usage:
    python tools/scripts/check_depth_of_field.py [--config Release]
"""

import argparse
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

FRAME = 8

OFF_MAX_DIFF = 0

# The camera sits at z = 4 looking down -Z, so these are 4 m and 30 m away.
NEAR_Z = 0.0
FAR_Z = -26.0

# Scaled so the far group subtends the same angle as the near one, which is
# what makes their sharpness comparable at all.
FAR_SCALE = 30.0 / 4.0

# Blur has to remove at least this fraction of the detail to count as blur.
# Measured at f/1.4 it removes far more; the margin is against noise, not
# against a close call.
MIN_BLUR_DROP = 0.35

# **And no more than this**, on the one reading that can tell (ENGINE-NOTES
# 7ba). Focused near, the far group loses 57.5% at f/1.4; a circle of confusion
# three times too large takes that to 74.5%, and ten times too large reads the
# same 74.5% because MaxRadius clamps it there. So the ceiling sits between the
# measurement and the saturation and catches a scale error of about two and up.
#
# The other direction cannot be bounded and does not pretend to be: focused
# far, the near group is already at the clamp (72.4% correct, 76.8% at 3x), so
# a ceiling there would be either useless or a coin toss. Stated rather than
# hidden -- the reading below the ceiling is the near-focus one only.
MAX_BLUR_DROP_FAR = 0.66

# f/16 is sub-pixel on this fixture -- a 50 mm lens focused at 4 m puts a
# 30 m subject at a few microns on the sensor -- so it must blur *nothing*,
# not merely less than f/1.4 (which claim 4 already says). Measured -0.1%;
# a CoC three times too large reads 0.2% and ten times reads 48.8%, so this
# catches the gross error the near-focus ceiling saturates on.
MAX_STOPPED_DROP = 0.10

# And the in-focus group must keep this much of its unblurred detail. Bleeding
# from behind shows up here first, as a foreground that went soft without
# anybody asking it to.
MIN_SHARP_KEPT = 0.92


def build(focus_near, aperture, dof, profile):
    """Two groups of thin bars, near and far, at matched angular size."""
    ids = iter(range(1, 1 << 62))

    def next_id():
        return (next(ids) * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF

    lines = [
        "Scene: Depth of field",
        "Version: 6",
        "Environment:",
        "  AmbientColor: [0, 0, 0]",
        "  AmbientIntensity: 0",
        "  Sky: 1",
        "  SkyHorizon: [0.02, 0.02, 0.025]",
        "  SkyZenith: [0.02, 0.02, 0.025]",
        "  SkyGround: [0.02, 0.02, 0.025]",
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

    # Left half near, right half far. Eight bars each, thin enough that a blur
    # of a few pixels visibly fills the gaps between them.
    for group, (z, scale, centre) in enumerate((
            (NEAR_Z, 1.0, -1.1),
            (FAR_Z, FAR_SCALE, 1.1))):
        for i in range(8):
            x = (centre + (i - 3.5) * 0.16) * scale
            lines += [
                f"  - EntityID: {next_id()}",
                "    TagComponent:",
                f"      Tag: Bar {group}-{i}",
                "    TransformComponent:",
                f"      Position: [{x:g}, 0, {z:g}]",
                "      Rotation: [0, 0, 0]",
                f"      Scale: [{0.05 * scale:g}, {3.0 * scale:g}, 0.01]",
                "    MeshComponent:",
                f"      Mesh: {make_aa_scene.PRIMITIVE_BASE + make_aa_scene.CUBE}",
                "      Material:",
                "        BaseColor: [0, 0, 0, 1]",
                "        Emissive: [0.7, 0.7, 0.7, 1]",
                "        Metallic: 0",
                "        Roughness: 1",
                "        Occlusion: 1",
            ]

    return chr(10).join(lines) + chr(10)


def shoot(exe, backend, out, scene, frame=FRAME, aa="none"):
    extra = ["--msaa=4"] if aa == "msaa" else []
    subprocess.run(
        [str(exe), "--render-defaults=on", f"--backend={backend}", f"--aa={aa}", "--validation=on",
         f"--scene={scene}", f"--screenshot={out}", f"--screenshot-frame={frame}",
         "--frame-time=0.016666"] + extra,
        check=True, capture_output=True, cwd=exe.parent)

    if not pathlib.Path(out).exists():
        print(f"FAIL: {backend} wrote no screenshot for {scene}")
        sys.exit(1)

    return np.asarray(Image.open(out).convert("RGB")).astype(np.int16)


def detail(frame, half):
    """How much high-frequency detail a half of the image holds.

    The mean absolute horizontal gradient. The bars are vertical, so blurring
    them fills the gaps and this falls -- it is the most direct measurement of
    "the edges went away" available without deciding where the edges were.
    """
    region = frame[:, half].astype(np.float64).mean(axis=2)
    return float(np.abs(np.diff(region, axis=1)).mean())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()

    exe = ROOT / "build" / "bin" / args.config / "RageVRuntime" / "RageVRuntime.exe"
    if not exe.exists():
        print(f"FAIL: {exe} does not exist; build {args.config} first")
        sys.exit(1)

    assets = ROOT / "SampleProject" / "assets"
    scenes = assets / "scenes"
    shots = ROOT / "build" / "dof"
    shots.mkdir(parents=True, exist_ok=True)

    base = {"BloomEnabled": False}

    profiles = {
        "untouched": postprofile.write_named(
            assets / "post" / "dof_untouched.rvpostprofile", dict(base)),
        "off": postprofile.write_named(
            assets / "post" / "dof_off.rvpostprofile",
            dict(base, DepthOfField=False)),
        # Focused on the near group, wide open.
        "near": postprofile.write_named(
            assets / "post" / "dof_near.rvpostprofile",
            dict(base, DepthOfField=True, FocusDistance=4.0,
                 FocalLength=50.0, Aperture=1.4)),
        # Same, focused on the far group.
        "far": postprofile.write_named(
            assets / "post" / "dof_far.rvpostprofile",
            dict(base, DepthOfField=True, FocusDistance=30.0,
                 FocalLength=50.0, Aperture=1.4)),
        # And stopped down, which should barely blur anything.
        "stopped": postprofile.write_named(
            assets / "post" / "dof_stopped.rvpostprofile",
            dict(base, DepthOfField=True, FocusDistance=4.0,
                 FocalLength=50.0, Aperture=16.0)),
    }

    failures = []

    for backend in BACKENDS:
        print(f"--- {backend}")

        # The scene is named by the *profile* and the screenshot by the tag:
        # the anti-aliasing runs below render the same two scenes again with a
        # different flag, and a second copy of an identical fixture on disk
        # would be two files to keep in step for no reason.
        def render(profile, tag, aa="none"):
            scene = scenes / f"dof_{profile}.rage"
            scene.write_text(build(True, 1.4, True, profiles[profile]))
            return shoot(exe, backend, shots / f"{backend}-{tag}.png",
                         f"scenes/{scene.name}", aa=aa)

        off = render("off", "off")
        untouched = render("untouched", "untouched")

        width = off.shape[1]
        near_half = np.s_[: width // 2]
        far_half = np.s_[width // 2:]

        # --- 1. off is exactly off --------------------------------------------
        off_diff = int(np.abs(off - untouched).max())
        print(f"  depth of field false vs never mentioned   max {off_diff}")

        if off_diff > OFF_MAX_DIFF:
            failures.append(
                f"{backend}: with depth of field off the frame differs by "
                f"{off_diff}/255 from one whose profile does not mention it. Off "
                f"has to be off to the byte, or every recorded threshold in this "
                f"repository has moved underneath it")

        sharp_near = detail(off, near_half)
        sharp_far = detail(off, far_half)

        # The two groups must start comparable, or nothing below means much.
        ratio = sharp_far / max(sharp_near, 1e-6)
        print(f"  unblurred detail  near {sharp_near:.3f}  far {sharp_far:.3f}"
              f"  (ratio {ratio:.2f})")

        if not (0.4 < ratio < 2.5):
            failures.append(
                f"{backend}: unblurred, the near and far groups differ in detail "
                f"by {ratio:.2f}x before anything is defocused. They are not "
                f"matched in angular size, so a difference after blurring would "
                f"be measuring perspective rather than focus")

        # --- 2 and 3. it follows the focus ------------------------------------
        near_focus = render("near", "near")
        far_focus = render("far", "far")

        kept_near = detail(near_focus, near_half) / max(sharp_near, 1e-6)
        lost_far = 1.0 - detail(near_focus, far_half) / max(sharp_far, 1e-6)
        print(f"  focused near: near keeps {kept_near * 100:5.1f}%,"
              f"  far loses {lost_far * 100:5.1f}%")

        kept_far = detail(far_focus, far_half) / max(sharp_far, 1e-6)
        lost_near = 1.0 - detail(far_focus, near_half) / max(sharp_near, 1e-6)
        print(f"  focused far:  far keeps  {kept_far * 100:5.1f}%,"
              f"  near loses {lost_near * 100:5.1f}%")

        if lost_far < MIN_BLUR_DROP:
            failures.append(
                f"{backend}: focused on the near group, the far group kept "
                f"{(1 - lost_far) * 100:.0f}% of its detail. Nothing is being "
                f"defocused -- the circle of confusion is not reaching the "
                f"gather, or it is being computed as zero")
        if lost_far > MAX_BLUR_DROP_FAR:
            failures.append(
                f"{backend}: focused on the near group, the far group lost "
                f"{lost_far * 100:.0f}% of its detail (ceiling "
                f"{MAX_BLUR_DROP_FAR * 100:.0f}%). The circle of confusion is "
                f"too large by two or more -- a unit or a scale, most likely, "
                f"since the aperture and focus were right when this was set")

        if lost_near < MIN_BLUR_DROP:
            failures.append(
                f"{backend}: focused on the *far* group, the near group kept "
                f"{(1 - lost_near) * 100:.0f}% of its detail. The blur is not "
                f"following the focus distance -- which is a fog rather than a "
                f"lens, and passes the previous claim perfectly")

        # --- 5. and the in-focus group keeps its edges ------------------------
        for name, kept in (("near", kept_near), ("far", kept_far)):
            if kept < MIN_SHARP_KEPT:
                failures.append(
                    f"{backend}: with the {name} group in focus it kept only "
                    f"{kept * 100:.0f}% of its unblurred detail. Something is "
                    f"softening what is supposed to be sharp -- most likely the "
                    f"background bleeding over it, which is the artefact the "
                    f"per-tap circle-of-confusion test exists to prevent")

        # --- 4. the aperture is an aperture -----------------------------------
        stopped = render("stopped", "stopped")
        lost_stopped = 1.0 - detail(stopped, far_half) / max(sharp_far, 1e-6)
        print(f"  stopped down to f/16: far loses {lost_stopped * 100:5.1f}%")

        if lost_stopped >= lost_far:
            failures.append(
                f"{backend}: f/16 blurred the background as much as f/1.4 "
                f"({lost_stopped * 100:.0f}% against {lost_far * 100:.0f}%). The "
                f"aperture is not reaching the circle of confusion, so the "
                f"thin-lens maths has a physical-sounding name and no physics")
        if lost_stopped > MAX_STOPPED_DROP:
            failures.append(
                f"{backend}: f/16 blurred the background by "
                f"{lost_stopped * 100:.0f}% (ceiling {MAX_STOPPED_DROP * 100:.0f}%) "
                f"on a fixture where its circle of confusion is sub-pixel -- the "
                f"circle is grossly too large, and the near-focus ceiling cannot "
                f"see it because MaxRadius saturates there first")

        # --- 6. and none of it depends on the anti-aliasing -------------------
        #
        # Each mode is measured against its *own* unblurred frame: MSAA and
        # SSAA both change the detail of an undefocused edge, and comparing
        # against the --aa=none baseline would be measuring the filter rather
        # than the lens.
        for aa in ("msaa", "ssaa"):
            off_aa = render("off", f"off-{aa}", aa)
            near_aa = render("near", f"near-{aa}", aa)

            base_near = detail(off_aa, near_half)
            base_far = detail(off_aa, far_half)

            kept = detail(near_aa, near_half) / max(base_near, 1e-6)
            lost = 1.0 - detail(near_aa, far_half) / max(base_far, 1e-6)
            print(f"  --aa={aa:5s} focused near: near keeps {kept * 100:5.1f}%,"
                  f"  far loses {lost * 100:5.1f}%")

            if kept < MIN_SHARP_KEPT:
                failures.append(
                    f"{backend}: with --aa={aa} the in-focus group kept only "
                    f"{kept * 100:.0f}% of its detail, against "
                    f"{kept_near * 100:.0f}% with anti-aliasing off. The lens is "
                    f"reading a depth that is not the scene's -- under MSAA that "
                    f"is a multisampled attachment handed to a sampler2D, which "
                    f"reads as the near plane everywhere and blurs the whole "
                    f"frame")

            if lost > MAX_BLUR_DROP_FAR:
                failures.append(
                    f"{backend}: with --aa={aa} the background lost "
                    f"{lost * 100:.0f}% (ceiling {MAX_BLUR_DROP_FAR * 100:.0f}%) -- "
                    f"the circle of confusion is too large under this mode")
            if lost < MIN_BLUR_DROP:
                failures.append(
                    f"{backend}: with --aa={aa} the background kept "
                    f"{(1 - lost) * 100:.0f}% of its detail with the focus on the "
                    f"near group. The circle of confusion is coming out near zero, "
                    f"so the depth this mode hands the lens is not a distance")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: off is off to the byte, the blur follows the focus distance "
          "rather than the distance, the aperture changes it, what is in "
          "focus keeps its edges, and none of that moves when the "
          "anti-aliasing does")


if __name__ == "__main__":
    main()
