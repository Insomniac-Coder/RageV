# moon.png

The full moon, for the night sky's moon disc (`Environment.MoonTexture`).

Rendered by NASA's Scientific Visualization Studio from **Lunar
Reconnaissance Orbiter** elevation and albedo data -- so the maria and the
crater rays are the real near side, not an approximation. Frame 0311 of the
2025 hourly *Moon Phase and Libration* set: 13 January 2025, ~22:00 UTC,
essentially full phase.

Source:
<https://svs.gsfc.nasa.gov/5415/>

Frame:
<https://svs.gsfc.nasa.gov/vis/a000000/a005400/a005415/frames/730x730_1x1_30p/moon.0311.jpg>

Licence: **Public domain.** NASA content is not subject to copyright; the
SVS asks only that the work be credited, which is what this file is for.

> Credit: NASA's Scientific Visualization Studio

Reprocessed by `tools/scripts/make_moon_texture.py`, which crops the disc to
the texture edge and adds the alpha channel the sky shader masks with. The
pixels are otherwise NASA's.

This file is not named `.meta` because that extension belongs to the asset
registry, which rewrites what it owns.
