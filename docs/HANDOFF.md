# RageV — handoff

**Read this first.** Updated 2026-09-05.

## 2026-09-05: WR-16 S3's first move is built, measured and PASSES

**Start here: S5**, the water's mirror and refraction rays at half resolution.
S3's remaining half -- the eight lanes and the controller -- is NOT built; what
is built is the stability half, and it passes its acceptance.

### The allocator is still, and `budget.Advance()` is back

Three mechanisms in `tile_budget.rvshader`, in the order they mattered:

1. **The demand is averaged across frames** (`RayBudgetImportanceSmoothing`,
   default 1/16, `--tile-smooth`). This did nearly all the work.
2. **A dead band of 0.75 rays** -- the design's own number, untouched, and only
   wide enough *because* the averaging removed the wobble first.
3. **A dwell of 8 frames** between changes.

Then `budget.Advance()` was restored, on the fourth attempt in this engine's
history, with the shape underneath it fixed first.

| sixty seconds, bar 0.01 changes/tile/s | AO | GI |
|---|---|---|
| bridge Headland, as it shipped | 2.4421 | -- |
| bridge Headland, now | **0.0082** | **0.0082** |
| showroom, now | **0.0000** | **0.0000** |

The picture is unchanged: no pixel differs from the undamped allocator by more
than 6 levels on any of the three cameras at 1440p, worst mean absolute
difference 0.004 levels.

### The cause, and the one flag that proves it

**With `--aa=none` the allocator reads 0.0000, zero tiles, on both scenes.**
Every bit of the restlessness was TAA's sub-pixel jitter walking silhouettes --
cables, tower edges -- across `importance_tiles`'s fixed 4x4 sample lattice, so
a tile's coverage changes while nothing moves. The jitter is periodic
(`TemporalJitterPhase: 8`), which is why averaging cancels it and why no
threshold could. **Reach for `--aa=none` first when the allocator looks
restless.**

### Three approaches measured and rejected -- do not retry them cold

- **A wider dead band** reaches the bar but needs 4 rays on the bridge and 8 in
  the showroom. Scene-dependent, because the wobble scales with what a tile
  asks for: the tell that it was the wrong knob.
- **Shifting the importance lattice by the jitter** -- the obvious fix, with an
  in-engine precedent in `rtao_compute`'s `JitterUv`. **Both signs made it
  worse** (0.216 off, 0.276 at +1, 0.404 at -1). The lattice is *point*-sampled,
  so a sub-pixel shift either lands on the same texel or jumps a whole one; it
  cannot express a fraction of a pixel and only adds a second thing that jumps.
- **A coverage-age counter.** Worked, then became unnecessary -- an averaged
  demand never collapses to zero for a tile covered five frames in eight.

### The test was measuring fiction, twice

`tools/scripts/tile_transitions.py` had never measured the allocator.

1. **It graded the animated picture.** The debug view draws its map over the
   tone-mapped frame at a fifth of that frame's brightness -- 51 levels a
   channel -- so moving water and flashing beacons counted as allocation
   changes. Caught by arithmetic: with the dwell at 31, which caps a tile at
   1.94 changes a second, Headland still read **21.13**. Now `--debug-view-mix`
   (0.2 for a person, 0 for a machine) plus a guard that refuses a capture
   whose tiles are not flat inside.
2. **Its warm-up counted as restlessness**, so the *better* setting scored worse
   (0.0796 against 0.0129). Now `--settle`, default 240.

It was also blind to the GI lane; `--debug-view=importance-gi` and `--lane` fix
that, and both lanes are graded.

### The 0.01 bar has no derivation

`RAY-BUDGET-DESIGN.md` asserts it twice and never justifies it; its only
concrete job is settling decision G. It counts events, not their size and not
their visibility. **`check_glint_flicker.py` is the test that measures what a
viewer sees and it has NOT been run on S3** -- the design lists it in S3's
acceptance alongside the still test. That gap is open.

### Three defects found by evaluation and fixed here

- **The dwell cap was enforced at one of two entry points.** `--tile-dwell`
  clamps to 31; `RenderSettings::RayBudgetDwell` did not, and the shader's
  counter saturates at 31 -- so a project asking for more froze every tile for
  ever. Capped in the shader now, where the cap is defined.
- **The tile ray ceiling was stated twice and disagreed** (16 at the call, 24 in
  the debug ramp), so the map's top third was unreachable. One constant now.
- **S4's entire win shipped switched off.** `LightSampling` defaulted to 0 and
  existed only as a command-line flag -- no settings field, no project key -- so
  the editor, a packaged build and `bench_night.py` all drew the *unsampled*
  frame while this handoff reported the sampled one. It has a `RenderSettings`
  home now, with the flag overriding for a run. **The default is still 0:
  turning it on changes every project's picture and is the owner's call.**

### Left owing

- The eight-lane widening and the controller -- the rest of S3.
- The flicker protocol on S3.
- **Nine more evaluation findings, none of them verified** -- the agents that
  would have challenged them all died on a session limit. See
  `wf_eb9ab3b7-4d5`'s journal. The largest: S4b's "the reuse loses" verdict may
  have been measured with a *biased* combine (the merge divides by the plain
  confidence sum rather than the unbiased Z count), which loses on every camera
  whether or not the tilt explanation holds.

## 2026-09-04, second session: WR-16 S4 is COMPLETE and pushed

**Start here: WR-16 S3, and its first move is already scoped.** Everything
below is done, measured, committed and on the remote at `04287c4`. The only
uncommitted file is `SampleProject/assets/scenes/goldengatedemo.rage.meta`,
whose SourceHash the runtime rewrites on load -- left alone deliberately, as
before, and it is the owner's call.

### What S4 finished with

Against the preset the project actually ships (RT optimisation **Quality**,
`SampleProject.rvproject:20`), at 1440p:

| camera | shipped | now | error (TAA) | blink (TAA) |
|---|---|---|---|---|
| Headland | 58.9 ms | **34.4** | 0.56% | 2.00% (preset 2.73) |
| Pier | 47.9 ms | **29.9** | 3.47% | 2.01% (preset 3.84) |
| Glitter | 42.9 ms | **27.6** | 0.96% | 2.68% (preset 3.22) |

36 to 42% off the frame, and the flicker is below the shipped preset *and*
below everything-traced on all three cameras. What it spends is per-frame
accuracy, worst on Pier.

Four commits: `ea78c13` (S4b measured, the reuse off, two defects), `3d023d6`
(S4c), `3e92900` (the world grid, and S4c's memory set to 4,2), `04287c4` (the
sixty-second still test).

### The four defects, because each is a rule rather than a fix

1. **A shadow ray was sent to lamps that cast none.** The lit shader never
   traces a light whose `kind` is zero -- `CastShadows` false in the scene, and
   **70 of this scene's 191 lights are authored that way on purpose** (48 tower
   floods, the post tops, nav, marine and beacons: WR-4's fill fixtures). They
   sit behind the deck, so tracing them blacked out the brightest glitter, 13.7
   where truth reads 192.2. **A pass that re-implements the lit shader's
   lighting must re-implement its exemptions, not only its arithmetic.**
2. **The sea covers itself and the prepass kept the wrong crossing.** At 400 m
   on Glitter three to five water fragments land on one pixel; with a shared
   depth tested-but-not-written the survivor was whichever rasterised last. One
   probed pixel stood 29 m from the water the draw shows, on the same view ray.
   It has a target and depth of its own now.
3. **The ray counters were blind to the lamp passes.** `FlushRayCounters` sits
   at the end of the lit shader's main, which `RV_TRACE_ONLY` compiles out. Any
   ray figure for this path from before today undercounts -- 6.6 M read where
   13.6 M were cast.
4. **The graph's default clear colour is `{0,0,0,1}`** and the position
   attachment's `w` is the "a wave was drawn here" mask, so taking the default
   marked every screen pixel as water and put both lamp passes over the whole
   frame: the choose pass 3.8 -> 7.6 ms. `SetClearColor` with a zero alpha.

### Two settings that are decisions, not defaults

- **The reuse of the *choice* is off and should stay off.** Both halves lose on
  every camera: Pier's error 1.32% with neither, 8.55% with the history alone,
  4.51% with the neighbours alone. The sea retilts every patch every frame, so
  a choice made last frame -- or twelve pixels away -- was made for a surface
  that has since turned, and on water the score is nearly all specular.
- **S4c's memory is PROVISIONAL at 4,2.** The owner picked 16,4 from a
  flicker-versus-error table, then watched the water move and said it looked
  blurry and "less like water". It did: 16,4 costs a third of the sea's fine
  detail. **When this is tuned again, show detail, flicker AND error together,
  and show stills** -- the error percentage cannot say "the water stopped
  looking like water" and detail-per-pixel can. Pier's curve, detail / blink /
  error: off 4.74 / 8.29 / 1.32, **4,2 3.39 / 4.18 / 1.95**, 8,2 3.37 / 4.14 /
  1.98, 16,4 2.72 / 2.01 / 3.47; traced reference 4.29, preset 4.30.

### S3: the first move, and the trap it walks into

**`budget.Advance()` has been restored and reverted three times** and made the
picture worse every time. The note where it lives says why and what to do
first: a damped step feeding a `floor(x + 0.5)` quantiser is the classic slow
limit cycle -- this engine has met one, breathing at about a hertz -- so
**fix the shape, then restore the line**. The shape is §4.2's dead band (up at
`n + 0.75`, down at `n + 0.25`) plus an 8-frame dwell, replacing the +/-1-a-frame
easing in `tile_budget.rvshader`.

**Its judge now exists**: `tools/scripts/tile_transitions.py`, the
sixty-second still test the design named and nobody had written. It reads the
allocator's own tile map through `--debug-view=importance`, samples the middle
of every tile each frame and counts changes; the bar is under 0.01 per tile per
second. **Headland today reads 21.84**, with 87.8% of tiles changing and the
busiest changing on every frame -- exactly what a budget with no history to damp
it should read, and the number the dead band has to move.

### Left owing, none of it blocking

- **S4's own acceptance test does not exist.** The design signs S4 off on a
  moving-camera ghosting test; every protocol here is still-camera and
  `--camera` is a single fixed pose. A reconstruction stage is precisely what a
  still camera cannot judge.
- **A blur the owner reports has been in the water "for a while"**, seen on a
  low close view of the tower base -- not one of the three benchmark cameras and
  not S4c, since with accumulation off the water carries *more* high-frequency
  content than the reference. **Ask for that camera before chasing it.**
- **Glitter keeps a one-sided residual and it is structural**: the passes shade
  one surface per pixel where the draw blends three to five, so the nearest
  crossing's light is applied to the whole stack -- about 10% too bright.
- **The streaks pulse on the temporal jitter's fixed 8-step cycle**
  (`TemporalJitterPhase: 8`). The real fix is WR-13, specular AA. **The owner's
  order is: finish WR-16 -- S3 then S5 -- and only then WR-13. They were asked
  twice about bringing it forward and said no; do not re-ask.**

### The instrument that settled everything

Five hypotheses were tested and killed by measurement before the first defect
was found. What ended it was making **both paths answer the same question at
the same pixel in the same frame**: the water draw reported 59 lamps reaching
it and 7 visible, and tracing those same lamps in the same shader at the same
instant returned 0 -- visibility no ray produced is a branch that never traced.
`--lamp-probe` now carries the water draw's own position, its lamp counts, both
`waterSpecular` sums and a walk over every light in the scene, for that reason.
Reach for it early rather than after five arguments.

New flags this session: `--water-lamp-history`, `--water-lamp-neighbours`,
`--water-lamp-tuning`, `--water-lamp-accumulate`, `--water-lamp-memory`,
`--water-lamp-clamp`, `--world-grid`.

---

**2026-09-03: the static / moving split, Hybrid Full Bake, the bigger
boxes and the packed atlas are built, tested, applied, committed and
pushed** -- `f9aa11e` (the engine, every scene marked, the docs),
`c4d1edf` (the bridge's lamps and boxes, its bake, the generator),
`b9b54a7` and `8c89119` (handoff; WR-16 as one document), all pushed on
the owner's word. **Late on 2026-09-03 three documents were corrected and
left uncommitted when the owner cleared context**: `RAY-BUDGET-DESIGN.md`,
`NEXT.md` and this file -- the correction is the stretched flicker rule
(below, "WR-16, prepared and decided"). `git status` will show the three;
commit them first. The evening's start-here is the first section below
(the numbers), the morning's the second (the flag), and the fourth
session's entry after them is still the map of everything else.

**2026-09-04: WR-16 steps S0, S1 and S2 are built, measured and
committed** -- the section right below this paragraph. **The gate passed**
(S1): four importance-chosen lamps a pixel, with a target that knows the
specular, reproduce the water's lamp shadows under TAA at or under the
shipped preset's bar on Headland and Glitter, with no lamp group
structurally missing at eight; Pier needs the reuse S4 adds. **S2 took 20
/ 28 / 32% off Headland, Pier and Glitter, bit-identical** -- most of it
from static pixels on screen that had been reading and dropping a hundred
lamps a frame. **Where to start cold: WR-16 step S4** as Part IV's "S1,
measured" defines it -- a few lamps per pixel chosen by a target that
includes the specular, shaded alone, the choice reused across frames and
neighbours, the result reconstructed -- **plus the world-space lamp grid
decided after S2** (cubes over the scene with each cube's lamps and live
sublist, built once from the lamps' positions, the candidate source for
every hit and pixel whether or not the point is in the picture; the S4
row in Part IV's re-sequenced table). Then S3, then S5.

**2026-09-04, evening: S4 was laid out for the owner and the sizing run
taken before any of it is built** (RAY-BUDGET-DESIGN Part IV, "S4's shape,
laid out and decided" and "S4's sizing, measured"; commit `0fa126d`).
Four calls: start with the measurement; build *both* candidate targets and
let the matrix pick; take the deferred step for the water's direct light
now rather than temporal reuse alone; solo. The new measurement flag is
**`--shade-lights=N`** -- every lamp's sixteen-byte record still walked,
only the first N read in full and shaded, ray included, on screen and at
hits; picture wrong on purpose, time honest -- and `water_cost_split.py`
runs the arms interleaved. **The whole lamp lever at K = 4 is 25.5 / 19.7 /
17.0 ms, 44 to 47% of the frame on Headland, Pier and Glitter, all of it in
the water pass.** Four rays a pixel cost under two milliseconds; K = 8 costs
two more than K = 4 (so K is a setting); the light walk at traced hits is
down to 2.8 / 4.8 / 1.6 ms, which is the world-space grid's own payoff.
**And the ranking turned over: on the water the rays are the bigger half
now, not the shading** -- S0's "light-bound more than ray-bound" was true
of the frame before S2 and must not be carried forward.

**2026-09-04, night: S4b is built -- the sea's lamps are chosen and shaded
in passes of their own, and the picture lands** (RAY-BUDGET-DESIGN Part IV,
"S4b, built and measured"). Four passes now sit between the backdrop copy
and the water draw: a surface prepass that draws the sea a second time to
write its normal, colour and position; a pass that scores every lamp in the
cell and keeps four by weighted reservoir sampling, folding in last frame's
four; a pass that reads three neighbours' choices, shades the survivors and
traces their rays; and the water draw, which adds the two pictures they
produce and walks the sun alone. Both fullscreen passes borrow the lit
shader's set 0 under RV_TRACE_ONLY the way rtgi_trace does, so the rays and
the cutout test are the lit shader's own.

**Measured on Headland against every lamp traced: 1.95% of the frame over
six levels with no AA, 0.78% under TAA**; the lamp-lit water reads 28.7
against truth's 29.7 and the dark water 10.3 against 10.3. **Start here:**
S4c, the reconstruction -- the direct light has no history of its own yet --
and the flicker protocol, which has not been run on any of this. The
neighbour ring (three taps at twelve pixels) and the confidence cap (20) are
both untuned assertions, and no frame time has been taken since the passes
landed: S4a's table is the last honest one.

**Four defects were paid for on the way and all four are worth reading
before touching this** (the same Part IV section): a descriptor set that was
never committed, so the passes read a scene block of zeros and shaded
nothing; choices read through a sampler instead of by texel, which handed
every pixel its neighbour's lamp; a neighbour test in metres, which on a sea
seen edge on rejected every neighbour; and -- the one the owner spotted from
the shape alone -- both passes reading the sea at `v_UV`, which on Vulkan is
upside down, so the whole glitter band was mirrored about the middle of the
screen. **The tool that found three of them is `--lamp-probe=x,y`**, which
prints one water pixel's entire arithmetic beside its own full walk. And one
wrong fix is recorded there too, because it nearly stuck: capping the
*merged* confidence looked like a large win only because it cancelled the
light the mirroring was losing.

**2026-09-04, evening: S4a, the sampler, is built and measured**
(ENGINE-NOTES 7da; RAY-BUDGET-DESIGN Part IV "S4a, the sampler,
measured"). `--light-sampling=K[,term|irradiance]`: a live surface whose
cell list is longer than twice the budget scores every candidate, keeps K
by weighted reservoir sampling, and shades and traces only those K, on
S1's unbiased estimate. **At K = 4 the frame is 43.4 / 34.5 / 35.2 ms
against the shipped preset's 54.2 / 46.3 / 39.8** (the project ships at RT
optimisation **Quality** -- `SampleProject.rvproject:20` -- not Off, as
some of these documents still say), with 0.29 / 1.29 / 0.07% of the frame
over six levels under TAA against everything traced: better than the
preset on Glitter, not yet on Headland and Pier, which is what S4b's reuse
is for. **The target took three landings and is the lesson**: the water's
roughness is an RMS slope and its lobe an anisotropic Beckmann in a streak
frame, so GGX scored the glitter's own lamps as dim; and at grazing angles
the Fresnel is most of the term. The score is the unshadowed term's
luminance now, and it reproduces S1's numbers. The gate is the irradiance
field's weight, never the Static flag -- the sea is marked static and no
volume covers the bay. **Next: S4b**, the reuse -- the choice kept across
frames and neighbours, in its own stage rather than the forward pass (the
owner's decision), then S4c's reconstruction, then the world-space lamp
grid.

**The session ended here on 2026-09-04, paused at the owner's request
(credit limit) with the context cleared.** Eleven commits on `main`, none
pushed; the scene meta file is the one uncommitted change (the runtime
rewrote its source hash on load; the owner decides). The pre-S2 runtime
is kept at `build/bin/Release/RageVRuntime_preS2` for A/B. **Before
building S4, lay out its shape for the owner and discuss it** -- it is
the most complex step, and the owner asked for the reasoning first, every
time. Two side-view drawings that finally explained S2's leftover cost
to the owner are `build/pier_hits2.svg` and `build/two_grids.svg`; when
explaining, start from the physical picture (stand on the pier; the water
bends your line of sight; the sand you see is below the frame) and only
then the engine consequence, one step at a time, and never lead with an
engine noun.

## Start here -- 2026-09-04: WR-16 S0, the rays counted where they are cast

**On `main`, three commits: `7b6b658` (the RHI's buffer readback and
fill), `c0d70c3` (the counters, the views, the flags, the calibration
script), and the docs commit after them. Not pushed -- pushing is the
owner's call every time.** Design record ENGINE-NOTES 7cy; the step's
state and numbers in RAY-BUDGET-DESIGN Part IV, "S0, built". Solo, no
agents, by the owner's standing rule.

### What exists now, in plain words

- **Every ray the frame casts is counted, by kind, in the shader that
  casts it**, and the count comes back a frame or two late the way the GPU
  timings do -- never a stall. The benchmark report has three new lines
  (`rays per frame: ...`, `lights per fragment: ...`, `temporal
  confidence: ...`), the runtime's F1 overlay and the editor's Statistics
  panel show the same, and `bench_night.py` parses them into its JSON and
  three extra table columns.
- **`--debug-view=rays|lights|confidence|importance`** replaces the
  picture with a heat map of one number per pixel: rays cast, lights
  walked, whether the temporal resolve reused history, the ray budget's
  per-tile allocation. Vulkan only. The rays view is what the owner's
  multi-light document asked for first; look at it before believing a
  mean.
- **`--casting-lights=N`**: only the first N positional lights keep a
  shadow ray. A measurement flag for the light-count sweep; the picture is
  wrong on purpose.
- **`RenderSettings::RayCost*`**: what a ray of each kind costs in
  milliseconds per million, measured by `tools/scripts/
  ray_cost_calibration.py` and stored in the project. Never learned online.
- **The validity lane**: the TAA resolve writes `o_Moments.w` as one where
  the pixel reused its history, zero where not. Written and counted, read
  by nothing yet -- S3's consumers will.
- **`RHIDevice::ReadBuffer`** and **`RHICommandList::FillBuffer`** for
  anyone who needs a number back from the GPU every frame; scenetest
  drives real frames through the readback on both backends.

### The number that mattered, and the day it cost

The first build put Headland at **74 ms against 59** -- the instrument was
a quarter of the frame. Bisected by staging three variants of the shader
under one binary (increments only, flush only, neither): the flush cost
13 ms. Packing the eight counter words into two registers won 4 ms.
Spreading the atomics over sixty-four addresses won nothing. **The cause
was the depth test**: a fragment shader with a memory side effect is no
longer culled by depth before it runs, so every fragment the depth
prepass had already rejected was shaded again -- rays, light loop and all
-- and counted. `layout(early_fragment_tests) in;` in the families that
neither discard nor write depth (opaque and water; not the alpha-cutout
family, whose discard must not leave depth behind) put the test back.
Interleaved on/off at 2560x1440, Headland: **62.3 / 61.6 / 62.6 / 61.6 ms
-- 0.85 ms, 1.4%**, picture bit-identical (max difference 0 over
1600x900). And the honest count is lower than the first one: **31.8 M
rays a frame on Headland, not 41.8 M** -- the ten million were rays the
counting had itself caused. Rule for every later step: **an instrument's
first number is the instrument's own cost until an interleaved A/B says
otherwise.**

Headland at 1440p, RT optimisation Quality, as it stands: shadow 31.8 M,
water 3.9 M, opaque mirror 0.4 M, AO 4.4 M, GI 0 (baked) rays a frame;
9.3 rays and 78.5 lights per lit fragment (max 147, the busiest cluster);
87.8 lights per traced hit over 1.9 M hits. Under TAA the temporal
confidence reads 100% on a still camera.

### What the calibration found, and what the owner's third document changes

The calibration table and its reading are in RAY-BUDGET-DESIGN Part IV,
"S0, built". The one finding to carry in your head: **the frame is
light-bound more than ray-bound.** With every lamp's shadow ray removed
(`--casting-lights=0`), Headland keeps 43.7 of 62.2 ms, Pier 41.1 of 51.4,
Glitter 37.7 of 49.3 -- the lamps' rays are 19 / 13 / 12 ms, the hit walk
another 8 / 12 / 8, and what remains is the shading of 77-125 lamps per
fragment before any ray. A ray budget alone caps its own gain near a
quarter of the frame; the larger lever is lights evaluated per fragment,
which is why S4 becomes RIS with a *cheap* target that shades only the
survivors (Part III's "unshadowed term as the target" would have kept the
whole walk). Also: one cost per ray kind is a two-to-four-fold model
between cameras (short rays under the deck, long rays across the bay), so
S3's controller reads pass timers as the truth and uses counts to split
them.

The same afternoon the owner brought a third document -- a spatiotemporal
reconstruction design (history validation, confidence, variance-guided
0-8 rays a pixel, spatial reconstruction, half-res tracing) -- with the
brief *judge it by the gain, not the work, and take what is useful.* It is
judged item by item against the numbers in Part IV, "The third source,
judged": taken are its history validation and allocation order (S3), its
shadow reconstruction as S4's denoiser, its half-resolution water rays as
a new S5, and its moving-camera benchmark and overhead bar as acceptance
lines; not taken are its GI/AO/reflection reconstruction (under 2.5% of
this frame) and zero-ray reuse on the water's mirror (the waves move).
**S1 now has two arms**: the raw floor, and the same per-pixel budget with
temporal accumulation behind it.

**The owner's decisions from that discussion (Part IV, F-K):** the direct
light keeps its own temporal history under every AA mode, its failures
without TAA a trade-off; two temporal filters in series are fine;
**variance comes in** as an importance input, quantised behind the dead
band and dwell, with the sixty-second still test as the judge; **the
order is S1, S2, S4, S3, S5** (S4 is where the milliseconds are, S3 spends
them well); S1's second arm runs under TAA; the bridge is the test, GI/AO
reconstruction stays off; and S4 is done the proper way -- a few lamps
per pixel by a cheap target, shaded alone, the choice reused across frames
and neighbours, the result reconstructed -- spending the shading lever and
the ray lever together. The re-sequenced table is at the end of Part IV.

### S1, the pre-check, measured the same afternoon

`--shadow-budget=K[,full]` (commit `554589b`) and
`tools/scripts/shadow_budget_precheck.py`; the tables and the verdict are
in Part IV, "S1, measured". The short form: **K = 4 with a target that
includes the specular.** Under TAA the frame over 6 levels against the
everything-traced truth is 0.29% on Headland and 0.07% on Glitter (the
shipped preset: 0.00 and 0.42), 1.14% on Pier under the deck (Quality
0.04; K = 8 gives 0.50, and S4's reuse of the choice is what closes it).
The water's signed mean at K = 8 is within 0.03 levels everywhere and the
diffs are speckle, not shape: no lamp group structurally missing. **The
cheap irradiance target is unusable on the water** -- the glitter lamps'
specular is a hundred times their irradiance, the rare draws come back as
spikes, the tonemapper clips them, and the water reads darker at every K.
Raw, under no AA and MSAA, the fixed per-pixel choice holds still at the
truth's own flicker figure; under TAA the walking choice adds 0.9 / 3.8 /
0.2 points of blink at K = 4, which is the residual S4's own history and
spatial pass exist to remove. Frame time with every lamp still shaded and
the reservoirs spilling: 7 / 13 / 3% under Quality, 20 / 16 / 18% under
the truth. Two things learned building it: a golden-ratio walk along the
lamp index correlates the reservoir's draws and biases it (hash them); and
the auto exposure meters a noisier frame differently, so a signed mean at
low K is a global offset before it is a bias -- read the diff image.

### S2, the cheap hit walk, built and measured the same day

ENGINE-NOTES 7cz; Part IV, "S2, measured". A sixteen-byte cull record per
light (set 0 binding 23) and a second, live-only list per cluster cell
beside the full one, in the full list's order; a static surface deep
inside the irradiance field walks the sublist and drops the rest by the
record, on screen and at traced hits. **Bit-identical** against the pre-S2
build (max difference 0 on six stills, Off and Quality, three cameras).
Interleaved at 1440p: **Headland 67.1 to 54.0 ms, Pier 58.2 to 42.2,
Glitter 55.5 to 37.8** -- two thirds of it the opaque pass, where every
static pixel had been reading and dropping 77 to 125 lamps a frame. Lamps
walked per fragment 77 / 116 / 125 to 35 / 50 / 63. What is left of the
hit walk is about 5 ms on Headland and Pier. **Not** the deck volumes'
edge band, as first written and withdrawn (the boxes are 28 m tall at 5 m
cells): Pier's refraction hits inside the cluster grid walk 1.5 lamps, but
half of them land on seabed below the bottom edge of the frame, outside
the grid, and a hit with no cell walks every light's sixteen-byte record.
The remedy is the world-space lamp grid, now written into S4's definition
(Part IV, the re-sequenced table): cubes over the scene with the lamps
that reach each, built once, serving every hit and every sampler whether
the point is in the picture or not. A guard band of extra rows was
considered and dropped (it would still miss mirror hits behind the
camera). Nothing in the scene was changed.
Two forms tried and dropped on the way: a per-cell count alone (never zero
under the deck) and a reordered single list (it would have changed which
thinned lamp WR-17's borrow takes its visibility from). The pre-S2 runtime
is kept beside the new one as `build/bin/Release/RageVRuntime_preS2` for
any further A/B this session. **The eight cameras after S2**
(`bench_night.py --label after-s2`, `build/bench/after-s2.json`; the
table is in Part IV, "S2, measured"): **272 to 282 ms over the eight
against 339 this morning**, every camera faster -- Headland 51.5 / 55.2,
Deck 8.7, Profile 26.9, Bluff 36.6, Pier 43.6, Cliff 18.2, Glitter 38.4,
Lime Point 48.5 (pass A) -- with the rays columns unchanged to the decimal.
Taken on a GPU that had run all day, so pessimistic against the morning's
table; the interleaved pairs are the per-camera truth.

**Work paused here on 2026-09-04 at the owner's request** (a credit limit).
Everything is committed on `main`, nothing pushed; the scene meta file is
the one uncommitted change, on purpose. The next step is S4, and it is the
most complex one: lay its shape out for the owner and discuss it before
building.

### The baseline and the flicker counts

`bench_night.py --label wr16-before`, 2560x1440, 300 frames, two passes
(`build/bench/wr16-before.json`). **Measure every later step against this,
on the same day or interleaved**: the GPU read about 8% slower today than
yesterday evening on the same frame, and the counters are 1.4% of it.

| camera | A: ms / fps | B: ms / fps | water ms | scene ms | busiest cluster | rays M a frame (shadow / water) | rays per fragment | lights per fragment |
|---|---|---|---|---|---|---|---|---|
| Headland | 62.7 / 16.0 | 63.7 / 15.7 | 47.1 | 14.6 | 146 | 40.4 (31.8 / 3.9) | 9.3 | 76.8 |
| Deck | 10.6 / 94.1 | 10.7 / 93.6 | 1.6 | 6.3 | 113 | 7.1 (2.2 / 0.1) | 3.2 | 48.7 |
| Profile | 31.7 / 31.6 | 32.1 / 31.1 | 24.9 | 5.6 | 102 | 4.1 (2.3 / 0.6) | 4.4 | 27.0 |
| Bluff | 44.1 / 22.7 | 44.8 / 22.3 | 28.5 | 14.1 | 135 | 29.8 (22.4 / 1.3) | 7.6 | 63.3 |
| Pier | 53.8 / 18.6 | 53.9 / 18.6 | 35.7 | 15.9 | 178 | 74.0 (65.6 / 3.3) | 18.2 | 115.5 |
| Cliff | 26.0 / 38.5 | 26.4 / 37.8 | 11.2 | 13.1 | 111 | 33.2 (25.2 / 1.4) | 8.8 | 99.5 |
| Glitter | 50.0 / 20.0 | 50.2 / 19.9 | 33.8 | 14.4 | 151 | 29.6 (22.0 / 4.0) | 7.4 | 125.4 |
| Lime Point | 57.5 / 17.4 | 57.5 / 17.4 | 39.3 | 16.0 | 142 | 79.3 (71.3 / 4.0) | 18.9 | 111.1 |

339 ms over the eight. Profile casts 4 M rays and spends 25 ms on the
water; Pier casts 74 M and spends 36: the water's time is not its rays.

The flicker protocol on Headland (1600x900, clock pinned, 16 frames from
240; `build/flicker/wr16-before/`): **no AA 0.006%, MSAA 4x 0.007%** -- the
metric's floor -- **TAA 2.73%** (worst swing 203 levels), which is the
scene's standing TAA residual (3.59% as shipped on 2026-09-02, the owner's
dial), not a change from S0: nothing reads the validity lane yet.

### Traps this session paid for

- **`cast` is a reserved word in GLSL** (as `coherent` was in 7cw). The
  occlusion shader failed to compile and took the whole post chain with it
  in scenetest -- twenty-nine failures from one identifier.
- **The readback ring holds its source buffers alive**, so it drops them
  before the deferred queue's final flush or the buffers outlive the
  device; and the copy that reads a buffer wants a barrier *after* it,
  into later submissions, or the next frame's fill of the same buffer is a
  write-after-read the validation layer names every frame.
- **`--hit-lights=off` moves the shadow count**, not only the walk: the
  sun's shadow ray at a hit lives inside the walk it skips. The counters
  were right; the first check was wrong.
- **`--rt-reflections=off` takes the water's mirror ray with the steel's**
  (one define), so the reflection arm runs on the Deck and is corrected.
- **The GPU read ~2.5 ms slower today than yesterday evening** on the same
  Headland frame with counting off (61.6 against 58-59). Compare only
  interleaved, same session; yesterday's tables are not today's baseline.
- **To bisect a shader cheaply**, copy the variant straight into
  `build/bin/Release/RageVRuntime/assets/shaders/` and restore the source
  copy after; no build, no staging swap under a running benchmark.
- `goldengatedemo.rage.meta`'s SourceHash is rewritten by the runtime on
  load. Left uncommitted on purpose; the owner should say whether the
  scene or the meta is the stale one.

**On `main` now, and pushed.** The 2026-09-02 session worked on `main` and
pushed four commits to it, ending at **`bd7f813`**. The paragraph that used
to sit here said `main` was still at `02406b2` and that everything lived on
`showroom2-and-the-card-look` — that was true until this session and is not
any more. It is the second time this line has gone stale while being read as
current, so: **check `git branch --show-current` and `git log -1`, do not
trust this sentence.**

(Pushes to `main` report "Changes must be made through a pull request" and
succeed anyway — the branch is protected and the owner's account bypasses
it. Expected, not an error, but worth knowing before it alarms someone.)

## Start here — 2026-09-03, evening: the bridge's lamps baked, Hybrid Full Bake, bigger boxes, WR-16 prepared

**On `main`, committed as `f9aa11e` and `c4d1edf` and pushed** (the
morning's split plus everything below). Read the
morning's entry after this one for the static / moving flag itself. This
entry is the evening: the owner asked for every bridge lamp baked and the
gain measured, and the measurement found three defects on the way to a
number that holds.

### The number, first

The bridge scene as it now stands (176 lamps **Hybrid Full Bake at 2 m**,
eight irradiance volumes, the packed atlas) against the scene as shipped
this morning, three interleaved pairs of `bench_night.py`, 2560x1440,
fields loaded and verified in every log:

| camera | shipped ms | now ms | saved | fps before, after |
|---|---|---|---|---|
| Headland | 94.0 | 59.0 | 37% | 10.6, 16.9 |
| Deck | 13.8 | 10.2 | 26% | 72.3, 98.0 |
| Profile | 43.2 | 29.7 | 31% | 23.1, 33.6 |
| Bluff | 79.6 | 41.0 | 48% | 12.6, 24.4 |
| Pier | 95.3 | 50.2 | 47% | 10.5, 19.9 |
| Cliff | 43.0 | 24.8 | 42% | 23.3, 40.4 |
| Glitter | 77.8 | 46.3 | 41% | 12.8, 21.6 |
| Lime Point | 94.8 | 54.1 | 43% | 10.5, 18.5 |

542 → 315 ms over the eight, 42% off. The water pass drops too (Headland
60 → 43 ms) because the water's reflection and refraction hits now read
the field on the bridge, the sea floor and the shores. **Hybrid at 2 m
against pure Full bake** (same boxes): 303 → 313 ms, +3%, per camera +2%
to +5% -- the price of keeping the lamp heads live. Diffs, Hybrid against
live: Profile 0.10 levels mean, Headland 0.48, Lime Point 0.69, Glitter
0.70, Cliff 0.74, Pier 1.72, Deck 4.59, Bluff 9.60. Hybrid against Full:
0.02–0.38 on the four cameras shot. Images in `build/static_split/
bridge_hybrid/`; the crops `crop_deck_post_half_vs_hybrid.png` and
`bluff_half_over_hybrid.png` are the two the owner was shown.

### What the picture said each time, and what got fixed

1. **All lamps Full bake read as half the frame on every camera. It was a
   bug.** "The live loops skip a fully baked light wherever a field is
   bound" tested whether the scene had a volume, not whether the pixel
   was inside one; the beach, the headland and most of the towers were
   outside the deck's boxes and simply lost their lamp light. Fast because
   wrong. Fixed with a weight, not a switch: `IrradianceFieldWeight` is
   the field's own edge fade (0 outside every volume), and a fully baked
   lamp is lit live by one minus it. Bluff went from 63% of pixels over 6
   levels to 3.8%. The honest Full bake gain was then **4%** -- the boxes
   covered the deck only, and the frame is the water and the shores.
2. **The bloom loss the owner saw is the 5 m field's resolution.** The
   post under a lamp head peaks at 208 live and 167 baked; the bloom
   follows the hotspot. The owner's answer: **Hybrid Full Bake** -- a
   fourth mobility, half baked within `HybridRadius` of the lamp, fully
   baked beyond, a slider on the light, 2 m by their call; and **bigger
   boxes** so the shores and the sea floor are covered. ENGINE-NOTES 7cx
   has both. The radius is the owner's trade-off dial; they said the diff
   matters less now that it exists.
3. **The bigger boxes made the field 754 MB a file.** The atlas stacked
   every volume along z padded to the widest one's cross-section; the bay
   box was 180 texels wide beside 9-wide deck boxes. `CreateAtlas` packs
   volumes as 3D boxes now (first fit by z, y, x; deterministic; order
   kept), every region carries an x/y corner (`IrradianceBox` row 5, the
   fill's spare rotation lanes), and the same boxes with the bay at 32 m
   are 90 MB a file, 7 minutes to bake instead of 17.
4. **Then every benchmark pass silently fell back to realtime**, because
   `BakedLighting::Read` refused the 90 MB file as "not credible" (a
   64 MB corruption cap). Six passes measured a scene with most of its
   lamp light missing and read as a win. The cap is a gigabyte; the chain
   scripts now fail any benchmark whose log carries the fallback line.
5. **scenetest crashed inside `Sample.dll`** after `Light` gained
   `HybridRadius`: the native script module was stale against the header.
   `cmake --build SampleProject/bin/module --config Release` -- the same
   trap as 2026-08-25, now in the memory note with today's date.

### The scene as it stands

`GoldenGateDemo.rage`: 176 lamps `Mobility: Hybrid Full Bake`,
`HybridRadius: 2` (the 14 flashing lights stay Realtime, the sun Half
bake); three volumes added -- `Bay irradiance` (0,-40,0) [1400,90,1400]
at 32 m, `Headland irradiance` (500,110,-1025) [250,60,375] at 12 m,
`South shore irradiance` (60,75,1200) [200,45,200] at 12 m -- beside the
five it had. Its bake is `baked/GoldenGateDemo/field_3217cd40954c3f78_*`
(90 MB a file, untracked). The committed `field_6ef83b34f7c820f0_*` files
are the shipped scene's Half bake with the *old* boxes and no longer match
anything; delete them when committing. The variant files and the chain
scripts are in the session's scratchpad, not the repo.

**Known quality cost of the boxes, owner-accepted for now:** Bluff's
beach reads flat and the pier's shadow on the sand is gone (9.6 levels
mean) because 32 m cells cannot hold a 5 m shadow; a 5 m box over each
shore a camera stands on (a few tens of thousands of cells each) is the
fix if it is ever wanted.

### WR-16, prepared and decided

`docs/RAY-BUDGET-DESIGN.md` is now **one document in four parts** (owner's
call): the owner's two source documents verbatim (Parts I and II, from
their Downloads -- they were never in the repo), the engine design of
2026-09-02 (Part III), and Part IV -- the solo review against the code and
today's frame (thirteen findings), the owner's decisions and the sequence.
**Decided:** the Hybrid bridge is the baseline; a fully baked lamp costs
nothing at a hit (compact record and per-cell live-lamp bit); the
controller targets the total ray-traced time under the RT optimisation
preset and the Off / Absolute / Fractional dial is retired; the water's
rays are inside the budget; and the order is **counters (S0) → the one-day
fixed-budget pre-check on the water (S1) → the cheap hit walk (S2) → the
budget, allocator then controller, always (S3) → ReSTIR as the shadow
spender (S4)**, built unless the pre-check finds lamp groups whose shadows
are structurally missing at 8 rays, and judged once built by §7 M4's tests
-- the flicker protocol under all three AA modes among them. **The owner
corrected a stretched rule tonight:** their constraint is "the flickering
fix should work no matter what AA technique is picked", not "no
accumulation without TAA"; ReSTIR keeps its own history in every AA mode as
the GI denoiser does. The reasoning for the order: the budget can spread
rays and lower counts, but only sampling takes a water pixel from 146
shadow rays to a few; the pre-check sizes that and catches bias in a day.
**The owner's own reading of S1, which is the one to keep:** it is the gate
-- if the one-day test does not work, the ReSTIR route goes; nothing
subtler than that. Start at S0.

**How the evening's conversation went, for whoever explains WR-16 next:**
four rounds of confusion came from my framing "allocator-and-controller
route versus ReSTIR route" -- they are not alternatives; the budget is
always built and ReSTIR is one way of spending its shadow lane -- and one
more from stretching the owner's flicker rule into "no accumulation
without TAA". Say: the budget always; ReSTIR if the gate passes; judged
by the flicker protocol under every AA like everything else.

### Traps this evening paid for

- **A frame-time gain is not a result until a picture agrees**, twice
  over (items 1 and 4). The shot checks caught both; the benchmark alone
  caught neither.
- **A corruption guard sized to yesterday's files is a silent quality
  cliff tomorrow.** Read the runtime log for "not credible" and "no bake
  matches" after any bake grows.
- **Detached measurement chains**: PowerShell `Start-Process` for anything
  over ten minutes, a monitor on the log; and a monitor that counts lines
  breaks when the log is truncated under it -- re-arm after the reset.
- **Killing a process by a command-line substring kills the monitors whose
  command line contains the substring.** Match the executable name.
- Everything in the morning entry's list still applies.

## Start here — 2026-09-03: the static / moving split is in

**On `main`, committed in `f9aa11e` with the evening's work and pushed.**
The fourth session's entry below is still the map of everything else; this one is the
task it named first — *"A Static / Moving flag per object"* — built, tested
and applied to every scene. Design record: **ENGINE-NOTES 7cx**. The plain
version:

- **Every mesh and every terrain has a `Static` checkbox, off by default**
  (owner's call: a thing moves until an author says otherwise). Water has no
  checkbox and is always live; a skinned mesh is never static whatever the
  box says.
- **The bake sees static objects only.** They are its occluders and its
  bounce surfaces. A moving object is in neither, so the car's shadow is no
  longer painted onto the floor it drives off. (Mechanically: every instance
  in the ray structure carries a static or a moving mask bit, and the solve's
  rays see the static bit alone.)
- **Static surfaces read fully baked lamps from the field and skip them
  live**, as before. **Moving surfaces take those lamps live**, shadows
  included, and do not read their stored light. Same rule at reflection and
  refraction hits, so the car in the water's mirror is lit live too.
- **A moving object still shadows the static floor from a fully baked lamp**
  (Vulkan only): a static pixel under a lamp that has a moving object inside
  its range traces one ray against the moving objects alone and subtracts
  the light it finds blocked. Only such lamps pay — on the bridge, the few
  over the car, never the 191. OpenGL has no rays, so there the shadow is
  absent, which is what Unity's baked lights ship.

### What the owner decided, in order

1. Existing scenes are **marked now** rather than the engine growing a
   "nothing marked means everything" rule. `tools/scripts/mark_static.py`
   did it: 231,421 blocks in 112 scenes, one `Static: true` line
   inserted under each block, nothing else touched, idempotent. Its rule —
   moving when the entity or an ancestor has a script, an animator or a
   non-Static rigid body, or is under an excluded root (the showroom's car)
   — left exactly the things that move: the fox and its label, the falling
   crates, the graph fixture's mover, the Knockdown barrel and crates, the
   car. The generators write the flag too (`make_demo_scene.Scene.mesh`
   defaults `static=True`; the fox and the billboard say `False`).
2. Terrain gets the checkbox like a mesh (first "always static", then "can
   be static or non static").

### Verified

- Release build, no new warnings. **scenetest 2465 green on Vulkan, 2418
  on OpenGL, exit 0**, with new claims: `Static` is a checkbox on both
  components, round-trips by name, loads absent as false, the draw list
  carries it, a skinned mesh never counts, and `MarkMovingLights` marks
  exactly the lights whose range reaches something moving.
- **The picture**, `tools/scripts/check_static_split.py`: three copies of
  the showroom baked from scratch and shot from the showroom camera —
  `live` (lamps Half bake, everything live), `split` (lamps Full bake, car
  moving), `carbaked` (lamps Full bake, car static too). Diff images are in
  `build/static_split/` (x4 gain).

| diff | mean (levels) | 99th percentile | pixels over 6 | over 24 |
|---|---|---|---|---|
| split vs live | 0.35 | 5.0 | 0.70% | 0.00% |
| carbaked vs split | 0.51 | 15.0 | 2.11% | 0.50% |
| carbaked vs live | 0.81 | 15.0 | 2.79% | 0.49% |

Mean brightness (0-255, after tonemapping) of four regions of the 1280x720
frame:

| region | live | split | carbaked |
|---|---|---|---|
| floor under the car | 2.54 | 2.11 | 2.99 |
| floor beside the car | 1.97 | 1.83 | 1.83 |
| back wall | 3.15 | 3.19 | 3.25 |
| car body | 33.49 | 33.46 | 27.58 |

**What the pictures say.** `split` against `live` is the claim and it
holds: the diff is black but for the floor's grout lines (the field's cells
against per-pixel lamps) and thin outlines in the car's own mirror
reflections (the reflected room is read from the field at static hits). The
car body is identical to a hundredth of a level -- lit live in both -- and
the floor under the car is shadowed in both. `carbaked` against `split`
lights up the whole car (read from 0.25 m cells instead of lit live) and the
floor around it: with the car in the bake its shadow comes out *weaker* than
live (+0.45 levels under the car), because a field that coarse smears a
shadow it cannot resolve. The split's floor reads a little darker than live
everywhere (-0.14 beside the car, -0.43 under it): the field's direct light
on this floor sits a few percent under the live lamps, and the subtraction
is exact relative to the field, so it inherits that gap. Thinning was ruled
out -- identical numbers with `--rt-optimisation=off`; Quality's thinning
never engages at showroom distances. At two levels out of 255 none of this
is visible; it is written down so nobody measures it twice.

### What this changes for the demo scenes — READ BEFORE THE NEXT BAKE

- **Every bake on disk predates the rule and still loads.** The lighting
  hash names the file and the flag is not in it (deliberately: per-frame
  state and geometry never renamed a bake). So the showroom's, the camp's
  and the bridge's fields are stale in one specific way: they hold the car,
  the fox and whatever else moves as occluders and bounce. Re-bake each
  (`--bake=force --benchmark=N`, or the editor's Bake) when its lighting is
  next touched; nothing is wrong until then except the ghost of a baked
  shadow under anything that has moved.
- **Auto Fit now fits the static meshes**, not every non-skinned mesh. A
  volume whose scene marks nothing static falls back to its authored box.
- **The bridge:** which lamps go Full bake is now purely a lighting call —
  the car shot works either way, because the car is moving and takes every
  lamp live. The lamps that go Full bake are the ones whose ~40 ms hit walk
  (RAY-BUDGET-DESIGN §1) disappears at static hits.

### Traps this session paid for

- **GLSL `out` parameters are undefined on entry.** Pre-zeroing the caller's
  variable buys nothing if the callee returns early; `storedDirect` is
  re-zeroed on the miss path explicitly.
- **Two lanes were reused rather than added**, both documented at both
  definitions: `InstanceData.Indices.w` is the previous bones' base on the
  skinned pipeline and the static flag everywhere else (the two never meet);
  `GpuLight.Shadow.y` is the map slot under maps and "a moving object is in
  range" under rays. The probe varying became `vec2 v_Instance` (probe,
  static) so no varying slot was added either — the water pipeline was at 14
  of the guaranteed 15.
- **A regex edit that consumes a trailing `\r` and puts back `\n`** leaves a
  CRLF file with LF lines in it and nothing shows it until git does. The
  exact-text helper in the session's scratchpad converts both sides to the
  file's own line ending first; four generators had to be re-normalised.
- **Several of this repo's files are CRLF with a BOM** (every `.rage`),
  several are LF (`pbr_fragment.glsl`, `Scene.h`, the manual). Check before
  editing by hand.
- **The Bash tool's command length has a ceiling.** A 15 KB heredoc was cut
  mid-way and reported as an unmatched quote; the edit went into a script
  file and ran from there.

## Start here — 2026-09-02, END OF THE FOURTH SESSION: where everything stands

**Sixteen local commits on `main` from `b35c82e` to `ee1de90`, none pushed,
tree clean, scenetest green on both backends at every commit that touched
code.** Read this section, then `docs/RENDERING-REVAMP.md` items WR-10,
WR-17, WR-18 and the "Frame-time candidates" section, then
`docs/RAY-BUDGET-DESIGN.md` (which is WR-16). The owner was confused by
item labels all day: **explain in plain words first, labels second, and
never split one instruction into two labels without saying so.**

### What is in, in the order it landed

1. **The baseline** (`0579352`): `tools/scripts/bench_night.py`, eight
   cameras at 2560x1440. Headland 114 ms. The table is in the agenda
   section below.
2. **WR-17, the owner's far-lamp rule** (`b3faaa9`, `ef90573`): shadow rays
   thin with a lamp's distance, a skipped ray borrows its neighbour's
   visibility, a floor keeps far lamps sampled, a light cutoff. Shipped as
   one setting, `RenderSettings::RtOptimisation`: Off / Quality / Balanced
   / Performance, numbers in `RayOptimisationPresetFor`. The falloff matrix
   (`tools/scripts/shadow_ray_matrix.py`, four arms) is written up in WR-17.
3. **WR-18, the water's rays** (`5b7b153`): one mirror and refraction ray
   per 2x2 quad on the water (quad broadcast, exact normal, no pass), and
   the refraction ray stops where the water has absorbed all but a 256th.
   In the presets.
4. **The scene ships at Quality** (`8ccee6a`, `0892972`): 7 to 32 percent
   off on the eight cameras, nothing visible changed (table below).
5. **The measurement that reorders the roadmap**: `--hit-lights=off` shows
   the 191-light walk at every reflection and refraction hit is ~40 ms of
   the frame, a memory cost. **WR-10 option A** (the hit walks its cluster
   cell's list) is in (`224b273`) and is exact and a null: the cost is the
   lamps genuinely in range. What is left for WR-10 with every lamp live
   is in the plan (compact records; a per-cell summary rebuilt each frame).
6. **Light mobility** (`224b273`): `LightMobility` Realtime / Half bake /
   Full bake replaces `IsBaked` everywhere; old scenes load; the hash mixes
   it only for Full bake. The scene's lamps are Half bake.
7. **The irradiance field is spherical harmonics with separate direct
   light** (`56a171f`, `f6dd7ae`): `IrradianceVolume::kTiles = 19` —
   tiles 0-8 bounce (+ sky in alpha), 9-17 the fully baked lights' direct
   light, 18 visibility and alive. `FieldBasis`, `DerivedLamp`,
   `VolumeIrradiance` with five outputs. Every field re-baked; a stored
   field with another tile count fails the stamp and is solved again.
8. **Full bake works** (`f6dd7ae`, `ee1de90`): a fully baked light's direct
   light is read at the pixel unconditionally and at hits, and its
   highlight is derived from the stored direction. Showroom, all sixteen
   lights fully baked against live: 0.99 overall, walls 1.04, car 0.86,
   floor 1.17, 2.4% of pixels moved over 6 levels (29% this morning).
9. **WR-16 is the combined ray budget** (`60b0a9f`, `c4e2569`):
   `docs/RAY-BUDGET-DESIGN.md`, controller + allocator + consumers + ReSTIR
   inside; five milestones with acceptance tests; not built.

### What the owner wants next, in their words

- **A Static / Moving flag per object.** "Everything except the car gets
  marked as static." Static surfaces read the fully baked lights from the
  field and skip them live; a moving object is lit live by the same lights
  and casts a live shadow from the few nearest (the engine has 4+4 shadow
  slots). The water stays live for the streak. This is the shadowmask /
  stationary split; the design document §4 describes it. Then decide which
  bridge lamps go Full bake — the owner's call, the car shot needs live
  lamps on the car and the water.
- After that: the eight-camera after-table per preset, the flicker protocol
  for the shipped preset under all three AA modes, then the design
  document's milestones (M0 instrumentation, M1 WR-10, M2 allocator, M3
  controller, M4 ReSTIR DI with its one-day pre-check first).

### Loose ends, small

- `SampleProject/assets/baked/irradiance_field/` still holds the old
  ten-tile bake: its solve did not finish in 8000 frames twice (the second
  attempt was killed by mistake). It falls back to the runtime solve with
  one log line; re-bake with `--bake=force --benchmark=30000`.
- Several fixtures baked under new hash names; their old files sit beside
  them (they were stale before today). Harmless, worth a cleanup commit.
- The `showroom_fullbake` test scene is deleted; recreate with
  `fullbake_test.py` in the session's scratchpad if the comparison is
  needed again (a copy of `showroom.rage` with `Mobility: Full bake` on
  its sixteen lights).

### Traps this session paid for

- `coherent` is a reserved word in GLSL (a memory qualifier).
- Stopping a background `bash` loop with TaskStop does not stop the
  runtime it spawned; a build then fails on the locked executable. Kill
  `RageVRuntime.exe` and the loop's `bash.exe` before building.
- Never build while a bake or benchmark chain runs: the build restages
  every executable's shaders under it.
- Three hypotheses for Full bake's darkness were built and measured before
  the consuming code was read; all three were beside the point. Read the
  path the light takes to the pixel first.
- `-- -m`, not `-- /m`; absolute paths for detached builds; `--frame-time`
  pinned with `0.000001`.

## Next session's agenda, set by the owner 2026-09-02

**First, a proper benchmark of the night scene at 2560x1440 from several
angles. Then WR-8, WR-10 and WR-16**, the three items that buy frame time
(the lamp row as line lights, a light grid for ray hits, ReSTIR for the
lamp shadows). Nothing else before those.

### The benchmark, so it can start cold

`--benchmark=N` runs N frames after a warm-up and prints `FrameProfiler`'s
by-pass report to stdout on exit; `tools/scripts/bench_scale.py` shows the
launch shape and the patterns it parses. A screenshot or a benchmark needs
`--render-defaults=off` to measure the project as configured (TAA, RT on).
This laptop's GPU drifts about a millisecond over a session, so **interleave
A/B runs** (A, B, A, B) rather than trusting one run each, and keep the
editor closed. The window is clamped to the display; 2560x1440 fits.

The eight scene cameras as `--camera=` flags (focus x,y,z, distance, yaw,
pitch; distance 0.01 puts the eye on the focus, the angles are the scene's
Euler rotations negated and in degrees):

| camera | flag |
|---|---|
| Headland (hero, ViewRank 0) | `--camera=500,89.47,-1100,0.01,-157.08,8.88` |
| Deck | `--camera=0,76.4,950,0.01,0,0` |
| Profile | `--camera=2400,150,0,0.01,-90,0` |
| Bluff | `--camera=45,49.94,1150,0.01,-5.04,8.88` |
| Pier | `--camera=70,4.5,705,0.01,-46.98,-2.86` |
| Cliff | `--camera=316,55,-810,0.01,-33.52,3.44` |
| Glitter | `--camera=500,2.5,180,0.01,-90,-1.146` |
| Lime Point | `--camera=185,5.5,-590,0.01,-143.24,1.146` |

So, from `build/bin/Release/RageVRuntime`:

    RageVRuntime.exe --project=<SampleProject> --scene=scenes/GoldenGateDemo.rage
        --rhi=vulkan --render-defaults=off --vsync=off --width=2560 --height=1440
        --benchmark=300 --camera=<row above>

**Or all sixteen runs in one command** -- `tools/scripts/bench_night.py
--label <name>` runs the eight cameras twice (passes A and B), keeps every
report under `build/bench/`, and prints the owner's table; `--table
<json>` reprints one. Note that the pose lands on the scene's *primary*
camera (Headland, 55 degrees), so every row renders at that field of view
rather than its own: Bluff's 42 becomes 55. Consistent before and after,
which is what the comparison needs.

### The baseline, before WR-8 (2026-09-02, fourth session)

Two passes, a millisecond apart everywhere, so the drift did not bite.
Water = the `scene/Transparent` pass, scene = `scene/Scene`, both GPU ms
from pass B; the busiest cluster is how many of the 191 lights one cell
holds -- the number of lights a water pixel walks and traces a soft shadow
ray to.

| camera | A: ms / fps | B: ms / fps | water ms | scene ms | busiest cluster |
|---|---|---|---|---|---|
| Headland | 114.4 / 8.7 | 115.2 / 8.7 | 75.0 | 38.1 | 146 |
| Deck | 14.8 / 67.4 | 14.7 / 67.9 | 2.1 | 9.9 | 113 |
| Profile | 52.6 / 19.0 | 52.5 / 19.0 | 40.6 | 10.3 | 102 |
| Bluff | 89.7 / 11.1 | 89.8 / 11.1 | 48.6 | 39.0 | 135 |
| Pier | 100.4 / 10.0 | 100.6 / 9.9 | 59.8 | 38.6 | 178 |
| Cliff | 47.0 / 21.3 | 47.0 / 21.3 | 19.4 | 25.5 | 111 |
| Glitter | 108.7 / 9.2 | 108.6 / 9.2 | 66.3 | 40.2 | 151 |
| Lime Point | 131.7 / 7.6 | 132.4 / 7.6 | 82.7 | 47.5 | 142 |

### The after-table, the scene at Quality (2026-09-02, end of the fourth session)

The owner picked Quality (`RtOptimisation: Quality` in the project): shadow
rays thin from 300 to 600 m with half kept at 450, and the refraction ray
stops where the water has absorbed all but a 256th. Same protocol
(`bench_night.py --label after-quality`, the project's own settings, no
flags).

| camera | before ms / fps | after ms / fps | change |
|---|---|---|---|
| Headland | 114.4 / 8.7 | 89.7 / 11.1 | −22% |
| Deck | 14.8 / 67.4 | 13.8 / 72.6 | −7% |
| Profile | 52.6 / 19.0 | 43.5 / 23.0 | −17% |
| Bluff | 89.7 / 11.1 | 76.6 / 13.1 | −15% |
| Pier | 100.4 / 10.0 | 89.6 / 11.2 | −11% |
| Cliff | 47.0 / 21.3 | 41.1 / 24.3 | −13% |
| Glitter | 108.7 / 9.2 | 73.7 / 13.6 | −32% |
| Lime Point | 131.7 / 7.6 | 90.1 / 11.1 | −32% |

Nothing the eye finds on any of them by the matrix's bar (the diff rows
are in RENDERING-REVAMP WR-17 and WR-18). The cameras lowest to the water
gain most: the refraction ray's reach is what they were paying for.

Every frame is GPU bound and every pass but the two lit ones is under a
millisecond. The water pass is the frame wherever the sea fills it, and
the opaque pass is 38-48 ms on the cameras that see the towers and deck;
the Deck camera, looking along the roadway with almost no sea in view, is
the only one near 60 fps. The 50.4 ms at 1600x900 from the third session
scales to this by pixel count, which says the cost is per fragment: the
lit loop over up to 178 clustered lights, each with a shadow ray.

**What the owner wants from it is the headline pair per camera: frame
time in milliseconds and frames per second**, before WR-8/10/16 and again
after each, so the improvement is a number. The by-pass shares (shadows,
reflections, refraction) are worth keeping beside them because they say
which item moved what, but the table the owner reads is eight rows of
ms / fps, before and after. The last such measurement (1600x900, Headland)
is in the first 2026-09-02 section below: 50.4 ms.

## Start here — 2026-09-02 (fourth session): the baseline, and WR-17 built and measured

**Committed locally at the end of the session, not pushed.** The project's
render settings are untouched, so the scene renders exactly as before
until the owner picks a setting from WR-17's menu.

### What happened, in order

1. **The baseline the agenda asked for** is in the agenda section above and
   in `build/bench/before-wr8.json`: eight cameras, 2560x1440, two passes a
   millisecond apart. Headland 114 ms, Lime Point 132, Deck 15. The water
   pass is the frame wherever the sea is in view; a water pixel walks up
   to 178 lights and traces a soft shadow ray to each.
2. **WR-8 was set aside by the owner before it started** ("why line
   lights? just use less rays for light sources beyond a certain distance …
   the fall off pattern needs to be tested"), and **WR-17** was built in its
   place: `RenderSettings::ShadowRayFade` (Off / Hard / Linear / SmoothStep /
   Log / Share) with `ShadowRayFadeStart`, `ShadowRayFadeEnd`,
   `ShadowRayFloor`, `ShadowRayShare`, and `LightCutoffDistance` — one value
   each, driving every light, per the owner ("no per-light dials"). Flags:
   `--shadow-rays=<shape>,<start>,<end>[,<floor>][,lit]` and
   `--light-cutoff=<m>`. The scene block grew one row (`ShadowRayFade`,
   appended, three mirrors). Off is bit-identical to before.
3. **The falloff matrix** (`tools/scripts/shadow_ray_matrix.py`, results in
   `build/shadow_rays/` with every still and amplified diff) ran four arms —
   skipped rays counted lit; borrowing the mean of the traced far rays; a
   floor of far rays always traced; borrowing the nearest traced
   neighbour. The tables and the reading are in RENDERING-REVAMP WR-17.

### What the matrix found (read the item for the numbers)

- **"The farther the lamp, the less it contributes" holds per lamp and
  fails per group.** Counting skipped rays lit brightened the water under
  the deck by 70 levels: a hundred negligible lamps behind one slab are not
  negligible.
- **A skipped ray must borrow a traced one's visibility, and that only
  works while the far group is still sampled.** Linear 300/600 — half the
  far rays kept at 450 m — meets the bar (Headland 0.18–0.32% of pixels
  moved, none over 6 levels) at −13%. Thin to one in eight and no borrowing
  rule (mean or neighbour) reconstructs the per-lamp visibility field
  under the bridge; the residual is 3.5% of the frame either way.
- **The shape never mattered.** Linear, smoothstep and log are within
  noise at both distance pairs. The end distance and the floor are the
  dial.
- **Far rays are the expensive rays**: keeping one in eight of them kept
  half their time.
- **The cutoff is a look, not an error**: it dims the far glitter band
  (450 m: −32% on Headland, but a third of the Glitter camera's streak
  band goes).

### The owner's decision, and what is next

The owner read the menu and asked for a preset instead of dials: **RT
optimisation** in Render Settings under the ray-tracing switch — Off /
Quality (linear 300/600) / Balanced (linear 150/300, floor 1/8) /
Performance (cutoff 300 m). `RenderSettings::RtOptimisation`, the table in
`RayOptimisationPresetFor`, one registry field; the detailed fields are
gone from the settings, the flags stay for measurement, and
`--rt-optimisation=<level>` benchmarks a preset without editing the
project. Each level was checked against its matrix row on Headland before
the commit (numbers at the end of WR-17). **The project ships at Off.**

1. Pick the level the scene ships with (the owner's eye, in the editor:
   the row is under ray tracing).
2. The after-table per level: `python tools/scripts/bench_night.py --label
   after-quality --extra "--rt-optimisation=quality"`, and the same for
   balanced and performance.
3. The flicker protocol for the shipped level under no AA / MSAA / TAA on
   Headland and Pier (`check_glint_flicker.py`). The dither is fixed per
   pixel and walks only under TAA by construction, which is the claim to
   measure.
4. Then the ray side in the order WR-16, `docs/RAY-BUDGET-DESIGN.md`, gives
   (the owner renamed the combined system WR-16; ReSTIR is inside it): M0
   instrumentation, **M1 WR-10 option A** (the hit walk is ~40 ms, a third
   of the frame — measured with `--hit-lights=off`), M2 the allocator, M3
   the controller, M4 ReSTIR DI behind the shadow interface, M5 the
   direct-light field if M1's leftover asks for it. WR-18 (the water's
   rays) is in and in the presets; Quality is 90 / 90 / 73 ms.

### Traps this session paid for

- **`cmake --build … -- /m` under Git Bash becomes `M:/`** and MSBuild
  fails with MSB1008 — after `--clean-first` has already deleted every
  binary. Use `-- -m`.
- **The shell's working directory drifts** after a `cd` inside a chained
  command; a background `cmake --build build` then fails with "could not
  load cache". Absolute paths for anything that runs detached.
- **A background command's final `grep -c` that finds nothing exits 1**
  and the task reports failure for a build that succeeded.
- **The matrix is only valid while nothing rebuilds**: every executable's
  staged shader is what runs, so a build mid-matrix swaps the shader under
  the benchmark. Edit sources freely, build only between arms.

## Start here — 2026-09-02 (third session): the flicker, measured per AA mode, half fixed

**Committed at the end of this session together with the second session's
tree, and pushed.** The owner's brief: fix the bridge's flicker (WR-13's symptom), and *the fix has to work
no matter which AA is picked*. The full account, tables and corrections are
in `docs/RENDERING-REVAMP.md`'s WR-13 item — **the WR-13 paragraphs in the
section below this one are superseded by it** (their "frozen" numbers were
taken on animated water; see the trap).

### What is settled by measurement

- **Under no AA and MSAA, 100% of the blinking was WR-15's soft-shadow
  sample** being re-rolled every frame (pixel ^ light ^ frame). Forcing the
  ray hard took the deck from 14.5% (no AA) / 23.8% (MSAA) to 0.0%.
- **Under TAA it is the jitter plus a resolve that rejects the history on
  sub-pixel geometry** — ribs, ropes, pickets, lamp standards. Jitter off:
  tower and cables 0.0%. All ray tracing off: still 40%. RTAO, reflections
  and refraction moved the count by zero parked.

### What shipped (AA-independent)

`TraceShadowSoftFrom` in `pbr_fragment.glsl`: interleaved gradient noise per
pixel shifting an R2 point set per light, frame-advanced only while
`u_Scene.Jitter` is non-zero. No AA / MSAA: 5.8% of the frame → 0.01%, the
floor. Stills unchanged. Both backends' scenetest green (see the log line
at the end of this section).

### What is built but is the owner's call (TAA's own dial)

`taa_resolve.rvshader` + `TemporalResolve` + `FrameGraphBuilder.cpp`: the
TAA and AO histories carry a second attachment with per-pixel frames and
luminance moments (the GI denoiser's pattern); the clamp never squeezes
below the pixel's own temporal sigma; `kStillFeedback = 0.98` for pixels
that moved less than a texel (the warm-up cap `kMaxFrames` had to go from 16
to 64 or the floor 1/16 silently held the feedback at 0.94 — three arms
identical to the pixel before that was found); and `--msaa=N` is honoured
under TAA as a measurement flag. Tower 34.5 → 6.0%, cables 1.9 → 0.7%, full
frame 3.59 → 0.78%. Under no AA / MSAA these change nothing (0.01% before and
after). **Judge it in motion before keeping it**: the still-feedback boost
trades response to a genuine lighting change on a static pixel; the moments
floor can ghost briefly where a pixel that used to flicker is then covered
by something else.

### The lamp sprites (WR-5, first half) — built, owner picking the intensity

`Renderer/LightGlow.{h,cpp}` + `light_glow.rvshader`, hooked at the end of
`Renderer3D::EndScene`, viewport handed over by the frame graph around the
scene draw (zero outside it, so probe faces and cascades draw none). Every
positional light with a `SourceRadius` becomes a fixed-size soft disc
carrying I/(d²Ω); fades out where the lens is bigger than the disc; the
flare is a share of the energy in a wider halo with rays, drawn as a ring
round a big lens. Six profile dials (`LightGlow*`, `LightFlare*`) in
`PostSettings`, the registry, the bridge profile and the generator.
**Lessons paid for:** narrow floods must not glow from the side (scaled by
cone width now); the flare must not fade with the disc; the physical
intensity (1.0) reads far too bright next to the eye-tuned lens boxes, and
the owner asked for it toned well down — candidates at 0.05/0.02 were shot
and **0.02, flare 0.2, no rays was chosen**; profile and generator carry it.

### What still flickers, and what it is

The deck band under TAA (~23%) is **not the lamps any more** (their heads
are absent from the blink mask): it is the lamp posts, railing pickets and
truss webs, a tenth of a pixel wide at a kilometre, lit bright, present one
frame in eight. `--msaa=4` under TAA barely moves it (22.5%). That is
content thinner than a pixel, and **the fix is built: a distance fade in
the material.** `MaterialParams::Macro.zw` = `FadeStart`/`FadeEnd` (metres,
serialised under those names); the masked lit shader discards past the end
and on a gradient-noise dither between (walked per frame only under TAA);
the ray hit test (`RayCandidateIsThere`) honours it too, or the water's
mirror rays keep reflecting members that are not drawn (the owner's "random
red dots in the water"). **Scope after the owner's verdict:** only members
under 0.2 m — pickets, lamp arms, brackets, band flanges — in `bridge_thin`
(300–450 m, `Blend: Masked`, `AlphaCutoff: 0`), one new entity, mesh and
`.rmat`. The first cut also faded the suspender ropes, rails, posts, lamp
shafts and the truss webs, and the ropes vanishing from the headland was
rejected on sight; those are back in `bridge_orange` and keep their
flicker. Scene regenerated with `--sky night --seabed bay --hero headland
--cinematic`; lights identical as a multiset to before; `cinematic.cube.meta`
restored after (the generator's known side effect). To tune the distances,
edit `bridge_thin.rmat` or the `fade` column in `write_materials`. The
round-trip guard fails against HEAD only because HEAD predates WR-4's
lights.

**Where the numbers stand at hand-back** (Headland, pinned clock, blink =
swing ≥ 6 with reversals; tower / deck / cables / water-near / full frame):
start of the session under TAA 34.5 / 32.6 / 1.9 / 26 / 3.59%; now under
TAA 28.0 / 31.8 / 1.8 / 17 / 2.77% — the lamps no longer pop and the water
is crisp (its 17% is sparkle, which is the picture), while the ropes, posts
and webs the owner chose to keep flicker as sub-pixel geometry always has.
Under no AA and MSAA: 5.8% → 0.00%. scenetest green on both backends.

### The TAA gate, and the water — read this before touching the resolve

The still-pixel feedback (0.98) first counted anything under a texel of
motion as still, and **the water paid**: its crests move a few thousandths
of a texel a frame from the headland, so fifty frames of glitter averaged
into a smear with horizontal bands (the owner's "streaks more blurry, weird
bands"). A thirtieth of a texel still caught it; **so did 0.003** — near the
horizon the water moves less on screen than float error and its sparkle
changes every frame regardless, and the owner saw the bands a second time.
**The boost is off** (`kStillFeedback = 0`, the constant and the reason
kept in the shader). The moments floor stays, gated on the same stillness
so it never touches the water, and the moments forget in 16 frames
(`kMomentFrames`) so a flashing beacon cannot leave a tail. The sub-pixel
members the boost was reached for are what the material fade removes.

### Traps paid for this session

- **`--frame-time=0` is the wall clock.** `Application.cpp` falls back to
  the measured step when the flag is zero. Pin with `--frame-time=0.000001`.
  Every "frozen" number in the second session was animated water and
  flashing beacons; `check_glint_flicker.py`'s docstring now says so.
- **Measure per region.** The whole-frame count is dominated by the water,
  and the water was moving. Tower / deck / cables / water regions told the
  story the single number hid.
- **A per-frame stochastic term must be gated on a temporal filter actually
  running**, and the jitter is the signal (no history, no jitter).

## Start here — 2026-09-02 (second session): WR-3, WR-4, WR-15

**Committed at the end of the third session (see above). Work was halted by
the owner part-way through the water-streak investigation.** Two owner
decisions are already made and applied; one item (WR-15) is built and
unjudged; one (WR-13) is measured but not started.

### Settled by the owner this session

- **Exposure is metered now, and the numbers are the owner's picks.**
  `AutoExposure: true`, `AutoExposureKey: 0.015`, `Exposure: 1.0` (compensation
  once metering is on), `BloomThreshold: 5`. All four in the generator's night
  preset, so a regenerate keeps them.

  The chain that got there, because it is the most useful thing in this
  section: WR-4 made the lamps physical, ~15x brighter than the values chosen
  by eye, and **everything downstream had been tuned against the dim ones.**
  `Exposure: 2.4` then put a correct road (14 lux, asphalt at 0.070
  reflectance, 0.31 cd/m^2) at **222 of 255 before bloom** — the deck read as
  blown out from every camera standing on it. And **`bloom_prefilter` carries
  no exposure term at all**, so it thresholds the raw HDR: lowering the stop
  changes not one pixel of what feeds the glow, and the threshold has to move
  with it. It marks "where the picture is bright", which is 1/Exposure.
  Finally a *fixed* stop cannot serve both framings — the deck is lit two
  decades above the water and sky in the wide shot, so 0.21 was right on the
  road and nearly black from the headland. Metering solves that, but only at a
  night key: 0.17 is the 18%-grey daylight convention and it turned the bridge
  into late afternoon, which is the note that had metering switched off
  originally.

  **The deck is on the auto-exposure floor at this key** — identical at 0.015
  and 0.02 because `AutoExposureMin: 0.15` clamps it. It is being limited
  rather than metered; that floor is the dial if it ever needs to go darker.


- **The 128-texel sky cube stays** (`kGradientCubeSize`, `Skybox.cpp`).
- **The bridge's fog is authored and reduced**: layer e-folding 165 m rather
  than 9 m, density 0.0008, `FogSkyAffect: 1`, `FogSkyOcclusion: 0.3`. Both in
  the generator and the committed profile.
- **WR-15's mechanism**: the shadow ray samples `SourceRadius`.

### WR-15 — built, not yet judged

`TraceShadowSoftFrom` in `pbr_fragment.glsl`. **One ray per light, aimed at a
point on the source disc instead of its centre**, seeded from pixel ^ light ^
`GlobalIllumination.y` (the free-running frame counter the traced bounce
already hashes, so frame N is reproducible). Falls through to the hard path at
radius zero and for directional lights, so every light authored before
`SourceRadius` traces the ray it always did.

- **The picket fence is gone.** Before: 120 lamps each cast a crisp
  point-source shadow of the deck railing onto the water, so the glitter came
  out as rectangles with the streak missing inside them — the owner reported
  it twice, from two cameras, before it was found.
- **A static seed is not enough.** The first landing seeded from pixel and
  light only, to keep screenshots reproducible, and came back as heavy
  salt-and-pepper: one sample per light needs *something* to integrate it, and
  TAA is what integrates it. The frame term is what made it resolve.
- **Cost +2.3 ms** (52.7 → 55.0 ms at 1600×900). No extra rays; it is the
  hash, the frame build, a sin/cos and a sqrt per shadow ray. If that needs
  winning back, the trig is the first thing to replace.
- `scenetest` green on both backends. **Judged good by the owner at the end
  of the third session**, after the sampling was rebuilt (see above).

### The streak investigation — what is and is not the cause

Measured, not assumed. **Correcting a wrong statement made mid-session:** an
earlier test concluded "the wedges are not shadows". That test was worthless —
the patcher only *replaced* existing keys and the Sodium lights carry no
`CastShadows` key at all, so it changed nothing and the byte-identical frame
was read as evidence. Redone by inserting the key, lamp shadows are exactly
what the wedges were.

| tried | result |
|---|---|
| `--rt-reflections=off` | streak band moves 2% — **not the traced path** |
| `ReflectionFloor` 0.5 → 30 | 23k isolated specks, mean unmoved — **the pinhole problem WR-6's own footnote predicts, not a column** |
| near-water slope floor 0.045 → 0.13 | streaks shorter *and* dimmer — **not roughness** |
| lamp `CastShadows` off (properly) | **wedges and truss lattice gone — this was the defect** |
| lamp `Range` 600 → 2400 | **the columns appear** — but see the cost below |

**So the streak's remaining problem is the `Range: 600` / `OuterCone: 85`
gate**, and it cannot be paid for as authored: 44.2 ms at 600, 92.6 at 1200,
106.4 at 2400 (interleaved, two pairs each). **The cost is the per-pixel light
loop, not shadow rays** — 120 lamps in range of every water pixel. That is
what makes **WR-8's line/tube LTC** the real dependency: the lamp row as a
handful of line lights fixes the streak's shape *and* collapses the count.
(One caveat: the "not shadow rays" half of that was measured with the same
broken flag. Re-measure before relying on it.)

### WR-13 — built, and it does not fix the flicker

**Implemented and correct; nine pixels of difference out of 120 000.** Full
account in `docs/RENDERING-REVAMP.md`'s WR-13 item — read that before touching
the bridge's flicker again. The short version:

- The filter keys off curvature within a pixel, and this bridge is smooth
  plate and cylinder. **Plumbing verified, not assumed** — forcing the value
  to 1.0 moves 242 686 pixels, so it reaches the shading and is simply small.
- Measured four ways with `tools/scripts/check_glint_flicker.py`: TAA +
  animation **14.0%** blinking, TAA frozen 13.2%, MSAA 4x + animation 7.2%,
  MSAA frozen 7.9%.
- **Animation is not the cause. TAA roughly doubles it, 8% to 14%** — the
  resolve does not converge on sub-pixel high-contrast detail. That is the
  lever.
- The residual ~8% is stochastic ray sampling with a frozen scene and no
  temporal filter; **WR-15's shadow jitter is 0.9 points of it**, the rest is
  RTAO and traced reflections.
- Keep it: a handful of ALU, and groundwork for WR-7/WR-8 which widen the same
  lobe. Do not record it as having fixed the flicker.

### WR-13 — the original baseline (superseded by the above)

The owner reported cables and lamp standards flickering at distance. That is
WR-13, and the mechanism is worth stating because the owner's reading was
close but not it: **MSAA 4× supersamples coverage, not shading**, so a
highlight smaller than a pixel flickers whatever the resolve does.

`tools/scripts/check_glint_flicker.py` (new) is the measurement, and the
metric is the blinking-pixel COUNT rather than the worst pixel — the lesson
from the earlier flicker session. **Baseline on the Headland camera, 16
frames: 14.17% of the deck-and-cables band blinks, mean swing 21.2 levels,
worst 220.6.**

### Traps paid for this session

- **`CastShadows` feeds `Scene::LightingHash`.** Any experiment with it
  invalidates the bake, and the frame then falls back to Realtime GI and comes
  back saturated red. Judge nothing from such a frame.
- **A scene patcher that only replaces existing keys silently does nothing**
  for a field the entity never wrote. It cost a whole wrong conclusion here.
  Report insertions and replacements separately.
- **`make_bridge_scene.build()` rewrites `cinematic.cube.meta` with
  `SourceHash: 0`** as a side effect. Restore that file after regenerating, or
  it lands in the diff as a placeholder.
- **The runtime clamps its window to the display**, so `--width=2560
  --height=1630` yields a 2560×1570 swapchain. There is no way to ask the
  runtime for an image larger than the screen; `--ssaa` supersamples internally
  but still resolves to the window's size.

## Start here — 2026-09-02 (second session): WR-3 and WR-4

**Committed at the end of the third session.** The owner has two decisions
open (below) and WR-4 carries an owner gate.

### WR-3, the fog takes the sky's colour — code done

`fog.rvshader` samples the sky cube `Skybox::ResolveEnvironment` already
builds, rather than sharing WR-1's gradient function: the push block was
already 128 bytes to the byte, and the cube path also works for a **cubemap
sky**, which an analytic evaluator cannot. Four scalars ride the `w` lanes of
the camera rows that were being uploaded as zeros. Two profile keys,
`FogSkyAffect` and `FogSkyOcclusion`, **both default 0 = the fog this pass
already had**; `FrameDesc::SkyCube` carries the cube in, and `Scene::
ResolveSky` is public for it.

- **The sample is lifted to the horizon.** Read literally, a ray aimed at the
  water reads the *ground* half of the gradient and the haze came out navy
  under an amber sky.
- **The committed night fog cannot show the feature.** Its layer e-folds at
  **9 m** and the hero camera is at **89 m** — the camera is above its own
  fog, and `FogSkyAffect: 1` moves 44 k pixels by one level. With a layer that
  reaches the towers the same dial moves the frame by 23 levels and varies
  with bearing. **The fog values are the owner's call and were left alone.**

### WR-4, photometry and fixtures — built, awaiting the gate

`tools/scripts/derive_lamp_color.py` is new and does both halves: chromaticity
→ linear Rec.709, and the deck-lamp intensity solved backwards through this
engine's own spot falloff from the survey's road illuminance. 191 lights where
there were 133 (48 graded tower floods, 8 LPS post-tops, 8 midspan nav lights,
2 flashing tower beacons), scene regenerated and **re-baked**
(`field_6ef83b34f7c820f0_*`; the old pair deleted).

- **The lamp colour was out by a gamma** — `(1.0, 0.60, 0.16)` is `#FF8B14`
  display-encoded in a linear field. Derived: `(1.0, 0.2795, 0.0091)`.
- **The lamps were ~15× too dim in luminous terms.** The roadway sat barely
  brighter than the sky behind it. **This makes `GiIntensity: 2.6` a live
  suspect for compensating for under-lit lamps.**
- **+2.3 ms** (50.4 → 52.7 ms at 1600×900) for 58 lights, none casting.
- **`CastShadows: false` is no occlusion test at all**, so a tower flood lights
  the far side of the shaft it stands under. The trade was taken deliberately
  against 15 ms of shadow tracing.
- **120 posts, not the survey's 128**: 128 at 45.72 m needs 2 880 m and this
  model is 2 737 m long. Spacing kept, discrepancy documented in
  `lamp_stations`.
- **Closed a regeneration trap**: the generator had been writing
  `ReflectionFloor: 30` over the committed `0.5` on every rebuild.

### The two open decisions

1. **What the bridge's fog should be.** Three captures were taken (as
   committed / thin haze reaching the towers / marine layer). The profile is
   untouched apart from the two new keys at 0.
2. **`kGradientCubeSize` 32 → 128**, left in the tree and revertible on its
   own. `CityElevationK: 20` makes WR-1's glow lobe about 3° tall and a
   32-per-face texel is 2.8°, so the lobe is under-resolved — in the fog, and
   in **every reflection and probe**, since it is the same cube. At 128 the
   fog's glow peak gains 12 levels and the city-to-ocean contrast goes 12 → 20.
   `scenetest` green on both backends.

## Start here — 2026-09-02: the rendering revamp's first items, and what the frame actually costs

Four commits on `main`: **`c4b6bdc`** (WR-0 + WR-1), **`45e1565`** (WR-2),
**`9f78bff`** (the moon, the frame-budget finding, WR-16), **`bd7f813`**
(traced sky visibility). `scenetest` green on both backends throughout.
`docs/RENDERING-REVAMP.md` is the plan being executed and is kept current —
read it before picking up an item, and read its §2 frame-budget box before
believing any per-item cost in it.

### The one finding that changes other decisions

**The plan's "~12.6 ms night frame" is a ray-tracing-OFF number, and every
per-item budget in it is ranked against a frame nobody is looking at.**
Measured on the committed night scene at **1600×900** — a third of the pixels
the plan quotes:

| | frame | water (Transparent) | Scene |
|---|---:|---:|---:|
| as committed (RT on) | **50.4 ms** | 32.9 | 16.5 |
| `--raytracing=off` | **13.6 ms** | 6.7 | 5.5 |

Isolated one flag per run: **shadows ~15 ms, water refraction ~13,
reflections ~9, AO ~0.1, GI 0** (it never runs — `RayTracedGiSource: Baked`).
Shadows lead for a structural reason: all 128 lamps cast, nothing caps how
many a pixel traces, and the sea fills the screen. That measurement is why
**WR-16 (ReSTIR DI) exists** — owner-set, scoped to DI only; ReSTIR GI stays
rejected because a second temporal reuse scheme fights the GI denoiser's own
history. WR-15 (the tower-base shadow-streak fan the owner spotted) is the
same 128-hard-shadows design seen from the image side.

### What landed

- **WR-0, generator parity.** `make_bridge_scene.py` now reproduces all 133
  committed lights and the Glitter camera's hand-tuned pose, so regenerating
  no longer silently reverts `dab692d`'s work. `check_bridge_roundtrip.py` is
  the standing guard (falsified before being trusted).
- **WR-1, shaped sky.** `SkyCurve` (default 0.45 — the value the shader
  hardcoded, so untouched scenes are unchanged), an additive city-glow lobe,
  and a moon. CPU mirror in `Skybox::GradientAt` matches the shader term for
  term; both feed `Scene::LightingHash`, so changing them invalidates bakes
  **by design**.
- **WR-2, dither.** Only `tonemap.rvshader` dithers, deliberately — see the
  trap below. `sky.rvshader` has an opt-in `SkyDither` with a compensated
  amplitude.
- **The moon is a photograph** (NASA/LRO, public domain, attribution beside
  the texture). `Environment.MoonTexture`; unset, the old analytic disc still
  draws and is the right shape for a **sun**.
- **Traced sky visibility** (`RenderSettings::RayTracedSkyVisibility`,
  Off/Quarter/Half/Full = 0/2/4/8 rays, **default Off**, +2.2 ms at Half).
  The sky term's occlusion previously came only from a baked irradiance
  volume, so it was 1.0 everywhere a volume did not reach.

### Traps this session paid for — do not re-run these

- **A test camera aimed at the horizon here sees only terrain, so the sky
  shader never runs and every sky probe reads as a null result.** This fooled
  me three separate times and sent me hunting a transform bug that does not
  exist. Aim *up* (≥34°) before concluding anything about the sky.
- **`--aa=none` does not disable TAA.** The `--aa` enum is
  none/fxaa/smaa/ssaa/msaa — there is no TAA entry, so that flag never
  touched it. A conclusion I drew from it was wrong.
- **Dither belongs only at the final write.** `1/255` added in sky/fog's
  linear pre-exposure space comes out near **10 levels of 255** after
  Exposure 2.4, ACES and gamma — gamma's slope is steepest exactly in the
  near-black a night sky lives in. Applies to WR-11 and WR-12, both of which
  add HDR-stage writes.
- **The moon renders nothing below ~0.02 rad** (the real moon is 0.0046).
  Not a bug — confirmed by dialling the size live in the editor, it shrinks
  smoothly and fades. The scene oversizes it ~3×; anything sub-degree in the
  sky, **including WR-12's stars**, will hit this.
- **`make_bridge_scene.build()` writes files as a side effect** (the post
  profile, and `make_lut` rewrites `cinematic.cube.meta` with a placeholder
  `SourceHash: 0`). Calling it for a "dry run" dirties the tree.
- **The generator always rewrites `bridge_cinematic.rvpostprofile` with
  `ReflectionFloor: 30`,** while the committed value is `0.5` (lowered
  deliberately in `5d28c0c`). So regenerating the scene silently re-raises
  it. That is a WR-0-shaped gap WR-6 should close.
- **Each executable stages its own copy of the shaders.** Editing
  `RageVEditor/assets/shaders/…` does not reach a running runtime until it is
  copied to `build/bin/Release/RageVRuntime/assets/shaders/`.

### State of the bakes — read before judging any screenshot

WR-1 added fields to `Scene::LightingHash`, which **invalidated every bake in
the project**. Only two scenes have been re-baked: **GoldenGateDemo** and
**showroom**. The other nine (`field_room`, `field_two`, `irradiance_field`,
`irradiance_leak*`, `showroom-mark85`, `showroom2`, `sky_occlusion`) still
have stale bakes and **silently fall back to Realtime GI**, which on the
bridge looks like everything turning saturated red (`GiIntensity 2.6` making
red albedo effectively emit). If a scene looks wrong, check the log for
"no bake matches" before debugging the renderer. Orphaned field pairs were
deleted in this session's cleanup; each scene now keeps only what it asks
for.

### Open, and the owner's call

- **The moon is not visible from the hero camera** — bearing 118°/elevation
  34° puts it behind the Marin ridge. Composition, not a defect; note the
  disc's direction is deliberately shared with the moonlight, so moving one
  moves the other.
- **Next in the plan's order is WR-3** (fog inscatter takes the sky's colour
  per view direction), which builds directly on WR-1.
- `GiIntensity: 2.6` is compensating for something and is the dial the plan
  blames for the red towers. A specific suspicion worth testing: emissive
  meshes become **one area-emitter rectangle from the mesh's bounding box**,
  so all 128 lamp lenses are a single glowing rectangle spanning the whole
  2.7 km bridge. (`kMaxAreaEmitters = 16` is *not* the binding constraint —
  the lamps are one entity, so the cap is never reached. I claimed otherwise
  mid-session and was wrong.)

## ✅ 2026-09-01 — area lights, first form: lights have a radius, and the water gets its streaks

**`Light::SourceRadius` (metres, default 0 = a point and bit-identical to
before -- verified: the showroom renders byte-for-byte with radius 0).** The
sphere-light treatment lives in the lit loop's specular only: the
representative point (Karis) aims the half-vector at the nearest point of the
sphere, the lobe is widened by the source's angular radius, and the energy is
renormalised. Diffuse, cones, range and shadows keep the true direction. The
radius rides the spare `GpuLight.Direction.w` lane; it is mixed into the
lighting hash **only when non-zero**, so every existing bake keeps its name.

**What it proves on the demo:** the four fender marine lights (new, realtime,
`IsBaked: false`) lay coherent red shimmer-paths on the water at
`ReflectionFloor: 0.5` with **zero flicker** -- the analytic lobe integrates
what one traced ray per pixel could only sample, which was the whole argument
of the area-lights roadmap row. The 120 sodium lamps (radius 0.6, Range 600,
outer cone 85) paint their pools along the span from the moved glitter camera
(500 m out, 2.5 m up -- streaks live below ~10 degrees of reflection
elevation, where Fresnel rises; nearer, steeper cameras cannot see them, which
is why earlier framings showed nothing).

**The NaN that multiplied.** `WaterBeckmannD` underflowed to 0/0 at the last
millradian before grazing: unreachable while the range window kept lamps off
horizon water, opened by Range 600, and self-breeding because TAA's history
clamp cannot reject NaN (min/max of NaN is NaN) -- white-cored holes that grew
frame over frame. Guarded at `NdotH <= 1e-3`. **Mid-bake frames also show the
field unconverged (red wash, hot spots); that is the solve settling and stores
nothing** -- judge lighting only after the bake exits.

**Owner's verdict, standing after both forms (2026-09-01): still not up to
the mark against the reference photographs.** The sphere form made pools; the
anisotropic streak frame made the pier's paths longer and narrower, and the
owner judged the result short of the references and paused the row here. The
next session picks this up at item 2 -- LTC line/rect sources -- which was
always this row's destination; the widening tricks have been taken as far as
they go.** The references show long, continuous vertical shafts
running far down the water; the sphere form produces soft elliptical pools
with short trails. The gap has named mechanisms, in likely order of yield:

1. ~~**The widening is isotropic and a streak is not.**~~ **Done the same
   evening**: sized lights on water now evaluate the lobe in the
   view-projected streak frame -- widened 3x along the viewer axis plus the
   source's angular term, held near Cox-Munk across -- with masking kept at
   the surface's own width (widening G with D collapsed G1(V) at exactly the
   grazing angles the shaft lives at and killed it), and half the energy
   renormalisation (the exact factor buried the shaft; the square root is the
   sun-glitter licence every renderer takes). The pier's marine paths run
   visibly longer and narrower; the 500 m span row is still pools, because
   perspective compresses any world-length shaft near the horizon -- the
   remaining gap to the reference is now items 2-4, chiefly LTC line sources.
   Original note: at grazing incidence a real streak elongates along the view
   axis because the up-down facet tilt dominates what can still mirror the
   source; spreading the lobe equally in all directions makes pools. At grazing incidence a
   real streak elongates along the view axis because the up-down facet tilt
   dominates what can still mirror the source; spreading the lobe equally in
   all directions makes pools. The standard fix is elongating the widened
   lobe toward the viewer (anisotropic widening in the view-projected frame),
   or proper LTC where the integration is exact.
2. **A lamp row reads as a line, not a point.** The deck's lamps sit metres
   apart; the reference shafts are plausibly the merged image of several.
   LTC tube/rect sources are the honest answer and were always this row's
   destination -- the sphere was the affordable first form.
3. **The water's anisotropy points along the wind, not toward the viewer**,
   so the Beckmann stretch fights the streak direction in most framings.
4. Exposure and bloom at the streak band are untuned.

Also still open from the first form: the traced path knowing emissive area
(mirror-sharp reflections still see a lamp as its mesh), and a specular-only
range so streak reach does not pay diffuse cluster cost. The marker emissives
stay emissive -- their streaks come from the four real marine lights, so the
reflection floor stays at 0.5 with no fireflies.

## ✅ Resolved 2026-09-01 — the windshield's lost reflection was a stolen `else`, fixed at `90b7ac1`

**Reported: "I no longer see reflection on the windshield of the car in
showroom."** Two wrong explanations died on the way to the right one, and all
three are worth recording.

**Wrong once: "it's the studio lighting mode."** The scene does open in the
dark studio mode (`StartMode: 1` since `09776b7`, semantics 1 = studio,
2 = showroom — the script reads `StartMode != 2`, so 0 is still studio), and
mode 2 is far brighter (windshield 122 against 39). But the owner had videos
from the 27th and a screenshot from the 29th with the shine present in the
same mode, which disproved it: the studio mode itself had dimmed.

**Wrong twice: "traced reflections are just like that."** They are not —
before the regression the glass cast mirror rays and caught the lit panel.

**The truth, found by a nine-build bisect** (windshield mean, same frame:
47.4 on the 27th morning → 44.2 after sky occlusion, a real correction →
39.15 from `7f0b72a` "Water as a component" onward): `RefreshDrawList` ended
its blended-table layout with

    if (gpuCull && !m_BlendSlots.empty()) { ...build the table... }
    else if (gpuCull)                     { m_BlendObjectCount = 0; }

and the water commit spliced its `if (!m_HasBlended)` water check **between
the `if` and its `else`**. The `else` re-attached to the water check — so
every scene that *had* blended geometry under GPU culling zeroed the blended
cull table it had just built. Traced reflections read glass instances through
that table; the raster paths do not. Hence the signature: every ordinary draw
perfect, and only the mirror image gone. The diff never showed it, because
the `else if` lines themselves never changed.

**Fixed at `90b7ac1`**: the water check moved below the closed `if/else`,
byte-identical otherwise, with a comment standing guard. Verified: windshield
back to 44.35 (the pre-regression value to the second decimal), bridge water
bit-stable against a pre-fix frame (354 px vs ~26k of pure animation noise),
showroom2 clean, scenetest green on both backends. Merged to `main`.

**Method notes that earned their keep:**

- The screenshot from the 29th was timestamped two minutes after `02406b2`
  landed — that pinned the regression to a 24-commit window better than any
  build could.
- The date-ordered `git log` hid a branch join: two "adjacent" commits
  differed by 57 files. Bisect on topology, not timestamps.
- Three shader-side suspects were eliminated by editing the *staged* copy
  (no rebuild): the firefly clamp, emissive at traced hits, and hit normals.
  The hit-normal debug is what showed glass casting no rays at all.
- A patch-bisect inside the culprit commit (shader includes → renderer →
  scene) landed on three water-view blocks, then on the one `if` — whose
  loop body never even runs in a waterless scene. The bug was pure structure.

## ✅ Resolved 2026-09-01 — the traced GI bounce runs; the "broken shader" was a stale asset copy

The previous version of this section said `rtgi_trace.rvshader` does not
compile, that `RV_RAY_GI` was not reaching it, and that the traced GI bounce
had therefore never run. **All three claims were wrong**, and the section is
kept because *how* they came to be believed is the useful part. Verified at
`bd98aee` with a clean tree:

- `shaderinfo` compiles it with the renderer's actual define set
  (`RV_BINDLESS RV_RAY_SHADOWS [RV_RAY_REFLECTIONS] RV_RAY_GI`) — and
  `Renderer3D.cpp` has pushed `RV_RAY_GI` unconditionally since `c71234b`,
  precisely so baking never depends on the realtime bounce's switch.
- A live `GoldenGateDemo` run prints `Renderer3D: global illumination traced`
  and renders clean. The bounce is on. Nothing was changed to make it so.

**What the error actually was: stale artefact #1, runtime flavour.** The run
that printed it (bake verification, 22:45 on 2026-08-31) resolved
`assets/shaders/` through `build/bin/Release/RageVRuntime/assets/` — a copy
refreshed only by a build. `pbr_fragment.glsl` was being edited right up to
the 22:54 commit, so that run compiled the day's `rtgi_trace` against an
older copy of the include it depends on. The next build refreshed the copy
and the failure ceased to exist. Same trap as "the editor keeps its own
staged shaders" below, wearing the runtime's clothes.

**How the false "it predates the session" was manufactured.** The standalone
check ran `shaderinfo` *without* `-DRV_RAY_GI` — and without that define,
`GiHash` (guarded by `#ifdef RV_RAY_GI`) vanishes and **any** version of
these files fails with the *identical* two errors at the *identical* lines,
because glslang counts `#ifdef`'d-out lines. Same error text, same numbers,
against HEAD's copy: it looked like proof the defect was old, and proved
nothing. The lesson worth keeping: **a reproduction is not a reproduction
until the inputs match — same defines, same asset copies — and "identical
error text" is what a wrong-input repro looks like too.**

One narrow claim survives: any night-session run whose log carries the
fallback line was judging frames on the screen-space bounce. The baked
volumes' conclusions stand regardless — they were judged against their own
before/after.


## Start here — 2026-08-31 (night session): lighting, tiling, and the two
## defects that were wasting everyone's time

**Committed and pushed as `2711321`.** `scenetest` is green on both backends
(exit 0) and the demo renders clean.

### The two findings that mattered most

**1. The coastline was a step, not a slope.** `heights_metres` ends in a
`np.where` that switches from the land field straight to the sea floor with
nothing between. At Lime Point that put high ground against a -20 m floor:
adjacent samples differing by **60 m over a 2.73 m cell**, which is 2.6 degrees
off vertical and a shape a heightfield cannot hold. It renders as one-cell-wide
vertical ribbons, and that corrugated wall was what "the terrain looks
stretched" actually was. The talus passes never touched it because they run on
`land`, before the join exists. Fixed by `limit_slope`, last, on the joined
field: max adjacent step 60.4 m → 6.3 m, steepest face 86.6° → 67.8°.

**2. The hero camera was two metres from a cliff.** Ray-casting the frustum
against the heightfield showed the right third of the frame was terrain at
**2 to 4 m** from the lens. No texture at any resolution survives that, which
is why two sessions of shader work on the cliffs showed nothing. Moved to
(500, ~102, -1100) — 145 m clear, still frames the tower, and closer to what
Battery Spencer is. **A `cliff` camera was added** because every other camera
looks *along* that coast rather than at it.

### Lighting

- **128 sodium lamps** as spots aimed down (Intensity 452, Range 44), positions
  from `bridge.lamp_light()` — the same function the geometry uses for the
  lens, so a light cannot drift from its lamp.
- **8 tower floods** washing the shafts, and **46 red obstruction lights**
  (saddles, cable markers, fender marine lights) which are **emissive only** —
  markers illuminate nothing, and 46 more lights would cost cluster binning for
  no pixel.
- **Night carries its own post profile**, attached whether or not
  `--cinematic` is given. Auto exposure is **off** in it: a night frame with an
  exposure hunting for mid grey comes back correctly exposed and completely
  wrong.
- **Five baked irradiance volumes** (2 tower, 3 deck at 5 m spacing — the truss
  is 7.6 m deep and a coarser cell hands the soffit the roadway's light).
  `MaxResolution` clamps **per axis** and its ceiling is 256, not 32, which is
  what makes long thin volumes legal.

### Tiling: three separate things, and they are not substitutes

- **Macro variation** (`MacroScale` / `MacroStrength`, both 0 = off) modulates
  albedo, roughness *and* relief from world position. It fixes **uniformity at
  range**. It cannot remove repetition — it multiplies a repeating pattern.
  On terrain (55 m / 0.34) and concrete (14 m / 0.55).
- **Stochastic tiling** removes it. `Assets::SynthesiseTiling` is Heitz &
  Neyret's histogram-preserving synthesis run **once into a larger tile**,
  cached on disk, with a `.rmat` toggle and a regenerate verb in the inspector.
  On `bridge_concrete`, and the pier's chevron lattice is gone.
- **A less directional source.** Concrete044D → **Concrete025**, chosen by
  measurement over nine candidates: anisotropy 1.90 → 1.58, low-frequency
  energy 0.27 → 0.10.

### Movie-grade BRDF (2026-08-31, last thing in)

Four lobes on top of metallic-roughness GGX, **all off by default and verified
so**. `MaterialParams` 96 → 128 bytes, `GpuMaterial` 80 → 112, both checked in
the suite: a block whose C++ and GLSL layouts disagree does not crash, it reads
the wrong sixteen bytes.

- **Clearcoat** — a second smooth dielectric interface at a fixed 4%. The base
  is *attenuated* by what the coat reflects, so the two sum to no more than
  arrived. Adding a coat without that is how a lacquered surface ends up
  brighter than the light falling on it.
- **Sheen** — Charlie distribution with Ashikhmin visibility. Charlie and not
  GGX because a GGX lobe falls to nothing at the silhouette and cloth does the
  opposite: velvet is brightest at its rim.
- **Anisotropy** — GGX split into two axes about the surface tangent, Disney
  aspect remap. Water keeps its own Beckmann lobe and ignores this.
- **Subsurface** — a *wrap*, not a diffusion profile, and the code says so. It
  buys the soft terminator of skin, marble and wax; it will not carry light
  through a thin object.

**They arrive through the `Surface` struct, never from `u_Material`.** The
lighting is shared with the layered variant, whose set 1 has no such fields —
reading them directly fails to compile for terrain. The layered body fills them
off, which is also correct: ground is not lacquered, fuzzy, brushed or
translucent.

**Named floats, not packed vec4s.** Four consecutive floats on a 16-byte
boundary occupy exactly a vec4's std140 bytes, so the GPU sees no difference
and the inspector gets six labelled rows instead of two rows of x/y/z/w with
unrelated meanings inside them.

**Verified unchanged when off**, and the control is the point: against a
pre-change frame the sky is bit-identical and the water and bridge differ by
2.39 / 0.54 — which is exactly what *two runs of the same build* differ by,
because the water is animated. Without that control the number means nothing.

### The sodium flicker on the water, observed 2026-08-31

The owner sees the lamps' reflections **flickering** on the sea. That is the
documented firefly mode, not a new defect: `ReflectionFloor: 30` lets a traced
mirror ray past the clamp that used to crush it to 0.05, so the reflections
arrive — but a 0.6 m lens at 300 m is found by *one ray occasionally*, and the
clamp's own comment predicts exactly this ("the direction drifts with the
sub-pixel jitter, so which texel that is changes every frame, and the pixel
blinks").

**It is evidence for the area-light row (NEXT.md 6), not against the floor.** A
source with extent is found reliably; a point is found by luck. Lowering
`ReflectionFloor` stops the flicker and takes the streaks with it — that
trade-off is the whole reason it is a setting rather than a constant.


### Traps this session paid for

- **A `//` comment inside a multi-line macro deletes the rest of it.** Every
  line of `SHADE_LAYER` ends in a backslash, and a continuation at the end of a
  `//` comment swallows the next line — silently, to the end of the macro. All
  of that macro's prose lives outside it for this reason.
- **The import cache can hold a truncated cooked mesh, and it bit twice.** A
  run killed mid-cook leaves a file that fails *differently each time* and
  never points at itself: first a 68-second hang on "uploading
  bridge_deck.fbx" plus three **skeleton** test failures, then later an
  outright `Uncaught exception: bad allocation` that aborted the suite before
  it reached any of the new checks. Both looked like the change in flight and
  were neither. `rm -rf SampleProject/Cache/v3/models` fixes it for the price
  of a re-cook. **Suspect this first** when the suite fails somewhere
  unrelated to what you touched.
- **`--render-defaults=on` is not an RT toggle.** It resets every render
  setting, so comparing it against `off` compares two different configurations.
  Use `--raytracing=on|off`.
- **Rotations in a `.rage` are radians.** Writing degrees tumbles a rock
  through fifty revolutions instead of tilting it.

### Next, in order

1. **The volumes cover towers and deck only.** Terrain, water, pylons and the
   arch are outside every box, and outside a volume `VolumeIrradiance` returns
   false so *nothing fills in* — the fragment keeps the unconfident traced
   bounce, which is where the remaining blotching lives.
2. **Stochastic tiling on the terrain.** The engine feature is done and the
   concrete proves it; the terrain layers still tile a 1K map every 2 m. Note
   the layered path samples through `textureGrad` already.
3. **The sodium's glitter path on the water is still missing**, and two
   explanations have been eliminated. The traced reflection's firefly clamp was
   crushing every reflection to 0.05 at night, because it bounds against a
   prefiltered probe that goes black — that is fixed and is now the
   `ReflectionFloor` setting (default 0.05, night sets 30), and the *tower's*
   reflection appears as a result. The lamps' do not. Nor is it their cast
   light: raising their range from 44 to 95 m to reach the sea changed nothing,
   and could not have — a lamp 75 m up delivers 1/75² of its intensity to the
   water, and the lit patch would sit under the deck rather than in frame.
   Reverted.

   What is left is a **sampling** problem. The streaks are the specular image
   of a 0.6 m emitter seen at 300 m, spread across many pixels by the water's
   roughness; a traced reflection samples that lobe with rays, and rays mostly
   miss something that small. The levers are calmer water (a tighter lobe makes
   a brighter, longer streak — the reference photographs are of fairly calm
   water), a physically larger or brighter lens, or treating the lamps as area
   emitters for the water specifically. A `glitter` camera was added for judging
   this: broadside across open water, which is the only framing that puts the
   deck's lamps where they *could* reflect into frame.
4. **Cliff and rock assets** — the owner is bringing authored ones. The
   generated cliff panels were deleted: a panel large enough to cover a face
   reads as a flat patch stuck on a hillside, because its outline is a
   rectangle and a cliff's is not.
5. Tower floods still read a little hot at the base from the pier camera.

---

## ⭐ IMPORTANT, DEFERRED — macro breakup in the shader (owner-set, 2026-08-31)

**Status: the shader half shipped in `2711321`, the same day this section was
written saying it had not.** `MacroHash`/`MacroNoise`/`MacroField`/`ApplyMacro`
are in `pbr_fragment.glsl` with three live call sites (raster, layered, traced
hit), the fields round-trip through the serialiser, and five shipped materials
drive it — `bridge_concrete` at 14 / 0.55 and the four `bay_*` terrain layers
at 55 / 0.34, all reachable from `GoldenGateDemo`.

**Two things are left:**

- **No inspector rows.** `ComponentRegistry::BuildMaterial` registers every
  sibling parameter from the same commit — clearcoat, anisotropy, subsurface,
  sheen, stochastic tiling — and not `Macro`. There is even tip prose pointing
  at a panel row that does not exist. So `MacroScale` and `MacroStrength` can
  only be set by hand-editing a `.rmat` or re-running a generator script.
- **Neither suite check was written**: that `MacroScale = 0` is bit-identical
  to a material without the field, and that two points a macro-wavelength apart
  shade differently. The first is the one that protects the "0 = off, nothing
  moves" guarantee the whole design rests on.

*The account below is why it was deferred and what the terrain needed first. It
is kept because the reasoning still holds; only the status line above was
wrong.*

The order changed because the terrain turned out to have a more basic problem
underneath this one, and macro variation could not have fixed it: all four
layers pointed at *one* soil texture tinted four ways, and the tints took it
to a linear albedo of 0.017 / 0.008 / 0.003 — darker than charcoal. Multiplying
a flat brown by a slow wave gives a slightly patchy flat brown. Real material
sets and honest albedo had to come first, and did.

**What that session then proved, which is the reason to come back.** Once the
maps carried real content, the repetition it had been hiding came straight
out: `bridge_concrete` at a 3 m repeat reads as a visible *grid* across a 12 m
pier, and the headland is correct in colour and still smooth because nothing
varies above the 8 m tile. Better textures did not create that — they revealed
it. **The old maps hid their own repetition by having nothing in them worth
repeating.** So this is now both justified and, for the first time, judgeable:
there is content to modulate.

Everything below was written before that and still stands.

---


**Large-scale variation across a whole pier -- the thing that stops any tiled
surface looking uniform at range -- needs macro breakup in the shader, driven
by world position at a scale much bigger than the tiling. That is an engine
change (a couple of material fields plus a term in `pbr_fragment.glsl`) and
it would benefit every material in the project, not just the bridge.**

### Why this is the next thing and not more texture work

The bridge's surfaces were taken as far as a tiling texture can go, and the
ceiling was reached in public. The sequence, so nobody walks it again:

1. First maps had low-frequency content -- `base=2` noise, and an outright
   vertical gradient multiplying the concrete's staining. Seamless edges, and
   still a visible lattice: **what the eye reads is not the seam, it is the
   content.** Anything in a tile bigger than a fraction of it appears once per
   repeat, in rows.
2. So the rule became **no low frequencies and no gradients** -- every field
   from `base=8` up. That killed the lattice and produced the opposite
   complaint, "way too uniform", which is the same fact seen from the other
   side: every repeat is byte-identical, so a large face is a perfectly
   regular field.
3. What is left inside the tile has been done: the regular features are
   varied plug by plug and board by board (`stud_grid` and `seam_field` take
   an rng), the pour lines waver, only a third of the ties weep. It helps at
   arm's length and changes nothing at four hundred metres.

**There is no texture-only answer.** A second, much larger-scale field that
does not repeat within the object is the only thing that breaks it, and that
field has to come from world position at shading time.

### The shape of the change

- Two material fields -- `MacroScale` (metres per macro cycle, 0 = off) and
  `MacroStrength` -- defaulting to 0, so **no existing material moves** and
  the showroom, camp and terrain scenes render byte-identical until one opts
  in. That default is what makes this safe to land.
- A term in `pbr_fragment.glsl` that samples a cheap value noise of
  `v_WorldPos / MacroScale` and modulates albedo brightness and roughness by
  `MacroStrength`. Three octaves is enough; it is a scalar, so it costs a
  handful of ALU and no texture fetch.
- The material UBO layout changes, which touches both the bound and the
  bindless paths -- check `LayeredParams` and `MaterialParams` pack the same
  way, and re-run both backends.
- Suite: a check that a material with `MacroScale = 0` is bit-identical to
  one without the field, and one that two points a macro-wavelength apart on
  the same material shade differently.

Related, and the reason this is worth doing once properly rather than per
scene: the terrain has the same problem and currently hides it behind four
painted layers.

---

## Start here — 2026-08-31 (later): the sky, the strait's real floor, and the
## terrain defect the cliffs found

**The claim from the water session is tested and it was right: the flat look
was the scene.** `Sky: Color` resolves to a literally black environment cube
(`Skybox.cpp`), so every facet of the sea was mirroring one flat value.
Switching to `Gradient` — no asset, and it builds an environment *and* an
irradiance cube — changed the frame more than every shader change of the
previous two sessions put together. Do not spend another hour on water
before the sky is real.

### The strait, to the surveyed numbers

`tools/scripts/make_bridge_seabed.py` writes `terrain/bay.rvterrain` (1025
samples over 2 800 m) plus four layer materials. The first draft was a
symmetric shelf and it was wrong in the way that matters — **the Golden Gate
is violently asymmetric, and that asymmetry is the place**:

| measured | source |
|---|---|
| south pier 1 100 ft off Fort Point, foundation 100 ft down | USNI Proceedings, Apr 1935 |
| north pier **on the mainland at Lime Point**, foundation 20 ft down | same |
| Lime Point cliff 400 ft | Fort Baker post history |
| channel scoured to 113 m in bedrock, nearer the Marin side | USGS multibeam 2004-05 |
| sand waves 30 ft tall, 700 ft crest to crest, across the current | USGS / KQED |
| Hawk Hill 280 m, 1.5 km north-**west** | Marin Headlands |

Axes, because two are counter-intuitive: **-z is north (Marin), +z is south
(San Francisco), +x is west (the Pacific)**. So the strait runs along x and
the bridge crosses along z.

Three things the seabed forced into the open, all now fixed:

- **The two piers stopped at the same -9 m.** Invisible over a bottomless
  bay; the moment there was a floor the south pier hung over a 30 m hole.
  `tower()` takes a pier base now: -7.5 north, -31.0 south.
- **The deck stopped at the pylons**, 340 m short of the shore at one end
  and inside a hillside at the other. It runs the real 2 737 m now, on
  approach bents over the Presidio, and the terrain is shaped to meet its
  soffit where it ends. The Marin end runs in a **cut** through the saddle,
  because a 122 m cliff is genuinely above a 67.4 m soffit.
- **The high ground was on the alignment.** Stacking height on "distance
  inland" put a 280 m hill on the road. Hawk Hill is north-*west*; the
  massifs are placed in world (x, z) now and the bridge crosses a saddle.

### THE TERRAIN DEFECT, and how three wrong answers were eliminated

The Marin cliffs wore a row of black teeth hanging under their skyline, and
it survived: stripping every terrain material to flat colour, forcing
`m_SkirtsDrawn = false`, and the skirt-depth rewrite. It vanished under
`--render-defaults=on`.

**It was ray-traced shadows.** Rays trace the terrain at level 0 always
(`ForRays`, and rightly: an acceleration structure has no camera), while the
raster draws a level chosen by distance. Wherever they differ the fine
geometry shadows the coarse surface — and **a sun near the horizon stretches
that mismatch by 1/tan(elevation), about seven times at 8 degrees.** One
metre of LOD error is seven metres of black.

Three changes in `Renderer/Terrain.{h,cpp}`, both backends green (Vulkan
2431, OpenGL 2384):

1. **`SelectLod` subdivides where the ground bends, not only where the
   camera is near.** Each chunk carries `LevelError[kLevels]` — the metres
   its level strays from level 0's, measured bilinearly at build. A level is
   refused when that error exceeds `kLevelErrorRatio` (0.0003) times the
   distance. **The ratio is tight because of the ray tracer, not the
   silhouette**: a silhouette budget alone would be five to ten times
   looser.
2. **Skirt depth is measured, not proxied.** It was half the chunk's height
   range plus 2 % of the terrain's — fine on rolling ground, an 80 m curtain
   on a cliff chunk. It is now the crack between this level and the next
   coarser one, per level.
3. **Neighbours differ by at most one level**, and a chunk wears its skirts
   only when a four-neighbour drew a different level.

**Still open, and the right fix for the class:** offset the shadow ray by
the drawn chunk's `LevelError` along the normal, the way `ShadowNormalOffset`
does for cascades. That would let the LOD budget go back to a silhouette
budget and take the triangles back.

**Also still open: the terrain has no triplanar projection.** uv is planar
(local metres / TextureScale), so any mapped layer on a near-vertical face
smears into vertical stripes. Worked around by giving the rock layer — the
one that covers steep faces — no maps at all, and painting it over those
faces outright. A mapped cliff needs triplanar in the terrain shader.

### The scene generator

`make_bridge_scene.py` takes `--sky=flat|dusk|night`, `--seabed=bay|none`,
`--hero=<camera>`. Six cameras, and **the ones on land have no Y written**:
`seabed.clear_eye` stands them on the terrain *and* lifts them until the
sight line to their subject clears the ground. Three attempts at the hero
shot came out as a hillside filling the frame before that existed.

**The hero moved to the Marin side.** The owner's approved numbers put it
170 m up on the *San Francisco* side, and with the real land there is no
170 m anywhere near Fort Point — the Presidio bluff is about 74. The
viewpoint that shot is, is Battery Spencer. The San Francisco original is
kept as `--hero=bluff`.

**Dusk, not night, until the lamps exist** (build order item 7): under a true
night sky with nothing lit there is no picture to judge.

---

## Start here — 2026-08-30 (later still): the water owed-list is paid

Everything the section two headings down said was owed, is in — one commit,
owner-directed, at the end of the session. Tests green on both backends
(scenetest OK on Vulkan and OpenGL), both render paths verified against real
frames of `GoldenGateDemo` on Vulkan. After the first framed renders the
owner asked for sharper waves and less, realer foam; the steepness budget now
tilts toward the short waves (same fold total), the injection bar rose to
J < 0.72, and residual foam is streaked along the wind through an elongated
lace read.

### What landed, in one pass over the frame

- **The foam accumulation buffer, rebuilt on the water's own set.** Per body:
  an RG16F ping-pong pair (r fresh, g residual), stepped by a compute pass at
  the top of the frame -- inject where the Jacobian folds, decay
  exponentially, fresh ages into residual, advect downwind. The wave sum
  moved to `include/water_waves.glsl`, parameterised, so the sim and the
  vertex stage compile the same crests. The vertex stage dropped its two
  history taps -- half its wave-sum cost -- because the buffer now carries
  the memory those taps faked.
- **Set 3 is the water's** (0 scene, 1 material, 2 heap): backdrop colour,
  backdrop depth-in-metres, foam buffer, and two tiles generated at first
  use from fixed seeds -- a periodic Gerstner detail-normal map (the ripple
  below the geometry floor, the owed item) and a ridged-noise foam lace.
  Sets are pooled per water run in the scene slot, like the meshlet sets.
- **The glassy see-through, raster form.** A `WaterBackdrop` pass between
  the opaque passes and the transparent one copies scene colour and
  linearised depth (the Unity/Unreal opaque-texture design, chosen over a
  read-only depth layout because it works at every MSAA count on both
  backends with no RHI change). The water refracts through it -- Snell
  direction reprojected through this frame's own projection, depth-rejected
  and frame-clamped -- and absorbs by Beer-Lambert with per-channel
  extinction quoted against GradientDepth. Depth-based colour came free:
  the shallow/deep ramp now reads the measured thickness. OIT alpha goes to
  1 under refraction because the background is already inside the water's
  own term -- revealage showing it again would double-expose the waterline.
- **The traced form, behind a Render Settings toggle** (`RT water
  refraction`, shown beside RT reflections, same prerequisites: ray
  queries + bindless). `RV_RAY_REFRACTION` compiles onto the *transparent
  pair* -- water reads it, pbr ignores it -- so the two set-0 layouts
  cannot diverge; the guard sites in pbr_fragment.glsl now admit it beside
  REFLECTIONS/GI. The refracted ray goes through `TraceSurface`/`ShadeTraced`
  and the hit distance is the true underwater path length.
- **The shine: anisotropic Beckmann for the water's analytic lights.**
  Cox-Munk's Gaussian, stretched 1.16:0.86 along:across the wind, with
  Walter's Smith fit projected per direction. **The roughness is read as RMS
  slope there, not squared** -- the footprint block's 0.24 ceiling *is*
  Cox-Munk's fresh-breeze slope, and squaring it would collapse the horizon
  variance a hundredfold. The analytic specular is also summed separately
  (`waterSpecular`) and held out of the absorption mix: glitter is surface
  reflection and must not fade where the water goes clear.

### The bug that was the reverted attempt's last mystery

**A `RHIResourceSet` keeps one descriptor set per frame in flight, and
`Commit()` writes the *current frame's*.** A set written and committed once
at creation leaves every other frame's slot unwritten -- which validation
reports as "never updated" and the shader reads as zero. That is almost
certainly what "the buffer still read as zero in the fragment" was in the
first foam attempt. The fix is one line: Commit the bound set every step;
with nothing newly set it replays the last full write into this frame's
slot and is then a no-op.

### THE NEXT STEPS, in the owner's order (updated 2026-08-31, after `e633a9d`)

Mips, caustics and the contact-foam rule landed and are pushed; what
remains of the water-realism claim -- theirs to test, mine to have been
right about -- is the two scene items, and after them the owner's stated
arc for the demo. In order:

1. **A real sky.** `SkyType::Gradient` costs no asset; the dusk numbers
   are in the research above (zenith #0C1620, horizon #1B2836, city glow
   #4A3520 at 10-25x zenith; scale up from those night values if the shot
   wants dusk rather than midnight). Sun low, azimuth opposite the hero
   camera, so the anisotropic track runs at the lens. This is the single
   biggest lever left: every facet of the sea currently reflects one flat
   grey value, and no shader can make that read as water.
2. **A seabed.** A coarse sloped plane 5-30 m under the bay near the
   bridge -- a tilted slab is enough for a smooth depth ramp. Everything
   it unlocks is already built and waiting: refraction has something to
   bend, absorption something to measure, the depth-colour ramp real
   metres to read, contact foam a shoreline to ring, caustics a floor to
   dance on. Judge the water again ONLY after these two.
3. **The repeating 15.2 m bay** -- truss, railing, suspender station,
   lamp standard (build order item 3, the next modelling job; the 3.81 m
   master module in the research drives the layout).
4. **Textures/surfaces** -- International Orange by orientation and
   repaint patches, wet asphalt authored dry and wetted in the shader
   (recipes in the research above); asphalt last, tuned live against the
   lamps.
5. **Level design** -- the barricades, the closed-bridge dressing, the
   headlands the brief implies.
6. **Post processing** -- exposure/bloom tuned for the night shot; the
   profile is bare today (PostProfile 0).
7. **Lighting, the full night setup** -- 128 sodium lamps as emissive
   meshes plus non-shadowing points at 1900 K (linear 1.0, 0.239, 0.03),
   four shadow casters following the car, emissive city glow; clamp
   secondary-ray radiance or the lenses will firefly (all numbers in the
   research section).

Standing notes for whoever picks this up:

- **The 2026-08-31 session's record, so nothing is re-derived.** Tile
  mips: full CPU box-filtered chains; **level 0 goes up via `UploadMip`,
  never `Upload` -- a plain Upload into a multi-level texture makes the
  RHI generate the chain by blit, which needs a TransferSrc usage the
  tiles do not carry** (the cooked loader feeds chains the same way).
  Caustics: `WaterCaustics` in pbr_fragment.glsl, two lace-tile webs at
  ripple scale multiplied, masked to a GradientDepth of thickness, both
  refraction paths. Foam: **the owner's rule, their words -- rare
  everywhere, common at contact.** Injection needs a hard fold (J < 0.70)
  AND the top of the swell; every read is quoted against the dominant
  wavelength (v_WaterMisc.y carries RV_WATER_LENGTH now); contact foam
  rings any geometry via thickness-to-zero, band hugging the last 1.2 m,
  lace-dominated, surge trough near zero. Final weights 0.5/0.35/0.22,
  lace at 0.22 wavelengths, caustic gain 2.2.
- **Pictures**: elevated, pitched down. The two approved cameras are in
  memory (`project-ragev-water-component`); grazing shots always read
  flat and the owner is rightly tired of them. Screenshot at frame 300
  so the foam trails accumulate.

### Owed still / known state

- **The owner judged the first framed render "super ugly", and the reasons
  are scene, not shader:** the bay has **no seabed geometry** -- refraction
  has nothing to show but the pier legs -- and the provisional flat-grey sky
  gives the surface nothing to reflect. A pond-style close-up (the reference
  image the owner sent: visible bottom, caustics, near-calm surface) needs a
  bottom under the water and calm dials; the shader side of that look is in.
  **Caustics are the one missing shader ingredient** -- a cheap projected
  pattern on the refracted backdrop, masked by thickness, would carry far
  more than its cost. Not built.
- Foam injection was tempered once already (0.80/2.0 against the fragment's
  0.88/2.6 live term) after the first render came out sheet-white; the
  Foam dial still scales everything, and the sim constants (decay 0.65/0.15,
  transfer 0.55, drift 0.35x speed) are code constants an author may
  eventually want as dials.
- The TAA flicker question above (blended geometry writes no velocity) is
  untouched and untested -- refraction may make it more or less visible.
- One pre-existing validation line under RT GI on first frames: `u_History`
  in a draw before the denoiser's history exists. Not water's.

## Start here — 2026-08-30 (late): THE GOAL, and the research behind it

### What is being built

**`GoldenGateDemo` — a night-time cinematic of a car running a closed Golden
Gate Bridge under sodium lamps, both ends barricaded.** The owner's framing:
*create something complex, find the challenges, address them.* The scene exists
to break the engine on purpose, and the gaps it opens are the roadmap. The full
brief is the artifact linked from `project_ragev_bridge_scene` in memory.

Where it has got to: the bridge is modelled to its own dimensions and the scene
opens on it, the sea is a real component with waves, and the lighting is still
the provisional daylight used to judge shape. **The night lighting has not been
started** — and that matters more than it sounds, because a flat sky is a large
part of why the water reads as flat: water shows its shape by reflecting a
structured environment, and there is currently nothing up there to reflect.

Build order from the brief, with where it stands:

1. ~~Silhouette to scale~~ — done, owner approved.
2. ~~One tower properly~~ — done; Art Deco stepping and the four portals.
3. **The repeating 15.2 m bay** — truss, railing, suspender station, lamp
   standard. NOT STARTED. This is the next modelling job.
4. **Light it before texturing it** — lamps as emissive meshes plus
   non-shadowing points, four shadow casters following the car, deep blue-grey
   sky, emissive city glow. NOT STARTED.
5. Surfaces, asphalt last and tuned live against the lamps.
6. Motion: the car, the chase camera, motion blur, the barricades.

---

## The research, so nobody pays for it twice

Three research passes ran. Their reports are not in the repo; what follows is
everything that changed a decision.

### The bridge, in numbers

| | |
|---|---|
| Main span, tower to tower | 1 280.2 m (4 200 ft) |
| Total length | 2 737 m |
| Tower height above water | 227.4 m (746 ft) to the cable centreline |
| **Roadway above water** | **75.0 m (246 ft)** — see the correction below |
| Truss soffit (navigational clearance) | 67.1 m (220 ft) |
| Stiffening truss depth | 7.62 m (25 ft) |
| Truss centres / cable plane / tower legs | 27.43 m (90 ft) — one number, three uses |
| Main cable diameter | 0.924 m; wrap wire 4.11 mm, so 243 bands a metre |
| **Cable sag at mid-span** | **143.26 m (470 ft)**, ratio 1:8.94 |
| Side span, tower to pylon | 342.9 m (1 125 ft) |
| Suspenders | every 15.2 m, 4 rope legs (2 doubled), 70 mm |
| Roadway | 6 lanes, 18.90 m curb to curb; 3.05 m sidewalk each side |
| Railings | outer 1.22 m, inner 1.37 m, posts at 3.81 m |
| **Lamp posts** | **45.72 m (150 ft) apart, OPPOSITE not staggered**, 128 of them |
| Lamp mounting height | 7.09 m; arm 1.52 m |

**The correction that mattered most: the quoted "220 ft above the water" is the
clearance to the *underside of the truss*, not the roadway.** The roadway is one
truss-depth higher. Three figures only agree at 75.0 m: 746 minus 500 is 246 ft
of roadway; 246 minus 25 is 221 ft of clearance. Building to 67 m sinks the deck
7.6 m and every proportion above it.

**One curvature constant governs all three suspended spans.** The second
derivative is load over horizontal tension, and both are the same either side of
a tower — so the side spans use the main span's constant with only the vertex
moved. Fitting them a separate, flatter curve is the usual mistake. Parabola and
catenary differ by at most 0.38 m across the half-span, so use the parabola. The
cable **kinks at the saddle**; do not smooth it.

**The tower's opening schedule** (above the roadway, in feet): openings 111, 90,
75, 69 — shorter going up — with strut bands of 34, 34, 26, 26 between them.
That accelerating rhythm is what makes it soar. Seven struts in all, four
portals, and the openings are **rectangular**; the apparent arch is corner
gussets. Leg plan 33 by 54 ft at the base tapering to 11 by 25 ft at the top, in
steps, on a 42-inch cell grid (103 cells down to 21). The vertical fluting is on
the **strut housings**, not the shafts — the shafts are flat riveted plate.

**The 12.5 ft (3.81 m) master module.** Rail posts 12.5, truss verticals 25,
suspenders 50, lamp posts 150 — 1, 2, 4 and 12 times one number. Drive the
layout off it and everything lands on the same grid.

Also: 24 belvederes (sidewalk widenings, 12.5 ft, centred between suspenders);
the sidewalk detours outboard around each tower leg; four pylons, not two; and
the deck is three articulated structures with visible joints at each tower.

### Night materials and lighting

- **International Orange `#C0362C`**, linear (0.527, 0.037, 0.025). Roughness
  0.25–0.35 recently repainted, 0.40–0.50 maintained, 0.65–0.80 chalked. Drive
  the variation off *orientation* (normal against up) and repaint patches, not
  noise.
- **High-pressure sodium behind the amber lens: CCT 1887 K, CRI 22.** Practical
  linear RGB **(1.000, 0.239, 0.000)**; lift blue to about 0.03, because both
  chromaticities sit outside sRGB and a hard zero makes ACES hue-skew. The
  chromaticity is essentially *on* the Planckian locus, so a 1900 K blackbody is
  fine for the light colour — the spectral spike only matters for how surfaces
  render under it. International Orange survives because its reflectance peaks at
  600 nm, right where the lamp emits. **That match is why the bridge looks right
  at night and most things do not.**
- 250 W HPS is about 27 500 lm; times 0.70 for the optics and 0.93 for the lens
  gives about 19 250 lm, or **4 000–6 000 cd** peak for a Type III roadway
  distribution. Road luminance is about 1.0 cd/m²; the lens itself about
  45 000 cd/m², roughly 15 stops above the road — **clamp secondary-ray radiance
  or it will firefly**.
- **Wet asphalt is the highest-value surface in the scene.** Author it dry and wet
  it in the shader: base times lerp(1, 0.35, wetness), roughness lerp(dry, 0.08,
  wetness). Dry roughness 0.82–0.92 on the crown and 0.62–0.72 in the
  traffic-polished wheel tracks. What sells damp is the *contrast between
  adjacent bands*, not uniform wetness. Puddles over only 3–8% of the area.
- **Night sky**: zenith `#0C1620`, horizon `#1B2836`, city glow `#4A3520` in the
  lower few degrees at 10–25 times the zenith. Zenith blue must be 3–4 times red
  — a grey-black sky is exactly what reads wrong. Budget the sky IBL at 0.5–1% of
  the road's illuminance; it contributes nothing to the road and is the only
  thing lighting the tower tops.

### Ocean waves — why the first attempt read as corrugated iron

Not the wave count. **Three faults stacked**, and this is the part most likely to
be re-learned the hard way:

1. **A 1.7-octave wavelength band.** Waves of nearly the same size at comparable
   amplitudes beat into a lattice. A real sea shows 3–4 octaves in the range
   geometry can carry.
2. **A 246-degree directional fan.** Real wind seas are forward-peaked, about
   ±35° at the spectral peak. Spread four waves over 246° and some pair lands
   near 90° apart — and **two near-orthogonal waves of similar wavelength ARE an
   egg carton**, analytically. That is the artefact, not bad luck.
3. **Every phase at zero**, so every crest passed through the world origin.

Also: a 0.68 decay is near two-thirds, and **near-rational is worse than either
extreme** — it beats slowly and the eye locks onto a drifting lattice.
Stratified sampling inside log-spaced octaves gives even coverage *and*
incommensurate ratios for free.

Other findings worth keeping:

- **Amplitude proportional to wavelength** is what a k⁻⁴ equilibrium spectrum on
  a log band comes to: constant steepness across scales. Normalise so the sum of
  squared amplitudes over two equals the square of a quarter of the significant
  wave height.
- **A wave shorter than about four grid quads cannot be drawn, only aliased.**
  The grid sets the short end of the band, not the dial.
- **Domain warping is nearly free and worth a lot**: evaluate the short waves at
  the position the swell has already displaced, so the chop rides the swell
  instead of marching through it.
- Production wave counts: GPU Gems uses 4 geometric plus about 15 in texture
  space; Unreal 16; Crest 14 octaves stratified; an FFT ocean about 65k per
  cascade. The eye stops resolving individual constituents around 30–60 when they
  are spectrally distributed.
- **FFT is not the next move.** A 32–64 wave spectrum-driven sum with accumulated
  foam and slope-variance specular is about 85% of the look for about 15% of the
  work. What FFT genuinely adds that a sum cannot fake is Gaussian *statistics* —
  wave groups — and that needs hundreds of components.
- Open sea at 10 m/s wind: significant height 2.47 m, peak period 7.9 s, dominant
  wavelength 96 m. **Both height and wavelength scale with the square of wind
  speed**, so steepness stays about 0.026 — which is the physical justification
  for amplitude proportional to wavelength.

### Foam and the sun track

- **The Jacobian is the right source**, but it only folds when choppiness is above
  about 1 — a sea held under 1 can never have a whitecap. Use a soft bias, not a
  hard test against zero.
- **Accumulate linearly, decay exponentially, and ADVECT.** Fresh foam decay
  about 0.25–1.0 per second, residual about 0.1; drift about 0.03 times wind
  speed. Without advection the foam sits still while the water moves under it,
  which reads as wrong immediately even when nobody can say why. Two channels:
  fresh decays *into* residual, which is what makes a trail rather than a blink.
- **The sun track is a slope problem, not a highlight problem.** Its width is set
  by the surface's mean-square slope, and Cox and Munk measured it from aerial
  photographs: variance is 0.003 plus 5.12e-3 times wind speed. At 10 m/s that is
  0.054 — **geometry at any playable grid holds under a tenth of it**, so the
  rest has to come out of roughness that grows with the pixel footprint. That
  footprint divides by the grazing term, which is exactly where the horizon
  smears.
- **Use Beckmann, not GGX, for water.** Beckmann's slope density is exactly the
  Gaussian that Cox and Munk measured; GGX's heavy tails spread glitter into a
  haze over the whole sea instead of a defined track. Make it anisotropic —
  along-wind and across-wind variances differ — which is what turns a disc into a
  streak.
- **F0 = 0.02, not the generic 0.04.** Already set, via Specular 0.25.
- The brightest thing on a night sea is the **wet crest just before it breaks**:
  drive three states off the Jacobian — foam below about 0.4, wet-and-smooth
  between 0.4 and 1.0, ordinary water above.

---

## Start here — 2026-08-30 (late): water is a component, and what is still owed

Three commits on `showroom2-and-the-card-look`, pushed: `7ffc2f9` (the model
generator learns smooth normals, real UVs and multi-material), `7f0b72a` (the
water component and its shader), `30291ae` (the Golden Gate asset and the
`GoldenGateDemo` scene). Each message carries its full reasoning. Tests green on
both backends at that commit.

### What water is now

A `WaterComponent` carrying Width, Length, Spacing and a block of dials — two
colours and a gradient depth, wave height, length, choppiness, speed, direction,
foam. `Renderer/Water` builds the grid; there is **no material field**, at the
owner's direction: an author tunes the dials and the engine builds the surface,
so nothing in a scene file can point a body of water at an arbitrary `.rmat`.
Water is a fourth `DrawKind` with its own transparent-only pipeline, because its
vertices move.

Waves are 32 Gerstner components picked by a spectrum: stratified inside a
log-spaced band, amplitude proportional to wavelength, a +/-35 degree fan that
widens for shorter waves, random phase each, and the short end clamped to four
times the grid spacing. Foam comes from the Jacobian, evaluated at two earlier
times and drifted positions so it trails a crest instead of sticking to it.

### THE OPEN PROBLEM: flicker during playback, and the first place to look

The owner reports **flicker while the scene runs**. Untested at the time of
writing; the strongest hypothesis, and it is specific:

**Blended geometry does not write velocity.** The water draws through the
weighted-blended OIT path, which is depth-tested and depth-write disabled, and
the motion-vector attachment is written by the opaque pass. So TAA reprojects
every water pixel using whatever velocity the geometry *behind* it wrote — the
sky, or nothing — while the surface under it is visibly moving. A jittered
sample landing on a wave that has moved between frames is exactly the shape of a
per-pixel sparkle locked to the jitter cycle.

Test it the way the showroom flicker was tested: `--screenshot-count=N` from one
run, diff consecutive frames, and look for the 8-frame autocorrelation
(`TemporalJitterPhase: 8`). If it is the jitter, the honest fixes are to give the
water a depth-prepass write so TAA has something to reproject, or to exclude it
from the temporal filter. **Do not tune the wave constants to chase it** — that
was the mistake the showroom flicker cost a day to.

Related and probably the same root: the showroom flicker recorded at the top of
this file. Both are transparent/glossy surfaces under TAA.

### REVERTED, deliberately: the foam accumulation buffer

A compute pass that accumulates foam into a world-anchored ping-pong texture —
inject where the surface stretches, decay exponentially, advect downwind, fresh
ageing into residual — was written, built, dispatched and then **reverted with
`git checkout`** rather than left half-wired. It is not in any commit.

It is the right design and the research backs it: per-frame Jacobian foam is
*attached* to the crest, and being left behind is most of what separates foam
from painted foam. What stopped it was the binding, not the simulation:

- The buffer reached the shader through `Material::SetOcclusionMap`, chosen
  because it takes a raw `RHITexture` and needs no change to a set layout every
  other material shares. That forced a **per-body material** (one material holds
  one texture), which is fine and was implemented.
- `SampleSurface` then multiplies occlusion by that same map, so the foam buffer
  darkened the sea everywhere foam was *absent*. Guarded under `RV_WATER`.
- After that the buffer still read as zero in the fragment. A compute pass
  writing a **constant** did not show either, which rules out the simulation
  maths and points at the descriptor or the bindless material record. The engine
  runs bindless; `Material::WriteRecord` does take a heap slot for the occlusion
  map, so the remaining suspects are the per-frame texture swap against the
  cached material record, or the storage-image/sampled-image aliasing of a
  ping-pong pair inside one frame.

**A `TextureBarrier(ComputeWrite -> ShaderRead)` after the dispatch is required
and was missing at first** — worth keeping whoever picks this up from losing the
same hour. It did not fix it on its own.

Next attempt should bind the foam through the water pipeline's own set rather
than smuggling it through a material slot. It is a set-layout change confined to
one pipeline, and it removes the per-body material, the occlusion collision and
the bindless record question in one go.

### Also owed

- **Depth-based colour.** The scene target already sets `SampleDepth = true`, so
  the depth image *is* sampleable — the blocker is layout, not capability: the
  transparent pass binds depth as an attachment and reading it at the same time
  needs a read-only depth layout the frame graph does not currently express.
- **"Glassy see-through" up close** (owner, on the close-up). Distinct from the
  transparency that already works; this is refraction and absorption with depth.
- **Detail normal maps below the geometry floor.** At 3 m spacing the shortest
  wave that can be drawn is 12 m, and there is currently *nothing* under that.
  This is the remaining source of the evenness the owner still sees.
- The tower gaps the owner marked are fixed (`30291ae`); the free shafts above
  the top strut are correct and not a gap.

---

## Start here — 2026-08-30: the showroom's depth of field never worked, and why

The owner reported that walking the mark85 showroom's camera in towards the
suit made it go soft — "it seems that it's unable to focus on it". It is not a
depth-of-field tuning problem. **Target-mode focus has been measuring the suit
with a box seven times its size since the scene existed**, and the effect has
never once produced a real photograph in that scene.

`Scene::ResolveFocus` solves the aperture from the subject's own depth, and it
reads that depth from `Mesh::GetBounds()`. Instrumented on the real scene, that
box came back **9.85 × 14.41 × 7.87 m, centred 2.40 m up** — in a room whose
ceiling is at 3.7 m, around a figure 2 m tall. Everything downstream is that
number's fault:

| orbit distance | depth | solved half-depth | f-number |
| --- | --- | --- | --- |
| 5.4 m (the scene's own opening shot) | 4.29 | 3.11 | raw 75 → **f/32** |
| 3.2 m (`MinDistance`) | 2.09 | 3.11 | raw **−179.9** → **f/0.7** |

So the shot the scene opens on is stopped fully down and has no visible depth
of field at all, and the moment the camera crosses 3.11 m the f-number goes
*negative* and the clamp underneath reads it as the widest opening there is. A
60 mm at f/0.7 focused at 2.09 m has about **5 cm** of field, sitting on a
phantom point a metre in front of the chest — hence a suit that cannot be
brought into focus by moving. It is a hard flip between two useless states, not
a gradual softening, which is exactly what it looked like.

### The cause: an inverse bind applied twice

`Anim::SkinnedBounds` reduces the vertices to a box per bone **in that bone's
own space** — `InverseBind * position`. What carries a corner of that box back
out to model space is the bone's *global* transform alone. It used the
**skinning** matrix, which is `global * InverseBind`, so the inverse bind went
on a second time and the box landed in a space nothing is ever drawn in:
scaled and displaced by whatever that rig's inverse binds happen to hold. Seven
and a half times, for this one. The fix is one call — `ComposeSkinning` →
`ComposeGlobal` in the accumulate lambda.

**This was never a focus bug; focus is just the first thing that could see
it.** The same box is what culls every rigged mesh in the project. Being too
*large* is the conservative direction, so it never dropped anything that should
have drawn — it only meant the GPU cull was doing less work than it could, and
that is why it sat here since 7.6 (2026-08-13) without a symptom.

### Why two tests sat either side of it and both passed

Worth reading before writing the next bounds test, because the shape is
general. The synthetic fixture gives its single bone an **identity**
`InverseBind`, and applying the identity twice is applying it once. The fox
check compares `SkinnedBounds`' bind-pose answer against `SkinnedBounds`' own
animated answer — a distortion present in both cancels and reads as a pass.
Neither ever compared the result with the vertices it was built from, which is
the property the fixture's own comment claims out loud ("with no clips the
bounds are the bind pose's").

The fox now asserts exactly that: the clipless bounds must **contain** its
vertex box and stay **within 1.35×** of it. The real per-bone fit costs 11% on
the fox's worst axis and is legitimate — an axis-aligned box carried through a
rotation and re-fitted grows; the doubled inverse bind cost 750%.

### The second defect, which is real on its own

`ResolveFocus` divided by `depth - half` and handed the result straight to
`Math::Clamp(aperture, 0.7f, 32.0f)`. A clamp cannot know a sign flipped, so
the uncontainable subject came back as the **widest** aperture — the exact
inverse of what that clamp's own comment promises. There is now an explicit
`depth <= half` guard that takes the smallest opening, and the two limits are
named constants rather than literals repeated from the inspector's slider.
Unreachable in this scene now that the box is right, but it fires for any
subject the camera walks inside the half-depth of, and it would have flipped
the same way with correct bounds and a closer dolly.

### Scale is not the cause, but it is why this surfaced now

The suit is scaled 30.03 on its root and 10 on each mesh — 300× — to fit the
room. That multiplies a mesh-space discrepancy into a 14 m box, and it put
`MinDistance: 3.2` inside the bogus half-depth where the sign flips. A model at
unit scale would have carried the same bug invisibly.

### Left open, deliberately

`showroom-mark85.rvpostprofile` carries `SubjectCoverage: 0.684`, which was
dialled in against the broken box and now means something different: the solve
gives **f/2.1** at the opening distance and **f/6.4** at the closest, with the
near two-thirds of the suit sharp. That is a defensible portrait look and it is
the owner's call, not a leftover — `1.0` is the product-photograph answer if
the whole suit should be crisp. Do not change it without showing them both.

**Verified:** scenetest 2426 checks green on Vulkan, 2379 on OpenGL, exit 0 on
both. `scenes/fox.rage` and `scenes/camp.rage` render with the rigged meshes
drawn and shadowing normally — the tighter culling boxes pop nothing.
ENGINE-NOTES §7.6 carries the amendment.

---

## 2026-08-30: the mark85's glow colours, and A REBAKE IS REQUIRED

The owner asked for the chest arc reactor and the glowing body panels to carry
the same colour as the eye slits. Done in the materials — but **read the rebake
note below before looking at the scene, because nothing will tell you the
lighting is stale.**

### Which surface is which, because the names lie

Established by rendering the three emissive materials as pure red / green /
blue and looking. Do this before editing any of them again; the FBX's material
names do not describe what they cover:

- **`showroom_mark85_glass`** is the **arc reactor panel** — the triangle on the
  chest. Not the material called "Arc Reactor".
- **`showroom_mark85_arc_reactor`** is the **body and leg panels**. It carries
  the reactor's *textures*, which is presumably how it got the name.
- **`showroom_mark85_lights`** is the **eye slits**, plus the hand, shoulder and
  boot lines.

Emissive was matched to the eyes' hue at each surface's own Rec709 luminance,
so only the colour moved: glass `[13, 16, 20]` → `[2.95, 16.21, 28]`, panels
`[3.25, 4, 5]` → `[0.81, 4.45, 7.69]`. The glass number is the one place the
match is not exact — a luminance-preserving transfer wanted blue at 30.8, and
the post profile's `BloomClamp` is 28, so the peak sits on the clamp and the
surface is 9% dimmer than it was. Same convention `SuitLights` already follows
for `PoweredEmissive`.

### THE REBAKE, which the engine will not ask for

**Editing an emissive material does not invalidate a bake in this engine, by
design, and it does not warn.** The bake key is a hash of lights and
environment only — `Scene.cpp` says it outright: *"It does not catch a moved
wall or an edited material... Recapture is the verb for them."* So the field
and the probe cubes on disk still hold the *old* glow, the hash still matches,
and the scene loads them without a word. The suit's own surface changes colour
and the light it throws into the room does not.

This matters here more than it usually would, because `rtgi_trace` treats
emissive geometry as **emitters** with rectangles the bounce aims at — the
reactor is a light source in the bake, not just a bright pixel.

So after any change to these three materials:

```
RageVRuntime.exe --project=SampleProject --scene=scenes/showroom-mark85.rage --bake=force --render-defaults=off
```

**One run covers both lighting modes** — no `StartMode` flipping needed.
`FieldBakePath` keys a file pair per lighting hash and the showroom has two,
but a force bake re-makes every lighting the scene *visits* while it runs, so
the mode sweep produces both pairs and both `.rvprobe` cubes in one go. Verified
2026-08-30: a single run from `StartMode: 1` rewrote `92bc94d1…` and
`c6f976a6…` a minute apart, then exited itself once settled held for 120
frames. It takes about two and a half minutes and roughly 5,000 frames.

### The LIGHTS ON button is disabled, in both suit scenes (owner, 2026-08-30)

`showroom-mark85.rage` and `showroom2.rage`: the Lights Button's
`UIRectComponent.Visible` and `UIButtonComponent.Interactable` are both false,
and its Lights Label's `Visible` too — the label needs its own, because
`Canvas::Walk` hides one element and never its children by design ("hiding a
panel must not move the label on it"). Only the MODE pill is on the credit bar
now, and it was moved into the slot the Lights pill vacated — offsets `-238/-142`
to `-110/-14`, the same 14 px right margin — so the bar reads as designed rather
than as something removed. Anchors are absolute here; nothing reflows on its own.

**The `SuitLights` component is still attached and still runs**, which is
deliberate and is what makes this a clean disable rather than a deletion. Its
`OnCreate` applies the resting state — no emissive override, and the reactor and
repulsor lamps at zero intensity — so the suit sits in exactly the look its
materials describe and nothing can move it. Re-enabling is three `false`s.

Two things were wrong with the powered state and are now moot, recorded only so
nobody rediscovers them if the button is ever turned back on: `GlowParts` reads
`'Lights,Arc Reactor'`, which is the eyes and the *body panels* — the chest
reactor is the Glass mesh and was never in the list, so it never powered up. And
`PoweredEmissive: '18 24 30'` is the old pale hue, so powering up pushed the
eyes and panels off the eye colour while the reactor held it.

---

## THE OPEN PROBLEM: the showroom flicker

The owner sees flicker in the showroom — baked and realtime, parked and
moving, worst in mode 2 on the glossy tiles, the windshield and the
headlight lenses. **A full day of fixes measured ~0% perceptual
improvement.** The memory file `project_ragev_flicker_open.md` carries the
complete record; the short version every future attempt must start from:

- It is per-pixel sparkle locked to the TAA jitter cycle (exact 8-frame
  autocorrelation 0.93; `TemporalJitterPhase: 8`). The blinking-pixel COUNT
  is the perceptual metric — halving the single worst pixel read as zero
  improvement to the owner.
- **Disproven by test:** the tile allocator (counts frozen parked, flicker
  unchanged, and it flickers with budgeting disabled); a
  reflections-in-a-pass rebuild with unjittered rays, temporal accumulation
  and a median despeckle (count 0.60% -> 0.53%, reverted the same night —
  see below); the strict spatial GI filter — which the owner then had
  removed outright (`74790c6`): two attempts, adaptive and strict, both
  blotched beside bright sources. No third under-tuned variant; SVGF-class
  or nothing.
- **Untested, and the owner's own timeline:** the flicker began when ray
  budgeting was merged and enabled (`5dd603f`, 2026-08-27, which set
  `RayBudget: Fractional` in the project). The A/B that settles it — that
  era's build, budget on vs `--ray-budget=100`, owner watching, baked —
  was never run. Run it before building anything new.

## Built and REVERTED the same night: reflections in a pass

The perf session's extraction (its patch is referenced in the 2026-08-27
notes; its shader was never captured) was rebuilt in full: mirror rays
reconstructed unjittered from depth in a fullscreen pass, written into the
existing ScreenReflections history with velocity-reprojected accumulation
(feedback 0.92) and a conditional-median despeckle, the lit shader's inline
ray compiled out. It ran at 0.43 ms and made frames faster.

It was reverted for two measured reasons. It barely touched the flicker.
And on the shipping baked config it slowed the showroom's mode switch: the
accumulated history refills over many frames after a lighting change, where
the inline ray it replaced answered instantly. Anyone rebuilding it needs
what production denoisers have and this did not: reflection reprojection by
**hit distance** (a reflection moves with the thing it reflects, not the
surface it is on) and a history-invalidation story for lighting changes.

## Sky occlusion: DONE, the cube encoding

`2f87153`. Six cosine-weighted sky scalars ride the light tiles' alpha
lanes; aliveness moved to tile 6's `.y`; buried cells read "no data, do not
darken". Judged on the eave fixture (`scenes/sky_occlusion.rage`, now
tracked): open floor 99 vs a 104 no-occlusion reference, covered wall -23%.
The L0/L1 harmonic (Unity APV's encoding, bake-time windowed) was built in
an eighth tile, judged on identical bakes, and removed when the owner chose
the cube. **kVersion is 3**: every older bake in every scene is refused
loudly and re-solves at runtime until rebaked (`--bake=force`). The
showroom and eave fixtures are already rebaked; other fixture scenes are
not.

## The allocator, and one honest loose end

`0daf01b` divides fixed per-type ray budgets (`RayBudgetAoAverage`,
`RayBudgetGiAverage`, `RayBudgetSpread` in RenderSettings) by a per-tile
importance map. The old frame-time dial no longer touches ray counts or
the reflection gloss window while the allocator runs; its own stepping was
also tamed (floor 0.5, gentler levels, wider dead band). **Loose end: the
allocator keys off whether a layer wires a RayBudget history, not off the
project's `RayBudget:` mode — the owner's `RayBudget: Off` today only
stops the old dial.** That mismatch needs an owner decision.

## Uncommitted, deliberately, in the working tree

- `SampleProject.rvproject` reads `GiSource: Realtime, RayBudget: Off` —
  changed by person or app unknown during today's testing. NOT committed,
  NOT reverted: the owner must say which settings are theirs.
- `Stats.g.cs` (generated churn) and `cutout_test.rmat.meta` (touched by
  every runtime launch) — noise, revert freely.

## Working with the owner — read this before touching anything visual

`feedback-ask-which-result-to-apply` and `project_ragev_flicker_open.md`
in memory. The compressed rules, each learned the hard way today: boot the
scene and let them look — never judge for them from a still or a region
statistic; one change at a time, verified before the next; ask before
adding anything, even a diagnostic flag; and never leave test state behind
(a forgotten `StartMode: 2` in the showroom cost an hour of "the project
regressed"). When they assert a timeline, treat it as the leading
hypothesis: they watch this scene across days; instruments here watch it
for minutes.

---

---

## The performance session — 2026-08-27

**The complaint:** frame time swung badly with how close the camera was to the
car, in both baked and realtime modes, on Vulkan with ray tracing.

**What it turned out to be.** Primary ray count never changes with distance --
that part of the design document was right and its proposed fix was not.
What changes is how many *fragments* run expensive per-pixel work. Measured at
2560x1600, close (4.6 m) versus far (9.0 m), triangle count held constant:

| | close | far | swing |
|---|---|---|---|
| Vulkan + RT | 12.39 ms | 8.01 ms | 4.37 ms (1.55x) |
| Vulkan, RT off | 6.93 | 5.67 | 1.26 |
| OpenGL | 9.59 | 7.87 | 1.72 |

So ray tracing is ~71% of the swing, and **OpenGL has the same problem at
1.22x** with no rays at all.

**What shipped, and what each is worth** (2560x1600, close camera):

- **Depth prepass** -- 12.51 -> 9.15 ms (**-27%**, 80 -> 109 FPS), swing
  4.42 -> 2.22 ms, **bit-identical** (0 of 4,019,200 pixels). It runs *inside*
  the scene pass with a colour-write mask rather than as its own pass, which
  is what avoided splitting `EndScene`. **Neutral on OpenGL** (9.656 vs 9.668,
  overlapping) -- the win is not fewer shaded pixels, it is fewer *rays* from
  pixels nobody sees. No toggle exists and none should.
- **Ray rejection** -- Vulkan -3.9%, **OpenGL -3.2%**, bit-identical. Skips
  the shadow ray for a light that is out of range or outside its spot cone.
- **Ray budget** -- `RenderSettings::RayBudget`: Off / Absolute ray time /
  Fractional, shown under Ray tracing. Off by default.
- **Height fog** -- off by default, 0.014 ms GPU.

### The ray budget, and why it is built the way it is

**It budgets the ray passes, not the frame.** Budgeting the whole frame is a
category error: most of a frame is raster, shadow maps and post that no ray
count can pay for, so a weaker GPU whose fixed costs already exceed the target
would pin the rays at their floor forever and still miss it -- stripped of
quality *and* slow. The ray passes therefore carry their own timestamp pair,
always, which is four timestamps against the seventy the by-pass table costs.

**Fractional is the one that travels.** A slower GPU renders a longer frame
and spends the same *share* on rays. Absolute stays for fixed hardware.

Four things it got wrong first, each found by measuring, each worth not
rediscovering:

- **Fractional chased its own tail.** Target = share of frame, so cutting rays
  shortened the frame, which lowered the target. It ratcheted two levels in
  under a second. Solved directly instead: `rays = f * fixed / (1 - f)`.
- **Both GPU averages started at zero** and eased up, so early frames claimed
  the rays were cheap and the quality step landed a second after the scene
  appeared. Seeded with the first real sample.
- **A continuous scale on an integer count flickers** whatever the damping.
  Discrete levels: 8, 6, 4, 2 taps.
- **The level ladder had a limit cycle** on its last step, which halves the
  rays. The climb test is predictive now: move up only if the level above
  would still fit.

### Traps this session paid for

- **Auto-exposure invalidates absolute brightness comparisons.** Turning a
  light off and measuring the region's brightness reads as *no change*,
  because exposure opens to compensate. Measure a patch against a nearby
  reference region instead -- the ratio cancels exposure. This wasted hours:
  bay-lights-off read as -3.03 absolute (nothing) and 1.336 -> 1.180 as a
  ratio (everything).
- **Push-constant vec4s are 16-byte aligned.** A block whose scalars end at 40
  gets padded to 48 by the shader and not by the struct, and every field after
  is read eight bytes out. The fog came out pink. `RtaoComputeParams` never
  hits it because its six scalars end at 48 already.
- **`--bake=on` does not bake.** It writes only what is missing and never
  triggers the solve; 2000 frames produced nothing. **`--bake=force` with a
  large frame budget (8000) is what works** -- the write needs
  `m_SettledFrames > 30` and a solve to have been requested.
- **`CastShadows: false` is not "weak shadows"** -- it is no occlusion test at
  all. The lit shader leaves the shadow term at 1.0 with no ray cast and no
  map sampled, so the light passes through walls by construction.
- **The showroom's post profile has no trailing newline and is CRLF.**
  Appending a key with `printf 'Key: v\n'` concatenates onto the last line and
  the key is silently ignored.
- **`RayTracing: true` with `RayTracedReflections: Off`** crashed once mid-
  session and did not reproduce at the time. ✅ **Found and fixed 2026-08-27.**
  It was a null buffer, not a race, and it reproduced every time once the
  condition was stated properly: **ray tracing on with *both* reflections and
  the traced bounce off**. Ambient occlusion made no difference either way,
  which is why "with RayTracedReflections: Off" looked like the wrong lead.

  `slot.RayInstances` was built only when something traced a *hit* --
  reflections or the bounce -- but `rtgi_trace.rvshader` always compiles with
  `RV_RAY_GI`, because the irradiance fill pass needs it whether or not the
  realtime bounce runs. So its descriptor set declared the ray-instance
  binding whenever ray shadows were on, and `EndScene` wrote that binding from
  a buffer nobody had allocated. Five sibling writes guard on
  `RayReflectionsOn || RayGlobalIlluminationOn`; the GI set's did not, and
  correctly so -- a declared binding *must* be written. The fault was
  conflating "build the table" with "have the table".

  Fixed by separating the two conditions: the casters are walked only when
  something reads a row, and the buffer is kept at one empty row otherwise --
  the same filler the bone buffer uses. Verified clean under validation
  layers with zero errors, on both backends.

  **The reason it hid for so long**: `SampleProject` runs reflections High and
  GI High, so no ordinary run of the project can reach it. Only
  `--render-defaults=on --raytracing=on`, which takes the settings' own
  defaults (all three effects `Off`), gets there -- which is exactly what
  `check_ssao.py` does, and why that script had been failing.

### Two things still open from this session

- **Point-shadow slots are now full**: 4 of `ShadowMap::kMaxLocal`, 1 of 4
  spot. A fifth shadow-casting point light in the showroom is silently
  demoted to "light but do not shadow". Relevant to the bridge's lamp count.
- **The reflection pass was built and reverted.** It gave -17% but could not
  be made bit-identical: `kNormalFormat` is `R8G8B8A8_UNORM`, so a screen-
  space pass can only recover an 8-bit octahedral normal and a mirror doubles
  that error. Widening to 16-bit removed 41% of the difference and the rest is
  MSAA sample-zero depth resolve plus the missing geometric normal. The patch
  is `scratchpad/reflection_pass.patch` if it is ever revisited -- but the
  depth prepass beat it (-27% vs -17%) at zero visual cost, so it is probably
  dead.

---

## Start here — 2026-08-27

**`docs/NEXT.md` is the single ordered list of what to do next.** It unifies
the three roadmaps that were running in parallel (the engine's phases, the
baking sub-roadmap, and the demo scene's demands) and says which one still
owns which detail. Read it before picking anything up; read the rest of this
file for the state of the work in flight.

### NEXT SESSION'S JOB, owner-set 2026-08-27: alpha-cutout materials

**The ask.** Materials are `Opaque` or `Blend` (weighted OIT) and there is no
alpha-tested mode. The glTF importer reads `alphaMode: MASK` as `BLEND` and
warns that the edge will be soft (`GltfImporter.cpp`, ~line 231). Every
railing, grate, chain-link, cable and leaf that any other pipeline would
author as a cutout texture therefore has to be modelled as geometry here --
which is what makes the Golden Gate scene expensive, and it compounds the
missing mesh LODs.

**It splits cleanly into an easy half and a hard half. Do not start the hard
half first.**

**The easy half -- rasterisation.** An `AlphaCutoff` scalar on the material
(glTF's own default is 0.5), a third `BlendMode`, and `discard` in the lit
fragment shader when `alpha < cutoff`. The same discard has to reach the
**shadow map** path or a cutout casts a solid shadow, and the sort key needs a
third bucket: masked draws belong with the opaque pass (they write depth) but
cannot share a depth-prepass shortcut with true opaques. This half also gives
OpenGL the whole feature, since that backend has no ray queries at all.

**The hard half -- ray tracing, and the mechanism is not the obvious one.**
Every ray in this engine is a **ray query**, not a ray-tracing pipeline --
there is no shader binding table and no any-hit stage, so the usual "write an
any-hit shader" answer does not apply. With ray queries the work happens
inside the traversal loop: the BLAS geometry must lose
`VK_GEOMETRY_OPAQUE_BIT`, and then

```
while (rayQueryProceedEXT(q)) { ...decide, then rayQueryConfirmIntersectionEXT(q)... }
```

has to fetch the *candidate's* material and interpolated UV, sample the base
colour's alpha, and either confirm the hit or let traversal continue. Today
that loop is empty (`while (rayQueryProceedEXT(q)) {}`) and every ray is
declared `gl_RayFlagsOpaqueEXT`. **The good news is that TraceSurface already
does exactly this data fetch -- instance -> material -> UV -> texture -- just
*after* the loop rather than inside it, so the work is largely hoisting code
that exists.** `TraceShadowFrom` needs the same treatment and additionally
drops `TerminateOnFirstHit` for masked geometry.

**Budget the cost honestly.** A texture fetch inside traversal is the known
performance cliff of alpha-tested ray tracing; that is precisely why the whole
scene is declared opaque today. Measure it on the showroom before deciding
whether masked geometry joins the acceleration structure at all -- an
acceptable v1 is *raster cutouts everywhere, and masked geometry simply absent
from rays* (it would cast no traced shadow), which is a real limitation but a
cheap and honest one.

**Two things to check on the way in.** `TextureCook` currently picks BC1/BC3;
**BC1 carries one bit of alpha**, which is exactly what a cutout wants and
would be free -- confirm what the cook chooses for a base colour with alpha.
And `ImportedModel` already carries the glTF alpha mode as far as
`BlendMode`, so the importer change is a third enum value rather than new
plumbing.

### In flight, unfinished

**1 · The on-plane-cell defects — fixed, verification incomplete.** This was
the standing item from 2026-08-26 and the cause turned out to be nothing the
old notes predicted. Four changes, all in the working tree:

- **`TracedSurface::Backface`** — the root cause. `TraceSurface` turns every
  hit normal to face the ray (shading depends on it), and the irradiance
  fill tested `dot(hit.Normal, direction) > 0` to find cells buried in
  geometry. After the turn that is *structurally impossible*: `backfaces` was
  always nought, **no cell in any bake this engine ever produced was
  classified as buried**, and cells sitting inside walls stored what they saw
  through them. Proven by dumping a field: 3 519 of 3 519 cells alive with
  alpha exactly 1.0, including cells inside a 200 mm divider holding 0.08–0.10
  of light. The fact is now recorded before the flip.
- **The reader's dead-anchor path no longer falls through to the fast path.**
  With buried cells finally existing, an empty visibility mask was dropping to
  plain hardware trilinear — no visibility, no facing, no aliveness, the most
  permissive read in the function.
- **The lookup steps off a buried anchor** to a cell that was actually
  surveyed, so the mask it judges its corners by means something.
- **The solve reads one cell, unblended, at a buried anchor** where the frame
  interpolates. Interpolation is the part that reaches across a wall, and a
  stored guess becomes the next sweep's fact.

Sealed rooms: `irradiance_leak`, `_point` and `_s2` all render **exactly
0.000**, bit-identical to realtime, from 2.5 / 8.4 / 2.9 this morning. `_s3`
(3 m cells) sits at 60.6 and stays out of envelope — a half-cell bias of 1.5 m
against 200 mm walls samples straight through them.

**What is NOT verified: whether the GI fixture's walls kept their light.** An
earlier cut of the strict guard cost them 3 levels uniformly (the diff image
showed whole wall faces, not contact bands), which is why the single-cell read
replaced the outright refusal. A full rebake-and-compare chain was running
when this was written; its results are the first thing to check. **If the
walls are still dark, the single-cell read is under-reading and the next thing
to try is dilation — fill buried cells from live neighbours but leave alpha at
zero, so the hardware filter gets a plausible value while the careful path
still refuses them.**

**2 · Independent irradiance volumes — ✅ DONE, shipped in `3fa5d36`.**
Volumes no longer merge into a union box; each keeps its own grid, spacing and
rotation, packed into one texture, and a fragment reads whichever it is
deepest inside. `field_two` solves as "2 volume(s) in a 14x9x34 atlas"; the
showroom's single volume comes through at 0.000 mean against its pre-change
baseline. **Not built: overlap blending** — a fragment inside two volumes
takes the deeper one outright, so abut volumes rather than nesting them.

**3 · The v2 rebake — was in flight when this was written; CHECK IT.** The
stamp gained a `Layout` hash and `BakedLighting::kVersion` went to **2**, so
every version-1 bake is refused with *"is version 1 and this build reads 2"*.
Five of the eight scenes that ship bakes were still committed at v1 when
`3fa5d36` landed. A rebake of all eight (old files cleared first) was running;
`scratchpad/rebake_v2.log` ends with `ALL V2 BAKES DONE`. **If those files are
not committed, do that first** — otherwise a clone silently renders eight
scenes on realtime GI. Probe cubes are affected too: the version check lives
in `BakedLighting::Read`, which both fields and probes go through.

### The demo scene

`bridge_race` is now **the Golden Gate at night** — a car running a closed
bridge under sodium lamps. Full brief, with real bridge dimensions, the five
materials, and the engine gaps it forces:
<https://claude.ai/code/artifact/dcba8e5c-a034-439e-9e96-1d0e9cdfe0ac>

The gaps it exposes are items 1, 2, 5 and 6 of `NEXT.md` — height fog,
reverse-Z, mesh LODs and volumetrics — and it is the reason those outrank
everything left in phase 8.

---

## Baked GI: rebuilt, eye-approved, and a pair of flavours

**2026-08-26, later the same day. The rebuild the section below asked for
is DONE and the owner passed both results by eye.** Full account:
ENGINE-NOTES 7cw. The shape of the product now:

- **Baked means baked, both dropdowns.** A Baked source that can be
  honoured drops the whole indirect chain (traced bounce, gather, voxel)
  and the lit pass reads the field per pixel. The hybrid is deleted.
- **A bake is a PAIR of files per lighting** -- `_rt` and `_ss`,
  loaded by whichever GI form is active. **Both currently carry the same
  multi-bounce field**: the ss flavour began as a converged single bounce
  matching the gather, and the owner then ruled it must chase the *voxel*
  stack's level, which measures 4.08 mean levels ABOVE a two-bounce
  reference on the fixture (the cones' own bias) -- the multi-bounce
  field is the brightest honest content, so one solve runs and the store
  writes it under both names. The pair machinery (naming, per-flavour
  loader, settled bookkeeping) stands for the day they diverge again.
  Old unsuffixed files are ignored. `BakedLightingSettled` is a script
  IsActive key and holds only when both files stand; ShowroomMode waits
  on it. NOTE: this also means the ss path now carries the rt flavour's
  sealed-room residue (open item 1) -- the single-bounce ss was exactly
  0.00 there.
- **The solver is Jacobi**: IrradianceVolume carries a swap pair
  (`SolveTarget`/`FlipSolve`); sweeps read the previous sweep's completed
  field. Blend 1.0 then 0.5. Ray budget 1M/frame; passes clamp 64; the
  editor never solves (child process does).
- **The three defects the eye test found, all fixed** (7cw): volume
  coverage (the authored box MUST cover everything visible -- the
  fixture's "sharp vertical line" was the box edge crossing a 16 m wall),
  box-proportional edge fade (now one cell wide), and a two-cell solve pad
  so geometry authored ON the box boundary reads full strength.
- **Acceptance was per-pixel diff images, owner's eye as judge.** Scalar
  means are banned as verdicts (owner said so twice). diffmap.py /
  signedmap.py patterns are described in the memory file; red = baked
  darker = bad, green = surplus = usually fine.
- **`Light::IsBaked` ("Is Baked" in the inspector, default true)**: off =
  realtime light -- direct and shadows as always, excluded from the
  lighting hash AND from the solve's hit shading (GpuLight.Params.w under
  RV_IRRADIANCE_FILL), so toggling it never invalidates a bake and its
  bounce is absent from baked GI. The showroom's four car lamps are
  flagged; the light switch no longer causes a realtime fallback.
- **`IrradianceVolumeComponent::AutoFit` ("Auto Fit", default off)**: the
  box becomes the union of the static meshes' world bounds, snapped
  outward to the cell grid; transform and Extents are ignored while on
  (Extents hides in the inspector). Skinned meshes and terrain are
  excluded -- author by hand there. Off by default because a hand-drawn
  box is a choice, not a mistake (owner's ruling). The editor overlay
  draws the DERIVED box (Scene::GetAutoFitBox), never a re-derivation.
- **Probe cubes ship as BC6H** -- a file format, not a texture format:
  BakedLighting encodes on Write (mode-11 single-partition, the right
  tool for smooth radiance) and decodes on Read, so every caller still
  meets raw RGBA16F and old uncompressed files load by their header's
  word. 12,288 KB -> 1,536 KB per cube (8:1); parity vs uncompressed
  measured at x12 diff gain: black except pixel speckle on the car's
  mirror highlights. Format::BC6H_UFLOAT exists in the RHI enum with
  both backend mappings for the day a probe samples it natively.

### NEXT SESSION, owner-set 2026-08-26: sky occlusion, then the on-plane cells

Two jobs queued, in this order. The first is new capability; the second
is the standing defect below.

#### 1. Sky occlusion in the irradiance volume

**What it is:** store per cell how much of the sky that cell can see,
and multiply it by the *live* sky colour at shade time. Static ambient
shadowing -- recesses, doorways, under eaves -- that still follows a
changing sky. It is the only baked-shadow technique that fits this
engine without a vertex-format change; lightmaps and shadowmasks both
need the second UV set `MeshVertex` does not have (see the note under
"Lightmaps" thinking in 7cw's neighbourhood), and direct-light shadows
need surface-scale sharpness a cell cannot hold. Unity's APV ships
exactly this feature, for exactly this reason.

**Why it is nearly free at bake time:** the solve already traces a full
sphere of rays per cell and *discards* the misses -- "a miss is nothing,
not the sky" in irradiance_fill, because the probe term already
integrates the sky and counting it here would double it. Sky occlusion
is the fraction of those same rays that missed. No new rays, no second
pass; record what is currently thrown away.

**Where it goes:** the visibility tile (index 6) stores its neighbour
bitmask in `.x` only -- `.y`, `.z`, `.w` are free. A scalar sky
visibility fits in `.y` at zero storage cost and with no change to
`IrradianceVolume::kTiles`, so the field's shape, stamp and file size
all stay as they are. (If a *directional* answer is wanted later --
sky visibility per axis, so a wall reads its own side -- that wants the
three spare lanes or a tile of its own, and a kTiles bump breaks every
stamp; decide before building, not after.)

**The trap that must be handled first:** because the shape does not
change, every existing bake still loads -- and reads 0 in the new lane,
which means "sees no sky at all" and would darken every scene that has
not been re-baked. Bump `BakedLighting::kVersion` so old files are
refused and re-baked, rather than trusting a lane nothing wrote. The
refusal costs a re-bake; the alternative is a lighting bug nobody can
find, which is the same argument the version field already carries.

**Reader side:** the lit pass multiplies the stored fraction into the
sky/ambient term only -- not into the field's bounced light, which
already carries its own occlusion. Getting that wrong double-darkens,
which is the same double-count the "a miss is nothing" rule exists to
prevent. Gate acceptance on the usual per-pixel diff maps plus a scene
with an obvious sky-lit recess.

#### 2. The on-plane-cell defects

The one open quality item is really one mechanism with two faces, and it
is the next session's whole task:

1. **Sealed-room leak residue**: 3.2 mean levels at 1 m cells under the
   multi-bounce field (a pitch-black room reads faintly grey; the
   point-light room reads 8.4). The carrier is MEASURED, not guessed:
   cells sitting exactly on grid-coincident surfaces trace half their
   rays from inside geometry, oscillate around the backface death
   threshold between sweeps, and feed a whisper of the lit side into the
   feedback loop.
2. **The divider's thin-edge bright strip** on the GI fixture -- the
   same cells, seen from the other side: a survivor on the plane stores
   a poison mix of both sides and the thin face reads over-bright.

The named fix is the APV/DDGI **virtual offset** (lift trace origins off
surfaces before casting). It was implemented once and measured MIXED --
point-light room 8.4 -> 0.2 (outright fix), sun-lit rooms 3.2 -> 15.6
(worse: waking the on-floor cell layer from dead to alive changes which
cells anchor the reads above it) -- and reverted with the data in 7cw.
The rebuild must treat the lift and the alive/dead/visibility rules as
ONE design, not a bolt-on: decide what a lifted cell's backface count
means, which mask a lifted cell publishes, and gate on all four
sealed-room fixtures plus the divider strip. At 3 m cells the reader
leaks regardless (68 levels; a half-cell bias is 1.5 m against 0.2 m
walls) -- out of envelope, document spacing guidance rather than chase
it.
3. **The voxel-bias fork, ruled 2026-08-26**: the owner chose to leave
   baked at the physically-correct level (option 3) -- no fudge factor.
   Voxel realtime runs measurably hot (4.08 mean levels above a 2-bounce
   reference on the fixture, its cones' own bias); calibrating it down
   toward truth -- where baked already sits, so the two would then match
   for free -- is deferred work for its own session.
4. The `.rvprobe` size decision: per-lighting cubes are 12.5 MB each.
   BC6H in the cooker, or stop persisting probe cubes.
5. An auto-fit "Global" volume mode (Unity-APV-style) so under-coverage
   becomes impossible instead of an authoring trap.

### What this session fixed that the plan stands on (all verified)

- **The committed bakes were files of zeros** -- every pre-2026-08-26
  "baked" number was fiction, which is how the quality gap stayed hidden.
  Bake ordering, per-lighting probe files, the never-executed probe adopt,
  and the play-vs-edit brightness jump are fixed (7cp, 7cs).
- **Realtime is pure**: the field binds only for a Baked source (or while
  baking). A stale bake cannot silently bend a Realtime render.
- **The editor's black wedge was RTAO self-intersection at small render
  heights** -- fixed engine-wide with a slope-proportional ray-origin lift,
  free at full resolution (7cu). It was never the baked system.
- **The Bake button is a non-blocking child process** teeing into the Build
  Log; forced bake runs end themselves when everything is stored (7cv).
- The measurement protocol that produced every number above is in 7ct;
  reuse it. Traps that cost time this session: `--rt-ao=half` and project
  `RayTracedGlobalIllumination: Half` both coerce to OFF (there is no
  half-res GI rung); a bake on OpenGL writes zeros; staged shaders reset on
  every build; mode 2 is reachable without baking by sed'ing the scene's
  `StartMode: 1` to 2; the runtime window clamps to 640 tall.

### Also open

- The `.rvprobe` size decision: per-lighting cubes are 12.5 MB each, ~31 MB
  uncommitted for the showroom. BC6H in the cooker, or stop persisting
  probe cubes.

## What this session measured mode by mode (superseded as direction -- see Start here)

**2026-08-26, end of day.** Everything below is in the working tree,
**not committed**. scenetest is green on both backends (2393 Vulkan, 2346
OpenGL). Full accounts: ENGINE-NOTES 7ct (the semantics), 7cu (the editor's
black wedge), 7cv (the Bake button).

**The one-line summary: the field was never the problem; the mode was.**
A loaded bake used to drop the whole traced GI chain and answer indirect
alone -- measured honestly against a two-bounce reference that is a 5.73
where one traced bounce is a 1.48. The same field *terminating* the traced
bounce scores **0.73, better than realtime, at the traced bounce's cost**.
So that is what the sources mean now:

- **RT GI on + source Baked -> the hybrid** (chain runs, rays terminate in
  the field): two bounces of transport for one bounce of rays.
- **RT GI off + post source Baked -> field-only**: the explicit perf mode
  (showroom 3.9 ms vs 7.2), coarse at walls and contacts by construction.
- **Realtime is pure**: the field binds only for a Baked source (or while
  baking), so a stale bake can never silently bend a Realtime render.

Showroom mode 2 -- the "26% of pixels wrong" scene -- is 2.98% under the
hybrid. The old "0.3% apart at half the cost" claim was validated against
bakes that were files of zeros; treat any pre-2026-08-26 baked-GI number
with suspicion.

**The editor's black wedge is fixed, and it was never the baked system.**
RTAO's ray origin sat below its own floor wherever a texel of grazing
surface spans more depth than the fixed lift -- true of any render shorter
than ~500 px, which only the editor's Game panel ever is. The lift now adds
the measured per-texel depth slope (rtao_compute.rvshader): 45.2% -> 0.0%
black at panel size, 0.002 mean levels at 1600x900. The runtime never
reproduced it because its window clamps to 640 tall.

**The Bake button runs a child process** (Vulkan runtime, `--bake=force`,
output teed to the Build Log, editor stays live) and a forced bake run
exits itself once everything is stored -- the fixture bakes and quits in
2.6 s. The editor reloads the files on completion.

### Open, in the order worth taking

1. **The `.rvprobe` repo-size decision.** Per-lighting probe cubes are
   12.5 MB each and the showroom now has two, plus two 3 MB fields --
   ~31 MB none of which is committed. Either BC6H in the cooker (the
   documented prerequisite) or stop persisting probe cubes and accept six
   cube faces at load.
2. **Field-only's contact halos.** 5.73 on the fixture is dark halos at
   cell scale; DDGI-style relocation or finer storage is the road if the
   perf mode ever needs to look better than it does.
3. **Hybrid costs +21% on the showroom** (7.2 -> 8.7 ms) -- the per-hit
   field fetch across a heavy ray budget. Skipping the field on reflection
   rays is the first thing to try if that ever matters.
4. **`--rt-ao` CLI values silently coerce** -- `half` parsed as a bool once
   during diagnosis; worth an explicit value list like the other ray flags.

## Start here: both showroom modes are baked now, and two bakes that were doing nothing

**2026-08-26.** Switching the showroom's lighting mode reported `no bake matches
this scene's current lighting` -- in the editor, in the standalone runtime and
in a game built with **Build Game**. Fixing it turned up three defects, only the
first of which was the one being looked for. Full account in ENGINE-NOTES §7cp.

1. **Mode 2 could not be baked at all.** A bake is one file per lighting, and
   mode 2's lighting is applied by `ShowroomMode.cs` at runtime -- so it does
   not exist until the script has run, and `--bake=on` only ever saw the mode
   the scene opens in. **A scene knows its lightings and the baker does not**,
   so the scene says: `Graphics.IsBakingLighting` (interop protocol 12) is true
   only under `--bake=on`, and `ShowroomMode` holds each mode for 180 frames and
   calls its own `Toggle` between them. One run, one file per mode.
2. **A solve consumed inside a probe capture was never re-asked for.** The
   capture runs before the main pass and re-enters the scene walk per cube face,
   where a solve is refused -- and a script that changes the lighting dirties the
   probe in the same breath, so *every* mode switch landed there. The field
   stayed at zeros, and under `--bake=on` the zeros were written. The showroom's
   committed mode-1 bake was 44 non-zero bytes in 156 896. Wanting a solve is
   now state (`m_FieldSolveWanted`), and the bake refuses to write while it is
   set.
3. **`ReflectionProbe::Adopt` had never run.** It sat above the line that builds
   the probe object, so it declined on its own null guard at load and was
   unreachable after. Every bake wrote a 12.5 MB `.rvprobe` that no run read.

**Where it stands:** `assets/baked/showroom/` holds a real field per mode, both
ship in `content.pak`, and a mode switch in a packaged build loads its bake with
no solve and no warning. 3.6 ms. Verified with `rvpack` and running the package
-- **nothing short of that exercises the pak**.

**To re-bake after touching the showroom's lights:**

```
build/bin/Release/RageVRuntime/RageVRuntime.exe --rhi=vulkan \
  --project=<repo>/SampleProject --scene=scenes/showroom.rage \
  --bake=on --render-defaults=off --benchmark=800 --vsync=off
```

Vulkan is not optional: **OpenGL solves nothing**, because the fill traces rays
and there are none there. A bake run on OpenGL writes a file of zeros that every
later run will load and trust. The staged runtime's `ragev.ini` says
`rhi = opengl`, so pass `--rhi=vulkan` explicitly.

### The Statistics panel's Renderer rows

They read `155 mesh draws, 0 triangles, 0 culled` -- three true numbers, because
a GPU-driven frame keeps its survivor counts in device memory. Triangles are now
counted as *submitted* across every pass that counts a draw, each row says which
half of the frame it describes, and the two quad rows are gone (nothing calls
`Renderer2D::DrawQuad`, so they were structurally zero). ENGINE-NOTES §7cq.

---

## Lighting is baked, and what that does not yet cover

**2026-08-26.** BAKING-ROADMAP items 1.2 and 1.3 are built and pushed
(`c71234b`, `077f493`, `8083baa`, `86979e6`). Indirect light can be computed
once, written beside the scene, and read back on later runs -- which is the
first thing in this engine's history that survives a restart.

```
done   0.  probe hygiene            Cached rename, invalidation, Recapture verb
done   1.  the field, plumbed       30e15bb
done   2.  the solve                a pass of its own, measured against realtime
done   3.  the baker + on disk      fields and probes, per lighting, composed
       never 4. lightmaps
```

**What it buys, measured.** The showroom carries a volume, is baked, and has
both GI sources set to Baked:

| | frame | fps |
|---|---|---|
| realtime traced GI | 7.4 ms | 134 |
| **baked** | **3.73 ms** | **267** |

**Correction, 2026-08-26:** the "0.5 levels apart" half of this claim was
measured against bakes that later turned out to be files of zeros, and the
chain-dropping mode it describes scores 4x worse than realtime on the GI
fixtures. The speed is real; the near-equal quality was not. See
ENGINE-NOTES 7ct for the honest mode-by-mode table and what Baked means now. On the GI corner, where indirect actually dominates, it is
0.83 ms against 1.62 and the pictures are 0.30% apart.

### How it fits together

- **`--bake=on` produces; nothing else does.** There is no cached mode and no
  way to reach one: without the flag a missing file leaves the field empty and
  the GI source falls back to Realtime. The solve exists to make a bake, not to
  be a third state between realtime and baked.
- **One file per lighting**, `field_<hash>.rvfield`, beside the scene in
  `assets/baked/<scene>/`. The hash that already told a field it was stale now
  also names which file to read, so baking a scene under two lightings leaves
  two files and switching between them picks one. Creation is deliberate;
  selection is automatic.
- **A scene composes every volume it has into one field** -- the union of their
  bounds at the finest spacing any of them asked for. A shader reads one field;
  choosing between several per fragment would spend every frame on a question
  with one answer per scene. Unity's adaptive probe volumes and Unreal's
  volumetric lightmap take the same road. A single volume keeps its rotation;
  several compose axis-aligned.
- **Probes bake too**, through `ReflectionProbe::Adopt`. A baked probe is ready
  on the frame the scene loads instead of six frames of cube faces later.
- **`RHIDevice::ReadTexture`** is the piece that made any of it possible, on
  both backends. The capture path that existed was a 2D RGBA8 diagnostic.

### What is open, in the order I would take it

1. **Per-light baked-vs-realtime, and named scenarios.** This is the one that
   matters. The hash covers each light's *position and direction* as well as its
   colour, so any light that moves or animates -- a day-night sun, a carried
   torch, a swinging lamp -- changes it every frame, matches no file, and falls
   back to Realtime forever. The feature would silently do nothing in a scene
   with dynamic lighting. The fix is the one Unity and Unreal both have: a light
   declares whether it contributes to baked GI, the hash covers only those, and
   deliberate variants become *named* scenarios rather than hashes. That also
   fixes the readability of these filenames and the editor-versus-runtime split
   at its root.
2. **A Bake button and a variant list in the inspector.** Baking is CLI-only
   today and the files are named by hash, so an author cannot see which
   lightings are baked or make one without a terminal.
3. ~~**The showroom's mode 2 is not baked.**~~ **Done 2026-08-26** -- see
   "Both showroom modes are baked now" below. A scene can walk its own lightings
   during a bake run through `Graphics.IsBakingLighting`.
4. **Probe bakes are 12.5 MB each** at 512 faces. The showroom's is committed;
   BC6H in the cooker is still the prerequisite before this scales past one.
   Field bakes are 150-670 KB and are in the repository.
5. **Contact-scale bleed**, ~1.9% of pixels beyond 4 levels, concentrated on
   geometry edges. A grid cannot resolve it; nested volumes or per-surface
   storage can.

### Two traps this cost, both worth reading before touching the bake key

**Never hash a struct's bytes to key anything that outlives the process.** The
lighting hash was an FNV over whole `LightRenderData` structs, which have
padding after their enum and bool -- and padding holds whatever the allocation
happened to contain. MSVC writes 0xCD there in a debug build and leaves it alone
in a release one. One unchanged scene hashed three different ways: the runtime
baked one file, the debug editor asked for a second, the release editor asked
for a third, and each fell back to Realtime reporting that nothing was on disk.

It fails in the direction that looks like a *missing file* rather than a *broken
key*, so baking again appears to fix it -- once per build, never twice. That is
what made it survive a wrong diagnosis (that the editor differed because the
mode script had not run) and a wrong fix (baking from the editor as well). Hash
named fields, through their bit patterns.

**Test a shipped build, not only the editor.** `BakedLighting::Read` opened its
file with an ifstream: correct in the editor, correct on a loose project, and
blind in a packaged game, where assets live in `content.pak`. Every bake fell
back to Realtime in the built game while working everywhere it had been tested.
Bakes go through `VFS::ReadBytes` now, which is what the scene loader has always
done -- "a scene in a shipped pak and a scene on disk are the same call". The
check that catches this class of bug is `rvpack` and then running the package;
nothing short of that exercises the pak.

**A warning must name what it looked for.** "No baked field on disk" is true and
useless when files are named by hash: a bake made under different lighting is a
present file with the wrong name, and reads identically to no file at all. The
warning now prints wanted, where, and what was found instead, which turned the
above from an afternoon of guessing into a one-run diagnosis.

### Verified at the point of writing

scenetest 2393 on Vulkan and 2346 on OpenGL, Debug and Release. Validation clean
with a volume in the scene. Bake, restart, load: bit-identical, and the stale
bake of a moved volume is refused. `SampleProject`'s script module must be
rebuilt after any change to a component's layout -- `PostSettings` gained a
field this session and scenetest segfaulted at 81 checks until it was.

---

## The field itself: how it is solved, and what it measured against

**2026-08-25.** Work started on the baking sub-roadmap
([BAKING-ROADMAP.md](BAKING-ROADMAP.md)) at item 2, the irradiance volume,
because it is the piece that serves dynamic objects and it turned out to be
worth more than that -- it is also the terminal term of every traced bounce ray,
which is the cheap road to behaving like a multi-bounce tracer.

```
done   0.  probe hygiene            Cached rename, invalidation, Recapture verb
done   1.  the field, plumbed       30e15bb  -- no-op by design
done   2.  the solve                7194a9b the shader, and now the pass
       3.  the baker + on disk               -- 2 is its only oracle
       never 4. lightmaps
```

A field is solved once -- on the frame a scene loads, or one something
invalidates -- and held after that. `scenes/irradiance_field.rage` is the check
scene, and the command below is how to run it.

### Where the solve runs, and the kind of pass it needed

**`RGPassKind::Standalone`**, a third kind of render-graph pass: the graph opens
nothing for it, and the pass records its own render pass and its own barriers.
It exists because the fill is pinned on three sides at once.

- **After the scene pass.** The solve traces, and the set it traces through is
  built by BeginScene -- which is why the scene walk can only *ask* for a solve
  (`Renderer3D::RequestIrradianceSolve`) and the graph is what runs it.
- **Outside a render pass.** It begins one of its own to raster a fragment per
  cell, and a barrier may not be recorded inside one either. `Scene::OnRender`
  *is* a graph pass's body, so nowhere inside it qualifies; an ordinary graphics
  pass of the graph does not either, because the graph has already opened one.
- **Once per frame, not once per viewport.** The request clears when it is
  solved, so the editor's second graph finds nothing to do.

The field is therefore read by the lit pass of the *next* frame. That one frame
of latency is the trade for tracing a scene that is current rather than one a
frame old, and it is invisible on something solved once and then held.

### What is already right, so nobody re-derives it

- **It had to be a fragment shader, not compute.** The hit shading belongs to
  `TraceSurface`, and a second copy of it is how two renderers start disagreeing
  about what a hit looks like. That include survives `RV_TRACE_ONLY` but not
  leaving the fragment stage: the shadow lookups above its guard sample with an
  implicit level of detail. Voxelisation already writes a 3D texture from a
  fragment shader here.
- **Bind `slot.GiSet`, never `slot.Set`.** A descriptor set belongs to the
  layout it was allocated against. `slot.Set` is the lit pipeline's and its
  set 0 carries the vertex stage's bindings; the fill shader includes the same
  header under the same defines as `rtgi_trace`, so `GiSet` is its set exactly.
- **One fill set per frame in flight.** The set is rewritten with the field
  being solved, and a descriptor set may not be rewritten while a command buffer
  still refers to it. A scene whose lights move asks for a solve every frame,
  which is precisely the case that would rewrite one still in use.
- **The projection is a mean and twice a directed mean.** `c0 = mean(L)`,
  `c1 = 2 * mean(L*d)` -- the Monte Carlo weight, both basis constants and the
  band factor all cancel. Verified against the case with an answer by hand:
  uniform radiance L gives constant term L and no lean, which is what the
  placeholder fill writes, so the two agree by construction. The lean needs
  three accumulators, one per channel; sharing one stores a red wall's
  directionality into green and blue as well.
- **A field records the lighting it was solved under** -- a byte hash of the
  light list and the environment -- so the showroom's mode switch invalidates
  it. `Recapture` is the verb for what the hash cannot see: moved geometry,
  edited materials.

### The trap this cost, and how it was closed

The volume textures are `Sampled | Storage`, and the pair has a rule attached. A
storage-capable image has its **sampled** descriptor written against
`VK_IMAGE_LAYOUT_GENERAL`, while a CPU upload used to leave it in
`SHADER_READ_ONLY_OPTIMAL` -- so every draw reading one was a layout mismatch,
shipped in `30e15bb` and not noticed. `Storage` was then taken off to make the
reads honest, which left the solve with nothing to write through.

Both halves are one rule in one place now: **`VulkanTexture::SettledLayout()`**
-- GENERAL for a storage image, read-only for everything else -- and every path
that moves an image and puts it back ends there, the uploads and the mip chain
alike. A storage texture may therefore be uploaded to, which this field needs:
stage one fills it flat so a field is never sampled while it holds whatever the
allocator left behind.

None of it was noticed because validation was run on a scene with **no volume in
it**, where the binding is never reached -- "a check that tests the path nothing
ships is not a check", which this repository already learned once on the texel
emitters' cooked-versus-raw mean. So, from the runtime's own directory:

```
RageVRuntime.exe --project=<repo>/SampleProject --scene=scenes/irradiance_field.rage
  --rhi=vulkan --validation=on --render-defaults=off --import-cache=off
  --screenshot-frame=60 --screenshot=field.png
```

It logs `irradiance field solved, 9x5x9 cells` once, and validation -- the
layers and synchronisation validation both -- says nothing. Against the same
scene without the volume (`scenes/gi_corner.rage`), the field adds a soft
red-tinted term on the surfaces facing the red wall and a grey one opposite:
+5.5 mean levels of red against +3.7 of green and blue.

### What the field is worth, measured against the thing it replaces

A stored field only earns its place if the picture is at least as good as the
realtime answer. The check that settles it: **one traced bounce plus the field,
against a true two-bounce render.** The field is filled by the same rays that
shade a bounce, so if it is working, one bounce terminating in it should land
near two bounces -- and if it is not, the difference says by how much.

`scenes/gi_corner.rage` and `scenes/irradiance_field.rage` are the same scene
with and without a volume. Frame 120, Release, Vulkan, this laptop:

| | GPU frame | error against a true 2-bounce render |
|---|---|---|
| one bounce | 1.535 ms | 1.483 |
| **one bounce + the field** | **1.591 ms** (+0.055) | **0.720** |
| two bounces | 1.938 ms (+0.45) | 0 by definition |

(Those are the current numbers, with the visibility term and four sweeps. The
first version of the field, before either, measured 0.735 at +0.064 ms.)

Half the second bounce's quality for **a seventh of its cost**, and the cost is
per frame while the solve is once. Error is mean absolute difference in levels
of 255 over the whole frame; the two runs of each were within 0.008 ms of each
other, which matters on a laptop that drifts a millisecond over a session.

### The currency rule, which is what made it wrong the first time

**A cell stores bounced light. Not sky, and not the whole answer.**

The first version stored everything reaching a point and every reader *added* it
to what they already had, so the first bounce and the sky were each counted
twice. It looked like a feature -- the picture got brighter -- and it was
brighter than two real bounces: +5.5 levels of red where the second bounce is
worth +2.2, an error nearly four times what the field was supposed to be worth.

So the field follows the rule the bounce already followed (7bb):

- **The fill drops a miss** rather than storing the sky, because every reader
  adds the probe irradiance beside it and the probe integrates the sky.
- **`ShadeTraced` adds the field** to the flat ambient and the probe, one part
  each, none of them standing in for the others.
- **The lit path treats the field and the screen-space bounce as alternatives,
  never a sum.** They carry the same quantity. The gather wins wherever it is
  confident -- it is per pixel and sharper -- and the field answers for the rest,
  which is the case the bounce has never had an answer for: a surface that was
  off screen last frame, or one a moving object just uncovered.
- **An unsolved field is zero**, so a scene with a volume renders exactly like
  one without until the pass runs. It used to be pre-filled with the flat
  ambient, from when a cell carried the whole answer.

### The shadow half of the bake, and the four versions of it that were measured

**A field now stores what stands in the light's way, and it is not optional.**
Every solve writes, from the same rays that carry the light back, an octahedral
map of sixty-four directions per cell: how far geometry is that way and the mean
of its square, two bins to a texel, thirty-two tiles beside the three of light.
There is no flag for it. A field that says where light is without saying what
blocks it is a field that leaks, so the two are solved together or not at all.

The lookup weights each of the eight surrounding cells by three things: its
trilinear share, whether it is in front of the surface's plane at all, and
Chebyshev's inequality against those stored distances -- the DDGI test, which
turns "further away than the wall this cell can see" into a soft weight rather
than a hard edge.

**This took four attempts, and the first three are worth recording because each
one looked right.**

| what was stored | leak, 2 m cells | error vs 2-bounce |
|---|---|---|
| nothing | 3.435 | 0.735 |
| distance as SH-L1, 4 coefficients | 3.366 | 0.794 |
| octahedral, 16 directions | 3.591 | 0.778 |
| octahedral, 64 directions | 3.165 | 0.778 |

A first-order fit of distance says "about nine metres, more to the left", and
what stops a wall leaking is knowing it is eight hundred millimetres off in
exactly one direction. Sixteen directions is a forty-five degree cone that
averages a wall with the room behind it. Sixty-four is sharp enough to mean
something -- and still did not close the 2 m case, which is the honest result
below.

### Where the leak actually comes from, which was not where it looked

It reads as a glow on the wrong side of a wall. It is not: amplified twelve
times it is uniform salt-and-pepper across the whole frame, and it enters
**entirely through the bounce terminator** -- disabling the field in
`ShadeTraced` alone takes 3.591 to 0.000. Rays from all over the dark room
occasionally land within a cell of the divider, read a lit cell, and spray that
sample into a random pixel.

A volume covering only the dark half reads 0.000, which is what rules out the
field inventing light and pins it on the lit cells next door.

### Converging, and why the sweeps do not feed on themselves

A solve is amortised: a band of rows a frame, budgeted at about 2048 cells, so a
large field arrives over several frames instead of as one hitch when a scene
loads. It sweeps four times, each pass blending into what the last one left, so
what accumulates is a mean over more rays than one pass can afford.

**The sweeps deliberately do not read the field.** Letting them was tried, and
it is seductive: each sweep terminates its rays in the last sweep's answer, so
the light gains a bounce a sweep, and it measured as the best quality of
anything here -- 0.641. It also multiplies every mistake by the same loop. The
sealed room went from 0.15 levels to 6.3, and the 2 m case from 3.4 to 12.4,
because five per cent of a leak fed back eight times is not five per cent. The
bounce the field terminates belongs to the *frame*, which reads a field nothing
is writing; `RV_IRRADIANCE_FILL` is the guard that says so.

### And the same test, spent where it is worth spending

Honouring the stored visibility costs the hardware's trilinear filter -- the
eight cells have to be weighted one at a time, at four fetches each instead of
three for the lot. Measured with it in both readers: **+0.61 ms on a 1.5 ms
frame**, which is more than a second traced bounce costs and buys a fraction of
what one delivers.

So the lit pass takes the careful road and the bounce takes the cheap one. The
lit pass reads the field only where the screen-space gather has no answer, which
is a small part of a frame; the bounce reads it at every hit.

| | GPU frame | error vs 2-bounce | sealed room, 1 m / 2 m / lamp |
|---|---|---|---|
| one bounce | 1.535 ms | 1.483 | 0 / 0 / 0 |
| **+ the field** | **1.591 ms** (+0.055) | **0.720** | 0.000 / 3.424 / 0.000 |
| + strict test in both readers | 2.093 ms (+0.55) | 0.767 | 0.000 / 3.216 / 0.000 |
| two bounces | 1.938 ms (+0.45) | 0 by definition | 0 / 0 / 0 |

Half a second bounce's quality for an eighth of its cost, the solve happening
once, and a lamp on the far side of a wall now reading exactly black where it
used to read 0.147.

### Rotation, which is no longer ignored

A volume's transform carries three columns, and their directions are as much a
part of the box as their lengths. The scene walk normalises them apart -- extents
from the lengths, axes from the directions -- and the axes go to the shader as
the three rows that take a world vector into the box's frame. Everything is then
asked in that frame: which cell, how far, which way, and which octahedral bin,
because a bin written against world directions would be the right number filed
under the wrong heading the moment a volume was turned.

`scenes/irradiance_field_turned.rage` is the check: the same corner covered by a
box turned thirty-five degrees, which lights it the same way (0.684 against
0.720, the difference being where its cells land rather than what they hold).
Turning a volume invalidates its field, as moving one does.

### What 1.2 still owes

- **The 2 m leak, which is the one thing four versions of the visibility term
  did not close.** 3.4 levels of 255 in a sealed room beside a lit one, at cell
  spacings coarser than the wall is thick; 0.000 at the default one-metre
  spacing and at three. The rule to give an author is that cells want to be
  smaller than the thinnest wall they straddle. Closing it properly is probe
  relocation -- moving a cell that sits badly rather than describing where it
  sits -- which is the piece of DDGI this does not have.
- **Nested and overlapping volumes**, which BAKING-ROADMAP 1.2 scopes out and
  which is where this ends up: a coarse field over a level and a tight one in
  the room that needs it. Choosing between them per fragment is a second
  question.
- **Nothing persists.** Every launch re-solves, and the OpenGL backend never
  solves at all -- the fill traces rays and there are none there. That is item
  3, and it is what would bring the field to a backend that cannot bake it.

### The largest error either form ever had, and it was not the field's

`TraceSurface` shaded every hit with **a shadow ray for the sun alone**. Point
and spot lights lit every traced hit through walls, floors and closed doors --
and the bounce reads those hits, so the light came back into the room as
indirect. On the leak scene with the sun swapped for a lamp next door, a
**sealed** room read **165 levels of 255** where the answer is black.

It now traces a shadow ray for local lights too, bounded by the distance to the
light rather than by the sky. Same scene: **0.147**.

**What it costs, and the decision it deserves:** +0.49 ms on the showroom
(6.421 ms against 6.907, three interleaved pairs agreeing within 0.014), and it
changes **555 pixels of 921,600** there -- because that scene is lit by area
emitters, which next-event estimation already shadows. So it is 7.6% of the
showroom's frame for nothing the showroom can see, and the difference between
light and light-through-walls in any room lit by a lamp. It is on; putting it
behind a render setting is a line of work if the trade is not wanted.

This matters more for a *stored* field than for a realtime bounce: a field
solved in a lamp-lit room would have baked the leak in and held it.

### The defect this found, and the fix: one texture, three tiles

**`30e15bb` took `pbr_layered` over OpenGL's sampler limit and terrain rendered
black.** A fragment shader gets thirty-two combined samplers there. That variant
is the widest the engine has -- twelve shadow maps, twelve layer maps, and the
six every lit shader needs, which is thirty -- and the field's three channel
volumes made thirty-three: `error C7612: profile doesn't support more than 32
samplers`, no layered pipeline, a black frame wherever terrain is drawn.

**The field is now one texture with three tiles stacked along z** -- red, then
green, then blue, each `Depth` slices tall -- so it costs one sampler and the
variant sits at thirty-one. Three fetches still, one per channel, and the
hardware still does the trilinear blend.

**Why the tiles are safe, and it is the texel-centre mapping that makes them
so.** The lookup was already mapping into texel centres, because the grid's
first and last samples sit *on* the box faces and a straight 0..1 mapping puts
half a cell outside the outermost centres. That same centring means the filter's
reach at either end of a tile is the tile's own first and last slice and never
the channel stacked beside it: at the last centre the weight on the slice beyond
is zero. Sampling in plain normalised coordinates would bleed red into green at
the seam. Measured: the field's frame is identical to the three-texture version
to within a level, its error against the two-bounce reference is the same 0.735,
and the leak table is unchanged.

**And a check, because the suite was green while terrain was black.**
`CheckSamplerBudget` in scenetest counts the samplers the layered variant
declares and asks the device to build it. The count fails on **either** backend,
which is what matters -- the defect was introduced and lived on Vulkan, where
nothing enforces the limit -- and the build is the half only OpenGL can answer,
since the refusal comes from the driver compiling the cross-compiled GLSL.
Verified by reintroducing the defect: both backends fail, and the message names
the count.

One consequence worth keeping: the depth term this field still owes wants a
sampler of its own, and there is now exactly one to spare.

---

## Start here: the showroom has two lighting modes, and a switch for them

**2026-08-24.** The owner's original idea for this scene was a car in a dark
room under one spot; what got built was a delivery bay under a ceiling of
light. Both now live in the one scene, on a pill at the right-hand end of the
credit bar, left of the light switch.

| | |
|---|---|
| **Mode 1 -- the studio** | The default. Dark room, four cells of the ceiling lit over the car, one fill under them, the service bay out. |
| **Mode 2 -- the showroom** | The scene as it was: full luminaire, nine fills, kickers, bay lit. |
| **`ShowroomMode.cs`** | Nine lights, four lights, two lights, four emissive lenses and one material. |
| **A second fitting** | `showroom_panel_studio.rmat` -- the same grid, four cells lit. |
| **Two engine fixes** | An enum set from a script compared *pointers*; area emitters read the material's emissive, not the entity's. |

### The three things worth carrying forward

1. **A mode is a different fitting, not a dimmer.** Dimming the whole panel
   was the first attempt and it is the wrong picture: a ceiling that is
   uniformly dull reads as a light nobody switched off, and dimming it far
   enough to read as *off* takes its albedo down with it -- at which point the
   car has nothing to reflect and goes flat, because car paint is almost all
   specular. Shrinking the panel to a square over the car is the other trap:
   at a 40 degree field the ceiling is only in frame beyond z = -1.3 m, so a
   panel centred on the car sits behind the camera's top edge and the mode
   loses its source altogether. What works is two materials over one mesh --
   same size, same mullions, same normal map, four cells lit instead of all of
   them -- swapped through `MeshComponent::Material`, which is an ordinary
   asset field a script can write by path.
2. **The area-emitter list was reading the material, not the entity** (fixed
   in `Scene.cpp`). A per-entity emissive override is how a light is dimmed or
   switched -- this mode uses one for the bay's lenses and the light switch
   already used one for the car's lamps -- and next-event estimation ignored
   all of them, so the traced bounce lit the room from a value nothing on
   screen was emitting. It also put the two halves of the estimator into
   disagreement: the ray-instance walk *does* resolve overrides, so the
   hemisphere term subtracted the emissive a surface really had while NEE
   added the one it used to. Mode 1's paint speckle was that double count;
   fixing it cut the noise 44% (5.68 to 3.20 on the bonnet) and took the
   room's walls from 13 to 6 of 255. Mode 2 is bit-identical either way, which
   is worth knowing before blaming it for anything: measured with the change in
   and out, the same frame twice.
3. **An enum written from a script never matched its own name.**
   `ComponentFieldFromText`'s enum branch compared `const char* text` against
   `const char* EnumNames[i]` with `==` -- two pointers, never equal -- so
   every enum a script set by name fell through to the ordinal parse, which
   fails on a word and leaves **zero**: the first enumerator. And it returned
   `true`, so the caller was told it had worked. `SetComponentField(...,
   "Update", "Realtime")` therefore meant `Baked`, silently, in both script
   languages and for every enum in the bridge.

   It surfaced as *noise*, which is why it is worth the retelling. The probe
   would not re-bake, so mode 2 was lit by the cube map captured during the
   **loading frames** -- 47 of them, drawn before any script runs and
   therefore under the scene exactly as authored, which is mode 1 and dark.
   The room's bounce collapsed (walls 55 to 21 of 255) and the traced GI's
   speckle stood out against the darker ambient. Nothing about "the car looks
   grainy" points at an enum comparison, and two wrong diagnoses came first:
   the emitter fix below, and a re-capture window that was counted in frames
   rather than held long enough. Post-fix mode 2 measures *less* speckle than
   the scene did before this session (3.18 against 3.27), because the probe
   now bakes at runtime under converged lighting instead of during the cold
   loading frames.

4. **A second control on the credit bar breaks the licence notice at 4:3.**
   The text centres in whatever rect it is given, and the rect ran the full
   bar -- fine at 16:9 where there is 190 units of slack, and at 4:3 the
   canvas is 1663 units instead of 1920 and the notice ran under both pills
   with the URL cut in half. The switches' width is now taken out of the
   notice's box. This file already warned that 4:3 is the aspect to check if
   either number moves; the number moved.

### Verified

Both modes rendered and measured; `check_gpu_lit`, `check_gi`, `check_oit`,
`check_bindless`, `check_depth_sort` (71x floor), scenetest on both backends
in Release **and** Debug, rhismoke. The credit notice reads in full at 4:3 and
16:10 with both switches clear of it.

**Not verified by a test, and worth knowing:** the click itself. The button is
wired exactly as the light switch beside it -- `UIButtonComponent` with
`OnClickMethod: Toggle` and the script on the same entity -- and both branches
of `Apply()` are verified by rendering with `StartMode` at 1 and at 2, but
nothing here drives a pointer at it. `scenetest`'s demo-button case
(`CheckDemoButtons`) has the harness for that -- `UI::UpdatePointer` plus
`centreOf` -- against script *graph* handlers; pointing it at a managed script
is the obvious next step for anyone who wants this covered.

---

## Start here: texel emitters landed, and a design document is owed

**2026-08-24, last session.** The traced bounce now aims its shadow rays at
the *lit texels* of an emissive map instead of radiating the whole mesh
evenly. Merged to `main`. Everything about it -- problem, idea, the three
stages, every measurement, the pictures and the mistakes -- is in
**`docs/TEXEL-EMITTERS.md`**, whose last section ("For the write-up") was
written for the next job.

### The next job, which the owner has already asked for

**A design document, as a PDF.** They want it to explain the feature *the easy
way* while carrying the real technical content, and to **use the before/after
pictures**. Their headings: problem statement, idea, implementation. The
material is all gathered:

- Narrative, numbers and the honest caveats: `docs/TEXEL-EMITTERS.md`,
  section "For the write-up".
- Pictures, already in the repo: `docs/images/texel-emitters/`
  -- `before-phantom.jpg` (main, one textured mesh: the whole ceiling
  glows), `after-aimed.jpg` (same scene, same frame, this feature),
  `before-workaround-split-mesh.jpg` (what shipping the correct picture used
  to cost in scene surgery), and three `fixture-*.jpg` from the purpose-built
  test room.
- The one-line summary if a reader takes nothing else: **the branch reaches
  from plain authoring what previously took surgery on the scene** -- grain
  2.31 against the workaround's 2.34 and the phantom's 3.31, crawl 11 levels
  against 91.

Not started. Nothing else is pending on it.

### What is genuinely open, and small

- **Five per cent of frame time has no nameable home.** 7.13 ms before, 7.50
  after, nine interleaved pairs in both orders, so it is not this laptop's
  drift. But the RT GI pass did not get slower (3.99 -> 3.80) and a
  pass-by-pass diff put the two builds within 0.05 ms. The owner has taken it
  as a later question; it wants a GPU capture rather than more benchmarking.
- **The showroom's split fitting is now dead weight.** `Luminaire Lit Block`,
  `showroom_panel_block.rmat` and `showroom_panel_off.rmat` exist only to make
  the emitter list's one-rectangle-per-mesh assumption true, and the
  acceptance test above proved the assumption no longer needs helping. It was
  deliberately left in place so the merge carried only the engine change; it
  can go on its own merits, and the pre-workaround generators are at
  `cbc9529^`.
- Stage 2 engages only for Plane and Quad primitives with untransformed uv.
  A modelled fitting or a tiled material falls back to stage 1's even
  radiance -- correct, only average. Extending it needs the mesh to retain uv
  for small meshes; the reasoning is in TEXEL-EMITTERS.md.

### And one habit to carry, because it cost three repeats

`grep -E "gi_"` matches **rt`gi_`trace.rvshader**. Reverting fixture churn
with a pattern like that quietly reverted the shader half of a change three
times, and the checks stayed green each time because the runtime compiles the
shaders *staged beside the exe*. `rvcheck.require_current_shaders` is in every
check for this reason -- trust it, and never hand-roll a revert filter over
shader paths.

---

## Start here 2: mode 1's grain was a phantom light, and the fix is a mesh

**2026-08-24, the session after the modes landed.** The owner reported heavy
grain on the car in mode 1 -- crawling, camera still. The chain, because each
link was measured: the grain was the traced GI (speckle 3.33 against 2.32
with `--rt-gi=off`), it never settled (consecutive frames apart by up to 92
levels at rest; mode 2's figure is 9), and pulling the ceiling out of the
NEE emitter list removed both the crawl **and half the car's light**.

That last measurement is the diagnosis. The emitter list takes a mesh's
emissive as **uniform over its bounds rectangle** -- "exact for a plane,
which is what every light fitting is" (7cc). Mode 1 lit four cells of the
7.8 x 11 m panel through its *texture*, so hemisphere hits saw four cells
(they sample the emissive map) while NEE sampled the whole rectangle at 4.7:
a ceiling-sized phantom light, feeding the car twice the light it should
have had, with the enormous sample-to-sample variance of a huge rectangle --
which the 0.9 accumulation turns into a permanent crawl rather than a
converged answer.

**The fix is authoring, not engine**: the lit two-by-two block is its own
mesh (`Luminaire Lit Block`, `showroom_panel_block.rmat` -- the panel maps
tiled to a 2x2-cell window), 5 mm proud of the big panel, always lit. The
big panel swaps between fully lit and a new all-dead
`showroom_panel_off.rmat` whose scalar emissive is exactly 1.0 --
deliberately not above it, so it stays out of the emitter list (`strength >
1`). In mode 2 the block vanishes into the identical cells of the lit panel
behind it. **One mesh per differently-lit region is the NEE contract**; a
texture that masks most of an "emitter" breaks it silently.

Post-fix: mode 1 speckle 2.34 (the GI-off floor is 2.32), temporal max 7
(floor 6); mode 2 unchanged. Mode 1 is darker than it was -- the phantom's
light is gone -- which is the reference's own look: a black room and one
fitting. If it ever wants lifting, the levers are the key light and the fill
under the block, not the ceiling.

---

## Start here: the gpu-lit defect is dead, and the references were the broken ones

**2026-08-24. Found, fixed, falsified, committed.** The 1,643,738-pixel
disagreement between `--gpu-lit` on and off was **two independent defects,
both from 2026-08-22's transparent-mesh work, both in `Renderer3D.cpp`** --
and the investigation's central assumption was backwards: the CPU path (and
the meshlet and bound paths, which share its submission code) was the wrong
picture. The gpu-lit path's opaque draws were right all along, and it was
separately broken on glass.

### Defect 1: the run reorder ignored the transparent bit (the 1.6M pixels)

EndScene's packed sort key carries `Transparent` at bit 63, so blended draws
sort to a contiguous block at the end -- and `TransparentBegin`, the point
where the opaque issue loop stops and the transparent pass takes over, is
simply "the first transparent draw". But the **run reorder** added for 7.8's
front-to-back ordering (2026-08-13, before any mesh could be transparent)
re-sorted the runs by `(Kind, Nearest)` and knew nothing of the partition.
The car's glass, nearer than the walls, sorted ahead of them, and **every
opaque run farther than the nearest glass -- walls, portal piers, header,
mirror, charge post, the whole service bay -- was handed to the OIT
pipeline**: no depth write, so depth of field blurred them as "far"; no
g-buffer, so SSR/SSAO/SSGI never saw them; weighted-blend shading where
opaque was authored. Fix: the reorder compares `Transparent` first, and the
run cut breaks at the boundary too (under bindless every material's batch
key is zero, so the last opaque and first blended run of one mesh would
otherwise merge into a single run = single pipeline).

Two knock-ons made it look stranger than it was:

- **Probe captures render through the CPU walk** (a probe face is never
  `sameCamera`), so the baked showroom probe was itself rendered with the
  bug -- every path's IBL, gpu-lit included, was lit by a poisoned cube map.
- `--meshlets=on` and `--bindless=off` share the same EndScene partition,
  which is why all three "references" agreed with each other byte for byte.
  **Agreement between siblings that share code is not a reference.** The
  gpu-lit indirect path was immune, which is exactly why it disagreed.

### Defect 2: the blended table's indirect loop mis-strode (the glass)

`GpuCull::CullLit` writes 24-byte `SlotCommand` rows (the draw command plus
`InstanceBase`) for **both** the opaque and blended tables -- one cull
shader writes both. The opaque and shadow loops step by
`sizeof(GpuCull::SlotCommand)`; FlushTransparent's GPU loop stepped by
`sizeof(DrawIndexedIndirectCommand)` (20), so slot 1 read from the middle of
slot 0 and **only the first blended mesh ever drew on the gpu-lit path**:
a windscreen, and no headlamp glass, grille or side windows behind it.

### What the last session got wrong, so the next one doesn't repeat it

- The "portal pixel" (1280, 300) that named the gpu path's "white-based
  interloper" is the **convex chrome mirror** -- the gpu path drawing it
  correctly. Decode locations against the scene before naming a culprit.
- The BREAKTHROUGH's coplanar-panel / z-fighting hypothesis is falsified:
  the generator builds single dark wall boxes, nothing coplanar. The CPU
  wall pixels failed the row-probe decode because the OIT pipeline (not the
  instanced opaque fragment) wrote them.
- "The CPU path is the honest reference" -- backwards, see above. The +1 ms
  cost gap died with the fix too: interleaved 3-pair A/B now reads 7.09-7.11
  (on) vs 6.88-7.08 ms (off) whole-frame GPU on the showroom.

### Verified, and guarded

- Showroom, committed profile, 2560x1600 frame 60: 1,643,738 px max 215 →
  **373 px max 1 level**, all in the car glass = weighted-blend accumulation
  order between slots (atomicAdd vs submission order; float addition is not
  associative). That residual is the only thing left of the old "unordered
  transparent table" observation -- OIT needs no inter-slot order.
- Under `--render-defaults=on --aa=none`: gpu-lit on/off, `--meshlets=on`,
  and `--depth-sort=off` are **bit-identical** (the UI bar excluded: the
  LIGHTS ON button's hover highlight follows the real mouse).
- **`tools/scripts/check_gpu_lit.py`** (new) asserts that parity and fails
  loudly with either bug reintroduced -- measured 73%/9.2% of pixels. Both
  falsifications were actually run. `check_bindless.py` is an implicit
  second guard (bindless off flips the whole submission family, zero
  tolerance) and passes again.
- scenetest 2390/2390 Vulkan + OpenGL, rhismoke both backends, check_oit
  4/4, check_depth_sort pixel-exact 0 of 4.3M.

### Fixed in the same pass (found by the adversarial review of the fix)

- EndScene's empty-frame early-return now counts the **blended** table as a
  frame, not just the opaque one -- a scene of nothing but glass used to
  skip the uploads FlushTransparent draws from (the "empty pending list"
  lesson, learned again by its twin).
- `DrawSkinnedMesh` now sets `Transparent` from the material (the scene and
  the renderer used to disagree: OIT attachments allocated, mesh drawn
  opaque, shadows skipping it as glass) and carries `ViewDepth` like the
  other submission paths. The transparent pass's skip of non-static blended
  runs now warns once, as its contract comment always claimed it did.
- Both flush merges cut runs on `Kind` (under bindless, material keys are
  all zero and only the mesh pointer separated pipelines at a kind
  boundary).
- FlushTransparent drops an armed indirect view it cannot draw instead of
  holding it across frames (a held view replays a dead camera's cull).

### The three follow-ups above, closed the same day

All three "still open" items from the first pass were fixed later on
2026-08-24; what follows is what each turned out to be.

- **The early-z floor was dead because its worst case had quietly become
  its best case.** `make_overdraw_scene.py` emitted slabs near-to-far, and
  "unsorted" draws in submission order -- which EnTT used to scramble
  (arbitrary iteration) and 10.2's sparse-set ECS preserves (insertion
  order). The day EnTT left, the unsorted case became perfectly sorted and
  the floor read 1.0x forever. Measured: 0.62 ms "unsorted" as committed,
  24.2 ms with the same slabs reversed. The fixture now emits **far to
  near** -- worst case as a property the file guarantees, not one an
  iteration order happens to provide -- and the floor reads **66x** (0.35
  vs 23.4 ms). Second layer: the check ran without `--gpu-lit=off`, and
  the indirect path never consults `--depth-sort` (its order is the cull's
  atomicAdd), so both halves compared a flag-independent path to itself.
  Every run in the check now pins `--gpu-lit=off`. Third layer, found by
  the pinning: the pixel-exact half screenshotted an **unpinned warm-up
  frame**, where the glass's legitimate 1-2 level OIT float-association
  jitter amplifies through TAA/SSR/DoF history into hundreds of levels --
  2,598,126 subpixels unpinned, 0 at `--aa=none --screenshot-frame=60`.
  Falsified end to end: `falsify.py sort-writes-depth` (early-z killed by
  a depth write) takes the floor to 1.0x and exit 1; restore, 67x, green.
  (An earlier note here claimed the perf FAIL "exits 0" -- wrong: `$?`
  after a pipe reads the tail's status. The script always exited 1.)
  One number the exercise produced for the roadmap's "depth sort still on
  the CPU" line: on the reversed fixture the gpu-lit path pays 6.3 ms
  whatever the flag says, against the CPU path's 1.17 sorted.
- **VoxelGI no longer voxelizes glass as a solid caster**: the caster walk
  skips blended materials, mirroring the shadow walk's "glass casts no
  shadow" rule for the same reason -- a windscreen that blocks the bounce
  puts the cabin the shadow rule keeps lit into indirect darkness.
  `check_gi.py` stays green end to end (its fixtures are opaque walls).
  One asymmetry, accepted and recorded: skipping a blended caster also
  skips its *emission* from the voxel bounce (injection only lights
  occupied voxels), while the traced form keeps blended surfaces as area
  emitters -- a blended emissive lens now bounces under one GI form and
  not the other. No committed content has one; the day some does, the
  voxelizer needs an emissive-only injection, not a solid caster.
- **`DrawIndexedIndirect` lost its default arguments** -- base interface
  and both backend overrides (defaults on virtual overrides bind
  statically through base pointers, a second trap in the same signature).
  The natural default stride is wrong for every arguments buffer the
  engine fills, and a defaulted stride was exactly the bug that dropped
  the blended table's glass. Callers now state their layout.

### How to reproduce the old defect's shape (for regression archaeology)

```
RageVRuntime.exe --project=SampleProject --scene=scenes/showroom.rage --rhi=vulkan   --render-defaults=off --width=2560 --height=1600 --frame-time=0.0166   --screenshot-frame=60 --screenshot=a.png            # gpu-lit (default)
... --gpu-lit=off --screenshot=b.png                   # CPU submission
```
Diff a against b: ≤ a few hundred pixels at 1 level is the OIT float floor;
anything more means a submission path regressed -- run
`python tools/scripts/check_gpu_lit.py` for the calibrated verdict.

---

## Start here: the frame was audited, and where it goes is now written down

**Updated 2026-08-23, evening session (Fable).** Everything below was measured
at 2560x1600, Release, Vulkan, RTX 5070 Ti Laptop -- interleaved A/B always,
because this machine drifts +/-1 ms thermally and single runs lie.

### The audit: where a frame goes

| pass | showroom | camp | demo | verdict |
|---|---|---|---|---|
| RT GI trace | 6.8 | 9.6 | 2.1 | the big one; see below |
| Scene (lit, traced shadows) | 4.7 | 5.0 | 2.6 | proportionate for the feature set |
| RTAO (runs under "SSAO compute") | ~~3.5~~ **2.4** | ~~5.5~~ **3.7** | 2.2 | 12 rays/px was oversampling the 9x9 blur; now 8 |
| everything else combined | ~1.3 | ~1.3 | ~1.3 | bloom, DoF, motion, tonemap, AE, denoise -- all tight, leave alone |
| Voxel GI gather (GL only, 128^3 x2) | -- | 3.5 | -- | only runs where rays cannot; fine |

### What landed this session (three commits)

1. **29ffe8d -- the white spots near the showroom's ceiling.** The NEE emitter
   term had no firefly clamp: radiance x area / d^2 against a partially
   occluded panel is a lottery, and whole regions flashed 90 display levels
   for one frame. Bounded at 8x the hemisphere clamp (32), plus a
   neighbourhood bound on the denoiser's current frame. Flicker pixels over
   the visibility line: 129 -> 0; settled energy kept to 62.6 of 63.1.
   check_gi's settle claim gained an absolute backstop because the fix makes
   the *first* frames almost as quiet as the last.
2. **407e6ec -- RTAO 12 -> 8 rays.** Identical noise metric after the blur,
   0.005 mean-level difference, saves 1.1-1.8 ms. Literature budget is 1-2
   rays with temporal accumulation, so the next step down exists but wants a
   temporal stage for AO first.
3. **471ad70 -- the reach dial verdict.** Built, measured as pure noise at
   both resolutions, removed. The "+1.2 ms camp regression" was thermal drift
   measured sequentially. GiRadius's inspector row stopped claiming the traced
   form obeys it.

### The decision waiting on the owner: RTGI at Medium

`RayTracedGlobalIllumination: High` traces 4 rays/px at full resolution.
Metro Exodus ships 0.25 rays/px; diffuse GI is the lowest-frequency signal in
the frame. Measured, interleaved, Medium (half-res, 4 rays) against High:

- showroom: trace 6.8 -> 1.8 ms, **frame 17.4 -> 11.6**
- camp: trace 9.6 -> 2.5 ms, **frame 22.1 -> 14.5**
- quality: means identical to three decimals, 1.76% / 0.18% of pixels differ
  by >2 levels, local-noise metric unchanged; the visible risk is silhouette
  edges, where the accumulated buffer upsamples bilinearly.

Not flipped, because the project's render settings are the owner's. If
adopted, re-run the High/Medium image diff first -- the numbers above predate
the firefly fixes -- and the right companion work is a bilateral (depth-aware)
upsample where the lit shader reads u_Indirect, which removes the silhouette
risk entirely.

### The research shelf, in order of value per effort

1. **Temporal accumulation for RTAO** (Microsoft D3D12 denoised-AO sample):
   enables 2 rays/px at full res, ~1.5 ms more back.
2. **Bilateral upsample of u_Indirect** -- small shader change, unlocks Medium
   GI as a safe default.
3. **Spatiotemporal blue noise** (Wolfe et al.) for ray direction seeds --
   same cost, visibly steadier under the accumulator; needs a texture binding.
4. **ReSTIR GI** (NVIDIA 2021): 9-166x MSE at 1 spp. The endgame; a
   reservoir buffer pair, temporal + spatial reuse passes, and careful bias
   handling. Weeks, not days.

### Traps confirmed this session

- **The import cache serves stale post profiles.** Editing a .rvpostprofile
  without touching its .meta means the engine keeps rendering the cached
  values -- a probe at reach 0.05 m rendered bit-identical to 250 m until
  `--import-cache=off`. Any profile-edit A/B without that flag is void.
- `--render-defaults=on` silently swaps traced GI for the voxel form (7ba) --
  an identity check under it compares two runs of the wrong feature.
- "Half" is not a RayDetail value ("Off/Low/Medium/High"); a wrong name in
  the project file parses as Off and the pass quietly vanishes from the graph.

### The close-up spike, measured

The owner's report -- the frame rises ~16 to ~20 ms near the car, on Vulkan --
reproduces exactly: orbit Distance 7.3 vs 4.6 (the zoom clamp), interleaved,
17.5 -> 20.6 ms. The growth is two passes and only two:

- **Scene: 5.15 -> 7.16 ms.** The car's pixels are the expensive pixels --
  smooth paint earns a traced mirror ray per pixel, whose hit runs the full
  20-light loop with a sun shadow ray, on top of per-light traced shadows for
  the surface itself. Close up, four million of them.
- **RTAO: 2.43 -> 3.57 ms.** Rays starting on the car probe its own
  490k-triangle BLAS instead of the room's sparse walls.

RT GI, transparent, DoF and everything else hold flat. So it is content cost,
not a defect: the most expensive material fills the frame. It is already ~2 ms
better than it was at twelve AO taps, and the Medium-GI decision above would
take the close-up view to roughly 15 ms. The remaining code lever worth
having: range-cull the 20-light loop at traced hits (a hit shades all twenty,
attenuation-zero or not), which trims exactly the pass that grows here.

---

## The session before: the showroom has a light switch, and scripts can read hover

**Updated 2026-08-22 (third session).** Work on `main`, which is clean and
**ahead of what has been pushed**. Pushing is the owner's.

A small session on top of the showroom, and everything in it came from the
owner looking at the picture: a black bar behind the attribution, a switch for
the car's lights, and a camera bug they found by dragging the orbit into a
corner of its own range.

| | |
|---|---|
| **The credit bar** | A full-width black band along the bottom, the notice centred in it. |
| **A lights switch** | Grey pill at the right-hand end; headlamps and tail lamps, on and off. |
| **Protocol 10** | `IsUIButtonHovered` — a script could not read hover in *either* language. |
| **`ShowroomLights.cs`** | The switch. Emissive on the lamp meshes, intensity on four spot lights. |
| **A camera fix** | Full zoom plus full pitch put the camera through the wall. Black frame. |
| **Target focus** | Depth of field can name a subject and solve the distance *and* the aperture from it. |

2321 checks on Vulkan and on OpenGL, both green.

### The three things worth carrying forward

1. **A lamp is two things.** Emissive makes the surface bright and feeds bloom
   and **lights nothing** — there is no emissive GI. The pool on the floor and
   the red in the service bay are four ordinary spot lights, authored at zero
   intensity and raised by the script. Tuned separately, and neither is
   optional.
2. **The protocol version lives in two files.** `Managed::Interop::kProtocolVersion`
   and `RageV.Interop.ProtocolVersion`. Bumping only one is not a build error —
   it is `protocol mismatch` at load and every C# script in the scene silently
   doing nothing.
3. **Two clamps that each hold alone need not hold together.** The camera
   clamped for the ceiling and for the walls, and the ceiling clamp works by
   flattening the pitch — which reaches further along the ground than the wall
   clamp was computed against. Order matters, and the failure needed both
   extremes at once, which is why it survived a whole session of use. (7cd)

4. **A focus distance is a distance to a subject.** The showroom's was a
   number in a file equal to the orbit's starting radius, so the car went soft
   the moment you leaned in. `Focus: Target` names the car instead. **Both**
   numbers are solved — the plane from where it is, the aperture from how deep
   it is — because putting the plane on a subject fixes *where* the sharpness
   is and not *how much* of it there is. (7ce)
5. **A defaulted parameter is a decision nobody makes.** `DrawFields` let
   `DrawField`'s `Scene*` default to null, which was correct at every call site
   until a settings block gained its first `EntityRef` — and then the slot drew
   "Missing entity" against a perfectly good reference, above an empty
   dropdown. Two symptoms, one default, neither pointing at it. The entity
   drawer now has a fourth state for "no scene", because the old one made a
   specific claim from a position where it could not check. (7ce)
6. **A fallback that renders the right picture can still be the wrong
   answer.** A post profile is an asset and a focus target is an entity, so a
   shared profile names a UUID the second scene has never heard of. That case
   falls back to `Manual` — the *mode*, not just the same numbers, because the
   same picture with a worse description leaves the next person debugging a
   mode the frame is not in. Following that through found what the picture
   could not show: the inspector greyed the two rows that would fix the shot,
   with a note saying a target was answering them, directly beneath that
   target drawn in red as missing. (7ce)

### Where the showroom's numbers live

`tools/scripts/make_showroom_scene.py` and `SampleProject/Scripts/ShowroomLights.cs`
**both** carry the lamp colours and intensities — the generator writes them into
the scene as script fields, and the script's defaults are the same values so a
scene without them still behaves. Change one, change the other.

The lamp positions are not guesses: they came out of the glTF's `POSITION`
accessor bounds, walked through the node transforms
(`EXT_Emissive_Light_Front` at z 1.64..1.86, the tail lamps at -2.17..-1.64).
The same walk showed `EXT_Glass_Emissive_Front` *does* have a box over the
headlamp aperture — so the missing lens 7ca went looking for is in the file
after all, which is worth knowing before anyone adds one.

**Do not match the car's lamps by material.** Nine meshes wear
`AUX_LIGHT_Porsche992` and only three are lights; the rest is roll cage, roof
trim and the front number-plate panel. Matching the material lights the cage.

---

## The session before this one: 8.9 closed, and the engine can draw glass

**2026-08-22 (second session).**

The day closed roadmap **8.9** — the last ordinary feature in phase 8 — and then
built a showroom scene, which turned into a renderer feature and five real
defects because the car in it needed things the engine did not have.

### What is new, in the order it was built

| | |
|---|---|
| **8.9 stage 2** | FBX skinning and animation. Phase 8 has no ordinary feature left. |
| **`tools/rvimport`** | A model instantiated into a scene file without the editor. |
| **Transparent meshes** | The renderer had no path for them at all. |
| **A second cull table** | Blended geometry is culled and drawn on the GPU, CPU as fallback. (Until 2026-08-24 its indirect loop mis-strode 24-byte SlotCommand rows and only the first blended mesh drew -- "Start here" above.) |
| **Next-event estimation** | The traced bounce aims at emitters instead of finding them by luck. |
| **A spatial GI filter** | The denoiser had never had one. |
| **`scenes/showroom.rage`** | The project's start scene. A Porsche under a ceiling luminaire. |

2282 checks on Vulkan and 2235 on OpenGL, both green.

### The showroom, and how to regenerate it

Three steps, and the middle one takes a minute:

```
python tools/scripts/make_showroom_textures.py
build/bin/Release/rvimport/rvimport.exe models/showroom/porsche_992_gt3_r.glb \
    --out=tools/scripts/data/showroom_car.yaml
python tools/scripts/make_showroom_scene.py
```

**The car is not generated and must not be.** A model with eighty-nine materials
is three hundred and thirty-seven entities, each with a mesh handle and a
`.rmat` that only exist once something has imported it — so `rvimport` runs
once, its output is committed as `tools/scripts/data/showroom_car.yaml`, and
regenerating the room does not renumber three hundred entities that did not
change. Re-run `rvimport` only when the model or the importer changes.

**Eighty-five of its hundred and fifty-one draws are re-authored** by the
generator. That is not a hack, it is the scene doing its job: the model's
uploader set **every material in the file to roughness 0.9577**, glass and lamp
lenses included. At 0.96 a dielectric's specular lobe has no peak, which is
visually identical to having no specular at all.

### Five defects the scene found, and every one was general

Each of these was in the engine, not the scene, and each presented as something
else:

1. **A saved scene lost every part of a multi-material model but one.** Mesh
   handles inside a model are `modelHandle + 1 + index` and only the model's own
   handle resolved. Reopening a scene silently dropped the rest. (7ca)
2. **The transparent pass only existed when the scene had particles.** Both
   layers gated it on weighted emitters, so the car's glass was never drawn —
   and *a windscreen that is not drawn and one that is perfectly transparent are
   the same picture*, which is why four fixes in a row appeared to do nothing.
   (7cb)
3. **A stale cooked mesh failed hard instead of re-importing.** A `MeshCook`
   version bump made every cached model answer "produced nothing". Now it falls
   through to the source. And **`ImportCache::kVersion` has to move with it** —
   bumping only the first left every load reading a v1 file, refusing it,
   re-importing, and never writing the result anywhere. (7cb)
4. **The reflection probe was inside the car.** At the car's centre, so it
   photographed a black cabin. Invisible on Vulkan, where traced reflections
   replace the probe; dead on OpenGL, which has neither ray queries nor a
   g-buffer for SSR to trace a blended fragment from.
5. **Blended surfaces were in the ray-tracing structure.** Every ray treats it
   as opaque, so glass stopped shadow, bounce and reflection rays alike — the
   cabin and every lamp interior were unlit. It looked like a backend
   difference: OpenGL fell back to shadow maps, whose resolution *leaked* light
   into the cavity the exact test correctly refused. **The one that looked wrong
   was the one that was right about its own inputs.**

### The lesson that would have saved the most time

**A change that produces no change is worth more than another hypothesis.**

Defect 2 survived four rounds of fixing — material roughness, specular, the
coverage maths, a descriptor set — because every one of them was reasonable and
none of them could work. What settled it in one render was forcing the
transparent shader to emit flat red and watching the glass stay grey. That
partitions the problem; another hypothesis only explores it.

The same shape appeared twice more the same day: the GI emitter list handed over
in `RenderShadows`' submit branch, where it compiled, ran on some frames, and
left the count at zero on the ones that mattered — three renders were compared
before anyone printed the count.

### Where the remaining known problems are

- **The headlamp has no lens mesh.** Settled by tinting each candidate material
  a different colour and rendering once: the LED elements appear, the roof light
  bar appears, and `EXT_Glass_Emissive_Front` never appears on the lamp. The
  aperture is open geometry. What is there now is a glossy reflector standing in
  for a cover — 0.22 roughness, 0.62 metalness. **Adding a real lens means two
  blended entities placed over the apertures from the scene**, which risks
  clipping through the curved wing and wants iterating on with the owner.
- **GI noise is better, not solved.** Three stages now — next-event estimation
  removes the emitter's variance exactly, a clamp bounds what the car's own
  bodywork spikes to, and the new spatial filter averages what is left. The
  residue is on downward-facing surfaces lit only by bounce off the car itself,
  and closing that needs rays rather than filtering. **In order of value: blue
  noise instead of a per-pixel hash** (the spatial filter is currently averaging
  partly-correlated samples), then à-trous with variance guidance, then adaptive
  ray counts.
- **The clamp is a bias**, stated rather than hidden: the brightest
  inter-reflections come back slightly dimmer than they should.

### Two traps that are still live

**The staged assets, again.** The runtime and the editor load `assets/` beside
their own executable, and the POST_BUILD copy only runs when the target
relinks — so a shader edit plus `cmake --build --target RageVRuntime` can leave
the old shader staged and the change looking like it did nothing. Check the
staged file, not the source:

```
grep -c "<the thing you added>" build/bin/Release/RageVRuntime/assets/shaders/<file>
```

**The editor writes Render Settings back to the `.rvproject`.** Opening it to
look at something is enough to change the file. Check `git diff` on the project
before committing — and do not assume a change there was accidental: it was the
owner's, twice.


---

### And the one before that: the frame got 4.35x cheaper, and EnTT is gone

**Updated 2026-08-22.** Work on `main`, which is clean. The day closed roadmap
8.3, 8.15, 8.16 and 10.2.

### What the frame costs now

On the owner's scene: **28 ms to 6.44 ms**, which is 36 FPS to 155. On the
scale fixtures, at constant density, Vulkan, 1280x720:

| objects | this morning | now |
|---|---|---|
| 1,000 | 1220 FPS | **2053** |
| 5,000 | 319 FPS | **701** |
| 20,000 | 74 FPS | **253** |
| 60,000 | 18 FPS | **78** |
| 120,000 | -- | **34** |

Five changes, and **the two that mattered were not the ones the roadmap named**:

| change | worth |
|---|---|
| the transform walk compares instead of recomputing (8.15) | ~1.8x |
| instance data out of the sorted record (8.16) | ~1.35x |
| the GPU-driven lit pass (8.3) | ~1.38x |
| one packed sort key (8.16) | ~1.1x |
| GPU cull for the depth passes (8.3) | ~1.03x |

The largest single piece was a function that appears in no roadmap row:
`UpdateWorldTransforms` recomputed every entity's world matrix from scratch,
four to seven times a frame, whether or not anything had moved -- 27.4 ms of a
52 ms frame, against ~0.6 ms for all the frustum culling that 8.3 is about.

**The method is the transferable part.** Three times in one day a measurement
contradicted a confident guess about where time went, and every one of those
guesses would have produced a change worth nothing. Put a timer in before
writing code. ENGINE-NOTES 7bx has each wrong turn beside its answer.

### What is switchable, and what it depends on

- `--gpu-cull=on|off` -- the depth views' compute cull. On by default, both
  backends.
- `--gpu-lit=on|off` -- the camera's. On by default, **and it requires
  bindless**, so it does nothing on OpenGL. That is not a limitation waiting to
  be lifted: it draws one indirect call per mesh, so the material has to be
  chosen per *instance*, which only bindless does. Without the guard OpenGL
  rendered a courtyard of white boxes with one textured fox in it -- the fox
  being skinned, and skinned meshes taking the CPU path.

**The CPU path is a real fallback, not a stopgap.** OpenGL, a device without
compute, and either flag set to off all take it, and it is the same picture:
camp and demo match within each backend's own run-to-run noise, and as of
2026-08-24 the showroom -- the scene with glass in it -- matches bit-for-bit
under `check_gpu_lit.py`'s conditions (the OIT accumulation-order floor is
the only residual under the committed profile).

### EnTT is gone (10.2)

`Scene/ECS.h` replaces it in a few hundred lines against thirty thousand: a
sparse set per component type, twelve registry calls and four view operations,
because that is what the engine used. 2.4 MB of vendored header deleted, no
change in checks or frame time. ENGINE-NOTES 7by.

**Four behaviours it was quietly relying on**, none visible at a call site and
all four of which broke something:

1. **Component references survive an insertion.** Pools are paged for this. A
   `std::vector` reallocates, and `AddComponent` hands back a reference that
   callers configure over the next several lines -- and the draw list borrows a
   `TransformComponent*` for a whole frame.
2. **Iteration is creation order.** EnTT went backwards and the serializer
   reversed to compensate; the reversal became the bug the moment EnTT left.
3. **A range-for evaluates `end()` once.** An iterator that re-reads the pool's
   size follows a pool that grows, which is not a longer loop but an infinite
   one.
4. **A destruction listener can already have removed the thing** it is being
   told about, so `Erase` and `Clear` re-check after notifying.

A component's pool index comes from a hash of `__FUNCSIG__` through one
exported function, never a counter: a counter inside an inline template is per
module, and the engine, the editor, the runtime and a project's script module
would each start their own.

### The two traps that cost the most time

**`SampleProject`'s `Sample.dll` is compiled against the engine headers.** Any
change to a type's layout -- which the ECS swap was, comprehensively -- makes
the engine and the script module disagree about memory. It cost hours today
across three separate incidents and never once presented as a DLL problem: it
hung inside a fixed step with audio still playing, then crashed, then rendered
nothing. Rebuild it for both configurations:

```
cmake --build SampleProject/bin/module --config Debug
cmake --build SampleProject/bin/module --config Release
```

**A capability the fast path depends on has to be asserted where the path is
chosen.** The lit pass assumed bindless because that is what the machine it was
written on has. No check caught it -- scenetest passes on OpenGL because
nothing in it compares a *textured* render against a reference -- and it was
visible only to somebody looking at the screen.

### Where the remaining time goes, at 60,000 objects

Roughly 12.8 ms: the lit pass's own walk and submission, the depth passes at
2.9, the transform walk at 2.6, and 2.7 of GPU. **Nothing on this curve is
GPU-bound**, and the real scenes are the opposite -- camp is 4.9 ms of which
4.9 is the graphics card. Two regimes, and CPU work is worth nothing to the
second one.


---

## Three stale artefacts cost most of a day, and none of them announced itself

Written down because they cost hours each and the symptoms pointed elsewhere
every time.

**1. The editor keeps its own staged shaders.** Editing
`RageVEditor/assets/shaders/` and building `RageVRuntime` leaves the editor
running the old shader. The fix landed, the runtime proved it, and the editor
still showed the defect -- which reads as "the fix did not work" rather than
"the fix is not in this binary". Build the editor target too, or all of them.
The runtime has the same staged copy (`build/bin/<config>/RageVRuntime/assets/`),
and it goes stale the same way: running it mid-edit compiled the day's
`rtgi_trace` against an older `pbr_fragment.glsl` and produced a compile
failure that a whole handoff section then blamed on the define plumbing (see
the resolved section at the top). A shader compile error from a run that did
not immediately follow a build is a stale copy until proven otherwise.

**2. A project's script module is built against the engine's headers.** Adding
three fields to `ParticleEmitterComponent` moved `Burst`, and `SampleProject`'s
native `Sample.dll` -- built earlier, not rebuilt -- wrote to the old offset. It
surfaced as a *UI button test* failing while the click, the button and the
binding all passed. `cmake --build SampleProject/bin/module --config Debug` and
again for `Release`.

**3. An incremental build does not always recompile what a header change
touched, and the failure is not a compile error.** Adding two vectors to `Scene`
changed its size; the exe kept the old `sizeof`, `make_shared<Scene>` allocated
too little, and the constructor wrote past the end. The symptom was a
**Release-only segfault inside `std::vector::clear()` on an empty vector**, with
Debug working perfectly -- so it looked like optimiser-exposed UB, and I chased
the structured binding, `GetMaterial` and component-pointer lifetimes before the
truth. `m_DrawItems` printed `size 18445272897802728025`.

**The rule this leaves: a header change that alters a type's layout needs
`--clean-first`, and "Debug works, Release crashes" should make that the *first*
hypothesis rather than the tenth.** Object timestamps are not evidence -- the
stale `RuntimeLayer.obj` was newer than the header it was stale against.

---

## Still open

- **Depth-of-field flicker at silhouettes.** The mechanism is understood --
  jitter-driven, the composite ramps over one pixel of circle-of-confusion --
  and three candidate fixes were measured and eliminated (+jitter 619, -jitter
  596, min-of-4 depth 604, against 222 unfixed and a 198 control). Nothing is
  half-applied; all three were reverted.
- **The camp's shot 5 to shot 6 travel clears the tent by 1.4 millimetres.** The
  scene generator prints it on every build. It does not clip and the roof still
  wipes half the frame for most of a second, and there is no waypoint that fixes
  it: the corridor between the tent and Frame Pine 3 is six centimetres and the
  one between the tent and the fire is four. It needs a shot or a tree moved,
  which is a decision about the film (7bw).
- **Roadmap 6.4's slider** is the one widget with no component. Everything else
  in phase 6 is built.
- **ENGINE-NOTES 7bs and 7bt are cited from four files and were never written.**
  `rvdoc --check` does not validate note cross-references, so nothing catches it.
- **The check suite never looks at a textured render.** That is how the lit
  path shipped for an evening drawing every static mesh with the default
  material on OpenGL: 2199 checks passed while the courtyard was white boxes.
  A reference-image check on one textured scene per backend would have caught
  it in seconds, and would catch the next one of its kind.
- **The showroom's headlamp lens.** 7ca concluded from a tint test that there
  is no lens mesh over the aperture and dressed the reflector to stand in for
  one. The glTF's accessor bounds say otherwise: `EXT_Glass_Emissive_Front` has
  a box at x -0.84..0.85, y 0.57..0.76, z 1.68..1.94, which is exactly the
  headlamp band. So the mesh is there and the tint test could not see it --
  worth an hour before anyone adds geometry the model already has (7cd).
- **Meshlets landed 2026-08-23**, owner-requested: `--meshlets=on` draws
  static shadow casters through a VK_EXT_mesh_shader pipeline -- a
  64-vertex/124-triangle cut per mesh (Meshlet.h), per-meshlet sphere-vs-clip
  culling in the mesh stage, bit-identical to the vertex path on showroom,
  camp and demo, validation-clean, and performance-neutral where the shadow
  phase is small (0.18 ms GPU either way). Off by default; a device without
  the extension logs and runs the classic path. When on, shadow views skip
  the GPU-cull indirect path -- per-meshlet culling stands in. **The lit
  stage landed the same evening**: pbr_meshlet.rvshader emits the vertex
  stage's ten varyings from a mesh stage and shares pbr_fragment.glsl
  verbatim, the scene set binds on both pipelines because set-layout stage
  flags are now coarse across graphics stages (VulkanPipeline.cpp says why),
  and static opaque runs draw as DrawMeshTasks with per-meshlet camera
  culling. Bit-identical to the CPU vertex path on all three scenes; cost
  identical to it too (showroom Scene 4.08 vs 4.11 ms).

  Verifying meshlets against `--gpu-lit=off` surfaced the 1.6M-pixel
  disagreement that the "Start here" section above lays to rest
  (2026-08-24): the CPU/meshlet side was the broken one -- opaque runs
  handed to the OIT pass -- and the gpu path was separately missing glass.
  The "5.0 vs 4.1 ms" cost gap measured then was two differently-broken
  frames; post-fix the interleaved A/B reads even (7.09-7.11 on vs
  6.88-7.08 off). `check_gpu_lit.py` now holds all four submission paths
  bit-identical. Remaining meshlet stage: a task stage fed from the cull
  table.

---

**The camp scene section below** is the project's demo scene. Its traps are
worth reading even if you are here for something else -- most of them are
engine contracts rather than scene problems.

The `vulkan-overhaul` branch is merged into `main` and is finished with.

Companion docs:
- [ROADMAP.md](ROADMAP.md) — where this is going, in dependency order.
- [ENGINE-NOTES.md](ENGINE-NOTES.md) — research distilled into decisions. Read
  §1 before touching the simulation loop, §3 and §3a before touching physics or
  contacts, §6 before touching the render graph, §7a before touching audio, and
  **§7b before deciding a change is verified** — it is why exiting and pixels
  are both part of the bar.
- [ARCHITECTURE.md](ARCHITECTURE.md) — renderer design detail.

---

## The camp scene — done (2026-08-20)

**The camp is the project's demo scene now.** `SampleProject.rvproject` starts
on `scenes/camp.rage`; the courtyard (`scenes/demo.rage`) is still there and
still works, but it is no longer what the engine opens with.

A clearing in a pine wood at night: a fire that is the only real light, a ridge
tent, a minivan parked at the edge, a mirror on a kickstand angled at the fire,
an animated fox and three rabbits, and a camera that shows it from **twelve
framed positions** with a pause button. 989 entities, 41 props, 30 materials.

### Where everything is

| | |
|---|---|
| `tools/scripts/make_camp_textures.py` | Seven flat-patch colour maps, two sprites, the grade. **31 KB in total** |
| `tools/scripts/make_camp_models.py` | 41 props, from boxes, cones, struts and thickened polygons |
| `tools/scripts/check_models.py` | Every piece of every prop is a closed solid wound outward. **Run after every model change** |
| `tools/scripts/make_prop_sheet.py` | The per-asset verification scenes: `--each`, `--only NAME`, `--assemblies`, `--clean` |
| `tools/scripts/make_camp_scene.py` | The scene, the materials, the curves, the post profile and the twelve shots |
| `tools/scripts/make_ambience.py` | The night bed: wind, leaves, insect wash, crickets. Seamless, and it *checks* that it is |
| `tools/scripts/make_fire_sound.py` | Now parameterised (`--output --seed --crackle --top --pops`) so a second scene can have its own fire |
| `SampleProject/Scripts/CampCamera.cs` | Walks entities named `Shot 1`…`Shot N`, eases between them, and `TogglePause()` |

The full design record is **ENGINE-NOTES 7bo**, which is where the reasoning
lives. What follows is only what someone picking this up needs in the first ten
minutes.

### The regeneration order

    python tools/scripts/make_camp_textures.py
    python tools/scripts/make_camp_models.py
    python tools/scripts/check_models.py
    # run the engine once so the registry mints handles for any new asset
    python tools/scripts/make_camp_scene.py

`make_camp_scene.py` reads every handle out of the `.meta` files rather than
carrying a pasted table, and **refuses to write a scene** whose camera would
stand below the ground or inside the tent, the van or the fire.

### Seven traps that cost a session between them

Every one of these failed silently and produced something that looked like a
modelling mistake.

1. **A `.rage` with no `Version:` is version 1, and version 1 discards every
   `EntityID`.** Every parent reference then resolves to nothing — 722 of them
   here. On screen: the tent's band and doorway sat on the campfire, every chair
   lost its seat, and a hundred tree trunks stacked into one column at the
   origin. Generated scenes must write `Version: 6`.
2. **A child with no `TransformComponent` renders at the world origin.** It does
   not inherit its parent's. `Scene.entity()` now always writes one.
3. **`NativeScriptComponent` is C++, `ManagedScriptComponent` is C#.** Getting
   it wrong logs `Scene references unknown script` and the entity does nothing.
   Use `Scene.managed_script()`.
4. **The runtime loads `SampleProject/Scripts/bin/Sample.dll` and never builds
   it** — only the editor does. A plain `dotnet build -c Release` writes to
   `bin/Release/net8.0/` and the runtime keeps the old assembly, silently. This
   cost two hours of camera changes that had no effect. Build it with:

        cd SampleProject/Scripts
        dotnet build -c Release -o bin -p:RageVScriptCore=<path to RageV.ScriptCore.dll>

5. **`PerspectiveFOV` is the horizontal angle.** On 16:9 that is 40° across and
   23° down, so any framing solved for a vertical FOV is a third too tight.
6. **`--width` / `--height` clamp to a minimum of 640.** Ask for 512×288 and you
   get 640×640 — and a contact sheet built for 512×288 tiles then crops every
   image, which looks exactly like a framing bug.
7. **An audio bus is an enum read by name**, so `"Sfx"` is not `"SFX"`. A near
   miss falls back to Master without a word.
8. **The import cache had two answers to "is this a model".** `CookedExtension`
   knew about `.fbx` and `Cook` did not, so every FBX went to the image decoder,
   warned, and was re-parsed from source every load. Fixed by giving the
   question one function -- but the shape of it is the thing to watch for: two
   predicates for one question, each defensible alone.

And two older ones that still hold:

- **`fbxwrite.cone` / `cylinder` were once wound inside out**, reported as *"the
  trees seem see through"*. `check_models.py` guards it, including `strut` and
  `panel`. An inverted face is not merely culled — it is lit from behind.
- **A cubemap sky is the scene's image-based light, not a backdrop.** The camp
  was lit like an afternoon because it still had the courtyard's dusk panorama.

## Fixed: the "banding" on flat surfaces was ray-traced GI acne

**Status: fixed (2026-08-21), `rtgi_trace.rvshader`, one line.** Reported as
horizontal banding on the camper's flank and the tent, Vulkan only. It is not
banding and it is not a precision problem: it is self-intersection in the
half-resolution bounce, and the full account with every measurement is
ENGINE-NOTES 7bu.

### The short version

`rtgi_trace` runs at half the frame at `RayTracedGlobalIllumination: Medium`
and below, and reads the scene pass's **full-resolution** depth. Its texel
centres therefore land on the depth texels' corners, so the ray origin was
reconstructed half a full-resolution texel from the point whose depth it used
-- along the view ray, which puts it off the plane. On a surface seen at a
grazing angle that beats the 2 mm the origin is lifted by, and the bounce rays
hit the surface they left. It reads as a lattice of dashes.

The fix is the rule SSAO already follows (7an) and RTAO's `ViewPositionAt`
already applies: snap to the depth texel's centre before fetching *and* before
reconstructing. Ripple on the flank, in display levels at its own frequency:
**2.64 before, 0.38 after** -- full-resolution GI measures 0.37 and GI off
measures 0.36.

### Two things worth keeping from how this went wrong

**"Vulkan only" was a true observation about a false cause.** The first plan
written here was to compare swapchain formats and add a dither. Output
quantisation is real -- a flat panel under one light contours to 39 levels
over a 183-to-221 ramp, flat runs averaging 9.35 px -- but it is **symmetric**:
the two backends render that fixture to within one level, `max |vk - gl| = 1`,
nothing differing by more than 2. It could never have explained a one-backend
artefact. *What explains "only on one backend" has to be something only one
backend does*, and OpenGL has no ray queries, so it never runs the traced
bounce at all.

**Subtract the frames.** Three passes at this sampled a patch and read the
numbers, and one of them concluded the artefact did not exist. What identified
it in minutes was rendering with `--rt-gi=off` and subtracting: the smooth
indirect light cancels and the lattice is unmistakable. A 2.6-level defect on
top of a 40-level ramp is invisible in the ramp's statistics.

### The fixture

`SampleProject/Assets/Scenes/band_panel.rage` -- one flat panel filling the
frame under one point light, which is the cleanest quantisation ramp the
engine can render. It is what settled the symmetric-quantisation question and
it is worth keeping for the next time something is blamed on precision:

```
build/bin/Release/RageVRuntime/RageVRuntime.exe --project=<repo>/SampleProject     --scene=Scenes/band_panel.rage --rhi=vulkan --render-defaults=on     --width=1280 --height=720 --screenshot=panel.png --screenshot-frame=40
```

Run it from the binary's own directory -- the runtime resolves `assets/shaders`
against the working directory, and from anywhere else 46 shaders fail to
compile and it exits 3.

## Fixed: the editor lost the device in Play on Vulkan

**Status: fixed (2026-08-21), and the fix is one line of intent.** The
acceleration structure is *world* state and the terrain's level of detail is
*view* state, and the ray-instance list was built out of both.

### What was actually wrong

Every view that draws the scene rebuilds the ray-instance list — clear, walk
every mesh, walk every terrain chunk, build. `RayShadows::Build` is guarded to
run **once per device frame**, because the structure holds every caster with no
frustum and one build serves every view. So in the editor, which draws the
scene twice:

| | viewport (first) | game panel (second) |
|---|---|---|
| Instance list | cleared, refilled | cleared, **refilled again** |
| Structure | **built from this list** | guarded — not rebuilt |
| What the traced passes read | list and structure agree | structure from view 1, **list from view 2** |

Both lists held the same *number* of instances, so nothing looked wrong. But
`Terrain::SelectLod` runs per view and picks each chunk's level from *that
camera's* distance, and the list took `chunk.Selected()`. The two cameras are in
different places, so the second list named different chunk meshes in the same
slots.

A traced hit carries the `instanceCustomIndex` it had in the structure — that
is, view 1's index — and every traced pass resolves it through the list, which
is view 2's. The record it lands on describes a different mesh. Whatever that
record is read as — a bindless slot, an offset — is then wrong, and a ray query
following it dereferences an address that is not mapped.

That is the whole thing:

- **Editor only.** The runtime draws one view, so its list and structure are the
  same list and structure. Thousands of frames, clean, always were.
- **Needs the Game panel open.** No second view, no second list.
- **Non-deterministic.** The cinematic camera has to be far enough from the
  editor camera for the levels to differ, which depends on where the shot is.
- **Not a synchronization hazard.** Nothing is racing; the data is wrong before
  it is submitted. Sync validation was clean, and a full barrier between the two
  graphs did not help, because there was nothing to order.
- **A random unmapped page each time.** Not a stale allocation — garbage.

### The fix

`Terrain::Chunk::ForRays()` returns `Levels[kRayLevel]` — level 0, the finest —
and the ray-instance list takes that instead of `Selected()`. A ray can start
anywhere, so there is no distance to pick a level from. Both views now produce
the identical list, so whichever one builds the structure, the other agrees with
it. `SelectLod` keeps the ray level non-stale alongside the level being drawn,
so a brush stroke does not leave reflections of terrain the data has moved on
from.

`RayShadows::Build` also checks the invariant on the guarded path — the list a
later view brings must be the same length as the one the structure was built
from — and says so loudly if it ever is not. It cannot fire as the code stands.
That is the point: when it *did*, the symptom was a lost device forty seconds
later with nothing to tie it to.

### Verified

- 3 x 2500 frames, all three ray-traced features on, frames-in-flight 1: clean.
- 2 x 2500 frames at frames-in-flight 2 and 3: clean.
- Previously: **lost within ~400 frames, 3 runs out of 3.**
- Invariant check silent throughout; runtime unaffected; `scenetest` failure set
  byte-identical before and after (97 pre-existing failures in this environment,
  all shader-compile and texture-cook fixtures).
- Held-camera shimmer unchanged: median 720 px before, 492 px after, mean delta
  0.454 vs 0.463.
- Cost: load 0.21 s -> 0.27 s, from building full-detail bottom-level structures
  for all 64 terrain chunks instead of whichever levels the camera happened to
  ask for. Bounded and one-off, where the old behaviour was unbounded.

### Two things the previous write-up got wrong

Both were load-bearing and both sent the search the wrong way.

**"GPU checkpoints: none reached — the fault precedes the first breadcrumb."**
It does not. Bookend checkpoints were added to *every* submission — the frame's
command list and every `ImmediateSubmit` — and the query still returns zero. So
`vkGetQueueCheckpointDataNV` returns nothing on this driver, and "none reached"
means the query is empty, not that the GPU got nowhere. The message now says
so. Do not spend a session on that clue again.

**"Device fault addresses, identical run to run."** They are not. Four runs gave
`0x670ea000`, `0xd47bf000`, `0x676da000`, `0x676da000` — a different unmapped
page most times, which is the signature of a *garbage* pointer rather than a
stale allocation, and was the clue that mattered.

### What the hunt left behind, and is worth keeping

- **The GPU address-range registry** (`Vk::NoteGpuRange`, `MarkGpuRangeFreed`,
  `DescribeGpuAddress`, `GpuRangeStatusOf`). Every buffer with a device address
  and every acceleration-structure backing registers its range and name;
  destruction marks it freed *inside the deletion*, not at drop, with a serial
  so a reused address cannot be mis-accused. A device fault now reads
  `-> inside 'fox.vertices' [0x... + 0x...]` or `-> 0x... past the end of
  'tlas.instances'`, which is what turned an address into a direction.
- **Used forwards, not only after the fact.** Under `--validation=on`, every
  top-level build checks each instance's structure reference against the
  registry before writing it. It is how "a freed bottom-level structure" was
  ruled out in one run instead of argued about.
- **Decoded fault types.** `type 6` now reads `shader instruction pointer
  (faulted here)` and `type 1` reads `INVALID READ`, and only the access types
  get an address lookup — an instruction pointer is in shader code and would be
  mis-attributed to whichever buffer sat below it.
- **`--play=on`** and **`--graph-barrier=on`**, both still useful.

### What ships

All three ray-traced features on. The crash was the only reason
`RayTracedAmbientOcclusion` and `RayTracedGlobalIllumination` were off; measured
cost on this machine is **9.3 ms/frame with reflections alone against 13.8
ms/frame with all three**, at 1600x900 with the editor drawing two views.

Turning them back on immediately exposed a second, unrelated defect -- see
*Ray-traced AO strobed with the TAA jitter* below. That is fixed too.

### Ray-traced AO strobed with the TAA jitter -- fixed (2026-08-21)

Reported as "the green trees in the background clearly show a flicker", in the
editor and not the runtime. It was **ray-traced ambient occlusion**, and it had
nothing to do with the device-loss fix: measured byte-identical at commit
`6bb7718` with the same settings, so it had been there as long as RTAO had.

`ViewPosition` reconstructs a point from the depth buffer without subtracting
the TAA jitter the depth was drawn through, so every reconstructed position is
displaced by an amount proportional to depth that changes every frame. SSAO, SSR
and SSGI convert straight back through `ViewToUv` and the offset cancels; RTAO
casts a real ray from that point and it does not. RTAO also has no temporal
accumulation, so it goes to the screen undamped. Full account in ENGINE-NOTES
7bq.

| Editor, held camera | luma swing | canopy pixels changing |
|---|---|---|
| Before | 1.48 %, cycling with `TemporalJitterPhase` | 1.20 % |
| After | 0.21 %, no pattern | 0.01 % |

**How it was found, because the method is the transferable part.** Three rounds
of whole-frame "percentage of pixels changing" reported 0.45 % and read as
noise. Laying the per-frame mean luma out *as a series* made it obvious --
`62.0 62.1 62.1 61.7 62.1 62.1 61.2 61.2`, repeating -- and changing
`TemporalJitterPhase` from 8 to 4 changed the period to 4, which named the
cause outright. A statistic that averages over a period cannot see a defect with
that period.

### TAA shimmer: what is measured, and what is not established

"Flicker in the Game view" was reported repeatedly during the crash work. It was
chased with a metric -- pixels in the Game panel changing by more than 32 between
consecutive frames, with the camera held still -- and what the metric responds
to is **temporal anti-aliasing**.

| Configuration | Shimmering pixels |
|---|---|
| Courtyard scene, pre-session settings (RT off, feedback 0.6) | **2023** |
| Camp scene, pre-session settings (RT off, feedback 0.6) | 1680 |
| Camp, RT reflections on, feedback 0.6 | 1312 |
| Camp, RT off, feedback 0.9 | 419 |
| Camp, RT reflections on, feedback 0.9 -- **what ships** | **142** |
| Camp, FXAA instead of TAA | 9 |
| Camp, no AA | 11 |

Three things follow, each worth keeping.

**Ray tracing is not the cause.** Turning it off makes shimmer *worse*, not
better, at matched feedback.

**Whether it is new is NOT established, and an earlier version of this section
claimed it was.** The evidence offered was that the courtyard -- the scene the
editor opened on before the camp existed -- shimmers *more* at the pre-session
settings than the camp does. That shows the shimmer is consistent across scenes.
It does not show that the Game panel did not regress, which is what was actually
reported. There is no measurement of the Game panel from before this work, so
there is nothing to compare against; do not let the table above imply otherwise.

What *is* established: two of the inputs changed in this session. Ray tracing
went from off to on, and `TemporalFeedback` went from 0.6 to 0.9. The table
shows what each is worth.

**It is `TemporalFeedback` behaving as documented.** At 0.6 each frame keeps 60 %
of the accumulated image and takes 40 % of a freshly *jittered* one, so a still
frame never settles. The field's own note says a project that knows it is mostly
still should raise it, and the camp holds its camera for 3.4 of every 7.6
seconds. The project sets 0.9 -- twelve times less shimmer than the pre-session
default -- and the engine default is untouched, because 0.6 is right for a
project that is mostly motion.

Checked and found clean, so nobody re-checks them: no intermittent flash frames
(48 consecutive frames, deltas 0.57-0.62, zero outliers above 3x the median); no
region-specific flicker (sky, ground, fire and the HUD each between 0.00 % and
0.04 % of pixels changing); the ~3 % luma pulse across the view is the `Flicker`
script on the fire light, which is deliberate.

**One lever measured but not applied**, left as a decision rather than taken:
`TemporalJitterScale: 0.5` halves shimmer again at no measurable cost to
sharpness -- 145 px to 101 px at feedback 0.9, 1302 to 705 at 0.6, with edge
sharpness unchanged at 1.24. It narrows the reconstruction filter, so it trades
some anti-aliasing quality for stability; worth trying if the shimmer is still
objectionable.

### If the graphics device is lost

`VK_ERROR_DEVICE_LOST` means the driver tore the device down -- a GPU fault, or
its watchdog resetting a frame that took too long. **It cannot be recovered
from** without recreating the device and every resource on it.

The engine now latches it: the first one is reported in full, everything after
it is silent, and the application closes. Before this it was treated as an
ordinary call failure, so the wait and the submit failed once each per frame
forever -- a report of it arrived as 250 identical lines inside one second with
the actual first failure long gone off the top of the log.

**To diagnose one**: set `validation = on` in the editor's `ragev.ini`, do the
thing that caused it, and read the *first* error. A fault has a cause and the
layers normally name it. If the fault only appears with ray tracing on, the
one-line test is `RayTracing: false` in `SampleProject.rvproject` -- the camp
turned all four ray-traced settings on and they are by far the heaviest thing
in the frame.

### The rules this scene is built to

- **Verify every asset on its own, then delete the scene that verified it.** One
  prop, plain floor, flat light, no post, and a ruler in ten-centimetre bands.
  A chair at half its height looks exactly like a chair.
- **A component can be right and the assembly wrong.** `--assemblies` covers the
  six props that only mean something put together.
- **Realism is not the goal.** No normal maps, no height maps, no roughness
  maps: flat cellular patches, posterised to three or four steps, close together
  in value.
- **Nothing is borrowed from the courtyard** — its own maps, sprites, grade,
  curves and sounds. The one exception is the **fox**, which keeps its glTF
  material and texture: it is the only rigged, animated, low-poly asset the
  project has, and the owner ruled it in.
- **Never push.** Pushing is the owner's action.

### What is not done

- **Skinning from FBX** (8.9 stage 2) — the fox is glTF because FBX skinning is
  not implemented. A skinned `.fbx` imports at the bind pose and says so.
- **No physics on the camp props.** Nothing here needs to fall over, and adding
  colliders to four hundred tufts would cost more than it shows.
- The van's interior is a dark pane; there is nothing behind the glass.

## 0. Cold start

The five-minute version, for picking this up with no memory of it.

**Where it is: phases 0-5 are complete.** The render graph, HDR post, sky and
cube maps, image-based lighting, shadows for every light type, frustum
culling, instanced batching, clustered forward lighting and skeletal animation
are all done. So is scripting, in both languages, with live reload in both:

- **C++ scripts live in the project's `Source/`**, which builds into a game
  module DLL. Opening a project loads it -- in `Project::Load`, so the editor,
  the runtime and a packaged game all get it the same way. Build Scripts
  unloads the module, rebuilds it in the background (live console, cancel),
  and reloads whatever links. New Script writes into `Source/`. No engine
  rebuild anywhere in a game developer's loop.
- **C# scripts live in `Scripts/`**, built the same way, loaded from *bytes*
  into a **collectible AssemblyLoadContext** that is retired and replaced on
  every build -- that is 5.5, and it is done. The unload is verified with a
  WeakReference; a context that will not die is reported, not ignored.
- **Building mid-play restarts the scene.** Live instances run the loaded
  code, so the editor stops the scene, swaps both languages' scripts, and
  resumes Play on the new code; after a failed build it stays stopped with
  the errors showing. One rule, both languages. Play pressed during a build
  queues until the build lands.
- **As of interop protocol 4 the languages are equals**, and protocol 6 gave
  them both a second rate; protocol 7 gave them the game's UI and protocol 8
  the render settings -- one flat name space over three owners since 9.0, so a
  script does not have to know which file a setting lives in. Audio, raycasts,
  hierarchy, and components by registry name with text values all reach C#.
  The one structural exception is typed GetComponent<T>, which cannot cross
  a boundary and is traded for the registry's named access.

**A project can have both languages on the same entity**, one inspector, one
scene format. The manual documents both guides and is generated with a drift
check (`rvdoc --check`).

**Also done, off-roadmap:** the developer manual and its generator, the
application icon, project creation with a per-project `bin/`, and 5.0 -- no
third-party type in a public header, and the public API segregated into
domain namespaces. The renderer was deliberately left out of that last one.

**Prove it still works** (from the repo root, ~2 minutes):

```bash
cmake --build build --config Debug
build/bin/Debug/scenetest/scenetest.exe --rhi=vulkan
build/bin/Debug/scenetest/scenetest.exe --rhi=opengl
```

1574 checks, `exit 0`. Then look at a frame:

```bash
build/bin/Debug/RageVRuntime/RageVRuntime.exe --rhi=vulkan --validation=on --screenshot=f.png
```

And the one check that compares a render against a render instead of against a
stored image — it found both of 6.9's bugs and is where a transparency
regression will show up first:

```bash
python tools/scripts/check_oit.py --config Debug
```

4/4, `exit 0`.

**Ten things that are easy to get wrong here, all learned the hard way:**

0. **A default nobody has measured is a default nobody has tested.** FXAA
   shipped as the default anti-aliasing for a whole roadmap phase with two
   inverted signs that made it a *complete no-op* on any clean edge. It
   survived screenshots on both backends, three documents describing it, and
   a hand-written shader comment explaining the very line that was wrong.
   What caught it was the first time anything asked it for a number.
   ENGINE-NOTES §7n.

1. **A pooled grow says `while`, not `if`.** Every renderer keeps a pool of
   batches or descriptor sets and a cursor into it. `if (cursor >= size)` adds
   one and then indexes the cursor, which is only safe when the cursor was
   exactly at the end -- and these pools get *cleared* whenever pipelines are
   rebuilt, which can happen mid-frame. That was a first-frame segfault in any
   scene with a reflection probe and a particle emitter once MSAA existed to
   trigger the rebuild. All seven sites now say `while`. ENGINE-NOTES 7q.

2. **Run each tool from its own directory.** Assets are staged per target.
3. **A straight edge is the easy case; build the awkward one too.** SMAA's
   diagonal pass measured as a clean 2x win on every straight-edge angle and
   was quietly making *small features worse* -- short runs everywhere, and a
   search firing on three pixels of evidence. What caught it is a fan of thin
   bars scored against a 4x supersampled render, which is also the only
   instrument here that measures arbitrary content rather than a line.
   ENGINE-NOTES 7p.

4. **`--validation=on` for *every* verification run, editor included.**
   Validation is off by default everywhere now -- it costs ~1.5 ms/frame and
   was making Vulkan read as slower than OpenGL -- so a run without the flag
   reports zero validation lines whether or not there were any. That already
   hid a black screen and a segfault once, back when only the runtime shipped
   with it off.
5. **Verify by exiting, not by killing.** `exit 0` is part of the bar. A killed
   process runs no destructors, which hid a leak of every scene and every
   render target for months.
6. **Compare the two backends' frames, not just each on its own.** A
   Vulkan-only bloom flip survived a whole roadmap phase of clean runs, because
   nothing in the scene was bright enough for a mirrored contribution to show.
   `--screenshot` on both and look at them side by side.
7. **Never measure anything at exactly 45 degrees.** An edge there advances
   one row per column, so the staircase lands on a perfect line and reads
   zero; every supersample grid is symmetric about it, so supersampling can
   measure as buying nothing. It cost a wrong conclusion about SMAA's
   diagonal pass, published in three places, because a degenerate case does
   not fail -- it reports a flattering number. `check_smaa.py` uses 43.

8. **`discard` costs a Vulkan device feature.** glslang targeting SPIR-V 1.6
   compiles it to `OpDemoteToHelperInvocation`, because `OpKill` is deprecated
   there, and that capability needs `shaderDemoteToHelperInvocation` which
   this engine does not enable. The symptom is one validation line per run,
   from shader *creation*, so it appears whether or not the shader is ever
   drawn with. Write the zero and return instead, unless the feature gets
   turned on deliberately.

9. **The harness prints `FAIL` in capitals.** A case-insensitive search for
   "fail" also matches the word *fails* inside the names of passing checks, and
   returns a count that looks like failures and is not. Match case, and read the
   `OK` line at the end.

10. **Every check here renders with `--aa=none`, and a setting the user can
    change is a setting nothing is testing.** MSAA left the scene depth
    multisampled and unresolved; four post passes then bound it to a
    `sampler2D` and the entire frame came back blurred. It survived a whole
    roadmap phase, including `check_depth_of_field.py` — the check whose only
    subject is the effect that broke, in a scene built for it, passing every
    run. When something reads a *target*, run the claim again in every mode
    that changes that target's shape. ENGINE-NOTES 7ai.

**What to read before changing something:** §5 of this document. Every entry
was a real bug, and most fail silently rather than obviously.

**What is true right now, honestly:** §6 — what works, what works with a
caveat worth knowing, and what is not built. §9 for defects, §10 for what has
already gone wrong here and what caught it.

**TAA (7.10) is done**, and it left the pieces phase 9 was built on. Designed
in ENGINE-NOTES 7r; the record below is kept because every trap in it is
still live.

Motion vectors: the scene target carries an RG16F velocity attachment, the PBR
shaders write real screen-space motion, and everything else in the scene pass
writes zero. TAA and 9.5 motion blur both read it now.

Jitter: a centred Halton(2,3) point, applied to the scene camera in
`Scene::OnRender` and nowhere else, so meshes, sky, grid, world text, icons and
particles all move together. `AntiAliasing::TAA` exists and selecting it gets
the jitter and no accumulation, which is worse than None -- the inspector and
the C# doc comment both say so. `tools/scripts/check_taa_jitter.py` measures
the properties that matter (26 launches, ~2 min): same frame twice is
byte-identical, frame 30 and 38 are byte-identical *to each other* while 30 and
31 differ, all eight offsets distinct, and nothing more than 4 px from an edge
moves. 353 pixels move per frame, the same number on both backends.

History: `TemporalHistory`, a caller-owned ping-pong pair, imported into the
graph. The target written this frame is next frame's history *and* what bloom
and tone mapping read, so nothing is copied. Rejection is a YCoCg
neighbourhood clip plus a 1/(1+luma) weighted mean; `TemporalFeedback` is the
ghosting-versus-flicker dial in the inspector. On a static scene it is the best
of all six modes at three of four angles on the linear yardstick.

**Both of the gaps that were open here are now closed**, and the moving
check that closed them is `tools/scripts/check_taa_motion.py` (~1 min, both
backends). The reprojection's vertical sign is measured, not argued: 19.8 RMS
as shipped against 29.9 inverted. The sky reports real motion now, from a
small uniform buffer, because its push-constant block had no room.

**The one thing to know before touching any of this again:** the resolve's
neighbourhood clip hides reprojection errors wherever the neighbourhood is
locally uniform, and it hid three separate things during 7.10. A flat bright
block showed no ghosting with the sign *inverted*. A smooth sky is
byte-identical whether its velocity is right, zero, or a wild constant --
which is why the sky fix measures nothing and is kept for 9.5 motion blur,
which has no clip to save it. **Only content with per-pixel detail can show a
reprojection error**, so any scene added to check one has to be textured.

**Before diffing two screenshots, prove they contain what you think.** An
inertness check here reported OpenGL as 100% changed in all five modes; the
reference images were the *loading screen*, caught at frame 30 by a cold
driver shader cache on the day's first OpenGL run. `check_smaa.py` and
`check_taa_jitter.py` are immune because they fit the edge and require the
unfiltered staircase to measure 1/sqrt(12) -- a flat grey cannot. A by-hand
comparison has no such control. ENGINE-NOTES 7r.

**One known gap in the motion vectors themselves**, recorded rather than
forgotten: **skinned meshes report only the object's motion, not the limb's**,
because the bones are not double-buffered. That is a memory decision, not an
oversight, and it is why a running fox's legs smear less than its body under
motion blur. (The sky used to write zero; it reports the camera's rotation
now, from a small uniform buffer -- fixed for 9.5, which has no clip to hide
the error.)

**The pattern that bit three times in a row today**, and will again: a
renderer learns the scene target's *shape* from BuildFrame, but a reflection
probe captures the scene **before** the graph is built on the first frame. So
anything shape-related -- sample count, colour formats, attachment count --
has to be stated at layer init *as well as* in BuildFrame, and scenetest has
its own two call sites that need it too. Grep for `R16G16_SFLOAT` to find all
five places.

**Phase 9 is complete (2026-08-15): 9.0 through 9.7 are built** -- settings
live on the project with the two profile assets over them (9.0), colour
grading (9.1), auto exposure (9.2), the Perlin-era grain rework (9.3),
depth of field (9.4), motion blur (9.5), SSAO (9.6) and SSR (9.7) all
landed, each with its check script and each **exactly off by default**,
which is what keeps every recorded threshold valid. Every effect is a
field on the `.rvpostprofile`, edited live in the inspector and written
through to the asset the instant it changes. **SSR forced the scene
target's first new attachment since velocity** (normal + roughness +
metallic); ENGINE-NOTES 7ad records every place its shape must be stated,
and that list is what the next attachment needs. C++ script fields moved
to declaration-site markers (`RVShowInEditor`, ENGINE-NOTES 7aa) the same
day, which also closed a first-frame descriptor-set hazard, themed the
loading screen, named the frame profiler's wait, gave realtime probes a
refresh-rate dropdown, and stopped the scene view wearing the game
camera's lens.

**What to do next: the 9.x follow-ups, in the owner's order, then phase
8.** 9.8 (SSAO reads the real normal -- and the derivation of the
reconstruction frame it forced, ENGINE-NOTES 7ae, which found and fixed a
transform in the SSR trace that was right only for sideways-free normals)
and 9.9 (SSR replaces the probe exactly: the blend moved into the
lighting, one frame late, ENGINE-NOTES 7af; the check measures 0.00 levels
against the law) and 9.10 (the SSR march is a screen-space walk with a
crossing test and a min/max pyramid, ENGINE-NOTES 7ag; correct on every
fixture, and honest that the pyramid does not pay on the demo's grazing
rays) and 9.11 (the probe convolution in six passes and six copies, not
thirty-six of each, ENGINE-NOTES 7ah; Vulkan probes 0.77 -> 0.59 ms) are
done. **Phase 8 is next, and it is the owner's call which item.** See
START HERE in the log below.

Roadmap **phase 8 is open work, not excluded** -- GI, bindless, GPU-driven
rendering, terrain, navmesh, networking, other platforms, XR, FBX, visual
scripting, an asset store. They were §7 non-goals and were *reopened at the
owner's direction*; nobody has ruled any of them out. What the roadmap adds is
a price tag, not a veto: every one is L or XL and several are larger than
everything built so far, so each wants a deliberate decision rather than being
picked up because it sounds interesting. 8.2 is the one that is a decision
about the engine rather than a feature -- descriptor indexing is core in
Vulkan 1.2 and OpenGL 4.5 has no equivalent, so it means either dropping
OpenGL or maintaining two binding models.

---

## 1. What this is

A Windows game engine. Originally a Hazel/Cherno-lineage 2D engine, shelved
mid-way through a Vulkan port, revived and taken through four of five roadmap
phases.

**Stated goal:** ease of use of Unity, some of Unreal's graphical fidelity,
scope closer to Godot.

The engine loop — *import → place → script → play → export* — **closes**, and
the renderer now does everything the roadmap asked of it. What remains is C#
scripting (Phase 5).

---

## 2. Build and run

```bash
cmake --preset vs2022
cmake --build build --config Debug
```

CMake is **not on PATH**. It ships with VS 2022 Build Tools at:
```
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
```

| Target | Purpose |
|---|---|
| `RageVEditor` | The editor. Opens the sample project's start scene. |
| `RageVRuntime` | The game, with no editor. Opens a project and runs it. |
| `scenetest` | 700 checks: serialization, undo, assets, scripts, physics, audio, project scaffolding, picking, packaging, render graph, post chain, settings writer, .NET hosting, the interop boundary, the math layer against glm. |
| `rvpack` | Packages a project into a runnable folder. Headless; no GPU. |
| `rhismoke` | Drives either backend headlessly. |
| `shaderinfo` | Compiles a `.rvshader`, prints reflection + generated GLSL. |

### Flags

Command line or `ragev.ini` next to the executable; command line wins.

```
--rhi=vulkan|opengl      backend (restart-time by design)
--vsync=on|off
--validation=on|off
--frames-in-flight=N
--fixed-hz=N             simulation rate, 20..240, default 60
--width=N --height=N     window size
--audio=on|off           open an output device at all
--scene=<path>           open this scene, not the project's start scene
--benchmark=N            run N frames, print what they cost, exit
--ui-scale=N|auto        editor font and spacing; auto follows the monitor
```

`--audio=off` is not only a mute switch. It takes the same path a machine with
no sound card takes, which is how that path stays working rather than being
assumed to.

`AudioEngine` has three modes — `Device`, `Silent`, and `Offline`, which drives
the same mixing graph with no device and hands back what it produced.
`scenetest` runs its whole audio suite on all three, and measures the mix on
the third. That last one matters more than it looks: every other audio check
can pass while the engine emits silence, because "a voice was created" says
nothing about whether a sample came out of it.

```bash
build/bin/Debug/scenetest/scenetest.exe --rhi=vulkan --dump-audio=out.wav
```

writes what the engine would have sent to the speakers, as a file anyone can
open. Use it when audio is suspected and nobody is sitting at the machine.

### Verifying a change

Run each tool **from its own directory** — assets are staged per target.

```bash
build/bin/Debug/scenetest/scenetest.exe --rhi=vulkan
build/bin/Debug/scenetest/scenetest.exe --rhi=opengl
build/bin/Debug/rhismoke/rhismoke.exe 120 --rhi=vulkan
build/bin/Debug/rhismoke/rhismoke.exe 120 --rhi=opengl
```

> [!TRAP]
> **The failure marker is `FAIL`, in capitals.** Counting failures by grepping
> for lowercase `fail` matches nothing and reports every run as clean. The
> reliable signal is the last line -- `OK`, or `N check(s) failed` -- and the
> exit code. A whole session's worth of "0 failures" was measured with a broken
> grep before the `OK` line caught it.

Then run the editor on both backends. **Zero `[Vulkan]` lines in the log** is
the bar — validation and synchronization validation are both on in Debug, and
every `[Vulkan]` line so far has been a real defect.

If the change touched the **editor UI**, also run:

```bash
python tools/scripts/check_theme_contrast.py
build/bin/Debug/RageVEditor/RageVEditor.exe --project=SampleProject --theme=light \
    --width=1280 --height=720 --screenshot=light.png
```

The contrast script measures both palettes against WCAG 2.2 AA and exits
non-zero on a failure. `--theme=dark|light` and `--ui-scale=N` are what make
checking the other theme and the other scale a script rather than an
afternoon -- and **both find real defects**: every bug in the §8 UI entry
below was found by rendering a case nobody renders by hand.

If the change touched a public script API, also run:

```bash
build/bin/Debug/rvdoc/rvdoc.exe --check
```

It fails when `ScriptableEntity.h` gained a member the manual does not document,
or the manual documents one that no longer exists — in either direction, and
also when it manages to parse suspiciously few members, because a check that
passes by parsing nothing is worse than no check at all.

---

## 2. Projects

A project is a folder with a `.rvproject` in it. **File > New Project...**
creates one and fills it in:

```text
MyGame/
  MyGame.rvproject     name, asset root, start scene, fixed Hz
  assets/{scenes,models,textures,audio,prefabs}
  bin/                 builds land here
  .gitignore           keeps bin/ out of version control
```

Three decisions in that worth knowing:

- **The skeleton is fixed and checked.** `scenetest` asserts every folder,
  because a path like `models/rock.gltf` only means the same thing across
  projects if every project has the same layout, and conventions that nothing
  checks do not stay true.
- **`Project::BinaryRoot()` is `Root()/"bin"`, a convention rather than a
  setting.** Builds belong next to the thing they were built from, so a project
  folder can be zipped or handed over with its output intact. **Build Game**
  goes there; **Build Game As...** asks.
- **The starter scene is not empty** -- ground, cube, sphere, angled directional
  light. An engine that opens on nothing makes the first five minutes a hunt for
  which of six missing things matters, and "no light, so everything is black"
  reads as a broken install. `EditorLayer::PopulateStarterScene`.

`NewProject` creates a *folder* named after the project, not a loose
`.rvproject` wherever the dialog pointed -- the platform layer only has file
dialogs, and taking the picked name literally would scatter `assets/` and `bin/`
into someone's Downloads folder.

---

## 1a. Namespaces

The public API is segregated by domain. There is no `RV` alias -- `RageV` is the
only spelling.

| Namespace | Holds |
|---|---|
| `RageV` | The engine vocabulary: `Entity`, `Scene`, components, `UUID`, `Timestep`, `Application`, `Layer`, `Project` |
| `RageV::Math` | `Vec2/3/4`, `UVec4`, `Mat3/4`, `Quat` and all the arithmetic |
| `RageV::RHI` | Devices, buffers, pipelines, formats |
| `RageV::Audio` | `Engine`, and the mixer behind it |
| `RageV::Physics` | `World`, `ScaleCollider`, `DrawColliders` |
| `RageV::Assets` | `Manager`, `Registry`, `AssetMetadata`, the glTF importer |
| `RageV::Anim` | `Clip`, `Channel`, pose sampling and blending |

**The rule, and it is one rule:** the *machinery* lives in the namespace, and the
small value types that appear in other domains' signatures are re-exported into
`RageV` with a using-declaration. `Audio::Engine::Init` reads as information;
`Audio::AudioBus` on a component field next to a plain `Vec3` reads as noise. It
is the same arrangement `RageV::Math` has used for `Vec3` since it was written.

So: `AudioBus`, `AudioVoice`, `RayHit`, `BodyType`, `ColliderShape`,
`AssetHandle`, `Skeleton`, `AnimationClip` are all still spelled unqualified.
`Asset`, `AssetHandle` and `AssetType` never moved at all -- they live in
`Asset.h` at the root, because every subsystem in the engine names them.

**Types were only renamed where the short name was free.** `AudioEngine` became
`Audio::Engine`, `PhysicsWorld` became `Physics::World`, `AnimationClip` became
`Anim::Clip`. `AudioMode` and `AssetHandle` kept their prefixes, because this
engine names members after their types -- `AudioMode Mode`, `AssetHandle Handle`
-- and a member that shares its type's name hides it, so `Mode::Silent` stops
compiling inside the very struct that needs it.

> [!TRAP]
> **Forward declarations do not follow enclosing-namespace lookup.** `class
> Scene;` inside `namespace RageV::Physics` declares a new
> `RageV::Physics::Scene` that nothing ever defines, and the error appears at
> the use site rather than at the declaration. `PhysicsWorld.h`,
> `PhysicsDebugDraw.h`, `ColliderShapes.h` and `AssetManager.h` all hoist theirs
> into a `namespace RageV { }` block, with a comment saying why.

> [!TRAP]
> **Free functions do not come along for free.** A call like
> `ScaleCollider(collider, scale)` from outside the domain fails even though
> ADL usually saves you: the argument is `RageV::ColliderComponent`, so ADL
> searches `RageV`, not `RageV::Physics`. Anim's functions *are* found
> unqualified, because their arguments are Anim types. The difference is which
> namespace the arguments live in, not which namespace the function does.

---

## 2b. Screenshots from the editor

**Shift+S** and **Scene ▸ Screenshot** write the Game panel's frame to
`<project>/captures/`. `Project::CaptureRoot()` and `NextCapturePath()` own the
path; the editor owns the trigger.

**It needed a new RHI entry, and the reason is worth knowing before reaching
for the old one.** `RHIDevice::RequestCapture` reads the *swapchain* — in the
editor that is mostly editor. A screenshot of the game is the texture the
game's own graph drew into, and nothing could read an arbitrary texture back
before, on either backend. `RequestTextureCapture(texture, callback)` is that,
with the same contract: armed now, satisfied at the next `EndFrame` once the
frame's work has completed, disarmed after it fires, RGBA8 top row first.

- **Vulkan** copies the image into a staging buffer between the frame's submit
  and the present, transitioning from whatever layout the texture is in and
  back to it — the panel samples that same texture next frame and expects to
  find it as the graph left it.
- **OpenGL** attaches it to its own scratch framebuffer and `glReadPixels`,
  then flips the rows, because GL hands them back bottom-up and the contract is
  top-first so a caller never has to know which backend produced it.
- Both refuse anything that is not `R8G8B8A8_UNORM` rather than reinterpreting
  the bits, which would produce a picture of noise.

> [!TRAP]
> **`Shift+S` needs three guards, not one.** `S` is the fly-backward key, so
> without `!flying` a sprint backwards fills the captures folder; and an
> unmodified letter must not fire while a text field has the caret. Both are
> the same guards `W`/`E`/`R` already needed, for the same reason (§7 on the
> gizmo keys).

---

## 2a. The application icon

`tools/scripts/make_icon.py` draws it — a chamfered tile holding a two-tone V,
red half and light half, on the site's near-black. Regenerate with:

```bash
python tools/scripts/make_icon.py
```

Pure stdlib: `zlib` writes the PNGs, `struct` writes the ICO container. No
imaging library, for the same reason there is no Node toolchain for the docs —
a clone builds with nothing but a compiler and one icon is not worth breaking
that. Small sizes are emitted as DIBs and 128/256 as PNG, which is what a
well-behaved `.ico` does.

> [!NOTE]
> **There are two icon mechanisms and both are needed.** `RageVEditor.rc` /
> `RageVRuntime.rc` compile `icon.ico` into the executable, which is what
> Explorer shows and what a pinned taskbar entry inherits.
> `WindowsWindow::SetWindowIcon` loads `assets/icon-32.png` and `icon-48.png`
> through GLFW, which is what the title bar and alt-tab use. Setting only the
> first gives a correct icon in Explorer and a blank default in alt-tab.

**Redrawn 2026-08-22, and the reason is worth keeping.** The first mark was a
rounded square holding a V built from two thick *segments*, which gave it
rounded corners and rounded stroke caps — soft at every size, which is what the
owner said and what a look at it confirms. This one has **no curve anywhere**:
the tile is a square with two opposite corners cut, the V is two straight-sided
quads with mitred ends and a real point, and every test in the file is
point-in-convex-polygon rather than distance-to-a-segment. A distance to a
segment has a round cap whether or not anybody wanted one, and the old icon had
four of them.

Two things in there are derived rather than chosen, and both matter if the
proportions are ever nudged:

- **Where the V's inner edges meet** (`_MEET`) follows from the top, the apex,
  the outset and the stroke width, because the inner edges are parallel to the
  outer ones. Pick it by hand and the inside of the V stops short of its
  outside the first time the stroke changes.
- **`SUPERSAMPLE` is 8, not 4.** Coverage here is a count of samples inside the
  shape rather than a distance, so the number of levels an edge can take *is*
  the supersample squared. A chamfer is a long diagonal across the whole tile,
  which is exactly where 16 levels show as steps. The whole set renders in
  seven seconds either way.

The red is `#e03030`, which is the manual's `--accent` and is shared on
purpose — `tools/rvdoc/main.cpp` emits the same value into the stylesheet, so
the mark and the docs cannot drift apart.

The PNGs live in `RageVEditor/assets/` only — that is the engine's asset root.
The runtime stages them explicitly alongside shaders and fonts, because its
asset list is deliberately a list rather than a copy of the whole directory.

---

## 2b. The developer manual

`docs/manual/` is Markdown; `docs/site/` is the generated HTML. Regenerate with
the `manual` target, or by hand:

```bash
build/bin/Debug/rvdoc/rvdoc.exe --in docs/manual --out docs/site
```

`tools/rvdoc` is a C++ target like the others — a Markdown subset, a syntax
highlighter, a client-side search index inlined as JS so the site works from a
folder without a server, and the reference check above. No new submodule, and no
Node toolchain to install before the docs will build.

`docs/manual/SUMMARY.md` is the table of contents. A page not listed there is not
part of the manual and will not be emitted.

This is a **different document from this one**. The manual is for someone making
a game with the engine; HANDOFF, ROADMAP, ARCHITECTURE and ENGINE-NOTES are for
someone working on the engine. Do not merge them — the first is a description of
supported surfaces, the second is a record of decisions and traps.

---

## 3. Environment

- Vulkan SDK 1.4.357.0, core component only. Building does **not** need it;
  headers and volk are vendored. The SDK supplies validation layers.
- Driver reports apiVersion 1.4.325 — a driver capability, not an SDK version.
  Nothing to fix.
- GPU: NVIDIA RTX 5070 Ti Laptop, driver 591.91. RenderDoc installed; debug
  names and command-buffer labels are wired throughout.
- 13 vendored submodules: imgui, **glm (test-only)**, yaml-cpp, ImGuizmo, PerlinNoise,
  GLFW, Vulkan-Headers, volk, VulkanMemoryAllocator, glslang, **cgltf**,
  **JoltPhysics** (v5.6.0), **miniaudio** (0.11.22).

---

## 4. Architecture

```
Project (.rvproject: asset root, start scene, fixed Hz)
     |
Application  (fixed-step loop, InputMap, AssetRegistry/Manager, AudioEngine)
     |
   Layers -> EditorLayer | RuntimeLayer
     |
   Scene (ECS)   --  PhysicsWorld (Jolt)   ScriptRegistry
     |                     |
     |                contact events
     |                     v
     |                 scripts  -->  AudioEngine (miniaudio)
     |
   RenderGraph  <-  BuildFrame  ->  PostProcess
     |
  Renderer2D / Renderer3D / DebugRenderer
     |
    RHI  ->  Platform/Vulkan | Platform/OpenGL
```

### The render path

One frame, built by `BuildFrame` and shared by both applications -- they differ
only in where the finished image lands (an imported viewport target, or the
backbuffer):

```
(prefilter)      -> roughness levels, 36 small renders, ONCE per environment
(shadow maps)    -> 4 cascades + a map per casting spot and a cube per point,
                    all scene renders, BEFORE the graph opens
(probe faces)    -> probe cube  0..6 scene renders, BEFORE the graph opens
Scene            -> SceneHDR    RGBA16F, linear, no tone curve
  meshes                        reflect the environment cube
  sky                           after the meshes, depth-tested, no depth write
  quads                         blended, so they need the sky behind them
Overlay          -> SceneHDR    colliders, preserved load, depth-tested
Bloom prefilter  -> Bloom0      13-tap, Karis weighted, clamped, soft knee
Bloom down 1..4  -> Bloom1..4   13-tap, halving each time
Bloom up 4..1    -> Bloom3..0   3x3 tent, ADDITIVE, accumulates in place
Tonemap          -> LDR         exposure, + bloom, ACES, 1/2.2
FXAA             -> Output      on the tone-mapped image, not the linear one
```

Eleven passes and seven pooled targets at 1600x900. Adding a Phase 3 feature
means a target and a pass in `FrameGraphBuilder.cpp` and nothing else -- that
is what 3.1 was for, and it held for 3.2.

The sky, the shadow maps, the probe captures and the environment prefilter are
the things *not* in `BuildFrame`. The
sky adds no target and belongs inside the scene pass, between the opaque meshes
and the blended quads. A probe capture opens render passes of its own, so it
cannot be inside one -- both applications call
`Scene::CaptureReflectionProbes()` and `Scene::RenderShadows()` between
`Renderer::BeginFrame` and the graph. Shadows take the camera they are about
to be drawn with, because cascades are fitted to a frustum; the editor's two
viewports therefore fit their own.

**`Renderer::SetTargetFormats` is told what the *scene* pass writes**
(RGBA16F/D32), not what the viewport texture is. Getting that backwards builds
every mesh pipeline against the wrong attachment format.

### The frame

```
clamp frame time (0.25s)
InputMap::Update()                    once per frame
for each fixed step:                  zero or more
    Layer::OnFixedUpdate(dt)          scripts, then physics
    InputMap::EndFixedStep()
alpha = accumulator / dt
BeginFrame -> Layer::OnUpdate(ts) -> ImGui -> EndFrame
```

**Simulation is fixed-step, rendering is not.** Zero steps in a frame is normal
above 60 Hz, which is exactly why rendering interpolates rather than reading the
simulation directly. See ENGINE-NOTES §1.

### Scene modes

| | edit | play |
|---|---|---|
| Scripts | no | yes, on **both** the fixed step (`OnTick`) and the frame (`OnFrame`) |
| Physics | no | yes, after scripts |
| Contact callbacks | no | yes, after the step that produced them |
| Audio | no | starts on play, silenced on stop |
| Restore on Stop | — | full snapshot |

`Scene::OnUpdateEditor` / `OnUpdateRuntime` / `OnFixedUpdateRuntime`.
`OnRuntimeStart` builds the physics world and starts play-on-awake sources;
`OnRuntimeStop` silences everything and tears the physics world down.

One fixed step, in order:

```
scripts (OnTick), C++ then C#
flush destroy queue
update world transforms
physics step
dispatch contacts  ->  OnCollisionEnter/Stay/Exit, OnTrigger*
flush destroy queue          again: a handler may have destroyed something
```

And one **frame**, in `OnUpdateRuntime`:

```
animators
interpolate physics transforms by the frame's alpha
update world transforms
scripts (OnFrame), C++ then C#
flush destroy queue  +  update world transforms again, if any script ran
audio: listener and source positions
particles, CPU then GPU
```

**Pause holds the frame, not just the fixed step** (`Scene::SetPaused`). The
physics blend runs on the *frame*, and the interpolation alpha keeps moving
while the two states it blends are frozen — so a pause that only stopped the
stepping re-blended a falling body to a different point along its last step
every frame, which reads as jittering in place. Paused, `OnUpdateRuntime`
derives world transforms and pushes audio positions and advances nothing:
no physics blend, no animators, no `OnFrame` scripts, no particles.
`OnFixedUpdateRuntime` guards itself too, so a game's own pause menu gets the
same behaviour through `SetPaused` without its layer having to know.
`OnRuntimeStart` clears the flag — a fresh run is never paused.

Audio is deliberately not in the fixed list. Listener and source positions are
pushed on the **frame**, after physics transforms have been interpolated — audio
is presentation, and belongs where rendering is for the same reason. `OnFrame`
sits ahead of it so a script that moves something has that reach audio, the
particle systems and rendering within the same frame.

---

## 5. Invariants that are load-bearing

### Fullscreen passes that take derivatives

- **Never take a derivative of a value computed at a per-pixel scale.** The
  grid picks its spacing per pixel and `floor` makes that jump between
  neighbours, so `fwidth(coord / spacing)` differences two numbers computed at
  different scales -- the derivative of a discontinuity, which is noise. Take
  the derivative of the continuous quantity once and divide *afterwards*. It
  arrived as a field of white speckle across the whole far half of the frame.
- **Do not reconstruct a world position from an NDC depth.** Depth is
  compressed: everything from the far clip to infinity lives in the last
  ten-thousandth of the range, where a float32 has a few hundred distinct
  values. Use it for `gl_FragDepth`, where that compression is exactly what is
  wanted, and use a *ray* for the position -- two points at well-conditioned
  depths keep full relative precision however far out the hit is.
- **No `discard` after an `fwidth`.** A derivative is a difference between
  neighbouring pixels in a quad, so a lane that has already terminated leaves
  its neighbours' derivatives undefined -- and the pixel next to the one you
  wanted to discard is exactly the one that still needs a correct answer. Clamp
  the out-of-range value to something finite, take the derivatives, and
  multiply the alpha to zero at the end. See `grid.rvshader`.
- **Write range tests as "is it inside", never as "is it outside".** Every
  comparison against a NaN is false, so `depth >= 0.0 && depth <= 1.0` rejects
  one and `depth < 0.0 || depth > 1.0` lets it through. The grid's solve
  produces a NaN whenever the camera looks exactly along the plane, which is a
  thing an editor camera does.
- **A fragment shader can replace depth and still not write it.** They are
  separate switches: `gl_FragDepth` decides what the depth *test* compares, and
  `DepthStencil.DepthWriteEnable` decides whether the result is stored. A
  translucent pass usually wants the first and not the second.

### Text and UI

- **A distance field cannot represent a self-intersecting outline.** See the
  section in 8. Run `tools/scripts/prepare_font.py` before `rvfont`, always.
- **Load a distance-field atlas linear and unmipped.** sRGB un-gammas every
  texel, which moves every distance and puts the edge somewhere else; a mip
  chain averages distances from opposite sides of a stroke into a number that
  describes nothing. Both render as text that is soft for no visible reason.
  `Assets::Manager::GetFontAtlas` does it, and the suite asserts the format and
  the mip count.
- **`screenPxRange` is measured from screen-space derivatives, never passed
  in.** A CPU-computed value is correct only for a quad the CPU laid out in
  screen space; the same glyphs on a sign in the world are under a perspective
  divide, where every pixel has a different answer. This is what lets
  world-space text reuse `ui.rvshader` unchanged.
- **Below 2, `screenPxRange` fails outright** -- colour fringes spread across
  the glyph instead of resolving to an edge. It is a property of the *atlas*
  and how large the text is drawn; `rvfont` prints the smallest size its output
  supports (16 px for the defaults).
- **Font metrics are in em units everywhere.** A layout multiplies by the size
  and is finished, and one table serves 12 px and 200 px. A check asserts it,
  because font units leaking through looks almost right.
- **The UI pass has no backend difference.** It draws into the *finished*
  image, and the post chain has already normalised the orientation -- branching
  on the framebuffer origin is wrong in both directions. `BuildProjection` is
  public and asserted per backend for exactly this reason.
- **Hit-testing walks the resolved list backwards.** `UI::ResolveScene` returns
  draw order, so the topmost element -- the only reading of "topmost" that
  agrees with what is on screen -- is the *last* one in it. Walking forwards
  finds whatever happens to be lowest and passes anything that does not overlap,
  which looks correct until two things overlap.
- **`UIButtonComponent`'s Hovered/Pressed/Clicked are not registered fields.**
  They describe what the pointer did, not what the author chose, and a
  registered field is a field the scene file stores -- a click that survived a
  save and a reload would be a click nobody made. That is also why reading one
  from C# needed a protocol entry rather than the component bridge.
- **A click edge is consumed by `UI::EndFixedStep`, at the end of
  `Scene::OnFixedUpdateRuntime`.** Clearing it per *frame* instead loses the
  click whenever a frame runs no simulation step; clearing it before the scripts
  run loses it always. Both are silent, and both are covered.
- **The pointer must arrive in the UI layer's pixel space**, which is the
  caller's job because only the caller knows what the screen is. The runtime
  hands the cursor straight through; the editor maps screen to panel to layer,
  and the last of those three matters only while a splitter is being dragged --
  which is precisely when nobody would blame the splitter.

### Editor UI

- **Never pass a computed width to ImGui without a floor.** A negative width is
  an inverted clip rectangle, which is an *assertion failure*, not a cosmetic
  problem -- the editor stops. The version this replaced used a fixed 140px
  label column, which starved `CalcItemWidth()` on a narrow panel and made
  `PushMultiItemsWidths` produce negatives. Proportional columns, and
  `-FLT_MIN` for "fill", never a negative literal.
- **A fill of the accent must push `OnAccent` as the text colour.** Default
  text on an accent fill measures **3.1:1 in light and 3.2:1 in dark**, against
  the 4.5:1 a label needs. Both themes, so it is not a light-theme oversight.
  `UI::AccentButton` and `UI::IconButton` do both together; there should be no
  raw `PushStyleColor(ImGuiCol_Button, ...Accent)` anywhere.
- **A widget that draws its own label must not also be stretched to the full
  cell.** That combination is what truncated "Base Colo" and "Roughnes": the
  control took every pixel and the label had none. Every labelled row goes
  through `UI::Row*`, which hides the widget's label with `##` because the row
  already drew one.
- **Anything drawn after `DockSpace()` is clipped away.** DockSpace takes the
  whole remaining content region. The toolbar lived there and had *never
  rendered* -- not in any screenshot of this editor, ever. Draw a toolbar
  before the dock space so it can claim its row.
- **A size in pixels is a size that is wrong at every UI scale but one.** The
  toolbar's buttons were 52 and 96 pixels, so at `--ui-scale=2` the font
  doubled and the button did not: "Local" became "Loc". Measure from
  `CalcTextSize` and the spacing tokens.
- **Inside a tree node, draw the row into the draw list, not as items.** An
  ImGui item becomes "the last item", and the drag source, the drop target and
  the context menu all attach to whatever that is. Adding an icon and a name as
  items made ImGui assert `id != 0` -- it refusing to make a text label
  draggable. Draw-list text does not clip like an item either, so it needs
  ellipsising by hand.
- **No colour literals.** Every one that existed was eyeballed against the old
  near-black surface and had no idea a light theme would arrive. Colours come
  from `EditorTheme::Colors()`, which is a role, not a value.


- **There are two script rates and the names say which.** `OnTick` is the fixed
  simulation step, `OnFrame` is the rendered frame. Gameplay in `OnFrame` is
  frame-rate dependent by construction; presentation in `OnTick` judders at any
  display rate that is not the simulation rate, and judders *differentially*,
  which is worse than not smoothing at all because the world around it is
  interpolated. `OnUpdate` no longer exists, in either language, and is kept as
  a `final` / `[Obsolete]` member purely so that a script written against it
  fails loudly.
- **The frame pass never creates a script instance.** Instances are made in the
  fixed pass and nowhere else, which is what makes it *impossible* for `OnFrame`
  to arrive before `OnCreate` -- a structural guarantee rather than a check that
  could be forgotten. A newly spawned entity gets its first `OnTick` before its
  first `OnFrame`.


Every one was a real bug. Breaking them tends to produce intermittent corruption
or silence rather than an obvious failure.

### Renderer

- **Vsync off means `VK_PRESENT_MODE_IMMEDIATE_KHR`, not mailbox.** Mailbox is
  the nicer mode on paper and was the original preference. It is also silently
  wrong in the case that matters: a swapchain created in mailbox *as the
  replacement for a FIFO one* presents at exactly the refresh rate anyway.
  Measured on the RTX 5070 Ti, four runs each -- created mailbox at startup,
  ~450 FPS; recreated mailbox after the surface had been presenting FIFO,
  240.0 FPS every run; immediate from the identical toggle, 442 FPS. So
  unchecking VSync in the editor did nothing at all, while starting with
  `--vsync=off` worked, which is what made it look like a compositor decision
  rather than a mode decision.

- **The PBR shader outputs linear HDR and nothing else.** Tone mapping and the
  transfer function belong to the tonemap pass. They were in the PBR shader,
  which meant every shader writing to the screen had to agree on the display
  transform and only one of them did -- quads and meshes were being shown
  through different ones. It also made bloom impossible, since bloom needs the
  values from before the curve.
- **Only write a descriptor binding the shader actually declares.** Writing one
  past the end of a layout is not a harmless extra; it is out of range and the
  driver takes it badly. Only the tonemap pass declares two samplers.
- **Per-batch storage, not per-frame.** A draw reads its buffer when the *GPU*
  runs it, not when it is recorded. Anything that ends a scene more than once in
  a frame — two viewports, or a batch overflowing 20000 quads — needs separate
  storage each time. Both renderers keep a pool reset by `Renderer::BeginFrame`.
- **A descriptor set that is already bound must not be rewritten.**
  `Material::Bind` used to commit on every draw; two objects sharing a material
  was enough to trip it. Written only on an actual change now.
- **Render targets are resized at the top of a frame**, never from a panel —
  panels draw after `OnUpdate` has already begun a pass on those targets.
- Render-finished semaphores are per swapchain image, not per frame in flight.
- The in-flight fence is reset only once the frame is definitely proceeding.
- Resources never touch the device from their destructor; they hold a
  `shared_ptr<DeletionQueue>` that outlives it.
- Swapchain layout barriers use `srcStageMask = COLOR_ATTACHMENT_OUTPUT`, not
  `TOP_OF_PIPE` — the latter forms no execution dependency.
- Depth layout depends on format: `D32_SFLOAT_S8_UINT` carries stencil and
  forbids `DEPTH_ATTACHMENT_OPTIMAL`. Use `DepthAttachmentLayout()`.
- Every element of a sampler array must be written, even unread ones.
- **OpenGL flat bindings are assigned densely from 0 per resource type**, and
  the map is shared between GLSL generation and binding. `set * 16 + binding`
  overflows `GL_MAX_TEXTURE_IMAGE_UNITS`.
- Camera aspect ratio is a property of the **pass**, not the scene. One camera
  drawn into two panels cannot have one correct stored aspect.

### Scene

- **Anything rewritten every frame needs one instance per frame in flight.**
- **Entities are addressed by UUID, never by `ECS::Entity`.** Handles are
  recycled, and play-mode restore recreates every entity.
- **`Entity`'s members are all `const`.** It is a handle, so const on it means
  "cannot be repointed", not "cannot be used" — the same reading that makes
  `T* const` allow writes through it. Without that, anything holding one by
  const reference, such as a script receiving a `Collision`, could not touch it.
- World transforms are recomputed by one unconditional top-down pass, not a
  dirty-flag cache — a missed flag renders an object in the wrong place
  silently.
- `SetParent` rejects cycles. That is what lets the transform pass recurse with
  no depth guard.
- Structural changes during a UI walk or a script pass are **deferred**. The
  script pass collects handles before stepping any of them.
- Only the parent link is serialized; child lists are rebuilt on load.
- **`Registry::Each` iterates in creation order**, and the serializer writes
  that order straight out. It used to reverse, because EnTT iterated backwards
  and writing its order directly flipped the file on every save/load cycle. The
  reversal went when EnTT did — a correction for a dependency's quirk outlives
  the dependency unless somebody looks for it (7by).
- **A component reference survives an insertion and not a removal.** Pools are
  paged, so adding a component never moves the ones already there; removing one
  moves the last into the hole. `AddComponent` handing back a reference that the
  next `AddComponent` invalidates is the bug this exists to prevent, and it was
  found by a test that held one across two adds.

### Physics

- **Batch body adds.** One at a time leaves a degenerate broad-phase tree, which
  *misses collisions* rather than merely being slow. `AddBodiesPrepare` reorders
  the array, so record the entity→id mapping first.
- **Jolt's globals must be registered before anything Jolt allocates**, which
  includes members constructed in an initialiser list.
- Jolt must use the **dynamic** MSVC runtime to match the project.
- Bodies simulate in world space; the result converts back through the parent.
- Rotations interpolate with **slerp**.

### Contacts

- **The contact listener runs on job threads with every body locked.** It may
  not touch the scene, the body interface, or any engine state — Jolt says a
  locking interface there *deadlocks*. It records the raw fact; everything else
  happens on the main thread after `Update` returns.
- **`OnContactRemoved` cannot read either body.** One of them may already be
  destroyed. Whatever the removal needs must have been cached when the contact
  was added.
- **Jolt withdraws the contacts of a body the instant it falls asleep.** Taken
  at face value that is a box leaving the floor about a second after it landed
  — exactly when it looks most stationary. Neither body being awake is what
  tells that apart from real separation, since bodies that separate are moving.
- **A destroyed body's contacts are reported only on the following step, and a
  pair left asleep is never reported again at all.** `RemoveBody` retires its
  own pairs rather than waiting for Jolt to mention them.
- Contacts arrive from several threads, so their order within a step is the
  scheduler's, not the scene's. They are sorted before delivery.
- Delivery is over a **moved** copy of the queue. A handler may destroy an
  entity, which removes a body, which queues more events — appending to a
  container being iterated.
- A trigger only sees bodies that are **awake**. Something that falls asleep
  inside one stops being reported until it moves.

### Audio

- **Every call works with no output device.** Voices are still allocated,
  tracked and retired, so engine behaviour does not depend on the hardware.
  `--audio=off` takes that path on purpose and `scenetest` runs the whole audio
  suite on it.
- A voice is stopped by an **`on_destroy` signal**, not by a line in
  `DeleteEntity`: entity destruction, component removal and registry clear all
  have to do it. A looping source on a destroyed entity would otherwise play
  until the process ended.
- **A voice is not scene data.** It is cleared on copy and never serialized —
  it means nothing outside the run that created it.
- Shutdown order is sounds, then groups, then the engine. Each holds a node in
  the graph owned by the next, and out of order leaves the audio thread reading
  freed memory.
- `AudioEngine::Update()` runs once per frame from the application loop. Without
  it, one-shots accumulate for the life of the process — nothing else owns them.

### Input

- Sampled once per frame; edges are held until a fixed step consumes them. A
  frame with no steps must not lose a press, and two steps must not see one
  press twice.

### Render graph and post

- **`BuildFrame` is the only description of a frame.** The editor and the
  runtime both call it. Writing the chain twice is how two transfer functions
  ended up in one image before 3.2, and it would happen again within a week of
  shadows landing.
- **A pass writes exactly one target and declares what it samples.** An
  undeclared read returns null rather than working by accident -- a dependency
  the graph cannot see is the first thing to break when passes move.
- **A target may carry several colours; a pass may bind a subset of them.**
  `RGTargetDesc::ExtraColors` and `WriteAttachments`. The subset is not an
  optimisation: a pipeline's declared `ColorFormats` must match what its pass
  binds, so a three-attachment target that could only be bound whole would be
  undrawable by every pipeline that declares one colour -- which is every
  pipeline that draws the scene. Binding a subset is what lets one target with
  **one depth buffer** serve both the scene and a transparency pass; separate
  targets would each own a depth image and the transparent geometry would
  ignore the world. `SameShape` compares the extra formats, or the pool hands
  back a target with the right first attachment and the wrong number of them.
- **`PreserveDepth` clears colour and keeps depth.** `RGLoad` says one thing
  about both, which is right until a pass wants fresh accumulation buffers over
  the depth the scene already wrote.
- **Targets are allocated and resized in `Compile`**, never while passes
  record. Same reason as the editor's viewport targets: resizing something a
  command buffer has bound destroys images it is holding.
- **The bloom upsample is additive and loads rather than clears.** That is what
  lets the chain use one target per level instead of two.
- **A fullscreen pass must flip its sampling coordinate on Vulkan and must not
  on OpenGL.** This is the one that hurt. The RHI gives Vulkan a
  negative-height viewport so geometry lands identically on both -- but that
  decides where a fragment *writes*, not what a texture coordinate *reads*. A
  fragment at the top of the destination samples `v = 1`, which is the last row
  of the source: the bottom on Vulkan, the top on OpenGL. `PostProcess::Dispatch`
  fills `FlipY` for every post shader.

  An **even** number of passes hides it, which is why it shipped in 3.2 and
  survived until 3.3: with anti-aliasing on, the scene goes tonemap -> FXAA and
  comes out the right way up. The bloom chain has an odd number, so its
  contribution was added mirrored about the middle of the frame -- invisible
  until something was bright enough to bleed, then unmistakable as blobs
  floating on the opposite side of the image from every highlight.

  **It bit a second time in 6.8, and the reason is worth more than the fix.**
  This entry used to end "nothing else samples a render target", which stopped
  being true the moment the weighted-blended resolve was written -- a fullscreen
  pass that reads two render targets and lives in `ParticleRenderer`, not in
  `PostProcess`, so it inherited none of this. It is a *single* pass, and one is
  an odd number: Vulkan composited every weighted particle upside down. The rule
  is therefore about what a pass *does*, never about where it lives: **anything
  that samples a render target and writes another owes `FlipY`.**

  It survived a full session of clean runs because the only test scenes were
  plumes near the middle of frame, and smoke that is flipped still looks like
  smoke. What found it was rendering the same particles as sorted `Alpha` and
  comparing the *silhouettes* -- an image is only checkable against something
  that is right by construction. `tools/scripts/check_oit.py` is now that check.
- **The cube-map face table is the contract between capture and sampling.**
  `CubeFaceDirection` is what both specifications give and they agree, so one
  CPU conversion feeds both backends. A probe's capture basis is checked against
  that table in `scenetest` rather than by looking, because a mirrored face, a
  rotated one and an upside-down one all produce a reflection that tracks the
  camera correctly and is simply wrong.
- **`CopyToTextureLayer` is where the two backends' row order is reconciled**,
  and the only place. Vulkan blits flipped; OpenGL copies straight through.
- **OpenGL needs `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)`.** glm is built
  with `GLM_FORCE_DEPTH_ZERO_TO_ONE`, and without this GL maps that clip range
  onto `[0.5, 1]` of the depth buffer. Half the precision, and a stored depth
  that no longer equals what a shader computes from the same matrix — every
  shadow comparison passed and the backend drew no shadows at all. The origin
  stays lower-left: flipping it would invert every render target's row order.
- **`CopyToTextureLayer` detaches before it attaches, on OpenGL.** Its two
  framebuffers are shared by every copy — a probe's colour faces and a point
  light's depth faces, at different sizes. A stale attachment of the wrong size
  is not an error in GL 4.5: the blittable region becomes the intersection, so
  the copy silently moves a corner and the rest of the face keeps what it had.
- **A render target's depth attachment is `TransferSrc`.** Nothing copied one
  until point shadows did, and Vulkan refused the blit.
- **A pooled render target is keyed by its own size, never by who asked for
  it.** The environment prefilter kept one chain keyed by base size and threw
  it away when a different environment arrived — which happened *mid-frame*,
  the moment a 128-pixel probe was filtered after the 512-pixel sky, destroying
  images the command buffer still had bound.
- **A per-frame descriptor cursor resets per frame, not per call.** Same bug,
  second form: the prefilter reset its cursor on entry, so a second environment
  filtered in the same frame rewrote sets the first had already bound.
- **Caches that hold the same object by different keys are cleared together.**
  `TextureLoader` holds environment maps by path, `AssetManager` by handle, and
  `EnvironmentIBL` holds their filtered cubes by raw pointer. Two of the three
  cleared only at shutdown, so changing project leaked every previous project's
  maps — and the pointer-keyed one had no way to know its keys had died.
  `AssetManager::ClearCache` now clears all of them.
- **A cube's face size is sent to the shader, not derived from its mip count.**
  `exp2(highest mip)` equals the face size only when the chain runs down to one
  texel. A prefiltered cube stops at its roughness levels, so the derivation
  gave a sixteenth of the real size and silently switched off the reflection's
  screen-space anti-aliasing term — a regression introduced by the prefilter
  itself, in code that had been correct until the thing it read changed shape.
- **A frustum's near plane is row 2 alone, not row 3 plus row 2.** glm is built
  with `GLM_FORCE_DEPTH_ZERO_TO_ONE`. The OpenGL form of that plane sits half
  the frustum away and culls geometry in plain view.
- **Each pass culls against its own frustum.** A shadow cascade sees a
  different volume from the viewer; culling it against the camera removes
  exactly the casters standing outside the view whose shadows fall inside it.
- **A module that logs "ready" must mean it, and must be askable.** Seven
  renderer subsystems announced readiness unconditionally, so a shader that
  would not compile produced a feature present in every sense except that it
  did nothing — nothing failed, nothing was red, and the only symptom was a
  picture that looked slightly wrong. The log is not the fix, because a log
  line cannot be tested: every subsystem now carries an `IsReady()` and
  `scenetest` asks all of them.
- **Nothing is culled in the shadow pass, on purpose.** Culling front faces
  hides acne by recording the back of each caster, and moves every shadow away
  from its caster by that thickness. On a sphere that is a diameter. Acne
  belongs to the normal-offset bias.
- **Cascades are fitted to a sphere and snapped to texels.** Both fix things
  that are invisible in a still frame: a box fit changes size as the camera
  turns and makes edges crawl; an unsnapped projection makes them shimmer on
  sub-texel movement. `scenetest` turns a camera through a circle and nudges it
  a millimetre at a time, because a screenshot cannot.
- **Mip generation belongs in the command buffer that wrote mip 0.**
  `RHITexture::GenerateMips` submits its own buffer and waits, so it runs
  *before* anything recorded into the current frame. A reflection probe's faces
  are recorded into the frame, so its chain was built from an empty mip 0 and
  every rough surface reflecting it read black for six frames. Use
  `RHICommandList::GenerateMips`. OpenGL cannot show this: one queue, no
  recording, so its order was already right.
- **A descriptor pool is a chain, not a ceiling.** A thousand meshes exhausted
  a fixed pool of 2000 sets and the backend segfaulted. Allocation adds a block;
  each set remembers which block owns it, because that is what it must be freed
  to. ImGui gets a pool of its own — it takes a handle once and keeps it, so it
  cannot follow the chain.
- **A registered enum field must be int-sized.** Reflection is type-erased, so
  every consumer of an `Enum` field — the serializer, the undo stack, the
  inspector's combo, the C# component bridge — reaches it through an `int*`.
  Declare one `: uint8_t` and it compiles, looks right in the inspector, and
  writes three bytes past its end into whatever member follows. `Field<>`
  static_asserts it now, and a suite check makes the same claim about the
  registry as it stands (which also covers a descriptor built by hand rather
  than through `Field<>`). The fix is always to drop the narrow underlying
  type: a component's enum is one field on one instance, never a packed array.
- **A deferred deletion is only deferred if its slot is not the next one
  flushed.** Entity destruction happens in the simulation phase, before
  BeginFrame — and a deleter pushed there used to land in the very slot
  BeginFrame flushes that same iteration, behind a fence covering a frame from
  two submissions ago, while the *previous* frame still executed. Out-of-frame
  pushes now slot by the most recently submitted frame; in-frame pushes stay in
  the recording frame's slot, since the resource may already be in its buffer.
  Found by the first game that destroyed material-bearing entities mid-play,
  and only under `--validation=on` — the GPU usually wins the race.
- **Identical state must be the same object, or a batch key cannot see it.**
  Every material built its own sampler from an identical description, which
  made every material's batch key unique and stopped the lit pass batching
  anything at all while the shadow passes batched fine. Draws fell 74% and the
  frame got *slower*.
- **A batch's base instance is a push constant, never the draw's
  `firstInstance`.** Vulkan's `gl_InstanceIndex` includes the base and OpenGL's
  `gl_InstanceID` does not, so reading it from the draw offsets twice on one
  backend and once on the other.
- **Every backend has more than one frame in flight, and a fence between
  them.** OpenGL reported 1 and had no fence at all, so every subsystem kept a
  single copy of its per-frame buffers and memcpy'd into it while the GPU was
  still reading the previous frame — persistently mapped, coherent, nothing in
  the way. A static scene rendered three different images across six frames.
  Invisible until the frame got cheap enough for the CPU to lap the GPU, and it
  read as a shading shimmer rather than as corruption. `scenetest` asks the
  device now.
- **A directional light is never binned into the cluster grid.** It has no
  position and reaches every cell, so binning it puts a copy in all 3456 of
  them. They sit at the front of the light buffer and every fragment reads them
  unconditionally.
- **The cluster tile comes from the interpolated clip position, not
  `gl_FragCoord`.** The two backends put a normalised device coordinate on
  opposite halves of the framebuffer, so a tile derived from the fragment's
  pixel would need the target's size and a per-backend flip. The NDC needs
  neither and is the space the grid was built in.
- **`far` recovered from a projection matrix is only good to about a tenth of a
  percent.** It comes from `P[2][2] + 1`, and at a far-to-near ratio of twenty
  thousand that addition cancels almost every bit a float has. Harmless here
  only because the shader is given the slice scale and bias derived from the
  same value, so the two sides agree with each other whatever the arithmetic
  did.
- **A vertex attribute's reflected format must carry its vector width.** The
  reflection switched on the base type and returned a one-component format for
  every integer, so a `uvec4` of joint indices came back as four bytes where the
  buffer holds sixteen. Every attribute after it then sat at the wrong offset
  and the mesh drew as a spray of triangles. Nothing about that is visible in a
  compile, a validation layer or a draw count -- `scenetest` reflects the
  skinned shader and checks the stride against `sizeof(SkinnedVertex)`.
- **A skinned mesh needs a full pose, never a short one.** Its vertices name
  bones by index, so a run shorter than the skeleton leaves them reading past
  their own instance's bones into the next character's. A mesh with no animator
  is given one identity *per bone*, which is what the bind pose is.
- **Whatever poses the lit pass must pose the depth pass.** Skinning the lit
  shader alone leaves a character walking while its shadow stands still in the
  bind pose, and that reads as a shadow bug for an afternoon.
- **A swapchain is retired, not destroyed and replaced.** `vkDeviceWaitIdle`
  waits on the *queues* and says nothing about the presentation engine, which
  may still hold images of the old swapchain and still be waiting on their
  semaphores. Destroying it first and creating a new one worked every time
  until the one time the compositor was a frame behind. The old handle is
  passed as `oldSwapchain` and destroyed after the new one exists; the
  per-image semaphores go with it, not before it.
- **A timestamp slot is claimed per scope, never fixed per phase.** A phase can
  run more than once in a frame — the editor fits shadows to each viewport and
  runs the graph for both — and the second pass then rewrites a query the first
  already wrote, which Vulkan rejects. Spans are summed per phase.
- **A query pool that has never been reset cannot be read.** Not an empty
  answer: a validation error. The first pass through each frame slot resets
  without reading.
- **A CPU timer around a call that can block measures the wait, not the work.**
  With vsync on, the profiler attributes the whole frame to whichever call
  happens to stall — the probe capture on Vulkan, ImGui on OpenGL. Neither is
  the cost. Phase attribution is only meaningful with vsync off, and properly
  only with GPU timestamps.
- **A solid primitive's triangles must face away from its centre.** The sphere
  was wound inside out for four roadmap phases: back-face culling kept the far
  hemisphere and drew its inside, which has the same silhouette and only looks
  wrong once something reads the normal. `scenetest` checks all of them now.

### Compute, and blending across two attachments

- **A dispatch must be recorded outside a render pass.** Vulkan forbids it;
  OpenGL would allow it. Both command lists assert, so a pass that obeyed the
  rule only on the backend that complains cannot ship. This is why the GPU
  particle simulation runs from `Scene::OnUpdateRuntime` and not from
  `OnRender` -- and `OnUpdateRuntime` is also **once per frame**, where
  `OnRender` runs once per *view* and the editor has two.
- **A resource set holds one descriptor set per frame in flight, and `Commit`
  writes only the current frame's.** Committing once at creation populates one
  and leaves the rest never written. Validation catches it on frame two; a
  release build renders garbage. Rebind and commit every frame -- cheap, and
  what every renderer here already did.
- **A persistently mapped host-visible buffer written by a compute shader is a
  synchronisation point on OpenGL.** The particle state buffer was mapped so a
  rare seed could be a memcpy; it made the GPU path *slower than the CPU one*
  (6.9 ms against 3.3 ms) and stopped GPU timestamps resolving at all. Device
  local, seed through staging.
- **Barriers bracket a batch of dispatches, not each one.** A barrier orders
  everything around it, not only the buffer it names, so interleaving makes
  every dispatch wait for the previous one.
- **`independentBlend` is a Vulkan device feature.** Different blend state on
  different attachments of one pipeline is not free; without it every
  attachment must match the first. Enabled where supported, with a warning and
  a fallback where not.
- **`glDrawBuffers` is persistent framebuffer state.** Set it on every pass, or
  a pass that bound a subset leaves the next pass over that target writing into
  its selection.
- **`glClearBufferfv` indexes the draw-buffer list, not the framebuffer.**
  Binding attachments 1 and 2 means clearing indices 0 and 1.
- **A weight that saturates its clamp is not a weight, it is a constant --
  and in weighted-blended transparency a constant weight cancels exactly.**
  The resolve divides accumulated colour by accumulated alpha, so only the
  *ratio* between overlapping fragments survives. Scale them all alike and the
  ratio is one: the output is an unweighted average that still looks like
  plausible transparency. The shipped weight did this at every distance, and
  it was proven by replacing the whole expression with the literal `1.0` and
  getting a **pixel-identical** image. When a formula's output feeds a
  normalisation, the test is not "is the number sensible" but "does it still
  *vary*" -- a constant is the one value that hides inside any normalisation.
- **`gl_FragCoord.z` is not a distance and must not be used as one.** With the
  sample scenes' near plane of 0.01 it spans 0.995 to 1.0 across the entire
  world -- 4% of range doing the work of a depth term. Anything wanting linear
  view distance should take `1.0 / gl_FragCoord.w`, which is exact for this
  projection because clip.w *is* the view distance. Constants copied from a
  paper or a blog carry that source's near plane with them, silently.

### Lifetime and shutdown

- **`Layer`'s destructor is virtual, and must stay so.** `LayerStack` owns
  layers as `Layer*` and deletes through that pointer. Without it only `~Layer`
  ran and every derived member leaked — the editor's scene, its render targets,
  every material in them. Undefined behaviour that survived months because
  nothing exited cleanly enough to notice.
- **Anything released on the deletion queue must tolerate the systems it talks
  to being gone.** The queue's final flush is in the device destructor, by
  which point the ImGui backend has shut down and taken its descriptor pool.
  `VulkanTexture` checks `IsImGuiVulkanReady()` before freeing its set.
- **Wait for the device to go idle before destroying anything, not partway
  through.** The loop can exit with frames still executing -- `--benchmark` and
  `--screenshot` both stop it in the iteration that submitted one, and a window
  close does the same -- so `~Application` idles first and clears the layers
  after. It used to be the other way round, and every benchmark run produced
  seven "currently in use by VkCommandBuffer" errors as the layers destroyed
  buffers, samplers, descriptor sets and pipelines out from under the GPU.
- **Verify shutdown by exiting, not by killing.** Every one of these was
  invisible for as long as runs were terminated rather than closed. `exit 0`
  is part of the bar now; `--screenshot` gives any app a clean exit to check.
- An assert with no debugger attached **exits** rather than showing the modal
  dialog (`Entrypoint.h`). The dialog parks the process with every subsystem
  live, and an abandoned instance holding the audio device repeats a fragment
  of whatever it was playing — several at once sound like a broken machine.

### Build

- **A translation unit whose only contents are static registrars will be
  dropped from a static library.** This no longer bites the engine, which is a
  DLL and therefore linked from its object files, but it is why built-in scripts
  are still registered by an explicit function referenced from
  `ScriptRegistry` -- and it would bite again the moment anything goes back into
  a `.lib`.
- **A tool must not link a vendored library the engine already links
  privately, and must not call one whose state lives in globals.** `RageV.dll`
  links `glfw` `PRIVATE`. `rhismoke` linked it *again* and called `glfwInit`
  and `glfwCreateWindow` itself, so the executable and the DLL each held their
  own GLFW with their own globals. The window was real -- the tool's null check
  never fired -- and the engine's copy had never heard of it, so
  `glfwGetWin32Window` inside `VulkanDevice::CreateSurface` answered null and
  Vulkan refused the surface with `hwnd is NULL`. OpenGL failed one step
  earlier and looked like a different bug entirely: glad was handed a
  proc-address loader belonging to a library nobody had initialised, and
  reported `gladLoadGLLoader failed`. **One cause, two symptoms that do not
  resemble each other.**

  Fixed 2026-08-11 by creating the window through `Window::Create` and dropping
  `glfw` from the tool's link line. `WindowProps::Visible` exists for exactly
  this case and already carried the diagnosis in its comment -- the prop had
  been added and `rhismoke` was never migrated onto it, which is its own
  lesson: a fix that leaves the old path compiling leaves it in use.
- **Static data members read by an inline accessor do not cross a module
  boundary.** A consumer links its own copy of the data rather than the
  engine's. `Application::Get`, `Log`'s two loggers and
  `Platform::GetPlatformType` are out of line for this reason; anything added
  with that shape has to be too.
- **A library with globals must be linked into one module only.** ImGui, GLFW
  and ImGuizmo each keep their state in globals. An executable that links its
  own copy gets its own state, and the engine cannot see it: that is exactly how
  `scenetest` came to fail with a null HWND on Vulkan and no current context on
  OpenGL. Either the module is linked once, or the state is handed across
  explicitly -- see `ImGuiBinding.h`.
- **Auto-export and LTCG are mutually exclusive.** `/GL` writes intermediate
  language into the object files, and the tool that scans them to build the
  export list cannot read it. The DLL would export nothing at all.
- Each app needs its own output directory, and `RageV.dll` is staged into each
  of them.

---

## 6. Current state

Everything below is what is true after a session that took Phase 3 from 3.2 to
the end of its lighting. Where something is listed as working, it has been run
on **both backends** with validation on and its frames compared; where it is
listed with a caveat, the caveat is real and was found rather than guessed.

### Works

| Area | State |
|---|---|
| Build | CMake, 13 vendored submodules; Debug, Release and **Dist** all build |
| RHI | Two complete backends, Vulkan + OpenGL, switchable at startup |
| Renderer | Cook-Torrance PBR, materials with 5 maps, primitives, **any number of lights** |
| Identity | Real UUIDs, `GetEntityByUUID` |
| Hierarchy | Parenting, world transforms, drag-to-reparent |
| Reflection | `ComponentRegistry` drives inspector + serializer + add menu |
| Inspector labels | Field names read as sentences; the serialized key is untouched |
| Serialization | Version 5, lossless round trip, subtree snapshots |
| Undo/redo | Full command stack, everything routes through it |
| Assets | Handles, `.meta` sidecars, content-hash cache, content browser |
| Import | glTF 2.0 via cgltf — meshes, materials, textures, node trees |
| Prefabs | Create, instantiate with id remapping |
| Loop | Fixed timestep, clamp, interpolation |
| Play mode | Snapshot / restore, pause |
| Input | Actions, axes, bindings, contexts |
| Scripting | Registry by name, rich native API, six built-in scripts |
| Physics | Jolt — rigid bodies, 3 collider shapes, triggers, raycasts |
| Contacts | Collision and trigger enter/stay/exit into scripts, both sides |
| Audio | miniaudio — clips as assets, 4 buses, 3D sources, listener, one-shots |
| Editor | Two viewports, proportional dock layout, red-on-black theme |
| Editor scene | Opens the project's start scene, so it matches the runtime |
| Picking | Click to select in the viewport, triangle-exact; colliders too |
| Debug draw | Collider and trigger wireframes on F3, sleeping bodies dimmed |
| Projects | A folder is a project; the asset registry roots there |
| Runtime | `RageVRuntime` opens a project and runs its start scene |
| Capture | `--screenshot=<file>` writes a PNG of one frame and exits |
| Packaging | `rvpack` and File > Build Game: a runnable folder, ~9 MB |
| Render graph | Declared passes, pooled targets, compile-time validation (3.1) |
| Post chain | HDR scene, 5-level bloom with Karis weighting and a clamp, ACES, FXAA (3.2) |
| Sky | Colour, gradient, or an environment map; panoramas and six-file sets (3.3) |
| Cube maps | Per-face upload, CPU panorama conversion, mip chains (3.3) |
| Reflections | Surfaces reflect the environment, mip chosen by roughness *and* by screen derivatives |
| Reflection probes | Baked and realtime, one face per frame, captured into a cube (3.3) |
| IBL | Irradiance convolution, GGX prefilter per roughness level, BRDF table (3.4) |
| Shadows | Directional cascades, spot maps, point cubes; per-light toggle (3.5) |
| Ray tracing | Vulkan with ray queries only: one checkbox under Shadows traces every casting light; options under it trace reflections and ambient occlusion in place of SSR/SSAO. Absent from the panel on OpenGL (8.12) |
| Terrain | A heightfield asset (`.rvterrain`) and component: chunk meshes at four levels of detail with skirts, up to four materials blended by paint stored in the asset, a brush that raises/lowers/smooths/flattens and paints the layers (one stroke = one undo, saved with the scene), Jolt height-field collision, culled/shadowed/traced/picked as ordinary meshes (8.4 stages 1-3) |
| Subsystem health | Every renderer module has `IsReady()`, and `scenetest` asks all seven |
| Culling | Frustum culling per pass, against each pass's own frustum (3.6) |
| Clustered forward | 16x9x24 cells, lights binned on the CPU, no light cap (3.8) |
| Skeletal animation | Skinned vertex format, skinned PBR and depth shaders, per-instance bone matrices, `AnimatorComponent` (3.7) |
| Batching | Instanced draws keyed on mesh and material; 3238 draws down to 60 on the stress scene |
| Profiler | CPU wall time and GPU timestamps per phase, live in the editor and printed by `--benchmark` |
| Tests | `scenetest`, **700 checks**, green on both backends |

**Phases 0, 1, 2, 3 and 4 are complete.** Phase 5, C# scripting, is in
progress -- 5.1 (hosting) is done, and **5.0 now precedes the rest**: no
third-party type may appear in a public header, and the public API is being
segregated into `RV::Math::`, `RV::Audio::` and friends. That is an API break
the C# bindings must not be written before, or they get written twice.

### Works, with a caveat worth knowing

These are all *implemented and running*. Each has a limit that is real, was
found rather than assumed, and is not a bug so much as a thing not built yet.

| Feature | The caveat |
|---|---|
| Reflection probes | One point of capture, so no parallax correction. Move a reflective object away from its probe and the reflection slides. |
| Reflection probes | One probe per scene render, chosen by distance to the **camera** rather than per object. With one probe in a scene the two agree; with several they do not. |
| Reflection probes | Prefiltered on the frame each completes a round of faces, so a realtime probe's roughness levels are up to six frames behind its capture. |
| Reflection probes | A probe inside a closed mesh only works because back-face culling hides the mesh. Placing one where geometry surrounds it in an open shape will capture that geometry. |
| Reflection probes | Realtime probes update one face per frame, so a reflection lags by up to six frames. |
| Shadows | Only the **first** directional light gets cascades, and four spot and four point maps exist. Beyond that a light lights but does not shadow — it now warns once per scene rather than doing it silently. |
| Shadows | Spot and point resolutions are derived from `ShadowResolution` (half and quarter) rather than being independently settable. |
| Shadows | A point light's near plane is a shared constant, not per light. |
| Lighting | Clustered forward. No cap on lights; shadow *casters* are still budgeted at one cascade set, four spot maps and four point cubes. |
| Lighting | Clustering is a loss when every light reaches the whole scene — the busiest cell then holds all of them and the indirection buys nothing. The benchmark reports that number so the case is visible rather than mysterious. |
| IBL | The prefilter assumes the surface is viewed head on, which is the standard split-sum approximation. It costs the stretched highlight a surface has at a grazing angle. |
| IBL | Irradiance is convolved once, at load. A scene that changes its sky colours at runtime rebuilds the gradient cube but a loaded environment map's irradiance is fixed. |
| Anti-aliasing | FXAA only. SMAA needs two lookup textures vendored; TAA needs motion vectors first. |
| Editor | With the game viewport open, shadows are rendered **twice** — once fitted to each camera. Correct, and twice the cost. |
| Bloom | The clamp defaults to 16, which bounds what one pixel contributes. A genuinely enormous highlight blooms less than energy conservation says it should. |
| Materials | Not assets. Two entities cannot share one from the inspector; each carries its own. |
| Lights and cameras | No billboard icons, and not clickable — picking tests geometry and they have none. |
| Audio | `.ogg` is deliberately not claimed: it needs stb_vorbis, and a file that imports and then will not play is worse than one that does not import. |
| Particles | A GPU emitter cannot sort its own alpha — sorting would need a readback. Use `Additive` or `WeightedBlended` there. Emitters are still sorted against each other. |
| Particles | Weighted blending is an approximation and is loose in one known way: a stack of equally transparent layers spread over a very large depth range renders with the near one too strong. No depth-only weight avoids that; `Alpha` is what exactness costs a sort. Measured in §8 (6.9). |
| Particles | No sub-emitters and nothing collides. Curves landed in 6.10. |

### Not built

- ~~C# scripting~~ -- **built, all of phase 5**, including hot reload, and as
  of interop protocol 4 the C# surface mirrors the C++ one entirely: audio,
  raycasts, hierarchy, LookAt, SpawnPrefab by path, and component access by
  registry name with text values -- the boundary's answer to GetComponent<T>,
  driven by the same ComponentRegistry as the inspector so it cannot go
  stale. Mid-play builds stop, swap and resume Play, in both languages.
- ~~Particles~~ -- **built**: an emitter component, CPU and GPU simulation with
  a switch that allocates nothing, billboard and flat facings (3D and 2D from
  one component), and three blend modes including order-independent. See the
  manual's particles page. The order-independent path is validated as of 6.9,
  and **curves are done as of 6.10** -- size, colour and alpha as `.rcurve`
  assets with a draggable editor, read identically by the CPU and GPU paths.
  Still missing within it: collision and sub-emitters, both deferred on
  purpose and scoped in §8. **GPU alpha sorting landed in 6.11**, exact up to
  2048 particles per emitter.
- ~~Text and game UI.~~ **Built, all of phase 6**: an MSDF font pipeline,
  screen-space canvases with anchors, buttons with hit-testing and bound
  handlers, and world-space text. Knockdown has a title and a live score,
  which is what the phase existed for.
- ~~A per-frame script hook.~~ **Done 2026-08-11**: `OnFrame` in both
  languages, `OnUpdate` renamed to `OnTick`, and the interpolation alpha
  readable from a script. See §8.
- **Front-to-back depth sorting.** Opaque draws are grouped by mesh and
  material so they batch, which is the half of 3.6 that was worth measuring.
  Sorting them by depth as well would let early-z reject more, and is worth a
  measurement before it is worth code.
- ~~**Animation blending between clips.**~~ **Done 2026-08-13 (7.5)**: a
  change of `Clip` starts a cross-fade, with the outgoing clip still running
  while it fades. See §7k of ENGINE-NOTES.
- ~~**Skinned bounds are the bind pose's.**~~ **Done 2026-08-13 (7.6)**: a box
  per bone, unioned over sampled poses of every clip, computed at load.
- ~~**A `.glb` imports untextured.**~~ **Done 2026-08-13.** `GltfImporter`
  only read a texture when the image had a `uri`, so images stored in a GLB's
  buffer views -- which is what glTF-Binary *is* -- were skipped silently, and
  a `data:` URI was stored as a *filename*. Both now extract beside the model
  and become ordinary project assets, which is what lets a material hold a
  handle to one. The fox is orange. **The import cache version was bumped to
  `v2` with it**: the model's source hash did not move, so a `v1` `.rvmesh`
  would keep serving the empty texture list it was cooked with and the fix
  would look like it had not worked -- *the importer counts as an encode rule*.
- **An archive format.** Packaging emits a folder, not a `.pak`. Packing needs
  a virtual file system on the loading side to be worth anything.
- **Asset cooking.** glTF is parsed at load, PNGs decoded at load.
- **SMAA and TAA.**

### What a frame costs

Measured, finally, with `--benchmark`. Release, 1600x900, 400+ frames after a
warm-up fifth. **Every number here is meaningless without the vsync column**,
which is why the benchmark prints them together and warns when vsync is on.

| Scene | Backend | vsync on | vsync off |
|---|---|---|---|
| sample, 12 meshes | Vulkan | 4.17 ms | **0.80 ms** (1255 FPS) |
| sample, 12 meshes | OpenGL | 4.17 ms | **0.97 ms** (1027 FPS) |
| stress, 1000 meshes | Vulkan | — | **1.88 ms** (531 FPS) |
| stress, 1000 meshes | OpenGL | — | **1.59 ms** (630 FPS) |

4.17 ms is 240 Hz to three figures. **The panel was the entire frame-time
history of this project.** Both applications still ship with `vsync = on`; that
is right for a game and wrong for a measurement.

**The renderer is CPU-bound on these scenes** — the phase total is 95-98% of the
frame on both backends. What that does *not* say is which phase, because these
are CPU wall-clock timers around calls that can block: with vsync on they
attribute the whole wait to whichever call happens to stall, which is the probe
capture on Vulkan and ImGui on OpenGL. Attributing GPU cost needs timestamp
queries, which do not exist yet.

Draw counts, stress scene: **18018 considered, 14780 culled, 3238 drawn before
instancing, 60 after.** The four distinct meshes in that scene now cost about
four draws a pass instead of one per object.

The gain was not symmetric and the reason is worth keeping. A resolution sweep
before instancing showed the cost was resolution-*independent* — OpenGL 5.03 ms
at 640x360 against 7.17 ms at 1600x900 — so it was submission, not fill.
Instancing then took OpenGL from 7.17 ms to 1.59 ms and Vulkan from 1.96 ms to
1.88 ms. **Vulkan was never submission-bound at this scale.**

### The "Vulkan waits so much" question, measured (2026-08-15)

Asked because the panel made Vulkan look worse than OpenGL. The benchmark
says the opposite -- demo scene, Release, vsync off:

| app | Vulkan | OpenGL |
|---|---|---|
| runtime | **1.771 ms (565 FPS)** | 2.067 ms (484 FPS) |
| editor | **2.308 ms (433 FPS)** | 2.867 ms (349 FPS) |

**Vulkan is faster in both applications.** What the user was seeing is an
attribution artifact, and it is now fixed rather than explained away: with
vsync on, both backends sit at exactly the display's refresh, and the wait
has to land somewhere. On Vulkan it lands in `vkWaitForFences` +
`vkAcquireNextImageKHR` inside device BeginFrame -- which no phase wrapped,
so the editor's panel showed ~3 ms of anonymous dead time (and early
capture-heavy frames inflated the phase columns). On OpenGL the driver
throttles *inside whatever call overfills its queue* -- ImGui's submission,
3.5 ms of "imgui" that is really the display -- so its wait wears a work
phase's name and looks innocent.

Two changes, so the next person reads the truth off the panel:

- **`wait (gpu/vsync)` is a phase now**, wrapping device BeginFrame. Vulkan
  vsync-on: unaccounted fell from 3.2 ms to 0.04, the wait says 2.96, and
  the verdict line says "waiting". Vsync off it reads GPU backpressure,
  which is what "GPU bound" looks like from the CPU.
- **No verdict is computed under vsync.** The wait row catches Vulkan's
  block, but OpenGL's hides inside a working phase and any verdict computed
  from those numbers repeats the lie -- the line now says "at the display's
  refresh" and points at the phase-split caveat.

One real cost surfaced on the way, then measured properly (subtraction
sweep, runtime, 2560x1440, vsync off): the demo's **Realtime reflection
probe costs 0.73 ms mean and owns the frame-time spikes** -- p95 6.6 ms
with it, 2.7 baked. The machinery is right (one face per frame, convolve
every sixth); the cost is that each face is a scene render and the
convolution is 36 small passes whose *barriers serialize on Vulkan* --
which is also why the probes phase reads 0.70 ms on Vulkan against 0.18
on OpenGL: bubbles, not work. **The refresh-rate dial is built** (same
day): `ReflectionProbeComponent::Rate`, a dropdown -- 15/30/45/60 Hz or
PerFrame -- gating the capture step on an accumulated frame-time clock,
default PerFrame so no existing scene changes. The demo probe runs at
15 Hz: 1440p mean 2.94 to 2.28 ms, probes GPU 0.70 to 0.03, p95 6.6 to
2.6 -- the spikes were the per-frame capture. Merging the convolution's
serialized barriers remains the deeper unbuilt fix. The rest of the sweep: DoF 0.28,
lens trio 0.18, auto exposure 0.14, bloom 0.09, TAA-over-none 0.19,
LUT ~0, SSAA doubles the frame. Nothing misbehaving -- the demo profile
simply orders everything on the menu at native resolution.

**The editor's scene view no longer applies cinematic post** (2026-08-15):
depth of field, grain, vignette and chromatic aberration are stripped from
the scene chain unless **View > Preview Post** is on -- through the editor
camera, DoF focus-blurs a distance authored for a different lens, which
read as a broken viewport. The Game panel, and the viewport when switched
to the scene camera, always keep everything; bloom, exposure, the grade
and the AA stay everywhere. The toggle persists in panels.ini.

### And where Vulkan's remaining time actually went

*Measured 2026-08-12. The note above said this was "unattributed and the next
thing to measure" and stayed that way for a while, because attributing it needed
GPU timestamp queries that did not exist yet. They do now, and the answer took
one command.*

Stress scene, 1000 meshes, Release, Vulkan, vsync off, 400 frames:

| phase | CPU ms | GPU ms |
|---|---|---|
| shadow maps | 0.495 | 0.231 |
| reflection probes | 0.217 | 0.257 |
| render graph | 0.220 | 0.424 |
| imgui | 0.083 | 0.003 |
| present | 0.122 | — |
| **accounted** | **1.138** | **0.916** |
| unaccounted | 0.278 | — CPU idle, waiting on the GPU or the present |

Whole frame: **0.922 ms of GPU work in a 1.417 ms frame.** Nothing dominates,
which is why it read as mysterious -- there was no single villain to find.

**One line looked actionable and was mostly an artifact of the test scene.**
The reflection probe was 31% of the frame -- 1.417 ms down to 1.086 without it.
But `make_stress_scene.py` writes `Update: Realtime`, and **realtime is not the
default**. The same scene with the probe left at `Baked`, which is what
`ProbeUpdate` defaults to:

| probe | CPU ms | GPU ms | frame |
|---|---|---|---|
| Realtime | 0.206 | 0.248 | 1.386 ms |
| **Baked** | **0.066** | **0.001** | **1.142 ms** |

A baked probe captures once and costs nothing afterwards, exactly as designed.
So there is no probe problem to fix: there is a stress scene that opts into the
expensive mode, and a measurement that has to name which mode it measured.

**Corrected ranking**, since the first version of this paragraph oversold 7.7 on
a number that was really "we asked for realtime and got it":

- **7.7 per-object probe selection is a correctness fix, not a performance one.**
  Every reflective object in a scene uses one probe chosen by camera distance,
  so several probes means everything reflects whichever is nearest the *camera*.
  Worth doing; not worth doing for speed.
- **Shadow maps are the largest CPU phase**, 0.495 ms, and that number is real.
- **The render graph is the largest GPU line**, 0.424 ms, which is what
  front-to-back opaque sorting (7.8) aims at.

An intermediate state is worth recording because it nearly shipped as a
success: with materials still holding a sampler each, draws fell 3238 to 854 and
the frame time did **not** improve — OpenGL got 14% worse. A 74% reduction in
draw calls bought nothing, because the lit pass was still one draw per object
and only the shadow passes had collapsed. The draw count moved and the frame did
not, which is the same mistake as the culling number, caught this time.

## 7. Decisions already made (do not relitigate)

- **CMake**, not premake.
- **Vendored dependencies**, no SDK requirement to build.
- **Dynamic rendering** — Vulkan 1.3, no `VkRenderPass` anywhere.
- **Backend is restart-time**, not hot-swappable.
- **"Entity", not "GameObject".**
- **Theme rule: red means "you can act on this, or it is acting now."**
  Structure stays greyscale.
- **Menu entries for absent features are shown disabled with a tooltip.**
- **cgltf, not fastgltf** — fastgltf needs simdjson for parse speed that does
  not matter at editor scale.
- **Jolt, not a hand-written solver.**
- **miniaudio, not OpenAL Soft or FMOD.** OpenAL Soft is LGPL, which constrains
  how a packaged game may link it; FMOD cannot be redistributed, so every user
  of the engine would need their own licence. miniaudio is one public-domain
  file with decoding, mixing, streaming and spatialisation already in it.
- **No Vorbis.** miniaudio decodes it only through stb_vorbis, which means
  vendoring another decoder for a format WAV and MP3 already cover.
- **Four fixed audio buses, not arbitrary named groups.** Every game needs
  exactly this separation, and a fixed enum is something the inspector, the
  serializer and a settings screen can all present without inventing a naming
  scheme first. Arbitrary buses can be added later; they cannot be removed.
- **A missing audio listener falls back to the primary camera** rather than
  producing silence. Requiring a component would mostly produce silent scenes
  and a confused user.
- **A purpose-built ECS** (`Scene/ECS.h`, 2026-08-22) — sparse sets are the
  right trade for editor-scale scenes, and the engine used twelve calls of the
  library that provided them.
- **A purpose-built component registry, not a reflection system** — reflection
  identifies members by hashed id, so a serializer must store the name anyway.
- **`ViewRank`, not an `isPrimary` flag** — a boolean can be true twice.
- **Scene view stays on the editor camera during Play.**
- **GPU-driven rendering and bindless are out of scope.** Bindless is where the
  two-backend commitment would start to cost something real; see
  ENGINE-NOTES §5.

---

## 8. Next steps

### RESOLVED (2026-08-18): the SSAO/SSR "regression" was the project file, and the bisection that blamed 9.13c was wrong

For about an hour on 2026-08-18 `check_ssao.py` and `check_ssr.py` were red on
Vulkan, reading **0.00** where they should read a bleed, and green on OpenGL.
The first write-up (one commit back in this file's history) bisected it to
9.13c and named three suspects inside it. **All of that was wrong, and the way
it was wrong is the thing to remember.**

**What it was:** commit `7d3d860` -- a one-line ROADMAP edit -- swept
`SampleProject/SampleProject.rvproject` in with `git add -A`, and that file had
**every ray-tracing switch turned on** (plus `courtyard.rvpostprofile` with GI
on at High). Only the editor writes those files (`Project::Save` has no other
caller outside scenetest's throwaway Probe project), and I did not launch it, so
they were most likely changed by hand in the editor while the session ran --
which is fine and expected. The failure was mine: `git add -A` with no
`git checkout -- SampleProject/` first, on the one commit that had no reason to
touch assets.

With `RayTracedAmbientOcclusion` and `RayTracedReflections` on in the project,
every "screen-space off" and "screen-space on" render in those two checks was
the *traced* twin -- identical to each other, hence 0.00 -- and OpenGL, having
no rays to switch to, stayed green. That is exactly the shape the checks
reported and exactly the shape I misread as a Vulkan barrier bug.

**Why the bisection lied:** it was `git checkout <rev> -- RageV RageVEditor` --
the engine directories only -- so every "old" state under test still had the
broken project file from HEAD. The one whole-tree checkout (`1ece654 -- .`) was
green *because it restored the project file*, not because 9.13c was at fault.
**A bisection that does not check out the whole tree bisects the wrong
variable.** The three "suspects" I then probed inside 9.13c were each reverted,
rebuilt, and still red -- which was the evidence that it was none of them, and I
read it as "must be the fourth."

**Fixed by:** restoring both files to their committed content, and pinning
`--raytracing=off` ahead of `extra` in `check_ssao` and `check_ssr`'s launch
helpers, so their screen-space claims measure the screen-space effect whatever
the project holds -- falsified by putting the broken project file back and
watching both stay green. **Every other check that measures a screen-space
effect inherits the project's Render Settings the same way and wants the same
pin; that is now a fourth shape for CHK.1.**

9.13c and 9.13d were green when committed -- 9.13c's OK lines were read, 9.13d's
were counted -- and the engine has had no regression at any point.

### START HERE (2026-08-20): 8.13 is gone. Rays and the voxel grid are exclusive again

**The hybrid second bounce is removed.** The owner reported it taking the frame
to 86-91 ms; that reproduces, it was measured properly, and the feature did not
survive the measurement.

`demo` at 2560x1600, Vulkan, vsync off, GPU timestamps on the render graph:

| configuration | Graph GPU | over one bounce |
|---|---|---|
| rays, one bounce | 6.638 ms | -- |
| rays, two bounces | 9.208 ms | +2.57 |
| rays + hybrid | 62.441 ms | **+55.80** |

**21x the traced second ray it existed to replace, for four fifths of its
quality** (1.80x against 2.25x). Strictly dominated by a feature already in the
engine, so it is out: `RenderSettings::HybridSecondBounce`, `--hybrid-gi=`,
`ResolveHybridSecondBounce`, `VoxelGI::PublishGrid`, the lit shader's bindings
17-19, the editor row, check_gi's claim 19 and falsify's `hybrid-probe-again`.
Where ray-traced GI runs the grid is not built, the voxel dials grey out, and
`Indirect` has one writer. **The full investigation, the variant table, the fix
that would have worked and the reasons it was not built are ENGINE-NOTES 7be**,
which is the thing to read before anyone proposes this again.

**What it cost to learn, and the rule that comes out of it: a performance claim
is a measurement or it is not a claim.** 7be, the ROADMAP row and an editor
tooltip all said "0.09 ms, less than the rays it replaces". 0.09 ms was the
*grid build*; the gather was never timed at all. Quality was measured properly
-- banded, falsified, a break that fails -- and the cost was asserted from the
design and shipped in three places.

**Verified after the removal:** Release, Debug and Dist build with 0 errors;
`rvdoc --check` green (23 pages); `scenetest` exit 0 with 0 `[Vulkan]` lines
and verdict OK on **both** backends; `check_gi.py` exit 0, no FAIL, verdict OK
with claim 19 gone from it; and `falsify.py voxel-no-lift` seen to take
check_gi to exit 1 before `restore`.

### Every GI setting now lives in the post profile (10.6, ENGINE-NOTES 7bg)

`GiBounces`, `VoxelGlobalIllumination` and the three voxel dials moved out of
Render Settings, so when ray-traced GI takes over, **every** rasterisation GI
row greys out together in one panel.

`GiBounces` had to move as well, and not for tidiness: **a registry predicate
is handed the block its row belongs to**, so a render row whose visibility
depends on a profile setting would have to cast the wrong struct or acquire a
global for the active profile. All five in one block means every GI predicate
casts what it was given. The comment on `VoxelGiTakesOver` says so, because
the next render row that greys on GI will otherwise bring the problem back.

**This amends a 9.0 rule, and the amendment is written down rather than
slipped in.** `GiBounces` used to say it lived in Render Settings because it
costs rays and "a camera cut must not change the ray budget". But
`GlobalIllumination` costs four passes and `GiQuality` moves the gather to
full resolution, and both were always profile settings -- so the line 9.0
actually draws is the *hardware* budget. The consequence is accepted and
stated in the field comment and the manual: **a cut between profiles can
change the ray budget.**

**Two things to know before touching this:**

- **`--render-defaults=on` no longer neutralises voxel GI**, because it
  replaces Render Settings and the form is not there any more. It comes from
  the scene's profile now, exactly as the GI on switch always has. `check_gi`
  is unaffected -- it writes each fixture's profile explicitly -- but any new
  check that cares which form runs must set the profile or pass
  `--voxel-gi=`.
- **An old `.rvproject` naming the five keys is warned, not migrated and not
  dropped** (the 7s rule: migrating means a load silently rewriting an asset;
  dropping is what happened to `TemporalFeedback`).

Verified including **the path that actually changed**, which no existing check
covers because they all use the CLI override: a profile carrying
`VoxelGlobalIllumination: true` alone builds the grid and changes the picture
-- R-B over the bleed patch **+59.81** screen-space against **+73.93** voxel.


### THREE VERIFICATION HOLES, ALL CLOSED (10.1, ENGINE-NOTES 7bf)

All three let a completely broken renderer report success. All three were
found the hard way building 8.13, and all three are now guarded by something
that has been *seen* to fail.

1. **A shader that did not compile, and a build that stayed green.** Shaders
   compile at runtime, so `cmake --build` proves only that the C++ did.
   `ShaderCompiler` now counts failures (`FailureCount`, `FirstFailure`,
   recorded in wrappers so no exit path can forget), and a run asked for
   `--screenshot` or `--benchmark` exits **3** rather than reporting whatever
   the frame contained. Interactive runs are untouched -- that is where a
   person edits a shader, sees the error and fixes it.
2. **A black frame read as a measured zero.** `rvcheck.require_drawn` refuses
   a frame nothing was drawn into; `check_gi`'s `shoot` and `sequence` pass
   every frame through it. The bar is two orders of magnitude below the
   darkest real frame, so it asks "did the renderer draw anything" rather
   than "is this bright enough".
3. **The runtime does not load the shaders in the source tree.**
   `rvcheck.require_current_shaders` compares the deployed tree against the
   source tree and refuses to measure when they differ, naming the files.
   **Every check that launches the runtime calls it** -- sixteen wired
   mechanically, `check_gi` by hand, and the four that never launch it cannot
   hit this.

**And a fourth that fell out of fixing the third:** a deliberate break is not
staleness, so `falsify.py` now writes a `.falsify` marker naming the active
break and clears it on `restore`. A check that finds it prints a banner and
continues. Before this, a check run after a forgotten `restore` measured a
deliberately broken shader and said nothing.

**Falsified, all four:**

| guard | break | result |
|---|---|---|
| shaders compiled | a bad `#include` in the deployed lit shader | runtime **exit 3** |
| the frame was drawn | `falsify.py lit-black` (compiles, renders black) | check_gi **exit 1** |
| the shaders are current | a line appended to the deployed lit shader | check_gi **exit 1** |
| a break is announced | `falsify.py lit-black` | the banner, then the guard above |

The first is the 8.13 defect exactly -- the same kind of missing include, in
the same file. It used to exit 0.

**New in the toolbox:** `tools/scripts/rvcheck.py`. Import it from any check;
it is where a shared measurement guard belongs from now on.


### THE THIRD CHECK AUDIT (CHK.3, 10.4, ENGINE-NOTES 7bi)

Asked for as "audit the other check scripts for floors that cannot fail". Two
passes already existed -- 7ba named four shapes and audited twenty scripts,
CHK.2 put every claim on the wrong side of itself -- so this one is scoped to
the three places they could not reach: **what was written after CHK.2**,
**a shape neither names** (a claim's evidence, not its threshold), and **a
break kind CHK.2 never used** -- every break for an effect switches it off or
inverts it, so nothing had ever asked one to do too *much*.

**Two scripts changed. Three findings, each measured before it was believed.**

1. **`check_graph`'s "every graph generates" claim could not fail.** The
   generated C# is tracked in git and only the fixture's own file was deleted
   before the generator ran, so the other five were answered by the last
   checkout. Renaming a node type in `Roster.rvgraph` had the runtime log
   *"at least one graph did not generate"* in the same run this check printed
   OK. It now compares **content against git** -- regenerate everything, and
   every graph must produce exactly what is committed.
2. **The engine behind it was worse, and is now fixed -- ROADMAP 10.10,
   ENGINE-NOTES 7bj.**
   `ScriptGraphSerializer::Load` **dropped** a node of unknown type and
   returned success, so the graph did not fail to generate -- it generated
   *silently emptied*. `Roster.g.cs` went thirty-one lines to an empty
   `OnCreate`, and `dotnet build` compiled it. It now refuses, `GenerateAll`
   removes the stale script, and `GetScriptGraph` answers null rather than an
   empty graph. **And the refusal is not a dead end**: the panel's page offers
   *Open without it*, which drops what cannot be read, says what that was,
   opens dirty and writes nothing until you save. Eight scenetest claims,
   falsified.
3. **Two of the three lens effects had no magnitude at all.** A vignette that
   ignores `VignetteSmoothness` and covers the whole frame passed; so did a
   twelvefold aberration. The existing corner-versus-centre claim is blind to
   the first *by construction* -- the corner reads 66.77 against 70.38 either
   way, because the falloff has saturated there under both, and the whole
   difference is at mid-radius: **3.02 against 23.29**. Five new thresholds,
   each between a measured correct reading and a measured broken one.

**And one finding about fixtures rather than thresholds.** The aberration
reads 0.08 % of the frame on the AA scene, and twelve times the offset only
doubles it -- two flat levels have nothing to disperse. On the tone ramp 9.3
built for the grain's response curve it goes **8.49 % to 58.71 %**. *A fixture
chosen for one effect cannot be assumed to measure another*, and the AA scene
is shared by most of the suite.

**`falsify.py` learns a third target kind**: `asset:<repo-relative path>`, for
a claim about a project asset rather than a shader or a check script. Restored
from git with the same tracked-or-refuse rule -- plus its generated sibling,
because putting a `.rvgraph` back leaves the `.g.cs` the broken one produced.
Four breaks added: `graph-stale-node`, `lens-vignette-everywhere`,
`lens-vignette-twice`, `lens-aberration-wide`.

**Left open, deliberately:** `require_drawn` is in **2 of 21** scripts where
`require_current_shaders` is in 18 of 18. Two black frames are byte-identical,
so every *"off is off, to the byte"* claim -- six scripts -- is perfectly
satisfied by a renderer drawing nothing. The commonest cause is caught
upstream by the exit-3 rule; a fixture that loads and puts nothing in frame is
not.

**State.** Phases 0-7 and 9 are done; 8.1, 8.2, 8.4, 8.10 and 8.12 are
done; phase 8 has 8.3, 8.5-8.9 and 8.11 open. **Everything through `fe13763`
is pushed.** This session's work is on top of that and is the owner's to push.

**1. `check_gi.py` now runs end to end and passes** -- the thing the last
START HERE said was owed. It is green on both backends, exit 0, verdict OK.
Running it found one real thing, which is why it was worth running:

**The voxel path is not byte-reproducible, on either backend.** Measured by
`check_gi` across five runs: Vulkan 1 level over 0.67-1.18% of channels,
OpenGL 1-2 levels over 0.68-2.08%; the screen-space path is 0 on both. (An
earlier note here said Vulkan was clean and blamed OpenGL synchronisation --
that came from probes without `--render-defaults=on`, whose Vulkan rows were
the traced form. See item 4.)
Claim 15 compared `GiRadius` 1 against 8 byte-for-byte and failed on that
noise -- the radius pair was *smaller* than the same-settings floor. `GiRadius`
genuinely never reaches the voxel gather (it is in neither `VoxelGI.cpp` nor
`voxelgi_gather`; the graph hands it only to `PostProcess::SsgiCompute`), so
the claim now measures the floor in-script and asks whether moving the radius
does more than relaunching does. **The cause is open, and ENGINE-NOTES 7bc
records three candidates already eliminated by measurement**: the voxeliser's
unsynchronised `imageStore` (a break that makes competing writers disagree
violently moved nothing), the temporal accumulation's start frame (`GiDenoise`
0 is *worse*, and frame 200 does not converge it away), and anything shared
with the screen-space chain. What is left: it is per-frame, OpenGL's alone,
and confined to the storage-image passes -- so look at synchronisation there
next.

**2. The falsify pass ran, and two of its entries were wrong.** `voxel-no-lift`
(+0.21), `voxel-wide-cones` (+0.11) and `voxel-iso-faces` (claim 14 at 2.04,
not claim 13) all catch what they should; 7bc's predicted readings were from
mid-development builds and are corrected to the measured ones.
**`voxel-no-shadow` is not caught, and the fixture is why**: on `gi_corner` the
sun lights essentially everything that contributes a bounce, so removing the
injection's shadow term moves the near brightness from +23.81 to +23.93.
Claim 14's ceiling is not too wide -- the scene cannot see the defect.
**Catching it needs a fixture with a caster between the sun and the bouncing
wall; until then the injection's shadow term is unguarded.** Two breaks written
for claim 15's floor both survived and were deleted rather than kept (CHK.2's
rule).

**3. 9.15 is specified, not landed (ENGINE-NOTES 7bd, ROADMAP row).** The
design named the gather's `1/(1+d^2)` as the cause of the screen-space form
reading +0.22 where the world-space forms read +1.78 and +1.86. It was
deleted, built and measured: **+0.22 became +4.17**, 2.2x the traced form, so
it is reverted and nothing shipped. The falloff is not gratuitous -- the gather
places every tap at the *same world radius* and reads whatever the depth
buffer shows behind it, so tap density is uniform over a shell rather than
over solid angle and nothing else carries the form factor. Moving the
inverse-square into the weight breaks 7bb, whose normalisation needs weights
comparable between a hit and a miss. **The remaining specification is narrow:
sample directions and march to the first hit, reusing `ssr_resolve`'s DDA
(9.10).** Target on the record: land between +1.78 and +1.86 on `gi_corner`,
+0.00 at the far end and off screen, backends inside 0.05. Two things the
failed attempt did establish: locality is `GiRadius`'s doing, not the
falloff's, and the off-screen discriminator does not depend on it either.

**4. A "defect" that was reported and then retracted the same day -- read
7bc's fifth finding, not just its table.** Measuring only `gi_shadowed`, the
Vulkan numbers did not respond to any edit of `voxel_inject.rvshader`, and it
was written up as the falsify lever failing to reach that backend. **It is
false.** A syntax error in that file is reported by both backends and disables
voxel GI on both; the same quarter-scale edit run against `gi_corner` moves
Vulkan from +1.78 to +0.38. There is no shader cache to be stale either --
`ShaderCompiler::SetCacheDirectory` is never called, so every launch
recompiles. **`falsify.py` works on Vulkan and CHK.2's guarantee stands.**

**And the rest is explained: Vulkan has ray tracing and OpenGL does not.**
`SampleProject.rvproject` has an uncommitted `RayTracing: true` +
`RayTracedGlobalIllumination: true`. The probe scripts never passed
`--render-defaults=on`, so on Vulkan `rayGi` was true and -- correctly, by the
documented resolve order rays > voxel > screen -- `--voxel-gi=on` was ignored.
**Voxel GI never ran on Vulkan in any of those probes**, so of course no edit
to `voxel_inject.rvshader` changed anything. The log says so plainly
(`VoxelGI: grids at 64^3 x 3 cascades, 7 levels` appears only on OpenGL, and
appears on Vulkan the moment `--render-defaults=on` is added). Nothing is
wrong with the engine.

**Rule to keep, same shape as the courtyard one: any script that renders for
measurement must pass `--render-defaults=on`**, or it inherits whatever the
owner last saved in the project -- and on a ray-capable device that silently
selects a different GI form. `check_gi.py` does this in `run()`; ad-hoc probes
did not.

**The local-light shadow follow-up is not blocked** -- it was left unbuilt for
time, not for tooling.

**Also landed:** the `gi_shadowed` fixture (an awning on the red wall, so
something shadowed actually bounces), generated by `make_gi_scene.py`. No
claim uses it yet and 7bc says what a claim would need.

**Next, the owner's call:** **9.15** as now specified (the march); or the
**OpenGL voxel reproducibility** hunt, which is the one open defect; or the
8.1 follow-ups, of which *local-light shadows in the injection* now comes with
a known missing fixture, and *skinned casters* is untouched
([VoxelGI.cpp:441](../RageV/src/RageV/Renderer/VoxelGI.cpp) skips
`mesh->IsSkinned()`, and 7an's compute-posed buffer is the path); or **8.9
FBX / Collada**, still the only non-XL row left in phase 8.

**The SSGI finding, kept and now sharper:** the screen-space form is
sub-visible at +0.22 against +1.78/+1.86, and 9.15's failed first attempt
shows the cause is the kernel's shape rather than one multiply.

**`SampleProject/assets/post/courtyard.rvpostprofile` still carries the
owner's own editor edit** (GI on, quality High), uncommitted and unreverted.
Stash it before any `git checkout -- SampleProject/`.

**Design first** -- an ENGINE-NOTES entry (7be is next) before code, then
HANDOFF, then the ROADMAP row in the same commit that first uses a new number.

**Two process rules that cost an hour each when they were learned:** `git
checkout -- SampleProject/` before every `git add -A`, and bisect the *whole
tree* (`git checkout <rev> -- .`), never engine directories alone. Patch
scripts go through the Write tool to the scratchpad -- inline Python through a
Bash heredoc collapses backslashes.

**Also carried, small:** `RayTracedGlobalIllumination` and `GiBounces` have no
typed C# properties; the in-focus group in `check_depth_of_field` reads 115 %
of its unblurred detail, observed and unexplained; `check_ray_shadows`' speckle
claim does not judge the ray origin offset from below on this fixture (zero
offset reads 0.000000 dark), so a fixture that can is a gap.

**The working tree when this was written:** `courtyard.rvpostprofile` carries
the owner's own editor edit (Global illumination on, quality High) and is
*not* committed and *not* reverted -- it is theirs. A handful of `.meta`
sidecars are dirty from the suite runs; `git checkout -- SampleProject/` is
still the rule before the next `git add -A`, but look at that profile first.

The 9.13 detail below stands as history.

Terrain is complete: `HeightAt` became a script call in both languages
(7au, protocol 9), which was the last item on every terrain stage's list.

**Twenty-six commits are unpushed.** Pushing is the owner's action.

### The previous banner: 9.12 global illumination was DONE (2026-08-17); terrain is finished

The owner asked for global illumination in the post profile plus a ray-traced
twin in Render Settings that greys the profile's row. **Both are in.** Read
ENGINE-NOTES **7at**: the design, the three bugs it took to get there (a shared
blur that blacked out every frame, one uniform block declared two ways that
made *OpenGL* render differently on every run, and a hit shaded through a table
nobody bound), and the measurements. `check_gi.py` is the evidence; scenetest
carries the graph assertions. Nothing here is outstanding.

---

### 8.4 terrain is DONE in three stages plus varieties (2026-08-17)

**Stage 3 -- the brush -- landed last** (ENGINE-NOTES 7ar; narrative and Done
entry below). Read 7ar first: the kernel and rates, one stroke = one
command, the lazy per-chunk rebuild in `SelectLod`, `RHITexture::UploadRegion`,
`Terrain::Raycast`, the write-back on save, edit mode only, and `--brush=`.
**Then one fix on top (2026-08-17, after the owner dropped a terrain into the
demo scene and looked under it -- "I can see the separators for each block"):
the skirts are drawn only while the camera is above the ground.** From under
a heightfield the surface culls away and each chunk's skirt (wound both ways,
on purpose) hung in the sky as a wall along every seam. `Terrain::SelectLod`
now also compares the camera, in terrain space, with `HeightAt` at its own
(x, z) -- clamped, so off the rim it compares with the nearest rim -- and
while under, every chunk draws its surface indices alone: the builder emits
the surface before the skirts and returns where they end
(`Chunk::SurfaceIndices`), `Terrain::DrawIndexCount(chunk)` says how many,
and `Renderer3D::DrawLayeredMesh` takes an index count (`PendingDraw::
IndexCount`, part of the run key). Shadow pass and BLAS keep the whole mesh.
Paragraph in 7ap, `check_terrain.py` claim 6 on the new `under` fixture (0
non-sky pixels both backends; 24633 with the rule forced off), scenetest +10.
**Stage 3b -- brush varieties -- is DONE too** (ENGINE-NOTES 7as, the owner's
ask at stage 3's landing): the kernel is a **shape** (the disc, or any square
greyscale mask from `RageVEditor/assets/brushes`, turned by an angle and
optionally following the stroke) times a **pattern** laid over the *ground*
(noise, or a tiled mask), and four more operations -- **Terrace**, **Ramp**,
**Set Height** and droplet **Erode**. Fifteen masks ship
(`tools/scripts/make_brushes.py`), every one a landform or a ground texture
after the owner rejected the first, decorative set: dome / soft / mid / hard /
pad / wispy / branch / mountain / ridge / mesa / crater as shapes, erosion /
rock / veins / dunes as patterns. The mode row became a **Sculpt** toggle plus
a Mode combo, which is also the fix for 7ar's "Ra Sm Fla Pai" papercut.
`--brush=` gained `key=value` options (shape, angle, follow, pattern, scale,
hardness, steps, height, to). scenetest +34 (1798 Vulkan / 1758 OpenGL);
`check_terrain.py` claim 7 on the new `stamp` fixture, falsified twice.

0. **Brush varieties.** "Different brush varieties which can generate terrain
   in different patterns like Unity or Unreal" -- brush *textures* (an alpha
   image as the kernel, rotated per stroke), and patterned brushes: noise
   (raise by a noise field under the disc), erosion (Unreal's hydro/thermal),
   ramp (two clicks, a slope between), clone/stamp-from-elsewhere, terrace,
   maybe set-height. The owner also said "a brush to paint texture" -- **that
   is already Paint mode**, painting the four layers of 7aq; if they mean
   more (a texture *as* the paint, i.e. decals or a fifth layer), ask. Build
   varieties on `TerrainBrush::Op` + `Weight()`: a brush texture is a
   replacement for the kernel's weight, a pattern is a replacement for the
   per-sample delta; the stroke, recorder, command, rebuild and check
   machinery is all shared and does not change. Put the design in 7as
   first. **The Brush block's mode buttons truncate to "Ra Sm Fla Pai" in a
   narrow Properties panel** -- two rows below ~260 px is the fix; a
   papercut to take with the varieties, since the row grows.

**Terrain landed on top of 8.12, in three stages the same day** -- read the
three terrain narratives below, their Done entries, and ENGINE-NOTES 7ap,
7aq and 7ar. Stage

1: a heightfield as a source of meshes -- an `.rvterrain` asset, a
`TerrainComponent`, chunk meshes at four levels with skirts, Jolt's height
field over the same samples; the renderer never learned the word. Stage 2:
**a layered material** -- up to four `.rmat`s in proportions painted into
the asset (RGBA8 per sample after the heights, `layers = 4`), a
`LayeredMaterial` beside `Material` bound as set 1 of a *third* lit
pipeline, and one fork in `pbr_fragment.glsl` -- `SampleSurface` -- that
assembles the surface from the layers and changes nothing below it. Three
maps per layer (base colour, normal, roughness) on both paths, because set 0
already spends sixteen of OpenGL's thirty-two texture units and the two
paths are compared pixel for pixel. `experiments/terrain/Chunk` is **kept,
cut off**, at the owner's direction.

**What is open (2026-08-17, after global illumination landed):**

1. ~~**`HeightAt` as a script call**~~ -- **DONE 2026-08-18 (ENGINE-NOTES
   7au)**, and with it the terrain list is empty. `Scene::TerrainHeightAt`
   holds the walk, the extent test and the highest-wins rule; `Scriptable
   Entity::GetTerrainHeight` and protocol 9's two table entries are forwards
   to it. **The shape worth remembering: it answers two questions, not one.**
   `Terrain::HeightAt` clamps to its own extent, so a call that returned only
   a float would report the rim's height for a point a kilometre past the edge
   -- hence `bool` plus an out-parameter that is zeroed on a miss. Fourteen
   claims in scenetest (1818 Vulkan / 1778 OpenGL), four falsifications, one
   of which found a check that passed for the wrong reason.
2. **9.13 GLOBAL ILLUMINATION, RESTRUCTURED -- five commits in, four pieces
   left. Designed in ENGINE-NOTES 7av, which is the document to read before
   touching any of it.** Session ended 2026-08-18; everything below is the
   state at that point.

   **The shape, so the rest makes sense.** Both GI forms now write one
   albedo-free `Indirect` buffer, and the lit shader reads it **one frame
   late** and multiplies by the surface's own albedo -- 9.9's exact pattern for
   SSR radiance. That multiply moving back into the shader is what retired
   SSGI's lit-pixel albedo stand-in outright, so the new full-resolution
   attachment the first draft had costed for albedo was never needed.

   **What is landed and verified** (scenetest 1824 Vulkan zero `[Vulkan]` /
   1781 OpenGL; check_gi, check_ssao, check_ssr green both backends; Release,
   Debug and Dist all build):

   - `d3e8d99` the receiving slot: `Renderer::ScreenIndirect`,
     `SceneUniforms::Indirect` **in both GLSL mirrors**, binding 16.
   - `5e3c2f6` the three frame chains each own a `TemporalHistory`.
   - `35f558c` the gather writes the buffer; the albedo stand-in is gone.
     SSGI +0.77 became **+1.71** because the wall's albedo (0.85) exceeds its
     lit brightness at a grazing angle -- the direction the fix predicts.
   - `7cfd016` the fourth colour attachment and RT GI resolving through it.
   - `44f36bb` check_gi's thresholds become bands.
   - `1b01c28` the split: **RT GI accumulates, SSGI does not.**
   - 9.13a: the convergence check, three claims, a third fixture, and 7aw.
   - 9.13b: the second bounce -- `TraceSurface`, `RenderSettings::GiBounces`,
     three more check_gi claims and a scenetest clamp unit. 7ax.
   - 9.13c: SSGI's loop closed, the split undone, and **7av's diagnosis of it
     corrected**. 7ay.
   - 9.13d: `PostSettings::GiQuality`. The probe fallback is **filed, not
     built** -- see 7az and the item below it.

   **ALL FOUR DONE -- and one bigger thing found on the way:**

   **(a) The convergence check -- DONE 2026-08-18, ENGINE-NOTES 7aw.** The
   ~30 % drop is closed: **nothing was lost, the unfiltered number was wrong.**
   Supersampling the shading, accumulating the frame under TAA and accumulating
   the buffer all move the reading to the same place, and the two most averaged
   configurations agree to 0.005 levels -- so **+2.69 / +1.25 were readings of
   the ray noise**, and the denoised +1.86 / +0.82 is the scene. `check_gi.py`
   gained three claims (it settles; without the filter it does not; it settles
   on the value TAA independently agrees with) and a third fixture,
   `gi_detail`.

   **Two things from it that outlive GI.** First: *a stochastic renderer
   measured through the tone curve reads high* -- by a third here, in the mean
   of a region with tens of thousands of pixels. Every check that compares a
   noisy feature's before and after is exposed, which makes CHK.1 bigger than
   it looked. Second: *a check that has never been seen to fail is not a check*
   -- a fourth claim was written, passed, and then survived three separate
   breaks of the filter reading within 3 % of unbroken, so it was deleted
   rather than kept. 7aw says why, and what instrument would be needed to do it
   properly (a debug view of a render-graph target, or a float capture).

   **(b) The second bounce -- DONE 2026-08-18, ENGINE-NOTES 7ax.**
   `TraceReflection` split into `TraceSurface` + `ProbeIrradiance` +
   `ShadeTraced`, and the GI path calls the tracer twice; the second hit's
   indirect takes the probe, so depth two terminates by construction.
   `RenderSettings::GiBounces` (1|2, default 1), `--gi-bounces=`, an inspector
   row under the RT block. **The split is a refactor and that was checked, not
   asserted**: the pre-split build and the post-split build at `GiBounces` 1
   render the same image to the byte.

   Measured: the wall beside the red one goes +1.85 to **+4.06**, and a wall
   whose bounce source is off screen +0.81 to **+1.83** -- large because these
   fixtures have a black sky, so at one bounce the probe every hit is shaded
   with holds nothing. Cost 1.042 ms to **1.577 ms** on `gi_corner`, +71 %
   rather than +100 % because a missed ray does not get shaded.

   **Left over from it, deliberately:** `RayTracedGlobalIllumination` and
   `GiBounces` have no typed C# properties. The convenience surface in
   `Engine.cs` is a curated subset and carries neither; the generic
   `RenderSettings.Get`/`Set` string pair reaches both today because they are
   registry fields. Giving the traced-GI switches typed properties is one job
   rather than half of one.

   **(c) SSGI's feedback loop -- DONE 2026-08-18, ENGINE-NOTES 7ay.** The lit
   shader publishes what it added to each pixel out of the indirect buffer --
   `kD * albedo * indirect * occlusion`, into the attachment that sat empty
   while the screen-space form ran -- and the gather subtracts it off every
   tap. Exact, and no new attachment: a direct-only colour would have cost a
   fifth full-resolution target *always*, because a target's shape cannot
   depend on a checkbox.

   **The bleed fell from +1.71 to +1.27**: a quarter of the number 7at
   calibrated was the gather reading its own output back. It was never one
   bounce.

   **7av's diagnosis was wrong and is corrected in place.** The +16.98 it
   blamed on the loop was mostly a *unit error*: the screen-space chain carries
   linear depth in alpha for its blur's edge test, `ssgi_apply` normalised it
   away and `GiDenoise` did not, and the lit shader multiplies this buffer's
   alpha into the bounce -- so the bounce was multiplied by a distance in
   metres. Reproducing it alone gives +6.93; with the loop open too, the two
   compound to 7av's figure. **The arithmetic was there to be done at the time:
   a loop of gain `g . albedo` cannot take 1.71 to 16.98 for any `g` under
   one.** That is the lesson worth keeping -- a mechanism that explains the
   *sign* of an error is not a diagnosis of its *size*.

   7av's split is undone: both forms end on one `GI denoise` pass, and
   `ssgi_apply.rvshader` plus `PostProcess::SsgiResolve` are deleted. On a
   still camera the accumulation changes nothing for the screen-space form --
   its kernel is fixed, so there is nothing to average -- and what it buys is a
   reprojected history under motion, which these fixtures cannot show.

   **(d) Sharpness -- DONE 2026-08-18, ENGINE-NOTES 7az.**
   `PostSettings::GiQuality` (Low/Medium/High: half res and 12 taps, half and
   24, full and 24), the blur following the resolution because it is handed the
   gather's dimensions. 0.489 / 0.603 / **1.281 ms**.

   **It does not narrow the bleed, and the check says so.** 7av wanted the
   check to measure the profile's width; that width is `GiRadius` in world
   metres -- 136 px at Low, 144 at High -- so a width claim would have been a
   check that cannot fail. It claims the true weaker thing instead.

   **A one-line bug the benchmark caught and the image could not**: the targets
   are sized by `RGTargetDesc::Scale`, and the first build changed only the
   numbers handed to the shader. High cost the same as Medium, which a
   four-times-larger gather cannot -- the gather was still at half resolution,
   reading a texel size for a grid twice as fine.

   **(d2) THE PROBE FALLBACK -- superseded by 9.14 (7bb), which delivers it as
   a normalisation with nothing to add. The paragraph below is the finding
   that led there.** Building it meant measuring what the probe already contributes, and
   **the environment is already counted twice**: the lit shader adds the GI term
   *on top of* the probe, and a traced ray that misses returns the sky. On
   `gi_corner` with the sun off and a grey sky -- every photon on the far wall
   is environment light -- GI off reads 171.0 and traced GI on reads **214.7**.
   Filling missed screen-space taps with the probe would add a third helping in
   exactly the directions it already answered for.

   **What has to happen first:** the GI term has to *replace* the environment's
   contribution over the directions it covers rather than add to it -- the lit
   shader knowing what fraction of the hemisphere the estimate covered, both
   forms reporting it, and **every calibrated number in check_gi re-measured
   after**. That is a change to 9.12's design, not a corner of 9.13d.

   **PARKED:** `build/9.13-denoiser-wip/` (git-ignored) holds the SSGI variant
   of the denoiser and `denoiser.patch`, which applies with
   `git apply --exclude='SampleProject/*'`. Re-applying it is also the
   falsification for check_gi's new bands -- it fails four of them.

   **THE FINDING WORTH CARRYING PAST GI.** Every threshold in check_gi was a
   *floor* -- "did the bounce happen" -- so a bleed ten times too strong
   printed OK. They are bands now, with ceilings on both forms and a limit on
   how far the two backends may disagree. **The other check scripts have not
   been audited for this**, and the same shape is likely in several: a floor
   answers "does the feature do anything", a band answers "is it still
   calibrated", and only the second catches a regression in a feature that
   still works.

   **The attachment sweep is thirteen places, listed in 7av**, and two of them
   hide: the pass binds a *subset* and `layout(location = N)` counts the
   subset rather than the target; and four callers set the target shape before
   `BuildFrame` runs, because probe captures happen first. Missing the latter
   left a pipeline at three attachments against a four-attachment pass -- ten
   validation lines and a picture that looked perfectly correct. **That is why
   the bar is zero validation lines and not zero visible artefacts.**

3. **The two long-standing non-blockers**: the focus-click guard has never
   been confirmed against a real click, and an orphaned LUT is not warned
   about.
4. **Phase 8: nine of its twelve rows are open** -- 8.1, 8.3, 8.5, 8.6, 8.7,
   8.8, 8.9, 8.10 and 8.11. Done are 8.2 (bindless), 8.4 (terrain) and 8.12
   (ray tracing). **8.9 FBX/Collada import is the only L; the other eight
   are XL**, which is the thing to say rather than "the rest are
   engine-sized" -- the count is what stops the list reading as empty.
   **8.1 is the one to be careful about**: it is still open and it is *not*
   what 9.12 built. 9.12 is one bounce, screen-space or four rays a pixel;
   8.1 is the world-space form -- SDFGI or voxels, multi-bounce, stable
   without a temporal filter. Both rows now say so.

**Papercut worth taking with whatever touches the editor next:** nothing
outstanding from terrain -- the "Ra Sm Fla Pai" truncation was fixed by the
Sculpt toggle + Mode combo in 7as.

**Twenty-six commits are unpushed** as of this writing -- terrain stage 1 and its
follow-ups (b37b8ed .. c8cb011), stage 2 (fe20a5c), stage 3 (d203b1b) and
the skirts-from-under fix on top; everything before them is on origin.
Pushing is the owner's action.

---

### Terrain (8.4 stage 3) -- the brush

**8.4 stage 3 (2026-08-17, ENGINE-NOTES 7ar) is done and verified.** The
shape: **`Asset/TerrainBrush`** -- `Op {Raise, Smooth, Flatten, Paint}`,
Radius, Strength, Hardness, Layer, Invert (Shift: lower/erase); `Weight(d)`
(1 inside hardness x radius, 1 - smoothstep to the rim); `Footprint` (the
disc's sample box) and `Apply(data, size, height, x, z, flattenTarget, dt)`
-- rates relative to Height and time (a quarter of Height per second at
strength 1; blend modes an eighth of the gap per sixtieth), Smooth a 3x3
mean read before written, Flatten toward the height at press, Paint a
replace that **materialises an unpainted texel as layer 0 first** and
**moves at least one unit when it means to move** (eight bits would stall a
held paint at 254 and a held erase at 4, and 4 is not 0 to the zero-sum
rule); `TerrainStrokeRecorder` grows a rectangle and copies each newly
covered sample before the step that writes it. **`Terrain`**: `Stale`
bitmask per chunk, `Invalidate(rect)` (grown by one sample, bounds refreshed
from the data), `RebuildStale(all)` -- `SelectLod` rebuilds each stale
chunk's *selected* level on its way past, the release rebuilds all --
`ApplyRegion(source, rect)` (heights and/or weights into the runtime's copy
+ `UploadWeightRows` through the new `RHITexture::UploadRegion`, both
backends), `Raycast(localOrigin, dir)` (box clip in x, z *and the y slab*
-- a vertical ray with an unbounded march hung scenetest until the slab
was added -- then half-cell march + 24 bisections). **`Assets::Manager`**:
`EditTerrain` (mutable, marks dirty), `SaveTerrain`/`SaveDirtyTerrains`
(write + `Registry::Reindex(handle)`, one file's hash), `IsTerrainDirty`.
**`TerrainStrokeCommand`** in SceneCommands: rect + before/after,
Execute/Undo through `ApplyRegion` on every terrain of that asset,
`TouchesScene` true. **Editor**: `Tools/TerrainBrushTool` (Aim through the
inverse world matrix, Begin/Step/End, `PushApplied` on release, ring
overlay through DebugRenderer, `ScriptStroke`, `Cancel`); the Terrain
block draws the Brush controls (mode buttons, size/strength/hardness, layer
buttons under Paint, hint); `EditorLayer` runs the tool from
`DrawViewportPanel` (mouse ray via the new `ViewportMouseRay`), picking
and the gizmo stand down while it owns the terrain, `[`/`]` size, Escape
cancels, `SaveScene` calls `SaveDirtyTerrains`, `--brush=` (EngineConfig
`BrushScript`) applies one stroke on the first update after load and saves.
Edit mode only (`Playing` flag; the block says so).

**Checks**: scenetest +34 (1754 Vulkan under validation, zero `[Vulkan]`;
1714 OpenGL) -- kernel values, raise rate/symmetry/lower/rate independence
/saturation/off-grid, smooth on a spike, flatten convergence to the
rounding, paint materialise/replace/second layer/erase, the recorder's
union and before, Raycast hits and misses, ApplyRegion + HeightAt + level-0
vertex + bounds, the command's undo/redo through a real Terrain, SaveTerrain
+ file + sidecar hash, UploadRegion out of range survivable.
`check_terrain.py` claim 5 on the new `brush` fixture: editor
`--brush=raise,-30,20,10,1,2` then the runtime renders the saved asset --
changed region begins at **row 148 against a derived 147.6** (the ridge's
Gaussian tail lifts the bump's far side; the derivation includes it);
`--brush=paint,...,1` turns the stroke's centre from `[27,8,9]` to
`[8,8,32]`. Falsified: raise inverted (row 603, a hole), wrong layer
painted (stays red). Verified Debug/Release/Dist, editor eyeballed with
the two rings on the mesa (`--camera=-30,14,20,50,35,35`), rvdoc green.

**Stated limits** (7ar): one brush shape; uploads wait (a stroke over four
chunks is nine GPU waits, a few ms); the write-back is on save; edit mode
only; `HeightAt` not a script call; smooth/flatten stop within four height
units of their target.

---

### Terrain (8.4 stage 2) -- a layered material, one fork in the lit shader

**8.4 stage 2 (2026-08-17, ENGINE-NOTES 7aq) is done and verified.** The
shape: **the paint lives in the `.rvterrain`** -- the header's reserved
`layers` word is 0 (stage-1 file) or 4, and when 4 the heights are followed
by `R x R x 4` bytes, one RGBA8 weight per sample on the heights' grid
(`TerrainData::Weights`, `WeightAt`, `HasWeights`; the serializer reads and
writes it and refuses any other count or a truncation inside it). The
shader normalises the four weights and takes layer 0 where the sum is zero,
so an unpainted terrain draws exactly as a stage-1 one. **`TerrainComponent`**
keeps `Material` under that key as **layer 0** (label "Layer 0") and gains
`Layer1`..`Layer3`; empty layers are inactive, an empty layer 0 is the
renderer's default. **`LayeredMaterial`** (`Renderer/Material.h`, beside
`Material`): four layer materials, the weight texture, `WeightUv`; `Refresh`
rebuilds a 368-byte `LayeredParams` block from the layers' *current* state
each frame and rewrites the set only on change (so an inspector edit to a
layer reaches the terrain with no dirty protocol); `Bind` writes the block
and, on the bound path, thirteen samplers (weights, and base colour /
normal / roughness of each layer as arrays of four); on the bindless path
the same maps are heap slots inside the block. **`Renderer3D`**: a third
lit pipeline over `pbr_layered.rvshader` (the static vertex stage, factored
into `include/static_vertex.glsl`, and `pbr_fragment.glsl` with
`RV_LAYERED`), `DrawKind { Static, Skinned, Layered }` replacing the bool
in the sort and run loop, `LayeredSet` per scene slot, `DrawLayeredMesh`,
`GetTextureHeap`. **`pbr_fragment.glsl`**: the surface is now one struct
filled by `SampleSurface`, whose single-material body is the code that was
inline and whose layered body reads the weights at `v_TexCoord * WeightUv`
(`Terrain::WeightUvFor`: scale `TextureScale / Size * (R - 1) / R`, offset
one half -- texel centres, not edges), then unrolls four `SHADE_LAYER(i)`
macros with `textureGrad` under a per-fragment weight branch and a tangent
frame per layer. **`Terrain`** owns the weight texture (RGBA8 from the
asset, or the 1x1 red `TextureLoader::Red` when unpainted) and the
`LayeredMaterial`; `RefreshLayers(component)` resolves the four handles once
per terrain per frame from `Scene::PrepareTerrains` (was `SelectTerrainLods`;
`ForEachTerrain` now underlies `ForEachTerrainChunk`). The preloader wants
all four layers. **Tools**: `make_terrain.py` writes weights (`hills`
painted from slope and height -- soil, a mossy soil on the flats, a sandy one
in the low ground, the tints over the demo soil's maps; a `layers` test
card: red on the left third, blue on the right third *at half intensity*, a
blend between, an unpainted strip inside the blue), four `.rmat`s with the
registry's own SourceHash, and `scenes/terrain_layers.rage`.

**Checks**: `scenetest` +21 (1702 Vulkan under validation, 1662 OpenGL) --
the painted round trip byte for byte, the two refusals, `WeightAt`,
`WeightUvFor` on texel centres at two texture scales, `sizeof(LayeredParams)
== 368`, the four handles through the scene file, and with a device: layer 0
empty is the default, absent layers inactive, the unpainted weight map is
the red texel, the batch key holds across a no-op refresh and moves with a
layer. `check_terrain.py` claim 4 on **three material paths** (Vulkan heap
on, Vulkan heap off, OpenGL): left `[222, 87, 84]`, right `[89.5, 87, 219]`,
middle `[196.6, 87, 189.2]`, unpainted-inside-blue `[222, 87, 84]` --
identical on all three. Falsified: weight channels swapped (left and right
trade places, six failures), normalisation dropped (right 332 vs left 393,
unpainted black, six failures). Vulkan heap on vs off on the hills:
identical to the pixel; vs OpenGL mean 0.34 levels.

**Verified for this commit**: Debug/Release/Dist; scenetest both backends
(zero `[Vulkan]` lines under validation); runtime on the painted hills with
`--raytracing=on --rt-reflections=on --rt-ao=on` under validation, zero
lines; runtime and editor exit 0 on both backends on the terrain and demo
scenes; `check_terrain`, `check_ray_shadows`, `check_ssao`, `check_ssr`,
`check_bindless` on Release all OK; `rvdoc --check` exit 0 with the three
new rows, the terrain page's Layers section and `docs/site` regenerated.

**Stated limits** (7aq): three maps per layer on both paths; four layers;
paint at the heights' resolution; no parallax and no triplanar on layers;
a terrain in a traced reflection shows layer 0 (the ray-instance record is
layer 0's); the sample project has no grass/rock/snow textures, so the
hills' layers are tints of the one soil.

**One trap found on the way, worth its line**: a scratchpad backup named
`pbr_fragment.bak` from the *previous* session was restored over the shader
mid-falsification and silently drew a stage-1 pipeline for one check run.
Name backups by date and diff before restoring.

---

### Terrain (8.4 stage 1) -- a heightfield that is a mesh source

**8.4 stage 1 (2026-08-17, ENGINE-NOTES 7ap) is done and verified.** The
shape: **`.rvterrain`** (`RVTR` v1: 32-byte header, `2^n + 1` a side, 33
to 4097, unsigned 16-bit heights row-major; `TerrainData` + `TerrainSerializer`
through the VFS; `AssetType::Terrain`; `Assets::Manager::GetTerrain` cached
by handle; a content-browser icon and a hierarchy mark). **`TerrainComponent`**
`{ Terrain, Size 256, Height 40, Material, TextureScale 4, Collision true }`
+ a runtime `Ref<Terrain>` the registry does not serialize; centred on the
entity; one `.rmat` tiled at `uv = local metres / TextureScale`.
**`Renderer/Terrain`**: chunks of 64 quads at four levels (65^2 / 33^2 /
17^2 / 9^2 vertices), skirts on edges a neighbour shares (both windings,
half the chunk's range + 2 % of the height), central-difference normals,
Jolt's triangle split; `SelectLod` per chunk per frame from the camera's
distance (full within four widths, one coarser per doubling), read by
`Scene::ForEachTerrainChunk` -- the one walk the scene pass, the shadow
casters, the ray-instance list and the picker share -- and
`Terrain::Resolve(component)` builds on first use and *replaces* the object
(never mutates the shared one) when the asset or a dimension changes.
**Physics**: `DescribeTerrainBody` -- a static `HeightFieldShape` at 16
bits per sample over the same heights, offset/scale carrying `Size`,
`Height` and the entity's scale, no RigidBody or Collider consulted (a
RigidBody on the entity is warned about and ignored); rays and contacts
name the entity. **Tools**: `make_terrain.py` (hills from value noise, an
analytic ridge, a ridged cliff; from a 16-bit PNG with `--png`), three
fixture scenes with profiles beside them, exactly as `check_terrain.py`
regenerates them.

**Two things the first frames taught, both fixed**: the level rule was two
chunk widths and the skyline stepped sixty metres out -- four now; and
skirts on the terrain's *outer* edge hung as a curtain off the rim -- they
are only on shared edges now. **And the crack fixture had to be designed
twice**: per-sample noise makes cracks and fills them with the next bump,
so skirts on and off measured the same; per-*column* ridges keep the seam
rough and the rise behind it smooth, and the two builds separate 0 holes
from 253.

**Checks**: `scenetest` +53 (1681 Vulkan under validation, 1641 OpenGL) --
serializer round trip and three refusals, triangle-exact sampling (0 and
0.5 where bilinear says 0.0625 and 0.5625), builder counts by arithmetic,
skirt depth 2.7 m on the 129 ramp, +Y winding, the level rule, `Create`
without a device, `HeightAt`, the scene walk, the scene-file round trip,
the ball at rest 5.50 +- 0.05, ray vs `HeightAt` to the centimetre, the
picker on both terrains. `check_terrain.py`: ridge silhouette row 90 vs
90.1 derived on both backends; shadow band 185.0 levels under maps and
rays; cliff 0 holes. Falsified: skirts off (253 holes), row-major swapped
(row 0, shadow 2.7).

**Verified for this commit**: Debug/Release/Dist; scenetest both backends
(zero `[Vulkan]` lines under validation, Debug and Release); runtime and
editor exit 0 on both backends on the terrain scene (Vulkan with
`--raytracing=on` and validation, zero lines) and on the demo; the editor
eyeballed with the terrain selected (asset pickers, dials, collision box);
`check_terrain`, `check_ray_shadows`, `check_ssao`, `check_ssr`,
`check_bindless` on Release; `rvdoc --check` exit 0 with the new
`TerrainComponent` row and manual page.

**Stated limits** (7ap): one material, no layers, no sculpting; no holes;
everything resident (2049 is ~180 MB of geometry, the practical ceiling);
LOD pops softened by TAA; interior seams' skirts show as short legs when
the terrain is seen from off its rim; `HeightAt` is not a script call; the
LOD is chosen by the last camera `RenderShadows` saw, so in the editor with
the game panel open the ray-traced structure holds the editor camera's
levels while the game view draws its own -- a terrain-shadow LOD mismatch
of one level in the game panel only.

---

### Stage 3 -- a hit is shaded, and SSR and SSAO get their traced twins

**8.12 stage 3 (2026-08-16, ENGINE-NOTES 7ao) is done and verified**, on
top of stages 1 and 2 the same day. A ray can now say *what* it hit:
`RayShadows` keeps a `RayCaster` (mesh, material, params, posed buffer)
per TLAS instance and `Renderer3D::EndScene` writes a **ray-instance
table** (`GpuRayInstance`, set 0 binding 15: vertex/index/posed buffer
device addresses, strides in words, the material record index shared with
the draws, the instance's own scalars) that the lit shader reads through
`GL_EXT_buffer_reference` under `RV_RAY_REFLECTIONS`. `RHIBuffer::
GetDeviceAddress()` is the one RHI addition. Hit shading is a *simplified*
copy of the lit pass -- base colour and emissive through the material and
the bindless heap, the interpolated normal (flat for a posed caster),
every light unclustered with one shadow ray toward the sun, sky irradiance
-- exact for emissive geometry, which is what the check that judges it is
made of.

**Ray-traced reflections** go in at the line SSR's radiance already enters
(7af): a glossy pixel traces its mirror direction and the result replaces
`prefiltered` by a weight from one at roughness 0.25 to zero at 0.6; the
SSR passes do not run when it is on. **RTAO** is SSAO's chain with a
different first pass: `rtao_compute.rvshader` casts the twelve taps as
short rays (`tMax` = `AoRadius`) into the frame's structure; the blur and
apply are SSAO's own, so the profile's radius and intensity drive both
forms. `PostProcess::Dispatch` gained an acceleration-structure binding at
4; the shader is compiled only where ray queries exist.

**The switches**: `RenderSettings::RayTracedReflections` and
`RayTracedAmbientOcclusion` (both off), `--rt-reflections=on|off`,
`--rt-ao=on|off`, C# mirror, `ResolveRayTracedReflections` /
`ResolveRayTracedAmbientOcclusion` (through `ResolveRayTracing`, plus
bindless for reflections). **The panel**: labelled "RT reflections" and
"RT ambient occlusion" under the Ray tracing checkbox; the whole block
`OnlyWhen(OffersRayTracing)` -- `RayShadows::IsAvailable()` and
`ShadowsEnabled` -- so OpenGL never shows it, and `UsesCascades` asks what
runs rather than what is ticked. **The profile**: a new registry hint
`DisabledWhen(pred, note)` beside `OnlyWhen`; `FieldEditor::DrawFields`
draws the row inside `BeginDisabled` with the note beneath. The SSR toggle
and its three dials, and the AO toggle, grey with "Ray-traced ... on in
Render Settings and ... used instead"; the AO radius/intensity stay live
because RTAO uses them. The predicates read the *resolved* state, so the
same profile on OpenGL shows every row live.

**Two things the verification found and fixed**:

- **RTAO's normal (9.8b's mistake, made a second time).** The first draft
  took the written normal wherever there was one -- "a ray does not care
  about depth-slope agreement". `check_ssao.py`'s brick-wall claim refuted
  it: AO factor p10 0.972 (bar 0.995). The written normal is the shading
  normal after the normal map; the rays go into the geometry, which is
  flat where the map says it is not. Fixed with SSAO's agreement rule
  (written only within 16 degrees of the reconstructed slope) plus one
  guard SSAO's kernel does not need -- the cosine kernel's lowest ray is
  12 degrees up, so a ray under the depth's slope is not cast. Wall p10
  1.000; seam 15.17.
- **Rays with shadows off.** `ResolveRayTracedAmbientOcclusion` said yes
  with `ShadowsEnabled` off, but the structure is built in
  `Scene::RenderShadows`, which returned before building it -- RTAO would
  trace the empty structure and the lit pass keep believing it traced.
  `ResolveRayTracing` now says no without shadows (the two options resolve
  through it), and `RenderShadows` tells `Renderer3D` before its
  shadows-off return, not after.

**Checks**: `check_ssr.py` (+2 claims): traced reflection vs a sky of the
block's colour 0.01 levels; the block moved above the top edge of the
frame reflects +109.85 traced vs -0.10 screen-space, the row derived from
the camera and the block's mirror image. `check_ssao.py` (+3): seam 15.17,
floor 0.000, wall p10 1.000, jitter-phase drift 0.04. Falsified three
ways: reflection along `-direction` (208 levels off, off-screen gain 0.00);
AO rays at zero length (seam 0.00); normal rule back to "written wherever"
(p10 0.972 -- the defect reproduced on demand).

**Verified for this commit**: Debug/Release/Dist built; scenetest 1628 on
Vulkan under validation with zero `[Vulkan]` lines, 1588 on OpenGL;
runtime and editor exit 0 on both backends with all three ray flags on
under validation; the editor eyeballed on both backends (Render Settings:
Vulkan shows the checkbox and its two options and hides the cascade dials
while ticked, OpenGL shows none of it; Properties: the greyed SSR/SSAO
rows with their notes on Vulkan, live on OpenGL with the same project);
`check_ssao`, `check_ssr`, `check_ray_shadows`, `check_bindless` on
Release; `rvdoc --check` exit 0.

**Stated limits** (7ao): hit shading is Lambert + emissive with a sun
shadow ray only, no normal map or parallax in a reflection; rough surfaces
keep the probe; no penumbra; RTAO is twelve rays through SSAO's blur, no
temporal accumulation; the ray options ride on Shadows and are off with
it, by design and by the panel.

---

### Stage 2 -- every light traces and the fox's shadow runs

**8.12 stage 2 (2026-08-16, ENGINE-NOTES 7an) is done and verified**, on top
of stage 1 the same day. Ray-traced shadows now cover **every casting light
of every kind** -- the shader traces one ray per light per pixel, along `L`
to infinity for the sun and to the light for a spot or a point, with no cap
on how many cast (the four-map budget is a maps thing) -- and **skinned
casters cast their pose**: `skin_positions.rvshader` (compute) poses each
one into a per-frame buffer and a *dynamic* BLAS is refit from it
(`AccelerationGeometryDesc::Dynamic`, `RHICommandList::BuildBottomLevelAS`,
`BufferSync::AccelerationBuild`) before the frame's TLAS build. Casters are a
pool in `RayShadows`, one posed buffer / structure / set per frame in flight
each; a skinned mesh with no pose keeps the mesh's cached bind-pose BLAS.

**At the owner's direction the setting is a checkbox**: `RenderSettings::
RayTracing` (bool, default off) replaced the `ShadowMethod` enum;
`ResolveRayTracing` replaced `ResolveShadowMode`; `--raytracing=on|off`
replaced `--shadows=maps|rt`; `RenderSettings.RayTracing` reaches C#. It
needs **no restart** -- flipping it recompiles the lit shaders on the spot,
both ways -- so there is no "restart to apply" dialogue like the backend
picker's; the tooltip says so. On a device without ray queries (OpenGL,
always) it uses the maps and logs once.

**A stale cap went with it**: `ShadowMap::kMaxLights = 8` -- "the shader's
MAX_LIGHTS", which the 3D shader has not had since 3.8a -- capped the
assignment table, so the ninth light in a scene could never shadow. It is a
vector now.

**Checks**: `check_ray_shadows.py` (21) -- the stage 1 claims plus, on a
new fixture (`make_ray_shadow_local_scene`, no sun, a spot, a point light,
five casting spots, the running fox): spot IoU 0.977, point 0.981, fox
0.946 against its posed mapped shadow and 0.854 between frames 30 and 45
while the boxes hold at 1.000; the budget warning under maps and not under
rays; OpenGL falls back. Falsified: tMax ignored (lids over the lights catch
the overshoot) collapses the local IoUs to 0.67-0.71; refit skipped gives
the fox 0.670 and 1.000 between frames -- the bind pose exactly.
`CheckRayQuery` gained the refit (1628 on Vulkan under validation, zero
`[Vulkan]` lines).

**And the flicker the owner reported in the editor viewport was SSAO's, not
the rays'** -- fixed, checked, and written up in 7an: `ReconstructedNormal`
chose its winding by the sign of `normal.z`, which for a wall seen along
its length is noise, and the noise turned with the TAA jitter; the winding
is now chosen against the ray to the point, and the reconstruction snaps
to the depth texel centre. `check_ssao.py` gained the jitter-phase claim
(0.04 levels of drift; the old sign gives 57). `--screenshot-count=N`
(consecutive frames in one run) is the tool that found it.

**Stated limits, stage 3 work**: the edge is hard (no penumbra); no
reflections yet -- hit shading needs buffers by address and a mesh table,
and with it the SSR fallback the roadmap named. Also, the compute skinning
is a second copy of the vertex shader's arithmetic (positions only); if one
changes shape the other must.

**Verified for this commit**: Debug/Release/Dist built; scenetest 1628 on
Vulkan with validation and zero `[Vulkan]` lines, 1588 on OpenGL; runtime
and editor exit 0 on both backends with `--raytracing=on` under validation;
`check_ray_shadows` 21/21, `check_ssao` (with the new claim), `check_ssr`,
`check_bindless`; `rvdoc --check` exit 0.

---

### Earlier the same day: 8.12 stage 1

**8.12 stage 1 (2026-08-16, ENGINE-NOTES 7am) is done and verified**, on the
same day as 8.2 and on top of it: acceleration structures in the RHI
(`RHIAccelerationStructure`, `CreateBottomLevelAS` / `CreateTopLevelAS`,
`BuildTopLevelAS` on the command list, `SetAccelerationStructure` on a set,
`BufferUsage::AccelerationStructureInput`, `DeviceCaps::SupportsRayQuery`),
ray queries proven in scenetest (`CheckRayQuery`, 13 checks), and
**ray-traced directional shadows** as `RenderSettings::ShadowMethod =
RayTraced` beside the cascades. `Mesh` builds a BLAS on first use;
`RayShadows` builds one TLAS per frame in `Scene::RenderShadows` (before the
graph -- building inside a render pass is forbidden); `pbr_fragment.glsl`
under `RV_RAY_SHADOWS` traces one ray per pixel in `ShadowFactor` and the
lit shaders are recompiled when the mode changes. `--shadows=maps|rt`
overrides the project (both since renamed by stage 2: the enum became the
`RayTracing` checkbox and the flag `--raytracing=on|off` -- see above).
OpenGL: null, false, falls back to maps and logs it.

**Checks**: `check_ray_shadows.py` -- traced and mapped agree to IoU 0.94
near and far, the traced edge is 0 px wide against the maps' 6, no
self-shadowing on open floor, both reproduce, OpenGL falls back; falsified
by negating the ray (IoU 0.000). **What the fixture refuted**: maps do not
stop at `ShadowDistance` -- the last cascade's footprint reaches far past
it -- so that planned claim is not made; 7am records it.

**Stated limits, all stage 2-3 work**: skinned casters trace at *bind pose*
(the fox's shadow stands while the fox runs); spot and point lights keep
their maps; the edge is hard (no penumbra); no reflections yet -- hit
shading needs buffers by address, which is the next section when it comes.

**Also today**: 8.2 bindless (75c9b22) -- read the previous START HERE
entry's substance in ENGINE-NOTES 7al, including the bound-path batch-key
bug the parity check found. E.1 Ctrl+S confirmed by the owner.

**Verified for this commit**: Debug built, both backends; scenetest 1619 on
Vulkan with validation and zero `[Vulkan]` lines, 1585+ on OpenGL; runtime
and editor exit 0 on both; `check_ray_shadows` 11/11 (Debug);
`check_bindless` and the 8.2 neighbours passed earlier today on Release.
Release/Dist were rebuilt at the end of the session; if the next session
finds them stale, rebuild before trusting a Release-config script.

---

---

**E.1 (2026-08-16), reported by the owner twice**: Ctrl+S did not save. It
saved fine with the pointer over the viewport and did nothing everywhere else
-- which is every time it matters, since you press it after changing
something and changing something means you were in a panel. The UI layer sits
*above* the editor layer and was consuming key events whenever ImGui wanted
the keyboard, and with keyboard navigation enabled that is true whenever any
panel has focus. Silently: no save, no error, no log line. Every other
shortcut went the same way. ENGINE-NOTES 7ak.

---

**X.1 (2026-08-16), at the owner's direction**: `RageV::Math` now carries the
whole of `<cmath>` the engine uses, and `RageV.Mathf` mirrors it in C#. A call
site says `Math::Sin` / `Mathf.Sin`, never `std::sin` or `MathF.Sin` -- 402
sites moved, one `std::` survives in scenetest's glm oracle and is commented.
Four functions deliberately differ from the standard library and one of those
differences is cross-language: **.NET rounds a half to even and C rounds it
away from zero**, so `Mathf.Round` names the mode. ENGINE-NOTES 7aj; the
complete list of both surfaces is a new manual page, `scripting/math.md`.

---

### The MSAA depth resolve -- M.1

**M.1 (2026-08-16), reported by the owner from the editor**: choosing MSAA
in Render Settings blurred the whole frame. The scene target's depth was
the one attachment MSAA never resolved -- true when MSAA was written,
false since 9.4 -- so depth of field, motion blur, SSAO and SSR were each
handed a 4x image for a `sampler2D`. Fixed in the RHI, both backends;
ENGINE-NOTES 7ai. **The lesson worth carrying**: every check in this
repository rendered with `--aa=none`, including the one whose whole
subject is the effect that broke. `check_depth_of_field.py` now runs its
claims at `--aa=msaa` and `--aa=ssaa` too, and `scenetest` states the
invariant without a scene.

**9.0 through 9.7 are all built** (2026-08-15), and the owner chose the
four small follow-ups before phase 8; all four are done, in this order:

1. **9.8 SSAO reads the real normal** -- done (below).
2. **9.9 SSR's exact probe replacement** -- done (below). The blend moved
   into the lighting, one frame late; ENGINE-NOTES 7af.
3. **9.10 Hi-Z for the SSR march** -- done (below). The march is a
   screen-space walk with a crossing test and a min/max pyramid; the
   sphere is smooth and the horizon clean, and 7ag says plainly that the
   pyramid does not pay on the demo's grazing rays.
4. **9.11 The probe convolution's serialized barriers on Vulkan** (V.3)
   -- done (below). Six passes and six copies per probe instead of
   thirty-six of each; Vulkan probes 0.77 -> 0.59 ms with the probe
   updating every frame, byte-identical frame.

Then **phase 8**, reopened at the owner's direction and priced in the
roadmap: every item is L or XL. 8.2 bindless is a *decision about the
engine* -- Vulkan 1.2 has descriptor indexing, OpenGL 4.5 has no
equivalent -- and wants deciding before anything that would build on it.
The two open non-blockers below (focus-click guard, orphaned LUT) are
still open.

---

### Done - terrain: skirts only from above the ground (2026-08-17)

The owner looked under a terrain and saw every seam's skirt as a wall.
`Terrain::BuildChunkGeometry` returns where the surface's indices end
(`Chunk::SurfaceIndices` per level, filled by `BuildLevel`, which now writes
into the chunk); `SelectLod` sets `m_SkirtsDrawn` from the camera in terrain
space against `HeightAt`; `DrawIndexCount(chunk)`; `Renderer3D::
DrawLayeredMesh(..., indexCount, ...)` with `PendingDraw::IndexCount` in both
run groupings and the draw; `Scene::OnRender` passes it. `make_terrain.py`
`under` fixture (`terrain_under.rage`, bright sky, camera three metres under
the ridge's plain looking away and up), `check_terrain.py` claim 6, scenetest
+10 (builder order and return, headless SkirtsDrawn on/off the rim and
through the world matrix, device-built two-by-two terrain's counts at levels
0 and 3). Falsified by forcing the skirts on. Paragraph in 7ap; manual.

---

### Done - 8.4 stage 3, terrain: the brush (2026-08-17)

Design first (ENGINE-NOTES 7ar). `Asset/TerrainBrush.{h,cpp}` (rect, brush,
recorder, copies), `Terrain` stale/Invalidate/RebuildStale/ApplyRegion/
UploadWeightRows/Raycast + BuildLevel/RefreshBounds, `RHITexture::UploadRegion`
(Vulkan `StageRegion`, GL `glTextureSubImage2D`), `Manager::EditTerrain/
SaveTerrain/SaveDirtyTerrains/IsTerrainDirty/HasDirtyTerrains`,
`Registry::Reindex`, `TerrainStrokeCommand`, `EngineConfig::BrushScript`
(`--brush=`), `RageVEditor/src/Tools/TerrainBrushTool.{h,cpp}`, the Brush
block in `SceneHierarchyPanel`, `EditorLayer` wiring (`ViewportMouseRay`,
`RunBrushScript`, keys, save), `make_terrain.py` `brush` fixture +
`terrain_brush.rage`, `CheckTerrainBrush` (+34), `check_terrain.py` claim 5;
falsified two ways; manual (terrain page Sculpting section).

---

### Done - 8.4 stage 2, terrain: a layered material, one fork in the lit shader (2026-08-17)

Design first (ENGINE-NOTES 7aq). `TerrainData::Weights` + serializer
(`layers` 0 or 4, RGBA8 per sample after the heights, refusals), `LayeredParams`
+ `LayeredMaterial` (Refresh/Bind/GetBatchKey, bound samplers or heap
slots), `Material` map getters, `TextureLoader::Red`, `pbr_layered.rvshader`
+ `include/static_vertex.glsl` + the `SampleSurface` fork in
`pbr_fragment.glsl`, `Renderer3D` third pipeline / `DrawKind` /
`LayeredSet` / `DrawLayeredMesh` / `GetTextureHeap`, `Terrain` weight
texture + `WeightUvFor` + `RefreshLayers`, `TerrainComponent::Layer1..3`
(+ registry rows, `Material` relabelled "Layer 0"), `Scene::ForEachTerrain`
+ `PrepareTerrains`, preloader wants, `make_terrain.py` paint + four `.rmat`s
+ `terrain_layers.rage`, `CheckTerrain` +21, `check_terrain.py` claim 4 on
three material paths; falsified two ways; manual (terrain page Layers
section, component and asset rows).

---

### Done - 8.4 stage 1, terrain: a heightfield as a source of meshes (2026-08-17)

Design first (ENGINE-NOTES 7ap). `Asset/TerrainData` + `TerrainSerializer`
(`.rvterrain`, `AssetType::Terrain`, manager cache, preload want),
`Renderer/Terrain` (chunk builder, four levels, skirts on shared edges,
`SelectLod`, `HeightAt`, `Resolve`), `TerrainComponent` + registry rows,
`Scene::ForEachTerrainChunk` / `SelectTerrainLods` / `HasTerrain` walked by
the scene pass, both shadow paths and the picker, Jolt height-field bodies
in `World::Build`/`AddBody`, editor icon and hierarchy mark, the manual's
terrain page + component/asset rows, `make_terrain.py`, three fixtures,
`CheckTerrain` (+53) and `check_terrain.py`; falsified three ways.
`experiments/terrain` kept and unlinked (`perlin_noise` gone). Also:
`tools/scripts/postprofile.py` now writes the registry's own FNV-1a
`SourceHash` (and no trailing newline, as the registry does) instead of 0,
so a check run no longer leaves every generated `.rvpostprofile.meta`
modified -- the churn §2 told people to `git checkout` before committing is
gone for the profiles; the demo scene and its profile still resave.

---

### Done - 8.12 stage 3, hit shading, ray-traced reflections and RTAO; offered only where the device traces (2026-08-16)

Design first (ENGINE-NOTES 7ao). **Hit shading**: `RayShadows::RayCaster`
per instance; `Renderer3D` writes `GpuRayInstance` records (96 bytes, set
0 binding 15) with the draws' material index; `pbr_fragment.glsl` under
`RV_RAY_REFLECTIONS` -- buffer references over the vertex/index/posed
buffers, `TraceReflection(origin, Ng, direction)`, `TraceShadowFrom` for a
shadow ray from an arbitrary point. `RHIBuffer::GetDeviceAddress()`
(Vulkan real, OpenGL zero). **Reflections** replace `prefiltered` at the
SSR mix point by a roughness weight; the SSR passes are skipped when on.
**RTAO**: `rtao_compute.rvshader` (460, ray query, structure at binding 4)
as SSAO's first pass; `PostProcess::RtaoCompute`; SSAO's agreement rule
for the normal plus a no-ray-under-the-slope guard.

**Settings and panel**: `RayTracedReflections`, `RayTracedAmbientOcclusion`
(off), `--rt-reflections`, `--rt-ao`, C#, resolve chain through
`ResolveRayTracing` (which now also needs `ShadowsEnabled`); registry
`OffersRayTracing` / `OffersRayReflections` gate the whole block on
`RayShadows::IsAvailable()` (+ bindless for reflections) so OpenGL shows
none of it; `FieldHint::DisabledIf/DisabledNote`, `DisabledWhen(...)`,
`FieldEditor::DrawFields` greying with the note; manual rows.
`Scene::RenderShadows` tells the lit pass before its shadows-off return.

**Checks and findings**: `check_ssr.py` +2 (exact 0.01, off-screen block
+109.85 vs -0.10), `check_ssao.py` +3 (seam 15.17, wall p10 1.000, jitter
0.04); falsified three ways; the RTAO written-normal defect (p10 0.972)
found by the wall claim and fixed as 7ao records; the rays-with-shadows-off
gap closed. Papercut noted: the vendored ImGui wraps text mid-word --
**fixed the same evening (47cc26e, merged 3379edf)**: not the vendored
wrapper but a *per-module static* -- ImGui is linked into both RageV.dll
and the exe, and since 1.92.6 the wrapper classifies characters through a
static table filled only where the atlas is built (the DLL); the exe's
copy stayed zero, so from that module every character read as a blank.
`ImGuiBinding::Bind()` fills it now. Same family as the two-module traps
§5 already lists.

---

### Done - 8.12 stage 2, every light and the skinned refit; the ray-tracing checkbox; the SSAO flicker (2026-08-16)

Design first (ENGINE-NOTES 7an). **Local lights**: the same shadow ray with
a length -- `TraceShadow(worldPos, L, tMax)` under `RV_RAY_SHADOWS`, `1e4`
for a directional light and the distance to the light for a spot or a
point; the map lookups (`ShadowFactor`, `SpotShadow`, `PointShadow`) are
not compiled under the define. `Scene::RenderShadows` under rays assigns
every casting light its kind and no slot, renders no map, and builds the
TLAS when *any* light casts (stage 1 returned early without a sun).
`ShadowMap::kMaxLights` removed; the assignment table is a vector.

**Skinned casters**: `skin_positions.rvshader` reads the `SkinnedVertex`
buffer as words (std430 would pad the vec3s; `Mesh.cpp` static_asserts the
offsets), the bones from a per-frame buffer of `RayShadows`' own, and
writes `vec4` positions; `Mesh`'s skinned vertex buffer gains
`BufferUsage::Storage`. RHI: `AccelerationGeometryDesc::Dynamic` (created
for update, not built at creation, scratch kept),
`RHICommandList::BuildBottomLevelAS` (full build first, in-place update
after, build-to-build barrier inside), `BufferSync::AccelerationBuild`
(build inputs are a *shader read* at the build stage), `IsDynamic()`.
`RayShadows::AddInstance(mesh, world, bones)` owns a caster pool reused by
index; `Build` poses, barriers, refits, then builds the TLAS.

**The checkbox** (owner's direction): `RenderSettings::RayTracing`,
`ResolveRayTracing`, `--raytracing=on|off`, `RenderSettings.RayTracing` in
C#, the manual rows and the rvdoc enum row; no restart needed, none asked
for. **`--screenshot-count=N`**: consecutive frames from one run.

**Checks and findings**: `check_ray_shadows.py` 21 claims (spot 0.977,
point 0.981, fox 0.946 / 0.854-between-frames, budget warning, OpenGL
fallback), falsified two ways; `CheckRayQuery` refit case; and the SSAO
flicker in the editor viewport, diagnosed frame by frame (period eight is
the jitter phase; AO off stops it; the reconstructed-normal winding was
chosen by `normal.z`, noise for a grazing wall) and fixed in
`ssao_compute.rvshader` -- winding against the ray to the point, uv
snapped to the depth texel -- with `check_ssao.py`'s new jitter-phase claim
guarding it (0.04 levels of drift; 57 with the old sign).

---

### Done - 8.12 stage 1, acceleration structures and ray-traced shadows (2026-08-16)

Design first (ENGINE-NOTES 7am): ray queries before pipelines, and a shadow
ray before a reflection, because a shadow needs no hit shading and hit
shading is a bindless-*buffers* project before it is a ray-tracing one.

**RHI**: `AccelerationGeometryDesc`, `AccelerationInstance`,
`RHIAccelerationStructure`; `CreateBottomLevelAS` (built immediately, like a
texture upload) and `CreateTopLevelAS(maxInstances)`; `BuildTopLevelAS` on
the command list, which packs the instances (the BLAS address is the one
field only Vulkan can fill), records the build, and ends with the
build-to-shader barrier; `SetAccelerationStructure`;
`ResourceType::AccelerationStructure` reflected from SPIRV-Cross;
`BufferUsage::AccelerationStructureInput` (dropped by a device that cannot
trace, so callers need not ask); `SupportsRayQuery`. **Vulkan**: the three
extensions and features enabled where present, VMA's device-address flag,
the descriptor pool grows the AS type, `VulkanAccelerationStructure` with
per-TLAS instance and scratch buffers so a rebuild allocates nothing.
**OpenGL**: null and no-op throughout.

**Renderer**: `Mesh::GetAccelerationStructure` builds and caches a BLAS
(skinned: bind pose); `RayShadows` (one TLAS per frame in flight plus an
empty one so the binding is never unwritten); `ShadowMode` /
`RenderSettings::ShadowMethod` with inspector rows that hide the cascade
dials under `RayTraced`; `ResolveShadowMode` beside `ResolveAntiAliasing`
with the caps fallback logged once; `Renderer3D::SetRayTracedShadows`
recompiles the lit shaders (SPIR-V cache makes it a file read) and binds the
TLAS at set 0 binding 14; `Scene::RenderShadows` builds the TLAS instead of
the cascades and keeps the local maps. **Shader**: `#version 460` on the
two lit fragment stages (glslang admits the ray-query keywords only from
460), `RV_RAY_SHADOWS` around one declaration and the body of
`ShadowFactor`; origin pushed 2-10 mm along the geometric normal, opaque,
terminate-on-first-hit.

**Checks and findings**: `CheckRayQuery` (13); `check_ray_shadows.py` (11)
-- IoU 0.944 / 0.946, edge 0 px vs 6, speckle 0, reproducible, OpenGL
fallback; falsified by negating the ray direction (IoU 0.000). The maps
reach past `ShadowDistance` (the fit is bounded, the footprint is not), so
the "no shadow past the distance" claim the design planned was dropped and
recorded. Under validation: zero `[Vulkan]` lines on the demo and the
fixture, including the per-frame TLAS build.

---

### Done - 8.2, bindless materials: one fork, and a bug in the path it replaced (2026-08-16)

Design first (ENGINE-NOTES 7al), then the code. The design's claim -- that
the split is forced in two places and neither is the RHI -- held: ~600 lines,
~200 of them Vulkan, ~150 the two forks, six in the OpenGL backend and all of
them "no".

**RHI**: `ShaderDesc::Defines` (glslang preamble, in the SPIR-V cache key);
`ResourceBinding::Count == 0` means runtime-sized;
`DeviceCaps::SupportsDescriptorIndexing` / `MaxBindlessTextures` set for
real; `RHIDevice::CreateBindlessTextureSet(capacity)`. **Vulkan**: the six
1.2 indexing features enabled where present; one canonical heap layout on
the device (`GetBindlessTextureLayout`, PARTIALLY_BOUND | UPDATE_AFTER_BIND |
VARIABLE_DESCRIPTOR_COUNT), borrowed by any pipeline whose reflection
declares a runtime array and not destroyed by it; `VulkanBindlessSet`, one
set with its own update-after-bind pool, writes landing immediately;
`VulkanSetBase` so `BindResourceSet` binds either kind. **OpenGL**:
`CreateBindlessTextureSet` returns null and says so once. **Renderer**:
`TextureHeap` (free list, dedup by (texture, sampler), weak textures, slot 0
= magenta, every slot written at creation, retire-then-recycle by frame in
flight); `Material::WriteRecord` and a 64-byte `GpuMaterial` per distinct
material per frame at set 0 binding 13; `InstanceData.Indices.z` names it;
`GetBatchKey(bindless)` returns 0 so runs merge across materials; the heap
bound at `TextureHeap::kSet` = 2 after each pipeline bind. **Shader**: one
`#ifdef RV_BINDLESS` block in `pbr_fragment.glsl` -- `u_Textures[]`,
`u_Materials`, `#define u_BaseColorMap u_Textures[nonuniformEXT(...)]` -- and
everything below it unchanged; `pbr_skinned` shares it.

**Checks.** `CheckBindlessHeap` in scenetest (32 checks on Vulkan: the
shader/renderer convention, slot bookkeeping, retire-and-recycle, and a
compute shader that samples through the heap and reads back red/green/blue
and magenta; 11 on OpenGL: the convention plus a stated skip).
`check_bindless.py`: on equals off to zero pixels on Vulkan, each path
reproduces, OpenGL takes the bound path, GPU-AV is quiet. Falsified three
ways: a wrong normal-map slot (1,432,357 px differ), a slot pushed past
capacity under `--validation=gpu` ("Index of 5004 used to index descriptor
array of length 4096" -- and nothing under `--validation=on`), and the batch
key reverted (1,036,879 px differ, the walls).

**Honest limits.** The parity check covers the maps the courtyard samples;
a wrong emissive/metallic/specular slot is invisible to it because no
courtyard material has one (verified: swapping emissive gave zero pixels
differing). 2D, UI and post are deliberately not on the heap -- see 7al for
why, and what it would cost to move them.

**Verified**: Debug/Release/Dist built; scenetest 1606 on Vulkan with
validation (zero `[Vulkan]` lines), 1585 on OpenGL, zero FAIL; editor and
runtime exit 0 on both backends; check_bindless, check_ssr, check_oit,
check_depth_of_field, check_ssao, check_smaa all pass on Release; rvdoc
--check exit 0.

---

### Done - D.5d, the check reads the values, not only the names (2026-08-16)

The coverage check asserted a row exists. It said nothing about what the row
claims, and that gap is where all three errors in D.5b and D.5c lived.

**Defaults**: the header's initialiser against the row's second column,
compared *numerically*, so `0.5f` against `0.5` is an agreement rather than a
special case. Only bare scalars are comparable; a handle, a vector or an
enumerator is counted as unchecked **and the count is printed**, so the
coverage is a figure rather than an impression. 114 compared.

**Enumerators**: every value of an enum the manual describes has to appear on
the page as a code span. 43 values across 13 enums. This is the check that
would have caught `ConstantPixelSize`.

Both falsified: `BloomKnee` changed to 0.4f gives *"defaults to 0.4f, but
post-processing.md says 0.5"*; `Capsule` renamed to `Pill` gives
*"ColliderShape::Pill exists but components.md does not name it"*.

**Writing them found three flaws in the checker, all the same family as the
bugs it hunts -- a check asserting less than it appears to.**
`DocumentedDefaults` was flat per page, so `Speed` on the animator collided
with `Speed` on the emitter and three *correct* defaults were reported wrong;
it is per heading now. `EnumValuesOf` read line by line, so a single-line enum
yielded one value and the check passed having verified half of it; it splits
on commas now. And `.at("")` threw on any page with two-cell tables.

**What is still unchecked, and cannot be**: the ten prose pages, and the
description text in every row. Nothing automatic can ask whether a paragraph
is still true.

---

### Done - D.5c, the missing pages (2026-08-16)

Ten pages, closing the half of the owner's criticism that D.5a and D.5b did
not: **materials, lighting, cameras, physics, audio, animation, user
interface, prefabs, assets, and the editor**. The manual went from 9 pages to
**22**.

These are prose rather than field tables -- how a thing fits together, which
is what `components.md` deliberately does not say. So they are *not*
drift-checked, and that is the honest limit of this slice: the numbers cannot
rot because the reference owns them, but a paragraph describing behaviour can
go stale and nothing will notice.

What each page is for, briefly: `lighting.md` says why there is no light cap
and what the sky is (a light, not a backdrop); `materials.md` says why tiling
is on the material and the overrides are per entity; `physics.md` names the
sleep rule that makes `OnCollisionExit` not fire; `ui.md` states the anchor
model in one sentence -- *equal anchors mean the offsets are a size, different
anchors mean they are a margin*; `assets.md` covers handles, `.meta` files,
cooking, the import cache and packaging; `editor.md` lists every panel, every
shortcut and both save schedules.

**Three factual errors were caught while writing, two of them mine.**
`CanvasScaleMode` is `ConstantPixels`, not `ConstantPixelSize`. `AudioBus` has
four members, not three -- `UI` was missing. And the camera's projection
default is genuinely ambiguous: the *type* defaults to orthographic, the
editor sets perspective on every camera it creates, and the page now says
both rather than picking one.

**Still open**: nothing is checked in these ten. If a system's behaviour
changes, the page describing it has to be updated by hand, and the only thing
that catches a mistake is somebody reading it.

---

### Done - D.5b, every component documented, and checked (2026-08-16)

`components.md`: all **23 component structs and roughly 150 fields**, each with
its default and what it does. Eighteen of the twenty-one registered components
had no documentation at all before this.

Two things worth keeping from how it is written. **Runtime state is called out
separately per component** -- `Probe`, `Skinning`, `Pool`, `Hovered`,
`PreviousWorld` and the rest are real and reachable from a script but derived,
not saved and not in the Inspector, and "I set it and it did not survive a
reload" is a question the manual now answers. And the two components nobody
adds -- `IDComponent`, `RelationshipComponent` -- are documented rather than
hidden, because the scripting API reads them.

**All 23 are in the drift check**, one page, names unioned across the structs
so a field two components share is satisfied by documenting it once. Falsified
on a real component: adding a field to `ColliderComponent` gives
`ColliderComponent::TemporaryUndocumented exists but components.md does not
document it`, exit 1.

**Extending it found a second hole in the checker**, after the parenthesis one
in D.5a. The guard "fewer than four members parsed means the parser is lost"
was right for one large class and wrong for components: eight of them have
fewer than four fields, so they were skipped *and then* every line the page
wrote about them was reported as documenting something that no longer exists --
a failure that reads as the manual being wrong when the manual was right. The
threshold is one now, and the reasoning is in the code: a lost parser reads
zero, a half-lost one leaves fields undeclared and the opposite direction
catches it. **The two directions together are the check; neither alone was.**

**Still open**: the system pages -- materials, lighting, cameras, physics,
audio, animation, UI, prefabs, the asset pipeline and the editor's panels --
explaining how these fit together rather than what each field is.

---

### Done - D.5a, the manual states the settings (2026-08-16)

The owner's criticism, and it was right: the manual explained a few things
thoroughly and assumed the rest. The example given was anti-aliasing -- named
in a table, with its five modes, `MsaaSamples` and `SupersampleFactor` never
listed anywhere. The audit behind it: **36 post-processing settings of which
about 6 were documented, 12 render settings of which none were**, plus twelve
whole systems with no page at all.

Two new pages, `rendering.md` and `post-processing.md`, each stating every
setting with its default, its range and what it costs -- including the measured
TAA feedback curve, which was in a header comment and nowhere a user could
reach. The per-effect detail moved *out* of `concepts.md`, which now carries
the concepts and a nine-row summary pointing at the reference; two places to
keep in step is how this rots.

**The part that makes it hold: both pages are drift-checked.** `rvdoc --check`
already compared `ScriptableEntity` against the C++ reference, and its table
said in a comment that the point of being a table was for more headers to join
it. `RenderSettings` and `PostSettings` are now two more rows, so adding a
field to either struct fails the check by name until it is documented.
Verified by adding one: exit 1, `PostSettings::TemporaryUndocumented exists but
post-processing.md does not document it`.

**And extending it found a hole in the checker.** The field parser skipped any
line containing a parenthesis, to avoid matching functions -- which also
skipped every field whose default is a call, so `UUID ColorLut =
UUID::Invalid();` was invisible and could never have been reported as
undocumented. It now tests the declaration rather than the whole line, and the
count went from 36 to 37.

**Still open**: this covered the settings. The missing *pages* -- materials,
lighting, cameras, physics, audio, animation, UI, prefabs, the asset pipeline,
the editor's panels -- are not written.

---

### Done - E.1, Ctrl+S saves from anywhere (2026-08-16)

ENGINE-NOTES 7ak. Two paths to one symptom, both fixed.

**The UI layer was eating the shortcut.** `ImGuiLayer` is an *overlay*, so it
is above `EditorLayer`, and `Application::OnEvent` stops at the first layer
that marks an event handled. Its rule consumed keyboard events whenever
`io.WantCaptureKeyboard` was set -- which, with
`ImGuiConfigFlags_NavEnableKeyboard` on, is true whenever any panel has
focus. So after changing anything in the Inspector, Ctrl+S, Ctrl+Z, Ctrl+N,
Ctrl+O, Ctrl+P and Delete all did nothing and said nothing. The rule now asks
`io.WantTextInput`: a caret in a field is a claim on the keyboard,
navigation focus is not.

**The blocker was also going stale.** It is set inside the Viewport panel's
draw, and `ImGui::Begin` returns false when that panel is collapsed or behind
another tab -- and the Game panel shares its dock node. Looking at the Game
tab took the early return, which left last frame's focus flags in place. Both
are cleared on that path now.

**Check**: `UiConsumesEvent` is a pure function of what ImGui says it wants,
so `CheckShortcutOwnership` can state the whole truth table with no context,
no window and nobody pressing a key. Restoring the old rule fails exactly one
line of it -- "a focused panel does not swallow a shortcut" -- which is the
bug, named. 1574 checks.

---

### Done - X.1, the engine's own <cmath> (2026-08-16)

ENGINE-NOTES 7aj, manual page `scripting/math.md`. `RageV::Math` gained the
trigonometry, exponentials, rounding, interpolation and queries the engine had
been reaching into `std::` for; `RageV.Mathf` is the C# mirror, named with the
`f` because a `RageV.Math` collides with `System.Math` under the two usings
every script carries. **Four deliberate differences from the standard
library**: `Acos`/`Asin` clamp their argument (the same NaN that `Normalize`
has guarded since 5.0c, met from the other side), `Mod` takes the sign of the
divisor while `FMod` takes the dividend's, `Fract` is always positive, and
`SafeSqrt` sits beside `Sqrt` rather than replacing it.

**Two things it fixed rather than renamed.** `Min`/`Max`/`Clamp` were
float-only, so `Max(width / 2u, 1u)` compared two unsigned values *as floats*
-- exact only below 2^24, and every such call site is a pixel count or a
buffer size. They are templates now, and mixing an int with an unsigned is a
compile error rather than a silent pick. And `Round(float)` moved into the
header with the other one-line forwards.

**Checks**: `CheckMathFunctions` in scenetest holds the wrappers against
`<cmath>` on awkward values and states each deliberate difference as its own
claim, including `Max(16777217u, 16777216u)` -- two numbers that are the same
float. Across the boundary, `Interop.EvaluateMath` walks 39 functions × 10
inputs and requires the two languages to agree; reverting the rounding mode
makes it say `Round(0.500000): C# 0.000000 vs C++ 1.000000`. 1567 checks.

**A neighbouring check turned out to be luck, and is not a regression from
this** (proved by stashing the work and watching the same failure):
`check_oit.py` demanded two renders' silhouettes match *to the pixel*, and
they differ by 36/0/76/0/0 at frames 10/20/30/45/60 out of ~152,000 covered.
It also rendered without `--frame-time`, so which frame it got was a
stopwatch. Time is pinned and the claim is now 0.5 % of the covered area --
ten times the worst fringe, forty-five times smaller than the upside-down
render it exists to catch (34,304 pixels).

---

### Done - M.1, the depth a multisampled target hands out (2026-08-16)

ENGINE-NOTES 7ai. A target whose depth is both multisampled and sampled
now carries a single-sampled depth twin, resolved by the pass that wrote
it -- `VK_RESOLVE_MODE_SAMPLE_ZERO_BIT` on Vulkan, a `GL_DEPTH_BUFFER_BIT`
blit on OpenGL -- and `GetDepthTexture` hands the twin out, exactly as
`GetColorTexture` has handed out the colour twins since MSAA existed.
Sample zero rather than an average, because averaging two depths across a
silhouette invents a surface between them. The twin exists only where
`DepthSampled` is set, and the multisampled attachment loses its `Sampled`
usage so the mistake cannot be made quietly again. One extra barrier
subtlety, written up in 7ai: a depth resolve is a *colour-attachment-output*
write, so the twin's transitions name that stage as well or sync validation
reports a write-after-write on the first frame.

Nothing above the RHI changed: the four passes that read depth, their
shaders and the frame graph are untouched, and no other AA mode moves.
**Cost**: one D32 image at frame size under MSAA only (14 MB at 1440p);
1440p demo on Vulkan, 3.92 ms at MSAA 4x against 3.86 TAA and 3.67 with AA
off. **Checks**: `check_depth_of_field.py` re-runs its lens claims at
`--aa=msaa` and `--aa=ssaa` (reverted, that is 23 % of the in-focus detail
kept on Vulkan, 28 % on OpenGL, against 115 % fixed), and
`CheckMultisampledDepth` in scenetest states the RHI invariant directly.
Verified: 1547 scenetest checks both backends under validation, zero
`[Vulkan]` lines, runtime at all six AA modes and the editor at MSAA on
both backends, all three configs, `check_ssao` / `check_ssr` /
`check_motion_blur` / `check_oit` / `rvdoc --check`.

---

### Done - 9.11, the probe convolution in six passes (2026-08-15)

ENGINE-NOTES 7ah. `EnvironmentIBL::Convolve` renders a level's six faces
as six viewports into one strip-shaped scratch in one render pass, and
copies them with the new `RHICommandList::CopyStripToTextureLayers` --
one transition pair and one six-region blit on Vulkan, six framebuffer
blits on OpenGL -- instead of thirty-six passes and thirty-six full-image
copies. The Vulkan viewport carries the negative height by hand, because
a viewport set inside a pass does not inherit the pass's flip. Probe
updating every frame at 1440p: Vulkan probes 0.77 -> 0.59 ms, OpenGL 0.24
-> 0.23; the demo frame on Vulkan is byte-identical before and after.
What remains of the gap is the six scene captures and their single-face
copies, and whole-array transitions -- named in 7ah, optional under the
15 Hz dial. Verified: 1538 scenetest checks both backends under
validation, zero `[Vulkan]` lines, editor demo both backends, all three
configs.

---

### Done - 9.10, the SSR march is a screen-space walk with a pyramid (2026-08-15)

ENGINE-NOTES 7ag. Priced as performance, it was correctness first: the
owner looked at the demo's brass sphere under 9.9's correct weight and saw
7ad's march -- fixed view-space steps compared against depths sampled a
texel away -- as orange flecks and self-hits. The march now walks in
screen space with the ray's depth interpolated perspective-correctly to
each texel it visits, a hit is a *crossing* (in front, then not), the
thickness asks how far behind the ray already was at the sample *before*
the crossing (asked of the sample after, it drops every steep ray -- a bug
on the way), and the landing is solved between the bracketing samples.
Above that, a min/max pyramid (two atlases, two passes, R32G32F, six
levels; `include/hiz_atlas.glsl` is the one layout both shaders use) lets
rays that leave their surface cross the frame in a dozen iterations. Rays
that hug their surface cannot use it at any level, and the demo's floor
and walls at a graze are all such rays -- so level 0 is a three-texel
stride growing 2 % a step, and the pyramid takes over only when a sample
finds the ray clear of its surface.

**Numbers, 1440p, demo, SSR total:** 7ad linear 0.46 ms (wrong), DDA
alone 0.67, walk + pyramid 0.75 (0.13 fixed, 0.08 pyramid). Written down
as it is: the pyramid does not pay on this scene. It pays where reflections
leave their surfaces. **Quality:** the fixture sphere's crescent is solid
where it was ragged with a hole (limb 84 -> 104 levels); the horizon band
is gone on both backends (zero pixels differ outside the block's
reflection); exactness still 0.00; the demo sphere reflects the wall and
flame as a continuous band. All checks green, both backends 0.1 rows apart.
The single-pass pyramid was tried first and cost 0.4 ms by itself -- a
thousand serial fetches per texel of the smallest level; two passes cap
the block at 64.

Verified: 1538 scenetest checks both backends under validation (the SSR
graph check names all four stages now), zero `[Vulkan]` lines, editor
demo both backends, all three configs, `check_ssr.py`, `check_ssao.py`,
`rvdoc --check`.

---

### Done - 9.9, SSR replaces the probe exactly (2026-08-15)

ENGINE-NOTES 7af. The follow-up said "one more channel"; the exact weight
is six numbers the post pass can never see, so instead **the blend moved
into the lighting**: the trace is written at the end of a frame into a
per-chain `TemporalHistory` pair (`FrameDesc::Reflections`, one each in
the editor's two chains and the runtime), and the next frame's PBR shader
swaps the probe's `prefiltered` for the traced radiance at the one line
where the split-sum weight and occlusion have not applied yet. Same
weight, same occlusion, same F0. One frame late, reprojected through
`v_PrevClipPos`; nothing new on the scene target; off is a uniform branch.
`Renderer::SetScreenReflections` hands the texture to the scene pass on
the jitter's edges. The resolve writes radiance + confidence and blends
nothing. SSR passes now sit *after* SSAO (a ray's radiance should carry
the crease it landed in) and are a side chain -- `shaded` is untouched.

**The law**: a metal floor with SSR on under a uniform sky K equals the
same floor with SSR off under a uniform sky the colour of the block it
reflects. `check_ssr.py` measures it: **0.00 levels** difference on both
backends. The mirror region's gain went 23 -> 80 levels and the sphere
limb 15 -> 84 -- the amount a dark metal at grazing incidence really
reflects, which the guess had under by the Fresnel term. Cost unchanged:
0.46 ms Vulkan / 0.39 OpenGL at 1440p on the demo.

**Two artefacts the correct weight exposed.** (1) The resolve read the
half-res trace *filtered* -- averaging hit uvs, which are places -- and
sampled the lit image at the average: a yellow dash of the block on the
far floor at the horizon, Vulkan only. It now point-samples the four
nearest trace texels, resolves each at its own hit and blends radiances;
gone. (2) Grazing self-hits at a far floor's horizon: a two-row band a few
levels off on one backend, inherent to a linear march compared against
point-sampled depth. **Not patched -- 9.10's screen-space DDA is the fix,
and this band is its measurement.** (3) Not an artefact but a trap:
`u_ScreenReflections` is binding **12**, because 11 is the skinned
pipeline's bone buffer and the first build was a device loss when the fox
drew. Validation named it in one line.

Verified: 1538 scenetest checks both backends under validation (seven new:
no history means no SSR passes; a history means both, advanced, paired,
after SSAO apply; off forgets the trace), zero `[Vulkan]` lines, editor
demo both backends, all three configs, `check_ssr.py` (now with the
exactness case) and `check_ssao.py`, `--benchmark` on both backends,
`rvdoc --check`. `TextureLoader::TransparentBlack` exists now because
`Black` is opaque and a confidence of one from a stand-in would have
mixed black into every metal.

---

### Done - 9.8, SSAO reads the real normal, and the transform it exposed (2026-08-15)

ENGINE-NOTES 7ae. SSAO's compute pass now binds the surface attachment and
takes its normal wherever the scene wrote one **and it agrees with the
geometric normal within 16 degrees** (9.8b, same day); the 7ac
reconstruction is the answer everywhere else. The first cut took the
written normal wherever it faced the camera and the owner saw the demo's
brick walls go off: a normal-map bump is not in the depth buffer, and a
hemisphere tilted by it dips into the flat wall the depth buffer *does*
have. `check_ssao.py` now has a brick wall seen along its length: AO
factor over the open wall 1.000 / worst 0.984 under the rule, 0.997 /
0.954 in the brick pattern without it. On the box fixture the seam darkens
13.6 levels on both backends where the reconstruction gave 12.1 (Vulkan)
and 13.3 (OpenGL) -- box faces agree, take the written normal, and it does
not wobble with each backend's depth quantisation. Open floor still 0.000.

**What the swap found.** Writing the "world normal into the reconstruction
frame" transform once for both passes meant deriving it, and the derivation
disagreed with 7ad's trace. The frame is view space under `diag(1,1,-1)`
on OpenGL and `diag(1,-1,-1)` on Vulkan (z is the depth; y follows texel
rows); the trace had negated y on the unflipped backend and nothing on the
flipped one, which is the correct map's *negation* exactly when the
view-space normal has no x-component -- and `reflect()` cannot tell a
normal from its negation, so a floor, a facing wall and a sideways wall all
reflected right, and every fixture and the demo courtyard was made of
those. A mirror sphere told them apart: its left limb reflected a slab
standing to its left on the *right*. `check_ssr.py` now has that sphere
(left 15.4, right 0.0 levels; the old transform scores 0.9 / 1.7 and
fails), and the floor case is unchanged to the second decimal -- it could
never have told. `include/view_reconstruction.glsl` is the one statement
of the frame, the map and the "is this texel a surface" sentinel (which
moved from the normal channels to the roughness channel; 7ae says why the
normal could lie). SSAO and the SSR trace both include it.

`PostProcess::ViewReconstruction` carries near/far, the inverse projection
scales and the view matrix for both passes; `SsrParams` nests it;
`SsaoCompute` takes the surface texture and one of these instead of five
loose floats. Verified: 1531 scenetest checks on both backends under
validation, zero `[Vulkan]` lines, editor demo both backends, all three
configs, `check_ssao.py` and `check_ssr.py` green, `rvdoc --check`.

---

### Done - 9.7, SSR (2026-08-15)

ENGINE-NOTES 7ad. **The scene target grew its first new attachment since
velocity**: RGBA8, octahedral world normal + roughness + metallic, written
by the PBR shaders and zeroed by everything else. The record of every place
its shape had to be stated is in 7ad, and it is the list the next
attachment will need. Then a half-resolution linear march with binary
refinement (48 steps, 6 refinements -- fewer bands), writing hit uv +
confidence, and a full-resolution resolve that blurs the hit by roughness
and blends it in with a stated approximation of the probe term it replaces.
After the resolve, before SSAO. `ScreenSpaceReflections` /
`SsrMaxDistance` / `SsrThickness` / `SsrIntensity` on the profile; the
demo's plinth cap became polished steel so the effect has something to
show -- the sphere seen from the cap's own viewpoint. **Not** stripped from
the editor scene view: it follows whichever camera you look through.

**One measured sign, in the family of taa_resolve's** -- and, it turned
out, measured on the one fixture that could not tell it from a wrong one.
The transform this entry originally described (negate y on the unflipped
backend) was correct only for normals with no x-component in view space;
9.8 derived the frame properly, replaced it, and put a mirror sphere in
the check. The 9.8 entry above and ENGINE-NOTES 7ae have the account.
`check_ssr.py` (~1.5 min, both backends): off is off to the byte, the
mirror floor gains 23 levels under the block, a roughness-0.7 floor gains
9, empty floor drifts 0.000, a frame reproduces, the two backends land
the reflection 0.1 rows apart, and the sphere reflects the slab on the
slab's side. ~0.45 ms GPU at 1440p.

---

### Read before touching SSR: what 7ac and 7ab deferred to it

ENGINE-NOTES 7ac (SSAO) and 7ab (motion blur) each say which decisions
were deferred to SSR; 7ad records how each landed.

**What it reuses, all in place:** the sampleable scene depth (7z), the
projection's inverse scales on `FrameDesc` (7ac), the view-space
reconstruction in `ssao_compute.rvshader`, the half-res compute + blur +
apply pass shape, the pixel-seeded dither rule, and the DoF gather's
depth-ordered tap test. The reflection probes are the fallback where a ray
leaves the screen or hits nothing -- `ProbeArray` slot selection already
happens per object in the PBR shader (7.7), so the *same* probe answer is
what SSR should blend toward.

**The design question that decides everything: SSR needs two things SSAO
deliberately went without.** Real normals -- normal-mapped, per pixel,
which no depth reconstruction can produce -- and roughness, to know how
blurred (or whether) a reflection should be. Neither is in the scene
target. Two routes:

1. **A G-buffer-lite attachment on the scene target**: octahedral-packed
   normal + roughness (+ metallic) in one RGBA8 or RG16F+R8. The honest
   route. It is also the full 7q ceremony: `FrameGraphBuilder`'s scene
   target desc, `Renderer::SetTargetFormats`, every scene pipeline's
   `ColorFormats` (PBR, skinned PBR, sky, grid, particles, world text,
   icons, debug), the transparency pass's `WriteAttachments`, scenetest's
   two call sites, and **both** probe capture paths -- the pattern that
   "bit three times in a row" when the velocity attachment landed. Grep
   `R16G16_SFLOAT` for every place the velocity attachment had to be
   stated; the new one needs the same list. 7ac recorded that SSAO switches
   to reading this attachment in the same commit it lands.
2. **Reconstructed normals + no roughness**: reflect everything as a
   mirror, only on surfaces whose depth-normal is smooth. Cheap, wrong on
   any normal-mapped surface, and cannot express a rough floor's blurred
   reflection. Not recommended -- the demo's brick floor is normal-mapped
   and would look like glass.

Route 1 is the one to build. Then: a hi-Z or linear ray march in view
space against the depth buffer (linear with a fixed step count and a
binary refinement is enough at this scale; hi-Z is a later optimisation),
edge fade so rays leaving the screen blend to the probe rather than
cutting, roughness-driven blur of the hit (or fewer, jittered rays --
pick one, record why), a temporal component is **not** in scope for the
first version, and the result blends into the lit image *before* SSAO
applies (a reflection is lighting; occlusion darkens it like everything
else). Placement: after the resolve, before SSAO. Off must be exact.

**Checks that would prove it:** a mirror floor under a bright block shows
the block below the horizon line; a rough floor shows a *blurred* version;
a ray leaving the screen produces no seam against the probe fallback; a
normal-mapped surface reflects along the mapped normal (the brick floor,
not a flat plane); off is off to the byte; a frame reproduces. Reuse
`make_ssao_scene.py`'s box-on-a-floor and give the floor metallic 1 /
roughness 0 for the mirror case.

**Cost expectation:** the attachment costs every scene pipeline a
fragment output and the target ~4 bytes/pixel; the march is the expensive
half and should be measured half-res first.

---

### Done - 9.6, SSAO (2026-08-15)

ENGINE-NOTES 7ac. Occlusion from depth alone: view position from the
projection scales FrameDesc now carries, normals from chosen-neighbour
differences (each axis differences toward whichever side is closer in
depth, so silhouettes keep their own surface's slope), a golden-angle
hemisphere at half resolution, two depth-aware blur passes, and a multiply
on the linear HDR image before DoF -- the stated forward-renderer
compromise, kept honest by restraint. The bias grows per metre, which is
what holds a grazing-angle floor at *zero* self-occlusion; the fixed bias
alone mottled it. `AmbientOcclusion` / `AoRadius` / `AoIntensity` on the
post profile; the demo runs 0.5 m at 0.8.

`check_ssao.py` (~1 min, both backends) on a box-on-a-floor fixture
measures restraint as much as darkness: off is off to the byte, the
contact seam darkens (12-13 levels) and deepens monotonically with
intensity, **the open floor holds at 0.000 levels of drift**, and a frame
reproduces -- the kernel rotation is seeded per pixel, never per frame.
~0.25 ms GPU at 1440p on the demo.

---

### Done - 9.5, motion blur (2026-08-15)

ENGINE-NOTES 7ab. McGuire-style reconstruction on the TAA velocity buffer:
pack (velocity convention + linear depth settled once), tile max, 3x3
neighbour max, then a full-resolution gather along the neighbourhood's
dominant motion with depth-weighted taps -- the blur happens where motion
*lands*, so objects smear over what they pass. After DoF, before bloom.
`MotionBlur` / `MotionBlurShutter` / `MotionBlurMaxRadius` on the post
profile; the demo runs a 0.5 shutter; the editor's scene view strips it
with the rest of the cinematic stack.

`check_motion_blur.py` (~1 min, both backends) on the falling-block
fixture: off is off to the byte, a still region under the blur is
untouched to the byte (the gather early-outs into an exact RGBA16F copy),
the smear is on-axis and scales 2.33x when the shutter doubles, and a
frame reproduces -- the dither is seeded per pixel, never per frame.
Costs ~0.24 ms GPU at 1440p on the demo. Known and documented, not bugs:
skinned meshes smear by the object's motion, not the limb's, and
particles and UI write zero velocity and never smear.

---

### Done - declaration-site script fields (2026-08-15)

ENGINE-NOTES 7aa. A C++ script field is declared once, where it lives:

```cpp
RVShowInEditor
float Swing = 0.34f;      // in the inspector, stored in the scene

RVCallable
void Ring();              // nameable by a button's OnClick

RVScript                  // a class with nothing else marked
class Rotator : ...
```

No trailing block. The markers are empty macros (ScriptableEntity.h); `rvgen`
-- a thin CLI over `ScriptGen` in the engine, where scenetest tests it --
scans `Source/` at **configure time** and emits one wrapper TU per marked
file that `#include`s the file and appends the registrations, because a class
defined in a .cpp can only be named by a TU that includes it. The module's
CMakeLists excludes wrapped .cpps from the glob and adds
`CMAKE_CONFIGURE_DEPENDS` on every script, so an edit re-runs the scan
through *both* build paths (Build Scripts and a by-hand cmake) with one hook.

What to know before touching it:

- **Refusal is the contract.** Every marked declaration the scanner cannot
  fully parse is a build error naming the line, MSVC-shaped so the build
  panel parses it. Never make it guess; a field that silently fails to
  appear is the failure this replaced.
- **`RV_REGISTER_SCRIPT` still works** (Rotator still uses it); both styles
  on one class is a build-time error, not the registry's first-wins.
- **The pair:** TemplateProbe.cpp proves markers-without-generator register
  *nothing*; scenetest's `fixtures/rvgen/MarkerProbe.cpp` goes through the
  real rvgen at build time and proves the same shape registers everything.
  Bell and Anvil are migrated, so `CheckDemoButtons` rides on generated
  registrations.
- **Old projects:** a `Source/CMakeLists.txt` that predates rvgen plus
  markers in a script warns in the build panel (`ModuleBuild::Build` scans
  before every build). Knockdown's CMakeLists is updated; third-party
  projects get the warning.

---

### Phase 9 in one paragraph, and the velocity-buffer warning

Phase 9 is **9.0 through 9.7 done** -- all of it -- plus the `.rvlut`
recipe and TAA's two extra dials. The order the items were built in is the
order their ENGINE-NOTES sections run: 7s (settings), 7t/7v (LUT and
recipe), 7y (auto exposure), 7x (grain), 7z (DoF), 7ab (motion blur), 7ac
(SSAO), 7ad (SSR). Each later one says which decisions the earlier ones
deferred to it, so read them in that order if touching the post chain.

**Read 7r before touching the velocity buffer** (which motion blur and TAA
now both read). Velocities are jitter-free by construction, and skinned
meshes report the *object's* motion rather than the limb's, because bones
are not double-buffered. Both will look like bugs.

### The demo scene has a rule now

`SampleProject/assets/scenes/demo.rage` is **generated** by
`tools/scripts/make_demo_scene.py`. Edit the generator and re-run it; the
scene it emits is byte-identical to what the engine writes on load-and-save,
so it round-trips and the editor can still open it normally.

It is a generator because of what the old scene had become: thirty-four
entities called things like "Sphere (gold)" and "Falling Box 3" on a grid --
every feature present and none of them anywhere for a reason. A 700-line YAML
is the shape that lets a checklist accumulate one entity at a time with nobody
able to see it happening.

**So: a feature goes into the demo when the place would have one.** It is a
forge courtyard at dusk, and every section of the generator says what it is
for. Adding depth of field meant focusing it on the plinth; adding auto
exposure meant a courtyard with a shaded side and a sunlit one. Adding
something with no reason to be there means writing that argument out loud and
finding it does not hold.

### Closed defect: the 74 first-frame validation lines (2026-08-15)

Auto exposure kept one descriptor set and one params buffer per pipeline;
the editor meters two frame chains per frame and records both before the
GPU runs either, so the second chain rewrote sets the command buffer already
held. Fixed by pooling per dispatch (`5b2ca2f`). **A permanent tripwire came
out of it**: under validation, `VulkanDevice` tracks every descriptor set
bound into the current recording and `VulkanResourceSet::Commit` names the
pipeline and bindings when it is about to rewrite one -- the layer only ever
reports the anonymous consequence. Its lifetime is one recording, cleared at
frame begin and after immediate submits; a present-cycle lifetime flagged
safe reuse in scenetest's AA-switch loop, whose no-warnings assertion caught
that too. Zero `[Vulkan]` lines is the bar again, and it holds.

### Two things left open, neither blocking

**The focus-click guard has never been confirmed against a real click.** Alt-tab
into the editor with the cursor over a Render Settings slider and press -- the
value must not move. Thirty seconds, and it closes the one thing that could not
be tested here.

**An orphaned LUT is not warned about**, the way an orphaned post profile now
is. It means scanning every profile asset rather than walking the scene's
cameras. Worth doing if it recurs.

---

**Done: 9.4, depth of field (2026-08-15).** ENGINE-NOTES 7z. A thin-lens circle
of confusion -- focus distance, focal length, f-number -- then a
half-resolution golden-angle gather and a full-resolution composite. Off by
default and off exactly, so nothing else moved.

**Two of the three claims in the roadmap's own entry needed revising.** The
depth buffer was "already a graph resource" and was one *nobody could read*:
`RGTargetDesc::SampleDepth` had never been set. And it is not a separable
blur, because a disc is not separable.

**The two bugs it shipped with both came from the same misconception**, which
is the part worth carrying forward: *a gather cannot be sized by the pixel
doing the gathering.*

- A defocused **foreground** never arrived. The sharp gaps between the bars had
  a small circle of confusion of their own, so they gathered a radius of about
  a pixel and never reached the bars that were supposed to be spreading over
  them -- and the bars, gathering only each other, averaged a uniform colour
  and came out *identical to no blur at all*. Fixed by gathering at the largest
  radius the settings allow and letting the per-tap test decide.
- A blurred **background** bled over sharp foreground. "Does this tap's circle
  of confusion reach here" is the right question in front and the wrong one
  behind: a blurred background does reach a sharp foreground, and is then
  **occluded** by it. Fixed by capping a tap that is behind the centre at the
  *centre's* radius, so a sharp pixel admits nothing from behind it and a
  blurred one admits everything, with no special case in between.

An earlier attempt at the second had a blanket "anything in front always
contributes" rule, which fired for taps in front by a hair and perfectly
sharp -- it softened exactly what was supposed to be crisp. **A special case
that fires on the sign of a value will fire on the noise in it.**

Unlike 9.2 and 9.3 this adds **no determinism hazard**: the sample pattern is
a fixed golden-angle spiral, so the pass is a pure function of the image and
the settings and `--screenshot-frame` needs nothing new from it.

---

**Done: 9.2, auto exposure (2026-08-15).** ENGINE-NOTES 7y. A 256-bin log2
luminance histogram via compute, reduced over a percentile window, adapted in
log space so the speed is uniform in stops.

**The design question the previous handoff flagged had a better answer than any
of the three it listed.** `--frame-time` already makes everything downstream a
function of the frame number -- its own comment says so -- and every screenshot
check already passes it. So the rule is one line about where a number comes
from: *the adaptation reads the frame time the loop hands down, never a clock
of its own.* A new `--exposure=fixed` flag would have been a second thing every
check had to remember, to solve a problem the first flag already solved.

And the real protection is that it is **off by default and off exactly**:
`check_smaa`, `check_color_grading`, `check_taa_*` and `check_lens_effects` all
kept their recorded thresholds with no change at all. `check_smaa` reports the
same six numbers to three decimals as before the feature existed.

State is **one per frame chain**, beside `TemporalHistory` -- 7u's ghost one
layer up. Two chains sharing an adapted value would have a bright game view
darkening the viewport and the viewport brightening it back, forever.

**The bug worth carrying forward, because it is a shape rather than an
incident.** Vulkan converged about four seconds slower than OpenGL. The
histogram is zeroed once from the CPU when the buffer is created, and *that
zeroing is not ordered against the first GPU read*: OpenGL landed it first,
Vulkan did not, so frame 0 metered uninitialised memory, adopted it, and then
smoothed away from it.

Three things about it are worth remembering:

- **It was reproducible**, so it read as a deliberately slow adaptation rather
  than as a bug. Determinism is not correctness.
- It only appeared on one backend, and *only because* the check compares both.
  A single-backend suite would have shipped it.
- What settled it was **forcing the smoothing off entirely**: both backends
  then agreed exactly, which proved the metering, the histogram and the
  barriers were all correct and localised the fault to what the first frame
  trusted. Removing the variable is faster than reasoning about it.

The fix is that a measurement is trusted on the *second* dispatch -- the first
frame whose histogram a previous frame's reduce pass has cleaned up after.

Also found by reading rather than by failing: `SHADER_READ_ONLY_OPTIMAL` mapped
to the fragment stage only, so any compute read of a graph target would have
been under-synchronised in both directions -- correct on one driver and wrong
on another. Widened before it could bite.

---

**Done: the grain rewrite (2026-08-15).** Reported as "a bit unnatural, maybe
Perlin noise would help". The instinct was right about the biggest of **three**
defects, only one of which was the noise function:

| | Was | Is |
|---|---|---|
| Shape | `hash(floor(coord / size))` — a grid of squares | two octaves of value noise |
| Response | `1.0 - luma * 0.5` — **loudest at black** | `sqrt(4L(1-L))`, peaking in the midtones |
| Colour | one value for all three channels | finest octave per channel |

The response curve is the one to remember. Its comment said it kept the
shadows clean and the arithmetic did the opposite, and noisy shadows are
exactly what makes noise read as a broken sensor. **Nothing in the suite could
have caught it:** every screenshot check builds on the AA scene, which is two
flat levels on purpose, and two levels cannot show that an effect varies with
brightness. The fix was a scene with a real tone ramp — eight emissive slabs
whose rendered luma spans 0.05 to 1.00, emissive values chosen by inverting
ACES rather than spaced evenly, because evenly spaced emitters all land in the
top half.

Not gradient (Perlin) noise, deliberately: it is exactly zero at every lattice
point, so a field of it carries a regular grid of grain-free dots — the same
artefact, rotated 45 degrees. ENGINE-NOTES 7x.

Two things that cost time and were not the feature: **`shared` and `common`
are both reserved words in GLSL**, and glslang names the line *after* the
offending one. And **a tonemap that fails to compile makes "off is off to the
byte" pass** — every variant is equally broken. What caught it was the other
half of that pair, "each effect does something", which is the third instance
of the same pattern now:

| Pair | What passes one half alone |
|---|---|
| off is exact / each effect lands | a shader that does not compile |
| animates / reproduces | a clock; a constant pattern |
| adjacent pixels differ / neighbours correlate | white noise; a blur |

**Done: 9.3, lens and film (2026-08-15).** Vignette, chromatic aberration and
film grain, all five knobs on the `.rvpostprofile`, all three off by default
and off *exactly* -- the shader branches past each rather than computing a
no-op, so every recorded threshold in the repository stays valid.
`check_lens_effects.py` asserts six properties (four here, two added by the
grain rewrite above) and every interesting one was confirmed able to fail.
ENGINE-NOTES 7w.

Worth carrying forward: the first attempt to falsify the grain-determinism
check used `rand()` without `srand()`, which is identical across runs and so
*passed*. A determinism check sabotaged with a deterministic mistake looks
green. Use a clock.

**Done: the `.rvlut` recipe (2026-08-15).** A look can now be authored in the
editor rather than only imported. `.cube` stays read-only baked data, `.rvlut`
is a recipe that bakes into a table at load, both are `AssetType::ColorLut`.
ENGINE-NOTES 7v.

Worth carrying forward: the identity check for it *nearly did not work*. At
the default table size 33 the step is 1/32, every coordinate is dyadic, and
the arithmetic is bit-exact by luck -- it passed with the guarantee removed.
It fails at 20 and 64. The same lesson as 7r's threshold and 7t's half-texel,
now three times over: **test a property where it can break, not where it
happens to hold.**

**Closed: the render setting that changed itself. It was an accidental drag,
and the bug was that nothing said so.**

Twice a field in `SampleProject.rvproject` was found on a value nobody typed:
`TemporalFeedback: 0` once, `0.98` the next time. Both are *exact clamp
bounds* of that field's slider -- the minimum and the maximum -- which is
what an overshooting drag leaves behind and not what a random value looks
like.

**Twenty unattended editor runs wrote nothing at all.** So the panel does not
write on its own; it takes a real gesture, and the two gestures were not
deliberate ones. Both writes happened while editor windows were being opened
and closed repeatedly in front of a person -- a window taking focus under a
moving cursor is all it takes, and the control spans the full width of its
row.

The damage was not the drag. It was that **the write was instant, permanent
and completely invisible**:

- The Render Settings panel saves the `.rvproject` the moment a drag ends --
  correct for a preference, and there is no Ctrl+S to reconsider during.
- The scene's unsaved dot deliberately ignores project edits, because they
  are already on disk and marking them "unsaved" would be a lie.
- The Build Log panel carries module builds, not the engine log, so
  `RV_INFO` reaches the console and nothing a person is looking at.

Three correct decisions that together mean a project file can change under
you with no trace. What was missing was the one that says so.

So the panel now names what it wrote, in the menu bar beside the scene's
mark, for six seconds and fading over the last one -- *"Feedback 0.600000 ->
0.980000"* -- with a tooltip that says it is already saved and that Ctrl+Z
puts it back. Every field it writes also goes to the log by name and value.

**And the accident itself is now prevented, by removing its cause rather
than adding friction.** The press that gives a window focus should not also
operate whatever it lands on -- macOS swallows it for exactly this reason,
GLFW does not. So `Window::IsFocused()` was added to the platform layer, and
for 0.4 s after focus is regained the Render Settings rows are drawn against
a throwaway copy: a click-through moves a slider on screen and changes
nothing. The grace does not tick down while a button is held, so a sweep that
began in that window stays suppressed until it is released rather than going
live halfway through.

Scoped to that block deliberately. The scene's environment beneath it and the
entity inspector are undoable *and* announce themselves -- an accident there
lights the unsaved mark and waits for Ctrl+S, which is recoverable. This
block writes the project file, which is not.

Two things it does not cover, both judged acceptable: a deliberate click in
the first 0.4 s does nothing (that is the point, and it is the same trade
every OS makes), and an accidental drag by somebody already working in the
window is still possible -- the notice is the net for that one.

---

**Done: BUG 2, the ghost over the Game view (2026-08-15).** `Renderer3D` kept
one previous view-projection for the whole process, so the editor's game
chain -- drawn second -- differenced itself against the *editor* camera and
TAA fetched its history from that gap. Fixed by giving the matrix the scope
the history already has: `CameraMotion`, owned by `TemporalHistory`. Design,
the measurement, and why no existing check could catch it: ENGINE-NOTES 7u.

Verified:
- Two editor runs differing only in the Viewport's size, which changes the
  editor camera's projection and nothing else: the Game panel is now
  **byte-identical**, 0 differing subpixels of 267,575, against max 87/255
  over 7,534 before. The game view no longer answers to the editor camera.
- `check_smaa.py`: TAA gains 1.64 / 1.44 / 1.47 / 1.64 at 8/43/47/77 deg --
  **the same figures recorded before the change**, so the single-chain
  behaviour is preserved to the digit.
- `check_taa_motion.py`: still 3.1x better than no filter, moving 1.01-1.02x,
  both backends. `check_taa_jitter.py`, `check_color_grading.py` pass.
- `scenetest --validation=on`: 1415 pass, 0 `FAIL`, exit 0, no `[Vulkan]`
  lines. Editor and runtime on both backends, same. Debug, Release and Dist
  all build.

**Done: BUG 1, the post profile and LUT lost on restart (2026-08-15).** One
gesture on two persistence schedules, with nothing saying so. See the commit
and ENGINE-NOTES 7s.

---

### The original BUG 2 report, and what was ruled out getting here

**BUG 1 is fixed (2026-08-14).** Kept below because the lead recorded for it
was *wrong*, and the correction is the useful part.

---

**BUG 1 — a post profile and its LUT gone after an editor restart. FIXED.**

**The recorded lead was wrong.** It said `grade_none.rvpostprofile` had
acquired `swap.cube`'s handle and that "something is saving to the wrong
file". Nothing was. The owner picked *that* profile in the camera's dropdown
and applied *that* LUT to it; the engine wrote exactly what it was told, to
exactly the right file. The generated profile carrying a hand-applied LUT was
the evidence that saving **worked**, read as evidence that it was broken.

The actual cause is the one line already written underneath it and dismissed
as a "design wart": **one gesture, two persistence schedules.** The LUT is
part of the profile asset and writes itself on the edit. The camera's
reference to that profile is scene data and waits for Ctrl+S. On restart the
camera had no profile, so the grade went with it, and both looked lost. The
scene file `demo.rage` was never modified -- `git status` said so all along.

Nothing told anyone a save was owed. The unsaved-changes dot asked
`CommandStack::CanUndo()`, true from the first edit until the scene closes,
so it was lit through every save and meant nothing; and closing the window
discarded the scene silently.

What landed: a real dirty *position* on `CommandStack`,
`EditorCommand::TouchesScene()` so the self-saving render settings do not
count, and one Save/Discard/Cancel prompt on closing, New Scene and Open
Scene. `WindowCloseEvent` moved to after the layer walk in
`Application::OnEvent` -- `Dispatch` marks the event handled from its
callback's return value, so a layer could never have vetoed it. Design and
reasoning in ENGINE-NOTES 7s; `CheckSceneDirty` in scenetest pins the cases a
boolean gets wrong.

---

**BUG 2 — a faint second copy of the whole scene over the Game view. OPEN.**

The owner's description, tightened over three messages:

- A **faint second copy of the whole scene, overlaid** -- not a reflection on
  a surface, and not one stray object.
- **Game view only.** The Viewport panel is clean.
- The **scroll wheel zooms it**, which is the editor camera's control. So the
  ghost is the *editor viewport's* image reaching the Game panel, live.
- After switching to the Viewport tab and back: **the ghost persists but no
  longer responds to scroll.** It freezes.

**It does not reproduce headlessly, and that is a finding.** Everything below
was run on OpenGL Release against `SampleProject/demo.rage`, `--screenshot`
at frames 5 and 40, both chains rendering:

- Game and Viewport as tabs (only one chain runs): clean.
- Game floating, Viewport docked (both chains, different sizes): clean.
- Both floating at **identical** sizes (both chains, same target shapes,
  which was the best guess for pooled-target aliasing): clean. The Viewport
  draws the grid and the Game view shows no trace of it, which a blended
  copy of the viewport could not hide.
- Two runs differing **only** in the Viewport window's size, `--frame-time`
  fixed, diffing the Game panel's pixels: max 87/255 over 2.7% of the panel,
  and the difference image is edges and highlights only -- TAA jitter phase,
  not a second picture.
- The runtime renders the same frame, so nothing editor-specific is needed
  to see what the game camera sees.

Also checked and **not** it: the pale rectangle near the RageV title is
Falling Box 1 hanging at y=4 because physics does not run in Edit mode --
correct behaviour, and it is in the runtime's frame too.

The missing ingredient is almost certainly **a moved editor camera**. At its
default the editor camera sits very close to the scene camera, so a blended
copy of the viewport would be nearly invisible in a still; the owner sees it
after scrolling, which is also when they can make it move. Nothing here can
scroll.

So the next step is not more static captures. Either drive the editor camera
from a flag (there is no way to set it today -- `panels.ini` does not persist
it), or give the editor a screenshot hotkey so the owner can capture the
ghost in the act and hand over a real frame. The second is a few lines and
unblocks the first properly.

Still-unexcluded suspects, in order: the reflection probe capture (Realtime
probes re-capture every frame, which fits "live then frozen" if capturing
stops); `PostProcess`'s pooled descriptor sets, whose cursor is reset once
per frame by `Renderer::BeginFrame` while two chains consume from it; and
the bloom chain's additive upsample accumulating onto a pooled level that
was not written first this frame.

---

**Done: 9.1, colour grading (2026-08-14).** A `.cube` lookup table on the post
profile's Colour LUT row, applied **after the tone curve** — which is where a
LUT exported from Resolve or Photoshop expects to be, so one does here what it
did there. The cost of that choice is stated rather than hidden: a LUT at that
point cannot recover highlight detail ACES has already compressed, and a
shaper-based path is a later addition rather than a correction of this one.

The prerequisite the roadmap named is in: `TextureType::Texture3D` on both
backends, with `TextureDesc::Depth` separate from `Layers` because **an
array's layers are not filtered across and a volume's depth is** — conflating
them gives a LUT with no interpolation along blue, which grades the picture
and grades it wrongly. 16-bit float entries, because 8-bit ones quantise
before the trilinear filter and band in the smooth gradients a grade is judged
on, and because linear filtering of a 32-bit float image is optional in Vulkan
and guaranteed for 16.

**The check is the point of the item.** A LUT is sampled at texel centres, so
a colour c maps to `(c*(N-1)+0.5)/N` and not to c. Get it wrong and the image
is still graded, still smooth, still plausible — just not the grade that was
authored, by a little, everywhere, with no artefact to notice. So
`check_color_grading.py` does not ask whether grading changes the picture. It
asserts that **an identity LUT is byte-identical to no LUT** — 0 differing
subpixels on both backends — and that a red/blue swap lands exactly on the
frame with its channels exchanged. Falsified rather than assumed: dropping the
half-texel term makes it fail on all 4,320,000 subpixels at 3/255, which is
precisely the "looks like nothing" magnitude the design predicted. ENGINE-NOTES
7t.

**Done: 9.0, where a setting lives (2026-08-14).** `SceneEnvironment` was one
struct holding three unrelated kinds of thing, serialized into every `.rage`.
It is three now, each in the file that owns it:

| Home | Holds | Edited in |
|---|---|---|
| `.rvproject` → `RenderSettings` | AA and its parameters, shadows | Render Settings → Render settings |
| `.rvpostprofile` → `PostSettings` | Exposure, bloom, and all of 9.1–9.7 | The **Camera component** that names it |
| `.rage` → `SceneEnvironment` | Ambient, sky | Render Settings → Environment |

**The owner revised the roadmap's design before it was built**, and both
changes removed work: no render profile asset (the project is the only layer
that was wanted), and the post profile is *attached to a camera* rather than
being a sparse override of project defaults — which deletes the per-field
"is this set?" bit, the inherited-versus-equal UI problem and the merge step
that §9.0 used to call the part to design for. A profile is optional; a
camera without one renders the neutral grade. The **post profile is on the
camera and nowhere else** — an earlier pass of this work also put it in the
Render Settings panel, which was wrong and is gone.

**All four formats are read and written through the registry**, by one pair of
functions in `Scene/FieldSerializer` (hoisted out of SceneSerializer, not
copied). That closes the drift `TemporalFeedback` shipped: registered,
inspectable, in nobody's serializer, resetting on every load. The checks
enforce the shape rather than a list — set every registered field to a
non-default, round-trip, compare — so a field added tomorrow is covered
without anyone choosing to cover it. `FindSetting` keeps one flat name space
for scripts over the three owners, and a check asserts no name is in two.

Scene version 6. A version-5 scene still carrying the moved keys is
**reported**, naming each and where it went, rather than migrated (a load
that edits the project file is hard to undo) or dropped (what
`TemporalFeedback` did). Only non-default values are named, so the generated
test scenes and `Knockdown/Main.rage` — which stores the whole block at its
defaults — load silently.

**The move changed no pixels, and that is measured:** `check_taa_motion.py`
reproduces its recorded numbers to three decimals through the new path, which
required `make_aa_scene.py` and `make_motion_scene.py` to write a real
`.rvpostprofile` with its own `.meta` and point the camera at it — their
`BloomEnabled: false` had become an inert key, and the checks would have gone
on printing numbers for a picture they were not designed for.

**It also surfaced a stale threshold nobody had re-run.** `check_smaa.py`
required TAA to beat no filter by 1.5x on a static scene, measured when the
feedback default was 0.9; 7.10 moved it to 0.6 and re-calibrated the *motion*
check only, so this one has been failing at two of four angles ever since on
a build working exactly as intended. Re-measured at 0.0, 0.6 and 0.9 and set
to 1.25 — clear at the shipped default, failing with accumulation off. Design
and full writeup: ENGINE-NOTES 7s.

**Three editor papercuts fixed alongside it (2026-08-14).** Ctrl+S opened a
Save-As dialog every time: `SaveScene` always prompted and never read
`m_ScenePath`, which was already tracked for the title bar and Set Start
Scene. It writes the file the scene came from now and only asks when there is
none; Ctrl+Shift+S is Save As. The window size is
written to `ragev.ini` on exit, through the same `width`/`height` keys
`--width`/`--height` already read, so the editor comes back the size it was
left at. And both modals — About and the backend-restart prompt — set their
position every frame rather than only on `Appearing`, which is why the
restart dialog stayed at the old centre's coordinates after a resize.

**Done: the tangent-frame backend parity bug (2026-08-13).** The suspect was
right and the planned fix was wrong. Under Vulkan's negative-height viewport
`dFdy` is the negative of GL's, both terms of T and of B flip, and the frame
comes out **rotated 180 degrees about N** -- and a rotation preserves
handedness, so the planned `dot(cross(T, B), N)` correction reads identically
on both backends and could never have caught it. The discriminant that works
is the screen orientation `det(dp1, dp2, N) = dot(dp1, cross(dp2, N))`;
`TangentFrame` now divides its sign out. Verified with a stated answer, not a
backend diff: `make_parity_fixture.py` writes a scene whose truth is known
(tilted normal map beside a flat control; `check_tangent_frame.py` gives the
verdict) -- before the fix GL read CORRECT and VK FLIPPED, after it both read
CORRECT, the backends are bit-identical on the fixture, and GL is
bit-identical to its pre-fix self. material_closeup dropped 14.7 -> 1.28/255;
the residual decomposes into driver LOD rounding at minification (0.79, bands
in the heatmap, shadows ablated and exonerated) plus parallax-displaced UVs
under implicit-derivative fetches -- nothing directional. ibl_check and
irradiance_uniform: 55 and 0 differing subpixels of 1.5M. 1160/1160 scenetest
checks on both backends, validation clean. The instrument stays in the tree;
any future derivative-convention doubt is one render plus one script away.
Full writeup: ENGINE-NOTES 7g.

**Done: 7.1, the pak and the VFS (2026-08-13).** A mount shadows a directory
-- the VFS answers the same paths the filesystem would -- so a development
run mounts nothing and costs nothing, and a shipped game has `content.pak`
where `content/` used to be (`rvpack --loose` keeps the folder form for
debugging). Every content reader routes through it: textures from memory,
the YAML serializers, cgltf via file callbacks, the registry scan (which
trusts pak entries and never re-hashes or rewrites them), and audio through
a ma_vfs bridge, streaming from the archive on its own thread. Proven by
pixel diff: Knockdown packaged pak and loose renders identically to the
run-to-run noise floor (12 subpixels) on both backends -- and getting there
caught a real bug, an existence check that had not gone through the VFS and
silently cost the packaged HUD its font. When a subsystem grows a routing
layer, grep for the existence checks, not just the reads: a missed read
fails loudly, a missed existence check quietly picks the fallback. Design
and full writeup: ENGINE-NOTES 7h.

**Done: 7.2, asset cooking (2026-08-13).** A content transform inside the
pak build: textures become `.rvtex` (pre-decoded, pre-mipped,
block-compressed via the vendored stb_dxt) and models become `.rvmesh` (the
import, serialized), each under the source's own path, with loaders sniffing
the magic -- so nothing that references them can tell, and a development run
never sees anything but source files. The RHI grew BC1/BC3/BC4/BC5 and
`UploadMip`; `PerturbNormal` reconstructs Z unconditionally so PNG and BC5
normals share one path. Measured on the 4K set (Release): material_closeup
drops 3.7s to 2.0s wall, VRAM arithmetic goes ~0.9 GB to ~0.15, the visual
delta is mean 0.89/255 -- under the cross-backend residual that already
exists -- and the cooked *mesh* path is pixel-exact. `rvpack --raw` ships
source bytes; `--loose` still ships the folder. One design correction made
out loud: content alone cannot pick BC4 -- "grey and opaque" describes a
smoke sprite as well as a roughness map -- so the encode keys on the
data-map name suffixes every pipeline here already uses. Design and numbers:
ENGINE-NOTES 7i.

**Done: 7.4, viewport gizmo icons (2026-08-13).** Lights, cameras, probes and
audio sources have no mesh and no collider, so `PickEntity` could not see
them and the hierarchy panel was the only way to select one. They now carry a
billboarded mark, drawn through `UIRenderer`'s existing world layer -- no new
pipeline, and editor-only the way the grid is, by being a pointer
`OnRenderRuntime` has nowhere to pass. The drawer and the picker share one
`EditorIcons::Collect` and one `EditorIcons::Radius`, so a mark's hit area
cannot drift from where it is drawn; constant *angular* size is what makes
that sharing possible, because the picker can size the mark from the ray's
origin alone. Marks compete with geometry by distance, so a wall in front of
a light wins -- which is what the depth-tested picture already says. Nine
scenetest checks, including the negative one that names the defect: without
the marks, a light cannot be clicked at all.

Also fixed in passing: the toolbar's ground-grid mark, reported twice as
"uneven". The geometry was symmetric all along -- the outline went through
ImGui's polyline rasterizer and the interior through its line rasterizer, and
those resolve about axes half a pixel apart. Measured against its own mirror:
9.4/255 mean before, 0.7 after. Two rounds were spent redrawing correct
geometry before measuring it; ENGINE-NOTES §7j has the lesson.

**Done: 7.5 and 7.6, the two debts skeletal animation left (2026-08-13).**
Both were the same shape -- machinery that existed, was tested, and was never
called. `BlendPoses` now runs because the animator notices `Clip` changing;
there is no Play call, so the inspector, a script and a loaded scene all get
the same easing, and the outgoing clip keeps running while it fades rather
than freezing mid-stride. Skinned bounds are a box per bone in bone space
from one vertex pass, unioned over sampled poses of every clip -- conservative
by construction, since a skinned position is a weighted average of the same
vertex placed by each of its bones.

**Both are checked against somebody else's rig.** `models/fox.glb` (Khronos'
sample fox, CC BY 4.0, attribution beside it) brings 24 bones and three
clips, and a fixture with one clip can only ever exercise the transition to
the bind pose. The blend check drives the real `UpdateAnimators` and pins the
mid-fade pose against what each clip alone would say, with a zero-blend
control proving the easing is the blend rather than the clips differing.
`scenes/fox.rage` runs it. ENGINE-NOTES §7k.

**Animation is deliberately unfinished, and will be revisited.** Two changes
landed at the owner's direction after 7.5/7.6: animators no longer run in
edit mode unless `RunInEditor` is ticked (off by default -- animation belongs
to the running game, and an editor full of mid-stride characters has nothing
standing still to build against), and a clip is chosen from a searchable
dropdown of the model's own clip names rather than typed as an index.

**When it is picked up again, settle the scene format first.** `Clip` is
stored as an *index*, so re-exporting a model with a clip inserted silently
repoints every scene that used it; storing the name is the robust choice and
is a file-format decision, which is the kind that gets more expensive the
longer it waits. Also absent, in rough order of how much each is missed: a
state machine with per-transition blend times (the blend length is currently
one number per animator), animation events, root motion, layers and masks,
and scrubbing in the preview. ENGINE-NOTES §7k has the reasoning behind what
is there.

**START HERE: the rest of phase 7** -- SMAA (7.9) and TAA (7.10). 7.8 is
done. Phase 7 is otherwise complete, and **Phase 9 (post processing) was
added 2026-08-13** at the owner's direction: seven items hanging off the
`PostProcess` pass that already exists, with colour grading (9.1) the best
value and the only one needing new RHI (a 3D texture).

**7.8 depth sorting is done, and the measurement rewrote the design twice.**
A *global* depth sort loses -- it dissolves the batching (0.567ms vs 0.548 on
1500 meshes). Sorting only the *batches* does nothing where it matters -- 200
slabs sharing one mesh are a single batch. Sorting **inside each run, then the
runs** wins: 0.32ms vs 33.9ms on a pure-overdraw scene (~100x), 0.546 vs 0.607
on the stress scene with a tenth the variance. Pixel-identical and
draw-count-identical on both backends. `--depth-sort=off` +
`check_depth_sort.py`. ENGINE-NOTES §7m.

**A build trap that cost an hour and will again: each executable's directory
holds its own staged copy of `RageV.dll`, and `--target RageV` does not
refresh them.** Only building the application target does. Three "different"
runs were the same binary, and it read as a genuine null result. The tell is
that the build output never names the .cpp you edited.

**Startup was rebuilt (L, 2026-08-13; ENGINE-NOTES §7l).** The complaint
was "Not Responding on launch"; the measurement said `Project::Load` was
0.18 s of a 4.79 s boot and 3.2 s was decoding 198 MB of PNG on the first
*draw*. Three changes, in the order they matter:

- **An import cache.** The 7.2 cookers had one caller -- the packager --
  so the editor decoded everything on every launch. They now run on
  import too, into `<project>/Cache`, keyed on the `.meta` `SourceHash`
  with the hash in the filename (existence is validity). **Git-ignored,
  here and in generated projects.** Editor 4.79 s → 2.70 s warm; runtime
  7.50 s → 3.38 s. First open costs 17 s to cook.
- **Loading on a worker, with a loading screen.** The window is created
  hidden and shown when the renderer can draw in it. `Layer::OnLoad`
  runs on a worker (files and CPU, **never the device**);
  `Layer::OnLoadStep` uploads on the main thread a slice per frame.
- **`Manager::PrepareScene`** walks the scene so the first frame has
  nothing left to fetch, weighted by bytes, cooking on **four** workers
  -- capped by memory, not cores: one 4K cook holds a 256 MB float mip
  0, so one per hardware thread would ask for gigabytes.

Cold open **17.2 s → 4.9 s**, drawing 249 frames throughout; warm 0.62 s.
The cooked bytes are byte-for-byte what the serial version produced --
the pixel check reports the same 0.540/255 to three decimals.

New flags: `--import-cache=on|off` (the ablation the pixel check needs)
and `--loading-screenshot=<file>`. New check:
`tools/scripts/check_import_cache.py`.

**The cooker got faster, bit-identically** (ENGINE-NOTES §7l): `rvpack`
**12.84s -> 9.70s**, editor first open **3.90s -> 3.47s**, with the
project's pak hashing the same before and after. `std::pow` and
`std::lround` in the per-texel loops are gone, replaced by a 4096-entry
**direct-indexed** table plus one branchless correction. `CheckSrgbEncode`
sweeps 221,800 values against `pow`-and-round; falsify it by deleting
the correction.

Three traps from it, all recorded in §7l: **profile in the power state
you care about** (the profile that started this was taken on battery and
pointed at the wrong work); **a lookup table is not automatically faster
than the maths** (the first version used a binary search and measured as
zero -- eight unpredictable branches cost what a `pow` costs); and
**building a threshold table by inverting the transfer is wrong in
floating point** (2 bytes differed in 190 MB -- bisect against the
reference instead). Parallelism *inside* one asset was built, measured
at zero on mains, and removed.

**Still open**: `.hdr` skies are converted to cube faces and convolved
on every load -- cooking those is a third format and its own feature.
`Registry::Init` still hashes every loose asset at every boot (FNV-1a,
a byte at a time, 198 MB here); it is on the worker and has a progress
phase, but it stays O(project) per launch and is the next thing to
hurt.

The user's 4K Poly Haven materials are committed at full resolution (their
call, 2026-08-13; ~198 MB -- forest_ground_06 as soil, bamboo_wall_02 as
wood, metal_plate_02 as rusted steel, brick still ambientCG). Still pending
their decision: `demo.rage`'s uncommitted state has the sun removed and the
HDR sky per their experiment -- the committed demo still has sun + gradient.

---

**Phases 0-5 are done, the game-module arc is done, and as of interop
protocol 4 the two script languages are equals.** C# reaches everything C++
reaches: audio, raycasts, hierarchy, LookAt, SpawnPrefab by asset path, and
components by registry name with text values -- the boundary's answer to
GetComponent<T>, driven by the same ComponentRegistry as the inspector so it
cannot go stale. Building mid-play stops the scene, swaps both languages'
scripts, and resumes Play on the new code; validation layers are opt-in
everywhere (~1.5 ms/frame of CPU when on, Vulkan only -- the manual's
getting-started page has the section).

### Done - the mini game (Knockdown/)

The game exists, plays, and ships. **Knockdown**: you stand at a launcher
(A/D swing it, W/S tilt the muzzle, Space or left-click fires), lob heavy
balls at a pyramid of six crates on a platform, and knock them all onto the
ground. The beacon beside the platform walks from red to green as crates
land in the out-zone trigger; the last one plays a chime and doubles the
light. F resets the round by respawning the crate stack prefab. No text, no
UI -- the light, the sounds and the crates are the whole scoreboard.

It deliberately spans both languages: C++ (`Source/Launcher.cpp`,
`Ball.cpp`) does input, raycast aiming (the emissive AimDot is the sight),
prefab spawning and launch velocity; C# (`Scripts/GameManager.cs`,
`Crate.cs`, `Ambience.cs`) does trigger counting, the component-bridge
light changes, impact/win/ambience audio by asset path, and the reset. The
sounds are synthesised by `tools/scripts/make_game_audio.py` -- same
convention as the sky generator.

Verified end to end on both backends with validation on: the full autoplay
round (crates scattered, beacon green, win logged), a mid-play Ctrl+B that
stopped the scene, rebuilt both languages and resumed Play, and File >
Build Game producing a folder that runs standalone -- including booting
.NET from its own managed/ and running the C# manager.

**The exercise worked: it found five real engine holes in one session,**
each now fixed and covered by a scenetest check:

1. `File > New Project` was implemented but never put in the menu.
2. A rigid body gained during play never joined the simulation -- AddBody
   existed and had no callers. The first fired ball hung in the air.
   (Reconciled at the top of the fixed step, skipping condemned entities.)
3. C# collision/trigger callbacks were never delivered from a real
   simulation: DeliverContact resolved only the native component. The
   whole managed contact surface was written, bound, documented, tested at
   the table -- and unreachable. (`RageV.Builtin.ContactCounter` is the
   worked example and the regression probe.)
4. Nothing outside scenetest ever booted the .NET host or loaded a
   project's built assembly: in a fresh editor, C# was dead until a manual
   build, and a packaged game had no C# at all. (Project::Load now loads
   scripts for editor, runtime and packaged game by the same line, and the
   packager ships managed/.)
5. Editor C# builds always failed: the ScriptCore reference was passed as
   a CWD-relative path, which MSBuild resolves against the .csproj's
   directory. (Absolute now.)

Smaller finds: creating a project left the previous module loaded (fixed),
the C# manual's prefab example used `.prefab` for `.rprefab` (fixed), and
the content browser draws folder icons as `[/]` tofu (open, cosmetic).

**Sixth find, fixed the following session:** destroying a
material-bearing entity mid-play could free its descriptor sets while the
previous frame still executed. Entity destruction happens in the
simulation phase, *before* BeginFrame -- and a deleter pushed there landed
in the slot BeginFrame was about to flush that same iteration, behind a
fence that only covers a frame from two submissions ago. Deferred by zero
frames, in every process; the GPU usually won the race, which is why it
read as a rare runtime flake. DeletionQueue::Push now slots out-of-frame
pushes by the most recently *submitted* frame (see the comment in
VulkanCommon.h). Proven with a churn scene -- ~180 material destroys a
second under GPU load: 10 validation errors in 4000 frames before, zero
in 12000 after.

### In flight - phase 6 particles (user picked; UI/text deferred)

The user chose particles (2D + 3D, with a per-emitter GPU option like
Unreal's) plus the small audio wants. **Landed and verified this session,
Debug config, 802 checks, both backends, screenshots compared:**

- **Audio, protocol 5**: pitch on every one-shot and `PlayOneShotAt` (an
  arbitrary point), C++ and C# equal. Three appended table entries --
  `PlayOneShotAt`, `PlayOneShotPitched`, `PlayOneShot2DPitched`; the old
  two entries keep their exact shape, unused.
- **CPU particles (6.5/6.6)**: `ParticleEmitterComponent` (rate, burst,
  cone, gravity, drag, size/colour over life, spin, Facing
  Billboard|Flat, Blend Alpha|Additive, Space World|Local, optional
  sprite, `SimulateOnGpu` authored now so scenes never migrate),
  deterministic xorshift sim in `Particles/ParticleSystem.cpp` (per
  FRAME, not fixed step -- particles are presentation), instanced
  renderer in `Renderer/ParticleRenderer.cpp` + `particle.rvshader`
  (storage-buffer instances, base-instance push constant, alpha sorted
  twice / additive unsorted, depth test on write off), registry-driven
  inspector + serialization, `Assets::Manager::GetTexture` (new -- there
  was no plain 2D texture-by-handle loader at all).
  `SampleProject/assets/scenes/particles.rage` is the visual check: fire
  (additive), smoke (alpha, sorted), 2D fountain (flat), run via
  `RageVRuntime --project=SampleProject --scene=scenes/particles.rage`.

**Trap found while building the emitter, and since closed:** the
component registry's enum contract is int-sized, and nothing said so.
`Field<>` now static_asserts it (§5 has the invariant), so the mistake is
a build error naming the offending enum rather than a silent stomp. An
audit found no existing offender -- every registered enum was already
`uint32_t` or plain `int` -- so this was a live trap, not a live bug.

**6.7a RHI compute is done** (819 checks, both backends, 0 validation
messages). What landed:

- `ComputePipelineDesc` / `RHIComputePipeline` as a sibling of
  `RHIPipeline`, not a graphics desc with the graphics half defaulted.
  `GetWorkGroupSizeX()` and `GroupsFor(n)` come off reflection, so a
  resized `local_size_x` cannot leave a dispatch covering a fraction of
  its data.
- `ShaderReflection` gained `LocalSize[3]` and `Stages`. `Stages` is
  what answers "does this file have a compute stage" -- `LocalSize`
  cannot, because a legal `local_size_x = 1` is indistinguishable from
  the default.
- `BindComputePipeline`, `Dispatch`, `BufferBarrier(buffer, from, to)`
  with a `BufferSync` vocabulary of *uses* rather than Vulkan's
  access/stage pair (the only honest shape both backends implement).
  Both command lists follow whichever pipeline kind was bound last, so
  resource sets and push constants need no second statement of it.
- `RHIDevice::ExecuteImmediate(record)` -- a one-shot command list,
  submitted and waited on, outside the frame loop. Vulkan wraps its
  existing ImmediateSubmit (via a new `VulkanCommandList::Adopt`, since
  Begin/End belong to a frame); GL issues and `glFinish`es. It is what
  makes a dispatch testable headlessly, and it is generally useful.
- Resource sets no longer couple to a pipeline *kind*: both backends
  take the layout-bearing half plus an owning handle
  (`VulkanPipelineCommon`, `OpenGLPipelineBindings`).
- Caps: `SupportsCompute`, `MaxComputeWorkGroupSize/Count`.

The suite runs a real dispatch -- 100 elements over a 64-wide group, so
the tail group runs past the end and the shader's bounds check is
exercised -- and checks every element. A graphics-only shader is refused
a compute pipeline rather than given one that dispatches nothing.

**6.7b GPU particle simulation is done** (825 checks, both backends, 0
validation messages). `SimulateOnGpu` on an emitter now means it:
a fixed pool in a device-local SSBO, one compute dispatch per emitter
integrating and spawning, writing the *same instance layout* the CPU
renderer fills -- so both paths reach one vertex shader and "they look
the same" is structural rather than a promise.

Measured, Release, vsync off, ~16k particles across three emitters:

| | CPU sim | GPU sim |
|---|---|---|
| Vulkan | 3.30 ms | **1.37 ms** |
| OpenGL | 3.80 ms | **1.58 ms** |

Both 2.4x, and both GPU-bound afterwards (frame time ≈ GPU work), where
the CPU path had ~2 ms of CPU on top.

**The switch costs nothing**, as asked: the compute pipeline is built
once at Init, and an emitter's buffers are cached by UUID and kept while
the emitter exists -- not while it is *using* them. Toggling is a
branch. A suite check asserts the buffer pointer is identical across
off-and-back-on, so a future refactor that starts freeing on toggle
fails rather than just getting slower.

CPU→GPU seeds the pool from the live CPU particles, so the switch is
visually continuous. GPU→CPU is not: reading the pool back would stall,
which is the one thing the GPU path exists to avoid, so it drops what is
in flight and refills. Documented, not hidden.

**Two traps this cost, both worth knowing:**
- **A resource set holds one descriptor set per frame in flight, and
  Commit writes only the current frame's.** Committing once at creation
  populates one and leaves the rest never written -- validation catches
  it on frame two, a release build renders garbage. Rebind and commit
  every frame, which is what ParticleRenderer already did.
- **A persistently-mapped host-visible buffer written by a compute
  shader is a synchronisation point on OpenGL.** The state buffer was
  host-visible so the rare seed could be a memcpy; it made the GPU path
  *slower than the CPU one* (6.9 ms vs 3.3 ms) and stopped GPU
  timestamps resolving at all. Device-local, seed via staging.
  Barriers also bracket the whole batch rather than each dispatch --
  interleaved, every emitter waits for the one before it.

Local-space GPU emitters work: the pool simulates in the emitter's own
frame and the model matrix is applied when the instance is written, so
particles ride a moving emitter instead of being dragged by it -- the
same split the CPU renderer uses, where the transform belongs to
presentation rather than to integration.

Still open in the GPU path: alpha particles are not depth-sorted *within*
an emitter, because sorting would need a readback. Emitters are still
sorted against each other, so two overlapping effects are ordered
correctly. The artifact only shows when particles inside one emitter
differ strongly in colour or opacity; uniform smoke looks identical
sorted or not. `scenes/particles_gpu.rage` is the visual check.

### Done - 6.8, weighted-blended transparency

`ParticleBlend::WeightedBlended` is a third option beside Alpha and
Additive: alpha that needs no sort. Fragments accumulate into two
attachments -- a sum and a product, both order-independent -- and a
resolve pass works out the answer, so a thousand overlapping particles
land in any order and look the same. It is what a GPU emitter of smoke
wants, since sorting its pool would mean a readback.

`scenes/particles_oit.rage` is the check; both backends composite it with
no validation messages, 888 checks.

**It costs nothing when nothing uses it.** The two attachments and the
resolve pass only exist in frames whose scene contains a weighted
emitter, asked of the *scene* before the graph is described -- asking the
renderer would answer for the previous frame, which is a one-frame lag
and a dropped first frame.

**Four things this needed, in order, each committed separately:**
per-attachment blend state and the two weighted equations; multi-colour
graph targets with per-pass attachment subsets; `PreserveDepth`, so the
transparent pass clears its own attachments while keeping the depth the
scene wrote; and the emitter plumbing.

**Traps paid for along the way:**
- **`independentBlend` is a device feature.** Different blend state on
  different attachments is not free in Vulkan; without it every
  attachment must match the first. Validation said so plainly on the
  first run. Enabled where supported, and pipelines fall back to
  attachment zero's preset with a warning naming the pipeline when it is
  not -- wrong picture, not a crash.
- **OpenGL's `glDrawBuffers` is persistent framebuffer state.** It is set
  unconditionally now, because a pass that bound a subset would otherwise
  leave the next pass over that target writing into its selection.
- **`glClearBufferfv` indexes the draw-buffer list, not the
  framebuffer.** Selecting attachments 1 and 2 means clearing indices 0
  and 1.

The depth weight is the one thing left unproven; it is the START HERE
below.

### Superseded - the plan for 6.8

A third `ParticleBlend` beside Alpha and Additive, so an emitter can opt
into order-independent transparency instead of living with unsorted
alpha. It also fixes something the CPU path only approximates: emitters
are currently ordered by sorting their origins, which is wrong whenever
two of them interpenetrate.

**Piece 1 of 3 is done and committed** (`bcd34e7`): pipelines carry
per-attachment blend state, and `WeightedAccumulate` /
`WeightedRevealage` exist on both backends. `BlendPerAttachment` is
empty everywhere else, so nothing that predates it changed -- verified
by screenshot on both backends.

**What is left, in order:**

0. **The render graph has to grow two things first.** This was missed in
   the first estimate and is the bulk of the remaining work -- the
   particle side is small once it exists.

   `RGTargetDesc` carries exactly one colour format, and
   `RGPassBuilder::Write` binds a whole target; weighted blending needs
   two colour attachments written by one draw. And a graph target owns
   its own depth, so a separate transparent target would get a freshly
   cleared one and its particles would draw through walls.

   The answer follows from dynamic rendering: there is no VkFramebuffer,
   so every pass names its own attachments. One target can carry HDR
   colour, accumulation and revealage over a single depth image, and a
   *pass* binds a subset of it -- the scene pass attachment 0, the OIT
   pass attachments 1 and 2, both depth-testing against the same image.
   That needs:
   - `RGTargetDesc`: extra colour formats beyond `Color`, appended to
     the `RHI::RenderTargetDesc::ColorAttachments` vector that already
     exists and is already a vector. `SameShape` has to compare them or
     the pool will hand back a target of the wrong shape.
   - `RGPassBuilder::Write`: which attachment indices this pass binds,
     defaulting to all of them so no existing pass changes.
   - `Sample`: which attachment to read, for the resolve.

   The pipeline's declared `ColorFormats` must match the attachments the
   pass binds -- that is why the subset matters rather than just adding
   attachments to the scene target: every existing scene pipeline
   declares one colour format and would otherwise have to declare three.

1. **Then the transparent pass.** `FrameGraphBuilder::BuildFrame` is the
   extension point -- the scene render is already a `DrawScene` callback
   inside a graph pass, and `DrawOverlay` shows the pattern. Add a
   `DrawTransparent` callback writing attachments 1 and 2, accumulation
   `R16G16B16A16_SFLOAT` and revealage `R8`, clearing accumulation to
   zero and revealage to one. Depth test on, depth write off.
2. **A resolve pass**: fullscreen triangle compositing
   `accum.rgb / max(accum.a, epsilon)` over the scene colour, weighted by
   `1 - revealage`. Premultiplied-alpha blend against the HDR target.
3. **The plumbing**: the enum value, the registry dropdown, and
   `ParticleRenderer` splitting its pending draws so weighted emitters
   record into the transparent pass and the other two stay where they
   are. The compute path needs nothing -- the sim already writes a
   colour, and which pass consumes it is the renderer's business.

**Weights matter more than the plumbing.** The technique lives or dies
on the depth weight function; McGuire's paper gives several, and the
wrong one makes distant particles vanish or near ones dominate. Start
with the paper's equation 9 and *look at it* against the additive and
alpha versions of the same scene before believing it.

**Resume here:**
1. ~~6.7b GPU sim~~ -- done; the notes below are kept for context
1. **6.7b GPU sim**: per-emitter fixed pool of `MaxParticles` in a
   device-local SSBO, one compute pass integrates + emits (spawn count
   pushed per frame, hash(index,frame) RNG), dead particles collapse to
   size zero -- no compaction, no readback, no indirect draw in v1; the
   draw is the same `particle.rvshader` reading the GPU buffer (the
   instance layout matches on purpose). `SimulateOnGpu` currently parks
   the emitter entirely (CPU skips it, nothing draws) -- honest but
   dead; 6.7b is what makes the flag true. Then measure CPU vs GPU with
   `--benchmark` on a heavy emitter and record both numbers.

   **The switch must be ~instant** -- the user asked for this
   explicitly. The compute pipeline is created once at Init, and each
   emitter's GPU buffers are cached by entity UUID and *retained* when
   the flag goes off, so toggling is a branch rather than an
   allocation. CPU->GPU can seed the SSBO from the live pool for free
   and stay visually continuous; GPU->CPU drops the pool rather than
   stalling on a readback -- document that asymmetry rather than hiding
   it.

   The dispatch must be recorded **outside** the render pass, so the
   sim needs a hook before the scene's BeginRenderPass rather than
   inside Scene::OnRender. Both command lists assert on this.
2. ~~Docs~~ -- done. `particles.md` is a new manual page, the C++ and C#
   audio sections carry the pitch and `PlayOneShotAt` variants, and the
   site regenerates with 8 pages and a passing `--check`.
3. **Feedback for Knockdown** ("juice" -- the small sensory responses
   that make an action feel like it landed rather than merely
   registering: a puff on impact, a pitch-varied thud, a burst on the
   win). Concretely: an impact burst when a ball hits a crate and a
   confetti burst on the win, both fired from the C# scripts. It is not
   decoration -- it is the first end-to-end proof that a script can
   write `Burst` and see particles, which nothing currently tests
   outside the suite.
4. Consider a soft-dot sprite generator (tools/scripts convention) --
   untextured squares read fine but round sprites read better.

### Done - 6.9, the depth weight validated (and two bugs it found)

**The weight was wrong, and so was something bigger next to it.** Both were
found the same way: by building a scene whose correct answer is known, rather
than by looking at smoke and deciding it looked like smoke.

`tools/scripts/make_depth_scene.py` builds it, in two modes. `layers` is the
instrument: three emitters at 2, 20 and 200 units, one motionless particle
each, red near / green mid / blue far at alpha 0.5, every spatial quantity
scaled by distance so all three subtend the *same* screen angle. Each emitter
seeds the same RNG and draws from it identically, so all three take the same
random rotation and land perfectly concentric. It renders bit-identically run
to run, which makes pixel comparison meaningful. The right answer is
arithmetic -- 0.5 red over 0.5 green over 0.5 blue is 4:2:1, a red-dominant
pixel -- and sorted `Alpha` produces it, so the reference is not a stored
image but a render that is correct by construction. `smoke` is the same
experiment with real plumes, for the eyes.

**Bug one: the depth weight was a constant.** `gl_FragCoord.z` with the sample
scenes' near plane of 0.01 spans 0.995 to 1.0 across the whole world, so the
paper's `1 - z * 0.9` term varied by 4% from arm's reach to the horizon -- and
the `1e8` multiplier put the product 39x over the clamp ceiling anyway, at
every distance. Proven, not argued: replacing the entire expression with the
literal `1.0` gave a **pixel-identical** image. Weighted blending was silently
computing a flat average. The layers scene rendered mid-grey where the truth
is red-dominant.

**Bug two, found while fixing the first: the resolve was upside down on
Vulkan.** It is a fullscreen pass that samples two render targets, so it owed
the `FlipY` every post pass pays -- and being a single pass, nothing
downstream cancelled it. It shipped because the test scenes were plumes near
the middle of frame and flipped smoke still looks like smoke. §5 has the
rule, now stated as "anything that samples a render target and writes another
owes FlipY" rather than the old, and already-false, "nothing else samples a
render target".

**What was chosen, and why it is not the paper's.** Weight now falls as
`1/d` on linear view distance (`1.0 / gl_FragCoord.w`, exact for this
projection). The exponent is a straight monotone trade, measured both ways:

| weight | 3 layers, 2/20/200 | dense plumes |
|---|---|---|
| flat (what shipped) | 1.90 | 1.17 |
| d^-0.3 | **0.00** | 0.76 |
| **1/d (chosen)** | 3.05 | 0.29 |
| 1/d^2 | 4.11 | 0.22 |
| equation 9 | 4.23 | **0.20** |

Steeper is better on plumes and worse across depth, without exception. Read
the first column carefully: `flat` scores well there while being the one
option that is definitely broken, because grey sits near the mean of that
stack while ordering nothing. And `d^-0.3` is *exact* there -- equal-alpha
layers want their weight halving per layer however far apart they are -- and
useless in a plume. No depth-only weight is right for both; that is the
technique, not a defect.

So the exponent was settled on a property the table does not show. Under the
paper's 1e-2..3e3 clamp, equation 9 only **discriminates** between 1.5 and 100
units and clamps flat outside it -- which is why it draws the 20-unit and
200-unit layers identically. 1/d^2 spans 0.94 to 520. 1/d spans 0.03 to 9000,
wider than any scene this engine will hold, and the range it buys is at the
*near* end, where a camera standing inside a smoke cloud lives. It gives up
0.09 against equation 9 on plumes to never fail that way.

**`tools/scripts/check_oit.py` is the regression check** -- silhouette against
the sorted render, and near-beats-far at the centre. Both failures were
reintroduced deliberately to confirm it fails, and it names the flip as a flip.
It is not in scenetest because scenetest cannot read pixels back; it runs the
runtime and compares screenshots.

Verified: 888 checks both backends, 0 validation messages, Debug/Release/Dist,
`rvdoc --check`, and `particles_oit.rage` re-rendered on both backends with the
weighted content now landing in the same place on each.

### Deferred - the rest of the particle wants

Named here so they are not rediscovered as ideas. In the order they were
judged worth doing, which is not the order they were asked about:

- **Particle collision.** Two different features wearing one word. Analytic
  shapes (per-emitter planes and spheres) are cheap, identical on CPU and GPU,
  need no frame reordering, and cover sparks off a floor -- do this one first.
  Screen-space depth-buffer collision is what Unreal's GPU sprites do and is
  general, but it needs the sim to sample depth, which means moving the
  dispatch after the depth pass (it runs in `OnUpdateRuntime`, before the
  render pass, because both command lists assert dispatch-outside-render-pass)
  and accepting one-frame-old depth. Per-particle Jolt queries are CPU-only
  and O(particles), which defeats the GPU path entirely.
- **Sub-emitters.** The heaviest, and the one that wants a design note before
  code. Needs particle *death events*. The CPU side is tractable; the GPU side
  needs an append buffer the sim writes and indirect dispatch to consume it
  without a readback, plus an ownership model -- pooled child emitters, never
  an entity per particle.

### Done - 6.10, curves beyond start and end

**Complete, all seven steps.** Every ramp that was a two-point lerp on
normalised age can now be a shape. The user's three calls (2026-08-11): curves
are **assets**, the editor is a **real draggable one**, and size, colour and
alpha each get their own -- alpha separate from the colour gradient, so one
gradient can be shared between emitters that fade differently.

**The design decision that made it small.** `AssetHandle` was already
`FieldType::Asset`, so a handle field cost nothing new: the inspector drew it,
the serializer wrote it, and the C# bridge reached it as a path string, all
without changes. An inline curve would have needed a new `FieldType`, a new
text form and a new inspector control before the first curve rendered. **One
asset type, not two** -- a scalar curve and a colour gradient want the same
file, handle and sampling and differ only in channel count.

What exists now:

- `Asset/Curve.h` -- keys sorted on insert, so sampling (per particle, per
  frame) never pays for authoring (when somebody drags a point). `Curve::Baked`
  is the 64-sample table **both** paths read.
- `Asset/CurveSerializer` -- `.rcurve`, YAML so a curve stays diffable. Loading
  goes through `AddKey`, so a hand-edited file with keys out of order loads as
  the curve it describes.
- `Manager::GetCurve / CreateCurve / ReloadCurve / GetBakedCurve`. **A valid
  handle never answers null** -- a missing file gives an *empty* curve that
  evaluates to its fallback, so the emitter samples a value instead of every
  call site remembering a branch. Only an invalid handle answers null.
  `ReloadCurve` exists because the cache is otherwise the stale-mip-chain bug.
- `SizeCurve`, `ColorGradient`, `AlphaCurve` on the emitter. **The pairs still
  work and are not legacy**: an unset handle leaves that channel to
  `SizeStart/SizeEnd` or the colour pair, so every existing scene is
  unaffected and nobody must author a curve to get a particle.
- `Particles::Evaluate` -- the override rule, as a free function, because
  three things must agree on it: the CPU path, the compute shader and the
  suite.
- The GPU path is **handed the answer, not the ingredients**: the CPU resolves
  the ramps once per emitter per frame into `ColorRamp[64]`/`SizeRamp[64]` in
  the emitter UBO, so the shader has no copy of the rule to drift from. 2 KB
  per emitter to delete a class of bug.
- `UI/CurveEditor` -- draggable points, wired into the inspector from
  `SceneHierarchyPanel.cpp`.
- `tools/scripts/make_curve_presets.py` writes the stock curves and the
  side-by-side scene; `scenes/particles_curves.rage` and its `_gpu` twin.

**Measured, and it found something.** Under `Additive` the CPU and GPU paths
agree to **0.10 of 255** on both backends. Under `Alpha` a curve-driven
emitter differs by **2.7, seven times the run-to-run noise** -- and switching
to an order-independent blend collapses it, which pins the gap on the GPU's
inability to sort within an emitter rather than on the ramp. **Curves make
that gap worse**, because they widen how much particles differ in colour and
opacity, which is exactly when unsorted alpha goes wrong. That is now the
measured argument for 6.11.

**One property worth keeping in mind**: baking rounds off a curve peak that
falls between two of the 64 samples, by under one table step. A check pins it,
so changing `kSize` shows as a number moving rather than silently.

### The sample project's flame

The demo scene's pedestal is lit: `Brazier Flame`, a child of the pedestal in
`scenes/demo.rage`, sitting on the ornament. It is the first particle effect
in the repository with a real sprite and the first thing that uses all of
6.10 at once -- worth keeping as the reference for what a finished emitter
looks like.

Additive because fire is light rather than matter; local space, so it rides
the pedestal and its uniform scale sizes the particles (0.75 is what makes a
bonfire read as a brazier); upward *emitter* gravity for buoyancy, which is
one field because an emitter's gravity is not the world's; and all three
ramps curved, because a flame is exactly the case where none of them is a
straight line -- a tongue is widest a third of the way up, which two
endpoints cannot say in either direction.

It lives in the demo scene rather than a scene of its own on purpose: an
effect belongs in the scene it decorates, and a showcase nobody opens is not
a showcase. `make_curve_presets.py` writes the curves it uses (`flame_size`,
`flame_alpha`, `ember`) and the scene references them by handle.

The sprite came in at 3840x2160 and 16:9. A particle quad is square and
stretches its sprite to fill, so it was cropped to its own alpha bounding
box, squared about that box's centre and resized to 512 -- 2.5 MB to 236 KB.
Squared about the *content* rather than the image, because a billboard
rotates about its middle and an off-centre sprite wobbles as it spins.

### Done - the architecture book (docs/design/)

**90 pages, 12 chapters, ~31,000 words, 64 code listings, 6 vector diagrams,
11 figures.** `architecture.html` is the source, `RageV-Architecture.pdf` the
deliverable, `README.md` the regeneration command and its traps.

The brief, which grew over the session: a knowledge-transfer document written
so a software engineer **learning game development** could follow it and arrive
at this engine -- explicitly modelled on learnopengl.com, a path from A to B
rather than a description of a finished artefact.

Chapter 2 is the mathematics (spaces, the projection matrix derived, why depth
is not distance, colour, the rendering equation, alpha compositing ending in
the algebra showing a constant OIT weight cancels). Chapter 3 is the hardware
and both APIs, ending with weighted-blended transparency traced all the way
down through each. Chapter 4 is the build order. Chapters 5-8 are the engine
as teaching rather than reference. Chapter 9 is every invariant grouped by
*kind of mistake*. Chapter 10 is the verification methodology. **Chapter 12 is
nine milestones with real code**, each ending with a proof you can run.

**Three traps in producing it**, all in `docs/design/README.md`:

- **Chrome does not paint the root background when printing.** Pages end up
  transparent, which most readers draw white and a dark-mode reader draws
  **black, with black text on it**. A fixed `body::before` in `@media print`
  paints a real white rectangle. Verify by decompressing the PDF's content
  streams and counting pages with a white fill -- the regeneration reports it.
- **Chrome refuses to write the PDF into the repository** ("Access is denied")
  and needs `--user-data-dir`. Print to a temp path and copy.
- **The Claude Code preview pane forces dark mode** regardless of
  `color-scheme`. Render with headless Chrome to see true colours.

Also: code blocks are light, not dark -- a dark block on a light page inverts
to light-on-light in a reader's dark mode and prints as a solid black
rectangle. And `table { width: 100% }` will flatten an inline matrix unless
`.mat` sets `width: auto`.

**What would still improve it** (judgement, not a list): a continuity
read-through. The chapters were written across several sessions and the seams
may show -- repeated explanations, cross-references assuming a different
reading order, and a tone that shifts between the reference chapters and the
walkthroughs.

### Done - Knockdown's juice, and two staleness bugs it exposed

**The last open item from the particle work, and the first end-to-end proof a
C# script can drive the particle system.** Nothing outside the suite exercised
that path.

Two emitters in `Main.rage`, neither emitting on its own. **Impact** is dust,
moved to `Collision.Point` and burst by whichever crate was hit -- one shared
emitter for all six crates, in world space so the dust hangs where it was born
rather than following the emitter to the next hit. **Confetti** sits above the
platform and fires once on the win from `GameManager`. The existing
impact-speed ramp drives both the sound and the dust, so a light tap looks as
light as it sounds.

**Two pre-existing breakages this uncovered, both of which made the game
unrunnable and neither caused by the change:**

- The managed assembly in `Knockdown/Scripts/bin/` predated protocol 5, so
  every collision threw `MissingMethodException` on `PlayOneShot` -- the
  signature gained a pitch parameter. **The engine looks for
  `Scripts/bin/<Name>.dll`**, not the SDK's nested `bin/Debug/net8.0/`; a hand
  run of `dotnet build` must copy it up.
- The native game module failed to load with **error 127**, so `Launcher` and
  `Ball` were unknown scripts and the game could not be aimed or fired.
  Rebuild it with:
  `cmake -S Knockdown/Source -B Knockdown/bin/module -DRAGEV_ENGINE=<repo>/build`
  then `cmake --build Knockdown/bin/module --config Debug`.

Both fixed; the runtime now reports 2 native and 3 C# scripts with no
unknown-script warnings.

**A verification lesson worth keeping.** The dust works -- the *user* confirmed
it by playing. My own screenshots kept missing it and I was drifting toward
the wrong conclusion: a 0.45 s effect sampled at four arbitrary frames is a
coin flip, and the brightness threshold I measured with would not have caught
grey dust against a pale sky anyway. What I had actually established was that
an authored burst renders and that `SetComponentField` returned true with the
emitter positioned; I had not closed the gap between those two facts. **When a
visual check keeps coming back negative, question the instrument before the
feature.**

### Done - the editor UI overhaul (2026-08-11)

**Researched first, then measured.** Six commits, `68c611e` to `4c38561`.
Three findings changed the design rather than decorating it: reading speed
drops measurably on pure-black dark themes and a saturated red fringes against
`#000`; inverting a palette is the classic light/dark mistake in both
directions; and left-aligned labels scan badly because of a *ragged gutter*,
not because they are on the left -- which is why an inspector keeps a fixed
label column instead of adopting the top-aligned labels that test faster on web
forms.

**What exists now**, all in `RageVEditor/src/UI/`:

- **`EditorTheme`** -- semantic tokens, one name and two values, so a call site
  never mentions a colour. Spacing on a 4px grid, one radius family, and a
  four-step type scale (caption, body, title, display) as *multipliers* on the
  base size, because the editor folds a UI scale and a DPI factor into that
  already. ImGui 1.92 rebakes a font at a new size on demand, so the scale is a
  push and a pop rather than preloaded fonts.
- **`Widgets`** -- the layout primitives. `BeginProperties`/`PropertyRow` and
  the `Row*` wrappers (`RowCheckbox`, `RowColor3`, `RowDragFloat`, ...),
  `AccentButton`, `IconButton`, `DragVec3`, `SegmentedControl`, `TextCaption`.
  Panels written with these cannot produce the truncation the fixed column did.
- **`AssetIcons`** -- **~24 icons drawn as vector paths**, not shipped as
  images. The browser has a 34-128px size slider with a UI scale on top, so a
  bitmap set is sharp at one size and soft everywhere else *and* would need a
  second set for the light theme. One definition in the theme's colours is
  correct at every size in both themes. Keyed on a `Kind` rather than an
  `AssetType`, because the browser lists what is in the folder -- scripts,
  shaders, a readme, the .csproj -- which is wider than the seven things the
  engine imports. Entities get icons too, chosen by component, most specific
  first.
- **Monochrome on purpose.** The palette's rule is that red means "you can act
  on this"; colouring a mesh red for being a mesh spends that signal on
  decoration. Type is carried by shape, which survives being small, being
  greyscale, and a reader who cannot separate red from green.
- **Two themes**, switchable from Window > Theme, remembered in `panels.ini`,
  overridable with `--theme`. `tools/scripts/check_theme_contrast.py` measures
  every pair the editor actually puts together against WCAG 2.2 AA and exits
  non-zero on a failure.

**Seven real bugs it found**, none of them cosmetic, most of them pre-existing:

1. **The inspector asserted and stopped** on a narrow panel -- fixed 140px
   label column starving `CalcItemWidth()`. Reproducible on demand by restoring
   the old `DrawVec3` on today's padding.
2. **The toolbar had never rendered**, in any screenshot ever taken of this
   editor, because it was drawn after `DockSpace()`.
3. **Accent-filled buttons failed contrast in both themes** (3.1:1 and 3.2:1).
4. **Toolbar widths were fixed pixels**, so they clipped at any UI scale but 1.
5. **Every docked panel drew two close buttons** -- its tab's and the dock
   node's.
6. **Ten `(?)` markers were clipped to a bracket**, drawn with `SameLine()`
   after a full-width table.
7. **The Build Log had no dock slot**, so it opened floating over the
   hierarchy.

Plus a `*` on component headers that meant "options" while meaning "modified"
everywhere else in software, and nine hardcoded colour literals.

**A verification lesson, and it is the same one as last time.** The contrast
script proved `OnAccent` on `Accent` passes, and had no way to know that *no
call site was using it*. **A palette being correct and a palette being used
correctly are separate claims, and only one of them was being checked.** The
guard now is that `AccentButton` is the only path to an accent fill.

**And a correction worth keeping.** Asked whether the assertion was really
fixed, the first attempt to reintroduce it did not reproduce: removing the
`std::max` clamp from `DragVec3` left the editor running at every size. The
clamp was not the fix -- the proportional column is. **A fix that works is not
evidence for the mechanism you assumed**, and there was a reproducer available
the whole time.

**Six more fixes landed after this entry was first written**, all from the
user looking at screenshots and pointing at things -- which is worth recording
as its own lesson: **every one of them was invisible to me and obvious to
somebody seeing the panel for the first time.**

- The **inspector had two left edges**: vector rows used a 30% label column
  while every other row used 42%. Two failed attempts before the right one --
  a flat 30% (what caused it), then an *adaptive* fraction, which misses the
  point entirely because alignment requires the **same** fraction, so anything
  adaptive is misaligned by construction. The answer was one fraction for every
  row, and 38% rather than 42%: a property name is one or two short words and a
  value can be a sign, four digits and a point.
- **The toolbar's left edge had no inset** while the right edge had 12px, so
  the gizmo buttons clung to the window while the camera toggle sat comfortably
  off it.
- **The rotate icon's arrowhead was attached to nothing** -- three points typed
  in by hand, in the opposite corner from where the arc actually ended. It is
  computed from the arc's own end angle now.
- **The Snap checkbox read as an empty button.** ImGui draws a checkbox as a
  framed square with the label to its right, which in a row of icon buttons is
  indistinguishable from a button with nothing in it.
- **Colour fields in registry-driven components** still showed four numeric
  boxes, unlike the material editor beside them.
- **Slider grabs** were a thin saturated bar in an empty field with no track
  behind them, which reads as a stray mark rather than a handle.

**Selecting an entity now reveals it in the hierarchy.** A viewport pick always
set the selection, but a child of a collapsed parent is never *drawn*, so the
highlight had nothing to appear on and the panel looked like it had ignored the
click. Ancestors are expanded on the way down and the row is scrolled to; the
setter raises a flag and the draw consumes it, because opening a tree node is
something only ImGui can do and only while drawing.

**What is still not designed.** The engine now looks correct and consistent; it
does not yet look *authored*. Left on the list: real font weights (the variable
font's weight axis needs FreeType -- ImGui's stb rasteriser cannot reach it, so
there is size hierarchy but no weight hierarchy), depth between the viewport
and the chrome around it, and a keyboard focus ring. Judgement, not defects.

**Note for the START HERE below.** None of this serves the *game* UI. These are
editor-side ImGui primitives; a game's UI ships, is skinned, is authored in
scenes and must work with no editor present. The design tokens are worth
reading for the reasoning; the code is not reusable there.

### Done - the infinite viewport grid (2026-08-11)

`RageV/src/RageV/Renderer/ViewportGrid.{h,cpp}` and
`RageVEditor/assets/shaders/grid.rvshader`. Infinite because it is not
geometry: a grid made of lines has an extent, and the edge of it is visible
from anywhere the camera can get to.

**A plane solve for the depth, a ray for the position.** A projective transform
maps planes to planes, so a point is on y=0 exactly when its clip position is
perpendicular to the **second row of the inverse view-projection** -- which for
a known pixel is one linear equation in one unknown:

```
depth = -(row.x * ndc.x + row.y * ndc.y + row.w) / row.z
```

That is the plane's depth at that pixel in one division, already in the [0,1]
both backends use, and the near plane and the horizon fall out of the range
check rather than each needing an epsilon. `row.z == 0` -- the camera looking
exactly along the plane -- produces a NaN the range test rejects for free.
`ViewportGrid::PlaneDepthAt` is the same solve on the CPU so the test suite can
check it against points whose depth is already known.

**The first version used that depth to reconstruct the position too, and that
was wrong.** It is invisible until you get a long way out, and then it is
obvious: NDC depth is compressed, so everything from the far clip to infinity
lives in the last ten-thousandth of the range, where a float32 has a few
hundred distinct values for the whole outer world. Positions built through it
quantise and the grid becomes speckle. The position comes from a ray -- two
points at well-conditioned depths, which keeps full relative precision however
far `t` runs. **Neither replaces the other**, and the claim in the first
version of this entry that the solve made the ray unnecessary was wrong.

**The backend trap the design warned about cannot happen here, and the reason
is worth keeping.** Vulkan's NDC has y down and OpenGL's has it up. It does not
bite because `v_NDC` is the vertex's own clip position carried through the
rasteriser: whatever the convention, a fragment's `v_NDC` *is* its NDC, and the
matrix reading it is the one the scene was drawn with. The trap is
reconstructing from `gl_FragCoord`, where the flip has to be applied by hand
and nothing notices its absence. Verified anyway -- the two backends' viewports
differ by a max of 23/255 on 667 of 560,000 pixels, all sub-pixel noise along
the lines themselves.

**Depth is written and depth writes are off**, which are two different things
and both deliberate. `gl_FragDepth` carries the plane's depth so the depth
*test* is real; without it every fragment sits on the far plane and the grid
appears only where nothing was drawn, hiding a line in front of a distant wall
as readily as one behind it. The pipeline still disables depth *writing*, like
the sky does: an antialiased line has edge pixels at five percent coverage, and
a fragment that faint has no business occluding a particle behind it.

**Nothing is discarded**, and that is not laziness. Every `discard` here would
be a discard *after* the `fwidth` calls above it, which leaves the neighbouring
pixels' derivatives undefined -- and at the horizon the pixel beside an
in-range one is precisely the one that was not. Out-of-range pixels clamp to a
finite depth so the derivative stays finite, and multiply their alpha to zero.
That turns the horizon into a fade instead of a fringe.

**The spacing is chosen per pixel, and three decades are cross-faded.** The
first version had a fixed pair -- one unit and ten -- with a density fade. The
user found what is wrong with that by zooming out: **a fixed spacing either
aliases when you zoom out or vanishes, and this one vanished**, leaving nothing
to navigate by. So each pixel picks the finest decade whose cell still covers
about nine pixels, and three of them are cross-faded so the handover has no
frame where the whole grid jumps a decade. Two sets can draw this but cannot
cross-fade it: at the rollover the middle set has to already be carrying the
weight the coarse one is about to take.

Per *pixel* rather than per frame, which is why one image shows fine lines
underfoot and coarse ones near the horizon. It also removes a precision floor
that would otherwise arrive around a hundred thousand units out, because what
`fract` is handed stays the size of a screen rather than the size of the world.

**And the far clip is not where the floor ends.** The same report: at eight
hundred units out the grid stopped dead at a hard horizontal edge, well short
of the horizon, because the solve rejected `depth > 1`. NDC depth for a point
in front of the camera asymptotes just above 1, so "beyond the far plane" is a
sliver of range covering everything from the far clip to infinity. It is
accepted now and pinned to 1 on the way out -- every such point is further than
anything that could occlude it, so one depth serves them all.

**The limit that remains, honestly.** Past roughly ten thousand units the lines
start to break into dashes. That is float32 through a projective inverse, not a
logic error, and it is ten times the editor camera's own far clip -- nothing in
the scene renders out there at all. Clean to about five thousand, checked.

**Where it is drawn.** Inside the scene pass, after the sky and before the
particles -- *not* the separate graph pass the design called for. After the
sky, because the sky is drawn at the far plane against the depth test and would
be rejected wherever the grid had already claimed a pixel. Before the
particles, because the grid is scenery to them. A graph pass would have to sit
after the whole scene pass, which puts it on the wrong side of the forward
particles.

**It reaches the scene through an argument, not a setting.**
`Scene::OnRenderEditor(camera, grid)` takes an optional `ViewportGridSettings*`;
`OnRenderRuntime` has nowhere to put one. So the game view and the shipped
runtime cannot get a grid by construction rather than by remembering to check a
flag.

**`--camera=x,y,z,distance,yaw,pitch`** was added for this and is the third
member of the `--screenshot` / `--select` family. An infinite plane looks
completely different at a grazing angle and from 400 units out, and those are
exactly the cases that alias; driving the camera by hand to check them is not a
check anybody repeats.

**A correction, and it is the same shape as the last two.** The first
screenshot appeared to show the grid drawing straight through solid geometry --
lines crossing the face of a cube. The obvious reading was a broken depth test.
It was not: `Ground` in the sample scene sits at y=-1 with scale 1, so its top
surface is at **y=-0.5**, and `Sphere (gold)` spans -0.8 to 1.0. The props
straddle the origin plane, and a grid at y=0 genuinely passes through them.
Two probes settled it -- writing a constant far depth (the grid vanished behind
everything, so the test worked) and then a solid fill at the computed depth
(the objects were cut at exactly y=0, so the depth was right). **The screenshot
was evidence about the scene, not about the code**, and reading it the other
way would have "fixed" a working depth test. Note this if the sample scene ever
looks wrong: the grid is at y=0 and that scene's floor is not.

**And a smaller one.** The line colour was first darkened in the light theme,
reasoning that a light theme has a light background. It does not -- the panels
go pale and the viewport does not, because what is behind the grid is the
scene's own sky and ground. The dark line then nearly vanished against the dark
horizon. That is the mistake `EditorTheme.h` warns about (inverting a palette
instead of authoring one) reaching a surface the theme does not own. One mid
grey reads against both; only the axes follow the theme, so that the grid and
the transform widget name X and Z the same way.

**The toggle** is `m_ShowGrid`, on by default -- the opposite call from the
collider overlay, because this is the floor rather than a diagnostic, and a
scene with nothing in it is otherwise a gradient with no scale, no horizon and
no way to tell where the camera is pointing. Toolbar button, Window > Show
Grid, **F2**, and `grid = ` in `panels.ini`. `IconKind::GroundGrid` is a floor
in perspective, deliberately unlike `SnapGrid`'s flat lattice three buttons
away; the first attempt had two rails and four rungs and read as a traffic cone
at 18px, which converging *interior* lines fixed.

**Verified**: 923 checks on both backends, 14 of them the grid's; the two
backends diffed against each other near and far (no shift, no flip -- the
difference is speckle *along* the lines, which is what sub-pixel precision on
thin high-contrast features looks like); grazing (camera in the plane), 400
units out from above and from below, 800/3000/5000/10000/20000 out, close in,
both themes, the toggle off, and a malformed `--camera`.

**Both of the zoom bugs came from the user looking at a screenshot**, which is
now three sessions running. Send them.

### Phase 6 text and UI: what is built, and START HERE for the rest

**Designed first**, in ENGINE-NOTES 7d -- read that before touching any of
this. It carries the reasoning; what follows is the state.

**Done (2026-08-11):**

| | |
|---|---|
| `tools/rvfont` | bakes a `.ttf` into an MTSDF atlas plus a metrics table |
| `tools/scripts/prepare_font.py` | resolves a font's geometry so it *can* be baked -- **run this first** |
| `tools/scripts/check_font_atlas.py` | reconstructs glyphs the way the shader will |
| `AssetType::Font` | `Font`, `FontSerializer`, and the atlas through `Assets::Manager` |
| `Renderer/UIRenderer` | one batched pipeline for sprites and glyphs |
| `UI/TextLayout` | UTF-8, kerning, wrapping, alignment -- all on the CPU |
| `UI/Canvas` | anchors, canvas scaling, sort order, resolved over the entity hierarchy |
| components | `UICanvas`, `UIRect`, `UIImage`, `UIText`, `UIButton`, registered and serialized |
| `UI/Interaction` | hit-testing, the button state machine, and `WantsPointer` |
| protocol 7 | `SetUIText` / `GetUIText` / `WasUIButtonClicked` / `IsPointerOverUI`, both languages |

The demo scene has a HUD **with a working button** and so does the shipped
runtime.

**6.4 is done (2026-08-12).** What landed beyond the obvious, each because the
alternative is a bug nobody can trace:

- **Blocking the pointer is opt-in, not opt-out** -- the reasoning is in
  ENGINE-NOTES 7d and the invariant is in 5. A button blocks regardless.
- **A press is captured**, so sliding off cancels it and coming back completes
  it, and `WantsPointer` stays true for the whole gesture.
- **A click is an edge on `InputMap`'s exact contract**: one press, one step.
- `ClickCounter` in `BuiltinScripts.cpp` is the worked example and the
  regression probe -- scenetest presses it and reads the label back, which is
  the only check here that would notice the step order being wrong.

**Phase 6 is done.** Knockdown has a title, a live score and a control hint,
which was the acceptance test -- the game existed for a whole phase with no way
to write "4 of 6 down" on the screen, and that absence is why the phase
happened.

**Binding validation is built too (6.4d).** `UI::ValidateBindings` runs at scene
load as a **warning** -- an author mid-edit has half-wired buttons all the time
-- and in `rvpack` as an **error**, because shipping one is not a normal state.
`--allow-dead-bindings` downgrades it, and has to be asked for. Every scene in
the project is checked, not only the start scene: a menu is usually its own
scene and is exactly where the buttons live.

> [!TRAP]
> **A check that cannot look must not answer "no".** C# handlers are only
> visible while .NET is up, so with the host down every managed binding reads as
> broken -- and the packager would refuse a game whose buttons all work. That is
> how a check gets switched off. `ValidateBindings` skips a managed target it
> cannot inspect.
>
> It was not hypothetical: rvpack had no `managed/` beside it, so .NET never
> started there. `ragev_stage_managed(rvpack)` fixes the cause; the skip covers
> every other way the host can be down.

**6.11 is built, so phase 6 is complete.** `particle_sort.rvshader` is a
bitonic sort in shared memory -- one dispatch, one workgroup, no readback --
writing a *second instance buffer* in sorted order rather than an index buffer
the draw indirects through. That costs 48 bytes per particle instead of 4 and
buys a draw path that does not change at all: `Gpu::GetInstances` hands back
the sorted buffer and the renderer cannot tell. Past 2048 particles an emitter
draws unsorted, as before, and says so once.

**What it costs, measured** -- Release, Vulkan, vsync off, 400 frames, the
particles scene with all three emitters on the GPU (two of them alpha, 1024
particles each). Four alternating pairs, so machine drift hits both:

| | GPU frame time |
|---|---|
| sorted | 0.247, 0.248, 0.245, 0.246 |
| unsorted | 0.217, 0.219, 0.218, 0.227 |

**+0.026 ms**, same direction in all four pairs, about five times the spread.
That is +12% *of this frame*, and the honest reading is that this frame is
nearly empty -- 0.25 ms total. In absolute terms it is 0.013 ms per sorted
emitter, which in a real 8-16 ms frame is a fifth of a percent. Both numbers
are true and quoting only the percentage would be alarming for no reason.

**I asserted this was negligible before measuring it**, which is precisely what
the roadmap warns about two lines from where it says two "obviously worth it"
optimisations in this renderer measured as worth nothing.

> [!TRAP]
> **A pixel comparison cannot verify this, and three thresholds got tuned
> before that was obvious.** The plan was to render one emitter on the CPU and
> on the GPU and diff the images, assuming both paths simulate the same
> particles. *They do not.* An identical burst scene rendered each way differed
> by **6.5 of 255 under additive blending**, where order cannot matter at all --
> the two simulations agree statistically, not particle for particle. Every
> version of that check was measuring the difference between two plumes and
> calling it ordering, and one run of a completely unsorted build passed it.
>
> The instrument that works is in scenetest: dispatch the real shader on a
> buffer built by hand, read it back, and check the order exactly. It catches
> an inverted comparison, which the pixel check waved through.
>
> On the way, this also found that **particles simulate on frame time**, so two
> captures of one scene were never the same picture -- the same measurement
> swung 0.78 to 0.23 between runs of an identical build. `--frame-time=<seconds>`
> now pins it, and any future screenshot comparison should use it.

### 7.7 -- per-object reflection probes. Built, shape B.

Reasoning in ENGINE-NOTES §7e. What is in the tree:

`ProbeArray` owns **one** radiance cube array and one irradiance cube array,
sixteen slots each. Slot 0 is the sky, always. Every complete probe gets a slot;
`Scene::ProbeSlotFor` answers, per object, which slot it reflects -- nearest
probe whose `Influence` reaches it, else 0 -- and the answer rides in
`InstanceData.Indices.y`, so two objects that chose differently are still one
instanced draw. The PBR shader and its skinned variant sample
`samplerCubeArray`.

**The design said one array per resolution; that was wrong.** Two arrays means
two bindings, and choosing between them is a per-draw decision -- which is the
sort-key fragmentation shape B exists to avoid. The array has one face size, the
largest `Resolution` in the scene, and a smaller probe is resampled up into its
slice. Prefiltering already resamples, so this costs nothing real.

**Probes now contribute diffuse light, which they never did before.** The only
irradiance convolution was `IrradianceFromCube`, a 200 ms CPU routine that runs
at asset load -- impossible for a capture that never touches the CPU. So a metal
object beside a probe reflected the probe while the diffuse half of the same
surface was lit by the sky. `irradiance.rvshader` is the GPU version, normalised
to match the CPU one exactly.

**Three RHI bugs came out of this**, all latent and all silent.
`CopyToTextureLayer` wrote the *source's* rectangle into the destination on both
backends -- correct only while nothing resampled, and a filled corner with a
stale remainder the moment something did. OpenGL allocated a cube array's
storage from `Layers` directly where Vulkan clamped to six per cube; they agreed
for 2D arrays, the only layered texture that existed, and would have disagreed
on the first cube array either allocated.

And **Vulkan never enabled the `imageCubeArray` device feature**, which both
`samplerCubeArray` in SPIR-V and a cube-array image view require. This driver
did it anyway. Every screenshot above was correct while the feature was formally
undefined, and the only thing that ever said so was a validation line -- found
by the editor run, after the runtime and scenetest runs had been checked for
`error` and `FAIL` and passed both. **Grep for `[Vulkan]`, which §2 has said all
along.** It is now a device-selection requirement, not just an enable: without
it the lit pipeline cannot be created, so failing at startup beats drawing
something wrong.

**To see it work:**

```bash
build/bin/Debug/RageVRuntime/RageVRuntime.exe --rhi=vulkan --scene=scenes/probes2.rage --frame-time=0.016666 --screenshot-frame=60 --screenshot=two.png
```

Two emissive rooms, red and green, a mirror in each, camera nearer the red one.
Left mirror red, right mirror green. Under the old camera-based selection both
go *sky*, because the camera sits outside both influence radii -- so nothing in
the scene gets a probe however close an object is to one.

The same scene has **three matte spheres** -- one per room, one outside both --
which is the diffuse half. No lights in the scene, so ambient is the only
illumination: red room pink, green room green, third one neutral.

**And one scene with a known-correct answer**, which is worth more than either:

```bash
build/bin/Debug/RageVRuntime/RageVRuntime.exe --rhi=vulkan --scene=scenes/irradiance_uniform.rage --frame-time=0.016666 --screenshot-frame=30 --screenshot=u.png
```

A white matte sphere under a sky that is one colour in every direction must be
the same brightness as the sky behind it, because the cosine-weighted average of
a constant is that constant. **203 against 205, ratio 0.990, both backends.** A
convolution returning the integral instead of the average would read 3.14 and
blow the sphere to white.

**Superseded -- this was the plan for phase 7 when it was written, and it is
kept only because the reasoning below it still explains why these were
grouped. The live list is the START HERE at the top of §8.** Of the items it
names, 7.1, 7.2, 7.3, 7.4, 7.5 and 7.6 are all done; only 7.8, 7.9 and 7.10
remain.

> [!TRAP]
> **A project's C# is loaded from `Scripts/bin/`, not from `bin/`.** Editing a
> `.cs` and running the runtime runs the *old* assembly, silently -- the game
> plays, the scripts are stale, and nothing says so. It cost a confused
> round-trip here: the HUD's score label kept showing its authored placeholder
> and the bug looked like it was in the new UI code.
>
> `dotnet build <project>/Scripts/<name>.csproj -o <project>/Scripts/bin
> -p:RageVScriptCore=<path to the built RageV.ScriptCore.dll>`, or press Build
> Scripts in the editor, which is what a person would do.
>
> **The way to catch this class of mistake: author the scene with a placeholder
> the script must overwrite.** "0 of 6 down" in the file *and* from the script
> proves nothing; "(score)" in the file proves the script ran.

### 6.4b -- text in the world. Built.

`WorldTextComponent`, drawn by `UI::DrawWorldText` through the **same shader,
the same atlas and the same `UI::Build`** the HUD uses. The hedge made in the
design paid exactly as intended: `screenPxRange` is measured from screen-space
derivatives, so the fragment stage was not touched at all.

What it actually cost: `a_Position` widened from `vec2` to `vec3` (the screen
layer writes z = 0), a second pipeline differing only in depth state and target
formats, and `UI::BillboardAxes`.

- **Depth tested, depth not written.** Testing is the point -- geometry
  occludes a nameplate. Writing would have each glyph quad occlude the
  transparent things behind it, including the rest of its own string wherever
  two quads overlap.
- **Before the particles**, for the reason the grid is: a label is scene that a
  blended particle blends against.
- **Billboarding is the caller's policy.** `DrawWorldText` takes the two axes
  the quads lie along. `Upright` is the default because `Full` tips the text
  back when the camera looks down, and a row of tipped nameplates reads as a
  bug.
- The camera's axes are normalised, the entity's are not -- a scaled sign has
  bigger letters, a scaled camera rig must not resize every label in the world.

> [!TRAP]
> **Creating a pipeline clears the batch pool, and that pool is shared.** With
> one pipeline that only ever happened before the first draw, so `AcquireBatch`
> growing by one per call was enough. The second pipeline is built *partway
> through* a frame in which the cursor has already advanced -- one push then
> leaves the pool shorter than the cursor, and the index runs off the end.
>
> It crashed on the first frame that drew both layers: `vector subscript out of
> range`, in code neither commit had touched. The fix is a `while` instead of an
> `if`. **A latent bug that having one caller kept dormant** -- worth
> remembering the shape, because the next shared pool will have it too.

### 6.4c -- the button calls your method. Built.

6.4 shipped **polling**: a script asks `WasButtonClicked` each step. I recorded
that as a deliberate omission and **the user overruled it on 2026-08-12** --
build the Unity-shaped thing, where the button holds a target entity and a
method name and the engine does the calling. They were right, and this is now
the primary mechanism.

**Polling stayed.** It is the primitive the binding fires from, it is what a
manager script reading five buttons wants, and it was already tested. The two
are the same click seen twice, so a button should use one or the other --
`ClickCounter` carries both and gates the polled half behind `PollOwnButton`
precisely because binding *and* polling one button counts it twice.

What landed:

| | |
|---|---|
| `EntityRef` + `FieldType::Entity` | a reference to another entity, as a registered field |
| `ScriptRegistry::Method<&C::M>()` | C++ methods a scene may name; C# needs none |
| `ManagedApi::ListMethods` / `InvokeMethod` | the C# half, by reflection |
| `Scene::InvokeScriptMethod` | both languages, both delivered -- modelled on `DeliverContact` |
| `Scene::DispatchUIClicks` | on the fixed step, after the script pass |
| inspector | an entity drop slot, and a combo of the target's methods |

**`EntityRef` is a struct wrapping a UUID, not an alias for one**, and that is
load-bearing: `AssetHandle` is *already* `using AssetHandle = UUID`, so a second
alias would be indistinguishable from an asset and every entity slot would come
up with the content browser's drop target. The wrapper is what makes
`FieldType::Entity` deducible at all.

> [!TRAP]
> **A method name in a scene file has no compiler behind it.** Rename the method
> and the button silently stops working -- the exact failure the `final
> OnUpdate` guard exists to prevent elsewhere. So an unresolvable binding warns
> **on every click**, naming the button, the target and the method.
>
> The first draft warned *once per button*, which reads better and is wrong in
> use: clicks two through ten then look exactly like the click never registered,
> which is the harder bug to report. A click is a deliberate act; a line per
> deliberate act is proportionate.

**What the event system could and could not give this.** `Events/Event.h` is the
Hazel-lineage platform dispatcher, and it is not reusable here: `EventDispatcher`
holds one `Event&` and type-switches on it -- there is no subscriber list to
join -- its `EventType` enum is window/key/mouse, its events are pumped outside
the fixed step, and `Event` is an `RV_API` C++ class **no C# script can
subscribe to**, which fails half the requirement on its own.

The reusable precedent was a different one: **`Scene::DeliverContact`** already
takes an engine-detected occurrence, finds the script instance on an entity, and
calls into it in both languages. `InvokeScriptMethod` is that shape with a named
method instead of a compile-time-known one, and `InvokeMethod` sits beside
`InvokeContact` in the same table.

**Verification note worth keeping.** The new UI tests failed *two checks in the
interop self-test* -- a file they do not touch. The pointer is one cursor and
therefore one global, and the suite left it parked over a button, so
`IsPointerOverUI()` answered 1 where the self-test asserts 0. `CheckUIInteraction`
now resets it on the way out. **A suite that leaves a global set is a suite that
breaks whatever runs next**, and the only reason this was cheap to find is that
something downstream asserted on it.

**Deferred to the end of the phase**, with their costs, in ENGINE-NOTES 7d: a
world-space *canvas* (the input path, not the rendering), rich text, input
fields, layout groups, localisation, RTL shaping.

### The font trap, and it will bite again

**A distance field cannot represent a self-intersecting outline**, and a great
many fonts have them -- variable fonts especially, because overlap removal
cannot be done once for shapes that change with an axis. RobotoFlex draws its
`e` as *one* contour where Arial, Segoe UI and Open Sans use two.

Nothing else in a font pipeline notices, because non-zero winding fills a
self-crossing path correctly. The field reports the edge it finds inside the
glyph, and the letter bakes with a bite out of it -- invisible small, unmissable
on a title.

**So run `tools/scripts/prepare_font.py` before `rvfont` on any font.** It says
which letters are affected, unions the contours, and writes a static copy. It
rewrites outlines and not layout tables, so kerning survives -- Roboto keeps all
3284 pairs. `RageVEditor/assets/Fonts/Roboto-Clean.ttf` is the processed one and
is what the shipped atlas is baked from; the RobotoFlex files beside it are the
source.

It needs `pip install fonttools skia-pathops`, which are installed here.

**How that was found, because the same method applies to the next one.** The
artifact reproduced *offline in the atlas*, which ruled out the shader. It was
identical at em 48, 64 and 96, which ruled out resolution. It was present in
MTSDF's alpha channel -- a plain single-channel SDF -- which ruled out edge
colouring. It survived `reverse()` and msdfgen's own `orientContours()`, which
ruled out winding. msdfgen's `validate()` passed on every glyph. Only then did
counting stb's contours against three other fonts settle it. **Five hypotheses
eliminated by measurement before the sixth was even proposed.**

### Done - two script rates, and the per-frame hook

**Complete, both languages, protocol 6.** The analysis this section used to hold
is now the code: `OnUpdate` is gone and there are two callbacks whose names say
*when* rather than what.

| | fixed simulation step | rendered frame |
|---|---|---|
| C++ | `OnTick(Timestep)` | `OnFrame(Timestep)` |
| C# | `OnTick(float)` | `OnFrame(float)` |

**The naming call was the user's** (2026-08-11), and it is the better one.
`OnUpdate` names an *action*, and "update" says nothing about when -- which is
the only thing that decides whether the code inside is correct. It is also
already spoken for: in Unity `Update` means *per frame*, so keeping it for the
fixed step would have meant a name that quietly reads backwards to most people
who arrive with habits. `OnFrame` is unambiguous -- there is exactly one thing a
frame is. `OnTick` leans on the networking sense of tick (a 64-tick server),
which is the dominant one outside Unreal; and next to `OnFrame` the pair
disambiguates itself in a way neither name does alone.

**Where it runs**: `Scene::StepFrameScripts`, called from `OnUpdateRuntime`
after the physics interpolation and the world-transform derive, and before
audio. That ordering *is* the feature -- `OnFrame` reads the transforms that are
about to be drawn. A second derive follows the pass, because everything
downstream (audio placement, both particle systems, rendering) reads the world
matrix.

**The frame pass deliberately does no reconciliation.** It creates nothing,
swaps nothing, and calls no `OnCreate`; instances are made in the fixed pass and
nowhere else. That is what makes `OnFrame` before `OnCreate` impossible by
construction rather than by a check -- and it means choosing a different script
in the inspector takes effect at a step boundary rather than halfway through a
frame.

**Also added**: `GetInterpolationAlpha()` / `Time.InterpolationAlpha`, for
smoothing a value the engine cannot see. Simulated bodies are already blended
before `OnFrame` runs; this is for something a script computed in `OnTick` and
wants to draw between steps.

**Two built-ins moved and are now the worked examples**: `Follow` (a camera --
the whole argument in one script) and `ImpactFlash` (the fade after the hit;
the hit itself stays on the contact callback). `Spinner` and `Mover` stay on
`OnTick`.

**The trap this exposed, and it generalises.** A rename on a boundary you do not
control can fail *silently*: C++ does not require `override`, so a script that
declared `void OnUpdate(Timestep)` without it would still compile -- as a brand
new method nobody calls. The script quietly stops working and nothing says so.
So `OnUpdate` stays in the base class as a **`final`** member, which turns both
spellings into the same clear compiler error, and C# gets the same treatment
with a non-virtual `[Obsolete(..., true)]` one plus a reflection warning at
create time for the hiding case. Both are deletable once nothing predates the
rename.

That guard paid for itself within the hour: it caught `Project.cpp`'s scaffolded
`Example.cs`, a *second* script template nobody had thought of, as a compile
error in the suite rather than as a script that silently did nothing in every
new project. **When renaming something on a boundary, ask what happens to code
that did not get the message, and make that outcome loud.**

**A redundancy this uncovered.** `Particles::System::Update` began with an
unconditional `scene.UpdateWorldTransforms()` -- a full hierarchy walk, every
frame, in every scene, including scenes with no emitters at all. It also
silently covered for anyone upstream who forgot to derive, which is why the
first version of the frame-pass test passed with the frame pass's own derive
deleted. It is now guarded by an emptiness check, which both removes the walk
and lets the test discriminate. **A defensive call that hides a missing one is
worse than either.**

**A latent test bug this turned up**, unrelated to the change and fixed in
passing: the game-module config check asserted the literal `"Debug"`, so it
failed in every configuration but that one. It now compares against the same
`RV_DEBUG`/`RV_RELEASE`/`RV_DIST` switch the engine uses. The suite had
evidently only ever been *run* in Debug -- "builds in three configs" and
"passes in three configs" are different claims. It now passes in all three.

**Verified**: 909 checks, both backends, exit 0, no validation messages; the
manual drift check (`rvdoc --check`) green; Knockdown runs end to end reporting
2 native and 3 C# scripts at protocol 6. The frame-pass test was falsified
before being trusted -- with the second derive removed the world matrix reads
0 against a local of 3.5, and the check fails.

### After that - 6.11, GPU alpha self-sorting

Now cheaper than it looks, and no longer urgent -- weighted blending is the
answer for GPU smoke and it works properly as of 6.9. This is for emitters
that want *exact* alpha.

Bitonic sort in compute, key = view depth, no readback; the pipelines,
dispatch and buffer barriers all exist. The pleasant part: `MaxParticles`
defaults to 1000, and anything up to 2048 sorts inside a **single workgroup**
in shared memory -- one dispatch, not the 55-stage global ladder. The draw
then reads an index buffer rather than the pool directly.

### Superseded - the original 6.9 brief

**The one thing in the particle work that is written but not proven.**

Weighted-blended transparency lives or dies on its depth weight function.
`particle_weighted.rvshader` uses McGuire and Bavoil's equation 9:

```glsl
float weight = clamp(pow(min(1.0, color.a * 10.0) + 0.01, 3.0)
                   * 1e8
                   * pow(1.0 - gl_FragCoord.z * 0.9, 3.0),
                   1e-2, 3e3);
```

**Why this needs eyes rather than a test.** The failure mode is not a
crash or a wrong number -- it is a picture that looks fine. Too steep a
curve and distant particles quietly disappear; too shallow and the
nearest fragment dominates everything behind it. Both look like
plausible smoke until compared against the sorted version, and the paper
itself says the constants want tuning per scene depth range.

It has been compared once, on `scenes/particles_oit.rage`, whose whole
depth range is about ten units. That proves the plumbing, not the curve.

**What to actually do:**
1. Build a scene with real depth range -- particles at 2, 20 and 200
   units, ideally the same emitter repeated so only distance differs.
2. Render it three ways: `Alpha` (sorted, the reference), `Additive`,
   and `WeightedBlended`. The alpha version is the truth to compare
   against, since a single CPU emitter *is* correctly sorted.
3. Look for the two named failures specifically. Distant emitters
   thinner than the sorted version means the depth term is too strong;
   near ones washing out what is behind them means it is too weak.
4. If it needs tuning, the constants to move are the `0.9` and the two
   exponents. The paper offers several alternative weights (equations
   7 through 10) trading depth sensitivity against alpha sensitivity --
   try 10 if 9 is too aggressive at range.
5. Record what was compared and what was chosen, here. A weight tuned
   once and undocumented is one somebody re-derives.

Everything under it is verified: per-attachment blend, multi-attachment
graph targets, the transparent pass and its shared depth, and the
resolve. 888 checks, both backends, no validation messages.

### After that - the rest of phase 6

### Picking what comes next

The papercut list is the phase 6 ballot, and what actually hurt while
building a real game, in order:

1. **No game UI or text.** The game cannot say "press F to reset", show a
   score, or put up a title. Everything had to be communicated with a
   light and four sounds -- workable for one mechanic, not for two.
   **Now the START HERE**, agreed 2026-08-11.
2. ~~No per-frame script hook.~~ **Done 2026-08-11** -- `OnFrame` in both
   languages, `OnUpdate` renamed to `OnTick`, and the interpolation alpha
   readable from a script. Deliberately done *before* UI so that UI can use
   it rather than be retrofitted onto it. Knockdown's camera still rides the
   launcher rigidly; it now has somewhere to be smoothed from.
3. ~~No particles.~~ Done -- CPU and GPU, three blend modes, curves, and a
   burst fired from a C# script in Knockdown.
4. ~~Small API wants~~ -- done: `PlayOneShotAt` and pitch on every
   one-shot, both languages, protocol 5.

The one remaining item is phase 6. After it the ranked list is: **6.11 GPU
alpha self-sorting** (bitonic, single workgroup for pools <= 2048, now with a
measured argument from 6.10), then **particle collision** (analytic shapes
first, screen-space second) and **sub-emitters**, then phase 7's deferred
debts.

### Worth knowing before extending scripting further

- **The NativeApi table is append-only and at protocol 7.** Inserting a
  field in the middle rebinds every field after it on one side only; the
  crash lands somewhere unrelated. Append, bump kProtocolVersion on both
  sides (Interop.h and Interop.cs), and the handshake tests follow the
  constant automatically. **Both** constants -- forgetting `Interop.cs` fails
  the handshake check with a clear message, which is the check working.
- **`ManagedApi` is not the same kind of table.** It is bound by *name*, one
  `GetFunctionPointer` per entry, and nothing outside the engine binds it, so
  its members may be renamed and reordered freely; `InvokeUpdate` became
  `InvokeTick` at protocol 6 with no ABI consequence at all.
- **A project's compiled C# must be rebuilt when the class library changes.**
  A stale assembly whose types override a member that no longer exists fails
  to load its types -- loudly, which is the good outcome. Rebuild with
  `dotnet build -c Debug -p:RageVScriptCore=<build>/bin/Debug/RageVEditor/managed/RageV.ScriptCore.dll`
  and copy the output *up* from `bin/Debug/net8.0/` to `Scripts/bin/`, which
  is where the engine looks. The same applies to a project's native module: a
  vtable that gained a virtual is an ABI change, so rebuild it too.
- The C# reload refuses while instances are alive, on both sides of the
  boundary: the editor parks the swap until Stop, and ScriptHost refuses with
  a log line if anything reaches it anyway.
- The unload-verification warning ("the previous script context did not
  unload") firing means a type from the project assembly is being held
  somewhere -- a static, an event, a cache. That is a leak of every version
  ever built, and it is loud on purpose.
- RetireProjectContext is [MethodImpl(NoInlining)] and must stay that way:
  inlined, the JIT may pin its locals to the caller's frame and the collect
  loop spins against the very reference it is waiting on.

### Housekeeping that used to be folklore, now automatic

- **Stale exports.def**: deleting an engine source used to strand its symbols
  in the DLL's export list and fail the link. A pre-build pass
  (cmake/PruneStaleObjects.cmake) prunes objects whose sources are gone and
  the .def with them. The one quirk left is the VS generator's: the first
  build after deleting a globbed source may fail compiling the ghost file;
  the second succeeds. That is CONFIGURE_DEPENDS reloading, not a bug here.
- **Dock layout proportions**: ImGui stores split sizes in pixels; the editor
  rescales the dock tree on window resize and persists the reference size in
  panels.ini, which also remembers which panels are open. Layout, size and
  visibility all survive restarts now.

### Done — Phase 5, C# scripting (`XL`)

**Phase 3 is complete.** Skeletal animation landed with the rest of it: a
skinned vertex format, a skinned PBR shader and a skinned depth shader sharing
the static ones' lighting through includes, a second pipeline in `Renderer3D`,
per-instance bone matrices, and an `AnimatorComponent` that advances a clip and
composes a pose.

`scenes/skinning.rage` in the sample project is the end-to-end check: two posts
side by side, one animated and one in its bind pose. The animated one bends and
**its shadow bends with it**; the other does not move at all, and is
pixel-identical between the two backends.

Phase 5 is the last MVP+ item, and the dependency argument for doing it now is
the strongest one left: C# has to mirror a native surface that has stopped
moving. Seven items in ROADMAP §5.

**5.1 is done.** `RageV/src/RageV/Managed/DotNetHost.h` boots CoreCLR through
hostfxr and hands back function pointers to static managed methods;
`RageVScriptCore/` is the managed assembly; `scenetest` proves the round trip
both ways, including the protocol-mismatch path. Three things about it worth
knowing before touching it:

- **The engine does not link nethost and does not need the .NET SDK to build.**
  `hostfxr.dll` is found at runtime (`DOTNET_ROOT`, then the registry, then
  `%ProgramFiles%\dotnet`) and the four API types are hand-declared. A clone
  still builds with nothing but a compiler; CMake reports C# as unavailable and
  moves on.
- **`EnableDynamicLoading` in the csproj** is what emits
  `RageV.ScriptCore.runtimeconfig.json`. Without that file
  `hostfxr_initialize_for_runtime_config` fails with a number rather than a
  sentence, and it is the usual reason a first attempt at hosting .NET fails.
- **`Interop.ProtocolVersion` is checked on the first call.** A partial rebuild
  that leaves one side stale is otherwise a stack corruption somewhere
  unrelated, minutes later.

**5.2 is done.** `RageV/src/RageV/Managed/Interop.h` is the boundary, and it
uses a different mechanism in each direction on purpose:

- **Managed calling native: a table of function pointers**, handed over once at
  bootstrap. Not `[DllImport]`: at the time there was no `RageV.dll` to import
  from, the engine being a static library. There is one now -- it became shared
  so a project could load a C++ game module -- so the original reason has
  expired, but the table stays. It skips the per-call marshalling stub, and
  binding by name across a boundary that already versions itself with
  `kProtocolVersion` would be a second answer to a question that has one.
- **Native calling managed: `[UnmanagedCallersOnly]` entry points**, reached
  through hostfxr. Static, blittable arguments only, and **no exception may
  leave one** -- an exception crossing an unmanaged frame terminates the process
  rather than unwinding, so each entry point catches at its own edge.

`NativeApi`'s field order *is* the ABI, mirrored field for field in `Native.cs`.
Adding to the end is the only safe edit; inserting in the middle rebinds every
field after it on one side only, and the result is a call through a pointer to
the wrong function. `Interop::kProtocolVersion` and `Interop.ProtocolVersion`
are compared before anything else is called, which turns a partial rebuild into
a sentence rather than a crash somewhere unrelated.

An entity crosses as a **UUID, never a pointer** -- play mode restores a scene
by recreating entities, so a pointer handed to a script dangles the moment Stop
is pressed. The scene binding is a raw `Scene*` for the same reason: managed
code must not be able to keep a scene alive past Stop.

The managed `SelfTest` walks every shape that crosses -- struct in, struct out,
string out, string back including the truncation contract, a float return, an
unknown entity, the log -- and reports each as its own bit. `scenetest` asserts
them individually, because a single pass/fail would say "interop is broken" and
leave the next person to work out which of nine things it was.

**5.3 is done.** `RageVScriptCore/src/Engine.cs` is the class library --
`Entity`, `Script`, `Input`, `Time`, `Log`, `Collision`, `Vector3` -- and
`ScriptHost.cs` instantiates a `Script` subclass by name, drives its lifecycle,
and delivers contacts. `BuiltinScripts.cs` has C# `Spinner` and `Mover` that are
line-for-line comparable with the native ones in `Scripts/BuiltinScripts.cpp`,
which is the clearest available statement that the two APIs are one API.

**5.4 is done.** A generated project scaffolds `Scripts/<Name>.csproj` and an
`Example.cs` that already compiles. **File > Build Scripts** shells out to
`dotnet build`, parses the diagnostics into file/line/code/message, shows them in
a Script Build panel with errors above warnings, and loads the result. Verified
end to end in `scenetest`: a project is created, compiled, and a script type
*from that project* is instantiated by name and stepped.

Three things in there that were not obvious:

- **`cmd` eats the outer quotes.** `_popen` runs through `cmd /c`, which strips
  the first and last quote of a command line beginning with one -- so a quoted
  `C:\Program Files\dotnet\dotnet.exe` arrives unquoted and cmd reports that
  `'C:\Program'` is not a command. The whole line is wrapped in one more pair of
  quotes to give cmd a pair to eat.
- **The project assembly must resolve `RageV.ScriptCore` to the copy already
  loaded**, via an `AssemblyLoadContext.Default.Resolving` handler. The obvious
  alternative -- copying the DLL next to the project's output -- is a trap: two
  files with the same assembly name load as two different assemblies, so the
  project's `Script` is not the engine's `Script`, `IsAssignableFrom` fails, and
  the error says the type is not a Script when it plainly is.
- **The assembly is the evidence, not the exit code.** A build can print nothing
  alarming and still not have produced anything, so success requires the file to
  exist *and* zero errors.

**5.6 is done, and with it a project can have both languages.**
`ManagedScriptComponent` puts a C# script on an entity: a type dropdown of what
the loaded assemblies actually contain, the script's fields reflected out of the
type, and both saved into the scene. `Scene::StepManagedScripts` runs them on
the fixed step, after the native pass so the ordering between the two languages
is stated rather than being whatever the component pools do this build.

Four decisions in there:

- **Fields come from reflecting the type, not from anything stored.** A script
  that gains a field shows it immediately; one that loses a field stops showing
  it with no scene migration. Only values somebody *changed* are stored, so
  editing a default in code reaches every entity that never overrode it.
- **Private fields are editable.** `private float m_Speed = 1.2f;` is how C# is
  written, and it would otherwise be the one thing nobody can tune. Unity needs
  a `[SerializeField]` attribute for this; not requiring one is fewer concepts
  for the same result. `readonly` and `static` are skipped.
- **Values cross as text, in the invariant culture.** The scene file is text
  anyway, and a tagged union would be two languages' worth of upkeep on
  something that happens when a person types. Invariant specifically: a machine
  with a comma decimal separator would otherwise write `1,5` into a scene that
  then loads nowhere else.
- **The handle is not copied and not serialized.** Components are copied when a
  scene is duplicated, which is what play mode does on every press of Play, and
  two components owning one managed instance would both destroy it.

**C++ scripts have editable fields too**, declared at registration because C++
has no reflection:

```cpp
RV_REGISTER_SCRIPT(Spinner).Field<&Spinner::Speed>("Speed");
```

The member must be **public** -- a registration names it from outside the class,
and reaching a private one needs a friend declaration in every script. C# has the
opposite rule, and for a good reason: reflection can read private fields, and
demanding `public` there would mean telling people to write worse C# for the
inspector's benefit. Both languages converge on one set of inspector rows, one
set of stored values, and one scene format.

The script component draws its own rows rather than using the generic field
list, and **`desc.Fields` is deliberately empty for both script components**.
Listing `ScriptName` there produced two "Script" rows -- one of them a plain text
box that could name a script which does not exist. The name is still written
under the same YAML key by `SerializeExtra`, so scenes from before that change
load unaltered.

Rows use `BeginField`/`EndField` like every other component: label in a
fixed-width left column, widget filling the rest. **New Script...** is the last
entry of the script dropdown rather than a button beside it -- choosing a script
and making one are the same decision. The popup cannot be opened from inside the
combo (the combo closes and takes it with it), so the entry sets a flag and the
popup opens after `EndCombo`.

A **Language** row (C++ / C#) converts the
entity between the two components. The name and field overrides are deliberately
*not* carried across: two scripts that share a name are still different scripts,
and moving one's tuning values onto the other applies numbers to fields that only
coincidentally match.

**New Script...** writes a working template, never a blank file. For C# it goes
into the project's `Scripts/` and is selected immediately. For C++ it goes into
the engine's own `Scripts/` folder -- and only when the editor was built from
source, which it knows via `RV_ENGINE_SCRIPTS_DIR`; a packaged editor explains
what to do by hand instead.

> [!NOTE]
> **This used to need `/WHOLEARCHIVE`, and no longer does.** A static library
> drops any object file nothing references, and a translation unit whose only
> contents are a static registrar -- exactly what a script file is -- has no
> referenced symbol. The script compiled, linked, and never appeared in the
> dropdown. There was no fix on the library side either: a
> `#pragma comment(linker, "/include:...")` lives in the object file being
> discarded, so the directive never reaches the linker. That was tried first.
>
> The engine is a DLL now, linked from its object files rather than from an
> archive, so every translation unit is in it whether or not anything calls into
> it. The same will be true of a project's game module. Worth knowing anyway:
> the failure returns the moment script code lands in a `.lib` again.
>
> `Scripts/TemplateProbe.cpp` is the generated template kept in the tree so every
> build compiles and registers it. It is what caught this, and it is what would
> catch it again.

**C++ scripts still have to be compiled into the engine or game binary**, so a
project cannot add one without rebuilding the engine. That is the remaining
asymmetry between the two languages. The machinery works end to end and
there is no way to attach it: no managed script component, no inspector entry,
the editor could not put one on an entity. 5.6 fixed that; see below.

C++ scripts have their own limit worth stating in the same breath: they must be
compiled into the engine or game binary, so a *project* cannot add one without
rebuilding the engine.

**5.0 was completed before 5.2, as planned.** No third-party type in a public
header, and the public API segregated into `RageV::Math::`, `RageV::Audio::`,
`RageV::Physics::` and so on. Binding C# against `glm::vec3` and then renaming it
means writing the interop, the marshalling and the class library twice — which
is the exact failure the "C# last" argument was meant to avoid, arriving through
a different door.

**5.0 landed, and went further than planned.** `RageV/src/RageV/Math/` is the
engine's own maths: `Vec2/3/4`, `UVec4`, `Mat3/4`, `Quat`, the operators inline
in `Functions.h`, and inversion, decomposition, projections and the whole
quaternion set in `Math.cpp`. **glm is not linked into the engine at all.** All
31 public headers and 35 `.cpp` files were migrated off it.

**The thing that makes that defensible is `tools/scenetest/GlmBridge.h`.** glm
is still vendored and still linked — into `scenetest` only — purely as an
oracle. 34 checks compare every operation against it on deliberately awkward
values, because axis-aligned test numbers pass through a transposed matrix
unharmed. Delete that block and the engine is running unverified arithmetic in
the hottest path of a renderer. It is the single most important test in the
suite and the least obvious one.

Four decisions in there worth not re-litigating:

- **Conversions in the bridge are member-wise, never a `reinterpret_cast`.** The
  layouts match and casting would work, but glm has shipped quaternions both
  `w`-first and `w`-last depending on version and build flags, so a cast that is
  right in one configuration rotates everything wrongly in another.
- **`Normalize` guards against zero length**, which glm does not. glm returns
  NaNs, a NaN in a transform takes the object and every child with it, and
  nothing reports anything. There is a check for it.
- **Right-handed, clip-space depth in `[0, 1]`.** Written at the top of
  `Math.cpp` because nothing else defines it any more. The textbook `[-1, 1]`
  projections differ in one column and still produce a picture.
- **`Math::Max` is float-only.** Integer maxima are `std::max`'s job.

**What is left of 5.0:** the domain namespaces — `RageV::Audio::`,
`RageV::Physics::`, `RageV::Assets::`. `RageV::Math::` and `RageV::RHI::` are
already in that shape. Watch for `Renderer`, `Scene` and `Input`, which exist as
*types* at the scope those namespaces would occupy.

### After MVP+ — phases 6, 7 and 8

Added 2026-08-09 at the owner's direction. **None of them blocks anything**, and
they are not in dependency order, because past MVP+ there is no spine left to
respect. ROADMAP §5 has the detail; the shape is:

- **Phase 6 — text, UI and particles.** The only remaining gap a *player* would
  notice rather than a developer. MSDF text, a screen-space canvas that is not
  ImGui, widgets, and CPU-simulated 2D/3D particles with a measurement before
  any compute pass.
- **Phase 7 — the deferred debts, collected.** Archive format and the virtual
  file system it needs, asset cooking, materials as assets, billboard icons,
  the animation blend nothing currently drives, skinned bounds that cover the
  animation, per-object probes, depth sorting, SMAA and TAA.
- **Phase 8 — formerly out of scope, reopened.** GI, bindless, GPU-driven
  rendering, terrain, navigation, networking, other platforms, XR, FBX, visual
  scripting, a plugin ecosystem. Each carries the reason it was excluded as its
  cost. **Two have prerequisites that are decisions rather than work**: bindless
  means dropping OpenGL or maintaining two binding models, and terrain means
  deleting `experiments/terrain/Chunk` first, which makes one entity per voxel
  face.

**The recommendation in ROADMAP §9 is to build a game before starting Phase 6.**
Forty-odd items with no ordering is a menu, and needing one is the only reliable
way to know which.

### Smaller, none blocking

- **Materials as assets**, so two entities can share one from the inspector.

Four entries that stood here are done, and two of them were wrong about what
they needed -- kept as a note rather than deleted, because the errors are the
useful part:

- **Billboard icons** and **per-object probe selection** shipped as 7.4 and
  7.7.
- **SMAA** was listed as needing "two lookup textures vendored". It does not.
  Its AreaTex answers "what fraction of this pixel did the line cover", which
  is a trapezoid -- about fifteen lines of arithmetic that a modern GPU runs
  for less than the dependent texture fetch it replaces. 179 KB of vendored
  data was avoided by deriving the thing it encodes (ENGINE-NOTES 7n).
- **TAA** was listed as `L`, needing motion vectors, a jittered projection and
  a history buffer. All correct, all built, and the estimate was right about
  the size and wrong about where the difficulty sits: none of the three parts
  was hard, and every real defect was in *what could see them* -- a check
  suite that never moved the camera, and a neighbourhood clip that hides
  reprojection errors wherever the neighbourhood is uniform (7r).

## 9. Known rough edges

Everything wrong or annoying that is not already a caveat in §6. Written down
because the alternative is someone finding each one by being confused.

### Correctness

- **Asset handles minted in the build output are lost on a clean build.** The
  assets root is the folder beside the executable, which CMake copies from the
  source tree. Assets added to the *source* tree keep their handle because the
  `.meta` is copied with them; assets dropped into `build/` do not. Recorded in
  `AssetRegistry.h`.
- **A script attached while playing is discarded on Stop.** Correct snapshot
  semantics, surprising the first time.
- **Nothing culls by distance.** A mesh behind the camera is skipped; a mesh a
  kilometre away that is two pixels across is drawn in full.
- **RageV implements its own math, and it costs nothing. Measured properly, on
  the third attempt.** The method matters more than the number here.

  The first comparison said **+3.0%** and was wrong. The two sets were taken
  forty minutes and several rebuilds apart; re-running the *unchanged* baseline
  later gave 2.275 ms against its own earlier 2.246 ms, so the machine had
  drifted +1.3% with no code involved. The owner called that as thermal drift
  before the data did.

  Settled by building each version, copying it aside, and alternating runs so
  drift hits both equally. Eight pairs, 600 frames each, Release, Vulkan, vsync
  off, no compilation between runs.

  Delegating every operation to glm, against glm at the call sites:

      paired difference  +0.021 ms (+0.9%),  sd 0.035,  t = 1.69 on 7 df
      the delegating build was faster in 3 of 8 pairs

  RageV's own implementation, against glm at the call sites:

      paired difference  +0.013 ms (+0.6%),  sd 0.087,  t = 0.44 on 7 df
      the native build was faster in 4 of 8 pairs

  Neither is significant at p < 0.05. **Anything under about 1.5% on this scene
  is below what this method can resolve**, and a difference measured across
  separate sessions is not a measurement at all — it is the machine's mood.

  Three arrangements were tried and all three measure the same: glm at the call
  sites, glm behind an out-of-line wrapper, and no glm at all. The choice
  between them was therefore never a performance question, which is worth
  knowing before anyone re-opens it.

### Performance, all unmeasured### Performance, all unmeasured

- **Both applications still ship with `vsync = on`**, which is right for a game
  and wrong for a measurement. Pass `--vsync=off --benchmark=N`; the report
  refuses to be quoted without its vsync line.
- **The editor renders shadows twice** when the game viewport is open, once
  fitted to each camera. Correct, and twice the cost.
- **A realtime probe re-runs the GGX prefilter every sixth frame**: 36 small
  renders, amortised to six a frame. Fine for one probe, untested for several.
- **Opaque geometry is grouped, not depth-sorted.** Draws are sorted by mesh
  and material so they batch; within a batch the order is arbitrary, so early-z
  does less than a front-to-back sort would. Worth a measurement before it is
  worth code.
- ~~Vulkan's remaining 1.88 ms is unattributed.~~ **Attributed 2026-08-12**,
  once GPU timestamps existed to do it with. See section 8; the short version
  is that nothing dominates -- 0.92 ms of GPU in a 1.42 ms frame, spread
  across shadows, probes and the graph.

### Editor and tooling

- **Running any application without `--screenshot` opens a window and waits.**
  That is correct behaviour and not a hang, but it will look like one in a
  script. Every verification run should pass the flag.
- **`Sandbox` is gone (2026-08-21).** It targeted the legacy renderer, and
  five of the seven things it included -- `RageV.h`, `Entrypoint.h`,
  `Texture2D`, `FrameBuffer`, `OrthographicCameraController` -- no longer
  exist, so the option that built it could not have succeeded.
- **The editor UI does not scale itself.** `--ui-scale=N|auto` exists and
  defaults to 1.0; `auto` follows the monitor. Deliberate — on a 150% display
  auto gives a 27px font, which is what the OS asks for and larger than anyone
  wanted. There is no per-monitor handling and no live reload; it is read once
  at startup.

### Housekeeping

- **`.meta` files belong in version control.** They are the identity, and they
  are tracked — checked, not assumed.
- `Chunk`/`Perlin` are parked in `experiments/terrain/` and not built.
- **`tools/scripts/make_sky_hdr.py` rebuilds the sample's sky, but not
  identically.** Whole-frame mean luminance 146.0 against the original's 149.1,
  upper sky 203 against 208, horizon falloff 5.9 per row against 4.2. Nobody
  has the original generator, so if the sample's `sky.hdr` is ever regenerated
  the scene will look slightly different and the shadow and probe tuning should
  be re-checked rather than assumed.
- The sample scene's shadow distance, bias and probe placement are tuned for
  that scene and are not general defaults.

---

## 10. What went wrong, and what it cost

Kept because each of these was expensive to find and cheap to prevent, and
because the pattern in them is more useful than the list.

### The test that disarmed itself

Worth its own heading, because it is the most dangerous shape a defect can take
in this repository and it very nearly shipped.

The glm migration was done with a substitution script. It rewrote `glm::vec3` to
`Vec3` everywhere — including inside the `scenetest` block whose entire job is
comparing `RageV::Math` against glm. Twenty-eight checks were left comparing the
answer to itself. **They passed.** The suite reported OK, and would have reported
OK forever, while covering nothing.

The same suite is what caught the argument-order difference between
`glm::decompose` and `Math::Decompose`, which would otherwise have silently
swapped position with scale in the glTF importer and the physics body sync. So
the disarmed test was guarding a real bug at the time it was disarmed.

Found by reading the diff, not by anything automated. Nothing in the build could
have caught it: the code compiled, the checks ran, the count did not change.

**The rule that came out of it:** a test whose value comes from comparing against
something external must say so in a comment, and anything that rewrites the tree
must exclude it. `Functions.h`, `Math.cpp`, `GlmBridge.h` and the block itself
all now carry that warning.

**The general form** — and this is the part worth remembering — *a check that
passes because it checked nothing is worse than no check at all*, because it also
consumes the attention that would have gone to writing a real one. `rvdoc` fails
when it parses suspiciously few members for the same reason.

### A 3% regression that was not there

The math migration appeared to cost 3% of a CPU-bound frame. It did not; the two
measurements were taken forty minutes and several full rebuilds apart, and the
machine had drifted. The owner said so before the data did.

Re-running the *unchanged* baseline later gave a different number from itself.
The fix was to build both versions, copy them aside, and alternate runs so drift
hits both equally — at which point the difference fell to +0.6% with the new
version faster in half the pairs, which is noise.

**The rule:** a benchmark comparison is only valid if both sides are measured in
the same session, alternating, with no compilation in between. §9 has the method
and the numbers. Sequential A-then-B on this machine cannot resolve better than
about 1.5%.

### Bugs that shipped and were found later

| Bug | How long it hid | Why |
|---|---|---|
| Vulkan post passes sampled with V flipped | A phase and a half | An even number of passes cancels it; the bloom chain has an odd number, so only bloom was mirrored — invisible until something was bright enough to bleed |
| OpenGL never called `glClipControl` | Since the Vulkan port | Depth landed in `[0.5, 1]`, so every shadow comparison passed. Nothing sampled a depth buffer as data until shadows did |
| The sphere primitive was wound inside out | Four roadmap phases | Back-face culling kept the far hemisphere and drew its inside. Same silhouette; nothing read the normal closely enough |
| Both cylinder caps were flipped | Four roadmap phases | Same. It was a tube and nobody looked down it |
| Mip-generation barriers named the wrong stage | Since the port | Nothing in the project had a mip chain until environment maps did |
| GL's copy framebuffers kept stale attachments | One feature | A mismatched attachment size is legal in GL 4.5; the blit region silently became the intersection |
| Seven modules logged "ready" unconditionally | Unknown | A failed shader compile produced a feature present in every sense except that it did nothing |
| A depth attachment was not `TransferSrc` | Until first use | Nothing had ever copied one |
| A probe's mips were built before its faces were drawn | Since probes landed | `GenerateMips` submits its own buffer and waits; the faces were still unsubmitted in the frame's. Healed itself six frames later, so it read as a warm-up rather than a bug |
| The Vulkan descriptor pool was a fixed 2000 sets | Since the port | Twelve objects never reached it. A thousand segfaulted |
| Every material built its own sampler | Since materials existed | Cost nothing visible until draws were keyed by bound state, then silently prevented all batching |
| The swapchain was destroyed before its replacement existed | Since the port | `vkDeviceWaitIdle` covers the queues, not the presentation engine. Only crashed when the compositor happened to be a frame behind, so it survived every resize until a present-mode change made it frequent enough to notice |
| OpenGL had no fence between frames | Since the port | The CPU only laps the GPU once a frame is cheap; before instancing it never got there. ~1% of pixels at delta 20/255 reads as shimmer, not as corruption |

### What actually catches these

1. **Compare the two backends' frames against each other.** Three separate
   bugs this session were each *internally* clean on both backends while the
   two disagreed. Per-backend verification cannot see that, and it is the
   single highest-yield check there is.
2. **Verify by exiting, not killing.** A killed process runs no destructors.
3. **Check the pixels.** A draw count cannot tell a rendered frame from one
   cleared afterwards.
4. **A convention documented as "these cancel" deserves a test.** That exact
   comment was repeated in six shaders and was false in one backend.
5. **Content that is too dim hides renderer bugs.** The mirrored bloom needed
   an HDR sky to become visible. Test scenes should stress the renderer, not
   only demonstrate it.
6. **Test the test.** The subsystem-readiness check was verified by breaking a
   shader on purpose and confirming the run went red — not by watching it pass.
7. **A flag can be tested; a log line cannot.** Anything worth announcing is
   worth exposing as state.
8. **A setting that works one way round is not a working setting.** Vsync off
   at startup was fast and vsync off at runtime was not, and the shared code
   path made "the OS decides" sound obvious enough to write down as a
   conclusion. It was wrong, and it was wrong for two sessions. Varying the one
   thing that had never been varied -- the present mode itself -- took ten
   minutes and answered it.
9. **Read the whole log, not the tail.** `--benchmark` prints its summary last,
   so `| tail` showed the numbers and hid seven validation errors underneath
   them. They had been printed on every benchmark run for as long as the flag
   had existed.

### Mistakes in the work itself, not in the code

- **Fixing the instance and describing the pattern.** The readiness bug was
  fixed in one module while the same words — "this is a bug generator" — were
  being written about it, and the other six were left. Scope a fix to the
  pattern that was named, or do not name it.
- **Writing a test that asserted something false.** Two of them: "each
  direction is dominated by the face it points at" is not true of a correct
  irradiance convolution, and the cube-face edge test paired the wrong two
  edges. Both were the test being wrong, not the code — worth checking which
  before changing anything.
- **Reporting a draw count as if it were a speed-up.** 144 draws became 60,
  which is real; the frame time barely moved, because both applications ship
  with vsync on and the panel was the limit. The number that was measured and
  the number that was implied were not the same number.
- **Tuning two variables at once.** The shadow bias was reduced at the same
  time as front-face culling was removed, so the contribution of each is not
  separable from the screenshots.
- **Writing a justification for a gap instead of closing it.** The prefilter
  refused any environment under 64 pixels a face, which is every gradient sky,
  and the comment beside the guard called that "a fine answer". It was the bug,
  with a rationale attached. Probe cubes were left box-filtered the same way,
  recorded as a deliberate trade — which is worse, because a decision is harder
  to notice than an oversight.
- **Listing defects instead of fixing them.** Several rounds of this session
  produced accurate inventories of what did not work, in place of work. An
  honest list of gaps is not a substitute for closing one.
- **A draw count is not a frame time, second time.** Instancing cut draws 3238
  to 854 and the frame got *worse*. The number that was easy to measure moved
  and the number that mattered did not — the same shape as the culling claim,
  caught this time only because the frame time was measured alongside it. The
  real fix was elsewhere entirely (a sampler per material), and finding it
  needed the reduction to be checked against a clock rather than celebrated.
- **A bug that heals itself reads as a warm-up.** The probe's black metal for
  six frames looked like the probe filling in, which is a thing that legitimately
  happens. It was reported by a person watching, not by any test, and it took a
  per-frame luminance measurement to show the transition was a hard step at
  frame 7 rather than a fade.
- **When a timing bug will not reproduce, go looking for the known hazard of
  that shape.** The vsync crash survived 25 forced toggles under validation in
  both build configurations. Failing to reproduce it was itself the evidence:
  a deterministic bug would have fired on the first one, so the search moved
  from "what did I break" to "what in this path is order-dependent" -- and
  swapchain teardown had the textbook mistake sitting in it. Confirmed fixed by
  the person who could reproduce it.
- **Ask whether a defect predates the change.** The OpenGL flicker was found
  while verifying instancing and looked like its fault. Stashing the work and
  rebuilding took ten minutes and showed it was already there — which is the
  difference between a regression to revert and a bug to schedule.
- **Reporting a fix by what was logged rather than by what changed.** The
  readiness work was presented as six modules' log lines when the actual fix
  was three new flags and a test. A log line cannot be tested and is not a fix.

