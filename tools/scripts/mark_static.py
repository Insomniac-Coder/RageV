"""Mark the objects of existing scenes static (ENGINE-NOTES 7cx).

The static flag landed on 2026-09-03 with its default *off*: a thing moves
until an author says otherwise. Every scene written before that day says
nothing, so under the new rule -- the bake sees static objects only -- each of
them would bake an empty room until somebody ticked the box on every wall.
This is the one-time pass that ticks it (owner's call, "option 1", 2026-09-03),
and it is kept because the rule it applies is the rule a person would apply by
hand, written down once:

    An object is MOVING when it, or any ancestor of it, carries a script
    (Managed or Native), an animator, or a rigid body that is not Static --
    or when its root is named in the per-scene exclusion table below (the
    showroom's car, which nothing in the file says moves). Everything else,
    meshes and terrains alike, is STATIC.

Skinned meshes are never static in the engine whatever the file says; the
animator rule keeps the file honest about them too.

Only `MeshComponent:` and `TerrainComponent:` blocks are touched, by inserting
one `Static: true` line directly under the block's key. Nothing is reordered
or rewritten, so a diff shows exactly what changed. Blocks that already carry
a `Static:` key are left alone, which makes the pass idempotent.

    python tools/scripts/mark_static.py            # every .rage under the repo
    python tools/scripts/mark_static.py --dry-run  # report only
    python tools/scripts/mark_static.py a.rage b.rage
"""

import argparse
import io
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# Roots whose whole subtree moves although nothing in the file says so. The
# showroom car is driven by hand in the editor and by scripts that address it
# by name at runtime; the file itself carries no script on it.
EXCLUDED_ROOTS = {
    "showroom.rage": {"porsche_992_gt3_r"},
    "showroom2.rage": {"porsche_992_gt3_r"},
    "showroom-mark85.rage": {"porsche_992_gt3_r"},
}

MOVING_COMPONENTS = ("ManagedScriptComponent:", "NativeScriptComponent:",
                     "AnimatorComponent:")


def parse_entities(lines):
    """Yield (start, end) line ranges, one per entity block."""
    starts = [i for i, line in enumerate(lines) if line.startswith("  - EntityID:")]
    for n, start in enumerate(starts):
        end = starts[n + 1] if n + 1 < len(starts) else len(lines)
        yield start, end


def block_lines(lines, start, end, key):
    """The line indices of a component block `key` inside an entity."""
    at = None
    for i in range(start, end):
        if lines[i].startswith("    ") and not lines[i].startswith("     "):
            at = i if lines[i].strip() == key else None
            if at is not None:
                rows = []
                for j in range(i + 1, end):
                    if lines[j].startswith("      "):
                        rows.append(j)
                    else:
                        break
                return at, rows
    return None, []


def is_moving_body(lines, rows):
    for j in rows:
        m = re.match(r"\s*Type:\s*(\S+)", lines[j])
        if m:
            return m.group(1) != "Static"
    return True


def mark(path, dry_run):
    text = io.open(path, encoding="utf-8-sig", newline="").read()
    bom = io.open(path, "rb").read(3) == b"\xef\xbb\xbf"
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(nl)

    entities = {}
    order = []
    for start, end in parse_entities(lines):
        eid = lines[start].split(":", 1)[1].strip()
        tag = None
        parent = None
        for i in range(start, end):
            m = re.match(r"\s{6}Tag:\s*(.*)", lines[i])
            if m and tag is None:
                tag = m.group(1).strip()
            m = re.match(r"\s{6}Parent:\s*(\d+)", lines[i])
            if m:
                parent = m.group(1)
        moving_here = any(block_lines(lines, start, end, key)[0] is not None
                          for key in MOVING_COMPONENTS)
        body_at, body_rows = block_lines(lines, start, end, "RigidBodyComponent:")
        if body_at is not None and is_moving_body(lines, body_rows):
            moving_here = True
        entities[eid] = dict(start=start, end=end, tag=tag or "?", parent=parent,
                             moving=moving_here)
        order.append(eid)

    excluded = EXCLUDED_ROOTS.get(path.name, set())

    def moving(eid):
        seen = set()
        root = eid
        while eid in entities and eid not in seen:
            seen.add(eid)
            if entities[eid]["moving"]:
                return True
            root = eid
            eid = entities[eid]["parent"]
            if eid is None:
                break
        return entities[root]["tag"] in excluded if root in entities else False

    inserts = []   # (line index to insert before, text)
    kept_moving = []
    already = 0
    for eid in order:
        e = entities[eid]
        for key in ("MeshComponent:", "TerrainComponent:"):
            at, rows = block_lines(lines, e["start"], e["end"], key)
            if at is None:
                continue
            if any(re.match(r"\s{6}Static:", lines[j]) for j in rows):
                already += 1
                continue
            if moving(eid):
                kept_moving.append(f"{e['tag']} ({key[:-1]})")
                continue
            inserts.append((at + 1, "      Static: true"))

    if inserts and not dry_run:
        for at, row in sorted(inserts, reverse=True):
            lines.insert(at, row)
        out = nl.join(lines)
        with io.open(path, "w", encoding="utf-8", newline="") as f:
            if bom:
                f.write("﻿")
            f.write(out)

    return len(inserts), already, kept_moving


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("scenes", nargs="*", help="scene files; default: every .rage in the repo")
    parser.add_argument("--dry-run", action="store_true", help="report, change nothing")
    args = parser.parse_args()

    if args.scenes:
        paths = [pathlib.Path(s) for s in args.scenes]
    else:
        paths = sorted(p for p in ROOT.rglob("*.rage")
                       if "build" not in p.parts and "bin" not in p.parts)

    total = 0
    for path in paths:
        marked, already, kept = mark(path, args.dry_run)
        total += marked
        if marked or kept or already:
            rel = path.relative_to(ROOT) if path.is_absolute() and ROOT in path.parents else path
            print(f"{rel}: {marked} marked static, {already} already flagged, "
                  f"{len(kept)} left moving")
            for tag in kept:
                print(f"    moving: {tag}")
    print(f"{'would mark' if args.dry_run else 'marked'} {total} blocks in {len(paths)} scenes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
