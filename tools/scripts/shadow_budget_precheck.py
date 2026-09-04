"""WR-16 step S1: the fixed-budget pre-check on the water.

docs/RAY-BUDGET-DESIGN.md Part IV, the re-sequenced table: before the
shadow spender (S4) is built, see what a few shadow rays a pixel look like.
`--shadow-budget=K` gives every pixel K rays in all, to K lamps chosen by
weighted reservoir sampling on a cheap importance, and takes the lamps'
light through the unbiased sampling weights -- exactly as noisy as K rays
allow, no reuse. Two arms, in one matrix:

    raw        under no AA and under MSAA 4x, where nothing averages the
               choice frame to frame: the floor.
    accumulated under TAA, where the choice walks per frame and the resolve
               integrates it: the accumulator that exists, standing in for
               S4's own history.

Against the truth -- every lamp traced (`--rt-optimisation=off`) under the
same AA mode -- and beside the shipped preset (Quality) for scale. Three
cameras, four budgets. Per arm, camera and AA mode: a still at frame 60
with the clock pinned, diffed against the truth still (luminance over 2
and over 6 levels, the mean, and the *signed* mean over the water band,
which is where a structurally missing lamp group shows as a bias the
averaging cannot remove); the flicker protocol (16 frames from 240,
blinking-pixel count); and, at the project's own settings, one benchmark
for the frame time.

    python tools/scripts/shadow_budget_precheck.py
    python tools/scripts/shadow_budget_precheck.py --cameras Headland --budgets 4
    python tools/scripts/shadow_budget_precheck.py --report build/shadow_budget

The verdict this has to support, in the design's words: the K to build
for, whether any lamp group's shadow is structurally missing at 8 even in
the accumulated frame (bias), and whether accumulation lets the water hold
that K. The bar for the accumulated arm is the standing one: diff images
judged per pixel, never a mean alone.
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

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import shadow_ray_matrix as matrix  # noqa: E402

ROOT = matrix.ROOT
RUNTIME = matrix.RUNTIME
CAMERAS = matrix.CAMERAS
FLICKER = ROOT / "tools" / "scripts" / "check_glint_flicker.py"

AA_MODES = {
    "none": ["--aa=none"],
    "msaa": ["--aa=msaa", "--msaa=4"],
    "taa":  ["--aa=taa"],
}

# The water band, as a fraction of the frame's height from the top: the
# three cameras look across the bay from low on the shore, and the sea
# fills the frame below the horizon. Crude on purpose -- the same band for
# every arm is what makes the signed means comparable.
WATER_BAND_FROM = 0.45


def arms(budgets, sampler=False, passes=False):
    """truth, the shipped preset, then each budget with the cheap target
    (irradiance: what S4 can afford for every candidate) and with the full
    one (the whole unshadowed term, BRDF included: what the water's glitter
    asks for). The gap between the two columns is the S4 design question."""
    out = [("truth", ["--rt-optimisation=off"]),
           ("quality", ["--rt-optimisation=quality"])]
    if passes:
        # WR-16 S4b (--passes): the sea's lamps chosen and shaded in passes of
        # their own, against the same truth and the same protocol as S4a, so
        # the rows read straight against its table. Three arms per budget,
        # because "what did S4b do" is two questions and one arm cannot answer
        # both: `fwd` is the sampler inside the water shader (S4a, the row to
        # beat), `pass-noreuse` is the same estimate moved into the two passes
        # (so fwd -> pass-noreuse is what the passes COST), and `pass` adds the
        # history and the three neighbours (so pass-noreuse -> pass is what the
        # reuse BUYS). Nothing here is tuned: the neighbour ring is three taps
        # at twelve pixels and the confidence cap is 20, both by assertion.
        for k in budgets:
            out.append(("fwd-%d" % k,
                        ["--rt-optimisation=off", "--light-sampling=%d,term" % k,
                         "--water-lamp-pass=off"]))
            # The reuse's two halves were measured on 2026-09-04 and both lose;
            # it is off by default now and its arms are not re-run. What the
            # pair below measures is S4c, the accumulation of the shaded light,
            # which is a different quantity and the one that survives.
            out.append(("pass-%d" % k,
                        ["--rt-optimisation=off", "--light-sampling=%d,term" % k,
                         "--water-lamp-accumulate=off"]))
            out.append(("pass-%d-s4c" % k,
                        ["--rt-optimisation=off", "--light-sampling=%d,term" % k]))
        return out
    if sampler:
        # WR-16 S4's sampler (--sampler): the same three cameras and the same
        # truth, so its rows read straight against S1's. Each K under both
        # targets -- the cheap irradiance S1 found unusable on the water, and
        # the same with the specular lobe's magnitude added -- because which
        # of the two ships is the question this matrix exists to answer.
        for k in budgets:
            for target in ("term", "irradiance"):
                out.append(("sampler-%d-%s" % (k, target),
                            ["--rt-optimisation=off",
                             "--light-sampling=%d,%s" % (k, target)]))
        return out
    for k in budgets:
        out.append((f"budget-{k}", ["--rt-optimisation=off", f"--shadow-budget={k}"]))
    for k in budgets:
        out.append((f"budget-{k}-full", ["--rt-optimisation=off", f"--shadow-budget={k},full"]))
    return out


def command(camera_flag, width, height, extra):
    return [str(RUNTIME), f"--project={ROOT / 'SampleProject'}",
            "--scene=scenes/GoldenGateDemo.rage", "--rhi=vulkan",
            "--render-defaults=off", "--vsync=off",
            f"--width={width}", f"--height={height}", f"--camera={camera_flag}"] + list(extra)


def run(cmd):
    proc = subprocess.run(cmd, cwd=RUNTIME.parent, capture_output=True, text=True,
                          timeout=900, errors="replace")
    return proc.returncode, proc.stdout + "\n--- stderr ---\n" + proc.stderr


def still(camera_flag, width, height, extra, path, frame=60):
    return run(command(camera_flag, width, height, extra)
               + ["--frame-time=0.000001", f"--screenshot-frame={frame}", f"--screenshot={path}"])


def flicker_frames(stem):
    return sorted(pathlib.Path(stem).parent.glob(pathlib.Path(stem).name + "_*.png"))


def flicker_count(frames):
    """The protocol's own script over the captured frames."""
    if len(frames) < 8:
        return {"error": f"{len(frames)} frames"}
    proc = subprocess.run([sys.executable, str(FLICKER)] + [str(f) for f in frames],
                          capture_output=True, text=True, errors="replace")
    match = re.search(r"blinking pixels\s+(\d+)\s+\(([0-9.]+)% of the region\)", proc.stdout)
    worst = re.search(r"worst swing\s+([0-9.]+)", proc.stdout)
    if not match:
        return {"error": "no flicker output: " + proc.stdout[-200:]}
    return {"blinking": int(match[1]), "blinking_pct": float(match[2]),
            "worst_swing": float(worst[1]) if worst else None}


def flicker(camera_flag, width, height, extra, stem):
    code, log = run(command(camera_flag, width, height, extra)
                    + ["--frame-time=0.000001", "--screenshot-frame=240",
                       "--screenshot-count=16", f"--screenshot={stem}.png"])
    frames = flicker_frames(stem)
    if code != 0 or len(frames) < 8:
        return {"error": f"exit {code}, {len(frames)} frames"}, log
    return flicker_count(frames), log


def benchmark(camera_flag, width, height, extra, frames):
    code, out = run(command(camera_flag, width, height, extra) + [f"--benchmark={frames}"])
    mean = re.search(r"frame\s+mean\s+([0-9.]+) ms", out)
    water = re.search(r"scene/Transparent\s+[0-9.]+\s+([0-9.]+)", out)
    rays = re.search(r"rays per frame: shadow ([0-9.]+) M.*?([0-9.]+) per lit fragment", out)
    return {"exit": code, "mean": float(mean[1]) if mean else None,
            "water": float(water[1]) if water else None,
            "shadow_rays_M": float(rays[1]) if rays else None,
            "rays_per_fragment": float(rays[2]) if rays else None}, out


def diff(reference, candidate, out_path):
    """The matrix script's diff, plus the water band's signed mean and its
    share over six levels -- the bias measure."""
    result = matrix.diff(reference, candidate, out_path)
    if "error" in result:
        return result
    a = matrix.luminance(Image.open(reference))
    b = matrix.luminance(Image.open(candidate))
    top = int(a.shape[0] * WATER_BAND_FROM)
    d = (b - a)[top:, :]
    result["water_signed_mean"] = float(d.mean())
    result["water_over6_pct"] = float((np.abs(d) > 6.0).sum()) * 100.0 / d.size
    result["water_mean_abs"] = float(np.abs(d).mean())
    return result


def report(results, cameras, arm_names):
    lines = []
    for camera in cameras:
        lines.append(f"**{camera}**")
        head = ("| arm | ms (project AA) | shadow rays M, per fragment | "
                + " | ".join(f"{aa}: >2% / >6% / water signed / water >6% / blink%"
                             for aa in AA_MODES) + " |")
        lines.append(head)
        lines.append("|" + "---|" * (head.count("|") - 1))
        for name in arm_names:
            r = results.get(f"{name}/{camera}")
            if not r:
                continue
            bench = r.get("bench") or {}
            cells = [f"{bench['mean']:.1f}" if bench.get("mean") else "?",
                     (f"{bench['shadow_rays_M']:.1f}, {bench['rays_per_fragment']:.1f}"
                      if bench.get("shadow_rays_M") is not None else "?")]
            for aa in AA_MODES:
                d = (r.get("aa") or {}).get(aa, {}).get("diff") or {}
                f = (r.get("aa") or {}).get(aa, {}).get("flicker") or {}
                if name == "truth":
                    cells.append(f"ref / ref / ref / ref / {f.get('blinking_pct', '?')}")
                elif "over2_pct" in d:
                    cells.append(f"{d['over2_pct']:.2f} / {d['over6_pct']:.2f} / "
                                 f"{d['water_signed_mean']:+.2f} / {d['water_over6_pct']:.2f} / "
                                 f"{f.get('blinking_pct', '?')}")
                else:
                    cells.append("no diff")
            lines.append(f"| {name} | " + " | ".join(cells) + " |")
        lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cameras", default=",".join(CAMERAS))
    parser.add_argument("--budgets", default="1,2,4,8")
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=900)
    parser.add_argument("--bench-width", type=int, default=2560)
    parser.add_argument("--bench-height", type=int, default=1440)
    parser.add_argument("--frames", type=int, default=200)
    parser.add_argument("--no-flicker", action="store_true")
    parser.add_argument("--no-bench", action="store_true",
                        help="keep the benchmarks an earlier run recorded; stills and flicker only")
    parser.add_argument("--analyse-only", action="store_true",
                        help="no runtime: recompute the diffs and the flicker counts from the "
                             "stills and frames already in --out")
    parser.add_argument("--passes", action="store_true",
                        help="WR-16 S4b's arms (the sea's own lamp passes, with and without "
                             "the reuse, against S4a's forward sampler); writes to "
                             "build/water_lamp_pass by default")
    parser.add_argument("--sampler", action="store_true",
                        help="WR-16 S4's sampler arms (--light-sampling) instead of S1's "
                             "fixed-budget ones; writes to build/light_sampling by default")
    parser.add_argument("--out", default=None)
    parser.add_argument("--report", help="print the tables from an earlier run's directory")
    args = parser.parse_args()

    cameras = [c for c in args.cameras.split(",") if c in CAMERAS]
    budgets = [int(b) for b in args.budgets.split(",") if b]
    if args.passes and args.budgets == parser.get_default("budgets"):
        budgets = [4]
    arm_list = arms(budgets, args.sampler, args.passes)
    if args.out is None:
        args.out = str(ROOT / "build"
                       / ("water_lamp_pass" if args.passes
                          else "light_sampling" if args.sampler else "shadow_budget"))
    arm_names = [a[0] for a in arm_list]

    if args.report:
        results = json.load(open(pathlib.Path(args.report) / "results.json", encoding="utf-8"))
        print(report(results, cameras, arm_names))
        return

    # Absolute, because the runtime resolves a relative path against its own
    # directory, not this script's -- the first run wrote every still under
    # build/bin/Release/RageVRuntime/ and the diff column came out empty.
    out = pathlib.Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    results_path = out / "results.json"
    results = json.load(open(results_path, encoding="utf-8")) if results_path.exists() else {}

    if args.analyse_only:
        for camera in cameras:
            for name, _ in arm_list:
                entry = results.setdefault(f"{name}/{camera}", {})
                entry.setdefault("aa", {})
                for aa in AA_MODES:
                    mode = entry["aa"].setdefault(aa, {})
                    png = out / f"{name}_{camera}_{aa}.png"
                    reference = out / f"truth_{camera}_{aa}.png"
                    if name != "truth" and reference.exists() and png.exists():
                        mode["diff"] = diff(reference, png, out / f"{name}_{camera}_{aa}_diff.png")
                    frames = flicker_frames(str(out / "flicker" / f"{name}_{camera}_{aa}"))
                    if frames:
                        mode["flicker"] = flicker_count(frames)
        results_path.write_text(json.dumps(results, indent=1), encoding="utf-8")
        print(report(results, cameras, arm_names))
        return

    if not RUNTIME.exists():
        sys.exit(f"no runtime at {RUNTIME}; build Release first")
    if "RageVEditor" in subprocess.run(["tasklist"], capture_output=True, text=True).stdout:
        sys.exit("RageVEditor.exe is running; close it first")

    for camera in cameras:
        flag = CAMERAS[camera]
        for name, extra in arm_list:
            key = f"{name}/{camera}"
            entry = results.get(key, {})
            started = time.time()

            if args.no_bench and entry.get("bench"):
                bench = entry["bench"]
            else:
                bench, log = benchmark(flag, args.bench_width, args.bench_height, extra, args.frames)
                (out / f"{name}_{camera}_bench.log").write_text(log, encoding="utf-8")
            entry["bench"] = bench
            entry.setdefault("aa", {})

            for aa, aa_flags in AA_MODES.items():
                mode = entry["aa"].setdefault(aa, {})
                png = out / f"{name}_{camera}_{aa}.png"
                code, log = still(flag, args.width, args.height, extra + aa_flags, png)
                (out / f"{name}_{camera}_{aa}_still.log").write_text(log, encoding="utf-8")
                mode["still_exit"] = code
                reference = out / f"truth_{camera}_{aa}.png"
                if name != "truth" and reference.exists() and png.exists():
                    mode["diff"] = diff(reference, png, out / f"{name}_{camera}_{aa}_diff.png")
                if not args.no_flicker:
                    stem = out / "flicker" / f"{name}_{camera}_{aa}"
                    stem.parent.mkdir(exist_ok=True)
                    for old in stem.parent.glob(stem.name + "_*.png"):
                        old.unlink()
                    mode["flicker"], log = flicker(flag, args.width, args.height,
                                                   extra + aa_flags, str(stem))
                    (out / f"{name}_{camera}_{aa}_flicker.log").write_text(log, encoding="utf-8")

            results[key] = entry
            results_path.write_text(json.dumps(results, indent=1), encoding="utf-8")
            taa = entry["aa"].get("taa", {})
            d = taa.get("diff") or {}
            print(f"{name:<9} {camera:<9} {time.time() - started:5.1f}s  "
                  + (f"{bench['mean']:6.1f} ms  {bench['shadow_rays_M']:5.1f} M shadow rays"
                     if bench.get("mean") and bench.get("shadow_rays_M") is not None else "NO BENCH")
                  + (f"  taa >6: {d['over6_pct']:.2f}%  water signed {d['water_signed_mean']:+.2f}"
                     if "over6_pct" in d else "")
                  + (f"  blink none {entry['aa'].get('none', {}).get('flicker', {}).get('blinking_pct', '?')}%"
                     if not args.no_flicker else ""), flush=True)

    print()
    print(report(results, cameras, arm_names))


if __name__ == "__main__":
    main()
