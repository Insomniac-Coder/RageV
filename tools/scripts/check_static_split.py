"""The static / moving split, judged by pictures (ENGINE-NOTES 7cx).

Three copies of the showroom, all with every static object marked (the
marking pass has run) and the car left moving, differing in one thing each:

  live      every lamp Half bake -- the lamps render live everywhere. The
            reference: this is what the room looks like when nothing is
            baked but the bounce.
  split     every Half bake lamp turned to Full bake. Static surfaces read
            the lamps from the field; the car is lit live by them and its
            shadow is subtracted from the floor through the moving-only ray.
  carbaked  as `split`, but the car marked static too, so it is baked into
            the field like the walls and its shadow is the field's.

Each is baked from scratch (`--bake=force`), then shot from the showroom
camera with the project's own settings. The claims are diff images, not
means -- the owner's bar -- and the numbers beside them:

  split vs live       should be close everywhere: the field standing in for
                      the lamps on static surfaces, the car identical (lit
                      live in both), the car's shadow present in both.
  carbaked vs split   should differ only where the car's shadow is, since
                      that is the one thing that moved from the ray to the
                      field -- and the car itself, now read from the field's
                      coarse cells instead of lit live.

    python tools/scripts/check_static_split.py [--config Release] [--keep]
                                               [--frames 6000] [--rhi vulkan]
                                               [--shot-flags "--rt-optimisation=off"]

The scene copies, their metas and their bakes are removed afterwards unless
`--keep` is given.
"""

import argparse
import io
import pathlib
import re
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import rvcheck  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENES = ROOT / "SampleProject" / "assets" / "scenes"
BAKED = ROOT / "SampleProject" / "assets" / "baked"
PREFIX = "static_check_"
CAR_ROOT = "porsche_992_gt3_r"


def read(path):
    return io.open(path, encoding="utf-8-sig", newline="").read()


def write(path, text):
    with io.open(path, "w", encoding="utf-8", newline="") as f:
        f.write("﻿" + text)


def entities(lines):
    starts = [i for i, l in enumerate(lines) if l.startswith("  - EntityID:")]
    for n, s in enumerate(starts):
        yield s, (starts[n + 1] if n + 1 < len(starts) else len(lines))


def variant(text, name, full_bake, car_static):
    """One copy of the showroom: lamps and the car as asked, all else as is."""
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(nl)
    lines[0] = re.sub(r"Scene: .*", f"Scene: {name}", lines[0])

    # Parent map, for the car's subtree.
    info = {}
    for s, e in entities(lines):
        eid = lines[s].split(":", 1)[1].strip()
        tag = parent = None
        for i in range(s, e):
            m = re.match(r"\s{6}Tag:\s*(.*)", lines[i])
            if m and tag is None:
                tag = m.group(1).strip()
            m = re.match(r"\s{6}Parent:\s*(\d+)", lines[i])
            if m:
                parent = m.group(1)
        info[eid] = (tag, parent, s, e)

    def under_car(eid):
        seen = set()
        while eid in info and eid not in seen:
            seen.add(eid)
            if info[eid][0] == CAR_ROOT:
                return True
            eid = info[eid][1]
        return False

    out = list(lines)
    edits = []   # (index, replacement or None to insert after)
    for eid, (tag, parent, s, e) in info.items():
        i = s
        while i < e:
            line = lines[i]
            if line.strip() == "LightComponent:" and full_bake:
                # Every Half bake lamp (absent or named) becomes Full bake;
                # Realtime lamps (the headlamps and tail glows) stay live.
                j = i + 1
                had = False
                while j < e and lines[j].startswith("      "):
                    m = re.match(r"(\s{6}Mobility:\s*)(.*)", lines[j])
                    if m:
                        had = True
                        if m.group(2).strip() != "Realtime":
                            edits.append((j, m.group(1) + "Full bake"))
                    j += 1
                if not had:
                    edits.append((i, None))
            if line.strip() == "MeshComponent:" and car_static and under_car(eid):
                j = i + 1
                has = False
                while j < e and lines[j].startswith("      "):
                    if re.match(r"\s{6}Static:", lines[j]):
                        has = True
                        edits.append((j, "      Static: true"))
                    j += 1
                if not has:
                    edits.append((i, "insert-static"))
            i += 1

    for idx, rep in sorted(edits, key=lambda x: x[0], reverse=True):
        if rep is None:
            out.insert(idx + 1, "      Mobility: Full bake")
        elif rep == "insert-static":
            out.insert(idx + 1, "      Static: true")
        else:
            out[idx] = rep
    return nl.join(out)


def run(exe, args, log_path):
    project = ROOT / "SampleProject"
    result = subprocess.run([str(exe), f"--project={project}", *args], cwd=exe.parent,
                            capture_output=True, text=True)
    log = (result.stdout or "") + (result.stderr or "")
    log_path.write_text(log, encoding="utf-8")
    return result.returncode, log


def bake(exe, rhi, scene, frames, logs):
    code, log = run(exe, [f"--rhi={rhi}", f"--scene=scenes/{scene}.rage", "--render-defaults=off",
                          "--bake=force", f"--benchmark={frames}", "--frame-time=0.000001",
                          "--width=960", "--height=540"], logs / f"{scene}_bake.log")
    done = re.search(r"Bake: done in (\d+) frames", log)
    gave_up = "Bake: gave up" in log
    if code != 0 or not done or gave_up:
        print(f"FAIL: bake of {scene} exited {code}; done={bool(done)} gave_up={gave_up}")
        print(log[-3000:])
        sys.exit(1)
    return int(done.group(1))


def shoot(exe, rhi, scene, path, logs, frame=60, extra=()):
    code, log = run(exe, [f"--rhi={rhi}", f"--scene=scenes/{scene}.rage", "--render-defaults=off",
                          "--frame-time=0.000001", f"--screenshot-frame={frame}",
                          f"--screenshot={path}", "--aa=none",
                          "--width=1280", "--height=720", *extra], logs / f"{scene}_shot.log")
    if code != 0 or not pathlib.Path(path).exists():
        print(f"FAIL: {scene} exited {code} / no image")
        print(log[-3000:])
        sys.exit(1)
    fell_back = "no bake matches" in log or "Baked, but this scene has no" in log
    if fell_back:
        print(f"FAIL: {scene} rendered without its bake (fell back to realtime)")
        sys.exit(1)
    return rvcheck.require_drawn(np.asarray(Image.open(path).convert("RGB")).astype(float), scene)


def diff(a, b, path):
    d = np.abs(a - b)
    Image.fromarray(np.clip(d * 4.0, 0, 255).astype(np.uint8)).save(path)   # x4 gain
    per_pixel = d.max(axis=2)
    return {
        "mean": float(d.mean()),
        "p99": float(np.percentile(per_pixel, 99)),
        "over6": float((per_pixel > 6).mean() * 100.0),
        "over24": float((per_pixel > 24).mean() * 100.0),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="Release")
    parser.add_argument("--rhi", default="vulkan")
    parser.add_argument("--frames", type=int, default=6000)
    parser.add_argument("--keep", action="store_true")
    # Extra runtime flags for the shots alone, e.g. "--rt-optimisation=off" to
    # take WR-17's shadow-ray thinning out of the live reference: the
    # subtractive ray is never thinned, so under the preset the live frame is
    # the less shadowed of the two under the car.
    parser.add_argument("--shot-flags", default="")
    args = parser.parse_args()

    exe = ROOT / "build" / "bin" / args.config / "RageVRuntime" / "RageVRuntime.exe"
    if not exe.exists():
        print(f"FAIL: {exe} does not exist; build {args.config} first")
        return 1
    rvcheck.require_current_shaders(ROOT, args.config)

    out = ROOT / "build" / "static_split"
    out.mkdir(parents=True, exist_ok=True)
    logs = out / "logs"
    logs.mkdir(exist_ok=True)

    source = read(SCENES / "showroom.rage")
    variants = {
        "live": (False, False),
        "split": (True, False),
        "carbaked": (True, True),
    }
    made = []
    try:
        for name, (full, car) in variants.items():
            scene = PREFIX + name
            write(SCENES / f"{scene}.rage", variant(source, scene, full, car))
            made.append(scene)

        frames = {}
        for scene in made:
            frames[scene] = bake(exe, args.rhi, scene, args.frames, logs)
            print(f"{scene}: baked in {frames[scene]} frames")

        shots = {}
        for scene in made:
            shots[scene] = shoot(exe, args.rhi, scene, out / f"{scene}.png", logs,
                                 extra=tuple(args.shot_flags.split()))

        results = {}
        for a, b in (("split", "live"), ("carbaked", "split"), ("carbaked", "live")):
            results[(a, b)] = diff(shots[PREFIX + a], shots[PREFIX + b], out / f"diff_{a}_vs_{b}.png")
            r = results[(a, b)]
            print(f"{a} vs {b}: mean {r['mean']:.2f} levels, p99 {r['p99']:.1f}, "
                  f"{r['over6']:.2f}% of pixels over 6 levels, {r['over24']:.2f}% over 24")
        print(f"images in {out}")
        return 0
    finally:
        if not args.keep:
            for scene in made:
                for suffix in (".rage", ".rage.meta"):
                    p = SCENES / f"{scene}{suffix}"
                    if p.exists():
                        p.unlink()
                shutil.rmtree(BAKED / scene, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
