#!/usr/bin/env python3
"""Bindless (8.2): the two material paths must agree to the pixel.

On Vulkan both paths run -- --bindless=off compiles the bound PBR variant on
a device that has the heap -- so the demo scene rendered each way is one
comparison that covers the whole fork end to end: heap, records, indices,
the shader's two front doors, and the batching that no longer keys on the
material. ENGINE-NOTES 7al.

1. **On equals off, to the pixel, on Vulkan.** Zero differing pixels. Not a
   tolerance: the two variants sample identical texels through identical
   samplers and shade with identical code, so any difference is a bug in one
   of them. On its first run this failed on every wall -- the *bound* path
   had been merging two materials that share their maps and differ in
   tiling, and drawing the walls at the plinth's tiling. That is what a
   second implementation is for.

   Falsified by making the record's normal-map slot read the base colour:
   1,432,357 of 1,440,000 pixels differ. **The honest limit:** it covers the
   maps the courtyard samples -- base colour, normal, occlusion, roughness,
   height. A wrong emissive, metallic or specular slot is invisible here,
   because no courtyard material has one and the shader only reads a map
   whose flag is set; the same swap on the emissive slot passed with zero
   pixels differing. CheckBindlessHeap in scenetest samples every slot it
   registers, but through a compute shader, not through the material.
2. **Each path reproduces.** Two runs of the same flag are identical, so a
   difference in (1) cannot be dismissed as noise. (Two runs of the same
   flag differed once by 16k pixels, all inside one UI button: the mouse was
   over it. Keep the pointer off the window.)
3. **OpenGL takes the bound path and says so.** No heap on that backend by
   design; the log states it, the frame renders, the process exits clean.
4. **GPU-assisted validation is quiet.** The only check that sees an
   out-of-range index into a bindless array; ordinary validation cannot know
   which element a shader will read. Under it, a deliberately bad index
   reports "Index of N used to index descriptor array of length 4096"
   (verified by hand, then reverted). The layer's own "adjusting settings"
   advisories at startup are the one thing this run is allowed to print.

Usage:
    python tools/scripts/check_bindless.py [--config Release]
"""

import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

FRAME = 30


def run(exe, args):
    result = subprocess.run([str(exe), *args], cwd=exe.parent,
                            capture_output=True, text=True)
    return result.returncode, (result.stdout or "") + (result.stderr or "")


def shoot(exe, path, *extra):
    code, log = run(exe, ["--rhi=vulkan", "--frame-time=0.016666",
                          f"--screenshot-frame={FRAME}", f"--screenshot={path}",
                          "--aa=none", *extra])
    if code != 0:
        print(f"FAIL: {' '.join(extra)} exited {code}")
        print(log[-2000:])
        sys.exit(1)
    if not pathlib.Path(path).exists():
        print(f"FAIL: {path} was never written")
        sys.exit(1)
    return np.asarray(Image.open(path).convert("RGB")).astype(int), log


def differing(a, b):
    return int((np.abs(a - b).max(axis=2) > 0).sum())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[2]
    exe = root / "build" / "bin" / args.config / "RageVRuntime" / "RageVRuntime.exe"
    if not exe.exists():
        print(f"FAIL: {exe} does not exist; build {args.config} first")
        sys.exit(1)

    shots = root / "build" / "bindless"
    shots.mkdir(parents=True, exist_ok=True)

    failures = 0

    def claim(ok, text):
        nonlocal failures
        print(("  pass  " if ok else "  FAIL  ") + text)
        failures += 0 if ok else 1

    # -- 1 & 2: on against off, on Vulkan, twice each --------------------------
    on1, log_on = shoot(exe, shots / "on1.png")
    on2, _ = shoot(exe, shots / "on2.png")
    off1, log_off = shoot(exe, shots / "off1.png", "--bindless=off")
    off2, _ = shoot(exe, shots / "off2.png", "--bindless=off")

    claim("through the bindless heap" in log_on,
          "Vulkan with --bindless=on reports the heap in use")
    claim("bound per material (heap disabled)" in log_off,
          "Vulkan with --bindless=off reports the bound path")

    on_repeat = differing(on1, on2)
    off_repeat = differing(off1, off2)
    claim(on_repeat == 0, f"the bindless path reproduces run to run ({on_repeat} px differ)")
    claim(off_repeat == 0, f"the bound path reproduces run to run ({off_repeat} px differ)")

    across = differing(on1, off1)
    total = on1.shape[0] * on1.shape[1]
    claim(across == 0, f"bindless on equals bindless off: {across} of {total} px differ")
    if across:
        d = np.abs(on1 - off1).max(axis=2)
        Image.fromarray(np.clip(d * 3, 0, 255).astype(np.uint8)).save(shots / "diff.png")
        print(f"        difference map written to {shots / 'diff.png'}")

    # -- 3: OpenGL ---------------------------------------------------------------
    code, log = run(exe, ["--rhi=opengl", "--frame-time=0.016666",
                          f"--screenshot-frame={FRAME}",
                          f"--screenshot={shots / 'gl.png'}", "--aa=none"])
    claim(code == 0, f"OpenGL exits clean ({code})")
    claim("bound per material (no descriptor indexing)" in log,
          "and reports that it takes the bound path for lack of descriptor indexing")
    claim((shots / "gl.png").exists(), "and renders the frame")

    # -- 4: GPU-assisted validation ----------------------------------------------
    code, log = run(exe, ["--rhi=vulkan", "--validation=gpu", "--frame-time=0.016666",
                          f"--screenshot-frame={FRAME}",
                          f"--screenshot={shots / 'gpuav.png'}", "--aa=none"])
    claim(code == 0, f"GPU-assisted validation run exits clean ({code})")
    claim("GPU-assisted validation enabled" in log, "and the layer confirms it was on")
    lines = [l for l in log.splitlines()
             if "[Vulkan]" in l and "adjusting settings" not in l]
    claim(not lines, f"and reports nothing ({len(lines)} [Vulkan] lines beyond the layer's own advisories)")
    for l in lines[:5]:
        print("        " + l.strip())

    print()
    if failures:
        print(f"FAIL: {failures} claim(s) did not hold")
        sys.exit(1)
    print("OK: bindless on and off agree to the pixel; OpenGL takes the bound path; GPU-AV is quiet")


if __name__ == "__main__":
    main()
