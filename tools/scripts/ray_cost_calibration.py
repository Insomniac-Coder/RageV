"""What a ray costs, by kind, on this machine -- WR-16 step S0's calibration.

docs/RAY-BUDGET-DESIGN.md Part III 4.1 and Part IV "M0, prepared" (4): the
controller needs the time of rays that are cast inside a shading pass, and
gets it as the counted rays times a cost per ray that is *measured once* by
isolation runs and stored in the project (RenderSettings::RayCost*), never
learned online. This script is the protocol, written down so it can be
re-run whenever the hardware or the shaders change.

Every arm is one --benchmark run of the runtime on the night scene, parsed
by bench_night's parser: the whole-frame GPU time, the by-pass table, and
the ray counters' lines (rays per frame by kind, lights per fragment, hits).

    python tools/scripts/ray_cost_calibration.py --label wr16-s0
    python tools/scripts/ray_cost_calibration.py --table build/bench/wr16-s0_calibration.json

The arms, per camera:

    base                the scene as configured (RT optimisation at Quality)
    casting N           --casting-lights=N for N in the sweep: only the first N
                        positional lights keep a shadow ray (the owner's Debug
                        Pass B). The frame time against the counted shadow rays
                        is a line whose slope is the shadow ray's cost, and
                        whose straightness says whether the frame is ray-bound.
    hit-lights off      --hit-lights=off: the light walk at traced hits skipped.
                        The saving over the counted hits is the walk's cost per
                        hit -- WR-10's number, and part of what a water ray costs.
    ray-rate 2          --ray-rate=2: the water's mirror and refraction rays one
                        per 2x2 quad. The saving over the rays removed is the
                        water ray's *marginal* cost -- lower than its average,
                        because the three idle lanes wait for the fourth
                        (WR-18: a 4x ray cut was a 1.25x time cut).
    reflections off     --rt-reflections=off: the opaque mirror rays gone (and
                        the water's mirror with them, so this arm is read on
                        the camera with the least water).

The costs come out in milliseconds per million rays, over the whole GPU
frame -- the number the controller will multiply a count by. AO and GI have
passes of their own, so theirs are the pass time over the pass's rays.

Two checks the counters themselves must pass, which is S0's acceptance:
the quad arm must remove three quarters of the water's rays (the rate is
one lane in four by construction), and the sweep's points must sit on
their fitted line within 5% -- a count that drifted from the work would
bend it. Both are printed with the table.

This laptop's GPU drifts about a millisecond over a session (HANDOFF), so
the arms are interleaved per camera -- base, arm, base, arm -- and each
arm's saving is taken against the base run beside it, never against a base
from ten minutes earlier.
"""
import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import bench_night  # noqa: E402

ROOT = bench_night.ROOT

# The three cameras the design's bars are quoted on, plus the Deck for the
# reflection arm: the least water of the eight, so the steel's mirror rays
# are most of what --rt-reflections=off removes there.
CAMERAS = [c for c in bench_night.CAMERAS if c[0] in ("Headland", "Pier", "Glitter", "Deck")]

SWEEP = [147, 75, 32, 16, 0]


def run_arm(camera_flag, frames, width, height, scene, extra):
    code, stdout, stderr, seconds = bench_night.run(camera_flag, frames, width, height, scene, extra)
    parsed = bench_night.parse(stdout)
    parsed.update(exit=code, wall=round(seconds, 1), extra=" ".join(extra))
    if "gpu" not in parsed or "rays" not in parsed:
        parsed["error"] = "no benchmark report or no ray counters in the output"
    # The loader's fallback lines: a run that measured a dark scene is not a run.
    for bad in ("not credible", "no bake matches"):
        if bad in stdout or bad in stderr:
            parsed["error"] = f"the log carries '{bad}': the field did not load"
    return parsed


def pass_gpu(result, name):
    return ((result.get("passes") or {}).get(name) or {}).get("gpu")


def fit_line(points):
    """Least squares y = a + b x over (x, y) pairs; returns a, b and the
    largest relative residual."""
    n = len(points)
    if n < 2:
        return None
    sx = sum(p[0] for p in points)
    sy = sum(p[1] for p in points)
    sxx = sum(p[0] * p[0] for p in points)
    sxy = sum(p[0] * p[1] for p in points)
    denominator = n * sxx - sx * sx
    if abs(denominator) < 1e-12:
        return None
    b = (n * sxy - sx * sy) / denominator
    a = (sy - b * sx) / n
    worst = 0.0
    for x, y in points:
        predicted = a + b * x
        if predicted > 0:
            worst = max(worst, abs(y - predicted) / predicted)
    return a, b, worst


def analyse(results):
    """Costs per camera and the two counter checks, from a list of arm runs."""
    out = {"cameras": {}, "checks": []}
    by_camera = {}
    for row in results:
        by_camera.setdefault(row["camera"], []).append(row)

    for camera, rows in by_camera.items():
        rows = [r for r in rows if "error" not in r]
        report = {}

        def arm(name):
            for r in rows:
                if r["arm"] == name:
                    return r
            return None

        # Each arm's base is the base run recorded beside it (interleaved).
        def base_for(row):
            for r in rows:
                if r["arm"] == "base" and r["pair"] == row["pair"]:
                    return r
            return arm("base")

        # The shadow sweep, as savings against the base run beside each arm:
        # x = shadow rays removed (M), y = GPU ms saved. A line through the
        # origin -- a ray removed saves its cost and nothing else -- whose
        # slope is the shadow ray's cost. The check is per arm and on the
        # frame: the base less the removed rays at that cost must land
        # within 5% of the frame the arm measured. Relative to the frame,
        # not to the saving, because an arm that removes almost nothing
        # (casting 147 on a camera whose cells never held those lamps) has
        # a saving of noise and a frame that is still right.
        points = []
        sweep = []
        for r in rows:
            if not r["arm"].startswith("casting"):
                continue
            b0 = base_for(r)
            if not b0:
                continue
            removed = b0["rays"]["shadow"] - r["rays"]["shadow"]
            saved = b0["gpu"] - r["gpu"]
            points.append((removed, saved))
            sweep.append((r["arm"], b0["gpu"], r["gpu"], removed, saved))
        sxx = sum(x * x for x, _ in points)
        if sxx > 1e-9:
            slope = sum(x * y for x, y in points) / sxx
            report["shadow_ms_per_M"] = slope
            worst = 0.0
            for name, base_gpu, arm_gpu, removed, saved in sweep:
                predicted = base_gpu - slope * removed
                worst = max(worst, abs(predicted - arm_gpu) / max(arm_gpu, 1e-6))
            report["sweep_worst_frame_error"] = worst
            report["sweep"] = sweep
            out["checks"].append((camera, "the shadow sweep's frames follow the counted rays at one "
                                          "cost, within 5%", worst <= 0.05,
                                  f"worst frame error {worst * 100:.1f}%"))

        # The hit walk, per million hits. The counts that must not move are
        # the traced surfaces' -- water, reflection and AO rays, and the hits
        # -- since the walk is what is skipped, not the rays that reach it.
        # The shadow count *does* move: the sun's ray at a hit lives inside
        # the walk and goes with it.
        if (r := arm("hit-lights off")) and (b0 := base_for(r)):
            hits = b0.get("hits") or 0.0
            if hits > 0:
                report["hit_walk_ms_per_M_hits"] = (b0["gpu"] - r["gpu"]) / hits
                report["hit_walk_saving_ms"] = b0["gpu"] - r["gpu"]
            traced_base = b0["rays"]["water"] + b0["rays"]["reflection"] + b0["rays"]["ao"]
            traced_arm = r["rays"]["water"] + r["rays"]["reflection"] + r["rays"]["ao"]
            same_rays = abs(traced_arm - traced_base) <= 0.05 * max(traced_base, 1e-6)
            same_hits = abs((r.get("hits") or 0.0) - hits) <= 0.05 * max(hits, 1e-6)
            out["checks"].append((camera, "hit-lights off leaves the traced rays and the hits "
                                          "unchanged", same_rays and same_hits,
                                  f"{traced_arm:.2f} M traced against {traced_base:.2f} M, "
                                  f"{(r.get('hits') or 0.0):.2f} M hits against {hits:.2f} M"))

        # The water's rays: the quad arm's marginal cost, and the counter check.
        if (r := arm("ray-rate 2")) and (b0 := base_for(r)):
            removed = b0["rays"]["water"] - r["rays"]["water"]
            if removed > 0:
                report["water_marginal_ms_per_M"] = (b0["gpu"] - r["gpu"]) / removed
                report["water_saving_ms"] = b0["gpu"] - r["gpu"]
            ratio = r["rays"]["water"] / max(b0["rays"]["water"], 1e-6)
            out["checks"].append((camera, "ray-rate 2 leaves one water ray in four",
                                  abs(ratio - 0.25) <= 0.05 * 0.25 + 0.01,
                                  f"{ratio * 100:.1f}% of the base's water rays"))
            # The average cost of a water ray, which is what the controller
            # multiplies: the water pass's time over its rays, less the shadow
            # rays it also carries at the calibrated shadow cost.
            water_pass = pass_gpu(b0, "scene/Transparent")
            if water_pass and "shadow_ms_per_M" in report:
                # The water pass casts the water's shadow rays too; their share
                # of the pass is not separable here, so the average is quoted
                # as an upper bound: the pass over its mirror and refraction
                # rays alone.
                report["water_pass_ms_per_M_upper"] = water_pass / max(b0["rays"]["water"], 1e-6)

        # The opaque mirror rays. --rt-reflections=off takes the water's
        # mirror ray with them (the same define), so the saving is corrected
        # by the water rays removed at the water's marginal cost; the raw
        # figure is kept beside it, and the camera with the least water is
        # the one this arm runs on for the correction to stay small.
        if (r := arm("reflections off")) and (b0 := base_for(r)):
            removed = b0["rays"]["reflection"] - r["rays"]["reflection"]
            water_removed = b0["rays"]["water"] - r["rays"]["water"]
            if removed > 0:
                saving = b0["gpu"] - r["gpu"]
                report["reflection_saving_ms"] = saving
                report["reflection_arm_water_rays_removed_M"] = water_removed
                report["reflection_ms_per_M_raw"] = saving / removed
                water_cost = report.get("water_marginal_ms_per_M")
                if water_cost is not None:
                    report["reflection_ms_per_M"] = max(saving - water_removed * water_cost, 0.0) / removed
                else:
                    report["reflection_ms_per_M"] = saving / removed

        # AO and GI from their own passes.
        if (b0 := arm("base")):
            # The occlusion pass: the graph names the compute stage "SSAO
            # compute" whichever form runs in it, and under rays that stage
            # is rtao_compute -- the counted AO rays say which it was.
            ao_pass = pass_gpu(b0, "scene/SSAO compute")
            if ao_pass and b0["rays"]["ao"] > 0:
                report["ao_ms_per_M"] = ao_pass / b0["rays"]["ao"]
                report["ao_pass_ms"] = ao_pass
            gi_pass = None
            for name, value in (b0.get("passes") or {}).items():
                if "RT GI trace" in name:
                    gi_pass = value.get("gpu")
            if gi_pass and b0["rays"]["gi"] > 0:
                report["gi_ms_per_M"] = gi_pass / b0["rays"]["gi"]
            report["base"] = {"gpu": b0["gpu"], "rays": b0["rays"],
                              "lights_per_fragment": b0.get("lights_per_fragment"),
                              "lights_max": b0.get("lights_max"),
                              "lights_per_hit": b0.get("lights_per_hit"),
                              "hits": b0.get("hits"),
                              "water_pass": pass_gpu(b0, "scene/Transparent"),
                              "scene_pass": pass_gpu(b0, "scene/Scene")}

        out["cameras"][camera] = report
    return out


def fmt(value, digits=2):
    return f"{value:.{digits}f}" if isinstance(value, (int, float)) else "?"


def table(results):
    analysis = analyse(results)
    lines = []
    lines.append("| camera | GPU ms | rays M (shadow / water / refl / AO) | lights/frag avg, max "
                 "| hits M, lights/hit | shadow ms/M | hit walk ms/M hits | water marginal ms/M "
                 "| refl ms/M | AO ms/M |")
    lines.append("|" + "---|" * 10)
    for camera, report in analysis["cameras"].items():
        base = report.get("base") or {}
        rays = base.get("rays") or {}
        lines.append(
            f"| {camera} | {fmt(base.get('gpu'), 1)} | {fmt(rays.get('total'))} "
            f"({fmt(rays.get('shadow'))} / {fmt(rays.get('water'))} / {fmt(rays.get('reflection'))} "
            f"/ {fmt(rays.get('ao'))}) | {fmt(base.get('lights_per_fragment'), 1)}, "
            f"{base.get('lights_max', '?')} | {fmt(base.get('hits'))}, "
            f"{fmt(base.get('lights_per_hit'), 1)} | {fmt(report.get('shadow_ms_per_M'))} "
            f"| {fmt(report.get('hit_walk_ms_per_M_hits'))} "
            f"| {fmt(report.get('water_marginal_ms_per_M'))} "
            f"| {fmt(report.get('reflection_ms_per_M'))} | {fmt(report.get('ao_ms_per_M'))} |")
    lines.append("")
    lines.append("The shadow sweep, arm by arm (base ms -> arm ms; shadow rays removed M; saved ms):")
    for camera, report in analysis["cameras"].items():
        for name, base_gpu, arm_gpu, removed, saved in report.get("sweep", []):
            lines.append(f"  {camera:<9} {name:<12} {base_gpu:6.1f} -> {arm_gpu:6.1f}   "
                         f"removed {removed:5.2f} M   saved {saved:5.2f} ms")
        if "shadow_ms_per_M" in report:
            lines.append(f"  {camera:<9} slope {fmt(report['shadow_ms_per_M'])} ms per M shadow rays, "
                         f"worst frame error {report['sweep_worst_frame_error'] * 100:.1f}%")
    lines.append("")
    lines.append("Checks:")
    for camera, what, ok, detail in analysis["checks"]:
        lines.append(f"  {'pass' if ok else 'FAIL'}  {camera}: {what} ({detail})")
    return "\n".join(lines), analysis


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", default="wr16-s0")
    parser.add_argument("--frames", type=int, default=200)
    parser.add_argument("--width", type=int, default=2560)
    parser.add_argument("--height", type=int, default=1440)
    parser.add_argument("--scene", default="scenes/GoldenGateDemo.rage")
    parser.add_argument("--cameras", default="all")
    parser.add_argument("--out", default=str(ROOT / "build" / "bench"))
    parser.add_argument("--table", help="print the table from an earlier run's JSON and exit")
    args = parser.parse_args()

    if args.table:
        text, _ = table(json.load(open(args.table, encoding="utf-8")))
        print(text)
        return

    if not bench_night.RUNTIME.exists():
        sys.exit(f"no runtime at {bench_night.RUNTIME}; build Release first")
    if bench_night.editor_running():
        sys.exit("RageVEditor.exe is running; close it first, it contends for the GPU")

    wanted = [c for c in CAMERAS if args.cameras == "all" or c[0] in args.cameras.split(",")]
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    arms = [(f"casting {n}", [f"--casting-lights={n}"]) for n in SWEEP]
    arms += [("hit-lights off", ["--hit-lights=off"]),
             ("ray-rate 2", ["--ray-rate=2"])]

    results = []
    for name, flag in wanted:
        camera_arms = list(arms)
        if name == "Deck":
            camera_arms.append(("reflections off", ["--rt-reflections=off"]))
        pair = 0
        for arm_name, extra in camera_arms:
            # Interleaved: a base beside every arm, so the drift is in neither.
            for label, flags in (("base", []), (arm_name, extra)):
                parsed = run_arm(flag, args.frames, args.width, args.height, args.scene, flags)
                parsed.update(camera=name, arm=label, pair=pair, flag=flag)
                results.append(parsed)
                stem = f"{args.label}_calibration_{name.replace(' ', '')}_{pair}_{label.replace(' ', '')}"
                (out / f"{stem}.log").write_text(
                    json.dumps(parsed, indent=1), encoding="utf-8")
                gpu = parsed.get("gpu")
                rays = (parsed.get("rays") or {}).get("total")
                print(f"{name:<9} {label:<16} exit {parsed['exit']}  {parsed['wall']:5.1f}s  "
                      + (f"GPU {gpu:6.2f} ms  rays {rays:6.2f} M" if gpu is not None and rays is not None
                         else parsed.get("error", "NO OUTPUT")), flush=True)
            pair += 1

    path = out / f"{args.label}_calibration.json"
    path.write_text(json.dumps(results, indent=1), encoding="utf-8")
    text, analysis = table(results)
    print()
    print(text)
    (out / f"{args.label}_calibration_analysis.json").write_text(
        json.dumps(analysis, indent=1, default=str), encoding="utf-8")
    print(f"\nwrote {path}")


if __name__ == "__main__":
    main()
