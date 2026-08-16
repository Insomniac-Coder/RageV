#!/usr/bin/env python3
"""Ray-traced shadows (8.12), against the cascaded maps they sit beside.

The fixture (make_ray_shadow_scene) is a grey floor under a low sun that
shines toward the camera, a near box, and a far wall a hundred and fifty
metres out whose shadow reaches back across the ground. Both methods render
it on Vulkan; the maps render it on OpenGL. ENGINE-NOTES 7am.

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
5. **OpenGL takes the maps and says so.** `--shadows=rt` on a backend with
   no ray queries logs the fallback, renders, and exits clean; its shadow
   agrees in shape with Vulkan's maps.

Falsified by negating the ray direction in the shader: every shadowed pixel
lights and IoU collapses to zero.

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
MIN_IOU = 0.9
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


def run(exe, args):
    result = subprocess.run([str(exe), *args], cwd=exe.parent,
                            capture_output=True, text=True)
    return result.returncode, (result.stdout or "") + (result.stderr or "")


def shoot(exe, backend, path, mode, failures):
    code, log = run(exe, [f"--rhi={backend}", "--scene=scenes/ray_shadows.rage",
                          "--frame-time=0.016666", f"--screenshot-frame={FRAME}",
                          f"--screenshot={path}", "--aa=none", f"--shadows={mode}"])
    if code != 0 or not pathlib.Path(path).exists():
        print(f"FAIL: {backend} --shadows={mode} exited {code} / no image")
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
    import postprofile

    scene = root / "SampleProject" / "assets" / "scenes" / "ray_shadows.rage"
    handle = postprofile.write_beside(scene, {"BloomEnabled": False})
    scene.write_text(make_ray_shadow_scene.build(handle))

    shots = root / "build" / "rayshadows"
    shots.mkdir(parents=True, exist_ok=True)

    failures = 0

    def claim(ok, text):
        nonlocal failures
        print(("  pass  " if ok else "  FAIL  ") + text)
        failures += 0 if ok else 1

    rt, log_rt = shoot(exe, "vulkan", shots / "vk_rt.png", "rt", failures)
    rt2, _ = shoot(exe, "vulkan", shots / "vk_rt2.png", "rt", failures)
    mp, log_mp = shoot(exe, "vulkan", shots / "vk_maps.png", "maps", failures)
    mp2, _ = shoot(exe, "vulkan", shots / "vk_maps2.png", "maps", failures)

    claim("directional shadows traced" in log_rt, "Vulkan with --shadows=rt reports tracing")
    claim("directional shadows traced" not in log_mp, "and with --shadows=maps does not")

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

    gl, log_gl = shoot(exe, "opengl", shots / "gl_rt.png", "rt", failures)
    claim("using shadow maps" in log_gl,
          "OpenGL asked for --shadows=rt reports it is using shadow maps")
    gl_iou = iou(dark(gl, NEAR), dark(mp, NEAR))
    claim(gl_iou >= MIN_IOU, f"and its mapped shadow agrees with Vulkan's, IoU {gl_iou:.3f}")

    print()
    if failures:
        print(f"FAIL: {failures} claim(s) did not hold")
        sys.exit(1)
    print("OK: traced and mapped shadows agree in shape near and far, the traced edge is hard, "
          "the floor does not shadow itself, both reproduce, and OpenGL falls back and says so")


if __name__ == "__main__":
    main()
