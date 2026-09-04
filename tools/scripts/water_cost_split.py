"""WR-16 S4, the sizing run: where the water pass's milliseconds actually go.

The split everyone quotes -- rays 20-30%, the hit walk 13-22%, the rest the
shading of 77 to 125 lamps a fragment -- was measured in S0's calibration,
*before* S2 removed the reads. S2 then took 20 / 28 / 32% off these three
cameras and left the water paying in full: it moves, so it is lit live, and
every patch of it still shades every lamp that reaches it. Before three
weeks are spent on the sampler, this says what the sampler can win.

Four levers, each a flag, so the arms subtract:

    base              the project's settings, everything as it ships
    no-rays           --casting-lights=0: every lamp's shadow ray gone, the
                      shading kept. base - no-rays is the lamps' ray time.
    shade-K           --shade-lights=K: the sixteen-byte cull record still
                      walked for every lamp (what a sampler pays to score
                      candidates), only the first K read in full and shaded,
                      ray included. base - shade-4 is the whole lamp lever,
                      rays and shading together -- S4's ceiling.
    shade-K-no-rays   both: what is left when the lamps cost as little as
                      they can. no-rays - shade-4-no-rays is the shading
                      lever alone, which no shipped flag can show.
    no-hit-walk       --hit-lights=off, for scale: the light walk at traced
                      hits, the half the world-space grid addresses.

Interleaved, because this laptop's GPU drifts about a millisecond over a
session and walks one way: every arm is run once per pass, and the passes
repeat, so a drift lands on all arms equally. The median over passes is
what the table prints.

    python tools/scripts/water_cost_split.py
    python tools/scripts/water_cost_split.py --cameras Headland --passes 2

The picture under --shade-lights is wrong on purpose (the lamps past K are
simply absent), so nothing here is a quality claim -- only a time bound.
"""
import argparse
import json
import pathlib
import re
import statistics
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import shadow_ray_matrix as matrix  # noqa: E402

ROOT = matrix.ROOT
RUNTIME = matrix.RUNTIME
CAMERAS = matrix.CAMERAS

ARMS = [
    ("base",            []),
    ("no-rays",         ["--casting-lights=0"]),
    ("shade-4",         ["--shade-lights=4"]),
    ("shade-8",         ["--shade-lights=8"]),
    ("shade-4-no-rays", ["--shade-lights=4", "--casting-lights=0"]),
    ("no-hit-walk",     ["--hit-lights=off"]),
]


def command(camera, width, height, frames, extra):
    # No --shadow-rays or --light-cutoff here: the point of `base` is the
    # project's own settings, and an override would quietly replace them.
    return [str(RUNTIME), "--project=" + str(ROOT / "SampleProject"),
            "--scene=scenes/GoldenGateDemo.rage", "--rhi=vulkan",
            "--render-defaults=off", "--vsync=off",
            "--width=" + str(width), "--height=" + str(height),
            "--camera=" + CAMERAS[camera],
            "--benchmark=" + str(frames)] + list(extra)


def measure(camera, width, height, frames, extra):
    proc = subprocess.run(command(camera, width, height, frames, extra),
                          cwd=RUNTIME.parent, capture_output=True, text=True,
                          timeout=1800, errors="replace")
    out = proc.stdout + "\n--- stderr ---\n" + proc.stderr
    mean = re.search(r"frame\s+mean\s+([0-9.]+) ms.*?\(([0-9.]+) FPS\)", out)
    water = re.search(r"scene/Transparent\s+[0-9.]+\s+([0-9.]+)", out)
    scene = re.search(r"scene/Scene\s+[0-9.]+\s+([0-9.]+)", out)
    rays = re.search(r"rays per frame: shadow ([0-9.]+) M, water ([0-9.]+) M", out)
    lights = re.search(r"lights per fragment: ([0-9.]+) avg.*?hits: ([0-9.]+) avg", out)
    if proc.returncode != 0 or not mean:
        print("    ! exit " + str(proc.returncode) + ", no frame time", flush=True)
        print(out[-1500:], flush=True)
    return {
        "exit": proc.returncode,
        "mean": float(mean[1]) if mean else None,
        "fps": float(mean[2]) if mean else None,
        "water": float(water[1]) if water else None,
        "scene": float(scene[1]) if scene else None,
        "shadow_rays_m": float(rays[1]) if rays else None,
        "water_rays_m": float(rays[2]) if rays else None,
        "lights_per_fragment": float(lights[1]) if lights else None,
        "lights_per_hit": float(lights[2]) if lights else None,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cameras", nargs="*", default=list(CAMERAS))
    ap.add_argument("--arms", nargs="*", default=[name for name, _ in ARMS])
    ap.add_argument("--passes", type=int, default=3)
    ap.add_argument("--frames", type=int, default=150)
    ap.add_argument("--width", type=int, default=2560)
    ap.add_argument("--height", type=int, default=1440)
    ap.add_argument("--out", default=str(ROOT / "build" / "water_split"))
    args = ap.parse_args()

    arms = [(name, extra) for name, extra in ARMS if name in args.arms]
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    runs = {camera: {name: [] for name, _ in arms} for camera in args.cameras}

    for p in range(args.passes):
        for camera in args.cameras:
            for name, extra in arms:
                result = measure(camera, args.width, args.height, args.frames, extra)
                runs[camera][name].append(result)
                print("  pass %d  %-9s %-16s %s ms  (water %s, scene %s)"
                      % (p + 1, camera, name, result["mean"],
                         result["water"], result["scene"]), flush=True)

    table = {}
    for camera in args.cameras:
        table[camera] = {}
        for name, _ in arms:
            got = [r for r in runs[camera][name] if r["mean"] is not None]
            if not got:
                continue
            table[camera][name] = {
                key: round(statistics.median([r[key] for r in got if r[key] is not None]), 3)
                for key in ("mean", "water", "scene", "shadow_rays_m",
                            "lights_per_fragment", "lights_per_hit")
                if any(r[key] is not None for r in got)
            }

    (out / "split.json").write_text(json.dumps(
        {"runs": runs, "median": table, "frames": args.frames,
         "resolution": [args.width, args.height], "passes": args.passes}, indent=1))

    print()
    header = "%-10s %-16s %8s %8s %8s %8s" % (
        "camera", "arm", "frame", "water", "scene", "shadowM")
    print(header)
    print("-" * len(header))
    for camera in args.cameras:
        for name, _ in arms:
            row = table[camera].get(name)
            if not row:
                continue
            print("%-10s %-16s %8.1f %8.1f %8.1f %8.2f" % (
                camera, name, row.get("mean", 0), row.get("water", 0),
                row.get("scene", 0), row.get("shadow_rays_m", 0)))
        base = table[camera].get("base", {})
        for name, label in (("no-rays", "lamps' rays"),
                            ("shade-4", "lamp lever, K=4"),
                            ("no-hit-walk", "the hit walk")):
            arm = table[camera].get(name)
            if base and arm:
                delta = base["mean"] - arm["mean"]
                print("%-10s %-16s %8.1f ms (%.0f%% of the frame)" % (
                    "", label, delta, delta / base["mean"] * 100))
        no_rays = table[camera].get("no-rays")
        floor = table[camera].get("shade-4-no-rays")
        if no_rays and floor:
            delta = no_rays["mean"] - floor["mean"]
            print("%-10s %-16s %8.1f ms" % ("", "shading alone", delta))
        print()
    print("written: " + str(out / "split.json"))


if __name__ == "__main__":
    main()
