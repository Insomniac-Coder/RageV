#!/usr/bin/env python3
"""Ray-traced shadows (8.12), against the maps they sit beside.

Two fixtures. The first (make_ray_shadow_scene, stage 1) is a grey floor
under a low sun that shines toward the camera, a near box, and a far wall a
hundred and fifty metres out whose shadow reaches back across the ground.
The second (make_ray_shadow_local_scene, stage 2) has no sun: a spot light
and a point light each throw a box's shadow onto a patch of floor of their
own, and a low spot throws the running fox's shadow back across the middle.
Both methods render both on Vulkan; the maps render them on OpenGL.
ENGINE-NOTES 7am and 7an.

Stage 1, the sun:

1. **Same shape, near and far.** The traced shadow and the mapped shadow
   overlap to at least 0.9 IoU under the near box and under the far wall.
   Where the maps are right, the rays agree with them -- and this is the
   claim that judges the ray's origin offset from above: an offset large
   enough to detach the shadow would move its edge off the maps'.
2. **The traced edge is hard.** Across the near shadow's edge the traced
   frame goes from lit to shadowed within one pixel; the maps take three or
   more (a 3x3 percentage-closer filter over hardware 2x2 taps). Measured
   as the count of pixels between 10% and 90% of the lit-to-shadow swing.
3. **No self-shadowing.** On open, sunlit floor the traced frame has no
   dark speckle -- fewer than one pixel in ten thousand below half the lit
   level. This judges the origin offset from below: too small and the floor
   shadows itself in a moire.
4. **A frame reproduces**, both ways.
5. **OpenGL takes the maps and says so.** `--raytracing=on` on a backend
   with no ray queries logs the fallback, renders, and exits clean; its
   shadow agrees in shape with Vulkan's maps.

Stage 2, the local lights and the fox:

6. **A spot's traced shadow is its mapped shadow's shape, and a point
   light's is** (IoU >= 0.9 each): the ray toward a light agrees with the
   map where the map is right, which judges its origin, direction and
   length together. A lid sits *beyond* each light -- a caster a ray that
   did not stop at the light would hit -- and shadows nothing under either
   method.
7. **The fox's traced shadow is its pose.** At the same frame the traced
   shadow and the mapped one -- the maps render the posed fox through the
   skinned depth shader -- overlap to IoU >= 0.85; and the traced shadow at
   frame 30 differs from the one at frame 45 (IoU <= 0.95) while the boxes'
   do not (>= 0.99): the fox moved and its transform did not. A structure
   built once from the bind pose fails the second half exactly.
8. **The maps' budget is the maps'.** Five spot lights ask to cast; under
   maps the log warns that the fifth will not, under rays it does not,
   because there is no map to run out of.
9. The traced local frame reproduces; OpenGL asked for rays on this fixture
   falls back and its spot and point maps agree with Vulkan's.

Falsified by negating the ray direction in the shader: every shadowed pixel
lights and IoU collapses to zero (stage 1). By passing 1e4 as every ray's
tMax: the lids shadow both discs and the local IoUs collapse (stage 2). By
skipping the refit -- a null bone list -- the fox's traced shadow becomes
the bind pose: its IoU against the maps falls and frames 30 and 45 become
identical.

**What the fixture found, and the design had wrong**: cascaded maps do not
stop at `ShadowDistance`. The setting bounds the cascade *fit*; the last
cascade's orthographic footprint on a receding ground plane reaches far
past it, and outside the map the sampler clamps to the edge texel -- so on
this fixture the maps shadow the wall's foot at a hundred and fifty metres
too. The "no shadow past the distance" claim the design planned to measure
was not true of the maps and is not made here.

Usage:
    python tools/scripts/check_ray_shadows.py [--config Release]
"""

import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

FRAME = 30
LATER_FRAME = 45
MIN_IOU = 0.9
MIN_FOX_IOU = 0.85
MAX_SAME_POSE_IOU = 0.95
MIN_STILL_IOU = 0.99
MAX_TRACED_EDGE_PX = 1
MIN_MAPPED_EDGE_PX = 3
MAX_SPECKLE_FRACTION = 1e-4

# Regions of the 1600x900 frame, in (rows, cols). Fixed by the fixture's
# stated camera and geometry; the near region holds the near box's shadow,
# the far region the wall's, and the open region is sunlit floor to the
# right of both. If make_ray_shadow_scene changes, these change with it.
NEAR = (slice(560, 830), slice(350, 720))
FAR = (slice(180, 320), slice(500, 1100))
OPEN = (slice(600, 880), slice(1000, 1550))
# A scanline across the near shadow's right edge, and the span it lies in.
EDGE_ROW = 720
EDGE_COLS = (560, 700)
# A lit floor patch, for the reference level.
LIT = (slice(700, 800), slice(1200, 1400))

# The local fixture, seen from almost straight above: each region holds one
# light's shadow of one caster, on floor only that light reaches. The fox
# region stops short of the fox itself, which is a dark thing in both frames
# and not a shadow. If make_ray_shadow_local_scene changes, these change.
SPOT = (slice(380, 520), slice(260, 410))
POINT = (slice(340, 525), slice(1180, 1350))
FOX = (slice(250, 535), slice(590, 920))
BUDGET_WARNING = "spot lights ask to cast"


def run(exe, args):
    result = subprocess.run([str(exe), "--render-defaults=on", *args], cwd=exe.parent,
                            capture_output=True, text=True)
    return result.returncode, (result.stdout or "") + (result.stderr or "")


def shoot(exe, backend, scene, path, mode, frame=FRAME):
    """`mode` is "rt" or "maps": the project's Ray tracing checkbox, overridden
    for this run by --raytracing=on|off."""
    code, log = run(exe, [f"--rhi={backend}", f"--scene=scenes/{scene}.rage",
                          "--frame-time=0.016666", f"--screenshot-frame={frame}",
                          f"--screenshot={path}", "--aa=none",
                          f"--raytracing={'on' if mode == 'rt' else 'off'}"])
    if code != 0 or not pathlib.Path(path).exists():
        print(f"FAIL: {backend} {scene} {mode} exited {code} / no image")
        print(log[-1500:])
        sys.exit(1)
    return np.asarray(Image.open(path).convert("RGB")).astype(float).mean(axis=2), log


def iou(a, b):
    inter = (a & b).sum()
    union = (a | b).sum()
    return inter / union if union else 0.0


def edge_width(image):
    line = image[EDGE_ROW, EDGE_COLS[0]:EDGE_COLS[1]]
    lo, hi = line.min(), line.max()
    if hi - lo < 1.0:
        return 0
    t = (line - lo) / (hi - lo)
    return int(((t > 0.1) & (t < 0.9)).sum())


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
    import make_ray_shadow_scene
    import make_ray_shadow_local_scene
    import postprofile

    scenes = root / "SampleProject" / "assets" / "scenes"
    for name, module in (("ray_shadows", make_ray_shadow_scene),
                         ("ray_shadows_local", make_ray_shadow_local_scene)):
        scene = scenes / f"{name}.rage"
        handle = postprofile.write_beside(scene, {"BloomEnabled": False})
        scene.write_text(module.build(handle))

    shots = root / "build" / "rayshadows"
    shots.mkdir(parents=True, exist_ok=True)

    failures = 0

    def claim(ok, text):
        nonlocal failures
        print(("  pass  " if ok else "  FAIL  ") + text)
        failures += 0 if ok else 1

    # --- stage 1: the sun ---------------------------------------------------
    rt, log_rt = shoot(exe, "vulkan", "ray_shadows", shots / "vk_rt.png", "rt")
    rt2, _ = shoot(exe, "vulkan", "ray_shadows", shots / "vk_rt2.png", "rt")
    mp, log_mp = shoot(exe, "vulkan", "ray_shadows", shots / "vk_maps.png", "maps")
    mp2, _ = shoot(exe, "vulkan", "ray_shadows", shots / "vk_maps2.png", "maps")

    claim("shadows traced" in log_rt, "Vulkan with --raytracing=on reports tracing")
    claim("shadows traced" not in log_mp, "and with --raytracing=off does not")

    # From the mapped frame: a broken traced frame must not move the bar it
    # is judged against.
    lit = mp[LIT].mean()
    threshold = lit * 0.5

    def dark(image, region):
        return image[region] < threshold

    near_iou = iou(dark(rt, NEAR), dark(mp, NEAR))
    far_iou = iou(dark(rt, FAR), dark(mp, FAR))
    claim(near_iou >= MIN_IOU, f"near box: traced and mapped shadows overlap, IoU {near_iou:.3f}")
    claim(far_iou >= MIN_IOU, f"far wall: traced and mapped shadows overlap, IoU {far_iou:.3f}")

    traced_edge = edge_width(rt)
    mapped_edge = edge_width(mp)
    claim(traced_edge <= MAX_TRACED_EDGE_PX,
          f"the traced edge is hard: {traced_edge} px between 10% and 90%")
    claim(mapped_edge >= MIN_MAPPED_EDGE_PX,
          f"and the mapped edge is filtered: {mapped_edge} px")

    speckle = float(dark(rt, OPEN).mean())
    claim(speckle < MAX_SPECKLE_FRACTION,
          f"open sunlit floor has no self-shadowing: {speckle:.6f} of pixels dark")

    claim(int((np.abs(rt - rt2) > 0).sum()) == 0, "the traced frame reproduces")
    claim(int((np.abs(mp - mp2) > 0).sum()) == 0, "the mapped frame reproduces")

    gl, log_gl = shoot(exe, "opengl", "ray_shadows", shots / "gl_rt.png", "rt")
    claim("using shadow maps" in log_gl,
          "OpenGL asked for --raytracing=on reports it is using shadow maps")
    gl_iou = iou(dark(gl, NEAR), dark(mp, NEAR))
    claim(gl_iou >= MIN_IOU, f"and its mapped shadow agrees with Vulkan's, IoU {gl_iou:.3f}")

    # --- stage 2: the local lights and the fox -----------------------------
    lrt, llog_rt = shoot(exe, "vulkan", "ray_shadows_local", shots / "local_rt.png", "rt")
    lrt2, _ = shoot(exe, "vulkan", "ray_shadows_local", shots / "local_rt2.png", "rt")
    lmp, llog_mp = shoot(exe, "vulkan", "ray_shadows_local", shots / "local_maps.png", "maps")
    later, _ = shoot(exe, "vulkan", "ray_shadows_local", shots / "local_rt_later.png", "rt",
                     frame=LATER_FRAME)

    # Each region's own lit level, from the mapped frame: the three lights
    # are not equally bright and a broken traced frame must not move the bar.
    def local_dark(image, region):
        return image[region] < np.percentile(lmp[region], 90) * 0.5

    spot_iou = iou(local_dark(lrt, SPOT), local_dark(lmp, SPOT))
    point_iou = iou(local_dark(lrt, POINT), local_dark(lmp, POINT))
    claim(spot_iou >= MIN_IOU,
          f"spot light: traced and mapped shadows overlap, IoU {spot_iou:.3f}")
    claim(point_iou >= MIN_IOU,
          f"point light: traced and mapped shadows overlap, IoU {point_iou:.3f}")

    fox_iou = iou(local_dark(lrt, FOX), local_dark(lmp, FOX))
    claim(fox_iou >= MIN_FOX_IOU,
          f"the fox's traced shadow is its posed, mapped shadow's shape, IoU {fox_iou:.3f}")
    pose_iou = iou(local_dark(lrt, FOX), local_dark(later, FOX))
    still_iou = min(iou(local_dark(lrt, SPOT), local_dark(later, SPOT)),
                    iou(local_dark(lrt, POINT), local_dark(later, POINT)))
    claim(pose_iou <= MAX_SAME_POSE_IOU,
          f"and it changes between frame {FRAME} and frame {LATER_FRAME}: IoU {pose_iou:.3f}")
    claim(still_iou >= MIN_STILL_IOU,
          f"while the boxes' shadows do not: IoU {still_iou:.3f}")

    claim(BUDGET_WARNING in llog_mp,
          "five casting spots: under maps the log warns that the budget is four")
    claim(BUDGET_WARNING not in llog_rt,
          "and under rays it does not: every casting light traces")

    claim(int((np.abs(lrt - lrt2) > 0).sum()) == 0, "the traced local frame reproduces")

    lgl, llog_gl = shoot(exe, "opengl", "ray_shadows_local", shots / "local_gl_rt.png", "rt")
    claim("using shadow maps" in llog_gl,
          "OpenGL asked for --raytracing=on on the local fixture reports it is using shadow maps")
    gl_spot = iou(local_dark(lgl, SPOT), local_dark(lmp, SPOT))
    gl_point = iou(local_dark(lgl, POINT), local_dark(lmp, POINT))
    claim(min(gl_spot, gl_point) >= MIN_IOU,
          f"and its spot and point maps agree with Vulkan's, IoU {gl_spot:.3f} / {gl_point:.3f}")

    print()
    if failures:
        print(f"FAIL: {failures} claim(s) did not hold")
        sys.exit(1)
    print("OK: traced and mapped shadows agree in shape for the sun, a spot and a point light, "
          "the traced edge is hard, the floor does not shadow itself, the fox's traced shadow "
          "is its pose and follows it, the maps' budget is the maps', frames reproduce, and "
          "OpenGL falls back and says so")


if __name__ == "__main__":
    main()
