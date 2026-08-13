#!/usr/bin/env python3
"""Fetches CC0 PBR materials from ambientCG and writes RageV `.rmat` assets.

ambientCG publishes everything under CC0, so these can live in the repository
with no attribution requirement. What it *does not* publish is a texture set in
the shape a glTF-style shader wants, and that is most of what this script is
for:

  * Not every material has a Metalness map -- a brick is never metal, so the
    file is simply absent, and the material falls back to its scalar. The
    shader takes roughness and metallic as separate maps precisely so no
    repacking stands between a downloaded material and the engine.
  * Displacement is downloaded and discarded. There is no displacement or
    parallax in the shader, and shipping a map nothing reads would be four
    megabytes of lie per material.
  * The normal map wanted is **NormalGL**, not NormalDX. They differ in the
    sign of the green channel, and picking the wrong one lights every dent as
    a bump -- which looks fine until you compare it with the colour map.

Usage:
    python tools/scripts/fetch_ambientcg.py SampleProject/assets/materials
    python tools/scripts/fetch_ambientcg.py SampleProject/assets/materials --size 512
"""

import argparse
import io
import json
import os
import sys
import urllib.request
import zipfile

try:
    from PIL import Image
except ImportError:
    sys.exit("This needs Pillow: python -m pip install pillow")


# The four surfaces, and what each is for in the demo scene. Tiling is in the
# material because the primitives' UVs span 0..1 per face whatever their size --
# see MaterialParams::UvTransform.
MATERIALS = [
    ("Metal053C",  "rusted_steel", (1.0, 1.0)),
    ("Ground037",  "soil",         (5.0, 5.0)),
    ("WoodFloor007", "wood",        (1.0, 1.0)),
    ("Bricks023",  "brick",        (1.5, 1.5)),
]

# ambientCG's suffix -> what RageV calls it. Displacement is deliberately absent.
WANTED = {
    "Color": "color",
    "NormalGL": "normal",
    "AmbientOcclusion": "ao",
    "Roughness": "roughness",
    "Metalness": "metallic",
}


def fetch(asset, resolution):
    url = f"https://ambientcg.com/get?file={asset}_{resolution}-PNG.zip"
    print(f"  downloading {asset}_{resolution}-PNG.zip")
    request = urllib.request.Request(url, headers={"User-Agent": "RageV-asset-fetch"})
    with urllib.request.urlopen(request, timeout=180) as response:
        return zipfile.ZipFile(io.BytesIO(response.read()))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("target")
    parser.add_argument("--resolution", default="1K", help="ambientCG resolution to fetch")
    parser.add_argument("--size", type=int, default=0,
                        help="resize every map to this square size (0 keeps the source)")
    args = parser.parse_args()

    os.makedirs(args.target, exist_ok=True)
    total = 0

    for asset, name, tiling in MATERIALS:
        print(f"{name} ({asset})")
        archive = fetch(asset, args.resolution)

        written = {}
        for suffix, role in WANTED.items():
            member = f"{asset}_{args.resolution}-PNG_{suffix}.png"
            if member not in archive.namelist():
                print(f"  no {suffix} map; skipping")
                continue

            image = Image.open(io.BytesIO(archive.read(member)))
            image = image.convert("RGB")

            # Normal maps keep their full resolution; everything else is
            # downsampled.
            #
            # Because for a normal map, resolution *is* the relief. Halving one
            # averages neighbouring normals toward flat, and measurably so:
            # Ground048 loses a third of its deviation going 1K to 512 (27.6 to
            # 19.4), which is the difference between ground that reads as
            # ground and ground that reads as a photograph of ground. A colour
            # or roughness map loses only detail nobody was looking at.
            resize = args.size and role != "normal"
            if resize and image.size[0] != args.size:
                image = image.resize((args.size, args.size), Image.LANCZOS)

            path = os.path.join(args.target, f"{name}_{role}.png")
            image.save(path, optimize=True)
            written[role] = os.path.basename(path)
            total += os.path.getsize(path)


        # The .rmat is written with *paths* here and rewritten with handles by
        # apply_materials.py once the registry has minted them. A handle cannot
        # be invented: it lives in the .meta the registry creates.
        sidecar = os.path.join(args.target, f"{name}.maps.json")
        with open(sidecar, "w") as handle:
            json.dump({"name": name, "maps": written, "tiling": tiling}, handle, indent=1)

        print(f"  wrote {len(written)} maps")

    print(f"\n{total / 1048576:.1f} MB written to {args.target}")
    print("Now run tools/scripts/apply_materials.py to turn these into .rmat assets.")


if __name__ == "__main__":
    main()
