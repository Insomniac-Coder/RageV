#!/usr/bin/env python3
"""Import ambientCG material sets that were downloaded by hand.

`fetch_ambientcg.py` downloads its four; this takes zips already sitting on
disk, which is what happens when somebody picks the sets themselves rather
than taking the ones this project started with. Same CC0 licence, same archive
layout, and the same traps -- restated here rather than assumed, because this
script is the one a second set of materials will come through:

  * **NormalGL, never NormalDX.** They differ in the sign of the green
    channel, and the wrong one lights every dent as a bump. It looks like a
    lighting bug forever afterwards and nothing downstream can tell.

  * **The Metalness map is deliberately dropped**, and that is the opposite
    of laziness. Metal060B's metalness measures a solid 255 -- bare steel --
    and a metal's F0 *is* its albedo, so tinting one gives a coloured mirror.
    The Golden Gate is **painted** steel: the paint is a dielectric film over
    the metal, so `Metallic` stays 0 and the orange comes from `BaseColor`
    multiplying a near-neutral map. Importing the metalness would produce an
    orange chrome bridge, which is a toy.

  * **Displacement is kept.** It drives the parallax mapping, which is what
    makes a surface read as deep rather than as a photograph of something
    deep.

**Every map is written as its own file, including for terrain.** The layered
terrain variant can only bind three samplers per layer and so needs roughness,
occlusion and height in one texture's three channels -- but that packing is
done by the engine, at material load, from whatever maps the `.rmat` names
(`TextureLoader::PackChannels`). It is a fact about how one shader binds
things, not about how a surface is authored, so nothing here has to know.

**The JPEGs are copied through byte for byte rather than re-encoded.** The
engine loads JPEG already (`Asset.cpp`, `ImportCache.cpp`), a re-encode could
only lose quality, and PNGs of these would be roughly six times the size in a
repository that has to carry them.

`.meta` is minted here as FNV-1a of the file name -- the same convention
`make_bridge_models.write_materials` computes its handles with, so a generator
can reference a texture the registry has never opened. **Never rewritten when
one exists**: the handle would change and every material pointing at the old
one would quietly fall back to the renderer's default.

Usage:
    python tools/scripts/import_downloaded_materials.py
    python tools/scripts/import_downloaded_materials.py --source D:/downloads
"""

import argparse
import pathlib
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEXTURES = ROOT / "SampleProject" / "assets" / "textures"

# The archive stem on disk, and what this project calls the set. `acg_` says
# where it came from, which is worth carrying in the name of a file that is
# CC0 rather than authored here.
SETS = [
    ("Rock050_1K-JPG",      "acg_rock"),
    ("Gravel032_1K-JPG",    "acg_gravel"),
    ("Ground048_1K-JPG",    "acg_ground"),
    # **Concrete025, not 044D, and the choice was measured.** The old one
    # tiled as a visible grid of chevrons on the pier, which is a directional
    # normal map repeating -- and no amount of macro variation hides a pattern
    # that recurs, because macro modulates a repeat rather than removing it.
    # Scored over nine candidates by the angular spread of their power spectrum
    # and by how much energy sits below a sixteenth of Nyquist:
    #
    #     Concrete044D   aniso 1.90   low-f 0.27      (what this was)
    #     Concrete025    aniso 1.58   low-f 0.10      (what it is)
    #
    # The second number matters more. Low-frequency content is exactly what
    # appears once per repeat and reads as a lattice; the same rule the bridge's
    # own procedural maps were rebuilt around after the first lattice complaint.
    ("Concrete025_1K-JPG", "acg_concrete"),
    ("Road001_1K-JPG",      "acg_road"),
    ("Metal060B_1K-JPG",    "acg_steel"),
]

# ambientCG's suffix -> what RageV calls it. Metalness is absent on purpose;
# see the docstring.
WANTED = {
    "Color": "color",
    "NormalGL": "normal",
    "Roughness": "roughness",
    "AmbientOcclusion": "ao",
    "Displacement": "height",
}


def fnv1a(data):
    h = 0xCBF29CE484222325
    for byte in data:
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def handle_for(name):
    h = fnv1a(name.encode("utf-8"))
    return h or 0x7261676556444D4F


def ensure_meta(path):
    """Mint the sidecar if the registry has not. Never overwrite one."""
    meta = path.with_name(path.name + ".meta")
    if meta.exists():
        return False
    meta.write_text("Handle: {0}\nType: Texture\nSourceHash: {1}".format(
        handle_for(path.name), fnv1a(path.read_bytes())),
        encoding="utf-8", newline="\n")
    return True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", default=str(pathlib.Path.home() / "Downloads"),
                        help="where the ambientCG zips are")
    args = parser.parse_args(argv)

    source = pathlib.Path(args.source)
    TEXTURES.mkdir(parents=True, exist_ok=True)
    total = 0

    for stem, name in SETS:
        archive = source / (stem + ".zip")
        if not archive.exists():
            print("  {0:14} MISSING {1}".format(name, archive))
            continue

        written, minted = [], 0
        with zipfile.ZipFile(archive) as zf:
            members = set(zf.namelist())
            for suffix, role in WANTED.items():
                member = "{0}_{1}.jpg".format(stem, suffix)
                if member not in members:
                    continue
                out = TEXTURES / "{0}_{1}.jpg".format(name, role)
                out.write_bytes(zf.read(member))
                minted += 1 if ensure_meta(out) else 0
                written.append(role)
                total += out.stat().st_size

        skipped = "  (metalness dropped)" if "{0}_Metalness.jpg".format(stem) in members else ""
        print("  {0:14} {1:38} {2} meta minted{3}".format(
            name, " ".join(sorted(written)), minted, skipped))

    print("\n{0:.1f} MB into {1}".format(total / 1048576.0, TEXTURES))


if __name__ == "__main__":
    main()
