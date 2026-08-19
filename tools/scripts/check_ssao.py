#!/usr/bin/env python3
"""SSAO (9.6), measured for restraint as much as for darkness.

On the box-on-a-floor fixture, both backends:

1. **Off is off, to the byte** -- a profile saying AmbientOcclusion: false
   renders the same bytes as one that never mentions it.
2. **The contact seam darkens.** The band where the box meets the floor
   loses brightness with AO on; that is the effect existing at all.
3. **The open floor barely moves.** A wide flat plane far from any geometry
   has nothing occluding it, and a kernel that darkens it anyway is
   self-occluding out of its own depth quantisation -- the artefact that
   makes SSAO read as dirt. Held to a fraction of a level, not to zero: the
   half-resolution upsample is allowed its rounding.
4. **Intensity is monotonic and one-sided**: doubling it deepens the seam
   and still leaves the open floor alone.
5. **A frame reproduces** -- the kernel rotation is seeded by pixel, never
   by frame. ENGINE-NOTES 7ac.

And on a normal-mapped brick wall seen along its length with nothing near
it (9.8b):

6. **A bump the depth buffer does not have must not occlude.** The wall's
   shading normal tilts along every mortar line; its depth is flat. The AO
   factor over the open wall must hold at one -- which the geometric normal
   does and the shading normal, taken wherever it faces the camera, does
   not (0.954 at worst on this fixture). ENGINE-NOTES 7ae.
7. **And it holds under the temporal jitter.** With TAA on, eight
   consecutive frames -- one full jitter phase -- of the same wall: the AO
   factor over the open wall stays at one in every one of them, and the
   wall's brightness moves by less than a level across the eight. This is
   the claim the editor viewport failed before the reconstructed normal was
   faced against the ray to the point rather than against the view axis: a
   wall seen along its length has a normal with almost no z, the sign of
   that z was noise, the noise turned with the jitter, and the wall went a
   third darker every eighth frame. ENGINE-NOTES 7an.

And with ray tracing on (8.12 stage 3, Vulkan only; ENGINE-NOTES 7ao),
the same claims of the traced form, `--rt-ao=on`, with the profile's own
AmbientOcclusion left *off* -- the render setting alone drives it:

8. **The seam darkens** under ray-traced occlusion, at least as much as the
   restrained bound asks of SSAO.
9. **The open floor holds still**, and **the brick wall holds at one** --
   a ray has no reconstruction to wobble and no normal map to be misled by.
   Held to a tighter floor than the screen-space form's, because it reaches a
   better number and its own guard is worth guarding: CHK.2 removed that
   guard and read 0.996 against a floor of 0.995 (ENGINE-NOTES 7ba).
10. **And it holds under the temporal jitter** across the eight-frame phase.

Falsified by giving the rays no length (tMax zero): nothing darkens.

Usage:
    python tools/scripts/check_ssao.py [--config Release]
"""

import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

BACKENDS = ("vulkan", "opengl")
FRAME = 30

# How much the seam must darken at intensity 1, in mean 8-bit levels, and
# how still the open floor must hold. **A band** (ENGINE-NOTES 7ba): the first
# version set a floor of 3.0 and called it "deliberately modest -- a restrained
# AO is the point", against a measured 13.65 -- so an occlusion at a quarter
# of its strength, or one that had quietly become a contact shadow at three
# times it, both passed. Measured 13.65 / 13.60 screen-space and 15.17 traced.
# The open-floor bound is the check's entire reason to exist and is unchanged.
MIN_SEAM_DARKENING = 9.0
MAX_SEAM_DARKENING = 22.0
MAX_OPEN_DRIFT = 1.0
# The AO factor (on / off) over the open brick wall: its tenth percentile
# and its worst texel. The geometric normal gives 1.000 and 0.984; the
# shading normal, taken wherever it faces the camera, 0.992 and 0.954.
MIN_WALL_P10 = 0.995
MIN_WALL_FLOOR = 0.97
# **And a tighter one for the traced form** (CHK.2, ENGINE-NOTES 7ba). Rays
# read 1.000 here, and the kernel's guard -- a ray under the slope the depth
# buffer implies is not cast -- is what holds them there. Removing that guard
# reads 0.996, which passed the 0.995 above by a thousandth: a claim that
# survives its own documented defect by rounding is not a claim. 0.998 sits
# between the two with room either side.
MIN_TRACED_WALL_P10 = 0.998
# Under TAA, across one full jitter phase: how far the open wall's mean
# brightness may move between the brightest and the darkest of the eight
# frames, in 8-bit levels. The broken sign gave 37 levels; the fixed one
# gives well under one.
JITTER_PHASE = 8
MAX_WALL_JITTER_DRIFT = 1.0


def run(exe, args):
    result = subprocess.run([str(exe), "--render-defaults=on", *args], cwd=exe.parent,
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAIL: {' '.join(args)} exited {result.returncode}")
        print(result.stdout[-2000:], result.stderr[-2000:])
        sys.exit(1)


RAY_FLAGS = ("--raytracing=on", "--rt-ao=on")


def shoot(exe, backend, path, scene="scenes/ssao_box.rage", extra=()):
    # `--raytracing=off` is pinned *before* `extra`, so the screen-space
    # claims measure the screen-space effect whatever the project file holds,
    # and a claim that wants the traced twin says `--raytracing=on` in `extra`
    # and wins because it comes later. This check inherited the project's
    # Render Settings until 2026-08-18, when a `SampleProject.rvproject` with
    # every ray-tracing switch on was committed by accident and every
    # screen-space claim here read 0.00 -- on Vulkan only, because OpenGL has
    # no rays to switch to. A check that depends on ambient state is a check
    # that fails for reasons outside its own claim.
    run(exe, [f"--rhi={backend}", f"--scene={scene}",
              "--frame-time=0.0166", f"--screenshot-frame={FRAME}",
              f"--screenshot={path}", "--aa=none", "--raytracing=off", *extra])
    if not pathlib.Path(path).exists():
        print(f"FAIL: {path} was never written -- the scene probably did not load")
        sys.exit(1)
    return np.asarray(Image.open(path).convert("RGB")).astype(float).mean(axis=2)


def shoot_sequence(exe, backend, path, scene, count, extra=()):
    """`count` consecutive frames from FRAME under TAA, in one run: separate
    runs are separate clocks and would not be consecutive. Returns them in
    frame order."""
    path = pathlib.Path(path)
    run(exe, [f"--rhi={backend}", f"--scene={scene}",
              "--frame-time=0.0166", f"--screenshot-frame={FRAME}",
              f"--screenshot-count={count}", f"--screenshot={path}", "--aa=taa", "--raytracing=off", *extra])
    frames = []
    for frame in range(FRAME, FRAME + count):
        numbered = path.with_name(f"{path.stem}_{frame}{path.suffix}")
        if not numbered.exists():
            print(f"FAIL: {numbered} was never written")
            sys.exit(1)
        frames.append(np.asarray(Image.open(numbered).convert("RGB")).astype(float).mean(axis=2))
    return frames


def regions(image):
    """The seam band and the open floor, as frame fractions.

    Fractions rather than found: the camera and the box are pinned by the
    fixture and --frame-time, so the geometry cannot move between runs --
    and locating the seam by looking for darkening would be asking the
    effect under test to define its own measurement.
    """
    height, width = image.shape
    # The box sits centre frame; its base line crosses just below the
    # middle. A band around it, box-wide.
    seam = (slice(int(height * 0.56), int(height * 0.66)),
            slice(int(width * 0.34), int(width * 0.66)))
    # The lower-left floor, far from every silhouette and inside the frame.
    open_ = (slice(int(height * 0.78), int(height * 0.95)),
             slice(int(width * 0.06), int(width * 0.28)))
    return seam, open_


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
    import make_ssao_scene
    import postprofile

    scenes = root / "SampleProject" / "assets" / "scenes"
    shots = root / "build" / "ssao"
    shots.mkdir(parents=True, exist_ok=True)

    scene = scenes / "ssao_box.rage"

    def profile(settings):
        handle = postprofile.write_beside(scene, { "BloomEnabled": False, **settings })
        scene.write_text(make_ssao_scene.build(handle))

    failures = []

    for backend in BACKENDS:
        profile({})
        absent = shoot(exe, backend, shots / f"{backend}-absent.png")
        profile({ "AmbientOcclusion": False })
        off = shoot(exe, backend, shots / f"{backend}-off.png")

        delta = int(np.abs(absent - off).max())
        print(f"{backend}: off vs never mentioned   max {delta}")
        if delta != 0:
            failures.append(f"{backend}: AmbientOcclusion: false changed the image "
                            f"(max {delta})")

        seam, open_ = regions(off)

        profile({ "AmbientOcclusion": True, "AoIntensity": 1.0 })
        on = shoot(exe, backend, shots / f"{backend}-on.png")
        profile({ "AmbientOcclusion": True, "AoIntensity": 2.0 })
        strong = shoot(exe, backend, shots / f"{backend}-strong.png")

        seam_1 = off[seam].mean() - on[seam].mean()
        seam_2 = off[seam].mean() - strong[seam].mean()
        open_1 = abs(on[open_].mean() - off[open_].mean())
        open_2 = abs(strong[open_].mean() - off[open_].mean())

        print(f"{backend}: seam darkens by {seam_1:.2f} at 1.0, {seam_2:.2f} at 2.0")
        print(f"{backend}: open floor drifts {open_1:.3f} at 1.0, {open_2:.3f} at 2.0")

        if seam_1 < MIN_SEAM_DARKENING:
            failures.append(f"{backend}: the contact seam darkened by only "
                            f"{seam_1:.2f} levels")
        if seam_1 > MAX_SEAM_DARKENING:
            failures.append(f"{backend}: the contact seam darkened by {seam_1:.2f} levels "
                            f"(ceiling {MAX_SEAM_DARKENING}) -- an occlusion this strong "
                            f"is a contact shadow, not ambient occlusion")
        if seam_2 <= seam_1:
            failures.append(f"{backend}: doubling the intensity did not deepen "
                            f"the seam ({seam_1:.2f} -> {seam_2:.2f})")
        if open_1 > MAX_OPEN_DRIFT or open_2 > MAX_OPEN_DRIFT:
            failures.append(f"{backend}: the open floor moved "
                            f"({open_1:.3f} / {open_2:.3f} levels) -- "
                            "self-occlusion or a halo")

        profile({ "AmbientOcclusion": True, "AoIntensity": 1.0 })
        again = shoot(exe, backend, shots / f"{backend}-again.png")
        repeat = int(np.abs(on - again).max())
        print(f"{backend}: frame {FRAME} twice       max {repeat}")
        if repeat != 0:
            failures.append(f"{backend}: the same frame rendered differently twice "
                            f"(max {repeat})")

    # --- the brick wall: a normal map over flat depth ------------------------
    wall = scenes / "ssao_wall.rage"
    material = make_ssao_scene.wall_material_handle(root)

    def wall_profile(settings):
        handle = postprofile.write_beside(wall, { "BloomEnabled": False, **settings })
        wall.write_text(make_ssao_scene.build_wall(handle, material))

    for backend in BACKENDS:
        wall_profile({ "AmbientOcclusion": False })
        off = shoot(exe, backend, shots / f"{backend}-wall-off.png",
                    scene="scenes/ssao_wall.rage")
        wall_profile({ "AmbientOcclusion": True, "AoIntensity": 1.0 })
        on = shoot(exe, backend, shots / f"{backend}-wall-on.png",
                   scene="scenes/ssao_wall.rage")

        # The AO factor, not the difference: the wall is textured, and a
        # difference scales with the bricks. Well inside the wall's wedge.
        height, width = off.shape
        region = (slice(int(height * 0.28), int(height * 0.72)),
                  slice(int(width * 0.02), int(width * 0.25)))
        factor = on[region] / np.maximum(off[region], 1.0)
        p10 = float(np.percentile(factor, 10))
        floor = float(factor.min())
        print(f"{backend}: open brick wall AO factor mean {factor.mean():.3f}, "
              f"p10 {p10:.3f}, worst {floor:.3f}")
        if p10 < MIN_WALL_P10 or floor < MIN_WALL_FLOOR:
            failures.append(f"{backend}: the open brick wall occludes itself "
                            f"(AO factor p10 {p10:.3f}, worst {floor:.3f}) -- "
                            "a normal-map bump the depth buffer does not have")

        # The same wall through one full jitter phase under TAA. The AO-off
        # frames are the reference for their own frame, so TAA's own
        # convergence is not what is being measured.
        wall_profile({ "AmbientOcclusion": False })
        off_frames = shoot_sequence(exe, backend, shots / f"{backend}-wall-taa-off.png",
                                    "scenes/ssao_wall.rage", JITTER_PHASE)
        wall_profile({ "AmbientOcclusion": True, "AoIntensity": 1.0 })
        on_frames = shoot_sequence(exe, backend, shots / f"{backend}-wall-taa-on.png",
                                   "scenes/ssao_wall.rage", JITTER_PHASE)
        p10s = []
        means = []
        for off_frame, on_frame in zip(off_frames, on_frames):
            factor = on_frame[region] / np.maximum(off_frame[region], 1.0)
            p10s.append(float(np.percentile(factor, 10)))
            means.append(float(on_frame[region].mean()))
        drift = max(means) - min(means)
        print(f"{backend}: under TAA across {JITTER_PHASE} frames the wall's AO factor p10 "
              f"is {min(p10s):.3f} at worst and its brightness drifts {drift:.2f} levels")
        if min(p10s) < MIN_WALL_P10:
            failures.append(f"{backend}: under TAA the open brick wall occludes itself on "
                            f"some jitter offset (AO factor p10 {min(p10s):.3f} at worst)")
        if drift > MAX_WALL_JITTER_DRIFT:
            failures.append(f"{backend}: under TAA the open brick wall's brightness moves "
                            f"{drift:.2f} levels across the jitter phase -- it flickers")

    # --- ray-traced occlusion (7ao), where the device can ---------------------
    probe_run = subprocess.run([str(exe), "--render-defaults=on", "--rhi=vulkan", "--scene=scenes/ssao_box.rage",
                                "--frame-time=0.0166", "--screenshot-frame=2",
                                f"--screenshot={shots / 'vulkan-rt-probe.png'}", "--aa=none",
                                *RAY_FLAGS], cwd=exe.parent, capture_output=True, text=True)
    can_trace = "shadows traced" in (probe_run.stdout + probe_run.stderr)
    if not can_trace:
        print("vulkan: this device does not trace; the ray-traced occlusion claims are skipped")
    else:
        # The profile says off throughout: the render setting is what turns
        # the traced form on, and the profile's toggle is not consulted.
        profile({ "AmbientOcclusion": False, "AoIntensity": 1.0 })
        off = shoot(exe, "vulkan", shots / "vulkan-rt-off.png")
        rt_on = shoot(exe, "vulkan", shots / "vulkan-rt-on.png", extra=RAY_FLAGS)
        seam, open_ = regions(off)
        rt_seam = off[seam].mean() - rt_on[seam].mean()
        rt_open = abs(rt_on[open_].mean() - off[open_].mean())
        print(f"vulkan: ray-traced occlusion darkens the seam by {rt_seam:.2f}, "
              f"open floor drifts {rt_open:.3f}")
        if not MIN_SEAM_DARKENING <= rt_seam <= MAX_SEAM_DARKENING:
            failures.append(f"vulkan: ray-traced occlusion darkened the seam by {rt_seam:.2f} "
                            f"(band {MIN_SEAM_DARKENING} to {MAX_SEAM_DARKENING})")
        if rt_open > MAX_OPEN_DRIFT:
            failures.append(f"vulkan: ray-traced occlusion moved the open floor by {rt_open:.3f}")

        wall_profile({ "AmbientOcclusion": False, "AoIntensity": 1.0 })
        wall_off = shoot(exe, "vulkan", shots / "vulkan-wall-rt-off.png",
                         scene="scenes/ssao_wall.rage")
        wall_on = shoot(exe, "vulkan", shots / "vulkan-wall-rt-on.png",
                        scene="scenes/ssao_wall.rage", extra=RAY_FLAGS)
        height, width = wall_off.shape
        region = (slice(int(height * 0.28), int(height * 0.72)),
                  slice(int(width * 0.02), int(width * 0.25)))
        factor = wall_on[region] / np.maximum(wall_off[region], 1.0)
        p10 = float(np.percentile(factor, 10))
        print(f"vulkan: ray-traced occlusion on the open brick wall: AO factor mean {factor.mean():.3f}, "
              f"p10 {p10:.3f}, worst {float(factor.min()):.3f}")
        if p10 < MIN_TRACED_WALL_P10:
            failures.append(f"vulkan: under ray-traced occlusion the open brick wall occludes "
                            f"itself (AO factor p10 {p10:.3f}, floor {MIN_TRACED_WALL_P10})")

        off_frames = shoot_sequence(exe, "vulkan", shots / "vulkan-wall-rt-taa-off.png",
                                    "scenes/ssao_wall.rage", JITTER_PHASE)
        on_frames = shoot_sequence(exe, "vulkan", shots / "vulkan-wall-rt-taa-on.png",
                                   "scenes/ssao_wall.rage", JITTER_PHASE, extra=RAY_FLAGS)
        means = [float(on_frame[region].mean()) for on_frame in on_frames]
        p10s = [float(np.percentile(on_frame[region] / np.maximum(off_frame[region], 1.0), 10))
                for off_frame, on_frame in zip(off_frames, on_frames)]
        drift = max(means) - min(means)
        print(f"vulkan: ray-traced occlusion under TAA across {JITTER_PHASE} frames: wall AO p10 "
              f"{min(p10s):.3f} at worst, brightness drifts {drift:.2f} levels")
        if min(p10s) < MIN_TRACED_WALL_P10 or drift > MAX_WALL_JITTER_DRIFT:
            failures.append(f"vulkan: ray-traced occlusion on the wall is not steady across the "
                            f"jitter phase (p10 {min(p10s):.3f}, drift {drift:.2f})")

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: off is off to the byte, the seam darkens and deepens with "
          "intensity, the open floor holds still, a frame reproduces, and a "
          "normal-mapped wall does not occlude itself -- on any jitter offset, "
          "and under ray-traced occlusion too")


if __name__ == "__main__":
    main()
