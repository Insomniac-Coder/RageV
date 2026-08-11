"""Measure the editor themes against WCAG 2.2, and print the table.

Why a script and not judgement: a dark theme and a light theme are two
different products, and the light one is the one that silently goes wrong --
a grey that reads as "secondary" on charcoal becomes invisible on white. The
thresholds are not opinions:

    4.5:1   body text against its background          (WCAG 2.2 AA, 1.4.3)
    3.0:1   large text, and the boundary or state of
            a UI component against what is adjacent   (WCAG 2.2 AA, 1.4.11)

The same numbers apply to light-on-dark and dark-on-light; there is no
dark-mode discount.

This is the source of the numbers in EditorTheme.h. Change a colour there,
run this, and the table tells you whether it is still legal. scenetest checks
the same pairs at runtime so the answer cannot drift from the header.
"""

import sys

# --------------------------------------------------------------------------
# The palettes. Hex, because that is what a designer reads and what every
# picker speaks; EditorTheme.h carries the same values as floats.
# --------------------------------------------------------------------------

DARK = {
    # Surfaces. Note the base is NOT near-black: reading speed drops
    # measurably on pure-black themes, and a saturated accent fringes against
    # #000 badly enough to look out of focus.
    #
    # BgControl is the surface of an input. In dark it sits ABOVE the panel;
    # in light it sits BELOW it. That is not an inconsistency -- it is what
    # "a control you can type into" looks like in each, and it is why the
    # token is named for its role and not for being lighter.
    "BgBase":        "#111116",
    "BgSurface":     "#17171E",
    "BgControl":     "#22222B",
    "BgHover":       "#2C2C36",
    "BgActive":      "#363642",
    "Line":          "#30303B",
    "LineStrong":    "#4E4E5C",
    "TextPrimary":   "#E8E8ED",
    "TextSecondary": "#A2A2B0",
    "TextDisabled":  "#70707E",
    # Desaturated relative to a pure red, which is what stops it vibrating
    # against the dark surface.
    "Accent":        "#E5484D",
    "AccentHover":   "#F26669",
    "AccentPressed": "#C13438",
    "OnAccent":      "#12070A",
    "Success":       "#48BB78",
    "Warning":       "#E8A33D",
    "Danger":        "#F2555A",
    "AxisX":         "#E5686D",
    "AxisY":         "#5FBF6A",
    "AxisZ":         "#5B8DEF",
}

LIGHT = {
    # Not an inversion of the dark values -- an inversion is the single most
    # common dark/light mistake in both directions. The light theme wants a
    # softer base than white so that panels can be white and still read as
    # raised.
    "BgBase":        "#E7E7EE",
    "BgSurface":     "#FBFBFD",
    "BgControl":     "#ECECF3",
    "BgHover":       "#E1E1EB",
    "BgActive":      "#D4D4E0",
    "Line":          "#DBDBE4",
    "LineStrong":    "#A8A8B8",
    "TextPrimary":   "#16161C",
    "TextSecondary": "#55555F",
    "TextDisabled":  "#8B8B98",
    # Darker and more saturated than the dark theme's: the same red on white
    # reads pink and washed out, and white text on it has to clear 4.5:1.
    "Accent":        "#C4262C",
    "AccentHover":   "#A81E24",
    "AccentPressed": "#8C1319",
    "OnAccent":      "#FFFFFF",
    "Success":       "#1F7A45",
    "Warning":       "#8A5A00",
    "Danger":        "#B3161C",
    "AxisX":         "#B3262C",
    "AxisY":         "#1F7A3A",
    "AxisZ":         "#2B5BC4",
}


def rgb(value):
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def luminance(color):
    def channel(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (channel(c) for c in rgb(color))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    lo, hi = min(la, lb), max(la, lb)
    return (hi + 0.05) / (lo + 0.05)


# --------------------------------------------------------------------------
# Every pair the editor actually puts next to each other. A palette is only
# accessible in the combinations it is used in, so the list is the contract.
# (foreground, background, minimum, what it is)
# --------------------------------------------------------------------------

PAIRS = [
    ("TextPrimary",   "BgBase",    4.5, "body text on the dockspace"),
    ("TextPrimary",   "BgSurface", 4.5, "body text on a panel"),
    ("TextPrimary",   "BgControl",  4.5, "text inside an input"),
    ("TextPrimary",   "BgHover",   4.5, "text on a hovered row"),
    ("TextPrimary",   "BgActive",  4.5, "text on a pressed control"),
    ("TextSecondary", "BgSurface", 4.5, "secondary text on a panel"),
    ("TextSecondary", "BgControl",  4.5, "secondary text in an input"),
    ("TextDisabled",  "BgSurface", 3.0, "disabled text (large-text threshold)"),

    ("Accent",        "BgSurface", 3.0, "accent fill against a panel"),
    ("Accent",        "BgBase",    3.0, "accent fill against the dockspace"),
    ("AccentHover",   "BgSurface", 3.0, "hovered accent against a panel"),
    ("OnAccent",      "Accent",    4.5, "label on an accent button"),

    ("Line",          "BgSurface", 1.2, "separator against a panel (visible, not loud)"),
    ("LineStrong",    "BgSurface", 2.0, "an emphasised border"),
    ("BgControl",      "BgSurface", 1.1, "an input is distinguishable from its panel"),
    ("BgHover",       "BgControl",  1.1, "hover is visible against rest"),

    ("Success",       "BgSurface", 3.0, "success text or icon"),
    ("Warning",       "BgSurface", 3.0, "warning text or icon"),
    ("Danger",        "BgSurface", 3.0, "error text"),

    ("AxisX",         "BgControl",  3.0, "the X axis badge"),
    ("AxisY",         "BgControl",  3.0, "the Y axis badge"),
    ("AxisZ",         "BgControl",  3.0, "the Z axis badge"),
]


def report(name, palette):
    print(f"\n=== {name} " + "=" * (58 - len(name)))
    print(f"{'pair':<52}{'ratio':>8}  {'min':>5}")
    print("-" * 68)
    failures = []
    for fg, bg, minimum, what in PAIRS:
        ratio = contrast(palette[fg], palette[bg])
        ok = ratio >= minimum
        if not ok:
            failures.append((fg, bg, ratio, minimum, what))
        print(f"{fg + ' on ' + bg:<34}{what[:17]:<18}{ratio:>7.2f}  {minimum:>5.1f}"
              f"  {'' if ok else '  <-- FAILS'}")
    return failures


def main():
    failures = report("dark", DARK) + report("light", LIGHT)
    print()
    if failures:
        print(f"{len(failures)} pair(s) below the threshold:")
        for fg, bg, ratio, minimum, what in failures:
            print(f"  {fg} on {bg} ({what}): {ratio:.2f}, needs {minimum:.1f}")
        return 1
    print("every pair meets its threshold, in both themes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
