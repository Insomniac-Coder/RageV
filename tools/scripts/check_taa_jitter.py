#!/usr/bin/env python3
"""The temporal jitter, and the property the rest of the test suite rests on.

TAA works by drawing every frame through a slightly different sub-pixel
offset and accumulating the results. That breaks an assumption every other
screenshot check in this repository makes without saying so: **that frame 30
is a pure function of the scene**. `--screenshot-frame=30` exists so the scene
has settled; `--frame-time` exists so nothing depends on how fast this machine
ran. Both assume you could render frame 30 alone and get the same picture.

With a temporal filter that is no longer true, and there is exactly one way to
keep it useful: index the jitter by the **frame number**, never by elapsed
time. Drive it from a clock and every check in this directory becomes
irreproducible, and the failure looks like noise rather than like a mistake.

So this measures that, and it is the whole point of the file:

**1. Reproducible.** Two separate runs of the same build, same frame, must be
byte-identical. A clock anywhere in the sequence fails here.

**2. Periodic.** The sequence is eight offsets long, so frame 30 and frame 38
are drawn through the same offset and must be far closer to each other than
frame 30 and frame 31 are. Not identical -- they accumulated 29 frames and 37
frames respectively, and the difference between those is what is left of the
first frame after each. This is the check a clock cannot accidentally pass: a
time-driven jitter can be reproducible run-to-run under `--frame-time` and
still have no period at all.

**3. ~~Eight distinct offsets.~~ Deleted by CHK.2 (ENGINE-NOTES 7ba).** It
asked that frames 30 to 37 all differ from each other, so that two frames
landing on the same offset would show. Since TAA.4 every frame carries a
different accumulation, so no two frames are ever identical *whatever* the
offsets -- the claim was measured against a sequence that advanced one offset
every two frames and stayed green. A claim that cannot fail is not a claim;
and claim 2 already catches the same defect, because a frame one period away
is then no closer than a frame one step away (0.0491 against 0.0150, measured
on that break).

**4. It only moves edges.** A half-pixel shift of a flat region is still the
same flat region, so every pixel away from an edge must be bit-identical to
`--aa=none`. This is what separates "the projection is offset" from "the
image is wrong": a jitter applied to some renderers and not others, or in the
wrong units, moves things that should not move.

And the control, which comes first: with `--aa=none` the same scene at frames
30, 31 and a second run must all be identical. If that fails, the scene is not
static and nothing below it is evidence.

Usage:
    python tools/scripts/check_taa_jitter.py [--config Release]
"""

import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

BACKENDS = ("vulkan", "opengl")

# Must match kJitterPhase in FrameGraphBuilder.cpp. Stated rather than
# imported because the point is to catch the two disagreeing.
PHASE = 8

# A shallow edge, so a horizontal sub-pixel offset moves a long run of pixels
# and the difference is unmistakable. The angle does not otherwise matter --
# this file measures reproducibility, not quality.
ANGLE = 8

# Pixels this far from the edge are not edge pixels, and a sub-pixel offset
# must leave them exactly alone.
FLAT_DISTANCE = 4.0

# How much closer a frame one whole period away has to be than a frame one
# step away. A floor to catch the sequence not being periodic at all, not a
# grade -- the measured ratio is far above it.
PERIOD_MARGIN = 3.0


def run(exe, args):
    result = subprocess.run([str(exe), "--render-defaults=on", *args], cwd=exe.parent,
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAIL: {' '.join(args)} exited {result.returncode}")
        print(result.stdout[-2000:], result.stderr[-2000:])
        sys.exit(1)
    return result.stdout + result.stderr


def shoot(exe, backend, mode, frame, path):
    run(exe, [f"--rhi={backend}", f"--aa={mode}",
              f"--scene=scenes/aa_edge{ANGLE}.rage",
              "--frame-time=0.0166", f"--screenshot-frame={frame}",
              f"--screenshot={path}"])
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int16)


def differing(a, b):
    """How many subpixels differ, and by how much at worst."""
    delta = np.abs(a - b)
    return int((delta > 0).sum()), int(delta.max())


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
    import make_aa_scene
    import postprofile

    scenes = root / "SampleProject" / "assets" / "scenes"
    shots = root / "build" / "taa-check"
    shots.mkdir(parents=True, exist_ok=True)

    # Rewritten every run, like check_smaa.py: a check that depends on a file
    # somebody generated once is a check that silently measures another scene.
    scene = scenes / f"aa_edge{ANGLE}.rage"
    profile = postprofile.write_beside(scene, { "BloomEnabled": False })
    scene.write_text(make_aa_scene.build(ANGLE, profile))

    failures = []

    for backend in BACKENDS:
        print(f"--- {backend} " + "-" * 52)

        # The control. Everything below is a claim about what the jitter
        # changed, and a claim like that is worthless if the scene moves on
        # its own.
        flat30 = shoot(exe, backend, "none", 30, shots / f"{backend}-none-30.png")
        flat30b = shoot(exe, backend, "none", 30, shots / f"{backend}-none-30b.png")
        flat31 = shoot(exe, backend, "none", 31, shots / f"{backend}-none-31.png")

        for label, other in (("a second run", flat30b), ("frame 31", flat31)):
            count, worst = differing(flat30, other)
            if count:
                failures.append(
                    f"{backend}: with no filter at all, frame 30 and {label} differ "
                    f"in {count} subpixels (worst {worst}). The scene is not static, "
                    f"so nothing this file measures about the jitter is evidence")
            else:
                print(f"  control: none@30 == none@{label:14s}  identical")

        # Eight frames, one per offset in the sequence.
        frames = {n: shoot(exe, backend, "taa", n, shots / f"{backend}-taa-{n}.png")
                  for n in range(30, 30 + PHASE)}

        # 1. Reproducible across processes.
        again = shoot(exe, backend, "taa", 30, shots / f"{backend}-taa-30b.png")
        count, worst = differing(frames[30], again)
        if count:
            failures.append(
                f"{backend}: two runs of taa@30 differ in {count} subpixels "
                f"(worst {worst}). The jitter is not a function of the frame "
                f"number alone -- a clock has got into it, and every screenshot "
                f"check in this directory is now irreproducible")
        else:
            print("  taa@30 == taa@30, run twice        identical")

        # 2. Periodic -- but no longer *identical*, and the difference is the
        #    whole of what TAA.4 changed.
        #
        # Frames 30 and 38 are drawn through the same offset. They are not the
        # same picture, because 30 has accumulated 29 frames and 38 has
        # accumulated 37, and the two accumulations differ by whatever is left
        # of the first frame -- 0.9^30 against 0.9^38, a few percent apart.
        # This check demanded byte-equality while the jitter had nothing to
        # accumulate into, and that premise died with the history buffer.
        #
        # What survives is scale-free and stronger for it: **a frame one
        # period away must be far closer than a frame one step away.** A clock
        # cannot satisfy that -- frame 38 would land on an unrelated offset
        # and be no nearer than any other frame.
        wrapped = shoot(exe, backend, "taa", 30 + PHASE,
                        shots / f"{backend}-taa-{30 + PHASE}.png")

        same_offset = float(np.abs(frames[30] - wrapped).mean())
        next_offset = float(np.abs(frames[30] - frames[31]).mean())

        if same_offset * PERIOD_MARGIN >= next_offset:
            failures.append(
                f"{backend}: taa@30 differs from taa@{30 + PHASE} by {same_offset:.4f} "
                f"per subpixel and from taa@31 by {next_offset:.4f}. One period away "
                f"should be far closer than one frame away, and it is not -- so the "
                f"sequence is not indexed by the frame number with a period of {PHASE}")
        else:
            print(f"  taa@30 vs taa@{30 + PHASE}: {same_offset:.4f} per subpixel, "
                  f"vs {next_offset:.4f} one frame away  ({next_offset / max(same_offset, 1e-9):.0f}x)")

        # 3 was "eight distinct offsets" -- every pair of the eight frames
        # byte-different -- and it is gone, because under accumulation every
        # pair is byte-different whatever the offsets: CHK.2 indexed the
        # sequence by frame / 2, two frames per offset, and the claim stayed
        # green. The period claim above catches that break (7ba).

        # 4. It moves edges and only edges.
        #
        # The edge is found from the unfiltered image the same way
        # check_smaa.py finds it -- column sums are exact regardless of what
        # filtered the image, because each is the number of covered rows.
        import check_smaa

        unfiltered = check_smaa.luma(shots / f"{backend}-none-30.png")
        slope, intercept, staircase, _ = check_smaa.fit_edge(unfiltered)
        if abs(staircase - check_smaa.QUANTISATION_RMS) > 0.01:
            failures.append(
                f"{backend}: the unfiltered staircase measured {staircase:.4f} px "
                f"rather than {check_smaa.QUANTISATION_RMS:.4f}. The edge was not "
                f"found, so the flat-region claim below is measuring nothing")

        height, width = unfiltered.shape
        rows, cols = np.mgrid[0:height, 0:width]
        # Vertical distance to the edge, in pixels, scaled to a perpendicular
        # one so a steep edge does not get a wider band than a shallow one.
        vertical = rows - (slope * (cols + 0.5) + intercept)
        distance = np.abs(vertical) / np.sqrt(1.0 + slope * slope)
        flat = distance > FLAT_DISTANCE

        moved = 0
        for n, image in frames.items():
            delta = np.abs(image - flat30)   # against no filter at all
            count = int((delta.max(axis=2) > 0).sum())
            leaked = int((delta.max(axis=2)[flat] > 0).sum())
            moved += count
            if leaked:
                worst = int(delta.max(axis=2)[flat].max())
                failures.append(
                    f"{backend}: taa@{n} differs from no filter at all in {leaked} "
                    f"pixels more than {FLAT_DISTANCE} px from the edge (worst "
                    f"{worst}). A sub-pixel offset of a flat region is the same flat "
                    f"region -- something is being moved that should not be, or is "
                    f"being moved in the wrong units")

        if moved == 0:
            failures.append(
                f"{backend}: taa is byte-identical to no filter at all on every one "
                f"of the {PHASE} frames. The jitter is not reaching the projection")
        else:
            print(f"  jitter moves {moved // PHASE:6d} pixels/frame  none of them flat")

        # There is deliberately no convergence check here.
        #
        # One was written and removed, and the reason is worth keeping. The
        # idea was that an accumulation settles, so consecutive frames late in
        # the sequence should differ less than consecutive frames early in it.
        # Measured: 0.0044 per subpixel at frame 12 and 0.0047 at frame 60 --
        # the wrong way round, on both backends, with a working filter.
        #
        # It was measuring the 8-bit output quantisation. A mean of 0.0045
        # across 4.3M subpixels is about 19,000 of them landing on the other
        # side of a rounding boundary, which is not a signal that decays. And
        # frames 12 and 60 sit at the same point of an 8-long sequence, as do
        # 13 and 61, so the jitter contributed identically to both pairs and
        # the whole difference was noise.
        #
        # Accumulation is already measured properly, against a computed ideal
        # rather than against another render, by check_smaa.py: TAA scores 2.9
        # to 5.1 times better than no filter on the linear-light yardstick,
        # and a filter that dropped its history could not. A second, worse
        # test of the same thing is worth less than none.

    print()
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print(f"OK: the jitter is a function of the frame number, repeats every "
          f"{PHASE} frames, and moves nothing but edges")


if __name__ == "__main__":
    main()
