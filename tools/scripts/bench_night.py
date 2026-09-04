"""The night scene's frame time, eight cameras at a time.

The owner's measure for the revamp's frame-time items (WR-8, WR-10, WR-16):
frame time in milliseconds and frames per second per camera, before and
after each item. Every number comes from the engine's own --benchmark
report; nothing here times anything itself.

    python tools/scripts/bench_night.py --label before
    python tools/scripts/bench_night.py --label after-wr8
    python tools/scripts/bench_night.py --table build/bench/before.json

Two passes by default (A then B, each over all eight cameras) because this
laptop's GPU drifts about a millisecond over a session: the pair shows the
drift instead of hiding it in one run. Compare *changes* by running the
script once per build and reading the tables side by side. Keep the editor
closed; it contends for the GPU.

The pose lands on the scene's primary camera (RuntimeLayer.cpp honours
--camera), so every row renders at that camera's field of view rather than
its own -- Bluff's 42 degrees becomes Headland's 55. Consistent before and
after, which is what a benchmark needs; not what the scene's camera shows.
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "build" / "bin" / "Release" / "RageVRuntime" / "RageVRuntime.exe"

# Focus x,y,z, distance, yaw, pitch: the scene's eight cameras, with distance
# 0.01 so the eye sits on the focus and the Euler angles negated into degrees.
CAMERAS = [
    ("Headland",   "500,89.47,-1100,0.01,-157.08,8.88"),
    ("Deck",       "0,76.4,950,0.01,0,0"),
    ("Profile",    "2400,150,0,0.01,-90,0"),
    ("Bluff",      "45,49.94,1150,0.01,-5.04,8.88"),
    ("Pier",       "70,4.5,705,0.01,-46.98,-2.86"),
    ("Cliff",      "316,55,-810,0.01,-33.52,3.44"),
    ("Glitter",    "500,2.5,180,0.01,-90,-1.146"),
    ("Lime Point", "185,5.5,-590,0.01,-143.24,1.146"),
]


def editor_running():
    out = subprocess.run(["tasklist"], capture_output=True, text=True).stdout
    return "RageVEditor" in out


def run(camera_flag, frames, width, height, scene, extra):
    command = [str(RUNTIME), f"--project={ROOT / 'SampleProject'}", f"--scene={scene}",
               "--rhi=vulkan", "--render-defaults=off", "--vsync=off",
               f"--width={width}", f"--height={height}", f"--benchmark={frames}",
               f"--camera={camera_flag}"] + extra
    started = time.time()
    proc = subprocess.run(command, cwd=RUNTIME.parent, capture_output=True, text=True,
                          timeout=900, errors="replace")
    return proc.returncode, proc.stdout, proc.stderr, time.time() - started


def parse(text):
    result = {"passes": {}, "phases": {}}
    match = re.search(r"frame\s+mean\s+([0-9.]+) ms\s+median\s+([0-9.]+)\s+p95\s+([0-9.]+)"
                      r"\s+min\s+([0-9.]+)\s+max\s+([0-9.]+)\s+\(([0-9.]+) FPS\)", text)
    if match:
        result.update(mean=float(match[1]), median=float(match[2]), p95=float(match[3]),
                      min=float(match[4]), max=float(match[5]), fps=float(match[6]))
    match = re.search(r"whole frame \(GPU\)\s+([0-9.]+) ms", text)
    if match:
        result["gpu"] = float(match[1])
    match = re.search(r"verdict: (.+)", text)
    if match:
        result["verdict"] = match[1].strip()
    match = re.search(r"(\d+) lights, busiest cluster holds (\d+)", text)
    if match:
        result["lights"], result["busiest"] = int(match[1]), int(match[2])

    # WR-16 S0: the ray counters' lines. Millions of rays per frame by kind,
    # the per-fragment figures, and the temporal resolve's confidence.
    match = re.search(r"rays per frame: shadow ([0-9.]+) M, water ([0-9.]+) M, "
                      r"reflection ([0-9.]+) M, GI ([0-9.]+) M, AO ([0-9.]+) M -- ([0-9.]+) M in all, "
                      r"([0-9.]+) per lit fragment \((\d+) frames counted\)", text)
    if match:
        result["rays"] = {"shadow": float(match[1]), "water": float(match[2]),
                          "reflection": float(match[3]), "gi": float(match[4]),
                          "ao": float(match[5]), "total": float(match[6]),
                          "per_fragment": float(match[7]), "frames": int(match[8])}
    match = re.search(r"lights per fragment: ([0-9.]+) avg, (\d+) max; at traced hits: "
                      r"([0-9.]+) avg over ([0-9.]+) M hits", text)
    if match:
        result["lights_per_fragment"] = float(match[1])
        result["lights_max"] = int(match[2])
        result["lights_per_hit"] = float(match[3])
        result["hits"] = float(match[4])
    match = re.search(r"temporal confidence: ([0-9.]+)% of pixels", text)
    if match:
        result["confidence"] = float(match[1])

    section = None
    for line in text.splitlines():
        if "[benchmark]" not in line:
            continue
        body = line.split("[benchmark]", 1)[1].rstrip()
        if "render graph, by pass" in body:
            section = "passes"
            continue
        if re.search(r"\bphase\s+CPU ms\s+GPU ms", body):
            section = "phases"
            continue
        if section and re.search(r"^\s+pass\s+CPU ms", body):
            continue
        row = re.match(r"^\s+(\S.*?)\s{2,}(-?[0-9.]+|--)\s+(-?[0-9.]+|--)(?:\s+x(\d+))?\s*(?:--.*)?$",
                       body)
        if row and section:
            name = row[1].strip()
            entry = {"cpu": None if row[2] == "--" else float(row[2]),
                     "gpu": None if row[3] == "--" else float(row[3])}
            if row[4]:
                entry["calls"] = int(row[4])
            result[section][name] = entry
        if body.strip().startswith("whole frame (GPU)"):
            section = None
    return result


def table(rows):
    cameras, passes = [], []
    for row in rows:
        if row["camera"] not in cameras:
            cameras.append(row["camera"])
        if row["pass_"] not in passes:
            passes.append(row["pass_"])

    def get(camera, pass_):
        for row in rows:
            if row["camera"] == camera and row["pass_"] == pass_:
                return row
        return None

    def fmt(value):
        return f"{value:.1f}" if value is not None else "?"

    # The rays columns (WR-16 S0) appear only when a run counted them, so an
    # older JSON prints the table it always did.
    counted = any(row.get("rays") for row in rows)
    header = ("| camera | " + " | ".join(f"{p}: ms / fps" for p in passes)
              + " | water ms | scene ms | busiest cluster |"
              + (" rays M/frame (shadow / water) | rays per fragment | lights per fragment |"
                 if counted else ""))
    lines = [header, "|" + "---|" * (header.count("|") - 1)]
    for camera in cameras:
        cells = []
        for pass_ in passes:
            row = get(camera, pass_)
            cells.append(f"{row['mean']:.1f} / {row['fps']:.1f}"
                         if row and row.get("mean") else "no output")
        last = get(camera, passes[-1]) or {}
        water = (last.get("passes", {}).get("scene/Transparent") or {}).get("gpu")
        scene = (last.get("passes", {}).get("scene/Scene") or {}).get("gpu")
        line = (f"| {camera} | " + " | ".join(cells)
                + f" | {fmt(water)} | {fmt(scene)} | {last.get('busiest', '?')} |")
        if counted:
            rays = last.get("rays") or {}
            line += (f" {fmt(rays.get('total'))} ({fmt(rays.get('shadow'))} / {fmt(rays.get('water'))})"
                     f" | {fmt(rays.get('per_fragment'))} | {fmt(last.get('lights_per_fragment'))} |")
        lines.append(line)
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", default="baseline")
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--passes", default="A,B")
    parser.add_argument("--width", type=int, default=2560)
    parser.add_argument("--height", type=int, default=1440)
    parser.add_argument("--scene", default="scenes/GoldenGateDemo.rage")
    parser.add_argument("--cameras", default="all", help="comma-separated names, or all")
    parser.add_argument("--out", default=str(ROOT / "build" / "bench"))
    parser.add_argument("--table", help="print the table from an earlier run's JSON and exit")
    parser.add_argument("--extra", default="",
                        help="further runtime flags, space-separated, e.g. "
                             "\"--shadow-rays=log,150,300 --light-cutoff=450\" (WR-17)")
    args = parser.parse_args()

    if args.table:
        print(table(json.load(open(args.table, encoding="utf-8"))))
        return

    if not RUNTIME.exists():
        sys.exit(f"no runtime at {RUNTIME}; build Release first")
    if editor_running():
        sys.exit("RageVEditor.exe is running; close it first, it contends for the GPU")

    wanted = [c for c in CAMERAS if args.cameras == "all" or c[0] in args.cameras.split(",")]
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    results = []
    for pass_ in args.passes.split(","):
        for name, flag in wanted:
            code, stdout, stderr, seconds = run(flag, args.frames, args.width, args.height,
                                                args.scene, args.extra.split())
            (out / f"{args.label}_{pass_}_{name.replace(' ', '')}.log").write_text(
                stdout + "\n--- stderr ---\n" + stderr, encoding="utf-8")
            parsed = parse(stdout)
            parsed.update(camera=name, pass_=pass_, exit=code, wall=round(seconds, 1), flag=flag,
                          extra=args.extra)
            results.append(parsed)
            mean = parsed.get("mean")
            print(f"[{pass_}] {name:<10} exit {code}  {seconds:5.1f}s  "
                  + (f"mean {mean:7.3f} ms  {parsed['fps']:5.1f} fps" if mean
                     else "NO BENCHMARK OUTPUT"), flush=True)

    path = out / f"{args.label}.json"
    path.write_text(json.dumps(results, indent=1), encoding="utf-8")
    print()
    print(table(results))
    print(f"\nwrote {path}")


if __name__ == "__main__":
    main()
