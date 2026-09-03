# WR-16 · One ray budget: the controller, the allocator, the consumers, and ReSTIR on top

**This is WR-16.** ReSTIR is no longer an item of its own; it is one implementation inside this system (section 4.3.3), and the item's number now names the whole.

**Status:** design, owner-commissioned 2026-09-02. Written for agents to
build from cold; every claim that is a number was measured this session on
the night scene (`SampleProject`, `GoldenGateDemo.rage`, 2560x1440, RTX
5070 Ti laptop, Vulkan) and the protocol that produced it is named.
**Owner's brief, verbatim:** "we should build and come up with a combined,
sophisticated system that works for all. One of my worries is that right
now since these systems exist as pieces they might end up fighting or not
cooperating well … general ray budgeting and light ray budgeting system can
be combined with restir on top."

The plan this sits beside is `docs/RENDERING-REVAMP.md` (WR-10, WR-16,
WR-17, WR-18 are the items this design absorbs). Read that document's
"Ground rules for every work item" first; every rule there binds here.

---

## 0 · The short version

Today three things decide how many rays a pixel gets, and none of them
knows about the others:

1. **The frame-level controller** (`Renderer::UpdateRayBudget`) watches the
   GPU time of the *ray passes* against an Absolute or Fractional target
   and steps one global scale between four levels — but the tile
   allocator, when on, bypasses that scale for the two types it serves.
2. **The tile allocator** (`importance_tiles` → `tile_reduce` →
   `tile_budget`) divides a *fixed* average of AO rays and GI rays across
   16x16 tiles by an importance product. It reads no frame time on purpose.
   Its damping is deliberately switched off because it oscillated.
3. **The light-ray rules inside the lit shader** — WR-17's distance
   thinning with borrow and floor, the light cutoff, WR-18's quad rate and
   refraction reach — are global settings chosen from a preset, blind to
   the tile and to the frame time.

And two large costs are outside all three: the shadow rays live inside the
lit pass where no timer sees them, and the **light walk at traced hits is
about 40 ms of a 114 ms frame** — the single largest item, measured today
with `--hit-lights=off` — and it is not a ray count at all.

The design is one **ray budget** with three layers and one contract:

- a **controller** that turns a target into a slowly-varying *pressure per
  ray type*, from measured time where a pass exists and from ray counters
  times a calibrated cost where the rays are embedded in a shading pass;
- an **allocator** that turns pressure and a per-tile importance (built
  from *inputs* only) into per-tile allocations for every ray type — AO
  rays, GI rays, the water's quad rate, the shadow-ray fraction, and the
  per-pixel shadow budget — quantised at the tile with a dead band and a
  dwell, which is what the damping needed and never had;
- **consumers** that read their tile's allocation and nothing else, each
  reducing work by a rule that is fixed per pixel and walks only under a
  temporal filter — the shadow-ray consumer having two implementations
  behind one interface, WR-17's thinning and WR-16's ReSTIR DI;
- and a **reconstruction contract**: one per-pixel history validity that
  every temporal consumer reads and none recomputes, and one accumulator
  per quantity, so ReSTIR keeps samples, the denoiser keeps radiance, TAA
  keeps the frame, and no two of them blend the same thing.

The largest single win on the table is not budgeting at all: **WR-10, the
light walk at hits**, and it goes first because every later percentage is
measured against a frame it changes.

---

## 1 · Where the frame goes, measured

Headland camera, untouched settings, 114.0 ms. Numbers from this session;
the shares come from isolation runs and overlap, so they sum past 100.

| item | ms | how it was measured |
|---|---:|---|
| light walk at traced hits (reflection + refraction hits, 191 lights each) | **~40** | `--hit-lights=off`: 114.2 → 74.4 (water pass 74.8 → 39.8, opaque 37.4 → 32.5); Pier 100 → 58, Glitter 107 → 68 |
| shadow rays, direct lighting (one per light per pixel, up to 178 lights) | ~38 | third session's isolation at 1600x900, scaled; WR-17's presets take 14–64 of it |
| water refraction rays (traversal; the walk is above) | ~13 of the 33 | WR-18's reach: 114 → 101 with a null diff |
| water mirror rays | ~15 | isolation; WR-18's quads take a quarter back |
| the lit loop's own BRDF walk (146 lights of Beckmann per water pixel) | ~17 water, ~14 steel | RT-off isolation |
| AO, GI, post, everything else | < 2 together | by-pass table |

Two consequences shape everything below. **The frame is pixel- and
ray-bound, and half its ray cost is light *reads*, not traversal.** And
**far rays are the expensive rays**: over the water toward a lamp half a
kilometre away a ray crosses most of the bridge's thin steel; keeping one
in eight of them kept half their time (WR-17's floor arm).

---

## 2 · What exists, by name

Read these before writing a line; the design reuses all of them.

**Controller.** `Renderer::UpdateRayBudget(rayGpuMs, frameGpuMs)`
(`Renderer.cpp:267`). Modes `RayBudgetMode::Off / Absolute / Fractional`
(`PostSettings.h:208`, fields `RenderSettings::RayBudget`, `RayBudgetMs`,
`RayBudgetFraction`). Fractional is solved as a fixed point
(`rays = f * fixed / (1 - f)`), not chased. Ten settle frames, a cooldown
per change, an asymmetric dead band (drop above 1.35x, climb only if the
climb would hold), the first move straight to the right level. Output
`Renderer::GetRayScale()` over `kRayLevels = {1, 0.85, 0.7, 0.5}`, read at
`FrameGraphBuilder.cpp:1557` (GI rays) and `:1674` (AO taps) — **only when
the tile allocator is off**. `--ray-budget=<ms>` forces Absolute.

**Allocator.** `FrameGraphBuilder.cpp:1170–1350`. 16x16 tiles
(`kTileSize`), a `TemporalHistory` pair "RayBudget" in
`R16G16B16A16_SFLOAT`. `importance_tiles.rvshader`: importance =
coverage x motion x complexity, a *product* so a zero factor zeroes the
tile; motion (the TAA velocity) replaced "how much the lit image changed"
because that was an *output* of the rays and the loop breathed at a hertz.
`tile_reduce` → the mean. `tile_budget.rvshader`: a tile's share is its
weight over the mean, times the per-type average (`RayBudgetAoAverage`,
`RayBudgetGiAverage`), clamped by `RayBudgetSpread`; AO in `.r`, GI in
`.g`. Consumers: `rtao_compute.rvshader:301` reads `u_Budget.r`,
`rtgi_trace.rvshader:257` reads `u_GiBudget.g`. **`budget.Advance()` is
deliberately absent** (comment at `FrameGraphBuilder.cpp:1307`, commit
`218c742`): with history the ±1-ray-a-frame easing fed `floor(x + 0.5)`
and limit-cycled; the comment names the fix — a dead band on the tile
count — and this design supplies it.

**Light-ray rules in the lit shader** (`pbr_fragment.glsl`; scene-UBO rows
`ShadowRayFade` and `RayRates`, mirrored in `scene_block.glsl` and
`Renderer3D.cpp`'s `SceneUniforms`): `ShadowRaySkip` / `ShadowRayKept`
(WR-17: shape, start, end, floor; interleaved-gradient-noise dither with
an R2 shift per light, walked per frame only while `u_Scene.Jitter` is
non-zero; a skipped light borrows `lastFarVisible`, the last thinned light
this pixel traced); the light cutoff (`Renderer3D.cpp`, the range clamp on
the frame's copy before `LightGrid::Build`); `QuadTraceLane` /
`QuadTraces` / `QuadShare` (WR-18: one mirror and refraction ray per 2x2
quad on the water, `GL_KHR_shader_subgroup_quad`); the refraction reach
(`-ln(floor) / sigma_min`). All chosen by `RenderSettings::RtOptimisation`
through `RayOptimisationPresetFor` (`RenderSettings.h`): Off / Quality /
Balanced / Performance. Measurement flags: `--shadow-rays=`,
`--light-cutoff=`, `--ray-rate=`, `--refraction-floor=`,
`--rt-optimisation=`, `--hit-lights=`.

**Traced-hit shading.** `ShadeTraced` (`pbr_fragment.glsl` ~2148): Lambert
over `u_Scene.LightCount` lights, no cluster, a shadow ray for the sun
alone, with a `distance2 >= range²` and an `NdotL <= 0` early-out. Reads
all 191 `GpuLight` records (80 B) per hit.

**Clusters.** `LightGrid` (CPU, 16x9x24 view-space cells, lights binned by
range sphere), `u_Cells` / `u_CellIndices` at set 0 bindings 9 and 10,
`ClusterIndexFor(worldPos)` — valid for **any world point inside the
frustum**, not only the fragment's own.

**Temporal state.** `TemporalHistory` (pairs of targets, `Prepare`,
`Current`, `Previous`, `HasHistory`, `Advance`, `Invalidate`, an optional
second attachment). TAA (`taa_resolve.rvshader`: reprojection, off-screen
rejection, neighbourhood clamp floored by the GI denoiser's moments,
`kMaxFrames = 64`, still-feedback boost OFF by owner's ruling). GI denoiser
(`gi_denoise.rvshader`: per-pixel sample count and two luminance moments
in the second attachment; validity in the colour's alpha). RTAO
accumulator (its sample pattern is fixed when nothing accumulates).

**Bake.** `RayTracedGiSource: Baked`: the irradiance field terminates GI
rays; `ProbeIrradiance(normal, probe)` is what a hit adds as indirect; the
lighting hash keys on the baked lights' parameters (`Scene::FieldBakePath`).

**Presets and settings.** One global value per lever, never per light
(owner's rule, memory `feedback_global_render_settings_not_per_light`).

---

## 3 · Goals and non-goals

**Goals.**
1. One place decides every ray count and every light read per pixel or
   tile; shaders read allocations and never re-thin.
2. The frame holds a target on this GPU across the eight cameras
   (`bench_night.py`) without visible stepping: no whole-screen quality
   change more than once in a settled minute, no tile popping.
3. Every reduction is judged by the standing bar: per-pixel diff images
   against the untouched frame (`shadow_ray_matrix.py`), the flicker
   protocol under no AA, MSAA and TAA (`check_glint_flicker.py`), and the
   eight-camera table.
4. Every reduction holds still without a temporal filter and integrates
   with one (WR-15's rule, owner's constraint).
5. The largest cost first: the light walk at hits, before any budgeting.
6. Baked and realtime alike: with GI baked its demand is zero and nothing
   idles at a level chosen for a load that is not there.

**Non-goals.** ReSTIR GI (rejected, unchanged reason: a second accumulator
on the bounce fights the denoiser). A per-pixel allocator (unaffordable
twice: normalising millions of weights, and divergent loop counts inside a
wave). Any visible parameter as a function of a value that moves frame to
frame (the 2026-08-28 lesson). Per-light dials.

---

## 4 · Architecture

```
                 target (preset / Absolute ms / Fractional)
                                  |
   measured time per pass ----->  CONTROLLER  <----- ray counters x calibrated cost
   (AO, GI, water, lit)          pressure p_t per ray type, slow, dead-banded
                                  |
   inputs only: coverage,         v
   motion, material,      ---> ALLOCATOR (16x16 tiles) --> u_RayAllocation (RGBA16F x2)
   light count, class             per tile, per type; quantised with a
                                  dead band and a dwell; consumers read it
                                  |
        +------------+------------+-------------+-------------------+
        v            v            v             v                   v
       AO           GI      water quad rate   shadow-ray         steel mirror
     (rtao)      (rtgi)     + refraction     consumer: WR-17     rate (later)
                             reach (fixed)   thinning | ReSTIR DI
                                                    |
                            RECONSTRUCTION CONTRACT: one validity, one accumulator per quantity
                            (TAA: frame; gi_denoise: bounce; ReSTIR: samples; nobody blends twice)
```

### 4.1 The controller: pressure per ray type

Keep `UpdateRayBudget`'s discipline — settle frames, cooldown, the
asymmetric dead band, the fixed-point Fractional — and change what it
outputs and what it reads.

**Output:** not one scale over four levels but a **pressure `p_t ∈ [0, 1]`
per ray type** `t ∈ {AO, GI, WaterRays, ShadowRays}`, continuous, moved by
at most one step per cooldown, where `p = 0` means "spend the preset's full
average" and `p = 1` means "spend the preset's floor". The consumer-side
quantisation (4.2) is what makes a continuous pressure safe; the old design
quantised at the controller and stepped the whole screen.

**Input:** ray time per type. Where a pass exists (AO, GI, and reflections
once they have a pass) the by-pass GPU timers are the truth. Where the rays
are embedded in a shading pass (shadow rays, the water's mirror and
refraction rays, hit shading) the pass timer cannot separate them, so:
- **counters**: an SSBO of per-type `uint` ray counts, incremented per
  subgroup (`subgroupAdd` then one atomic per wave), zeroed each frame,
  read back a frame late like the timers are;
- **calibrated cost** `c_t` in ms per million rays, stored in the project
  (`RenderSettings::RayCost[t]`), set by the isolation protocol
  (`--shadow-rays=off`, `--hit-lights=off`, `--ray-rate=1|2`) at project
  setup and re-checked by `bench_night.py` — never learned online (an
  online fit is an output feeding an input, the loop this engine has
  already met);
- `time_t = count_t * c_t`, and the embedded pass's remainder is the fixed
  cost the Fractional fixed point needs.

**Split by type, as the owner's original document asks (§16):** the target
is apportioned by the preset's *shares* (e.g. Quality: shadows 40%, water
35%, GI 15%, AO 10%); a type under its share lends to a type over it, and
a type with zero demand (GI baked) lends everything. Pressure rises only on
the types over their share. This is the "if reflections become expensive,
reduce GI samples" rule, and it is what stops one feature quietly eating
two others.

**What it must not do:** read image variance (an output), change more than
one type per cooldown, or move while `s_RayBudgetSamples < kSettleFrames`.

### 4.2 The allocator: importance to allocations, per tile

Keep the pipeline (`importance_tiles` → `tile_reduce` → `tile_budget`) and
widen it from two lanes to two RGBA16F textures (eight lanes), Vulkan-only,
declared under `RV_RAY_SHADOWS` so OpenGL's sampler budget (31 of 32 on
the layered variant) is untouched:

| lane | allocation | consumer | today's equivalent |
|---|---|---|---|
| A.r | AO rays per pixel | `rtao_compute` | `u_Budget.r` |
| A.g | GI rays per pixel | `rtgi_trace` | `u_GiBudget.g` |
| A.b | water ray rate: 1 or 2 (lanes per quad axis) | water lit shader | `RayRates.x`, global |
| A.a | shadow-ray fraction ceiling `f_max ∈ [floor, 1]` | `ShadowRaySkip` | `ShadowRayFade`, global |
| B.r | shadow-ray budget per pixel `N_s` (ReSTIR) | ReSTIR DI | — |
| B.g | steel mirror rate (later, see 4.3.4) | opaque lit shader | — |
| B.b, B.a | spare | | |

**Importance inputs, per tile, inputs only** (the hertz lesson): coverage
(surface present), motion (TAA velocity), material complexity (roughness,
metallic), **light count** (the maximum `Count` over the tile's clusters
across its depth range — the CPU has it, `LightGrid::Cells()`; write a
per-tile max into the tile map's fourth lane before the importance pass),
and **class** (water, steel, terrain — from the surface buffer's material
flags). Never the lit image's variance.

**Quantisation with a dead band and a dwell — the fix the comment at
`FrameGraphBuilder.cpp:1307` asks for.** For each integer lane (AO rays,
GI rays, rate, `N_s`) a tile changes its value only when the continuous
target has crossed the *next* boundary by a margin (enter at
`n + 0.75`, leave at `n + 0.25`: a 50% hysteresis band) **and** the tile
has held its current value for at least `kDwellFrames` (8). For the
continuous lane (`f_max`) a rate limit of 1/64 per frame. With that,
`budget.Advance()` comes back and the ±1-a-frame easing is replaced by the
band; the acceptance test is the one that reverted it three times: the
showroom ceiling and the night water, sixty seconds, transitions per tile
counted (§7).

**Tile uniformity is a correctness requirement, not an optimisation.** The
water's quad rate must be uniform per 2x2, and it is uniform per 16x16
tile by construction; a wave never straddles two allocations.

### 4.3 The consumers

Each consumer reads its lane and applies one rule. None reads frame time,
none re-decides.

#### 4.3.1 AO and GI — as today
`rtao_compute` and `rtgi_trace` already read per-tile counts. The
resolution rung (`RayDetailIsFullRes`) stays a preset choice, not a
per-tile one; the memory says Medium's half resolution was indistinguishable
at 12x on the showroom, so Quality may keep High and the others Medium.

#### 4.3.2 Water rays — WR-18, per tile
`QuadTraceLane` reads `A.b` for its tile instead of `RayRates.x`. The
refraction reach is physics, not budget: it stays `-ln(1/256) / sigma_min`
on every preset but Off. The quad walk under TAA and the hold without it
are unchanged.

#### 4.3.3 Shadow rays — one interface, two implementations

**Interface.** For a pixel with a cell list of `n` lights and a per-tile
allocation (`f_max`, `N_s`), the consumer returns a visibility estimate
`v_i` per light that the lit loop multiplies into that light's term. The
unshadowed term of every light is computed *before* visibility is asked
for (it is today: `radiance`, `NdotL`, the BRDF), so the consumer can use
it as importance for free.

**Implementation A — thinning (WR-17, shipped).** Deterministic: the
skip fraction from distance capped at `1 - f_max`, the fixed dither, the
local borrow, the floor. Holds under every AA mode by construction. Its
limit is measured: below one traced far ray in two, the per-lamp
visibility field under the bridge cannot be reconstructed by any borrowing
rule (Balanced's 3.5–5% residual), and counting a skipped light lit puts a
group's shadow back (70 levels on the water under the deck).

**Implementation B — ReSTIR DI (WR-16).** The same interface, sampling
instead of thinning. Per pixel: **RIS** over the cell list with the
unshadowed term's luminance as the target function (already in hand),
`M = min(n, 16)` candidates; **one reservoir per `N_s`** (`N_s` from
`B.r`, 1–4), 16 bytes each in a `TemporalHistory` pair; **temporal reuse**
with the previous frame's reservoir reprojected, `M` capped at 20x the
current, discarded (not blended) where the reconstruction contract says
the history is invalid; **spatial reuse** one pass, 3–5 neighbours within
a 16 px radius, rejected on depth (10%) and normal (25°); **`N_s` shadow
rays** to the survivors; the unbiased combination weight (Bitterli 2020
§4.3; the SIGGRAPH 2023 course's generalised RIS) — this is where a
plausible-looking image with wrong weights is the standard silent failure,
so the fixture in §7 tests the *weights*, not the look. The flashing
emitters (tower beacons, nav lights) hard-clamp their reservoir history
like every other history in this engine. **Without a temporal filter
(`u_Scene.Jitter == 0`) temporal reuse is off** and the estimator is
per-frame RIS + spatial reuse with the fixed dither seeding candidates —
noisier, still unbiased, still holding still. That is what makes B admissible
under the owner's rule at all.

**Selection.** The preset chooses: Off and Quality use A (Quality's 300/600
is free by the bar); Balanced and Performance use B when the flicker
protocol and the diff say it beats A at the same cost, else A — decided by
measurement in M4, not by this document.

**Why B is not "thinning with reuse".** A decides per light; B decides per
pixel which lights are worth a ray at all and reuses that *decision*
across time and neighbours. The matrix showed the decision, not the ray,
is what the far lamps lack: one in eight rays per light cannot tell the
deck from the sky, but four rays to the four lamps that matter most, kept
and refined over frames, can. That is the whole bet of M4, and §7's
pre-check measures it before three weeks are spent.

#### 4.3.4 The steel's mirror rays — later
The opaque lit shader traces a mirror ray per pixel at roughness under the
gloss window (the whole bridge at 0.27). Quad sharing is not safe there
yet: a quad can hold a lane the thin-member fade discarded, and a broadcast
from a dead lane is undefined. Two routes, pick by measurement: `demote`
semantics (`GL_EXT_demote_to_helper_invocation`, SPIR-V 1.5) so a
discarded lane still participates in quad ops, or the separate half-res
reflection pass once the normal buffer is 16-bit. Either way the rate comes
from lane `B.g`.

#### 4.3.5 Hit shading — WR-10, the largest item, not a ray count

The 40 ms is 191 x 80-byte reads per hit plus one range test each. Three
options, ordered by generality; do A first, it is exact and quick, then
measure whether C is wanted for this scene.

**A. Compact records and the on-screen cluster at the hit** (1–2 days,
exact). Split `GpuLight` into a 16-byte cull record (`position.xyz`,
`range²`) in its own SSBO and the full record; the walk reads the cull
array and fetches the full record only in range. For a hit inside the
frustum — the reflected bridge, the refracted seabed, nearly every hit on
these cameras — use `ClusterIndexFor(hitPosition)` and walk that cell's
list instead of all 191; off-frustum hits walk the cull array. Under the
bridge a cell still holds ~146 lamps, so the read drops from 15 KB to
~2.3 KB per hit and the ALU stays; the expected saving is most of the
memory-bound part, measured by `--hit-lights=off` against the new walk.
Bit-identical where every in-range light is in the cell (the grid is
conservative), so the acceptance is a diff of zero.

> **Measured 2026-09-02, late: A is a null on the night scene** (Headland
> Off 115.0 vs 114.4, Quality 88.1 vs 89.7). The old walk's range test
> already cost one dot product per out-of-range light; the 40 ms is the
> ~146 in-range lamps' 80-byte reads. The compact-record half of A (64 B,
> then 32 B at half precision) is the part that addresses it and was not
> yet built; the per-cell live summary (an ambient cube per cluster cell,
> rebuilt each frame, read at hits) is the other live route. **C exists
> now as the Full bake mobility**: a fully baked light's direct light is
> stored in its own nine coefficients per cell and read at hits and on
> screen (diffuse only; highlights are the open half). Whether the bridge
> lamps take it is the owner's static/moving split, next.
>
> **The split exists (2026-09-03, ENGINE-NOTES 7cx):** `Static` per mesh
> and per terrain, off by default; the bake sees static objects only, a
> static surface reads fully baked lamps from the field, a moving one takes
> them live, and a moving object's shadow is subtracted from the field's
> light on static pixels through a ray against the moving objects alone --
> only for lamps with a moving object inside their range. Which bridge
> lamps go Full bake is now purely the owner's lighting call.

**B. A world-space grid with per-cell CDF sampling** (3–4 days, the WR-10
brief's original). Only if A leaves the walk expensive: 1–4 lights per hit
sampled by the cell's `power / d²` CDF with `1/pdf` weights. Adds noise to
the reflection of the bridge that TAA must eat; judged on the moving scene.

**C. A direct-light field for the baked lights** (2–3 days, this scene's
big win, and a quality gain; **the light mobility enum — Realtime / Half
bake / Full bake, replacing `IsBaked` — is this option's setting, not a
separate task**: Half bake stores direct light for hits only, Full bake
for the screen too; the scene stays Half bake for the car shot). The solve already stores the bounce; store
the *direct* irradiance of every baked light too (a second field, or two
more channels), and let `ShadeTraced` add that field and walk only the
realtime lights (four marine lights and the beacons here). One 3D fetch
replaces 191 reads, and — since the bake includes visibility — the hits
gain the lamp shadows they do not have today (a hit under the deck is
currently lit by lamps through the roadway). The field's resolution is
metres, which is right for a reflection on wavy water and a seabed seen
through it. Keyed by the lighting hash like the bounce; realtime lights
stay live.

---

## 5 · The cooperation contract — the owner's worry, answered rule by rule

| pair | how they fight today | rule |
|---|---|---|
| controller vs allocator | the global scale and the tile counts could both scale GI/AO; today the code makes them exclusive by accident (`hasRayBudget ? … : … * scale`) | the controller feeds the allocator's *averages* through pressure and touches no consumer; `GetRayScale()` is retired |
| allocator vs WR-17/WR-18 | the tile map allocates AO/GI while the lit shader thins shadows and quads globally, blind to the tile | one allocation texture; the lit shader reads `A.b`, `A.a`, `B.r` for its tile; the global rows become the *floors* the preset guarantees |
| thinning vs ReSTIR | both would decide shadow rays for the same pixel | one implementation active per pixel, chosen by preset and by `Jitter`; never both |
| ReSTIR vs TAA vs the GI denoiser | three temporal accumulators on one image (the reason ReSTIR GI was rejected) | one accumulator per quantity: reservoirs keep *samples*, the denoiser keeps the *bounce*, TAA keeps the *frame*; ReSTIR emits shaded direct light that TAA sees as ordinary shading, and no separate direct-light denoiser at first |
| history validity | every filter has its own rejection (off-screen, depth, normal, disocclusion) | one per-pixel validity written once at reprojection time (TAA's, extended with depth/normal) into a shared R8 target; every temporal consumer reads it; disocclusion discards, never blends |
| dither walking vs AA mode | a random draw blinks under no AA (WR-15's first landing) | every stochastic choice is IGN + R2 per pixel and light, walking only while `Jitter != 0`; ReSTIR's temporal reuse is off without a filter |
| damping vs quantisation | an integrator feeding `floor(x + 0.5)` limit-cycles (three reverts) | quantise with a hysteresis band and a dwell at the tile; damp nothing else |
| importance vs output | image variance as importance breathes at a hertz | importance from inputs only: coverage, motion, material, light count, class |
| skipped ray vs truth | "lit" restores a group's shadow; a global mean darkens lit lamps | a skipped light borrows a *neighbour's* traced visibility, and a floor keeps neighbours traced; below that, sample (ReSTIR) rather than thin |
| presets vs flags | a flag could silently change what a preset means | precedence, fixed: measurement flags > preset > project; a run with any flag prints the resolved allocation in the benchmark report |
| the bake vs runtime rays | a cutoff or a proxy could change what the solve sees | nothing in this system applies while `BakeLighting` / `ForceLightingBake`; the hash keys on authored lights only |

---

## 6 · Interfaces

**Scene UBO** (three mirrors: `scene_block.glsl`, `pbr_fragment.glsl`,
`Renderer3D.cpp`): the existing `ShadowRayFade` and `RayRates` rows become
the **floors** (what a preset guarantees whatever the allocator says);
append `RayAllocationInfo` (`x` = tile size in pixels, `y` = 1 when the
allocation textures are bound, `zw` = tiles across and down).

**Allocation textures:** two `RGBA16F` at tile resolution, a
`TemporalHistory` pair each, set 0 binding 19 and 20 under
`RV_RAY_SHADOWS` only (Vulkan; the RT path). Written by `tile_budget`,
read by `rtao_compute`, `rtgi_trace`, and the lit shader (water and
opaque variants).

**Ray counters:** one SSBO of 8 `uint` at a set-0 binding under
`RV_RAY_SHADOWS`, incremented per wave (`subgroupAdd` + one atomic), read
back through the same fence the GPU timers use. Reported in the benchmark
report as "rays: shadow N M, water M, GI M, AO M" and in the HUD.

**Reservoirs (M4):** a `TemporalHistory` pair, `RGBA32UI`, one texel per
pixel per `N_s` (four textures at `N_s ≤ 4`, or one `RGBA32UI` array):
light index, `W`, `M`, target-function value.

**Validity:** one `R8_UNORM` written by the TAA reprojection (or by a
tiny pass when TAA is off: then it is all ones and nothing reuses).

**Settings:** `RenderSettings::RtOptimisation` stays the user's one
dial. Behind it the preset table gains per-type averages and floors and
shares; `RayBudget` (Off / Absolute / Fractional) stays as the controller's
mode. `--ray-budget=`, the WR-17/18 flags and `--hit-lights=` stay as
measurement overrides. Debug views (owner-asked, from the multi-light
document): rays per pixel, lights per pixel, allocation per tile, ReSTIR
`M` per pixel — `--debug-view=rays|lights|allocation|reservoirs`.

---

## 7 · Milestones, each with its acceptance test

Every milestone is independently shippable and revertible, ends with
`scenetest` green on both backends, and is measured by the same three
instruments: `bench_night.py` (eight cameras), `shadow_ray_matrix.py`
(three cameras, diff images), `check_glint_flicker.py` (no AA / MSAA /
TAA). Numbers below are the bars, against today's Headland 114 / Quality
90.

**M0 · Instrumentation (1 day).** Per-type ray counters; the calibration
table and its protocol; `--debug-view=rays|lights`; the benchmark report's
rays line; the shared validity target (written, not yet read).
*Accept:* counters agree with the isolation runs within 5%; bit-identical
picture.

**M1 · WR-10 A, compact records and the on-screen cluster at hits
(1–2 days).** *Accept:* diff of zero against the walk on the three cameras
(the grid is conservative); Headland under 90 ms from 114 with everything
else Off, and the water pass's share of `--hit-lights=off` recovered by at
least half. Then decide C from the number that is left.

**M2 · The allocator widened (3–4 days).** Eight lanes, light count and
class in the importance, the dead band and dwell, `Advance()` restored,
WR-17's ceiling and WR-18's rate read per tile. *Accept:* the reverted
test — sixty seconds still on the showroom ceiling and on the night water,
tile transitions counted per second, under one per hundred tiles; the
flicker protocol at or under today's numbers in all three AA modes; diff
against the global presets within 0.5% over 2 levels; ms at or under.

**M3 · The controller (2–3 days).** Pressure per type, shares, embedded
types through counters x cost, `GetRayScale()` retired. *Accept:* the
showroom's oscillation run (eight changes in a short run historically):
at most one whole-screen change per settled minute; the eight cameras
within ±10% of an Absolute target where the target is reachable, and a
plain log line where it is not (the fixed cost exceeds it).

**M4 · ReSTIR DI behind the shadow interface (2–3 weeks).** First the
**pre-check, one day**: a fixed budget of 1, 2, 4 and 8 rays per pixel
spread by importance over the cell list, no reuse, under the three AA
modes, diffed — it bounds what any budgeted scheme can reach here before
the reservoirs are written. Then RIS, temporal, spatial, weights.
*Accept:* a fixture with the lamp row and a known occluder where the
estimator's mean over 100 frames matches full tracing within 1% per
light (the weights test); the flicker protocol; and on the three cameras,
Balanced's cost (74 / 70 / 59) with a diff under Quality's (0.3 / 0.6 /
1.9% over 2 levels). If it does not beat thinning at the same cost, say so
and keep A — the plan's own rule.

**M5 · WR-10 C, the direct-light field (2–3 days, optional).** *Accept:*
hits under the deck darken where the bake says they should (a quality
diff, judged by eye and by the fixture), and the walk's remaining cost
gone on the three cameras.

---

## 8 · Traps, all paid for this session — read before touching anything

- **Counting a skipped ray as lit restores a group's shadow**: a hundred
  negligible lamps behind one slab are not negligible (70 levels on the
  water under the deck).
- **Borrowing the global mean darkens lamps that are visible**; borrow the
  neighbour's. Below one traced far ray in two, no borrowing rule
  reconstructs the field — sample instead.
- **"Skip when the contribution is small" is the Share shape and it lost**:
  contribution is small exactly where a hundred small shares add up.
- **Far rays are the expensive rays**; a floor on their count is a floor
  on their time.
- **Quad ops need every lane at the call and no dead lane in the quad**;
  the idle lanes wait, so a 4x ray cut is a 1.25x time cut.
- **A random draw blinks under no AA**; fixed IGN + R2, walking only under
  `Jitter`.
- **An integrator feeding a quantiser limit-cycles**; band and dwell at
  the consumer, damp nothing upstream.
- **Image variance as importance breathes**; inputs only.
- **A header layout change wants `--clean-first`**; `-- -m`, never
  `-- /m` (Git Bash rewrites it and the clean has already run); absolute
  paths for anything detached; never build while a matrix runs — the
  staged shader swaps under the benchmark.
- **`--frame-time=0` is the wall clock**; pin with `0.000001`.
- **The lighting hash keys on the authored lights**; nothing here may apply
  during a bake.

---

## 9 · What this document does not decide

- Whether Balanced ships with thinning or ReSTIR: M4's numbers decide.
- The preset's per-type shares: start at shadows 40 / water 35 / GI 15 /
  AO 10 and let M3's eight-camera table move them.
- The steel's mirror rate mechanism (demote vs a pass): 4.3.4, by
  measurement.
- Whether WR-10 C is built: M1's leftover decides.
- VRS and render scale (the candidates list in RENDERING-REVAMP) sit
  outside this system and compose with it; the allocator's tile map is a
  ready-made shading-rate image if VRS is ever taken.

---

## 10 · Review and preparation, 2026-09-03 (solo, after the static/moving split)

**What changed under this plan today** (ENGINE-NOTES 7cx): every object has
a `Static` flag and the bake sees static objects only; lights gained
`HybridFullBake` (half baked within `HybridRadius` of the lamp, fully baked
beyond) and the bridge's 176 lamps use it at 2 m; three more volumes cover
the sea floor, the headland and the south shore; the atlas packs volumes
as boxes; a fully baked lamp is lit live wherever the field does not cover
the pixel. Measured, three interleaved pairs, fields loaded and verified:

| camera | Half bake (shipped) ms | Hybrid ms | opaque pass | water pass |
|---|---|---|---|---|
| Headland | 94.0 | 59.0 | 32.1 → 14.5 | 60.0 → 42.7 |
| Deck | 13.8 | 10.2 | 9.8 → 6.1 | 1.4 → 1.5 |
| Profile | 43.2 | 29.7 | 8.9 → 5.6 | 32.8 → 22.6 |
| Bluff | 79.6 | 41.0 | 36.1 → 13.0 | 41.3 → 26.0 |
| Pier | 95.3 | 50.2 | 37.5 → 14.7 | 55.5 → 33.3 |
| Cliff | 43.0 | 24.8 | 23.7 → 11.9 | 17.2 → 10.9 |
| Glitter | 77.8 | 46.3 | 31.8 → 13.2 | 44.1 → 31.1 |
| Lime Point | 94.8 | 54.1 | 39.7 → 15.0 | 53.0 → 37.0 |

542 → 315 ms over the eight, 42% off. **Hybrid at 2 m against pure Full
bake** (every lamp fully baked, same boxes, three interleaved pairs): 303
→ 313 ms over the eight, +3%; per camera +2% (Headland, Profile, Bluff)
to +5% (Glitter). That is the whole price of keeping the lamp heads live:
the hybrid loop still reads every lamp's record to learn its radius, and
the two or three lamps within 2 m of a pixel are traced. The radius
slider walks up from there; each metre of radius buys back post and pool
at a cost this table does not measure (the owner declined the 10 m
point).

### Findings, in the order they change the plan

1. **§1 and every bar in §7 are stale.** The frame this plan budgets is
   the Hybrid frame above, not Headland 114 / Quality 90. M1's "Headland
   under 90 from 114" is already true for a different reason; the bars
   have to be re-based on `bench_night.py --label wr16-before` taken on
   the Hybrid scene, once, before any milestone starts.
2. **§4.3.5 C exists and is applied, and it leaves a cost the doc did not
   foresee.** At a static hit inside a volume every fully or hybrid baked
   lamp is skipped -- but only after its 80-byte `GpuLight` has been read
   to learn its mobility and radius. The hit walk's remaining cost is
   therefore *reads of lamps that contribute nothing*. M1's compact
   record should carry position, range², and the packed mobility/radius
   (16 B), so a static hit rejects a lamp on one read; and the CPU can
   mark each cluster cell with "a lamp is live here" (realtime, half
   baked, or hybrid within its radius of the cell) so a static hit in a
   fully baked cell walks nothing. That is the honest M1 now.
3. **The water surface is the frame.** Headland: 42.7 of 59.0 ms is the
   water pass, and the opaque static world is 14.5. The water is live by
   the owner's rule (the lamps' streak), so it walks 146 lamps of Beckmann
   and traces their shadow rays per pixel. §4.3.2 (quad rate, refraction
   reach) and §4.3.3 (the shadow consumer: thinning, then ReSTIR) are
   water items first; the opaque path is mostly off the table.
4. **§2 is stale in one line:** traced hits walk the cluster cell when the
   hit is in view (WR-10 A, `224b273`), not "every light, no cluster".
5. **Bindings.** Set 0 uses 0–18 today (18 is the irradiance field); 19,
   20 and 21 are free, so §6's allocation textures at 19/20 and a counter
   SSBO at 21 fit, all under `RV_RAY_SHADOWS`. RENDERING-REVAMP's ground
   rule "set 0's bindings are all spoken for" means the existing ones
   cannot be repurposed, not that none are free.
6. **The validity lane already exists.** TAA's second attachment writes
   `o_Moments = vec4(frames, mean, meanSq, 0.0)`; its `.w` is a free lane
   in a `TemporalHistory` attachment every temporal consumer can bind.
   The shared validity of §5 can live there, written once by the TAA
   resolve, no new target. With TAA off there is no history and nothing
   reuses, which is §5's rule already.
7. **Counters need a readback the RHI does not have.** `RHIDevice` reads
   textures one-shot (stalling) and resolves timestamps through query
   pools; there is no buffer readback. M0's first task is
   `RHIDevice::ReadBuffer` with a per-frame-in-flight staging ring, read
   one frame late the way timestamps are. `subgroupAdd` needs
   `GL_KHR_shader_subgroup_arithmetic` beside the basic and quad
   extensions the lit shader already requires.
8. **Four launch sites carry every ray**, so four increments count them
   all: `TraceShadowFromMasked` (every shadow ray; the mask says whether
   it is the sun, a lamp, or a moving-only ray), `TraceSurface` (mirror,
   refraction, GI, sky -- the caller knows which, so it passes a type),
   `rtao_compute`, and the fill's shadow ray (bake time, not counted).
9. **There is no `--debug-view` yet.** M0 adds the flag and one composite
   that reads a per-pixel `R16UI` "rays this pixel" the lit shader writes
   only under the flag.
10. **Hybrid changes the allocator's "light count" input.** A tile's
    importance (§4.2) should count the lamps that are *live* at its
    pixels -- realtime, half baked, or hybrid within radius -- not the
    lamps in the cell, or the allocator spends rays where the field has
    already paid.
11. **`HybridRadius` is authoring, not a preset lever**: it is per light
    by the owner's design (the slider), and §5's "presets vs flags" table
    should say so; the presets keep every other lever global.
12. **The bake-versus-runtime rule holds** through today's changes: the
    field weight and the baked share apply only where a field is bound,
    and the fill reads no allocation.
13. **A flag that changes nothing is not always broken.** `--rt-optimisation
    =off` gave the same picture as Quality on the Deck and Bluff cameras
    because Quality's thinning starts at 300 m; on a near camera the two
    presets are the same frame.

### The owner's two documents, read in the original against this design

The design above was written from the revamp plan's reading of the
owner's two documents (`dynamic_ray_budgeting_realtime_rt.md`, 25
sections; `RT_Multi_Light_Shadow_Optimization_Debugging.md`, 35 sections;
both in the owner's Downloads, neither in the repo). Read in full on
2026-09-03, they agree with this design in shape and differ from it in
four places worth stating, because two of the differences are the
design's deliberate choices and two are omissions.

- **Temporal variance as an importance input.** The first document's
  core signal (§5, §11, §15: coverage x variance x complexity) is what
  this design refuses in §3 and §4.2 ("inputs only"), on the 2026-08-28
  evidence that variance -- an *output* of the rays -- made the loop
  breathe at a hertz. The document itself asks for "temporal hysteresis"
  and "quantized sample counts" (§15), which is exactly the dead band and
  dwell of §4.2; so the honest position is: variance may return as an
  input **only** behind that quantisation, slowly, as one factor among the
  inputs -- and M2 should try it as an arm, with the sixty-second
  transition count as the judge, rather than rule it out by memory.
- **Spatial smoothing of the importance map** (§15's last bullet) is not
  in §4.2. A tile whose importance jumps against its neighbours pops on
  its own; a 3x3 smoothing of the tile weights before quantisation is
  cheap and belongs in M2.
- **The light-count sweep** (second document, Debug Pass B: 147 / 75 /
  32 / 16 / 0 casting lights against RT time) is not in this design's
  protocol; the fixed-budget sweep (Experiment 4) is, as M4's pre-check.
  Both belong in M0's calibration: the light sweep says whether the
  frame is light-count-bound or ray-bound, and it costs one hour with
  `--shadow-rays=off` and a mobility edit.
- **Debug views and the HUD** (second document §24, §31): this design
  lists rays, lights, allocation and reservoirs. Add temporal confidence
  (the validity lane) and importance, and put "lights per pixel avg /
  max" and "temporal confidence %" beside the rays line in the HUD and
  the benchmark report. All cheap once the counters exist.

Where the documents and the design already agree, in plain words: a
bounded per-pixel budget instead of per-light rules (both documents'
central recommendation, this design's §4.3.3 B); stochastic selection by
importance with the unbiased weight, never the top-N brightest (second
document §14 warns against top-N for the same reason the Share shape
lost here); separate budgets by ray type with lending between them
(first document §16, §4.1 here); importance from screen coverage,
material and light count rather than camera distance (both); profiling
before optimising, a rays-per-pixel view first (both, and M0); terminate
on first hit, instance masks, proxy geometry for far rays (second
document §21-23; the masks are in since 7cx, the proxy BLAS is the
RENDERING-REVAMP candidate). The second document's "shadow maps for
static lights" (§20, §32 priority 8) is met differently: since 7cx a
static lamp beyond its half-bake radius is *baked*, light and shadow, in
the field -- cheaper than a map and exact where the field's cells are
fine enough. And the first document's §20-25 -- baked GI does not mean
no runtime rays -- is this scene exactly: the field carries the bounce
and the far lamps, and the water's mirror, refraction and shadow rays
are the runtime cost that remains.

One caution the documents share and this scene should hear: their
targets (RT shadows at 3-4 ms of a 16.7 ms frame) assume 1080p on a
desktop part. Here, after the split and Hybrid Full Bake, Headland is
59 ms at 2560x1440 on a laptop 5070 Ti and 43 of it is the water pass.
The budget WR-16 has to hold is the water's.

### M0, prepared

In order, each a commit: (1) `RHIDevice::ReadBuffer` and its ring; (2) the
counter SSBO at set 0 binding 21 and the four increments; (3) the
"rays: shadow N, water N, GI N, AO N" line, "lights per pixel avg / max"
and "temporal confidence %" in the benchmark report and the HUD; (4) the
calibration runs on the Hybrid scene -- `--hit-lights=off`,
`--shadow-rays=off`, `--ray-rate=1|2`, and the light-count sweep (147 /
75 / 32 / 16 / 0 casting lamps) -- into `RenderSettings::RayCost[]` and
the handoff; (5) the validity lane in `o_Moments.w`, written and not yet
read; (6) `--debug-view=rays|lights|confidence|importance`; (7)
`bench_night.py --label wr16-before` and the flicker protocol on the
Hybrid scene. Accept as §7 M0 says, against the Hybrid baseline: counters
within 5% of the isolation runs, bit-identical picture.

### Decisions for the owner before M0 starts

- **A.** The Hybrid scene (lamps at 2 m, the three new boxes) is the WR-16
  baseline. Recommended.
- **B.** M1's scope is the compact record *and* the per-cell live-lamp
  bit (finding 2). Recommended; the bit is a CPU loop over cells the grid
  already builds.
- **C.** `RayBudget` Off / Absolute / Fractional stays as the controller's
  mode dial, as §6 says, rather than folding into the RT optimisation
  preset. Recommended, so a measurement can pin a target in ms.
