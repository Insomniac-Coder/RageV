"""Alpha-cutout materials: does the cutoff cull part of a surface, and only part?

The scene is one cube with one masked material whose base colour texture is
opaque on its left half and alpha 0 on its right. That shape is the whole
point: a cutout that removed the *object* would pass a presence test just as
well as one that removed the right texels, and only the first of those is a
cutout. So this asks where the hole is, not whether one exists.

Three claims.

**Half is culled and the other half is not.** The right of the cube reads as
floor, the left reads as cube. Either half being wrong is a failure, and the
two together are what separates a cutout from an object that vanished.

**A surviving fragment is an opaque fragment.** The masked variant is the lit
shader plus one test, so the half that survives must shade *byte for byte*
like the same material declared Opaque. That is exact, and it is what catches
the variant drifting from the shader it copies -- a define that changed the
lighting, a set bound against the wrong pipeline, a pipeline state that did
not match.

**The prepass wrote no depth where the fragment was discarded.** A hole whose
depth was already laid down occludes what is behind it, and that is invisible
unless something *is* behind. The floor is what is behind, so a prepass that
included the cutout shows up as a gap in the floor rather than as a missing
half.

Both backends, deliberately. The first version of this feature was perfect on
Vulkan and drew solid black on OpenGL, because `discard` at SPIR-V 1.6 lowers
to an instruction that has no OpenGL form and the whole fragment stage failed
to cross-compile. Nothing but running it on both would have said so.

Usage:
    python tools/scripts/check_cutout.py [--config Release] [--rhi vulkan]
"""

import argparse
import io
import pathlib
import subprocess
import sys

try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("FAIL: this check needs Pillow and numpy")
    sys.exit(1)

SCENE = "scenes/cutout_test.rage"
SHADOW_SCENE = "scenes/cutout_shadow_test.rage"
MATERIAL = pathlib.Path("SampleProject/assets/materials/cutout_test.rmat")

# The material as authored: a half-transparent base colour against a cutoff of
# 0.5, so exactly the right half of every face fails the test.


def read(path):
    return io.open(path, encoding="utf-8", newline="").read()


def write(path, text):
    with io.open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def shoot(exe, project, backend, out, extra=(), scene=SCENE):
    args = [str(exe), "--render-defaults=on", f"--rhi={backend}",
            f"--project={project}", f"--scene={scene}", "--frame-time=0.0166",
            "--screenshot-frame=20", f"--screenshot={out}", "--aa=none", *extra]
    result = subprocess.run(args, cwd=exe.parent, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAIL: {' '.join(args)} exited {result.returncode}")
        print(result.stdout[-2000:])
        sys.exit(1)
    return np.asarray(Image.open(out).convert("RGB")).astype(int)


def numpy_abs_max(masked, opaque, h, w):
    """Max difference over the half that survived, which must be exact."""
    y0, y1 = int(h * 0.36), int(h * 0.55)
    x0, x1 = int(w * 0.41), int(w * 0.47)
    return int(np.abs(masked[y0:y1, x0:x1] - opaque[y0:y1, x0:x1]).max())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="Release")
    parser.add_argument("--rhi", default="vulkan")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[2]
    exe = root / "build" / "bin" / args.config / "RageVRuntime" / "RageVRuntime.exe"
    project = root / "SampleProject" / "SampleProject.rvproject"
    material = root / MATERIAL
    shots = root / "build" / "cutout"
    shots.mkdir(parents=True, exist_ok=True)

    if not exe.exists():
        print(f"FAIL: {exe} does not exist; build {args.config} first")
        return 1

    original = read(material)
    if "Blend: Masked" not in original:
        print(f"FAIL: {MATERIAL} is not in its authored state (Blend: Masked)")
        return 1

    try:
        # As authored: masked, with the half-transparent base colour.
        masked = shoot(exe, project, args.rhi, shots / f"{args.rhi}-masked.png")

        # The control: the same material, declared Opaque. Nothing is tested,
        # so both halves survive -- which is what the left half has to match
        # and what proves the right half's geometry was really there.
        write(material, original.replace("Blend: Masked", "Blend: Opaque"))
        opaque = shoot(exe, project, args.rhi, shots / f"{args.rhi}-opaque.png")
    finally:
        write(material, original)

    failures = []
    h, w, _ = masked.shape

    # Three patches: the cube's two halves, and open floor well away from it.
    def patch(image, x0, x1, y0=0.36, y1=0.55):
        return image[int(h * y0):int(h * y1), int(w * x0):int(w * x1)].mean()

    left_m, right_m = patch(masked, 0.41, 0.47), patch(masked, 0.51, 0.57)
    left_o, right_o = patch(opaque, 0.41, 0.47), patch(opaque, 0.51, 0.57)
    floor = patch(masked, 0.10, 0.20, 0.75, 0.85)

    print(f"{args.rhi}: masked  left {left_m:6.1f}  right {right_m:6.1f}")
    print(f"{args.rhi}: opaque  left {left_o:6.1f}  right {right_o:6.1f}")
    print(f"{args.rhi}: open floor reads {floor:6.1f}")

    # The control must show a cube on both halves, or the scene proves nothing.
    if abs(right_o - floor) < 5.0:
        failures.append("the opaque control shows no cube on the right; the scene is wrong")

    # The right half is gone, and what shows through is floor rather than a hole.
    if abs(right_m - floor) > 2.0:
        failures.append(f"the right half was not culled, or the prepass left depth "
                        f"behind it (reads {right_m:.1f}, floor is {floor:.1f})")

    # The left half is still there, and shades exactly as the opaque control.
    if abs(left_m - floor) < 5.0:
        failures.append("the left half was culled too; the cutoff is eating everything")

    delta = numpy_abs_max(masked, opaque, h, w)
    print(f"{args.rhi}: surviving half vs opaque control: max channel diff {delta}")
    if delta != 0:
        failures.append("a surviving cutout does not shade like the opaque material it copies")

    # --- the shadow, which is the other half of "the alpha decides" ---------
    #
    # A separate scene, because it needs what the first one must not have: a
    # light, a floor that is not emissive, and a caster floating clear of it.
    # It is also a single-face quad rather than a cube, and that is not
    # incidental -- the shadow pass culls nothing, so a *closed box* casts
    # from its back face too, whose UVs run the other way in x. The union of
    # the two covers the whole width and the cutout looks broken when it is
    # not. A real cutout asset is a plane; so is this.
    try:
        shadow_masked = shoot(exe, project, args.rhi,
                              shots / f"{args.rhi}-shadow-masked.png", scene=SHADOW_SCENE)
        write(material, original.replace("Blend: Masked", "Blend: Opaque"))
        shadow_opaque = shoot(exe, project, args.rhi,
                              shots / f"{args.rhi}-shadow-opaque.png", scene=SHADOW_SCENE)
    finally:
        write(material, original)

    def shadow_span(image):
        """Where the shadow reaches across a floor band below the caster."""
        sh, sw, _ = image.shape
        row = image[int(sh * 0.31):int(sh * 0.38)].mean(axis=(0, 2))
        dark = np.where(row < 100)[0]
        return (int(dark.min()), int(dark.max())) if len(dark) else None

    span_m, span_o = shadow_span(shadow_masked), shadow_span(shadow_opaque)
    if span_m is None or span_o is None:
        failures.append("no shadow at all in one of the two runs; the scene or the light moved")
    else:
        wide_m = span_m[1] - span_m[0] + 1
        wide_o = span_o[1] - span_o[0] + 1
        print(f"{args.rhi}: shadow masked x {span_m[0]}..{span_m[1]} ({wide_m} px), "
              f"opaque x {span_o[0]}..{span_o[1]} ({wide_o} px)")

        # Half the caster is transparent, so half the shadow should be gone --
        # and gone from the correct end, which is what the shared left edge
        # says. A cutout that cast nothing would also halve nothing; a cutout
        # ignored by the shadow pass would match the control exactly.
        ratio = wide_m / max(wide_o, 1)
        if not 0.35 < ratio < 0.65:
            failures.append(f"the cutout casts {ratio:.2f} of the solid shadow, expected about "
                            f"half -- 1.0 means the shadow pass ignored the alpha, "
                            f"near 0 means it discarded everything")
        if abs(span_m[0] - span_o[0]) > 6:
            failures.append("the masked shadow does not start where the solid one does; "
                            "the surviving half is the wrong half")

    # --- and the same again with rays, which is a different mechanism -------
    #
    # The shadow above is a shadow *map*: a depth pass with a fragment stage.
    # With ray tracing on there is no map -- the light is a ray query, and the
    # alpha test moves inside the traversal loop, where the engine has no
    # any-hit stage and has to confirm each candidate by hand. Same picture,
    # entirely different code, so it gets its own measurement.
    #
    # Reflections are switched on because that is the variant carrying the
    # ray-instance table the test reads its cutoff and its UVs from.
    if args.rhi == "vulkan":
        rt = ("--raytracing=on", "--rt-reflections=on")
        try:
            traced_masked = shoot(exe, project, args.rhi,
                                  shots / f"{args.rhi}-traced-masked.png",
                                  extra=rt, scene=SHADOW_SCENE)
            write(material, original.replace("Blend: Masked", "Blend: Opaque"))
            traced_opaque = shoot(exe, project, args.rhi,
                                  shots / f"{args.rhi}-traced-opaque.png",
                                  extra=rt, scene=SHADOW_SCENE)
        finally:
            write(material, original)

        span_tm, span_to = shadow_span(traced_masked), shadow_span(traced_opaque)
        if span_tm is None or span_to is None:
            failures.append("no traced shadow in one of the two runs")
        else:
            wide_tm = span_tm[1] - span_tm[0] + 1
            wide_to = span_to[1] - span_to[0] + 1
            print(f"{args.rhi}: traced shadow masked ({wide_tm} px), opaque ({wide_to} px)")
            ratio = wide_tm / max(wide_to, 1)
            if not 0.35 < ratio < 0.65:
                failures.append(f"the traced shadow is {ratio:.2f} of the solid one, expected "
                                f"about half -- 1.0 means the traversal loop confirmed every "
                                f"candidate, near 0 means it rejected them all")

    if failures:
        for line in failures:
            print("FAIL:", line)
        return 1

    print("\nOK: half the surface is culled and half is not, the surviving half shades "
          "exactly as opaque, the prepass left no depth behind the hole, and the "
          "half that survives is the half that casts a shadow")
    return 0


if __name__ == "__main__":
    sys.exit(main())
