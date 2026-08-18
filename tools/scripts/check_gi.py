#!/usr/bin/env python3
"""Global illumination (9.12, ENGINE-NOTES 7at): does light carry colour?

The one claim both forms make is that light picks up the colour of what it
bounced off. So the fixture is the smallest scene where a red pixel has
exactly one explanation: a white floor and a white wall meeting a saturated
red one, one white directional light, a black sky and no ambient. Nothing in
frame is red but the red wall, so red on a white surface arrived by bouncing.

Five claims, all on `gi_corner`/`gi_away` from make_gi_scene.py, all measured
in regions stated as frame fractions -- the camera and the geometry are pinned
by the fixture, and locating the bleed by looking for redness would be asking
the effect under test to define its own measurement (check_ssao's rule).

1. **Intensity zero is the image without it.** The screen-space chain runs --
   its four passes are in the graph -- and the frame is identical to the byte
   to the frame with the feature off. Nothing is "nearly" off.

2. **The screen-space form bleeds, and bleeds *locally*.** With it on, the
   white wall beside the red one reddens (R over B, against the same region
   with GI off) by a stated margin, and the far end of the *same wall* reddens
   by much less. A global tint would lift both.

3. **The traced form bleeds too**, on the same region, with `--raytracing=on
   --rt-gi=on`. Vulkan only: the ray form needs ray queries and bindless.

4. **The traced form sees what the screen cannot.** `gi_away` is the same room
   with the camera turned until the red wall is off screen -- zero reddish
   pixels in the frame at all. The screen-space gather has nothing red to
   find, so it must add **no** red; the traced form, whose rays do not care
   what is in frame, must still redden the wall. This is the difference the
   two forms exist to have, and no threshold-fiddling can make the
   screen-space one pass it.

5. **One at a time.** With the traced form on, flipping the profile's
   GlobalIllumination changes nothing to the byte: the graph does not add the
   screen-space passes at all, which is what the greyed-out row in the editor
   is telling the truth about.

Usage:
    python tools/scripts/check_gi.py [--config Release]
"""

import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

FRAME = 30

# Regions, as fractions of the frame. The white wall just past the corner, and
# the far end of that same wall.
NEAR_REGION = (0.25, 0.45, 0.52, 0.60)   # y0, y1, x0, x1
FAR_REGION = (0.20, 0.40, 0.88, 0.98)
# The away view has no red in it at all. The region is its *left* edge -- the
# part of the white wall nearest the corner the camera has turned away from,
# where a bounce that does not care what is on screen lands hardest. Measured:
# the traced form puts +1.26 levels of red here and the screen-space form
# +0.00, because it has nothing red to gather.
AWAY_REGION = (0.28, 0.52, 0.02, 0.20)

# How much redder (R - B, in display levels) a region must go for a bounce to
# count as measured, and how much less the far end may take before "local"
# stops meaning anything.
# Measured on this fixture: the screen-space form puts +0.77 levels of red on
# the near region and +0.00 on the far one; the traced form +2.72 and +1.26 in
# the away view's left edge. The thresholds sit well under those, and the far
# and off-screen claims are qualitative -- 0.00 against a real number.
MIN_NEAR_BLEED = 0.4
MAX_FAR_SHARE = 0.6
# **And a ceiling, which the first version of this file did not have.** Every
# threshold here was a floor -- "did the feature do anything" -- so a bleed ten
# times too strong passed as happily as a correct one. That is not a
# hypothetical: wiring a temporal denoiser onto the indirect buffer turned
# SSGI's +1.71 into +16.98, because the gather reads the lit image and the lit
# image now carries last frame's indirect, and this file printed OK. A floor
# cannot tell a feature from a runaway. ENGINE-NOTES 7av.
MAX_NEAR_BLEED = 6.0
# The two backends compute the same bounce by the same rules, so they must
# agree about it. They do, to 0.02 levels -- and under that loop they diverged
# by 2.03, because the result had become a function of accumulated history
# rather than of the scene. A number that differs by backend is a number
# nobody can tune against.
MAX_BACKEND_SPREAD = 0.75
# The away view's discriminator: the screen-space form must add essentially
# nothing (it has nothing to gather), the traced one must add a real amount.
MAX_SCREEN_AWAY_BLEED = 0.3
MIN_TRACED_AWAY_BLEED = 0.6
# Ceilings for the traced form, for the reason the screen-space one has them.
MAX_TRACED_NEAR_BLEED = 4.0
MAX_TRACED_AWAY_BLEED = 2.0


def run(exe, args):
    result = subprocess.run([str(exe), *args], cwd=exe.parent, capture_output=True, text=True)
    return result.returncode, (result.stdout or "") + (result.stderr or "")


def shoot(exe, backend, scene, path, extra=()):
    code, log = run(exe, [f"--rhi={backend}", f"--scene=scenes/{scene}.rage",
                          "--frame-time=0.0166", f"--screenshot-frame={FRAME}",
                          f"--screenshot={path}", "--aa=none", *extra])
    if code != 0 or not pathlib.Path(path).exists():
        print(f"FAIL: {backend} {scene} exited {code} / no image")
        print(log[-2000:])
        sys.exit(1)
    return np.asarray(Image.open(path).convert("RGB")).astype(float)


def redness(image, region):
    """Mean R - B over a region: zero on anything grey, positive on a surface
    that has taken red light."""
    height, width = image.shape[:2]
    y0, y1, x0, x1 = region
    patch = image[int(height * y0):int(height * y1), int(width * x0):int(width * x1)]
    return float(patch[:, :, 0].mean() - patch[:, :, 2].mean())


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
    import make_gi_scene
    import postprofile

    scenes = root / "SampleProject" / "assets" / "scenes"
    shots = root / "build" / "gi"
    shots.mkdir(parents=True, exist_ok=True)
    make_gi_scene.main()

    failures = []
    near_by_backend = {}

    def profile(scene, settings):
        """Rewrite one fixture's profile, and its scene with it -- the scene
        carries the profile's handle."""
        path = scenes / f"{scene}.rage"
        handle = postprofile.write_beside(path, { "BloomEnabled": False, **settings })
        camera = None if scene == "gi_corner" else make_gi_scene.AWAY_CAMERA_POSITION
        rotation = None if scene == "gi_corner" else make_gi_scene.AWAY_CAMERA_ROTATION
        path.write_text(make_gi_scene.build(handle, camera=camera, rotation=rotation),
                        encoding="utf-8", newline="\n")

    # --- 1 and 2: the screen-space form, on both backends ---------------------
    for backend in ("vulkan", "opengl"):
        profile("gi_corner", {})
        off = shoot(exe, backend, "gi_corner", shots / f"{backend}-corner-off.png")

        profile("gi_corner", { "GlobalIllumination": True, "GiIntensity": 0.0 })
        zero = shoot(exe, backend, "gi_corner", shots / f"{backend}-corner-zero.png")
        difference = float(np.abs(zero - off).max())
        print(f"{backend}: intensity zero differs from off by {difference:g}")
        if difference != 0.0:
            failures.append(f"{backend}: an intensity of zero changed the image (max {difference:g})")

        profile("gi_corner", { "GlobalIllumination": True, "GiIntensity": 2.0, "GiRadius": 4.0 })
        on = shoot(exe, backend, "gi_corner", shots / f"{backend}-corner-ssgi.png")

        near = redness(on, NEAR_REGION) - redness(off, NEAR_REGION)
        far = redness(on, FAR_REGION) - redness(off, FAR_REGION)
        print(f"{backend}: screen-space bleed -- near the corner +{near:.2f} levels of red, "
              f"far along the same wall +{far:.2f}")
        near_by_backend[backend] = near
        if near < MIN_NEAR_BLEED:
            failures.append(f"{backend}: the wall beside the red one did not redden "
                            f"(+{near:.2f} levels, wanted {MIN_NEAR_BLEED})")
        if near > MAX_NEAR_BLEED:
            failures.append(f"{backend}: the bleed is far past this fixture's calibration "
                            f"(+{near:.2f} levels, ceiling {MAX_NEAR_BLEED}) -- "
                            f"a feedback loop looks exactly like this")
        if far > near * MAX_FAR_SHARE:
            failures.append(f"{backend}: the far end reddened as much as the near one "
                            f"(+{far:.2f} against +{near:.2f}) -- a tint, not a bounce")

    if len(near_by_backend) == 2:
        spread = abs(near_by_backend["vulkan"] - near_by_backend["opengl"])
        print(f"the two backends' near bleed differs by {spread:.2f} levels")
        if spread > MAX_BACKEND_SPREAD:
            failures.append(f"the backends disagree about the bounce by {spread:.2f} levels "
                            f"(allowed {MAX_BACKEND_SPREAD}) -- the same rules on the same "
                            f"fixture must give the same answer")

    # --- 4a: the screen-space form has nothing to gather off screen -----------
    profile("gi_away", {})
    away_off = shoot(exe, "vulkan", "gi_away", shots / "vulkan-away-off.png")
    profile("gi_away", { "GlobalIllumination": True, "GiIntensity": 2.0, "GiRadius": 4.0 })
    away_screen = shoot(exe, "vulkan", "gi_away", shots / "vulkan-away-ssgi.png")
    screen_away = redness(away_screen, AWAY_REGION) - redness(away_off, AWAY_REGION)
    print(f"with the red wall off screen, the screen-space form adds {screen_away:+.2f} levels of red")
    if screen_away > MAX_SCREEN_AWAY_BLEED:
        failures.append(f"the screen-space form reddened a wall whose bounce source is off screen "
                        f"({screen_away:+.2f} levels) -- it cannot know that colour")

    # --- 3, 4b and 5: the traced form. Vulkan only ---------------------------
    ray = ["--raytracing=on", "--rt-gi=on"]
    profile("gi_corner", {})
    corner_off = shoot(exe, "vulkan", "gi_corner", shots / "vulkan-corner-off.png")
    profile("gi_corner", { "GiIntensity": 2.0 })
    traced = shoot(exe, "vulkan", "gi_corner", shots / "vulkan-corner-rtgi.png", ray)
    traced_near = redness(traced, NEAR_REGION) - redness(corner_off, NEAR_REGION)
    print(f"traced bleed near the corner: {traced_near:+.2f} levels of red")
    if traced_near < MIN_NEAR_BLEED:
        failures.append(f"ray-traced GI did not redden the wall beside the red one "
                        f"({traced_near:+.2f} levels)")
    if traced_near > MAX_TRACED_NEAR_BLEED:
        failures.append(f"ray-traced GI reddened the wall far past calibration "
                        f"({traced_near:+.2f} levels, ceiling {MAX_TRACED_NEAR_BLEED})")

    profile("gi_away", {})
    away_base = shoot(exe, "vulkan", "gi_away", shots / "vulkan-away-base.png")
    profile("gi_away", { "GiIntensity": 2.0 })
    away_traced = shoot(exe, "vulkan", "gi_away", shots / "vulkan-away-rtgi.png", ray)
    traced_away = redness(away_traced, AWAY_REGION) - redness(away_base, AWAY_REGION)
    print(f"with the red wall off screen, the traced form adds {traced_away:+.2f} levels of red")
    if traced_away < MIN_TRACED_AWAY_BLEED:
        failures.append(f"ray-traced GI did not redden a wall whose bounce source is off screen "
                        f"({traced_away:+.2f} levels) -- the one thing it is for")
    if traced_away > MAX_TRACED_AWAY_BLEED:
        failures.append(f"ray-traced GI's off-screen bounce is far past calibration "
                        f"({traced_away:+.2f} levels, ceiling {MAX_TRACED_AWAY_BLEED})")

    # 5: while the traced form runs, the profile's toggle is not consulted.
    profile("gi_corner", { "GlobalIllumination": True, "GiIntensity": 2.0, "GiRadius": 4.0 })
    both = shoot(exe, "vulkan", "gi_corner", shots / "vulkan-corner-both.png", ray)
    profile("gi_corner", { "GlobalIllumination": False, "GiIntensity": 2.0, "GiRadius": 4.0 })
    traced_only = shoot(exe, "vulkan", "gi_corner", shots / "vulkan-corner-tracedonly.png", ray)
    exclusive = float(np.abs(both - traced_only).max())
    print(f"under ray-traced GI, the profile's toggle changes the image by {exclusive:g}")
    if exclusive != 0.0:
        failures.append(f"with ray-traced GI on, the profile's Global illumination still did "
                        f"something (max {exclusive:g}) -- the two are meant to be exclusive")

    make_gi_scene.main()   # leave the fixtures as committed

    if failures:
        print()
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("\nOK: intensity zero is the image without it, light carries the red wall's colour onto "
          "the white one beside it and not onto the far end, the traced form does the same and "
          "also reddens a wall whose bounce source is off screen -- which the screen-space form "
          "cannot -- and only one of the two ever runs")


if __name__ == "__main__":
    main()
