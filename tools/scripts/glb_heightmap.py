"""Pull a heightmap out of a terrain .glb and write it as a 16-bit PNG.

The kind of .glb this is for is a DEM someone triangulated for export -- the
vertices sit on a perfect square lattice, so the mesh is a heightmap wearing a
mesh costume and the triangles carry no information the grid does not. Turning
it back into an image lets TerrainComponent take it directly (the engine cooks
a heightmap image to terrain samples on load), which matters because a terrain
built that way sheds detail with distance through the chunk LODs the terrain
system already has, while a 2-million-triangle static mesh would not.

It prints the three numbers the component needs afterwards -- Size, Height and
the Y to place the entity at so a chosen datum (sea level, usually) lands on
zero -- because getting those wrong is the difference between real terrain and
a mountain range with the sea running through it at the wrong altitude.

    python tools/scripts/glb_heightmap.py <in.glb> <out.png> [--sea auto|<metres>]
"""

import argparse
import json
import struct
import sys

import numpy as np
from PIL import Image


def read_glb(path):
    """The JSON chunk and the binary chunk of a .glb."""
    with open(path, "rb") as f:
        magic, version, _length = struct.unpack("<III", f.read(12))
        if magic != 0x46546C67:
            raise SystemExit(f"{path} is not a .glb (bad magic)")
        if version != 2:
            raise SystemExit(f"{path} is glTF {version}; this reads version 2")

        js = None
        binary = None
        while True:
            header = f.read(8)
            if len(header) < 8:
                break
            length, kind = struct.unpack("<II", header)
            payload = f.read(length)
            if kind == 0x4E4F534A:      # 'JSON'
                js = json.loads(payload.decode("utf-8"))
            elif kind == 0x004E4942:    # 'BIN'
                binary = payload
    if js is None:
        raise SystemExit("no JSON chunk")
    return js, binary


def accessor_floats(js, binary, index):
    """A float32 accessor as an (n, components) array."""
    acc = js["accessors"][index]
    if acc["componentType"] != 5126:
        raise SystemExit("expected float32 positions")
    comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[acc["type"]]
    view = js["bufferViews"][acc["bufferView"]]
    offset = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    count = acc["count"]

    stride = view.get("byteStride")
    if stride and stride != comps * 4:
        # Interleaved: walk it rather than reshaping a contiguous run.
        out = np.empty((count, comps), dtype=np.float32)
        for i in range(count):
            start = offset + i * stride
            out[i] = np.frombuffer(binary, np.float32, comps, start)
        return out
    return np.frombuffer(binary, np.float32, count * comps, offset).reshape(count, comps)


def node_scale(js, mesh_index):
    """The uniform scale a parent applies to this mesh.

    Exporters routinely normalise geometry to a unit-ish box and put the real
    size on an ancestor, so reading positions alone gives a terrain 100 metres
    across that should be four kilometres.
    """
    parent_of = {}
    for i, node in enumerate(js.get("nodes", [])):
        for child in node.get("children", []):
            parent_of[child] = i

    holder = next((i for i, n in enumerate(js.get("nodes", []))
                   if n.get("mesh") == mesh_index), None)
    scale = 1.0
    seen = set()
    while holder is not None and holder not in seen:
        seen.add(holder)
        node = js["nodes"][holder]
        if "matrix" in node:
            m = node["matrix"]           # column-major
            axis = [(m[0] ** 2 + m[1] ** 2 + m[2] ** 2) ** 0.5,
                    (m[4] ** 2 + m[5] ** 2 + m[6] ** 2) ** 0.5,
                    (m[8] ** 2 + m[9] ** 2 + m[10] ** 2) ** 0.5]
            if max(axis) - min(axis) > 1e-4 * max(axis):
                print(f"  note: node {holder} scales unevenly {axis}; using x")
            scale *= axis[0]
        if "scale" in node:
            scale *= node["scale"][0]
        holder = parent_of.get(holder)
    return scale


def pick_terrain_mesh(js, binary):
    """The mesh whose vertices lie on a square lattice, largest first.

    A terrain export usually carries buildings, vegetation and street furniture
    alongside the ground. Only the ground is a grid, so the grid test picks it
    out without relying on a node being named a particular thing.
    """
    best = None
    for mi, mesh in enumerate(js["meshes"]):
        for prim in mesh["primitives"]:
            pos = accessor_floats(js, binary, prim["attributes"]["POSITION"])
            n = len(pos)
            side = int(round(n ** 0.5))
            if side * side != n or side < 33:
                continue
            # Two of the three axes must each take exactly `side` distinct
            # values; the third is the height.
            distinct = [len(np.unique(np.round(pos[:, a], 3))) for a in range(3)]
            flat = [a for a in range(3) if distinct[a] == side]
            if len(flat) != 2:
                continue
            up = ({0, 1, 2} - set(flat)).pop()
            name = next((nd.get("name", "?") for nd in js["nodes"]
                         if nd.get("mesh") == mi), "?")
            if best is None or n > best[0].shape[0]:
                best = (pos, side, flat, up, mi, name)
    if best is None:
        raise SystemExit("no mesh in this file is a regular grid; it is not a heightmap")
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("glb")
    ap.add_argument("png")
    ap.add_argument("--sea", default="auto",
                    help="datum to report an offset for: 'auto' finds the flattest "
                         "level (a water surface), or give metres, or 'none'")
    args = ap.parse_args()

    js, binary = read_glb(args.glb)
    pos, side, flat, up, mesh_index, name = pick_terrain_mesh(js, binary)
    scale = node_scale(js, mesh_index)

    print(f"terrain mesh: '{name}' -- {side} x {side} grid, height on axis {'xyz'[up]}")
    print(f"parent scale: {scale:g}")

    a, b = flat
    order = np.lexsort((pos[:, a], pos[:, b]))
    heights = (pos[order, up] * scale).reshape(side, side)

    span_a = (pos[:, a].max() - pos[:, a].min()) * scale
    span_b = (pos[:, b].max() - pos[:, b].min()) * scale
    lo, hi = float(heights.min()), float(heights.max())

    if args.sea == "auto":
        # The flattest level: a water surface is thousands of samples at one
        # height, which no landform matches.
        counts, edges = np.histogram(heights, bins=600, range=(lo, hi))
        sea = float(edges[int(np.argmax(counts))] + (edges[1] - edges[0]) * 0.5)
        share = counts.max() / heights.size
        print(f"datum: {sea:.2f} m carries {share * 100:.1f}% of the samples "
              f"-- {'a water surface' if share > 0.05 else 'NOT obviously water, check it'}")
    elif args.sea == "none":
        sea = 0.0
    else:
        sea = float(args.sea)

    # 0 maps to the lowest sample and 65535 to the highest, so the whole range
    # of the source survives the 16 bits rather than being clipped to a datum.
    scaled = (heights - lo) / max(hi - lo, 1e-6)
    # No `mode=`: Pillow infers I;16 from a uint16 array, and passing it
    # explicitly is deprecated from Pillow 13.
    Image.fromarray((scaled * 65535.0 + 0.5).astype(np.uint16)).save(args.png)

    print()
    print(f"wrote {args.png}  ({side} x {side}, 16-bit)")
    print(f"  ground covers   {span_a:.1f} x {span_b:.1f} m")
    print(f"  relief          {lo:.1f} .. {hi:.1f} m  (span {hi - lo:.1f})")
    print()
    print("TerrainComponent:")
    print(f"  Size    {max(span_a, span_b):.1f}")
    print(f"  Height  {hi - lo:.1f}")
    if args.sea != "none":
        print(f"  entity Y  {-(sea - lo):.2f}   "
              f"(puts the {sea:.2f} m datum at world zero)")


if __name__ == "__main__":
    sys.exit(main())
