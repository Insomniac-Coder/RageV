# Rendering

What the frame costs, as opposed to what it looks like. Anti-aliasing, shadows,
and the order everything happens in.

These live in the **project**, edited in **Render Settings → Render settings**,
and written to the `.rvproject` the moment they change. They are a judgement
about the hardware, and that judgement does not change because a different
level loaded — see [Core concepts](concepts.md#where-a-setting-lives) for why.

The other half — exposure, bloom, grading, the lens effects — belongs to a
camera's post profile and is documented in
[Post processing](post-processing.md).

## Anti-aliasing

Six modes. They are not a quality ladder: three of them fix different problems,
and the right one depends on whether your aliasing is geometry, shading or
motion.

| Mode | What it does | Fixes shading aliasing | Cost | Its dials |
|---|---|---|---|---|
| **None** | Nothing. | — | none | — |
| **FXAA** | Looks at one pixel's neighbourhood and guesses where the edge was. | No | one pass, cheapest | none |
| **SMAA** | Reconstructs the silhouette: finds the run of pixels an edge spans, works out the slope, computes the coverage the rasterizer should have produced. | No | three passes over two small intermediates | none |
| **MSAA** | Several coverage samples per pixel, one shading sample. | **No** | bandwidth, not shading | `MsaaSamples` |
| **SSAA** | Renders the whole scene larger and averages down. | **Yes** | the *square* of the factor | `SupersampleFactor` |
| **TAA** | Jitters the projection each frame and accumulates, converging on a supersampled image for one sample's cost. | **Yes** | one extra pass, plus a history buffer | `TemporalFeedback`, `TemporalJitterScale`, `TemporalJitterPhase` |

**Default: FXAA.**

### Every render setting, at a glance

| Setting | Default | Range | Applies to |
|---|---|---|---|
| `AA` | `FXAA` | `None`, `FXAA`, `SMAA`, `SSAA`, `MSAA`, `TAA` | always |
| `MsaaSamples` | `4` | 1 to 8, and what the hardware offers | MSAA |
| `SupersampleFactor` | `2` | 1 to 4 | SSAA |
| `TemporalFeedback` | `0.6` | 0 to 1 | TAA |
| `TemporalJitterScale` | `1.0` | 0 upward; over 1 is legal | TAA |
| `TemporalJitterPhase` | `8` | any positive count | TAA |
| `ShadowsEnabled` | `true` | — | always |
| `ShadowCascades` | `4` | 1 to 4 | shadows |
| `ShadowResolution` | `2048` | powers of two | shadows |
| `ShadowDistance` | `40.0` | metres | shadows |
| `ShadowSplitLambda` | `0.85` | 0 to 1 | shadows |
| `ShadowNormalOffset` | `0.9` | shadow texels | shadows |

Each dial is ignored by every mode but its own, so a project can leave them all
set. The sections below say what each one is for.

The column that decides most choices is the third one. A specular highlight
sparkling across curved metal, or a texture shimmering into moiré, is a signal
the frame never sampled finely enough — no filter working on the finished image
can invent what was missed. FXAA, SMAA and MSAA all work on geometry edges and
do nothing for it. Only SSAA and TAA sample more.

MSAA is the one people expect to fix everything and does not: it takes several
*coverage* samples but runs the fragment shader **once**, so edges get several
levels of coverage for close to the price of one shaded pixel, and shading gets
nothing at all.

> [!NOTE]
> MSAA resolves in **linear light, before the tone curve**, which is the correct
> place and a visible difference on high-contrast edges.

### The dials in detail

#### `MsaaSamples` — MSAA only

Costs bandwidth and a little rasterizer work -- **not** the square of
anything. 4 is an ordinary choice, where supersampling at 4 would be a
statement.

#### `SupersampleFactor` — SSAA only

**Cost is the square**: 2 is four times the pixels shaded, 4 is sixteen.
Capped at 4 because a 4K output would then ask for a 16K target, which is past
what much hardware will allocate and all of what would be sensible.

#### `TemporalFeedback` — TAA only

How much of the accumulated image survives each frame. Costs nothing --
it is a blend weight.

**This is the ghosting-versus-sharpness dial, and there is no correct value.**
There is a measured curve, against a 4× supersampled render of the same frame,
on a scene with one textured patch falling 16 px a frame beside an identical
one standing still — RMS error, lower is better:

| Feedback | Moving | Still |
|---|---|---|
| no filter | 16.08 | 16.73 |
| 0.0 | 17.15 | 17.95 |
| **0.3** | **15.38** | 10.14 |
| **0.6** (default) | 16.32 | 5.39 |
| 0.9 | 19.83 | **3.82** |

Still content improves all the way up. Moving content bottoms out near 0.3 and
is *worse than no filter at all* by 0.9. 0.6 sits within a quarter of a unit of
no filter under motion while still being three times better standing still.

**Raise it for a mostly-still project; lower it for a mostly-moving one.** That
is the whole reason it is a number rather than a constant.

#### `TemporalJitterScale` — TAA only

How far the per-frame sub-pixel offset reaches, as a fraction of a pixel.

A different dial from feedback: feedback decides how much of the past is kept,
this decides how far apart the accumulated samples were taken. 1.0 is the whole
pixel, which is what makes the accumulation converge on the supersampled image
the mode aims at. Under 1 trades coverage for sharpness. Over 1 samples outside
the pixel — a deliberate blur, and why the range does not stop at 1.

> [!NOTE]
> **0 is not "TAA off".** The history still accumulates; it just accumulates the
> same sample every frame, which converges on the unjittered image and buys
> nothing.

#### `TemporalJitterPhase` — TAA only

How many frames the offset sequence runs before repeating.

The sequence is **Halton**, so successive points fall in the gaps earlier ones
left. Eight random offsets can easily leave a quarter of the pixel unsampled;
eight Halton offsets cannot.

### Overriding the mode

Three layers, narrowest last:

| Where | Scope | Use |
|---|---|---|
| The `.rvproject` | The project, for everyone | The real setting |
| The `AntiAliasing` key in `ragev.ini` | This machine | A viewing preference that survives a restart without editing a project file |
| `--aa=none\|fxaa\|smaa\|ssaa\|msaa\|taa` | This run | Checks, and comparing two modes back to back |

`--msaa=N` and `--supersample=N` override their dials the same way. The Render
Settings panel says so when one of the narrower layers is winning.

## Shadows

One directional light casts cascaded shadow maps; spot and point lights have
their own. These six settings are the directional cascades.

| Setting | Default | What it does | Cost |
|---|---|---|---|
| `ShadowsEnabled` | `true` | Off skips the shadow passes entirely | — |
| `ShadowCascades` | `4` | More cascades, better texel density near the camera | One scene render each. 4 is the usual answer and the most supported |
| `ShadowResolution` | `2048` | Per cascade, square | **The single biggest lever on both quality and cost** — four 2048 maps is 64 MB of depth |
| `ShadowDistance` | `40.0` | How far from the camera shadows are drawn at all, in metres | Shorter is sharper everywhere it reaches |
| `ShadowSplitLambda` | `0.85` | Blend between a logarithmic split (correct texel distribution, starves the far cascades) and a uniform one (the reverse). 1 is fully logarithmic | none |
| `ShadowNormalOffset` | `0.9` | How far along the surface normal a sample is pushed, in shadow texels | none |

> [!TRAP]
> `ShadowDistance` is **not** the camera's far plane, which is usually a
> kilometre. Past this distance the shadow texels are so large that the shadow
> is worse than no shadow, which is why it is a separate and much smaller
> number.

> [!TRAP]
> Raising `ShadowNormalOffset` removes acne and starts detaching shadows from
> their casters. **There is no value that has neither** — which is why the
> shadow pass also writes back faces.

## The order of a frame

Useful when a setting seems not to apply, because most of those turn out to be
questions about what ran first.

1. **Scene** — opaque geometry, writing colour, velocity and the surface
   description SSR reads
2. **Transparent** and **ResolveTransparent** — weighted-blended, sharing the
   scene's depth
3. **Overlay** — the debug draw, inside the HDR target so it depth-tests
4. **SSAA resolve** *or* **TAA resolve** — mutually exclusive, and both before
   bloom, because averaging is only meaningful in linear light
5. **SSAO** — occlusion is lighting, so it lands on the sharp image
6. **SSR** — a side chain: traced now, read by the *next* frame's lighting
7. **Depth of field** — after the resolve, before bloom
8. **Motion blur**
9. **Bloom**
10. **Tone map**, then the colour LUT, then film grain
11. **FXAA** *or* **SMAA** — these work on the finished image, which is what
    makes them post filters rather than sampling strategies
12. **UI**

MSAA does not appear in that list because it is not a pass: it changes what the
scene target *is*, and the hardware resolves it when the pass ends.

## Where to go next

- [Post processing](post-processing.md) — the other half of the settings, on
  the camera's profile
- [Core concepts](concepts.md#where-a-setting-lives) — which file a setting
  lives in, and why
