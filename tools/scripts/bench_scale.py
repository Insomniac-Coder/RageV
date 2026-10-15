"""The object-count curve: what the renderer costs as a scene grows.

Roadmap 8.3 defers GPU-driven rendering on one measurement -- "the renderer
draws a thousand in 1.9 ms and is nowhere near the wall" -- which is a claim
about a curve made from a single point on it. This walks the curve.

Run it before and after any change that claims to make scale cheaper. Every
number it prints comes from the engine's own benchmark; nothing here times
anything itself.

**RT2-2c: arms, motion, order.** Each scene runs in two arms at the project's
own settings -- `rt` as saved (ray tracing on) and `raster` with only
`--raytracing=off` added -- and, where a `scale_<n>_moving.rage` twin exists,
still and moving (one object in ten spinning). Every configuration runs twice
in A,B,...,B,A order, so a laptop warming through the batch shows as spread
rather than as a difference between the configurations; the table gives the
mean of the two and their half-range. Besides the frame it reads the CPU
phases, the G-buffer pass's recording, the GPU total and the ray-tracing
structures' zones (instance list, BLAS builds, TLAS build; CPU and GPU).

    python tools/scripts/bench_scale.py --counts 60000
    python tools/scripts/bench_scale.py --scenes scale_content_20000 --motion still

Each run starts only on mains power and once the CPU package has read at most
`--cool` degrees for 45 s (HWiNFO's shared-memory registry values; skipped with
a note where HWiNFO is not running), and a run whose peak passed `--hot` is
thrown away and run again after the wait (the owner's thermal rule for long
batches). Unpinned, as the engine schedules its own threads.
"""
import argparse
import pathlib
import re
import statistics
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "build" / "bin" / "Release" / "RageVRuntime" / "RageVRuntime.exe"
PROJECT = ROOT / "SampleProject" / "SampleProject.rvproject"
SCENES = ROOT / "SampleProject" / "assets" / "scenes"
# Every run's whole log, for the per-pass table the summary leaves out.
LOGS = ROOT / "bench" / "scale" / "logs"

ARMS = {"rt": [], "raster": ["--raytracing=off"]}

# Each value is the first number of the row (the CPU column) unless the
# pattern captures two, in which case both CPU and GPU are kept.
PATTERNS = {
    "frame": r"frame\s+mean\s+([0-9.]+) ms",
    "p95": r"frame\s+mean\s+[0-9.]+ ms\s+median\s+[0-9.]+\s+p95\s+([0-9.]+)",
    "wait": r"\] *wait \(gpu/vsync\)\s+([0-9.]+)",
    "shadow": r"\] *shadow maps\s+([0-9.]+)\s+([0-9.]+)",
    "graph": r"\] *render graph\s+([0-9.]+)\s+([0-9.]+)",
    "unaccounted": r"\] *unaccounted\s+([0-9.]+)",
    "gbuffer": r"scene/GBuffer\s+([0-9.]+)\s+([0-9.]+)",
    "gpu": r"whole frame \(GPU\)\s+([0-9.]+) ms",
    "instances": r"rt/instance list\s+([0-9.]+)",
    "blas": r"rt/BLAS builds \(posed, water\)\s+([0-9.]+)\s+([0-9.]+)",
    "tlas": r"rt/TLAS build\s+([0-9.]+)\s+([0-9.]+)",
    "draws": r"([0-9]+) mesh draws \(([0-9]+) indirect\), ([0-9]+) triangles submitted",
    "verdict": r"verdict: (.*)",
}


def hwinfo_temperature():
    """The CPU package in degrees, or None when HWiNFO is not publishing."""
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\HWiNFO64\VSB") as key:
            index = 0
            while True:
                label = winreg.QueryValueEx(key, f"Label{index}")[0]
                if label == "CPU Package":
                    return float(winreg.QueryValueEx(key, f"ValueRaw{index}")[0])
                index += 1
    except OSError:
        return None


def on_mains():
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          "(Get-CimInstance Win32_Battery | Select-Object -First 1).BatteryStatus"],
                         capture_output=True, text=True).stdout.strip()
    return out in ("", "2")   # no battery at all, or on AC


def wait_cool(limit, hold=45.0):
    if hwinfo_temperature() is None:
        return "no HWiNFO reading"
    start = None
    while True:
        t = hwinfo_temperature()
        if t is not None and t <= limit:
            start = start or time.monotonic()
            if time.monotonic() - start >= hold:
                return f"{t:.0f} C"
        else:
            start = None
        time.sleep(1.0)


def run(scene, arm, frames, width, height, tag):
    """One benchmark run: the parsed numbers, and the peak package temperature."""
    command = [str(RUNTIME), "--project=" + str(PROJECT), "--scene=scenes/" + scene,
               "--rhi=vulkan", "--render-defaults=off", "--vsync=off",
               f"--width={width}", f"--height={height}", f"--benchmark={frames}"] + ARMS[arm]
    # **--vsync=off is not optional here.** With it on, every number is the
    # display's refresh and the benchmark says so in as many words.
    peak = [0.0]
    done = threading.Event()

    def watch():
        while not done.is_set():
            t = hwinfo_temperature()
            if t is not None:
                peak[0] = max(peak[0], t)
            done.wait(1.0)

    watcher = threading.Thread(target=watch, daemon=True)
    watcher.start()
    p = subprocess.run(command, cwd=RUNTIME.parent, capture_output=True, text=True,
                       errors="replace", timeout=1800, stdin=subprocess.DEVNULL)
    done.set()
    watcher.join()
    LOGS.mkdir(parents=True, exist_ok=True)
    (LOGS / f"{tag}.log").write_text(p.stdout, encoding="utf-8")

    found = {}
    for name, pattern in PATTERNS.items():
        match = re.search(pattern, p.stdout)
        if match:
            found[name] = match.groups()
    return found, peak[0]


def number(found, key, column=0):
    value = found.get(key)
    if not value or len(value) <= column:
        return None
    try:
        return float(value[column])
    except ValueError:
        return None


COLUMNS = [("frame", "frame", 0), ("p95", "p95", 0), ("gpu", "GPU", 0), ("wait", "wait", 0),
           ("shadow", "shadow", 0), ("graph", "graph", 0), ("gbuffer", "gbuf rec", 0),
           ("unaccounted", "other", 0), ("instances", "rt list", 0), ("tlas", "TLAS cpu", 0),
           ("tlas", "TLAS gpu", 1), ("blas", "BLAS cpu", 0), ("blas", "BLAS gpu", 1)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--counts", default="60000",
                        help="object counts of the scale_<n> scenes, comma separated")
    parser.add_argument("--scenes", default="",
                        help="scene names instead (without .rage); a <name>_moving twin is the moving arm")
    parser.add_argument("--arms", default="rt,raster")
    parser.add_argument("--motion", default="still,moving")
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--cool", type=float, default=65.0)
    parser.add_argument("--hot", type=float, default=100.0)
    parser.add_argument("--no-validate", action="store_true")
    args = parser.parse_args()

    if not RUNTIME.exists():
        sys.exit(f"no runtime at {RUNTIME}; build Release first")
    if not on_mains():
        sys.exit("on battery: timings taken off mains power are not comparable; plug in first")

    names = ([s for s in args.scenes.split(",") if s] if args.scenes
             else [f"scale_{c}" for c in args.counts.split(",")])
    configs = []
    for name in names:
        for motion in args.motion.split(","):
            scene = f"{name}.rage" if motion == "still" else f"{name}_moving.rage"
            if not (SCENES / scene).exists():
                print(f"  (no {scene}; skipped -- make_scale_scenes.py or make_foliage_scene.py writes it)")
                continue
            for arm in args.arms.split(","):
                configs.append((name, motion, arm, scene))

    sys.path.insert(0, str(ROOT / "tools" / "scripts"))
    from vulkan_messages import validate
    if not args.no_validate:
        for scene in sorted({c[3] for c in configs}):
            # RT2-1: the validation pre-pass, on each scene, before its numbers.
            problems = validate([str(RUNTIME), "--project=" + str(PROJECT), "--scene=scenes/" + scene,
                                 "--rhi=vulkan", "--render-defaults=off", "--vsync=off",
                                 "--width=640", "--height=360"], cwd=RUNTIME.parent)
            if problems:
                sys.exit(f"validation pre-pass failed on {scene}: {problems[0][:160]}")

    # A,B,...,B,A: every configuration twice, the second pass reversed.
    order = list(range(len(configs))) + list(reversed(range(len(configs))))
    results = {i: [] for i in range(len(configs))}
    for step, i in enumerate(order, 1):
        name, motion, arm, scene = configs[i]
        while True:
            note = wait_cool(args.cool)
            found, peak = run(scene, arm, args.frames, args.width, args.height,
                              f"{scene[:-5]}_{arm}_{1 if step <= len(configs) else 2}")
            if peak > args.hot:
                print(f"  [{step}/{len(order)}] {scene} {arm}: peak {peak:.0f} C over {args.hot:.0f}; again")
                continue
            break
        frame = number(found, "frame")
        print(f"  [{step}/{len(order)}] {scene:30s} {arm:6s} started at {note}, peak {peak:.0f} C: "
              + (f"{frame:.2f} ms" if frame else "no benchmark output"))
        results[i].append(found)

    header = f"{'scene':24s} {'motion':6s} {'arm':6s} " + " ".join(f"{label:>9s}" for _, label, _ in COLUMNS)
    print()
    print("mean of the two runs (ms); +- half their difference under the frame")
    print(header)
    print("-" * len(header))
    for i, (name, motion, arm, scene) in enumerate(configs):
        runs = results[i]
        cells = []
        spread = ""
        for key, _, column in COLUMNS:
            values = [v for v in (number(r, key, column) for r in runs) if v is not None]
            cells.append(f"{statistics.mean(values):9.3f}" if values else f"{'--':>9s}")
            if key == "frame" and len(values) == 2:
                spread = f"+-{abs(values[0] - values[1]) / 2:.2f}"
        print(f"{name:24s} {motion:6s} {arm:6s} " + " ".join(cells))
        draws = runs[-1].get("draws") if runs else None
        verdict = runs[-1].get("verdict", ("",))[0] if runs else ""
        print(f"{'':39s}{spread:>9s}   "
              + (f"{draws[0]} draws ({draws[1]} indirect), {int(draws[2]) / 1e6:.1f} M triangles; " if draws else "")
              + verdict.split(" -- ")[0])


if __name__ == "__main__":
    main()
