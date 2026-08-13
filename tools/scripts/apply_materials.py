#!/usr/bin/env python3
"""Turns the maps fetch_ambientcg.py downloaded into `.rmat` assets.

Split from the fetch because of an ordering constraint that cannot be worked
around: a `.rmat` references its textures by **handle**, and a handle is minted
by the asset registry when it first sees a file and written into a `.meta`
sidecar beside it. Nothing outside the engine can invent one. So the sequence
is necessarily:

    fetch  ->  run the engine once (the registry sidecars every new PNG)
           ->  this, which reads those sidecars and writes the .rmat
           ->  run the engine again (which sidecars the .rmat itself)

Usage:
    python tools/scripts/apply_materials.py SampleProject/assets/materials
"""

import json
import os
import sys


def handle_for(path):
    """The handle the registry minted for a file, from its .meta sidecar."""
    meta = path + ".meta"
    if not os.path.exists(meta):
        return None
    for line in open(meta):
        if line.startswith("Handle:"):
            return line.split(":", 1)[1].strip()
    return None


# Which downloaded map feeds which `.rmat` key.
ROLES = {
    "color": "BaseColor",
    "normal": "Normal",
    "ao": "Occlusion",
    "roughness": "Roughness",
    "metallic": "Metallic",
    "height": "Height",
}


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "SampleProject/assets/materials"

    sidecars = sorted(f for f in os.listdir(target) if f.endswith(".maps.json"))
    if not sidecars:
        sys.exit(f"No .maps.json in {target}; run fetch_ambientcg.py first.")

    missing = []
    for sidecar in sidecars:
        spec = json.load(open(os.path.join(target, sidecar)))
        name = spec["name"]

        maps = {}
        for role, filename in spec["maps"].items():
            key = ROLES.get(role)
            if not key:
                continue
            value = handle_for(os.path.join(target, filename))
            if value is None:
                missing.append(filename)
                continue
            maps[key] = value

        if not maps:
            continue

        tiling = spec.get("tiling", [1.0, 1.0])

        # A scalar *multiplies* its map, so 1 is right when a map exists -- the
        # map is then the whole answer. With no map the scalar stands alone,
        # and 1 means something entirely different.
        #
        # Getting this wrong is not subtle and it is not obviously a material
        # problem either: most ambientCG materials ship no Metalness map at all
        # because they are not metal, so a blanket "Metallic: 1" made soil and
        # brick fully metallic. Metal has no diffuse response, so the ground
        # rendered black -- which reads as a broken texture or a broken light
        # long before it reads as a metallic value nobody set.
        metallic = "1" if "Metallic" in maps else "0"
        roughness = "1" if "Roughness" in maps else "0.85"

        lines = [
            f"Material: {name}",
            "BaseColor: [1, 1, 1, 1]",
            "Emissive: [0, 0, 0, 1]",
            f"Metallic: {metallic}",
            f"Roughness: {roughness}",
            "Occlusion: 1",
            "NormalScale: 1",
            "Specular: 0.5",
            f"HeightScale: {spec.get('height_scale', 0.05):g}",
            f"Tiling: [{tiling[0]:g}, {tiling[1]:g}]",
            "UvOffset: [0, 0]",
            "Maps:",
        ]
        for key in ("BaseColor", "Normal", "Occlusion", "Roughness", "Metallic", "Height"):
            if key in maps:
                lines.append(f"  {key}: {maps[key]}")

        path = os.path.join(target, f"{name}.rmat")
        with open(path, "w") as handle:
            handle.write("\n".join(lines) + "\n")
        print(f"wrote {path} ({len(maps)} maps)")

    if missing:
        print("\nNo handle yet for:")
        for name in missing:
            print("  ", name)
        print("Run the engine once so the registry sidecars them, then re-run this.")


if __name__ == "__main__":
    main()
