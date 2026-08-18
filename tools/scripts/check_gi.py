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

Three more about the traced form's temporal stage (9.13a, ENGINE-NOTES 7aw),
all on Vulkan because the traced form is Vulkan's alone:

6. **The accumulation settles.** On a still camera, over a run of consecutive
   frames from one launch, the noise falls and the level stops moving -- and
   with `GiDenoise` at 0 neither happens. Both halves are needed: a filter
   frozen on its first frame has no noise and no movement either, and only the
   comparison against 0 tells settled from stuck.

7. **It settles on the right value.** This is the claim that closed 7av's open
   number. The denoised level agrees with the same scene measured under TAA at
   `GiDenoise` 0 -- an independent linear average, taken at a different stage
   of the frame -- so a filter that loses energy is caught even though the
   image looks perfectly plausible. **Read 7aw before touching the bounds**:
   the unfiltered figures in 7at and 7av are a third too high, because a
   stochastic renderer measured through the tone curve reads high, and
   comparing against *them* would measure the noise.

Three more about how deep the light goes (9.13b, ENGINE-NOTES 7ax):

8. **A second bounce reaches further.** `gi_away` is the hard case -- the red
   wall is off screen, so every level of red there arrived by tracing -- and
   `--gi-bounces=2` must put more there than 1 does. A band, not a floor: a
   second bounce that doubles the whole estimate is counting the first one
   twice.

9. **It is transport, not a multiply.** The lift is *uneven*: a surface that
   already receives plenty at one bounce gains a few per cent, and one that
   receives almost nothing gains a third. Anything that scaled the indirect
   term by a constant -- the obvious way to get claim 8 by accident -- lifts
   both by the same factor and fails here.

10. **The dial costs nothing unused.** With the screen-space form running,
    `--gi-bounces=2` is the same image to the byte: a screen-space gather has
    one bounce and no way to have two, and the setting is not consulted.

**There is no claim here that the filter keeps the bounce's structure**, and
the empty space is deliberate -- see ENGINE-NOTES 7aw. One was written, on a
fixture built for it, and then three separate breaks of the filter (a smeared
history; a smeared estimate; both, with the neighbourhood clamp disarmed) left
it reading within 3% of unbroken. A measurement that does not move when the
thing it measures is destroyed is not a measurement. The reason is that a
diffuse bounce is smooth at the scale a 3x3 clamp works at *even on a receiver
covered in edges*: what varies from face to face is how much of a smooth field
each face collects, which is the shading's doing and not the buffer's.

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

# --- the temporal stage (9.13a, ENGINE-NOTES 7aw) ----------------------------
#
# The run of frames claim 6 watches. From frame 2 because the point is to see
# the accumulation start; long enough that the last quarter is unambiguously
# settled.
SETTLE_FIRST = 2
SETTLE_COUNT = 48
SETTLE_WINDOW = 12

# Claims 7 and 8 read a settled image instead of a run: late enough that both
# TAA and the denoiser have converged, several frames deep because the band is
# only a few per cent wide.
DETAIL_FRAME = 80
DETAIL_COUNT = 4

# --- the second bounce (9.13b, ENGINE-NOTES 7ax) -----------------------------
#
# Claims 9 and 10 read *luminance* rather than redness. Redness is the right
# measure for "did light carry a colour" and the wrong one for "how much light
# arrived": a white surface bouncing white light moves the mean without moving
# R - B at all, and by the second bounce most of the transport in this room is
# white on white.
#
# The two regions, chosen for how much light reaches them at *one* bounce and
# not for how they respond to a second:
#   - the red wall is directly lit and faces the lit floor, so one bounce
#     already delivers most of what it will ever get;
#   - the open floor faces a black sky and is lit by the sun at a glancing
#     angle, so almost everything indirect it receives came off a wall that had
#     itself been lit indirectly.
LIT_REGION = (0.20, 0.45, 0.10, 0.35)
DIM_REGION = (0.75, 0.90, 0.30, 0.60)

# Claim 8: how much more red a second bounce puts on a wall whose bounce source
# is off screen. Measured +0.81 at one bounce and +1.83 at two, a factor of
# 2.25 -- large because this fixture has a black sky and no ambient, so at one
# bounce every hit is shaded with an irradiance probe that holds nothing and
# the second bounce is the first light those hits get.
MIN_SECOND_BOUNCE_LIFT = 1.5
MAX_SECOND_BOUNCE_LIFT = 3.5

# Claim 9: how much more the dim region gains than the lit one, as a ratio of
# their gains. Measured 1.33 against 1.07, so **1.25 for a traced second bounce
# -- and 1.08 for a constant scale of 2.25 standing in for one**, which is the
# break this bound exists to catch and was measured rather than assumed. (Not
# 1.00: the tone curve answers a constant scale differently at different
# brightnesses, which is 7aw's finding turning up as a floor under this number.)
# The bound sits between the two, and the margin either side is thin because
# there is nowhere roomier for it to be.
MIN_BOUNCE_UNEVENNESS = 1.15

# Claim 6a: how much of the per-pixel flicker the accumulation has to remove.
# Measured on this fixture: 0.67 display levels of frame-to-frame change with
# the filter against 9.71 without it, a factor of fourteen. A fifth is asked
# for, which is a real requirement rather than a restatement of the measurement.
MIN_FLICKER_DROP = 5.0
# Claim 6b: the level has to stop moving. Its drift over the last window
# against its drift over the first: measured 0.005 against 0.068, fourteen
# times. And **the same ratio with the filter off must not show it** --
# measured 1.1, because an unfiltered estimate is as unsettled at frame 49 as
# at frame 3. That second bound is the one that makes the first mean anything.
MIN_SETTLE_RATIO = 4.0
MAX_UNFILTERED_SETTLE_RATIO = 2.0

# Claim 7: the settled level against an independent linear average of the same
# quantity -- TAA, which accumulates the shaded frame one stage later by a
# different mechanism. Measured 0.94, and it sits below 1 legitimately: TAA's
# own averaging is about four frames deep against the denoiser's fifty, so the
# reference still carries some of the noise inflation 7aw describes.
#
# **Tight on purpose, and these bounds were fitted to a break rather than to
# the measurement.** The first version allowed 0.80 to 1.20 and a history that
# lost three per cent a frame -- a fifth of the bounce at steady state --
# passed it at 0.87. The band reads in display levels and the tone curve
# compresses, so a fifth of the light is an eighth of the number: **allow in
# levels what you would refuse in irradiance and the check refuses nothing.**
MIN_SETTLED_AGREEMENT = 0.89
MAX_SETTLED_AGREEMENT = 1.08



def run(exe, args):
    result = subprocess.run([str(exe), *args], cwd=exe.parent, capture_output=True, text=True)
    return result.returncode, (result.stdout or "") + (result.stderr or "")


def shoot(exe, backend, scene, path, extra=(), aa="none", frame=FRAME):
    code, log = run(exe, [f"--rhi={backend}", f"--scene=scenes/{scene}.rage",
                          "--frame-time=0.0166", f"--screenshot-frame={frame}",
                          f"--screenshot={path}", f"--aa={aa}", *extra])
    if code != 0 or not pathlib.Path(path).exists():
        print(f"FAIL: {backend} {scene} exited {code} / no image")
        print(log[-2000:])
        sys.exit(1)
    return np.asarray(Image.open(path).convert("RGB")).astype(float)


def sequence(exe, backend, scene, path, first, count, extra=(), aa="none"):
    """A run of consecutive frames out of one launch, as `<stem>_<frame>.png`.

    One launch rather than one per frame, because the thing being measured is
    an accumulation: frame 40 of a fresh process is frame 40 of a *different*
    accumulation, and comparing those would measure process startup.

    Stale frames are cleared first, and the caller's stems must not be
    prefixes of one another -- `foo` and `foo2` share a glob, and the run that
    cleans up second silently deletes the first one's frames.
    """
    path = pathlib.Path(path)
    for stale in path.parent.glob(path.stem + "_*.png"):
        stale.unlink()

    code, log = run(exe, [f"--rhi={backend}", f"--scene=scenes/{scene}.rage",
                          "--frame-time=0.0166", f"--screenshot-frame={first}",
                          f"--screenshot-count={count}", f"--screenshot={path}",
                          f"--aa={aa}", *extra])
    frames = [path.with_name(f"{path.stem}_{n}{path.suffix}")
              for n in range(first, first + count)]
    missing = [f for f in frames if not f.exists()]
    if code != 0 or missing:
        print(f"FAIL: {backend} {scene} exited {code} / {len(missing)} frames missing")
        print(log[-2000:])
        sys.exit(1)
    return [np.asarray(Image.open(f).convert("RGB")).astype(float) for f in frames]


def patch(image, region):
    height, width = image.shape[:2]
    y0, y1, x0, x1 = region
    return image[int(height * y0):int(height * y1), int(width * x0):int(width * x1)]


def flicker(frames, region):
    """Mean absolute change between consecutive frames, over a region.

    Per pixel and then averaged, not the change in the region's mean: noise
    that cancels within a frame is exactly what an accumulation is for, and a
    region mean cannot see it move.
    """
    patches = [patch(f, region) for f in frames]
    return [float(np.abs(b - a).mean()) for a, b in zip(patches, patches[1:])]




def luminance(image, region):
    """Mean of all three channels over a region: how much light arrived,
    without regard to what colour it is."""
    return float(patch(image, region).mean())


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
        # Named rather than "not the corner": `gi_detail` is the corner view
        # too, and the version of this that tested for `gi_corner` would have
        # pointed the third fixture's camera at the wrong wall.
        away = scene == "gi_away"
        camera = make_gi_scene.AWAY_CAMERA_POSITION if away else None
        rotation = make_gi_scene.AWAY_CAMERA_ROTATION if away else None
        path.write_text(make_gi_scene.build(handle, camera=camera, rotation=rotation,
                                            detail=scene == "gi_detail"),
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

    # --- 6: the temporal stage settles, and without it nothing does ----------
    #
    # One launch each, `--screenshot-count` frames apart, still camera. The
    # difference between the two runs is the profile's GiDenoise and nothing
    # else: the same pass runs either way and reads its feedback from there,
    # so this is the filter being measured rather than a branch being taken.
    profile("gi_corner", { "GiIntensity": 2.0, "GiDenoise": 0.9 })
    accumulated = sequence(exe, "vulkan", "gi_corner", shots / "settle-filtered.png",
                           SETTLE_FIRST, SETTLE_COUNT, ray)
    profile("gi_corner", { "GiIntensity": 2.0, "GiDenoise": 0.0 })
    unfiltered = sequence(exe, "vulkan", "gi_corner", shots / "settle-raw.png",
                          SETTLE_FIRST, SETTLE_COUNT, ray)

    filtered_flicker = np.mean(flicker(accumulated, NEAR_REGION)[-SETTLE_WINDOW:])
    raw_flicker = np.mean(flicker(unfiltered, NEAR_REGION)[-SETTLE_WINDOW:])
    drop = raw_flicker / max(filtered_flicker, 1e-6)
    print(f"the accumulation removes {drop:.1f}x of the frame-to-frame flicker "
          f"({filtered_flicker:.2f} levels against {raw_flicker:.2f})")
    if drop < MIN_FLICKER_DROP:
        failures.append(f"the temporal stage removed almost none of the ray noise "
                        f"({drop:.1f}x, wanted {MIN_FLICKER_DROP}x) -- four rays a pixel "
                        f"converge by being accumulated or not at all")

    def settle_ratio(frames):
        """How much less the *level* moves at the end of the run than at the
        start. One number, so the two runs can be compared by it."""
        levels = [redness(f, NEAR_REGION) for f in frames]
        steps = np.abs(np.diff(levels))
        return float(np.mean(steps[:SETTLE_WINDOW]) / max(np.mean(steps[-SETTLE_WINDOW:]), 1e-6))

    filtered_settle = settle_ratio(accumulated)
    raw_settle = settle_ratio(unfiltered)
    print(f"the level settles {filtered_settle:.1f}x with the filter and "
          f"{raw_settle:.1f}x without it")
    if filtered_settle < MIN_SETTLE_RATIO:
        failures.append(f"the indirect level never stopped moving ({filtered_settle:.1f}x, "
                        f"wanted {MIN_SETTLE_RATIO}x) -- an accumulation that does not "
                        f"converge is a lag, not a filter")
    if raw_settle > MAX_UNFILTERED_SETTLE_RATIO:
        failures.append(f"the level settled {raw_settle:.1f}x with the temporal stage *off* "
                        f"(allowed {MAX_UNFILTERED_SETTLE_RATIO}x) -- then claim 6 is "
                        f"measuring the scene going quiet, not the filter")

    # --- 7: the value it settles on -----------------------------------------
    #
    # Under TAA, on `gi_detail`. TAA is the reference on purpose (ENGINE-NOTES
    # 7aw): it is a linear average of the same light taken one stage later, so
    # a denoiser that loses energy disagrees with it while a denoiser that
    # removes noise does not. Measuring against an *unfiltered* frame instead
    # would compare against a number a third too high, which is how the drop
    # looked like a loss for a session.
    #
    # `gi_detail` rather than `gi_corner` because a receiver with occlusion and
    # faces at many angles is a harder place to settle on the right value than
    # two flat walls -- and because that fixture is where the structure claim
    # was tried and abandoned, which is worth being able to re-run.
    def settled(name, settings, extra=()):
        """Four consecutive frames, averaged. One frame would do for the mean
        of a settled image, but claim 7's band is a few per cent wide and a
        single frame's residual wobble is a fair part of that."""
        profile("gi_detail", settings)
        frames = sequence(exe, "vulkan", "gi_detail", shots / f"detail-{name}.png",
                          DETAIL_FRAME, DETAIL_COUNT, extra, aa="taa")
        return np.mean(frames, axis=0)

    # The baseline keeps the ray flags and turns the *intensity* to zero, rather
    # than turning ray tracing off: `--raytracing=on` changes more than the
    # bounce, and a baseline rendered down a different path would be subtracted
    # from both readings and quietly move their ratio. Claim 5 is what licenses
    # this -- intensity zero is the picture without the feature.
    detail_off = settled("off", { "GiIntensity": 0.0 }, ray)
    detail_raw = settled("raw", { "GiIntensity": 2.0, "GiDenoise": 0.0 }, ray)
    detail_filtered = settled("filtered", { "GiIntensity": 2.0, "GiDenoise": 0.9 }, ray)

    reference_level = (redness(detail_raw, NEAR_REGION)
                       - redness(detail_off, NEAR_REGION))
    settled_level = (redness(detail_filtered, NEAR_REGION)
                     - redness(detail_off, NEAR_REGION))
    agreement = settled_level / max(reference_level, 1e-6)
    print(f"the settled level is {agreement:.2f} of an independently averaged one "
          f"({settled_level:+.2f} against {reference_level:+.2f})")
    if not MIN_SETTLED_AGREEMENT <= agreement <= MAX_SETTLED_AGREEMENT:
        failures.append(f"the accumulated bounce is {agreement:.2f} of the same light "
                        f"averaged by TAA instead (wanted {MIN_SETTLED_AGREEMENT} to "
                        f"{MAX_SETTLED_AGREEMENT}) -- the filter is moving energy, not noise")

    # --- 8, 9 and 10: the second bounce --------------------------------------
    #
    # `--gi-bounces=` rather than the scene's RenderSettings because the
    # fixtures are generated and a render setting lives on the project, not on
    # the scene -- the same reason the ray flags are passed this way.
    profile("gi_away", { "GiIntensity": 0.0 })
    bounce_base = shoot(exe, "vulkan", "gi_away", shots / "vulkan-away-bounce-base.png",
                        ray, frame=90)
    profile("gi_away", { "GiIntensity": 2.0 })
    away_one = shoot(exe, "vulkan", "gi_away", shots / "vulkan-away-bounce1.png",
                     ray + ["--gi-bounces=1"], frame=90)
    away_two = shoot(exe, "vulkan", "gi_away", shots / "vulkan-away-bounce2.png",
                     ray + ["--gi-bounces=2"], frame=90)

    one = redness(away_one, AWAY_REGION) - redness(bounce_base, AWAY_REGION)
    two = redness(away_two, AWAY_REGION) - redness(bounce_base, AWAY_REGION)
    lift = two / max(one, 1e-6)
    print(f"a second bounce puts {lift:.2f}x the red on a wall whose source is off screen "
          f"({two:+.2f} against {one:+.2f})")
    if lift < MIN_SECOND_BOUNCE_LIFT:
        failures.append(f"a second bounce added almost nothing ({lift:.2f}x, wanted "
                        f"{MIN_SECOND_BOUNCE_LIFT}x) -- the ray is being traced and its "
                        f"light is not arriving")
    if lift > MAX_SECOND_BOUNCE_LIFT:
        failures.append(f"a second bounce more than {MAX_SECOND_BOUNCE_LIFT}x the light of "
                        f"one ({lift:.2f}x) -- a bounce that beats the light that made it "
                        f"is a term counted twice")

    profile("gi_corner", { "GiIntensity": 0.0 })
    corner_base = shoot(exe, "vulkan", "gi_corner", shots / "vulkan-corner-bounce-base.png",
                        ray, frame=90)
    profile("gi_corner", { "GiIntensity": 2.0 })
    corner_one = shoot(exe, "vulkan", "gi_corner", shots / "vulkan-corner-bounce1.png",
                       ray + ["--gi-bounces=1"], frame=90)
    corner_two = shoot(exe, "vulkan", "gi_corner", shots / "vulkan-corner-bounce2.png",
                       ray + ["--gi-bounces=2"], frame=90)

    def gain_ratio(region):
        base = luminance(corner_base, region)
        return ((luminance(corner_two, region) - base)
                / max(luminance(corner_one, region) - base, 1e-6))

    lit_gain = gain_ratio(LIT_REGION)
    dim_gain = gain_ratio(DIM_REGION)
    unevenness = dim_gain / max(lit_gain, 1e-6)
    print(f"the second bounce lifts the dim region {dim_gain:.2f}x and the lit one "
          f"{lit_gain:.2f}x -- {unevenness:.2f} times as much")
    if unevenness < MIN_BOUNCE_UNEVENNESS:
        failures.append(f"the second bounce lifted both regions alike ({unevenness:.2f}, "
                        f"wanted {MIN_BOUNCE_UNEVENNESS}) -- a constant scale on the "
                        f"indirect term looks exactly like this and is not transport")

    # 10: with the screen-space form running, the dial is not consulted.
    profile("gi_corner", { "GlobalIllumination": True, "GiIntensity": 2.0, "GiRadius": 4.0 })
    screen_one = shoot(exe, "vulkan", "gi_corner", shots / "vulkan-corner-screen1.png",
                       ["--gi-bounces=1"])
    screen_two = shoot(exe, "vulkan", "gi_corner", shots / "vulkan-corner-screen2.png",
                       ["--gi-bounces=2"])
    unused = float(np.abs(screen_one - screen_two).max())
    print(f"with the screen-space form running, the bounce count changes the image by {unused:g}")
    if unused != 0.0:
        failures.append(f"asking for two bounces changed the screen-space gather "
                        f"(max {unused:g}) -- it has one bounce and no way to have two")

    make_gi_scene.main()   # leave the fixtures as committed

    if failures:
        print()
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("\nOK: intensity zero is the image without it, light carries the red wall's colour onto "
          "the white one beside it and not onto the far end, the traced form does the same and "
          "also reddens a wall whose bounce source is off screen -- which the screen-space form "
          "cannot -- only one of the two ever runs, the traced form's accumulation settles, "
          "and settles on the value an independent average agrees with, and a second bounce "
          "reaches further than one, unevenly, without touching the form that cannot have one")


if __name__ == "__main__":
    main()
