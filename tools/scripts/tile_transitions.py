"""WR-16 S3: the sixty-second still test -- how often the allocator changes its mind.

The ray budget hands every 16x16 tile a whole number of rays, and a whole
number is a stair. `budget.Advance()` -- the damping that eases a tile toward
its target rather than snapping to it -- has been restored and reverted three
times, on three different versions of the surrounding code, and made the
picture worse every time. The note where it lives
(FrameGraphBuilder.cpp, "budget.Advance() belongs here and is deliberately
absent") says why: a damped integrator feeding a quantiser is the classic slow
limit cycle, and this engine has already met one, breathing at about a hertz.
Damping the input to a stair does not stop the stair being climbed; it makes
the climbing rhythmic.

So the thing to measure is not frame time and not a diff against a reference.
It is **how often a tile changes its allocation while nothing in the scene is
moving**. On a still camera, in a still scene, the right answer is "almost
never": every change is the allocator talking to itself.

    python tools/scripts/tile_transitions.py
    python tools/scripts/tile_transitions.py --seconds 60 --camera Pier
    python tools/scripts/tile_transitions.py --keep-frames   (leave the pngs)

**What it reads.** `--debug-view=importance` draws the allocator's tile map
itself, and its red channel is the AO rays per pixel that tile was given
(debug_view.rvshader: `value = texture(u_Aux, uv).r`). One sample at the middle
of each tile, every frame, and a change between consecutive frames is a
transition. The colours are a ramp over the count, so a changed colour means a
changed count; the reverse can fail only if two adjacent integers land on the
same ramp value, which does not happen for the small counts a budget hands out.

**The bar, from the design**: under one tile change in a hundred per second --
0.01 transitions per tile per second. Over sixty seconds that is a tile
changing its mind less than once in every one and a half tiles.

**The clock is pinned to a fixed step** rather than left on the wall clock, so
sixty seconds means the same sixty seconds on every machine and a slow frame
does not quietly shorten the test. The memory of the flicker work applies here
too: `--frame-time=0` is the wall clock and not frozen at all.
"""
import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import shadow_ray_matrix as matrix  # noqa: E402

ROOT = matrix.ROOT
RUNTIME = matrix.RUNTIME
CAMERAS = matrix.CAMERAS

# The allocator's tile is 16x16 screen pixels (PostProcess::ImportanceTiles).
TILE = 16
# The bar the design sets: 0.01 changes per tile per second.
BAR = 0.01


def capture(camera, width, height, seconds, fps, stem, extra):
    """Run the scene still, with the clock pinned, and write one png a frame."""
    frames = int(round(seconds * fps))
    cmd = [str(RUNTIME), f"--project={ROOT / 'SampleProject'}",
           "--scene=scenes/GoldenGateDemo.rage", "--rhi=vulkan",
           "--render-defaults=off", "--vsync=off",
           f"--width={width}", f"--height={height}",
           f"--camera={camera}",
           "--debug-view=importance",
           f"--frame-time={1.0 / fps:.8f}",
           # Started after the allocator has settled from the first frame, so
           # the count is the steady state rather than the warm-up.
           "--screenshot-frame=60",
           f"--screenshot-count={frames}",
           f"--screenshot={stem}.png"] + list(extra)
    proc = subprocess.run(cmd, cwd=RUNTIME.parent, capture_output=True, text=True,
                          timeout=3600, errors="replace")
    return proc.returncode, proc.stdout + "\n--- stderr ---\n" + proc.stderr, frames


def tile_samples(path, width, height):
    """One pixel from the middle of every tile."""
    image = np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)
    rows = np.arange(TILE // 2, height, TILE)
    cols = np.arange(TILE // 2, width, TILE)
    rows = rows[rows < image.shape[0]]
    cols = cols[cols < image.shape[1]]
    return image[np.ix_(rows, cols)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--camera", default="Headland",
                        help="a name from shadow_ray_matrix.CAMERAS, or x,y,z,d,yaw,pitch")
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument("--fps", type=float, default=60.0)
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=450)
    parser.add_argument("--out", default=str(ROOT / "build" / "tile_transitions"))
    parser.add_argument("--keep-frames", action="store_true",
                        help="leave the captured pngs behind; they are deleted otherwise")
    parser.add_argument("--analyse-only", action="store_true",
                        help="no runtime: count from the pngs already in --out")
    parser.add_argument("extra", nargs="*",
                        help="anything else to pass the runtime, e.g. --rt-optimisation=off")
    args = parser.parse_args()

    camera = CAMERAS.get(args.camera, args.camera)
    out = pathlib.Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    stem = out / "tile"

    if not args.analyse_only:
        if not RUNTIME.exists():
            sys.exit(f"no runtime at {RUNTIME}; build Release first")
        if "RageVEditor" in subprocess.run(["tasklist"], capture_output=True,
                                           text=True).stdout:
            sys.exit("RageVEditor.exe is running; close it first")
        for old in out.glob("tile_*.png"):
            old.unlink()

        print(f"capturing {args.seconds:g}s at {args.fps:g} fps "
              f"({int(round(args.seconds * args.fps))} frames)...", flush=True)
        code, log, frames = capture(camera, args.width, args.height,
                                    args.seconds, args.fps, str(stem), args.extra)
        (out / "capture.log").write_text(log, encoding="utf-8")
        # The one failure that looks like a pass: the view runs, draws a dark
        # map, and every tile reads the same thing for ever -- zero
        # transitions, which is the answer the test is looking for.
        if "has no source this frame" in log:
            sys.exit("the ray budget's tile allocator is off, so the map is dark and a\n"
                     "count of zero would mean nothing. Turn the budget on and re-run.\n"
                     f"log: {out / 'capture.log'}")
        if code != 0:
            sys.exit(f"the runtime exited {code}; see {out / 'capture.log'}")

    files = sorted(out.glob("tile_*.png"))
    if len(files) < 2:
        sys.exit(f"{len(files)} frames in {out}; nothing to compare")

    previous = tile_samples(files[0], args.width, args.height)
    changes = np.zeros(previous.shape[:2], dtype=np.int64)
    for path in files[1:]:
        current = tile_samples(path, args.width, args.height)
        changes += np.any(current != previous, axis=-1)
        previous = current

    seconds = (len(files) - 1) / args.fps
    tiles = changes.size
    per_tile_second = changes.sum() / (tiles * seconds)
    moved = int((changes > 0).sum())

    print()
    print(f"{len(files)} frames, {seconds:.1f}s of scene time, {tiles} tiles")
    print(f"  transitions in all           {int(changes.sum())}")
    print(f"  per tile per second          {per_tile_second:.4f}   (bar: under {BAR})")
    print(f"  tiles that ever changed      {moved} ({100.0 * moved / tiles:.1f}%)")
    print(f"  the busiest tile changed     {int(changes.max())} times")
    print(f"  VERDICT: {'PASS' if per_tile_second < BAR else 'FAIL'}")

    # A picture of where the allocator is restless, which a number cannot show:
    # a limit cycle is usually a region, not a scatter.
    if changes.max() > 0:
        heat = (changes.astype(np.float64) / changes.max() * 255.0).astype(np.uint8)
        Image.fromarray(heat).resize((changes.shape[1] * 4, changes.shape[0] * 4),
                                     Image.NEAREST).save(out / "transitions.png")
        print(f"  where: {out / 'transitions.png'}")

    if not args.keep_frames and not args.analyse_only:
        for path in files:
            path.unlink()


if __name__ == "__main__":
    main()
