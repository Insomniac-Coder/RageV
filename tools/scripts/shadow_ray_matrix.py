"""WR-17's falloff matrix: every shadow-ray thinning shape, measured and diffed.

The owner's rule: the farther a light, the less it contributes, so the
farther it is the more of its shadow rays a pixel may skip -- but the shape
of that thinning has to be tested, and the far end may be pushed hard only
where the picture does not change. This renders each candidate on three
cameras, benchmarks it, and diffs its still against the untouched frame,
because the bar on record is the per-pixel diff image, never a mean.

    python tools/scripts/shadow_ray_matrix.py                 # the whole matrix
    python tools/scripts/shadow_ray_matrix.py --only off,linear-300
    python tools/scripts/shadow_ray_matrix.py --report build/shadow_rays

Per variant and camera: one --benchmark run (frames per --frames) for the
frame time, and one still at frame 60 with the clock pinned
(--frame-time=0.000001: zero is the wall clock and the sea keeps moving) at
the project's own settings, so TAA has integrated the dither. The diff
counts pixels whose luminance moved more than 2 and more than 6 levels of
255 against the `off` still, writes the amplified difference beside the
stills, and prints one table.
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "build" / "bin" / "Release" / "RageVRuntime" / "RageVRuntime.exe"

CAMERAS = {
    "Headland": "500,89.47,-1100,0.01,-157.08,8.88",
    "Pier":     "70,4.5,705,0.01,-46.98,-2.86",
    "Glitter":  "500,2.5,180,0.01,-90,-1.146",
}

# name -> --shadow-rays value. Two distance pairs per shape: the gentle one
# and the aggressive one. The share shape has no end distance; its start is
# the floor under which nothing is skipped, and its number is the share of
# the pixel's direct light below which a light earns no ray.
# The third number is --light-cutoff: metres past which no positional light
# reaches at all (0 = every light keeps its range). The lamps are authored
# at 600 m so the Headland streaks exist; the cutoff rows show what a harder
# cut costs the picture.
# Rows without a suffix count a skipped ray's light lit (the `lit` token):
# the first arm measured, and the one whose Headland diff showed the water
# under the deck brightening by seventy levels. The `+borrow` rows are the
# fix -- a skipped light takes the mean visibility of the far rays the pixel
# did trace -- and the shape is judged on those.
VARIANTS = [
    ("off",        "off",            0),
    ("hard-300",   "hard,150,300,lit",   0),
    ("hard-600",   "hard,300,600,lit",   0),
    ("linear-300", "linear,150,300,lit", 0),
    ("linear-600", "linear,300,600,lit", 0),
    ("smooth-300", "smooth,150,300,lit", 0),
    ("smooth-600", "smooth,300,600,lit", 0),
    ("log-300",    "log,150,300,lit",    0),
    ("log-600",    "log,300,600,lit",    0),
    ("share-2",    "share,150,0,0.02,lit", 0),
    ("share-5",    "share,150,0,0.05,lit", 0),
    ("cut-450",    "off",            450),
    ("cut-300",    "off",            300),
    ("linear-600+cut-450", "linear,300,600,lit", 450),
    ("linear-300+borrow", "linear,150,300", 0),
    ("linear-600+borrow", "linear,300,600", 0),
    ("smooth-300+borrow", "smooth,150,300", 0),
    ("log-300+borrow",    "log,150,300",    0),
    ("log-600+borrow",    "log,300,600",    0),
    ("share-2+borrow",    "share,150,0,0.02", 0),
    ("share-5+borrow",    "share,150,0,0.05", 0),
    ("log-300+borrow+cut-450", "log,150,300", 450),
    # The floor: past the end, this fraction of a far light's rays is still
    # traced, so the borrowed pool always holds the far group. Zero is the
    # +borrow rows above; these are the aggressive settings that keep the
    # deck's shadow.
    ("linear-300+floor8",  "linear,150,300,0.125",  0),
    ("linear-300+floor16", "linear,150,300,0.0625", 0),
    ("linear-300+floor32", "linear,150,300,0.03125", 0),
    ("log-300+floor8",     "log,150,300,0.125",     0),
    ("hard-300+floor8",    "hard,150,300,0.125",    0),
    ("linear-300+floor8+cut-450", "linear,150,300,0.125", 450),
    # The local borrow: a skipped light takes the visibility of the last
    # thinned light this pixel traced (a neighbour along the deck) instead
    # of the mean of all of them. The +borrow and +floor rows above were the
    # global mean; these are the same settings under the local rule.
    ("linear-300+local",   "linear,150,300,0",       0),
    ("linear-300+local8",  "linear,150,300,0.125",   0),
    ("linear-300+local16", "linear,150,300,0.0625",  0),
    ("linear-300+local32", "linear,150,300,0.03125", 0),
    ("linear-600+local",   "linear,300,600,0",       0),
    ("linear-300+local8+cut-450", "linear,150,300,0.125", 450),
    # WR-18: the water's rays. A fourth element carries further flags.
    ("refraction-256", "off", 0, ["--refraction-floor=0.00390625"]),
    ("rate2",          "off", 0, ["--ray-rate=2"]),
    ("rate2+refraction-256", "off", 0, ["--ray-rate=2", "--refraction-floor=0.00390625"]),
    ("quality",     "off", 0, ["--rt-optimisation=quality"]),
    ("balanced",    "off", 0, ["--rt-optimisation=balanced"]),
    ("performance", "off", 0, ["--rt-optimisation=performance"]),
    # WR-10: the traced hit walks its cluster's list. Diffed against the
    # pre-WR-10 `off` still, so a null here is the exactness proof.
    ("off-wr10",     "off", 0, ["--rt-optimisation=off"]),
    ("quality-wr10", "off", 0, ["--rt-optimisation=quality"]),
]


def base_command(camera_flag, width, height, shadow_rays, cutoff, extra=()):
    # A preset row leaves the two overrides out, or they would win over it.
    overrides = [] if any(f.startswith("--rt-optimisation") for f in extra) else [
        f"--shadow-rays={shadow_rays}", f"--light-cutoff={cutoff}"]
    return [str(RUNTIME), f"--project={ROOT / 'SampleProject'}",
            "--scene=scenes/GoldenGateDemo.rage", "--rhi=vulkan",
            "--render-defaults=off", "--vsync=off",
            f"--width={width}", f"--height={height}",
            f"--camera={camera_flag}"] + overrides + list(extra)


def run(command):
    proc = subprocess.run(command, cwd=RUNTIME.parent, capture_output=True, text=True,
                          timeout=900, errors="replace")
    return proc.returncode, proc.stdout + "\n--- stderr ---\n" + proc.stderr


def benchmark(camera_flag, shadow_rays, cutoff, frames, width, height, extra=()):
    code, out = run(base_command(camera_flag, width, height, shadow_rays, cutoff, extra)
                    + [f"--benchmark={frames}"])
    match = re.search(r"frame\s+mean\s+([0-9.]+) ms.*?\(([0-9.]+) FPS\)", out)
    water = re.search(r"scene/Transparent\s+[0-9.]+\s+([0-9.]+)", out)
    scene = re.search(r"scene/Scene\s+[0-9.]+\s+([0-9.]+)", out)
    return {
        "exit": code,
        "mean": float(match[1]) if match else None,
        "fps": float(match[2]) if match else None,
        "water": float(water[1]) if water else None,
        "scene": float(scene[1]) if scene else None,
    }, out


def still(camera_flag, shadow_rays, cutoff, path, width, height, frame, extra=()):
    code, out = run(base_command(camera_flag, width, height, shadow_rays, cutoff, extra)
                    + ["--frame-time=0.000001", f"--screenshot-frame={frame}",
                       f"--screenshot={path}"])
    return code, out


def luminance(image):
    rgb = np.asarray(image.convert("RGB"), dtype=np.float32)
    return rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def diff(reference, candidate, out_path):
    a = luminance(Image.open(reference))
    b = luminance(Image.open(candidate))
    if a.shape != b.shape:
        return {"error": f"size mismatch {a.shape} vs {b.shape}"}
    d = b - a
    absd = np.abs(d)
    # The amplified difference, signed: brighter than the reference in red,
    # darker in blue, eight times the level so a two-level change is visible.
    amplified = np.zeros(a.shape + (3,), dtype=np.uint8)
    amplified[..., 0] = np.clip(np.maximum(d, 0.0) * 8.0, 0, 255)
    amplified[..., 2] = np.clip(np.maximum(-d, 0.0) * 8.0, 0, 255)
    Image.fromarray(amplified).save(out_path)
    total = absd.size
    return {
        "over2_pct": float((absd > 2.0).sum()) * 100.0 / total,
        "over6_pct": float((absd > 6.0).sum()) * 100.0 / total,
        "mean_abs": float(absd.mean()),
        "max_abs": float(absd.max()),
        "signed_mean": float(d.mean()),
    }


def report(results, cameras, variants):
    lines = []
    head = "| variant | " + " | ".join(f"{c}: ms / >2 lvl % / >6 lvl %" for c in cameras) + " |"
    lines.append(head)
    lines.append("|" + "---|" * (head.count("|") - 1))
    for name, *_ in variants:
        cells = []
        for camera in cameras:
            r = results.get(f"{name}/{camera}")
            if not r or r["bench"].get("mean") is None:
                cells.append("no output")
                continue
            d = r.get("diff") or {}
            cells.append(f"{r['bench']['mean']:.1f} / "
                         + (f"{d['over2_pct']:.2f} / {d['over6_pct']:.2f}" if "over2_pct" in d
                            else "ref" if name == "off" else "no diff"))
        lines.append(f"| {name} | " + " | ".join(cells) + " |")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--frames", type=int, default=150)
    parser.add_argument("--width", type=int, default=2560)
    parser.add_argument("--height", type=int, default=1440)
    parser.add_argument("--still-frame", type=int, default=60)
    parser.add_argument("--only", help="comma-separated variant names")
    parser.add_argument("--cameras", default=",".join(CAMERAS))
    parser.add_argument("--out", default=str(ROOT / "build" / "shadow_rays"))
    parser.add_argument("--report", help="print the table from an earlier run's directory")
    args = parser.parse_args()

    cameras = [c for c in args.cameras.split(",") if c in CAMERAS]
    variants = [v for v in VARIANTS if not args.only or v[0] in args.only.split(",")]

    if args.report:
        results = json.load(open(pathlib.Path(args.report) / "results.json", encoding="utf-8"))
        print(report(results, cameras, variants))
        return

    if not RUNTIME.exists():
        sys.exit(f"no runtime at {RUNTIME}; build Release first")
    if "RageVEditor" in subprocess.run(["tasklist"], capture_output=True, text=True).stdout:
        sys.exit("RageVEditor.exe is running; close it first")

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    results_path = out / "results.json"
    results = json.load(open(results_path, encoding="utf-8")) if results_path.exists() else {}

    for name, value, cutoff, *rest in variants:
        extra = rest[0] if rest else ()
        for camera in cameras:
            key = f"{name}/{camera}"
            started = time.time()
            bench, log = benchmark(CAMERAS[camera], value, cutoff, args.frames, args.width,
                                   args.height, extra)
            (out / f"{name}_{camera}_bench.log").write_text(log, encoding="utf-8")
            png = out / f"{name}_{camera}.png"
            code, log = still(CAMERAS[camera], value, cutoff, png, args.width, args.height,
                              args.still_frame, extra)
            (out / f"{name}_{camera}_still.log").write_text(log, encoding="utf-8")
            entry = {"bench": bench, "still_exit": code}
            reference = out / f"off_{camera}.png"
            if name != "off" and reference.exists() and png.exists():
                entry["diff"] = diff(reference, png, out / f"{name}_{camera}_diff.png")
            results[key] = entry
            results_path.write_text(json.dumps(results, indent=1), encoding="utf-8")
            mean = bench.get("mean")
            d = entry.get("diff", {})
            print(f"{name:<11} {camera:<9} {time.time() - started:5.1f}s  "
                  + (f"{mean:7.2f} ms" if mean else "NO BENCH")
                  + (f"  >2: {d['over2_pct']:5.2f}%  >6: {d['over6_pct']:5.2f}%  max {d['max_abs']:.0f}"
                     if "over2_pct" in d else ""), flush=True)

    print()
    print(report(results, cameras, variants))


if __name__ == "__main__":
    main()
