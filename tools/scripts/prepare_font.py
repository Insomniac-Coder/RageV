"""Make a font safe for distance-field text.

A distance field measures distance to a glyph's *outline*. That works only if
the outline is the boundary of the shape -- and in a great many modern fonts it
is not. Variable fonts in particular build a letter from **overlapping, often
self-intersecting contours**, because overlap removal cannot be done once for a
family whose shapes change with an axis. Rasterisers do not care: non-zero
winding fills a self-crossing path correctly. A distance field does, because
the crossing leaves real outline geometry *inside* the glyph, and the field
faithfully reports a nearby edge there.

What that looks like is a notch. RobotoFlex's 'e' is a single contour where
almost every other font uses two, and it bakes with a chunk bitten out of the
junction between the crossbar and the bowl. '&' and '@' get slashes. It is
invisible at small sizes and unmissable on a title.

So: resolve the geometry first. This instantiates a variable font at a chosen
location, unions the overlapping contours with skia-pathops, and writes a
static font that rvfont can bake.

    pip install fonttools skia-pathops
    python tools/scripts/prepare_font.py <in.ttf> <out.ttf> [--axis wght=400 ...]

Kerning is preserved: this rewrites outlines, not the layout tables.
"""

import argparse
import sys
from pathlib import Path

try:
    from fontTools.ttLib import TTFont
    from fontTools.ttLib.removeOverlaps import removeOverlaps
except ImportError:
    sys.exit("fonttools is required: pip install fonttools skia-pathops")


def count_contours(font, char):
    """Contours in one glyph, as the outline actually stores them.

    The number that matters: a letter with a counter -- e, a, o, B, 8 -- needs
    at least two. One means the counter is formed by the path crossing itself,
    which is exactly what a distance field cannot represent.
    """
    cmap = font.getBestCmap()
    name = cmap.get(ord(char))
    if not name or "glyf" not in font:
        return None

    glyph = font["glyf"][name]
    if glyph.numberOfContours < 0:
        return None   # a composite; its parts have their own contours
    return glyph.numberOfContours


# Letters whose shape requires an enclosed counter. If any of these comes back
# with a single contour, the outline crosses itself.
PROBES = "eaoBb8@&"


def report(font, label):
    counts = {c: count_contours(font, c) for c in PROBES}
    print(f"  {label}:")
    suspicious = []
    for char, n in counts.items():
        if n is None:
            continue
        flag = ""
        if char in "eaoBb8" and n < 2:
            flag = "  <-- self-intersecting"
            suspicious.append(char)
        print(f"    '{char}'  {n} contour(s){flag}")
    return suspicious


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--axis", action="append", default=[],
                        help="axis=value for a variable font, e.g. wght=400. "
                             "Repeatable. Ignored for a static font.")
    args = parser.parse_args()

    font = TTFont(args.source)

    print(f"{args.source.name}")
    before = report(font, "before")

    # --- a variable font has to be pinned to one instance first ------------
    if "fvar" in font:
        from fontTools.varLib import instancer

        location = {}
        for pair in args.axis:
            if "=" not in pair:
                sys.exit(f"--axis wants axis=value, got '{pair}'")
            name, value = pair.split("=", 1)
            location[name.strip()] = float(value)

        # Anything not named is pinned to the axis default, which is what
        # "Regular" means for that axis.
        for axis in font["fvar"].axes:
            location.setdefault(axis.axisTag, axis.defaultValue)

        print(f"  variable font: pinning {location}")
        font = instancer.instantiateVariableFont(font, location, inplace=False)

    # --- the actual work ---------------------------------------------------
    #
    # A boolean union of each glyph's contours. Overlaps disappear, and a
    # self-crossing path becomes an outer contour plus the counters it implied.
    print("  removing overlaps...")
    removeOverlaps(font)

    after = report(font, "after")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    font.save(args.output)
    print(f"  wrote {args.output}")

    if after:
        print(f"\nFAIL: {''.join(after)} still self-intersect after preprocessing")
        return 1

    if before:
        print(f"\nfixed {''.join(before)} -- safe to bake with rvfont now")
    else:
        print("\nnothing needed fixing; the outlines were already clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
