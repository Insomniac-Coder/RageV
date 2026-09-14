# RageV — measured change: replacing RT-5's anti-lag

**Design, 2026-09-14, for the owner's go before anything is built.** Owner's decision
(2026-09-13): *"Measure real change instead of guessing. Why guess when you can measure."*

## In plain words

The ray-traced lighting is noisy, so its filters average every pixel over many frames. When
the light really changes -- the car's lamps switched off -- that average takes seconds to
catch up (RT-16: 95% of the lamps' light gone only after 186 frames). The anti-lag tried to
*guess* a change from one noisy frame, and guessed wrong constantly: faster fades, but a still
picture that never settled (RT-5 part 4's record).

This measures it instead. Each frame, for about one pixel in nine, the lighting is computed a
second time with **exactly last frame's inputs** -- the same point on the surface, the same
random numbers, the same viewpoint -- but in **this** frame's scene. If nothing in the scene
changed, the answer is identical to last frame's to the bit, so the difference is exactly
zero: noise cannot show up in it. If a lamp went out or a shadow moved, the difference is that
change and nothing else. Those sparse differences are spread into a smooth "how much did the
light change here" map, and the filters shorten their memory where the map says so -- only
there, and only by as much as it says.

This is the published method behind the best real-time denoisers (A-SVGF: Schied, Peters,
Dachsbacher, *Gradient Estimation for Real-Time Adaptive Temporal Filtering*, 2018), adapted to
this engine's signal contract.

## What has to be true for "same inputs" to mean the same answer

Measured against the code as it stands (2026-09-14):

| input | today | what the design does |
|---|---|---|
| **the surface point** | rebuilt each frame from the G-buffer, *through this frame's jitter* -- a different sub-pixel point every frame | **stored**: last frame's chosen sample's world position, normal and material are kept in a small record and re-lit from there, never re-read from the G-buffer |
| **the lamp picks** | `direct_trace` hashes `(pixel, frame salt, reservoir)` and each candidate's **array index** | the record keeps the pixel and frame; the hash takes a **stable light id** instead of the index (next row) |
| **light indices** | `Scene::CollectLights` drops zero-intensity lights, so switching one lamp off **shifts every later light's index** -- every pixel lit by those lights would draw different picks and read as "changed" | **(as built, 2026-09-14: the owner ruled out changing the lighting)** the trace keeps keying its draws on the index, untouched. Each `LightRenderData` carries its entity (`Id`, never uploaded); the record keeps the upload's id order, and the re-light reads a per-frame map from this frame's index to the index the same lamp had last frame (`u_ChangeRolls`), so its draws are last frame's draws. A lamp new this frame gets an index no lamp had. |
| **the soft-shadow sample** | `TraceShadowSoftFromMasked` reads `gl_FragCoord` and the frame counter directly | both become parameters, so a pass running at a different resolution can ask for last frame's pixel and frame |
| **the candidate lights** | `ClusterCellFor(P)` uses this frame's camera froxels | unchanged for a still camera (the case RT-16 is about); under camera motion the cell's list can change and the re-light differs by resampling noise -- handled in "Hazards" |
| **the viewpoint** | `V` from this frame's camera | the record stores last frame's eye, so a view change is not measured as a lighting change (view change is the motion tests' job already) |

## The passes (direct light, phase 1)

All at **stratum resolution**: the frame cut into 3x3 blocks, one sample per block
(533 x 300 at 1600 x 900).

1. **Record** (after `DirectTrace`, this frame). For each block, one pixel chosen by a hash of
   the block and the frame. From the G-buffer and the fresh traced pair at that pixel: world
   position, octahedral normal, roughness (both), metallic, albedo, specular, occlusion, the
   static flag and field weight, the eye, the pixel coordinate and the frame, and the
   luminance of the fresh diffuse and specular it produced. Full floats (a half-float step at a
   kilometre is half a metre -- RT-5 part 3's lesson). Four RGBA32F lanes, kept as a history
   pair (this frame's record is written while last frame's is read): about 20 MB at
   1600 x 900.
2. **Re-light** (next frame, before `DirectAccumulate`). For each block, last frame's record run
   through **the same estimate function the trace uses** -- `direct_trace`'s per-point body
   lifted into `EstimateDirect(DirectPoint, pixel, frame)`, so there is one copy of the lighting
   and the two cannot drift -- against this frame's lights and acceleration structure.
3. **Gradient.** Per block: `delta = L_now - L_then` and `norm = max(L_now, L_then)`, diffuse and
   specular apart. Invalid (no information, which is not "no change") where the record had no
   surface, where its surface was moving last frame, or where its point is off screen now.
4. **Filter.** A-SVGF's: a few a-trous iterations over the block grid, edge-stopping on the
   records' normals and depths, accumulating `|delta|` and `norm` separately, then
   `lambda = clamp(sum |delta| / sum norm, 0, 1)`. Sparse single-block spikes (one resampled
   pick) are outvoted by their neighbours; a lamp going out moves every block it lit.
5. **Use.** Read by texel, upsampled from the block grid with the depth-aware rule the
   signals already use:
   - **the direct accumulate**: its blend weight becomes `max(1/memory, lambda)` -- continuous,
     no reset, no touching the moments the bound needs;
   - **the temporal resolve**: RT-16 measured that TAA's still feedback and the accumulator hold
     the lag *together* (111 frames, 31 with both off), so the resolve's feedback moves toward
     the moving feedback by `lambda`, and its moments forget by the same amount.

Then **the anti-lag goes**: `--anti-lag`, `--anti-lag-floor`, `SignalAntiLag*`, and both shader
blocks (`reflection_accumulate`, `taa_resolve`).

## Phases

1. **Direct light** -- the owner's case (the car's lamps, the tubes' switch). Everything above.
2. **Reflections** -- the lamps' reflection on the wet floor lingers in its own accumulator. The
   record keeps the lobe sample and hit; the re-light re-shades the hit with this frame's
   lights. Same map format, its own lambda.
3. **GI** -- the bounce of a switched light. Same pattern on `rtgi_trace`.
4. **AO is not measured**: it changes only with geometry, which the motion tests already cover.

## Cost (estimates; measured with the palindrome before any default changes)

- Re-light: a ninth of `DirectTrace`'s 2.3 ms in the garage, about 0.26 ms. Record, gradient and
  filter at block resolution: about 0.05 ms. Memory about 20 MB.
- Phases 2 and 3: about 0.25 ms and 0.3 ms more.

## How it is judged

Nothing becomes a default until all of these are shown to the owner as pictures and numbers:

- **The lights button** (`stage_run.lamps_off_scene`, `rt5_fade_analyse.py`): frames until 95% of
  the lamps' light has left the floor, against 186 today. Target: a handful.
- **The parked still, where the anti-lag failed**: per-frame change and the 64-frame mean equal to
  today's within run-to-run noise, garage and bridge -- a measurement that fires on nothing when
  nothing changes. The probe view of `lambda` on a still frame should be black.
- **The moving cube and the dolly** (`rt20_measure.py`): no new noise, no new trail.
- **The bridge's beacons**: their glow should now end with the flash; the water must not sparkle
  more.
- `--validation=on` clean and scenetest green on both backends.

## Hazards, and what each gets

- **Light indices shift on a switch** -> stable ids (above). Without them a single lamp switching
  off would mark most of the frame as changed.
- **A removed candidate changes the other picks in its cell** -> unavoidable with reservoir
  resampling, and confined to cells that held the switched light -- which is where the light
  changed anyway. The filter absorbs the resampling part.
- **Camera motion changes cluster membership** -> the re-light can differ by resampling noise
  under motion. First build: `lambda` is weighted by the pixel's stillness, because under motion
  the memory is already short (the smear cap) and the lag RT-16 describes is a parked-camera
  defect. Revisit if moving-camera lag shows.
- **A record on a moving object** re-lit at a point the object has left -> marked invalid from
  the velocity lane when the record is written.
- **The first frame, a camera cut, a resize** -> no previous record, no gradient, `lambda = 0`:
  exactly today's behaviour.

## Decisions (owner, 2026-09-14)

1. **Both filters**, phase 1 started.
2. **Nothing about the lighting may change** -- so no stable ids in the trace; the index shift is
   handled inside the check alone (the table above).

## Phase 1 as built (2026-09-14)

- `--measured-change=on|off` (off by default), `--change-iterations=N` (4), `--change-floor=x`
  (0.02), `--debug-view=change`.
- `direct_trace.rvshader` compiled twice more, only when the flag is on: `RV_DIRECT_RECORD` (one
  pixel per 3x3 block, a hash of block and frame; position rebuilt exactly as the trace rebuilt it,
  the G-buffer's surface/albedo/id texels, the trace's luminance, a moving flag from the velocity
  lane) and `RV_DIRECT_RELIGHT` (last frame's record through the trace's own lighting code with the
  draw keys -- pixel, frame, animated, eye, lamp numbering -- redirected by macros that spell the
  trace's own text when not re-lighting).
- `change_filter.rvshader`: signed a-trous passes over the block grid (normal and plane
  edge-stops), the last one dividing into shares.
- The accumulate and the resolve: where a share `s` of the light changed, the frame count becomes
  at most `1/s` (the new frame counts for at least `s`, the average restarts from there).
- Runs only while the camera matrix is bit-identical to the record's; otherwise the frame is
  exactly today's.

## Phase 1 measured (2026-09-14)

Scripts: `tools/scripts/garage/session_2026_09_14/mc_measure.py`, `mc_bisect.py`, `mc_cost.py`.
Arms: `base` today's binaries (rendered before the build), `off` the new build without the flag,
`on` with `--measured-change=on`.

- **Off is today.** Bridge Deck 136-199: identical texel for texel (`off2`; the first run after a
  rebuild differs by 1-4 levels on ~0.1% of pixels and the next does not -- the bisect staged each
  edited shader's old text and all three came back identical, so it is not a shader). Garage park:
  identical up to its known frame-168 nondeterminism.
- **A still garage with the check on is today** to within 2 levels, inside that same noise. The map
  (`--debug-view=change`) is black on every parked frame.
- **The lights button** (`rt5_fade_analyse.py mc_fade_base mc_fade_on 470 899 8.3`): at the first
  frame drawn with the lamps off, 27% of the lamps' light is left against 85% today; half gone at
  0 frames against 42, three quarters at 1 against 89; 95% at 118 against 186. The map on that frame
  covers the headlamps' pool (diffuse) and their shine on the wet floor (highlight). **What is left
  is the reflection layer** -- `--debug-view=reflection-picture` is identical off and on and still
  holds the lenses' image fading: phase 2.
- **Bridge, check on:** the map fires only when the tower beacons ramp (frames 62-64, 81-83,
  182-185, and a few blocks at 3, 21-29, 122-125, 141-148), as a speckled field over the surfaces the
  beacon reaches -- the per-block estimate of a lamp seen through one soft-shadow ray and a
  reservoir. Frame-to-frame change in quiet frames is identical to off; over a flash (180-199) the
  middle third changes 0.870 levels a frame against 0.852 (pixels over 4 levels +7%). Replaying last
  frame's picks (the choices pass) removed the other lamps' resampling but not this; a wider or
  support-weighted filter is the lever if it shows.
- **Motion:** the dolly is identical to base on 100.000% of pixels (the check waits for a still
  camera). The chrome cube: 0.022% of pixels differ by >4, nearer the truth there (36.42 against
  37.74, 9413 pixels 8+ levels better, 5626 worse); edge flicker identical; behind the cube the error
  is 0.1 level higher at 1-20 frames since it left (its shadow now followed).
- **Cost** (garage, `--benchmark=240`, palindrome): the passes 0.598 ms -- re-light 0.269, choices
  0.173, record 0.080, filter and map 0.075; frame 14.879 +- 0.043 off, 15.023 +- 0.364 on.
  Memory about 47 MB at 1600x900 (record and choices 8 RGBA32F lanes on the block grid, kept as
  pairs, 41 MB; the re-light's targets). Both above the estimate; the two record passes can be one,
  and the pairs could be single targets read before they are written.

## Cost cut (2026-09-14, owner: "cut the cost first")

- **Skipped when nothing it depends on changed.** `DirectChangeKey` (Renderer3D.cpp) hashes the
  lamps and their cull records in upload order, the cluster, grid and field blocks, the camera, and
  `RayShadows::GetGeometryKey` (every ray instance's transform, structure, index, mask, cutout, and a
  fresh value on any frame a skin was posed). The re-light compares it with the key the record was
  taken under and draws nothing when equal -- the answer could only be "no change" -- and the record
  is kept rather than retaken. The passes stay in the graph and draw nothing; the accumulate and the
  resolve are handed no map (`RelitThisFrame`).
- **One record, not a pair**: the re-light reads it before the record pass (which loads it,
  `RGLoad::Preserve`) may overwrite it. **The choices merged into the record**: one pass, eight lanes.
- **The record waits for a still camera** (the camera equal to last frame's), so a moving camera
  pays nothing.
- Measured, garage parked, palindrome: the check's passes **0.019 ms** (was 0.598); frame time
  within its noise. Memory about 30 MB (was 47). Every earlier result reproduced: the lights button
  27.4% left on the switch frame, 95% at 118; still garage identical to base (1 frame, 1 level --
  its known nondeterminism); bridge quiet frames identical, +2% change over a flash; cube and dolly
  as before. `--validation=on` clean.

## Phase 2: the traced reflections (2026-09-14)

- `reflection_trace.rvshader` under `RV_REFLECTION_RECORD` / `RV_REFLECTION_RELIGHT`. Simpler than
  the direct light: the ray's direction is `GlossyReflectionLD(N, V, roughness, texel, frame)` -- a
  sequence, not a hash -- and a hit is lit by walking its lamps with hard shadows, choosing nothing,
  so the record keeps only the point, the G-buffer's surface and albedo texels and the luminance
  written, and the re-light traces the same ray again. Four lanes. Its key adds every ray instance's
  material (colour, glow, surface, cutout): a lens that stops glowing changes a reflection with no
  lamp moving.
- **The accumulator's alpha is two things, and phase 2 split them.** The lit shader reads it as its
  trust in the traced picture against the probe, fading the probe out over four frames of a young
  history (`reflectionsForScene.Intensity` 1/4). A measured restart set it to one, so the probe --
  captured with the lamps on -- came back for a frame (the fade went 20.0% -> 27.5% on frame 1) and
  the temporal resolve held that for a second. Now, with the check on, the blend's own count rides
  in the id lane's green and the alpha keeps its trust; off, nothing changes.
- The lights button with both phases (`mc_fade_on4`): 20.0% left on the switch frame, 17.6% the
  next, 50% and 25% gone at 0 frames, 10% at 15 (was 145 today, 64 after phase 1), 5% at 63 (186,
  118), 2% at 130 (240, 180). The map (`--debug-view=reflection-change`) fires on the switch frame
  only, on the two spots under the headlamps and the lamps' images in the car and the pillars.
- **What is left is the bounce** (`--capture-signals`, 20 frames after the switch against the end):
  the GI history holds 44% over its settled value; the direct diffuse 1.1% (partial shares restart
  partially); the reflections 0.3%; the resolve a little on the lenses. Phase 3.
- The moving cube: pixels differing from base 0.128% (the cube's reflections followed), nearer the
  truth where they differ (71.36 against 73.80, 58248 pixels 8+ levels better, 27465 worse); the
  dolly identical.

## Phase 3: the traced bounce (2026-09-14)

- `rtgi_trace.rvshader` under `RV_GI_RECORD` / `RV_GI_RELIGHT`, on the trace's own grid (half
  resolution in the garage), three lanes: the world point with the texel's column and flags, the
  G-buffer's surface texel, and the luminance, ray count and row. The seed is the trace's hash of
  texel and frame; the ray count is the tile allocator's, kept in the record. Its key adds the
  emitters, the emitter tables, the probes and the bounce settings.
- **The record lights its own point.** A half-resolution texel's centre lands on the corner of four
  full-resolution depth texels, so which surface the trace snapped to is not recoverable from the
  record; reading the trace's output made the forced map speckle. The record now runs the same
  estimate at the point it stores, and the re-light compares against that.
- `--change-force=on`, still garage: all three maps black.
- The lights button (`mc_fade_on5`): 19.3% left on the switch frame, **10% at 10 frames** (145
  today, 15 after phase 2), **5% at 44** (186, 63), 2% at 106 (240, 130). The bounce's map
  (`--debug-view=gi-change`) fires on the switch frame. What is still left at 5% has not been
  looked at (`--capture-signals` 20 frames after the switch is the tool).
- Everything else as after phase 2: still garage 22 of 40 frames differ from base, worst 10 levels,
  the known frames 168-170 (phase 2: the same); bridge quiet frames identical to off, +2% frame
  change over a flash (the same numbers -- no traced bounce there); cube 0.117% of pixels differ
  (0.116%), its trail numbers unchanged; dolly identical (the camera moves and the check waits).
- Cost, garage parked, palindrome: the passes **0.055 ms** for all three (0.045 for two).
  `--validation=on` clean (lamps-off garage, normal and forced); scenetest green on both backends.

## Made the engine's anti-lag (2026-09-14, owner: "if they are good this becomes our new anti lag method")

- `EngineConfig::MeasuredChange` is **on by default**, the way `DirectSignal` and `GiSignal` are: an
  engine switch, with `--measured-change=off` the reference arm. `mc_measure.py` arms other than
  `on*` now state off.
- **RT-5's evidence test is deleted**: `--anti-lag`, `--anti-lag-floor`, `SignalAntiLag` and
  `SignalAntiLagFloor`, the `AntiLag` lane in the signal push block (now 208 bytes; the accumulate and
  the blur declare it) and in the resolve's (56 bytes, `ChangePad` gone with it), `kAntiLagFloor`, and
  the tests in `reflection_accumulate` and `taa_resolve`. It was off by default, so the default picture
  loses nothing.
- **Built (Release engine and the Sample module, no errors) and not yet verified** -- the owner ended
  the day before the checks ran. First thing next: (1) `--validation=on` on the garage and the bridge
  with no flag (both push blocks changed size); (2) the still garage with the check off against
  `mc_park_base`, run twice (the first run after a rebuild differs) -- the deletion must change
  nothing; (3) a run with no flag against `on5` -- must match; (4) scenetest on both backends.
- **Memory is the cost that grows with the screen**: about 30 MB for the direct light's record and
  targets at 1600x900 (measured after the cost cut), so roughly 170 MB at 3840x2160 by pixel count
  (not measured). Packing the record's exact-in-half-float lanes is the lever if that matters.
- **Still open: the moving camera** -- the owner's "non negotiable", postponed by the owner at the
  end of the day. The plan is in HANDOFF's 2026-09-14 entry.

## Default on, verified (2026-09-14, owner's task list)

The four checks the stop left: `--validation=on` with no flag -- garage clean, bridge only the two
known 09-13 errors; the check off against the build before measured change -- garage identical but
for its known frame-168 noise, bridge within its own run-to-run spread (two runs of that older build
already differ by up to 4 levels on 46 of 64 frames); no flag against `on5` -- the lights button's
curve identical (10 / 44 / 106), the bridge identical; scenetest Vulkan 2491 and OpenGL 2432 passes,
no failures.

## The moving camera (2026-09-14, owner: "a non negotiable")

**What held the check to a still camera, and what replaced each:**

1. **The key included the camera.** It does not now: a re-light lights the record's own point from
   the record's own eye and frame, so nothing it reads moves with the camera
   (`DirectChangeKey()` and its two extensions take no view).
2. **The direct light's re-light walked this frame's cluster cell**, which is cut through this
   frame's view: once the camera moves a recorded point falls in another cell, and off screen in an
   edge cell whose list need not hold its lamps. The re-light walks the **world grid's** cell instead
   (`RelightCellIndex` in `direct_trace.rvshader`), or every positional lamp outside the grid. A world
   cell holds every lamp reaching the point in the brightness order the cluster cells keep
   (`LightGrid.cpp`), so the lamps that add anything are added in the same order. The reflection and
   bounce re-lights needed nothing: under `RV_TRACE_ONLY` a hit already walks every lamp.
3. **The records' "moving" flag read the velocity lane**, which holds the camera's motion as well as
   the surface's, so a moving camera marked everything moving. `include/change_record.glsl`'s
   `RecordMovedOnItsOwn` takes the camera's part out -- RT-15's `ObjectShift` test, the same eighth of
   a texel between rounding and motion.
4. **The accumulate and the resolve read the map at this frame's texel.** The map lives on last
   frame's grid, so both now read it where the pixel's history came from: the accumulate at the texel
   moved by the picture's own travel (`shift`), the resolve at the pixel moved back by its velocity;
   under the motion floors (an eighth of a texel, `kStillWithinTexels`) at the texel itself, so a
   still camera reads exactly where it did.
5. **The record waited for a still camera.** It is now retaken every frame the camera moves (and, as
   before, when the key changed); a re-light runs on a record taken under last frame's camera
   (`RecordIsLastFrame`, FrameGraphBuilder.cpp).

**Measured** (garage, dolly along world X at 0.4 m/s unless said):

- **Exact in motion**: `--change-force=on` on the 0.6 m/s dolly, all three maps black on all 80
  frames; on the moving lights-button run, all three black before the switch and lit on frame 501.
- **The lights button with the camera moving** (arms aligned frame for frame against a lamps-never-off
  and a lamps-long-off run of the same dolly; `mc_measure.py fademove`): frames until this much of the
  lamps' light is left, check off / on -- 50%: 5 / 1, 25%: 14 / 2, 10%: 25 / 15, 5%: 34 / 25,
  2%: 45 / 37. Before this work a moving camera got nothing, i.e. the off column.
- **Per history, ten frames after the switch** (`--capture-signals`, share of the lamps' light still
  held, check off / on): parked -- direct 41.1 / 8.2%, reflections 38.5 / 7.8%, bounce 77.4 / 12.6%,
  resolve 29.1 / 2.1%; moving -- direct 7.9 / 4.1%, reflections 14.6 / 6.9%, bounce 23.8 / 9.1%,
  resolve 11.1 / 4.9%. Each accumulator keeps as little or less moving as parked; what is left in
  both is partial shares (blocks lit by other lamps too restart partly).
- **Still camera**: the lights-button curve identical (10 / 44 / 106), bridge within 2 levels on ~93
  pixels, and one real difference: the record is now taken on a run's first frame, so the check also
  measures the scene finishing arriving on frame 2 (all three maps fire there) -- which leaves about
  400 pixels on the garage's ceiling-tube rims up to 11-15 levels different 150 frames later.
  **Bisected**: a build with the old first-frame timing matched the previous build exactly (parked and
  on the dolly). Kept, because it is a real change measured.
- **Moving camera with nothing changing** (the dolly): no re-light, no map; with the old first-frame
  timing identical to the build before measured change. **Moving camera and a moving chrome cube**
  (the scene changes every frame, so the re-light runs every frame): diff images against the check off
  near black -- no trail, no noise (`build/rt5/mc/mc_cubemove_def8_vs_off.png`).
- **Cost** (palindrome, `--benchmark=240`): parked **0.048 ms**; with the camera moving the whole run
  **0.91 ms** (frame 15.43 -> 16.25 ms), nearly all of it the records retaken every frame -- the
  bounce's **0.55 ms** (it traces its own estimate, 1/9 of the bounce trace), the direct light's
  0.28 ms (the reservoir choice at 1/9 of the pixels), the reflections' 0.05 ms. **The lever, not
  taken without the owner:** the bounce's record and re-light could cast one ray where the trace cast
  N -- the replay stays exact (same seed, same rays), only the size of a real change is estimated from
  fewer rays -- about N times cheaper.
- `--validation=on` clean on the moving garage; bridge only the two known errors; scenetest green on
  both backends.
- **Traps paid for**: `mc_measure.py` arms not named `on*` or `def*` run with the check **off** --
  twice a "result" was of the check switched off; and `AtSeconds: 100` on the lamps-never-off arm put
  its camera a sub-texel out of step with the other arms (99.5 does not), which halved the reference
  light and made every share read low.

## The bounce's check at one ray (2026-09-14, owner: "reserve 1 ray for performance and balanced settings only")

- `RayOptimisationPreset::ChangeRays` (RenderSettings.h): the rays a point of the bounce's check casts
  -- 0 the trace's count (Quality and the reference), 1 one (Balanced and Performance).
  `--change-rays=0|1` overrides it for a run. The record casts that many and keeps the count, so the
  re-light casts the same rays and the replay stays exact.
- Exact: forced `gi-change` black on all 80 frames of the moving dolly, and lit on the switch frame of
  the moving lights button.
- Looks the same: the lights button parked, 10 / 45 / 107 frames against 10 / 44 / 106, diff images
  black at 3, 30, 90 and 180 frames after the switch (`build/rt5/fade_mc_fade_def8_vs_mc_fade_onr1.png`);
  camera moving, 1 / 2 / 15 / 25 / 37 frames for both, within 0.2% at every frame
  (`build/rt5/mc/mc_fademove_onr1_vs_def8.png`).
- Cost while the camera moves: the bounce's record **0.60 -> 0.21 ms**, all the check's passes 0.94 ->
  0.56 ms. `--rt-optimisation=balanced` measured 0.21 ms (0.52 with `--change-rays=0`), quality 0.59.
  Scenetest green on both backends.

## The water test (2026-09-14, owner: "yes run the water test")

`tools/scripts/garage/session_2026_09_14/water_test.py`: the bridge at the glitter camera, its 120
sodium lamps switched off at 3.0 s by the engine's Switcher (the scene edited in place and restored
byte for byte -- its bake is found by the scene's own name), frames 170-290; beside it the same run
with the lamps off since 0.5 s and one with them never off (99.5 s), all three with the check off and
again as shipped. Share of the lamps' light still shown, pixel for pixel against those two:

- **On the water: gone on its own.** Check off: 24% one frame after, 10% at 5 frames, 5% at 9, 2% at
  16. As shipped: 20%, 4, 8, 14. The sea's own averaging keeps a few frames and no more, so there is
  nothing for a water anti-lag to take. **Nothing built.**
- **On the bridge itself (above the waterline): slow, and not the filters.** Check off: 10% left at
  108 frames; as shipped: 50% at 1, 10% at 79, 5% not within 110. `--capture-signals` 20 frames after
  the switch: the direct light's history holds 0.9% of the lamps' light, the reflections' 0.4%, the
  resolve's 13.4% (it carries the whole picture). **The cause is the bake**: the sodium lamps are
  Hybrid Full Bake, a lamp at zero intensity leaves `CollectLights`, the lighting hash changes, and
  `Scene`'s field is recreated -- no bake on disk matches the new hash, so the field is zeroed and a
  solve is asked for, and the traced bounce takes over (its history exists in the switched runs and
  not in the lamps-never-off run). The bounce the bake held is rebuilt over the next second or more.
  Not measured-change work; left for the owner.
