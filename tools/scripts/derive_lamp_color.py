#!/usr/bin/env python3
"""The sodium lamps' colour, derived rather than quoted.

WR-4 (docs/RENDERING-REVAMP.md). The research strands agreed on the bridge
lamps' *chromaticity* -- x 0.51-0.52, y 0.42, high-pressure sodium seen
through the amber acrylic lenses fitted in 1972 to preserve the original
1937 low-pressure-sodium colour -- and then produced two conflicting RGB
translations of it. Neither is worth arguing about: the conversion is four
lines of arithmetic and the argument is only about which of them somebody
skipped.

**What the engine's colour numbers actually are.** `tonemap.rvshader` applies
the Narkowicz ACES curve per channel, encodes with `pow(x, 1/2.2)`, and takes
luminance with the weights 0.2126 / 0.7152 / 0.0722. Those are Rec.709's, so
the renderer's working space is **linear Rec.709 (sRGB primaries, D65), and
the values in a scene file are linear** -- not the hex a colour picker shows.
That distinction is the whole reason this script exists: the value the scene
carried before WR-4, (1.0, 0.60, 0.16), is almost exactly `#FF8B14`
*display-encoded* typed into a linear field, which is a lamp roughly a gamma
too pale.

**Why normalise on the peak channel rather than on luminance.** Dividing by
luminance would make `Intensity` read directly in candela, which is tempting
-- and it puts the red channel at 2.4, outside the [0, 1] a colour picker
will hold. One pass through the inspector would silently clamp it and
desaturate every lamp on the bridge. So the colour keeps its peak at 1 and
the luminance it gives up is reported here, for whoever is setting the
intensity to divide back out.

Run:
    python tools/scripts/derive_lamp_color.py

Nothing is written. The numbers it prints are pasted into
`make_bridge_models.py`, where the scene generator reads them.
"""

# Linear Rec.709 / sRGB from CIE XYZ, D65. The standard matrix; quoted to the
# digits IEC 61966-2-1 gives so the result is reproducible by anyone checking.
XYZ_TO_LINEAR_RGB = (
    ( 3.2404542, -1.5371385, -0.4985314),
    (-0.9692660,  1.8760108,  0.0415560),
    ( 0.0556434, -0.2040259,  1.0572252),
)

# Rec.709 luminance, the same weights tonemap.rvshader uses.
LUMA = (0.2126, 0.7152, 0.0722)


def linear_rgb(x, y):
    """A chromaticity to linear Rec.709, peak-normalised.

    Returns (rgb, luminance): the colour with its largest channel at 1, and
    the relative luminance that colour carries. A saturated orange keeps far
    less luminance than white does, and a fixture quoted in candela has to be
    divided by this to emit the right amount of light.
    """
    # Y = 1: the chromaticity says the hue, the intensity says the rest.
    big_x = x / y
    big_y = 1.0
    big_z = (1.0 - x - y) / y

    rgb = [sum(row[i] * v for i, v in enumerate((big_x, big_y, big_z)))
           for row in XYZ_TO_LINEAR_RGB]

    # **Clipped, and the clip is the point for a sodium lamp.** A source this
    # saturated sits outside the sRGB triangle, so the blue term comes out
    # negative -- a colour the display cannot make. Zero is the closest it
    # can, and for a 589 nm line source that is also the physically honest
    # answer: there is no blue in it.
    rgb = [max(v, 0.0) for v in rgb]

    peak = max(rgb)
    rgb = [v / peak for v in rgb]
    return rgb, sum(w * v for w, v in zip(LUMA, rgb))


SOURCES = (
    # The deck lamps. Top of the agreed range: the amber lens exists to hold
    # the 1937 low-pressure colour, so of the two ends the saturated one is
    # the one with a reason behind it.
    ("Deck lamp -- 250 W HPS through amber acrylic", 0.520, 0.418),
    # For comparison, and to check this script against something already
    # known. Bare high-pressure sodium, ~2100 K.
    ("Bare HPS, for comparison", 0.519, 0.418),
    # The tower-base post-tops: 35 W low-pressure sodium, a single 589 nm
    # line. **This is the script's own test.** The plan quotes `#FF8000` for
    # it from an independent source; that hex is linear (1, 0.2158, 0), and
    # anything but agreement here means the conversion above is wrong.
    ("Post-top -- 35 W LPS, 589 nm", 0.5693, 0.4300),
    # The tower floods are described as neutral-white HPS class. D65 white
    # is the honest stand-in until a lamp type is chosen for them.
    ("Tower flood -- neutral white (D65)", 0.3127, 0.3290),
    # The midspan navigation lantern. IALA green: a specified *region* of the
    # chromaticity diagram rather than a point, so this is its centre. The
    # white of the same column is D65 above.
    ("Navigation green -- IALA region centre", 0.2000, 0.6000),
)


# --- the deck lamps' intensity, solved against the engine's own falloff -------
#
# **Derived from the measured road illuminance, not from the lamp's lumens.**
# A 250 W HPS is 28 000 lm and that number is useless on its own: what reaches
# the road depends on the luminaire's distribution, and a real cutoff shoebox
# throws most of its light *along* the road at 60-70 degrees off nadir, while
# the engine's spot is flat inside its inner cone and falls linearly to the
# outer one. Pushing the lamp's flux through that cone puts 74 lux under each
# post -- five times what the DOE survey measured -- because the model has no
# way to be a shoebox.
#
# So the derivation runs the other way, from the quantity the survey actually
# reports: **13-15 lux average on the drivelane.** Sum the engine's exact
# attenuation over a grid of the carriageway, from every lamp near enough to
# matter, and scale until the average lands on the target. What comes out is
# right *for this engine's cone*, which is the only thing that can be right.

LAMP_HEIGHT = 7.09          # make_bridge_models.LAMP_HEIGHT
LAMP_SPACING = 45.72        # make_bridge_models.LAMP_SPACING
LAMP_ARM = 1.52
RAIL_HALF_WIDTH = 12.62
CARRIAGEWAY_HALF = 9.45     # 18.90 m curb to curb
LAMP_RANGE = 600.0          # what the scene authors, for the glitter path
LAMP_INNER = 34.0
LAMP_OUTER = 85.0
TARGET_LUX = 14.0           # mid of the survey's 13-15


def engine_attenuation(dx, dy, dz, range_m, inner_deg, outer_deg):
    """`pbr_fragment.glsl`'s spot falloff, to the line.

    Windowed inverse square times a cone linear in cos(theta), with the lamp
    aimed straight down -- which is how the scene rotates them. Duplicated
    here rather than approximated, because a derivation against a different
    falloff from the renderer's is a derivation of nothing.
    """
    import math
    distance2 = dx * dx + dy * dy + dz * dz
    if distance2 < 1e-8:
        return 0.0
    t = distance2 / (range_m * range_m)
    ratio = max(0.0, min(1.0, 1.0 - t * t))
    attenuation = (ratio * ratio) / distance2

    # theta between the light's axis (straight down) and the direction to the
    # surface point. dy is the drop from lamp to road, so cos(theta) = dy/d.
    cos_theta = dy / math.sqrt(distance2)
    cos_inner = math.cos(math.radians(inner_deg))
    cos_outer = math.cos(math.radians(outer_deg))
    if cos_outer < cos_inner:
        cone = (cos_theta - cos_outer) / max(cos_inner - cos_outer, 1e-4)
        attenuation *= max(0.0, min(1.0, cone))
    return attenuation


def deck_lamp_candela(target_lux=TARGET_LUX):
    """The nadir intensity that puts `target_lux` on the drivelane, averaged.

    Horizontal illuminance, so each contribution carries the cosine the road
    presents to it -- which the attenuation above already has, since the lamp
    points straight down and the road is level: the same dy/d.
    """
    import math
    # One repeating cell of the road: half a lamp spacing either side of a
    # station, the full carriageway across. Lamps stand at both curbs, and
    # every station within 4 spacings contributes something.
    samples, total = 0, 0.0
    steps_z, steps_x = 24, 16
    for iz in range(steps_z):
        z = (iz + 0.5) / steps_z * LAMP_SPACING - LAMP_SPACING * 0.5
        for ix in range(steps_x):
            x = (ix + 0.5) / steps_x * (2 * CARRIAGEWAY_HALF) - CARRIAGEWAY_HALF
            lit = 0.0
            for station in range(-4, 5):
                lamp_z = station * LAMP_SPACING
                for side in (-1.0, 1.0):
                    lamp_x = side * (RAIL_HALF_WIDTH - 0.55 - LAMP_ARM)
                    a = engine_attenuation(x - lamp_x, LAMP_HEIGHT, z - lamp_z,
                                           LAMP_RANGE, LAMP_INNER, LAMP_OUTER)
                    # The cosine the level road presents. attenuation already
                    # divides by d^2; the horizontal projection is one more
                    # cos(theta) on top of it.
                    d = math.sqrt((x - lamp_x) ** 2 + LAMP_HEIGHT ** 2
                                  + (z - lamp_z) ** 2)
                    lit += a * (LAMP_HEIGHT / d)
            total += lit
            samples += 1
    per_candela = total / samples
    return target_lux / per_candela, per_candela


def main():
    for name, x, y in SOURCES:
        rgb, luminance = linear_rgb(x, y)
        print("{0}\n  xy ({1:.4f}, {2:.4f})"
              "\n  linear Rec.709  [{3:.4f}, {4:.4f}, {5:.4f}]"
              "\n  relative luminance {6:.4f}"
              "  (candela / {6:.4f} = the Intensity to author)\n"
              .format(name, x, y, rgb[0], rgb[1], rgb[2], luminance))

    candela, per_candela = deck_lamp_candela()
    _, lamp_luminance = linear_rgb(*SOURCES[0][1:])
    print("Deck lamp intensity, solved against the engine's spot falloff")
    print("  drivelane average, per candela  {0:.6f} lux".format(per_candela))
    print("  for {0:g} lux average           {1:.0f} cd"
          .format(TARGET_LUX, candela))
    print("  authored Intensity (cd / {0:.4f})  {1:.0f}"
          .format(lamp_luminance, candela / lamp_luminance))
    print("  illuminance directly under a post {0:.1f} lux"
          .format(candela * engine_attenuation(LAMP_ARM, LAMP_HEIGHT, 0.0,
                                               LAMP_RANGE, LAMP_INNER, LAMP_OUTER)
                  * (LAMP_HEIGHT / (LAMP_ARM ** 2 + LAMP_HEIGHT ** 2) ** 0.5)))
    print("  road luminance at that point      {0:.2f} cd/m^2 (asphalt rho 0.10)"
          .format(candela * engine_attenuation(LAMP_ARM, LAMP_HEIGHT, 0.0,
                                               LAMP_RANGE, LAMP_INNER, LAMP_OUTER)
                  * (LAMP_HEIGHT / (LAMP_ARM ** 2 + LAMP_HEIGHT ** 2) ** 0.5)
                  * 0.10 / 3.14159265))


if __name__ == "__main__":
    main()
