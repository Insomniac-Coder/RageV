#!/usr/bin/env python3
"""Regenerate the sample project's sky.hdr.

The original was generated and the generator was not kept, which meant the one
file in the repository nobody could reproduce was the one the whole lighting
path is tuned against. This replaces it: a 1024x512 equirectangular Radiance
image with a sun at 40 degrees azimuth and 17 degrees elevation.

**A close match, not a reproduction.** Rendering the sample scene under each
and measuring: whole-frame mean luminance 146.0 against the original's 149.1,
upper sky 203 against 208, and a horizon that falls off at 5.9 luminance per
row against 4.2. Nobody has the original generator, so the numbers above are
the standard -- if this is ever regenerated into the sample project, the scene
will look very slightly different and the shadow and probe tuning should be
re-checked rather than assumed.

    python tools/scripts/make_sky_hdr.py --output SampleProject/assets/textures/sky.hdr

It is deliberately HDR and deliberately has a sun far brighter than 1.0. A test
scene that never exceeds the display range hides renderer bugs -- the mirrored
bloom that survived a roadmap phase needed a bright sky to become visible at
all, and a sky clamped to white would have hidden it again.

Radiance RGBE, written uncompressed (one scanline of raw RGBE per row). Every
reader handles it and stb_image, which is what the engine loads with, is
happiest with it.
"""

import argparse
import math
import struct


def to_rgbe(r, g, b):
    """One pixel, as Radiance's shared-exponent byte quadruple."""
    brightest = max(r, g, b)

    # Below what the format can represent. Zero is the exact answer, and it is
    # also the only value whose exponent byte is allowed to be zero.
    if brightest < 1e-32:
        return (0, 0, 0, 0)

    mantissa, exponent = math.frexp(brightest)
    scale = mantissa * 256.0 / brightest

    return (
        min(255, int(r * scale)),
        min(255, int(g * scale)),
        min(255, int(b * scale)),
        min(255, exponent + 128),
    )


def direction_for(u, v):
    """Equirectangular pixel to a unit direction.

    Matches Cubemap.cpp's convention, which is the one that matters: longitude
    is atan2(x, -z) so the centre of the panorama is straight ahead, and the
    top row is +Y.
    """
    longitude = (u * 2.0 - 1.0) * math.pi
    latitude = (0.5 - v) * math.pi

    cos_lat = math.cos(latitude)
    return (
        cos_lat * math.sin(longitude),
        math.sin(latitude),
        -cos_lat * math.cos(longitude),
    )


def build(width, height, sun_azimuth, sun_elevation, sun_intensity, sun_size,
          ground_blend):
    azimuth = math.radians(sun_azimuth)
    elevation = math.radians(sun_elevation)
    sun = (
        math.cos(elevation) * math.sin(azimuth),
        math.sin(elevation),
        -math.cos(elevation) * math.cos(azimuth),
    )

    # Linear, and none of these are display colours -- the zenith is dimmer
    # than the horizon because that is what a clear sky does, and the ground
    # is a dull bounce rather than a colour anyone picked.
    horizon = (0.52, 0.60, 0.76)
    zenith = (0.13, 0.26, 0.58)
    ground = (0.045, 0.038, 0.031)

    # The angular radius of the disc, plus a much wider soft falloff around it.
    # A hard disc alone gives a sky that prefilters into a ring rather than a
    # glow, which reads as a rendering error rather than as a sun.
    disc = math.radians(sun_size)
    halo = math.radians(sun_size * 9.0)

    rows = []
    for y in range(height):
        v = (y + 0.5) / height
        row = bytearray()

        for x in range(width):
            u = (x + 0.5) / width
            dx, dy, dz = direction_for(u, v)

            # The sky first, ignoring the ground entirely.
            if dy >= 0.0:
                t = pow(min(dy, 1.0), 0.45)
                sky = tuple(horizon[i] + (zenith[i] - horizon[i]) * t for i in range(3))
            else:
                sky = horizon

            # Then the ground mixed over it, across a band *centred* on the
            # horizon rather than starting at it.
            #
            # Starting at the horizon forces a choice between the two things
            # that both have to be true: a transition soft enough not to draw a
            # line, and a lower half dark enough not to wash the scene out. A
            # band that straddles the horizon gets both -- the darkening begins
            # just above it, so by a few degrees below it is already ground,
            # and nowhere is there an edge.
            #
            # Smoothstep, because its derivative is zero at both ends. A curve
            # that arrives with slope left over draws a visible crease, which is
            # the artefact that reads instantly as a generated sky.
            edge = min(1.0, max(0.0, (ground_blend * 0.5 - dy) / ground_blend))
            t = edge * edge * (3.0 - 2.0 * edge)
            base = tuple(sky[i] + (ground[i] - sky[i]) * t for i in range(3))

            # Angle to the sun. Clamped before acos: floating point can put a
            # dot product a hair outside [-1, 1] and acos then raises.
            dot = max(-1.0, min(1.0, dx * sun[0] + dy * sun[1] + dz * sun[2]))
            angle = math.acos(dot)

            r, g, b = base
            if angle < disc:
                r += sun_intensity
                g += sun_intensity * 0.97
                b += sun_intensity * 0.88
            elif angle < halo:
                # Smooth, and steep. The exponent is what stops the glow
                # looking like a painted gradient.
                falloff = 1.0 - (angle - disc) / (halo - disc)
                glow = sun_intensity * 0.02 * pow(falloff, 4.0)
                r += glow
                g += glow * 0.95
                b += glow * 0.82

            row += bytes(to_rgbe(r, g, b))

        rows.append(bytes(row))

    return rows


def write_hdr(path, width, height, rows):
    with open(path, "wb") as handle:
        handle.write(b"#?RADIANCE\n")
        handle.write(b"# Generated by tools/scripts/make_sky_hdr.py\n")
        handle.write(b"FORMAT=32-bit_rle_rgbe\n\n")
        # -Y first means the first scanline written is the top one, which is
        # what direction_for assumes.
        handle.write(f"-Y {height} +X {width}\n".encode("ascii"))
        for row in rows:
            handle.write(row)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--width", type=int, default=1024)
    parser.add_argument("--height", type=int, default=512)
    parser.add_argument("--sun-azimuth", type=float, default=40.0)
    parser.add_argument("--sun-elevation", type=float, default=17.0)
    parser.add_argument("--sun-intensity", type=float, default=110.0,
                        help="linear radiance of the disc; far above 1 on purpose")
    parser.add_argument("--sun-size", type=float, default=1.6,
                        help="angular radius of the disc, in degrees")
    parser.add_argument("--ground-blend", type=float, default=0.13,
                        help="width of the horizon band, in sin(angle), centred on the "
                             "horizon; larger is softer")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    rows = build(args.width, args.height, args.sun_azimuth, args.sun_elevation,
                 args.sun_intensity, args.sun_size, args.ground_blend)
    write_hdr(args.output, args.width, args.height, rows)

    print(f"{args.output}: {args.width}x{args.height}, sun at "
          f"{args.sun_azimuth} deg azimuth / {args.sun_elevation} deg elevation")


if __name__ == "__main__":
    main()
