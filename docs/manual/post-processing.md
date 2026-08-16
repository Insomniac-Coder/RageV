# Post processing

Everything on a `.rvpostprofile`: **thirty-six settings across nine effects**,
each listed here with its default, its range and what it costs.

A profile is attached to a **camera**, on the Post profile row of its Camera
component, and it is **optional** — a camera with no profile renders with
exactly the defaults below. There is no "is this set?" bit beside each value,
because there is nothing underneath a profile to inherit from: every field it
holds is a value it means.

> [!NOTE]
> **Six of the nine effects are off by default, and off is exact.** No pass is
> added and the shader branches past it rather than computing a no-op, so a
> profile that has never touched an effect renders the same bytes as a build
> without it. That is what keeps every screenshot check in the repository valid.

For where profiles live, how they are shared and when they save, see
[Core concepts](concepts.md#the-post-profile). For anti-aliasing and shadows,
which are **not** here because they belong to the project, see
[Rendering](rendering.md).

## Exposure

| Setting | Default | Range | What it does |
|---|---|---|---|
| `Exposure` | `1.0` | > 0 | Applied **before** the tone curve |

That placement is what makes it an exposure control rather than a brightness
one: it slides the scene along the response curve instead of scaling the result
of it.

> [!NOTE]
> With auto exposure on, this stops being the exposure and becomes exposure
> **compensation** — it multiplies what the metering worked out, the same
> control a camera has.

## Bloom — on by default

| Setting | Default | Range | What it does |
|---|---|---|---|
| `BloomEnabled` | `true` | — | The one effect that is on out of the box |
| `BloomThreshold` | `1.0` | ≥ 0 | Brightness at which a pixel starts to bleed. Above 1, only genuinely over-bright things glow |
| `BloomKnee` | `0.5` | ≥ 0 | Width of the ramp around the threshold. **Zero is a hard cut**, which pops as something crosses it and reads as flickering |
| `BloomIntensity` | `0.06` | ≥ 0 | How much of the blurred result is added back |
| `BloomClamp` | `16.0` | > 0 | Ceiling on what a *single pixel* may contribute |

> [!TRAP]
> `BloomClamp` exists for one specific artefact: the sun reflected in curved
> metal — a few hundred nits across less than a texel of the mip being read —
> survives the whole chain as an isolated blob floating in the air near the
> surface that produced it. The clamp bounds the *contribution*, not the pixel:
> the scene keeps its real values, and only what bleeds out of them is limited.

## Colour grading

| Setting | Default | Range | What it does |
|---|---|---|---|
| `ColorLut` | none | a `.cube` or `.rvlut` asset | The lookup table applied after tone mapping |
| `ColorLutStrength` | `1.0` | 0 to 1 | How much of the graded result is used against the ungraded one |

Two kinds of thing go in the LUT row:

| | What it is | Editable |
|---|---|---|
| `.cube` | A baked table, exported by Resolve, Photoshop or any grading tool | No — it is data |
| `.rvlut` | A **recipe**: temperature, tint, lift, gamma, gain, contrast, saturation | Yes, and that is its purpose |

Grading is applied **after** the tone curve, on display-referred values, which
is what a LUT was authored against — one exported from Resolve does here what
it did there. The trade, stated rather than hidden: a LUT at that point cannot
recover highlight detail the tone curve has already compressed.

## Auto exposure — off by default

Metering with a **histogram** rather than an average, and moving the exposure
toward it the way your eyes adjust on stepping outside.

| Setting | Default | Range | What it does |
|---|---|---|---|
| `AutoExposure` | `false` | — | On dispatches two compute passes |
| `AutoExposureMinLog` | `-8.0` | stops | Bottom of the histogram's span |
| `AutoExposureMaxLog` | `4.0` | stops | Top of it |
| `AutoExposureLowPercent` | `0.5` | 0 to 1 | Fraction of the darkest pixels thrown away before averaging |
| `AutoExposureHighPercent` | `0.95` | 0 to 1 | Fraction kept from the bright end |
| `AutoExposureKey` | `0.18` | > 0 | What the metered average is exposed to. **0.18 is middle grey** |
| `AutoExposureMin` | `0.03` | > 0 | Floor on the result |
| `AutoExposureMax` | `32.0` | > 0 | Ceiling on it |
| `AutoExposureSpeed` | `3.0` | stops/second | How fast it moves |

**Discarding the tails is the whole reason for a histogram.** The top few
percent are the sun and the specular hits, and an average chases them every time
the camera turns past something bright — the picture breathes. The low and high
percentages are what stop that.

A pixel outside the log range lands in an end bin rather than being dropped —
except below the bottom, which is reserved and discarded, because a night scene
is mostly pixels with no light in them and letting them vote meters the
darkness.

`AutoExposureSpeed` is frame-rate independent: converted with
`1 - exp(-rate · dt)` rather than `rate · dt`, so ten steps of 10 ms land where
one step of 100 ms does. The min and max bound the result for a scene with
nothing in it to meter.

## Depth of field — off by default

A real lens rather than a blur slider: the circle of confusion comes from the
thin-lens equation, so the controls are the ones a photographer already has and
the relationship between them is not something to rediscover per scene.

| Setting | Default | Range | What it does |
|---|---|---|---|
| `DepthOfField` | `false` | — | On adds three passes |
| `FocusDistance` | `5.0` | metres | Where the plane of sharp focus is |
| `FocalLength` | `50.0` | millimetres | 50 is normal on the 35 mm sensor these are measured against; longer is both narrower and shallower |
| `Aperture` | `2.8` | f-number | f/1.4 throws a background away; f/16 keeps most of a scene sharp |
| `MaxBokehRadius` | `24.0` | pixels of output | Ceiling on the blur radius |

Runs after the anti-aliasing resolve and before bloom, so a bright out-of-focus
highlight glows as the disc it has become rather than as the point it was.

> [!TRAP]
> A long focal length at a wide aperture is a *very* shallow field — 85 mm at
> f/1.4 focused at 5 m keeps about ten centimetres sharp. That is what those
> numbers mean on a real camera too. **If nothing looks sharp, the focus
> distance is wrong before the effect is.**

`MaxBokehRadius` is not cosmetic: the gather's tap count is chosen against it.
Let the radius grow without bound and the disc thins into a ring of separate
dots, which reads as a broken effect rather than a shallow one.

## Screen-space reflections — off by default

Reflections traced through the depth buffer for anything already on screen,
swapped in for the reflection probe's answer where the trace is confident and
left to the probe where it is not.

| Setting | Default | Range | What it does |
|---|---|---|---|
| `ScreenSpaceReflections` | `false` | — | On adds four passes |
| `SsrMaxDistance` | `20.0` | metres | How far a ray may travel before giving up |
| `SsrThickness` | `0.5` | metres | How far behind the depth surface a step may land and still count as a hit |
| `SsrIntensity` | `1.0` | ≥ 0 | Scale on the traced reflection's share. 1 is the weight the material implies |

`SsrThickness` is the one to reach for when reflections look wrong: **thin
objects want it small**, and a large value lets rays hit walls through railings.

> [!TRAP]
> **Screen-space means screen-space.** Anything off screen, behind the camera or
> hidden behind something nearer has no depth to trace against and cannot be
> reflected — the probe answers there instead. The trace is also one frame late
> by design. Neither is a bug to report.

## Ambient occlusion — off by default

Occlusion from depth alone, applied as a multiply on the lit image.

| Setting | Default | Range | What it does |
|---|---|---|---|
| `AmbientOcclusion` | `false` | — | On adds four passes at half resolution |
| `AoRadius` | `0.5` | world metres | How far the hemisphere reaches |
| `AoIntensity` | `1.0` | ≥ 0 | An exponent on the occlusion |

Small radii give contact darkening in creases; large ones give soft room-scale
shading and cost cache misses.

Because `AoIntensity` is an exponent, an open surface — occlusion 1 — is
untouched at any setting, and only the dark end deepens.

> [!NOTE]
> Applying occlusion as a post multiply darkens **direct** light too, which is
> the stated compromise of every forward-plus-post AO. Treat it as contact
> shadowing and keep the intensity restrained.

## Motion blur — off by default

A reconstruction gather along the motion vectors the scene already writes for
TAA, so an object smears over what it passes rather than stopping at its own
silhouette.

| Setting | Default | Range | What it does |
|---|---|---|---|
| `MotionBlur` | `false` | — | On adds four passes |
| `MotionBlurShutter` | `0.5` | 0 to 1 | Fraction of the frame the virtual shutter is open |
| `MotionBlurMaxRadius` | `20.0` | pixels | Ceiling on the smear, **and** the tile size the dominant motion is tracked at |

0.5 is the 180-degree shutter every film camera defaults to. It scales the
per-frame velocity directly, so it stays honest at any frame rate.

> [!TRAP]
> `MotionBlurMaxRadius` is two things at once. A blur that can reach further
> than its tiles can see tears at tile boundaries, which is why one number
> controls both.

## Lens and film — all three off by default

Each runs at a different point in the chain because each models a different
physical thing.

| Setting | Default | Range | Runs | What it does |
|---|---|---|---|---|
| `VignetteIntensity` | `0.0` | 0 to 1 | Before the tone curve | How dark the corners go |
| `VignetteSmoothness` | `0.5` | 0 to 1 | — | How gradually it arrives. Low is a hard circle; high is a slow darkening reaching most of the frame |
| `ChromaticAberration` | `0.0` | fractions of frame width | Before the tone curve | Lateral dispersion — three taps of the scene at three offsets |
| `FilmGrain` | `0.0` | ≥ 0 | **Last**, after the LUT | Grain strength |
| `FilmGrainSize` | `2.0` | pixels per speck | — | The period of the noise lattice. Larger reads as a faster stock |

The vignette runs in **linear light, before the tone curve**, because a vignette
is less light reaching the corner — so it should roll off through the same
response curve the rest of the frame does. Applied afterwards it multiplies
display values and reads as a shadow somebody painted on.

Aberration is a lens effect too, and a lens disperses before the sensor sees
anything. The bloom is deliberately **not** dispersed: it is already blurred
wider than any sane offset, so two more taps would shift something nobody could
see had moved.

Grain lands after the tone curve **and** after the LUT, because grain is the
texture of the recording medium rather than a colour anybody graded — run it
before the LUT and the grade re-maps the noise, so grain changes character with
the look instead of sitting on top of it. It is built from two octaves of value
noise rather than a hash per pixel, so it is round clumps of varying size
instead of a grid of squares, and it is strongest in the **midtones**: film has
no variation left to show once nothing is exposed or everything is.

> [!TRAP]
> Below about 2, `FilmGrainSize` puts the finest of the octaves past what the
> pixel grid can resolve — it sharpens into noise instead of showing specks.

> [!NOTE]
> Grain animates, and it is seeded from the **frame number** rather than a
> clock, so rendering frame 30 twice produces the same bytes. That is the rule
> TAA's jitter follows, for the same reason.

## Reading a profile from a script

Every field above is reachable by name through the render-settings bridge, in
both languages — see [Scripting](scripting/index.md).

## Where to go next

- [Rendering](rendering.md) — anti-aliasing, shadows, and the frame's pass order
- [Core concepts](concepts.md#the-post-profile) — how profiles are attached,
  shared and saved
