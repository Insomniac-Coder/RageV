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
| `RayTracing` | `false` | — | shadows, on a device with ray queries |
| `RayTracedReflections` | `false` | — | ray tracing, with bindless materials |
| `RayTracedAmbientOcclusion` | `false` | — | ray tracing |
| `RayTracedGlobalIllumination` | `false` | — | ray tracing + bindless |
| `GiBounces` | `1` | 1 or 2 | ray-traced and voxel global illumination |
| `VoxelGlobalIllumination` | `false` | — | where a profile's Global illumination is on, and rays do not win; **greyed in the editor while ray-traced GI is on**, because the grid is not built at all then |
| `VoxelGiResolution` | `64` | 32, 64 or 128 | voxel global illumination |
| `VoxelGiCascades` | `3` | 1 to 4 | voxel global illumination |
| `VoxelGiVoxelSize` | `0.25` | metres | voxel global illumination |
| `ShadowCascades` | `4` | 1 to 4 | shadows, maps |
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

With maps, one directional light casts cascaded shadow maps and up to four
spot and four point lights have their own. With ray tracing on, every casting
light of every kind traces and no map is rendered. The five settings after
`RayTracing` are the directional cascades.

| Setting | Default | What it does | Cost |
|---|---|---|---|
| `ShadowsEnabled` | `true` | Off skips the shadow passes entirely | — |
| `RayTracedReflections` | `false` | With ray tracing on: trace the mirror ray from every glossy surface and shade what it hits, in place of the profile's screen-space reflections (their rows grey out and say so). Off-screen and hidden things reflect with correct parallax; rough surfaces keep the probe. Offered only where materials are bindless as well as ray queries. ENGINE-NOTES 7ao | One ray plus a hit shade per glossy pixel |
| `RayTracedAmbientOcclusion` | `false` | With ray tracing on: cast the ambient-occlusion taps as short rays into the scene in place of the profile's depth-buffer probe (its toggle greys out and says so; its radius and intensity still apply). No halos, off-screen occluders count. ENGINE-NOTES 7ao | Twelve short rays per half-resolution pixel |
| `RayTracedGlobalIllumination` | `false` | With ray tracing on: cast one diffuse bounce as four rays per pixel from every surface and shade what they hit, in place of the profile's screen-space gather (its Global illumination toggle greys out and says so; its intensity still applies). Light arrives from behind the camera and behind other things, and lands on the surface's own albedo. Needs bindless materials, as reflections do. ENGINE-NOTES 7at | Four shaded rays per pixel -- the most expensive switch here |
| `GiBounces` | `1` | How many times indirect light bounces before the reflection probe answers for the rest. Ray-traced: at 1 every bounce ray's hit is shaded with the probe's guess at what reaches it, which in a room the probe does not describe well leaves anything that can see nothing directly lit as dark as that guess; at 2 one more ray answers instead, and *its* hit takes the probe -- so the path ends at depth two by construction (ENGINE-NOTES 7ax). Voxel: at 2 the grid is also lit from last frame's grid, one bounce more each frame, which on a still scene converges on every bounce (7bc). Not read by the screen-space gather, which has one bounce and no way to have two | Ray-traced: one more ray per bounce ray; the temporal filter absorbs the noise. Voxel: one cone gather per occupied voxel, about 0.3 ms at the defaults |
| `VoxelGlobalIllumination` | `false` | Where a camera's profile asks for global illumination, gather it from a **voxelised scene** instead of from the screen: every frame the scene is rasterised into a clipmap of 3D grids around the camera, lit from the shadow cascades and the local lights, mipped per viewing direction, and cone-traced from every pixel. Light arrives from behind the camera, behind other things and off every edge of the frame -- what the screen gather cannot know -- **on both backends, with no ray hardware**. The profile's Global illumination stays the on switch and its GI intensity, quality and denoise apply; its GI radius does not (a cone runs to the grid's edge) and its row says so. **Ray-traced GI and the voxel grid are exclusive**: where the traced form runs, the grid is not built at all and these three dials grey out. A hybrid that read the grid at each ray's first hit was built and removed (8.13, ENGINE-NOTES 7be) -- it cost 21x the traced second ray it replaced, for four fifths of its quality.

Ray-traced global illumination wins where it runs -- and *wins* means the voxel grid is never built, so this row and its three dials are **greyed in the editor** while it is on, the way the post profile's SSR and SSAO rows are under their traced equivalents. Static meshes and terrain are voxelised; skinned meshes are not. A wall thinner than a voxel leaks a little light through itself. ENGINE-NOTES 7bc | About 0.1 ms to rebuild and light the grid and 0.9 ms for the gather at 1600x900 and the defaults; GI quality `High` gathers at full resolution and costs four times that |
| `VoxelGiResolution` | `64` | Voxels along each cascade's side, rounded to 32, 64 or 128 | Memory and the voxelisation cost go with the cube of it: about 25 MB at 64 and three cascades |
| `VoxelGiCascades` | `3` | How many nested grids, each covering twice the distance of the last at half the detail. Three at the default voxel size reaches 64 metres | One voxelisation of the scene each |
| `VoxelGiVoxelSize` | `0.25` | The finest cascade's voxel, in metres. Smaller resolves thinner walls and a smaller room; larger reaches further for the same grid | none |
| `RayTracing` | `false` | Trace rays instead of rendering shadow maps: one ray per pixel toward every casting light — sun, spot and point — into an acceleration structure of the scene. No acne, no detachment, no distance limit, no cap on how many lights cast; skinned casters cast their pose; the edge is hard. Offered only on a device with ray queries — Vulkan on hardware that traces, never OpenGL — and under `ShadowsEnabled`, whose pass builds the structure the rays trace into; elsewhere the row is absent, the value is kept, and the maps are used (a `--raytracing=on` on such a device is logged once and falls back). Applies at once — no restart. `--raytracing=on\|off` overrides it. ENGINE-NOTES 7am, 7an | One ray per light per pixel and a per-frame acceleration-structure build (skinned casters posed in compute and refit), no map renders |
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
