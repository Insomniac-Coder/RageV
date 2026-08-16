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
# how still the open floor must hold. The seam number is deliberately modest
# -- a restrained AO is the point -- and the open bound is the check's
# entire reason to exist.
MIN_SEAM_DARKENING = 3.0
MAX_OPEN_DRIFT = 1.0
# The AO factor (on / off) over the open brick wall: its tenth percentile
# and its worst texel. The geometric normal gives 1.000 and 0.984; the
# shading normal, taken wherever it faces the camera, 0.992 and 0.954.
MIN_WALL_P10 = 0.995
MIN_WALL_FLOOR = 0.97
# Under TAA, across one full jitter phase: how far the open wall's mean
# brightness may move between the brightest and the darkest of the eight
# frames, in 8-bit levels. The broken sign gave 37 levels; the fixed one
# gives well under one.
JITTER_PHASE = 8
MAX_WALL_JITTER_DRIFT = 1.0


def run(exe, args):
    result = subprocess.run([str(exe), *args], cwd=exe.parent,
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAIL: {' '.join(args)} exited {result.returncode}")
        print(result.stdout[-2000:], result.stderr[-2000:])
        sys.exit(1)


def shoot(exe, backend, path, scene="scenes/ssao_box.rage"):
    run(exe, [f"--rhi={backend}", f"--scene={scene}",
              "--frame-time=0.0166", f"--screenshot-frame={FRAME}",
              f"--screenshot={path}", "--aa=none"])
    if not pathlib.Path(path).exists():
        print(f"FAIL: {path} was never written -- the scene probably did not load")
        sys.exit(1)
    return np.asarray(Image.open(path).convert("RGB")).astype(float).mean(axis=2)


def shoot_sequence(exe, backend, path, scene, count):
    """`count` consecutive frames from FRAME under TAA, in one run: separate
    runs are separate clocks and would not be consecutive. Returns them in
    frame order."""
    path = pathlib.Path(path)
    run(exe, [f"--rhi={backend}", f"--scene={scene}",
              "--frame-time=0.0166", f"--screenshot-frame={FRAME}",
              f"--screenshot-count={count}", f"--screenshot={path}", "--aa=taa"])
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

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("OK: off is off to the byte, the seam darkens and deepens with "
          "intensity, the open floor holds still, a frame reproduces, and a "
          "normal-mapped wall does not occlude itself -- on any jitter offset")


if __name__ == "__main__":
    main()
