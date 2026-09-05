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
    python tools/scripts/tile_transitions.py --lane gi
    python tools/scripts/tile_transitions.py --keep-frames   (leave the pngs)

**What it reads.** `--debug-view=importance` draws the allocator's tile map
itself, and its red channel is the AO rays per pixel that tile was given.
`--lane gi` asks for `--debug-view=importance-gi` and the bounce's count
instead. **Both lanes have to be graded**: they are allocated separately, from
different averages against different ceilings, so they cross their boundaries
at different moments and a cycle can live in one while the other is still.
One sample at the middle of each tile, every frame, and a change between
consecutive frames is a transition. The colours are a ramp over the count, so a
changed colour means a changed count; the reverse can fail only if two adjacent
integers land on the same ramp value, which does not happen for the small counts
a budget hands out.

**And it asks for `--debug-view-mix=0`, which is the whole reason this test can
be believed.** The debug view normally draws its map over the tone-mapped frame
at a fifth of the frame's brightness, so a map keeps its geography -- and a
fifth of an animated frame is 51 levels a channel. On the night scene the water
moves and the beacons flash, so every tile changed colour every frame no matter
what the allocator did: **Headland read 21.13 changes per tile per second with
the dwell at 31 frames, which by arithmetic cannot exceed 1.94.** That is how
this was caught, and it is why the run now refuses to score a capture whose
tiles are not flat inside.

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


def capture(camera, width, height, seconds, fps, stem, extra, view, scene,
            settle):
    """Run the scene still, with the clock pinned, and write one png a frame."""
    frames = int(round(seconds * fps))
    cmd = [str(RUNTIME), f"--project={ROOT / 'SampleProject'}",
           f"--scene={scene}", "--rhi=vulkan",
           "--render-defaults=off", "--vsync=off",
           f"--width={width}", f"--height={height}",
           *([f"--camera={camera}"] if camera else []),
           f"--debug-view={view}",
           # Exactly the map, nothing of the scene: see the note above.
           "--debug-view-mix=0",
           f"--frame-time={1.0 / fps:.8f}",
           # Started after the allocator has settled from the first frame, so
           # the count is the steady state rather than the warm-up. Sixty was
           # enough when a tile had no memory; the averaged demand needs a
           # multiple of its own time constant, and a setting still converging
           # when the count begins scores as restless (0.0796 against 0.0129,
           # measured, purely from warm-up).
           f"--screenshot-frame={settle}",
           f"--screenshot-count={frames}",
           f"--screenshot={stem}.png"] + list(extra)
    proc = subprocess.run(cmd, cwd=RUNTIME.parent, capture_output=True, text=True,
                          timeout=3600, errors="replace")
    return proc.returncode, proc.stdout + "\n--- stderr ---\n" + proc.stderr, frames


def tile_grid(shape):
    """The row and column of every tile's middle pixel.

    Taken from the image rather than from --width/--height, which are a
    request: the window has a minimum and 800x450 comes back as 800x640, so
    the asked-for height silently graded the top seven tenths of the frame.
    """
    return (np.arange(TILE // 2, shape[0], TILE),
            np.arange(TILE // 2, shape[1], TILE))


def tile_samples(path):
    """One pixel from the middle of every tile."""
    image = np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)
    rows, cols = tile_grid(image.shape)
    return image[np.ix_(rows, cols)]


def flat_tiles(path):
    """Which tiles are one flat colour, which is which tiles the map owns.

    With the mix at zero a tile IS one flat colour: the map is point-sampled
    and a tile is exactly TILE output pixels across. A tile that is not flat
    has something else drawn over it -- the scene showing through a non-zero
    mix, or whatever the runtime paints along the bottom two rows of a
    showroom capture -- and a changed pixel there is that thing changing, not
    the allocation. That failure read as a result for a whole session (21
    changes per tile per second under a dwell that allows 2), so it is
    measured rather than assumed: tiles that are not flat are counted, named
    and left out of the score.
    """
    image = np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)
    rows, cols = tile_grid(image.shape)
    inner_rows = rows[rows + 3 < image.shape[0]]
    inner_cols = cols[cols + 3 < image.shape[1]]
    flat = ~np.any(image[np.ix_(inner_rows, inner_cols)]
                   != image[np.ix_(inner_rows + 3, inner_cols + 3)], axis=-1)
    # A partial tile at the right or bottom edge has no interior to compare
    # against, so it is not scored either.
    full = np.zeros((len(rows), len(cols)), dtype=bool)
    full[:flat.shape[0], :flat.shape[1]] = flat
    return full


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--camera", default="Headland",
                        help="a name from shadow_ray_matrix.CAMERAS, or "
                             "x,y,z,d,yaw,pitch, or empty for the scene's own")
    parser.add_argument("--settle", type=int, default=240,
                        help="frames to let the allocator converge before the "
                             "count begins; at least a few times 1/tile-smooth")
    parser.add_argument("--scene", default="scenes/GoldenGateDemo.rage",
                        help="the design names two: the night water and the "
                             "showroom ceiling. The bar assumes a STILL scene, "
                             "and the bridge's sea is not one -- its tiles' "
                             "importance changes because the water does.")
    parser.add_argument("--lane", choices=("ao", "gi"), default="ao",
                        help="which of the allocator's two counts to grade")
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
                                    args.seconds, args.fps, str(stem), args.extra,
                                    "importance" if args.lane == "ao"
                                    else "importance-gi", args.scene, args.settle)
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

    flat = flat_tiles(files[0])
    if flat.mean() < 0.5:
        sys.exit(f"only {flat.mean() * 100:.1f}% of tiles are one flat colour, so\n"
                 "this capture is something else showing through the map and every\n"
                 "count below would be that. Pass --debug-view-mix=0 (this script\n"
                 "does) and check the runtime accepted it.")

    previous = tile_samples(files[0])
    changes = np.zeros(previous.shape[:2], dtype=np.int64)
    for path in files[1:]:
        current = tile_samples(path)
        changes += np.any(current != previous, axis=-1)
        previous = current

    # Only the tiles the map owns are scored; the rest are named below.
    dropped = int((~flat).sum())
    changes = changes[flat]

    seconds = (len(files) - 1) / args.fps
    tiles = changes.size
    per_tile_second = changes.sum() / (tiles * seconds)
    moved = int((changes > 0).sum())

    print()
    print(f"{len(files)} frames, {seconds:.1f}s of scene time, {tiles} tiles, "
          f"{args.lane} lane, settled {args.settle}"
          + (f" ({dropped} not flat, left out)" if dropped else ""))
    print(f"  transitions in all           {int(changes.sum())}")
    print(f"  per tile per second          {per_tile_second:.4f}   (bar: under {BAR})")
    print(f"  tiles that ever changed      {moved} ({100.0 * moved / tiles:.1f}%)")
    print(f"  the busiest tile changed     {int(changes.max())} times")
    print(f"  VERDICT: {'PASS' if per_tile_second < BAR else 'FAIL'}")

    # A picture of where the allocator is restless, which a number cannot show:
    # a limit cycle is usually a region, not a scatter.
    if changes.max() > 0:
        grid = np.zeros(flat.shape, dtype=np.float64)
        grid[flat] = changes
        heat = (grid / grid.max() * 255.0).astype(np.uint8)
        Image.fromarray(heat).resize((grid.shape[1] * 4, grid.shape[0] * 4),
                                     Image.NEAREST).save(out / "transitions.png")
        print(f"  where: {out / 'transitions.png'}")

    if not args.keep_frames and not args.analyse_only:
        for path in files:
            path.unlink()


if __name__ == "__main__":
    main()
