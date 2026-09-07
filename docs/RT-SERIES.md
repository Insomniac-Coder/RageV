# RageV — the RT series: one list for the RT-first engine

**Written 2026-09-06 late evening, at the owner's word after T5:** *"I think T and R series should be combined into RT series tasks and, knowing the direction that we are heading (RT first), reanalyze and come up with the RT series of tasks which takes tasks from both R series and T series and picks and/or modifies tasks to implement our RT-first goal; if anything in the R series pushes us away from it, it needs to be re-evaluated and redesigned. Once the RT series is done we move back to WR, whose finished items may need revisiting since they were made when the engine didn't use G-buffers."* Also owner-set for this list: **everything RT reads the G-buffer** ("anything that has been implemented for RT, if possible implement it using G buffers, use it to your advantage"); **the freedom to tear down and rebuild systems** to get there; **`Lamps` becomes "rays per pixel"**; and **the blur weakens as the G-buffer-based TAA and accumulation get better**.

This replaces two lists: `docs/RT-FIRST.md` §2b (T1–T13) and `docs/RENDERING-REVAMP.md` "WR-16 R" (R1–R12). Those files keep the design records and measurements; the order lives here. The protocol is unchanged: one item per green signal, report after each, solo.

## What the two lists became

**Closed (done, measured, recorded):**

| was | what | record |
|---|---|---|
| T1 | The G-buffer pass and the lit pass split, pixel-identical | RT-FIRST §2, §5 |
| T2 | Editor greying by signal source, nothing hidden | RT-FIRST §2b |
| T3 | Surface ids for every stage (`InstanceData.Extra`) | RT-FIRST §2b |
| T4 | The reconstruction contract as code (`SignalParams`, accumulate + young blur, any signal) | RT-FIRST §2c |
| T5 / R8 | **The direct light as a signal:** `DirectTrace` from the G-buffer, K lights a pixel, the contract with a second payload; the lit shader adds two pictures | RT-FIRST §2d (parity 0.034 levels; K = 4 at 10.7 ms against the loop's 11.9) |
| R1, R2, R3 | Stale shaders deleted; bench counters; the resolve's taps from the preset | RENDERING-REVAMP WR-16 R |
| R12 | Short memory under motion + the young blur (medium) for reflections | RENDERING-REVAMP WR-16 R; **the blur is now a thing to retire, see RT-5** |
| R6 (part) | `reflection-refusal`, `reflection-picture`, `direct-light`, `direct-refusal` views | RT-FIRST §2c, §2d |

**Re-evaluated against RT-first (what pushed away, and what replaces it):**

- **R4 (confidence-driven memory) as designed is dropped.** Tried twice, both failed for the same reason: reading a change out of a noisy fresh sample against its own history is guessing. With the G-buffer the accumulator can *know* whether the surface is the same one (id, depth, normal) and only has to *judge* whether the light on it changed — a far smaller question, with a noise floor and a hold. That is RT-5's anti-lag, not R4's.
- **WR-17's distance thinning, S1's `--shadow-budget`, S4's in-loop sampler and `--shade-lights`** are dead for every opaque pixel under the direct-light signal (the pass chooses by contribution, which the owner's own rule called the physical form). They come out in RT-1 rather than rot beside the new path.
- **R12's young-history blur** was the right patch for one-ray reflections without a G-buffer; on the direct light it smeared hard shadow edges (measured, T5) and is off there. As the G-buffer validation (RT-5) and the G-buffer TAA (RT-6) land, it is weakened signal by signal and measured out — the owner's direction.
- **R11 (the floor's −3 darkening)** is not its own item; it is re-measured inside RT-4 once reflections trace from the G-buffer like everything else, because its remaining suspects are the resolve's ratio cap and the tonemap, and the G-buffer path changes the first.
- **R7 (the water writes its motion)** is inside RT-8; the water joins the G-buffer rather than patching its velocity alone.
- **R9 (GI audit)** and **R10 (the tubes as line lights)** keep their intent and become RT-3 and RT-7.
- **R5's silhouette rule** (the hit-distance test at silhouettes, restricted to same-surface neighbours) is inside RT-4; the G-buffer id makes "same surface" a lookup instead of a heuristic.

## The RT series, in build order

| # | what | from | why RT-first changes it | size |
|---|---|---|---|---|
| **RT-1** | **The lit shader stops walking lights under RT, and the direct pass is finished.** The subtractive case (a static surface under a fully baked lamp with a moving object in range) moves into `DirectTrace` — the pass can read the field — so the opaque lit shader's light loop runs *only* in raster mode; the pass tests the 16-byte cull records before the 80-byte light, and scores with irradiance times a cheap lobe estimate so K = 8 stops costing what every light costs (2.16 ms against 2.18 today); `lights per fragment` counted once. **`Lamps` → `RaysPerPixel`** in the preset, the label, `--light-sampling` → `--rays-per-pixel` (old key still read). WR-17's thinning and the S1/S4 instruments removed from the opaque path. | T5 follow-ups, WR-17, owner's rename | The lit shader is 6000 lines with a 27-light loop inside; under RT it should carry none of it. | medium — **✅ done 2026-09-06 late evening, record below** |
| **RT-2** | **Ambient occlusion as a signal from the G-buffer.** `rtao_compute` traces before the lit pass, the contract denoises it (the SSAO-style accumulate goes), and the lit shader reads it and applies it **to the ambient and indirect terms only** — today "SSAO apply" multiplies the whole shaded frame after lighting, which darkens direct light that has its own shadow rays. `--debug-view=ao`. | T6 | The wall's motion noise the owner saw is most likely this: RTAO's per-frame sample with a history TAA invalidates under motion. The garage's direct light cannot be it (hard shadows, measured). | medium — **✅ done 2026-09-06 night, record below; the motion-noise guess did not hold up** |
| **RT-2.1** | **The doubled rasterisation of the pending kinds** (owner-filed 2026-09-06 as RT-2's sub-task, because RT-2 exposed it). With every opaque kind in the G-buffer, the pending draws -- the terrain above all -- rasterise twice: Headland's G-buffer pass 0.08 → 3.3 ms, the old path 14.4 → 18.1 ms. **First a measurement:** which LOD the far terrain chunks are drawn at (4.6 M triangles for that view is suspect); if too fine, most of it returns in both passes with no renderer change. **Then the resolve:** the lit pass stops rasterising the plain kinds and lights them from the G-buffer in a fullscreen pass, with the lanes that needs -- emissive (the tubes), the coat's wrap and anisotropy with its tangent, sheen -- which gets back the second rasterisation (about the 3.3 ms) and not the G-buffer pass itself. | RT-2's finding, step 1's deferred question | The G-buffer is now the whole scene; paying its raster once is the point of having it. | measure 0.5 d; the resolve medium — **✅ done 2026-09-07, record below: the measurement, and the fix it pointed at (the parallax march at mip 0, not the raster); the resolve is RT-2.2** |
| **RT-2.2** | **The deferred resolve** (owner-filed 2026-09-07 from RT-2.1's finding, **for the end of the series** -- after RT-13, not before). The lit pass reads albedo, normal, roughness, metallic, specular and occlusion from the G-buffer for every opaque kind instead of sampling the material again; the raster kept, so the tangent, the emissive map and the coat and sheen uniforms stay where they are and no lane is added (`RV_GBUFFER_FED` on the six lit-kind pipelines, three bindings re-committed in `DrawLit` the way 26-28 are, verified by diff image against the sampled path). **Precondition:** the albedo lane is `R8G8B8A8_UNORM` linear; it goes to sRGB8 or 16F first, or the lit pass bands in the dark tones. | RT-2.1's record | The material evaluated once, in the G-buffer, is the RT-first shape; worth ~0.6 ms at Headland today, more where heavy materials sit near the camera. | small; **at the end** |
| **RT-3** | **GI as a signal this frame.** The RT GI trace reads the G-buffer before the lit pass; `gi_denoise` audited against the contract's four properties (reprojection by surface, geometric tests, motion-capped memory, a bound) and replaced by it where it falls short; the lit shader reads *this* frame's indirect instead of last frame's (`u_Indirect` is a one-frame-late history today); the baked field stays the far-field fallback. | T7, R9 | One contract for every signal, no second temporal system fighting it (WR-16's ReSTIR-GI rejection stands). | medium — **✅ done 2026-09-07, record below** |
| **RT-3.1** | **The contract runs at each signal's own resolution.** RT-2 and RT-3 both upsampled a half-resolution signal to the frame and then ran the whole reconstruction contract there, paying four times the texels a half-resolution estimate carries information for. Instead: a guidance downsample (the G-buffer's depth, normal and velocity lanes onto the signal's grid, by selection), the contract at that grid, and one joint bilateral upsample at the end -- the arrangement every real-time denoiser uses. Both the occlusion and the bounce. | owner-directed 2026-09-07, from RT-3's cost | The filter belongs where the signal was traced; upsampling first was the smaller diff, not the better design. | small -- **✅ done 2026-09-07, record below** |
| **RT-4** | **Reflections as an instance of the shared code, traced from the G-buffer.** The trace and resolve move before the lit pass and read the G-buffer; the lit shader reads the reflection like any signal — **decide by measurement** whether the composite stays after TAA (the edge-flicker finding of the ninth entry) or the reflection passes through TAA like everything else. R5's hit-distance rule restricted to same-id neighbours; R11 re-measured; the resolve's ratio cap tested as R11's first suspect. | T8, R5, R11 | The reflection is the only signal still traced after the lit pass; ids make its silhouette rule exact. | medium |
| **RT-5** | **The contract validates by the G-buffer, and the blur starts to go.** History rejection by id, depth and normal (not colour alone); the grazing-angle plane test fixed (the ceiling and far pipes are refused every frame today — T5's refusal view); the bound relaxed for a long, converged history so a skewed K-sample estimate stops biasing it (−0.16 on the floor today); the evidence-driven anti-lag with a noise floor and a hold, gated by "same surface, no motion" (what R4 wanted, done where it can be known); the young blur weakened signal by signal and measured out. | T13 (half), R4 redesigned, T5's open items | The reprojection is right (measured); what the G-buffer adds is *knowing* the surface, which no colour test can. | medium |
| **RT-6** | **TAA on the G-buffer.** One shared reprojection for TAA and every signal; disocclusion told by depth, normal and id, the colour clamp kept for what geometry cannot tell; the still-feedback rule read from the G-buffer's velocity per pixel instead of a project constant (0.98 in the garage, forced to 0 on the bridge today). | T13 (half), R12's direction | The owner's expectation, and the precondition for a weaker blur everywhere. | medium — **✅ geometric half done 2026-09-07, record below; the still-feedback half waits on RT-8** |
| **RT-6.5** | **The accumulator tests the material, not just the roughness.** Its history gate checks the reflector's normal, plane and roughness, and roughness alone does not separate a metal from a dielectric: a boundary between them passes today, and the two shade nothing alike. Add the surface id (the G-buffer lane RT-6 already keeps) and the metallic, as a further term of RT-6.4's match confidence rather than a fourth cutoff. **§4C of the owner's specular specification.** Overlaps RT-5, which names id rejection for the contract in general; this is the reflection accumulator's own gap and is small enough to do first. | owner's spec §4C, RT-6.3's record | The one validation axis the accumulator has never had, and the cheapest left. | small |
| **RT-7** | **The tubes as lights the rays can sample.** Emissive geometry becomes a light with endpoints (luminaire binding, WR-9); LTC line lights for the specular term (WR-8); the direct pass shades them like any lamp, so the floor's tube reflections stop depending on rare hits; rays skip the lens emission (no double count). | T9, R10, WR-8, WR-9 | The variance the reconstruction has been fighting all week is removed at its source. | large |
| **RT-8** | **The water on the G-buffer.** The water's surface prepass becomes a G-buffer layer (position, normal, roughness, wind, id, velocity — R7 lands here); its direct light comes through `DirectTrace`, its choose/shade/accumulate passes fold into the contract, its mirror and refraction rays are signals (WR-18's half-res pass); TAA gets its motion. | T12, R7, S4, S5, WR-18 | The sea is the one surface with its own copy of every system. | large |
| **RT-9** | **The budget's shadow lane and the one dial.** The allocator widened to K per tile (S3's widening), the direct pass reading the tile map like GI does, "rays per pixel" the one setting across land and water; the counters honest for every pass. **And the allocation driven by temporal confidence** (owner's spec §10, filed here 2026-09-07): fewer rays where the history is trusted, more where it was refused, most where a pixel was just disoccluded or its reflection direction swung -- all three of which are now measured per pixel (RT-6, RT-6.3, RT-6.4) and thrown away. The constraint is the specification's: **do not raise the ray count globally**, spend the same budget where reconstruction cannot answer. | S3, WR-16, owner's "rays per pixel" | Now there is a consumer to size the lane for (Part IV decision H). | medium |
| **RT-10** | **ReSTIR DI on the G-buffer.** The choose/shade split (the water had it) for every opaque pixel, temporal and spatial reuse of the choice validated by id, depth and normal, feeding the same contract. | T10, S4, WR-16 M4 | The many-light scenes (the bridge: 78 lights a pixel) are where K = 4 is not enough. | large |
| **RT-11** | **Next-event estimation at GI and reflection hits** with the resampled light (the 7 M rays the hits trace today, chosen by importance). | T11 | The last place a ray shades every light. | medium |
| **RT-12** | **Signal debug views, complete:** history length, refusal, reach, the K choice, the raw fresh picture, per signal, on one log ramp (the direct-light view saturates at any linear scale). **Plus the confidence set** (owner's spec §11, filed here 2026-09-07): the combined history confidence, the reflection direction as RGB and its frame-to-frame difference, and the rejection reason split by which test refused it -- depth, normal, material, disocclusion -- and the rays actually allocated per pixel. **This session hit plumbing that was declared, bound, read and never connected three separate times**, each caught only by staging an absurd constant and checking the frame moved; a refusal-and-confidence view would have caught all three in one look. | R6 remainder | Tune with views, not the final image. | small |
| **RT-13** | **Surfaces outside the G-buffer join it: skinned, layered (terrain), and transparent (the car's glass, OIT).** RT-1 found the first two on the bridge -- the G-buffer pass draws only the plain and masked kinds, so under the direct-light signal the terrain and the characters had no light in the pass and none from the loop; they keep the loop for now (`RV_SKINNED` / `RV_LAYERED` compile without the signal's inputs). The fix is a G-buffer variant per kind and their draw in the G-buffer pass; transparent surfaces decide between a thin layer and the in-shader path. | RT-1's finding, new | Every opaque surface must be in the G-buffer or every signal skips it. | **skinned + layered ✅ done inside RT-2** (see its record); transparent: decide first, small |

**Verification, every item:** the reference arm is the old path where one exists (`--direct-signal=off` and its siblings), a converged still against it (diff images, no structure beyond grain, mean under a level), the dolly arms (`parked_stats`, `smear_metric`, `edge_shake`), the ray counters, and the bridge's three cameras for anything the garage cannot show (sized lamps, 78 lights, the baked field with a moving car).

## Complexity, item by item (owner asked 2026-09-06)

Effort is solo days at this week's pace (build, measure, report, wait); risk is the chance the first landing is wrong in a way the metrics catch and a second landing is needed; the last column is what actually makes it hard.

| # | effort | risk | what makes it hard |
|---|---|---|---|
| RT-1 | 1.5–2 d | low–moderate | Lifting the field's stored-direct read into the trace-only region so the subtractive case can move; deleting the S1/S4/WR-17 paths out of a 6000-line shader without touching the water's; the bridge is the only scene that exercises the baked-lamp case, so its three cameras are the check. The rename is mechanical. |
| RT-2 | 2 d | moderate | It changes the look on purpose (AO stops darkening direct light), so there is no pixel-identical reference; the contract carries a scalar in an RGB payload for now; raster's SSAO must move the same way or the two modes diverge. |
| RT-2.1 | ✅ done in a day (2026-09-07): the measurement and the parallax fix; the resolve is proposed at ~0.6 ms on Headland, gated on the albedo lane's storage | low | The extra G-buffer lanes (emissive, coat, anisotropy's tangent, sheen) are bandwidth per pixel; the resolve must reproduce the lit shader's ambient, probe and field terms from position and normal alone; MSAA/SSAA modes and the transparent kinds (still forward) must keep working beside it. The measurement may make most of it unnecessary. |
| RT-2.2 | 1 d (the lane's storage first, then the variant) | low | The albedo lane's quantisation is the one way it can change the picture; a diff image against the sampled path settles it. **Deferred to the end of the series by the owner (2026-09-07).** |
| RT-3 | 3 d | moderate | The contract assumes the signal is at the G-buffer's resolution and GI traces at half; the accumulate's texel-to-surface lookup needs a scale. The garage bakes its GI, so a realtime-GI scene (the camp) is the test. The audit may find `gi_denoise` better on one axis and the contract on another. |
| RT-4 | 2 d + a measurement day | moderate | Whether the reflection should pass through TAA is an open measurement, not a design choice: the ninth entry found compositing after TAA on jittered geometry to be the larger half of the edge flicker, and TAA's clamp may ghost mirror content the other way. R11's −3 levels has had every obvious suspect ruled out already. |
| RT-5 | 3–4 d | **high** | The anti-lag was tried twice and both failed on a still (the R4 spotting); the bound cannot be relaxed without reopening lag on a light switch — one trade measured on four metrics at once (parked drift, the Switcher's settle, the dolly, the edge shake). The id needs a history lane the accumulator does not have yet. |
| RT-6 | 3 d | moderate | TAA changes are visible on every pixel of every scene; geometric disocclusion needs last frame's depth, normal and id kept (the G-buffer is single-buffered today); the velocity-driven still rule is wrong on the sea until RT-8 writes its motion, so it ships with a guard or after RT-8. |
| RT-7 | 4–5 d | moderate–high | A new light type (endpoints, length) through the scene data, the light record and the editor; the LTC fit tables (or the representative-point capsule first, which WR-7 half has); the sealed-room rule — a pixel that takes the analytic term must never also receive the lens emission from a ray — and a seam-free roughness window between the analytic term and the traced mirror. Verified against the 400-frame unclamped truth. |
| RT-8 | 4–6 d | **high** | The sea has bitten every session (the prepass crossing, lamps that cast nothing, the choice reuse); it is a second G-buffer layer, not a lane, with its own BRDF; the Gerstner velocity needs the wave evaluated twice per vertex; every check is the bridge at three cameras under the flicker protocol. |
| RT-9 | 2–3 d | moderate | The tile map's four lanes are full (a second map or a repack); the allocator's restlessness has a history — the sixty-second still test at 0.01 changes per tile per second is the bar, and the direct signal's own temporal moments are the new importance input to get right. |
| RT-10 | 5–7 d | **high** | Bias control (M caps, the MIS weights for spatial reuse, visibility reuse or not); the water's version measured as a loss for a physical reason, and land must be shown to differ; the garage shows little at K = 4, so the bridge's 78-light pixels are the test throughout. |
| RT-11 | 1–2 d | low–moderate | Mostly RT-1's score applied at the hit; the noise it moves into GI and reflections must be absorbed by their contracts, measured on the reflection arms. |
| RT-12 | 0.5–1 d | low | Plumbing; the log ramp is the only design. |
| RT-13 | 0.5 d to decide; 2–3 d if a layer | low | Deciding is most of it; a thin transparent layer is a bounded copy of the water's prepass. |

**Dependencies:** RT-9 wants RT-1's cheap score; RT-10 builds on RT-9's lane and RT-5's validation; RT-6's velocity rule wants RT-8 or a guard; RT-7 carries WR-9 inside it. **The whole series at this pace: roughly seven to eight weeks of solo days**, front-loaded with the medium items so the high-risk ones (RT-5, RT-8, RT-10) land on a validated contract.

## Records

### RT-1 — ✅ done 2026-09-06, late evening (uncommitted, both copies staged)

**What was built.** (1) Under the direct-light signal the opaque lit shader's light loop walks nothing (`total = 0`); the field's loss (a static surface under a fully baked or hybrid lamp with a moving object in range: one moving-only ray, the loss clamped to what the field holds through the ambient Fresnel and the coherent highlight) moved into `direct_trace.rvshader`, which reads `VolumeIrradiance` and `BakedShare` like the loop did; the G-buffer's id lane packs the shading roughness (integer part over 65535 -- ten bits moved chrome's highlights) with the material's occlusion (the fraction) for the clamp; the loss walk is skipped in a cell whose live sublist is its whole list. (2) The survivors' rays are de-duplicated (two reservoirs keeping one lamp trace once) and the score is cheap (irradiance times the lobe's peak at normal-incidence Fresnel, no masking). (3) `Lamps` → `RaysPerPixel`, `--light-sampling` → `--rays-per-pixel=K[,target]` (the old key still read -- the first landing's text replacement ate the alias, found by the ray count). (4) WR-16 S1's `--shadow-budget` (`RV_SHADOW_BUDGET`, four shader spans) and S4's `--shade-lights` (on screen and at hits) removed: fields, parsing, help, define, bits. `lights per fragment` counts once.

**Measured, garage 1600x900 (T5's numbers in brackets):** lit pass **1.0–1.1 ms** (2.2); K = 4 frame **10.0 ms** (10.7; the loop 11.9), DirectTrace 1.42; K = 8 frame 10.6, DirectTrace 2.16 (2.16 -- the rays halved, 10.9 M → 5.0 M, but the eight full shades cost what they cost: the sampler's remaining lever is fewer shades, not a cheaper score); shadow rays at K = 4 **3.35 M** (5.42) beyond the hits' 7.05 M. **Parity:** the raw pass at every light against the loop **0.0335** mean (T5: 0.034); through the contract 0.155 (with T5's tuning 0.189) -- the accumulator's own effect on a converged still (bilinear history reads under the jitter, the bound), **RT-5's**, and the loop itself is bit-identical before and after the edits (0.0000).

**The bridge, every light, one frame at 60, on against off:** Deck 0.039, Pier 0.072, **Headland 0.048 -- after a T5 defect the garage could not show:** the first Headland read 0.721 with the terrain and the far hills 0.38 darker, because **the G-buffer pass draws only the plain and masked kinds**, so the skinned and layered surfaces had no light in the pass and, under the signal, none from the loop either. They keep the loop now (their pipelines compile without the signal's inputs, and their sets carry no such binding); drawing them into the G-buffer is RT-13. Frame time on the bridge at the preset's K = 8: Headland 14.36 against 14.34 ms, Pier 12.43 against 12.52 -- no gain, because the bake carries the bridge's lamps and few pixels trace a ray there (3.7 M against 4.6 M); the win the design promised for 78-light pixels waits for a live-lit many-light scene.

**Open, into the RT series:** the contract's 0.15 on a converged still (RT-5); skinned and layered into the G-buffer (RT-13); K = 8's cost is in the eight shades (RT-9's per-tile K and RT-10's reuse are the levers).

### RT-2 — ✅ done 2026-09-06, night (uncommitted, both copies staged)

**First, RT-13's opaque half, pulled forward because every pre-lit signal needs it.** The G-buffer pass drew only the GPU-culled plain and masked draws; everything in the pending list -- the skinned and layered kinds, and on the bridge 184 of its 201 draws, the terrain among them -- was outside the G-buffer. Now: `pbr_skinned` / `pbr_layered` compiled with `RV_GBUFFER`, their pipelines and sets (uploaded like their lit sets: the CPU visibility list, instances, bones), plus a pending-draw pair of G-buffer sets for statics and masked draws indexed by the CPU list (the indirect G-buffer set carries the GPU cull's), and `DrawGBufferPending` -- DrawLitBody's vertex path with the G-buffer pipelines -- in the split's G-buffer half. The first landing drew nothing (it read the opaque range from `TransparentBegin`, which the lit draw computes later); the second finds the range itself. **Headland, every light, on against off: 0.064** (RT-1's fix had the terrain keep the loop; now the pass lights it), the loop bit-identical. **The cost is real:** the pending kinds are rasterised twice, and Headland's terrain is 4.6 M triangles -- the G-buffer pass went from 0.08 to 3.3 ms there, the frame at the old path from 14.4 to 18.1 ms. That is the price of "everything in the G-buffer" on a terrain-heavy view under forward+ with a prepass; the lever is a lit pass that resolves the plain kinds from the G-buffer instead of rasterising them again (the deferred question of step 1, now with a number), or a cheaper terrain LOD for the G-buffer half. Filed under the WR revisit, not fixed here.

**Then the signal.** `OcclusionCompute` (RTAO under rays at half resolution reading last frame's budget map, imported early; SSAO at its rung in raster) from the G-buffer's depth and normal before the lit pass; `OcclusionUpsample` -- the old apply shader against a white scene, which is exactly its depth-aware upsample with the intensity folded in, so the contract runs at the G-buffer's size and the lit shader reads by texel; the contract as `OcclusionAccumulate` + three blurs (`AoSignal()`: diffuse kind, slot 2, young blur 6 texels); the lit shader reads set 0 binding 28 under `RayRates.w` bit 23 and multiplies **the ambient, the stored indirect and the environment's specular only** -- never the direct light. `--ao-signal=off` keeps the old post-apply chain as the A/B; `--debug-view=ao`. Works in raster too (SSAO through the same path).

**Measured, garage:** frame 10.8 ms against 10.1 with the post chain (the contract at full resolution costs 0.85 ms where the old chain cost 0.27); no errors under rays or raster. **The picture changes as designed, and it is a look change the owner should judge (accepted by the owner 2026-09-07):** where the lamps' cones hit the graffiti wall the old chain darkened the direct light by the AO, the new one does not -- the wall +8.2 levels (of 25), the poles +7.7, the car +1.1, the floor +0.06, the ceiling +0.4; the diff image (`rt2_ao_diff_signed_x8.png`) is the lit pools on the wall and under the poles, nothing else. Physically AO belongs to the ambient terms; if the old look is wanted, applying the same signal to the direct light too is one multiply. On a still the signal's per-frame change is 0.80 on the wall against the old chain's 0.61 (the contract without the old chain's 0.9 feedback).

**The motion-noise guess did not hold up.** Reflections off, the dolly's flat-pixel per-frame change is the same with the AO signal on and off (wall 11.30 against 11.43, floor 9.17 both), as it was for the direct light in RT-1: at 1.5 m/s that metric is texture displacement, not noise, and it cannot attribute the wall's motion noise to any signal. The attribution needs a reprojected reference (RT-5/RT-6's territory); what T5's design named "the wall's motion noise" is still unattributed by measurement.

**The bridge at the preset, both signals against the old path (both with the pending kinds in the G-buffer):** Headland 19.7 against 18.1 ms (the AO contract +0.85, the direct trace +0.76, the lit pass −0.57), Pier 15.6 against 17.5 (the lit pass −1.2, the G-buffer −0.4). Mixed, and both numbers carry RT-13's doubling above.

**Open:** the AO debug view reads near-white on a linear ramp (RT-12's log ramp); the transparent kinds are still outside the G-buffer (RT-13's remainder); the doubled rasterisation of the pending kinds is **RT-2.1** (owner-filed as RT-2's sub-task; ✅ done 2026-09-07 -- it was the parallax march, not the raster; record below).

### RT-2.1 — ✅ done 2026-09-07 (uncommitted, both copies staged)

**The framing was wrong, and the measurement said so before anything was built.** RT-2's record blamed "the doubled rasterisation of the pending kinds -- Headland's 4.6 M-triangle terrain". The 4.6 M was the benchmark's whole-frame *triangles submitted* line (shadow maps and the water included); the terrain in the camera's view is **184 chunks and 751 K triangles**, and the benchmark now says so -- a new `terrain:` line: the chunks drawn per level with their triangles, what the distance rule alone wanted, how many chunks the ground's veto and the neighbour cap held finer. An offline replica of `Terrain::SelectLod` (`tools/scripts/garage/session_2026_09_07/terrain_lod.py`: the same error metric, distance rule, veto, cap, skirt rule and frustum test) matches the runtime within 10% and prices any rule change without a rebuild.

**What was ruled out, one measurement each (Headland, 1600x900, `--frame-time=0.0166`, the RT-2 build):**
- *The LOD veto.* `--terrain-lod-error=0.3` (a new measurement flag; the veto off): 751 K → 324 K triangles, the G-buffer pass 3.64 → 3.49 ms, the lit pass 4.36 → 4.19. The veto is worth 0.15 ms a pass and stays (it exists for the ray tracer, which traces level 0 whatever is drawn).
- *The draw count.* Chunks of 128 quads (64 chunks, 50 drawn instead of 184): G-buffer 3.39, lit 4.45 -- nothing; restored to 64.
- *The pixels.* At 400x225, a sixteenth of the pixels, the G-buffer pass was still 1.68 ms and the lit pass 2.24: the cost did not scale with pixels either.
- *The terrain at all.* A scene copy without the TerrainComponent: G-buffer 0.08 ms, lit 0.26, the frame 10.5 ms against 20.3. The terrain was the whole of both passes and half the frame (the traced passes grow with it too, +1.5 ms of DirectTrace, ReflectionTrace and the accumulates: more surface to trace from, legitimate).
- *The parallax march.* The layered material marches each layer's height map 8-24 steps, twice (the floor and the wall projection), `textureLod(…, 0.0)` at every step. With the march disabled in the runtime's shader copy: G-buffer 3.64 → 0.53 ms, lit 4.36 → 2.02, the frame 20.3 → 14.2. **5.4 ms of a 20 ms frame was four terrain layers marching mip 0 at a kilometre, where every fetch of a layer's dozen is a cache miss** -- which is why the cost scaled with neither pixels nor triangles: a sparser pixel grid makes each fetch miss harder.

**The fix: the march reads the height at the pixel's own mip.** `FootprintLod(map, ddx, ddy)` -- the hardware's isotropic level-of-detail rule from the coordinate's explicit derivatives -- goes into `Parallax` (a material) and `ParallaxLayer` (a terrain layer), computed outside divergent control flow (the material's before its branch; the layers' from the per-layer derivatives that were already there). A far pixel now marches the relief its footprint sees, filtered as its colour is; up close the footprint is under a texel and the level is zero, as before. **Measured:** G-buffer 3.64 → 0.70 ms, lit 4.36 → 2.20, **Headland 20.3 → 14.5 ms** (the no-march floor was 14.2); the garage 11.55 → 11.65 ms (noise: its height maps sit at level 0 at that distance). **The picture:** Headland's still against the baseline, mean 0.001 levels, max 0.7, no pixel off by more than 2; the garage's exactly 0.000 (`build/rt21/headland_mip_diff_x8.png`, `garage_mip_diff_x8.png`, made by the session's `diff_still.py`). Raster mode (`--ray-tracing=off`) compiles and runs.

**What is left of "the doubled rasterisation", honestly priced now:** the terrain's raster plus its material is the G-buffer pass's 0.62 ms at Headland (0.70 less the bridge's 0.08), and the lit pass pays the material once more -- about 0.6 ms of its 2.2. The deferred resolve -- the lit pass reading albedo, normal, roughness, metallic, specular and occlusion from the G-buffer for every opaque kind instead of sampling the material again, *the raster kept*, so the tangent, the emissive map, the coat and sheen uniforms stay where they are and no lane is added -- is worth about that 0.6 ms here, more on a scene with heavy materials near the camera. **Not built here: filed by the owner as RT-2.2, for the end of the series. Its precondition:** The G-buffer's albedo lane is `R8G8B8A8_UNORM` *linear* ("sRGB storage once the attachment path is checked", step 1a's note); a lit pass fed from 8-bit linear albedo bands in the dark tones, so the lane goes to sRGB8 or 16F first, then the variant (`RV_GBUFFER_FED` on the six lit-kind pipelines; three bindings re-committed in `DrawLit` the way 26-28 are; verified by diff image against the sampled path). A small item; the numbers say it is not urgent, which is why it waits.

**Noted, no item:** under RT-first the veto's reason (rays trace level 0) could go by the TLAS carrying each chunk's *selected* level, but the veto costs 0.15 ms a pass. Chunks of 128 quads were neutral on the GPU (fewer draws, coarser LOD granularity: 1.24 M triangles for the same view) and stay at 64. The benchmark's *triangles submitted* is the whole frame, shadow maps and water included -- never read it as one pass's count again.

### RT-3 — ✅ done 2026-09-07 (uncommitted, both copies staged)

**What it does.** Under ray tracing the bounce is now a signal of *this* frame:
`rtgi_trace` runs between the G-buffer pass and the lit pass (it already read
only depth and normal, so it needed no new input), a joint bilateral upsample
brings it to the lit pass's grid, the reconstruction contract settles it, and
the lit shader fetches it by texel at binding 16 — the same binding the
one-frame-late buffer used, with `RayRates.w` bit 24 saying which of the two is
in it. `gi_denoise` and the one frame of latency it carried are gone from the
traced path. `--gi-signal=off` is the reference arm and puts the traced form
back on the old chain whole; the screen-space forms (SSGI, voxel) stay there
always, because their gather reads the lit image and cannot run before the pass
that makes it. New: `gi_upsample.rvshader`, `PostProcess::GiUpsample`,
`Renderer3D::GiSignal()`/`SetGiSignal`/`SetScreenIndirectSignal`,
`FrameDesc::GiLight` (its own history in both layers), `--gi-signal=on|off`,
`--debug-view=gi-light` and `gi-refusal`.

**The audit RT-3 asked for: `gi_denoise` against the contract's four properties.**

| property | `gi_denoise` | the contract | verdict |
|---|---|---|---|
| reprojection **by surface** | by the velocity buffer alone, then a 3x3 colour clamp. Its own header says so: *"a clamp only rejects history where the neighbourhood varies, and indirect light is nearly uniform almost everywhere — which is exactly where a wrong reprojection hides"*, and *"the off-screen rejection is"* what makes it correct. On a near-uniform signal that is the whole of its geometry. | rebuilds the surface at the texel centre, reprojects through the previous projection minus the previous jitter, and refuses a history whose normal (dot < 0.8), plane (0.05 + 0.01·eye) or roughness (0.5) disagrees — then searches the eight neighbours (SVGF) before giving up. | **contract wins outright.** This is the property `gi_denoise` never had and could not get without a G-buffer. |
| **geometric tests** | none. Disocclusion is caught only by the reprojection landing off screen, and by whatever the weak colour clamp catches. | the three above, plus the silhouette rule and the id lane. | **contract.** |
| **motion-capped memory** | `kMaxFrames = 16` and a fixed feedback, both constants; nothing measures how far the reprojection moved. A texel sliding a pixel a frame still averages sixteen positions. | memory halved per texel of travel, capped so the average never spans more than `SmearTexels` of it, floored at `MovingMemory`, and shortened again at a silhouette whose other side moves. | **contract.** |
| **a bound** | a good one, and the one place `gi_denoise` is ahead: it accumulates in a range-compressed space (`c/(1+luma)`), bounds the fresh centre at 1.25× the brightest neighbour before the box is built from it, widens the box by the neighbourhood's *spread* rather than its extremes, and floors it at the pixel's own temporal sigma. | the same shape — mean and spread of the fresh 3x3, floored at the pixel's measured fluctuation — but **no range compression and no firefly bound on the fresh sample**. | **`gi_denoise` ahead on two counts.** Kept as an open item below rather than merged blind: the contract's bound was tuned against three other signals and the compression changes what every one of them averages. |

So: replaced, and the one thing the old denoiser did better is written down rather
than lost. Its spatial 3x3 blend (`kSpatial = 0.65`) is answered by the contract's
three a-trous blurs at strides 1, 2, 4 with `YoungRadius = 12`, the widest young
blur any signal here takes.

**The one contract change: `BoundWidth` 3 → 6 for this signal.** The bound is
built from the fresh 3x3's spread, which assumes the neighbours are independent
estimates. After an upsample from half resolution they are not — four
full-resolution texels share one traced sample — so the measured spread
understates the real variance and a bound built from it refuses a history that
was never wrong. The temporal floor is what holds this signal; this keeps the
spatial ceiling out of its way. One number in `GiSignal()`, no shared code touched.

**Why upsample first and accumulate at full resolution** (the complexity note's
open question: *"the contract assumes the signal is at the G-buffer's resolution
and GI traces at half; the accumulate's texel-to-surface lookup needs a scale"*).
It is RT-2's answer, and RT-2 is the precedent that settles it: a
half-resolution texel sits on the corner of four full-resolution ones, so
"the surface under this texel" — the question every one of the contract's tests
asks — has four answers there, and a sampled depth across a silhouette is the
depth of neither surface. Upsampling first makes the tests honest and needed no
change to code four signals share. **It is also the expensive choice, and the
price is measured below.**

**The picture: no regression, and no visible win either.** Both scenes, converged
still, the signal against `--gi-signal=off`:

| scene | mean \|d\| | p99 | max | pixels > 2 levels | frame signed |
|---|---|---|---|---|---|
| garage (forced `--gi-source=realtime`) | 0.098 | 1.33 | 42.3 | 0.31% | +0.046 |
| camp (realtime GI, the series' named test) | 0.079 | 0.67 | 30.3 | 0.09% | +0.048 |

The diff images (`build/rt3/garage_off_vs_on_x8.png`, `camp_off_vs_on_x8.png`) are
near-black at ×8 with a faint uniform green on walls and pillars — the a-trous
blur against `gi_denoise`'s 3x3, slightly favouring the signal — and no structure,
no halo at any silhouette, which is the joint bilateral upsample doing its job.
Region by region on the garage the bounce is preserved to within 0.03 levels.

**The reference arm is bit-identical to the pre-RT-3 build** (mean 0.0000, max
0.0, on the garage's parked frame 169): `--gi-signal=off` is a true A/B, not the
same storage in a different shape.

**The lag it removes is not measurable in either scene, and the reason is worth
recording.** The one-frame-late buffer's error is a camera-motion error, so it
should show under the dolly and not when parked. It does not: the two arms differ
by 0.104 levels at frame 40 and 0.114 at frame 189, flat across the whole move.
The direct test — is the reference at frame N closer to the signal at N or at
N−1? — answers **N** at every frame by a factor of forty (0.12 against 5.5),
because a frame of camera travel moves the picture by 5.5 levels and **the entire
traced bounce is worth +0.383 levels in the garage and +0.588 in the camp**. A
one-frame error in a term that small cannot be seen. RT-3's win here is
architectural: one contract, one temporal system, and the trace reading the
G-buffer like everything else. A scene where the bounce carries real light would
be needed to show it in a picture, and neither test scene is one.

**The cost, and it is the honest problem with this item.**

| | garage (realtime GI) | camp |
|---|---|---|
| frame, `--gi-signal=off` | 15.04 ms | 6.21 ms |
| frame, `--gi-signal=on` | **16.62 ms** | **6.93 ms** |
| the old chain (trace + `gi_denoise`) | 2.796 + 0.081 | 1.091 + 0.080 |
| the new chain (trace + upsample + accumulate + three blurs) | 3.046 + 0.041 + 0.223 + 0.252 + 0.205 + 0.170 | 1.111 + 0.042 + 0.207 + 0.188 + 0.168 + 0.113 |
| the GI passes' delta | +1.06 ms | +0.66 ms |

**Where it goes: the contract runs at full resolution on a half-resolution
signal, so its four passes pay four times the texels they carry information for**
— 0.85 ms of the garage's 0.89 ms chain is the accumulate and the three blurs.
`gi_denoise` ran at half. Halving the contract's resolution would put those four
passes at roughly 0.22 ms and save about 0.63 ms, but it needs the contract
taught a scale for its G-buffer lookups — the shared code the upsample-first
choice was made to avoid. **That is a decision for the owner, not a default: it
is the same trade RT-2's occlusion signal made silently, and it is now priced.**

**Scenes that do not use realtime GI are untouched, verified rather than assumed:**
the garage at its own baked setting and the bridge at Headland build zero GI
signal passes, and the bridge's Headland still against RT-2.1's accepted capture
is mean 0.034 levels, p99 0.67, no structure.

**Three defects found on the way, all in this item's own plumbing, and the
instrument that found them.** The first two were invisible to any comparison of
finished frames, which is the lesson:

1. **The signal was computed every frame and read by nobody.** `u_Scene.Indirect.x`
   is both the GI intensity and the switch that says there is a bounce to add, and
   it was filled only when the one-frame-late history had a frame in it — right for
   a buffer written last frame, wrong for a signal computed for this one, which the
   signal path never advances. **A probe writing a bright constant into every texel
   of the upsample moved the frame by exactly as much as the real signal did (mean
   0.799 levels, max 53.0, to three decimals identical), because neither was read.**
   That equality is the tell, and nothing else in the measurement kit would have
   shown it: the arms differed, the difference had a plausible size, and it was
   entirely the *old* chain being switched off. After the fix the same probe moves
   the frame 274× more than the signal does.
2. **The two paths into binding 16 disagree about what the alpha means.**
   `gi_denoise` writes 1 — a validity flag the lit shader multiplies into the
   bounce; the contract writes the *frame count* (`o_Accumulated = vec4(kept,
   frames)`), and zero where no surface stood. The lit shader was multiplying the
   bounce by a number that runs to 64. The same class of unit error 7ay recorded
   when linear depth arrived in this channel and read as a feedback loop. Under the
   signal the irradiance is taken whole and the count is read as what it is: one or
   more means this pixel has an estimate, zero means the field answers.
3. **A null texture into `SetTexture`** — segfault, not a validation message. Once
   `haveIndirect` could be true with no texture behind it (which is the point: the
   picture arrives in `DrawLit`, after `BeginScene`), both binding-16 sites had to
   fall back to transparent black rather than pass the null through.

**Open, filed here rather than fixed:** (a) the contract's bound has no range
compression and no firefly bound on the fresh sample, which `gi_denoise` had and
which a hemisphere estimate wants — it belongs to RT-5, where the bound is the
subject, and it changes what all four signals average; (b) the full-resolution
contract on a half-resolution signal, priced above at ~0.63 ms; (c) the traced
bounce is worth under 0.6 levels in both test scenes, which is a question about
the scenes or the GI settings rather than about RT-3, and is the reason this item
has no picture to show for itself.

### RT-3.1 — ✅ done 2026-09-07 (uncommitted, both copies staged)

**Owner-directed, and the reason is worth writing down.** RT-2 and RT-3 both
traced at half resolution, upsampled to the frame, and ran the contract there.
The argument for it was real -- the contract validates history by the surface
under each texel and reads depth, normal and velocity with
`texelFetch(..., ivec2(gl_FragCoord.xy))`, which is only honest on the
G-buffer's own grid -- but the conclusion was the smaller diff, not the better
design: **the standard arrangement is to filter at the resolution the signal was
traced at and spend one cheap bilateral upsample at the end**, and what that
needs is guidance lanes on the coarse grid, which is a new pass rather than a
change to shared code. The owner called it: *"you SHOULD NOT take the easy
approach, now rework the AO and GI task and make them use half resolutions."*

**What it does.** A `Guidance downsample` pass writes the G-buffer's depth (32-bit,
it feeds an inverse view-projection), normal lane and velocity onto a signal's
grid, **each one a whole texel, never averaged** -- the average of two normals
across a silhouette faces neither way, the average of two depths is a plane
in front of one surface and behind the other, and the average of two velocities
is the motion of nothing. `addSignal` takes a `SignalGuidance` (the three lanes
and a divisor); passing none means the G-buffer and a divisor of one, which is
what the reflections and the direct light pass, so **they are untouched by
construction**. Built once per distinct divisor and shared, so GI and the
occlusion on the same rung pay for one downsample between them.

**Which texel the downsample picks, and the alternative that was measured.** The
one the contract itself would have read at full resolution: `t * d + d/2`, in
integer texels through `gl_FragCoord`, so no uv convention or row flip can come
between the two sides. **Nearest-to-camera -- the conventional rule -- was tried
and is worse:** on the bridge it took the error against the full-resolution
contract from 0.190 levels to 0.299 and p99 from 3.00 to 5.33. Matching the
grid the contract reasons on beats picking the most prominent surface in the
footprint.

**The tuning follows the grid.** `Slack`, `SmearTexels`, `YoungRadius` and
`MaxRadius` are all counted in texels, and a half-resolution texel covers twice
the screen; they are scaled by the divisor inside `addSignal`. Left alone, a
signal moved down would have held its history through twice the camera motion
before the smear cap bit, and blurred half as far across the picture.

**The occlusion needed one extra thing.** Its raw buffer is
`vec4(ao, linearDepth, 0, 0)` -- the compute pass carries its own depth in G for
its bilateral tap weights -- and that payload cannot enter the contract, whose
bound and moments are built from `Luma(rgb)`: with metres in the green channel
the luma is almost entirely the depth. So the resolve that already strips it
(white scene, intensity folded in) moved *down* to the occlusion's own
resolution, where its bilateral weights collapse to a passthrough. That also
keeps RT-2's order -- the intensity curve applied before accumulation, as it was
when the owner accepted the look.

**The cost, which is the point of the item.**

| chain (garage, realtime GI) | RT-3 | RT-3.1 | saved |
|---|---|---|---|
| GI: upsample + accumulate + three blurs | 0.891 ms | 0.268 ms | **0.62** |
| occlusion: resolve + accumulate + three blurs | 0.732 ms | 0.258 ms | **0.47** |
| the guidance downsample (shared) | — | 0.018 ms | −0.02 |

| frame | RT-3 | RT-3.1 |
|---|---|---|
| garage, `--gi-signal=on` | 16.62 ms | **14.97 ms** |
| garage, `--gi-signal=off` (the occlusion alone moved) | 15.04 ms | **14.79 ms** |
| garage at its own baked setting (occlusion only) | 11.63 ms | **11.39 ms** |
| camp, on / off | 6.93 / 6.21 ms | **5.92 / 5.67 ms** |

**The GI signal now costs +0.18 ms over the one-frame-late chain it replaced**
(garage), against +1.59 ms when RT-3 landed. Every scene with an occlusion
signal gets the 0.24 ms whether or not it traces GI.

**The picture: small everywhere, except on thin geometry.**

| comparison | mean \|d\| | p99 | max | > 2 levels |
|---|---|---|---|---|
| garage, occlusion alone, full-res contract → half | 0.081 | 2.00 | 63.0 | 0.95% |
| garage, both signals | 0.084 | 2.00 | 62.7 | 0.94% |
| camp, both signals | 0.119 | 2.00 | 48.3 | 0.85% |
| **bridge (Headland), occlusion alone** | **0.190** | **3.00** | **178** | **1.20%** |

The garage and camp diffs are thin lines on beam, pillar and car edges. **The
bridge is the one that matters and it is a real loss:** the cables and vertical
suspenders come out darker -- signed −0.577 levels over the cable band, worst
pixel 207 -- while the water and sky are unchanged (+0.029). One-to-two-pixel
geometry is where half-resolution filtering loses, because a coarse texel's
guidance describes whichever surface the selection picked and a cable often is
not it; the full-resolution contract could keep a per-pixel history for the
cable and this cannot. **Owner's word (2026-09-07): the bridge cables are a
known problem with a flicker of their own, to be dealt with separately** -- so
this is filed as evidence for that work, not as a blocker here. It is a
specific, reproducible symptom of the same thin-geometry weakness.

**A normal term in the upsample was tried and measured out.** Weighting each tap
by how nearly it faces the way the pixel does, on the theory that the residual
edge lines were the upsample spreading one surface over its neighbours: 0.081 →
0.079 levels, p99 unchanged at 2.00, share of pixels off by more than two from
0.95% to 0.90% -- for 0.28 ms. The residual is the contract validating on the
coarser grid, not the upsample choosing badly, and those edges are edges in
*depth*, which the existing term already sees. Removed; the reasoning is kept in
`signal_upsample.rvshader`'s header because it is an obvious idea to have twice.

**Shape now, both signals:** trace (or compute) at the dial's resolution →
[occlusion only: resolve, stripping depth and folding in the intensity] →
`Guidance downsample` → accumulate → three a-trous blurs → `SignalUpsample` to
the lit pass's grid → the lit shader reads by texel. `gi_upsample.rvshader`
became `signal_upsample.rvshader` and `PostProcess::GiUpsample` became
`SignalUpsample`, since two signals share it now. `PostProcess::Dispatch` grew a
third colour attachment for the guidance pass's three lanes.

**Verified unchanged:** the reflections and the direct light (divisor one, the
G-buffer's own lanes -- the same arguments they passed before); the garage at
its own baked setting; raster mode (`--ray-tracing=off`, 11.80 ms, clean).

### RT-6 — ✅ the geometric half done 2026-09-07 (uncommitted, both copies staged)

**What it does.** The temporal resolve can now refuse a history because it is a
*different surface*, not only because it reprojected off screen. `taa_guide.rvshader`
packs the G-buffer's clip depth, octahedral normal and signed object id into one
RGBA32F lane and keeps it (the G-buffer is single-buffered and transient, so a
copy is the only way last frame's identity survives); the resolve compares the
two and, where they disagree, **searches the eight neighbours before giving up** --
SVGF's rule, the one `reflection_accumulate` already runs. The colour box stays:
geometry answers "is this the same surface", the box answers "is the light on it
the same", and neither substitutes for the other. `--taa-geometry=off` is the
reference arm.

**It fires where it should.** Measured by staging a probe that paints refused
pixels: **1.1–1.7% of the garage's pixels per frame under the dolly** against
0.10–0.22% for the off-screen rejection that was the only refusal before, and
**3.5–4.2% on the bridge**. On a probe frame they sit on the car's and poles'
silhouettes, the ceiling beams and the pipes -- exactly where a surface is being
uncovered. About a quarter of them come from the normal test alone, some of that
on the graffiti wall's normal map rather than on geometry; left in, because the
neighbour search recovers the history rather than discarding it.

**The picture, and it is the point.** `build/rt3/taa_car_sidebyside.png` (bridge,
Deck camera, frame 90, the 320x200 window where the two arms disagree most, at
3x): the tower's horizontal members, the lamp standards and their heads, the deck
markings, the suspender ropes and the railing are all **visibly sharper** with the
test on. What TAA was doing was keeping history across surfaces it had no right
to and blurring the result.

**The two metrics in this repository could not see it, and that is worth
recording.** "Per-frame change mid-dolly" and "settle to the arm's own converged
still" both scored the geometry arm slightly *worse* (poles 8.34 against 7.79;
settle 2.63 against 2.36). **Both proxies reward keeping more history -- which is
what a ghost is** -- so neither can separate "less ghosting" from "worse". A 4x
supersampled reference did not help either: the RMS against it is 28 levels,
dominated by the difference between SSAA and TAA rather than by disocclusion.
The side-by-side crop is what settled it. Do not use those two proxies to judge a
history-rejection change again.

**The neighbour search earned its place.** Hard rejection alone -- take this frame
whole -- was roughly a wash: it trades a ghost for aliasing at every silhouette.
With the search the same numbers move back toward the reference arm (poles 8.69 →
8.34, car 12.57 → 11.66, settle on the car 3.77 → 3.57) while the sharpness win
stays. Nine taps, and the history fetch turns point where a neighbour served,
because the texels between belong to the other side of the edge.

**Cost:** the guide pass 0.05 ms, the resolve 0.132 → 0.270 ms; about 0.19 ms in
all, and a full-resolution RGBA32F pair, allocated only under TAA. Garage 11.24 →
11.28 ms, bridge Headland 14.43, camp 5.35 -- all within noise of where RT-3.1
left them. FXAA and the other modes allocate nothing and are untouched.

**A defect of the same kind as RT-3's, caught by the same probe.** The two lanes
were declared, bound in `Dispatch`, read by the shader -- and **never passed into
the call**. The patch added the comment saying where they rode and not the two
arguments. Every arm was bit-identical (max 0.0) and the geometric refusal count
was exactly 0.000%, while a probe that returned "refuse everything" *did* change
the frame, because it never read the textures. **The probe that finds this is a
refusal counter, not a frame diff:** paint the refused pixels and count them.
Zero, against an off-screen count that is non-zero, is the tell.

**The still-feedback half is not done, and is deferred by the owner to RT-8.**
The rule is now per-pixel *and* geometrically validated -- reaching that line
means the surface test passed -- but the *value* stays a project setting, because
the sea reads zero velocity and no lane in the G-buffer says "this is water".
Globalising it would smear the bridge's water at 0.98, which is why the project
forces it to 0 there today. Owner's word, 2026-09-07: "your concern about water
motion vectors is valid but we will deal with that once we get on RT-8."

### RT-4 (partial) — the composite's open measurement, answered: it stays after the resolve

**The question RT-4 filed:** whether the traced reflection should keep being
composited *after* the temporal resolve, or pass through it like everything
else. The ninth entry suspected the former was the larger half of the edge
flicker, so the expectation was that moving it would help.

**It was moved, measured, and moved back.** The whole chain -- trace, resolve,
contract, composite -- is independent of the resolve (the trace reads the
G-buffer's depth and normal and the acceleration structure, never the lit
colour), so it lifts above the resolve cleanly, reading last frame's ray-budget
tile map the way RT-2's occlusion and RT-3's bounce already do.

**The numbers said yes and the picture said no.** On the garage floor mid-dolly:

| | detail (HF) | frame-to-frame |
|---|---|---|
| blur 12, composite after (shipped) | 6.77 | 5.87 |
| blur 12, composite before | 8.04 | 3.17 |
| blur 3, composite before | 9.01 | 3.12 |
| blur 0, composite before | 9.65 | 3.13 |

Instability halves at every setting and stops depending on the blur at all --
which reads like proof that the blur was standing in for the filter. **It is
not.** `build/rt3/reflection_four_way.png`: with the composite before the
resolve the floor smears into horizontal bands and the wet gravel disappears.
**The resolve reprojects by the *surface's* motion, and a mirror image does not
move with the surface carrying it** -- which is precisely why
`reflection_accumulate` reprojects by the virtual image instead. One velocity
buffer cannot serve both, so the composite stays where it was.

**The metric that misled is the one this session had already written down as
misleading** -- RT-6's record says both "frame-to-frame change" and "settle"
reward keeping more history, and smear is keeping more history. It was used as
the primary measure here anyway. **For anything touching a temporal filter, the
crop is the measurement and the scalar is the hint.**

**What did come out of it: the ground reflection is sharper.** The young-history
blur goes 12 texels → 3 (`Renderer3D::ReflectionSignal`), owner-asked. Swept and
judged by eye: 12 is soft, 3 has the gravel crisp, 0 trades texture for grain.
`--reflection-blur=<texels>` is the measurement dial.
`build/rt3/reflection_before_after.png`.

**Still open for RT-4/RT-5:** giving the resolve the reflection's own motion --
the virtual image's, which the accumulator already computes -- is the only way
the composite could move, and it needs a second velocity lane. Not attempted.

### RT-6.1 — ✅ done 2026-09-07 (uncommitted, both copies staged)

**Owner-filed, from RT-4's finding.** RT-4's measurement said the reflection
could not be composited before the temporal resolve, because the resolve
reprojects by the *surface's* motion and a mirror image does not move with the
surface carrying it -- the wet floor smeared into horizontal bands. The owner's
answer: give the resolve the image's own motion. *"This right here please
implement it and just call it RT-6.1."*

**What it does.** The specular accumulate already measures the thing: `shift`,
how far the picture at each texel moved since last frame, both sides on the
unjittered grid, reprojected by the virtual image at `P + sight * image`. It now
writes that out as a fourth attachment, in the scene velocity lane's own units,
so no second convention exists. The composite -- which knows how much of each
pixel is reflection, because the lit shader wrote that weight into the scene's
alpha -- emits the velocity the resolve should use: **chosen, not blended**, the
image's where the reflection carries more of the pixel's brightness than
everything else in it, the surface's otherwise. A weighted average of two
motions describes neither, which is why every velocity read in this engine is
point sampled. Then the whole reflection chain moves above the resolve, reading
last frame's ray-budget tile map as RT-2's occlusion and RT-3's bounce do.

**The picture, mid-dolly on the garage floor** (detail = high-frequency energy,
change = frame to frame):

| | detail | change |
|---|---|---|
| after the resolve, blur 12 (what shipped) | 6.77 | 5.87 |
| after the resolve, blur 3 | 10.70 | 8.86 |
| **before it, no motion lane, blur 0** (RT-4's rejected arm) | 9.65 | 3.13 |
| **before it + the motion lane, blur 0** (this) | **9.00** | **5.98** |

**A third more detail than what shipped, at the same stability** -- and by eye
(`build/rt3/rt61_decision.png`) it has the sharp arm's gravel without the sharp
arm's grain, which is the whole point: the resolve is finally filtering the
reflection. The rejected arm's 3.13 is smear, not stability;
`build/rt3/rt61_four_way.png` shows its horizontal banding gone here.

**And the young blur retires.** Twelve texels of it existed to stand in for a
temporal filter the reflection never got. Between 3 texels and 0 it now moves
the measurement by almost nothing (8.51 → 9.00), which is the measurement saying
it has stopped doing work. `YoungRadius = 0` for the reflection signal;
`--reflection-blur=<texels>` remains as the dial.

**Cost:** one more RGBA16F attachment on the reflection history and its
accumulate, and one more on the composite (R16G16). Garage 11.22 ms, bridge
Headland 16.09, Pier 12.82, camp 5.17 -- all within noise. Every AA mode clean
(none 10.21, FXAA 10.59, MSAA 11.31): where the reflection chain does not run,
`reflectionMotion` is invalid and the resolve reads the scene's lane exactly as
before.

**The trap this closes, and it is worth stating plainly.** The composite's own
header had documented the surface-motion problem since 2026-09-05, with a
measurement. RT-4 re-derived it on 2026-09-07 and reverted. Neither time was the
obvious next question asked -- *the accumulator already computes the right
motion; why does the resolve not have it?* -- because both times the conclusion
was "the composite must stay after the resolve" rather than "the resolve is
missing a lane". A constraint that comes from a missing input is not a law.

### RT-6.2 / RT-6.3 — material-aware temporal work, 2026-09-07 (uncommitted)

**RT-6.2, the clamp shaped by material: built, measured, and it does nothing.**
The theory was the standard one -- a rough dielectric's shading does not change
with the view, so its history is right and clipping it toward the neighbourhood
mean every frame is what blurs the texture; a smooth metal's history is genuinely
stale and wants the tight box. The resolve now reads the G-buffer's roughness and
metallic (binding 8, from the lane it already had) and widens the box by
`(1 - metallic) * smoothstep(0.15, 0.5, roughness)`.

**Swept 1x, 2x, 4x, 8x, it changes the picture by under one per cent everywhere**
-- garage wall 22.868 / 22.863 / 22.849 / 22.833, floor 8.999 / 9.024 / 9.057 /
9.105. The mechanism is live (widening to 40x moves the frame 0.55 levels), so
this is a real negative result, and the reason is worth keeping: **on a detailed
surface the 3x3 neighbourhood box is already wide**, because the neighbours
genuinely differ. The clamp only bites where the neighbourhood is flat -- where
there is no detail to lose. It cannot be what is blurring the texture.

**What is:** the current sample is Gaussian-filtered over the 3x3 before blending
(`kFilterSigma = 0.5`, Unreal's filtered-current). Dropping it to 0.15 raises
detail on every surface -- wall 22.86 → 23.28, floor 9.02 → 9.53, poles 13.74 →
14.32 -- at about four per cent more frame-to-frame change. **That is the lever
for "TAA blurs essential detail", and it is not material-dependent**, which is
why the material-aware version of the wrong lever measured flat. Left at 0.5;
making the *filter* stability-aware is the item to try, not the clamp.

**RT-6.3, the reflection-direction test: built, live, kept.** Every history test
the reflection accumulator had asks about the *reflector* -- same plane, same
facing, about as rough -- and on a smooth metal the camera is orbiting, all of
them pass. The surface has not changed. What changed is `R = reflect(-V, N)`,
which swings with the view, so the history is accepted and the old image is
dragged across the metal. **That is why smearing is worst on shiny surfaces**, and
no amount of surface validation can see it.

The reflector's normal is already stored, so with last frame's eye recorded
beside its view-projection the previous reflection direction is exact rather than
guessed. Their agreement is scaled by the lobe -- cos(1.8 degrees) for a mirror,
0.86 for fully rough -- and **shortens the memory rather than refusing the
history**: refusing would trade a smear for the noise of a one-frame estimate on
exactly the surfaces that show noise worst. Free (the frame is unchanged within
run-to-run spread: 11.09, 11.36, 11.75 across reruns), and the chrome poles show
visibly shorter vertical smear (`build/rt3/rt63_metal.png`).

**Against the specular anti-smearing specification the owner supplied**, this
engine already had: the compact RT surface buffer (§1), the reflection direction
(§2), reprojection (§3), depth, normal, roughness and disocclusion validation
(§4A-D, though the normal test is a hard cutoff rather than a smoothstep),
roughness-aware bound width and memory (§6 in part), variance-aware clamping
(§8), and a ray budget (§10). **§5, reflection-direction validation, was the one
piece genuinely missing** -- the specification calls it the most important part,
and it was right.

**Still not done from that specification:** a material-*id* test in the
accumulator (it tests roughness, not the id, so a metal/dielectric boundary
passes), smoothstep confidences for the normal and depth tests instead of
cutoffs, confidence-driven ray allocation (§10), and the confidence debug views
(§11).

**Third dead-plumbing defect of the session, same shape as the other two.** The
direction test measured bit-identical until it was probed: the eye was recorded
on the temporal resolve's motion record, and the reflection signal keeps *its
own*. `PreviousEye.w` stayed zero and the branch never ran. The probe that finds
this class -- change a constant to an absurd value and check the frame moves --
is now three for three. **Run it on every new input before believing a number.**

### RT-6.4 — the current-sample filter, and the tests that fade — ✅ 2026-09-07 (uncommitted)

**The filter, 0.5 → 0.15.** `kFilterSigma` blurs the current sample over the 3x3
before it is blended -- Unreal's filtered-current, there so the jitter does not
move a bright edge a tenth of the way to whichever side it landed on. **It is
the blur**, and RT-6.2 found it by elimination: the material-aware clamp measured
flat because on a detailed surface the neighbourhood box is already wide, so the
clamp only bites where there is nothing to lose. This bites on every pixel of
every frame. Narrowing it is also safer than it was when 0.5 was chosen -- the
edge wobble it guards against is now held by RT-6's geometric test and its
neighbour search too.

| garage, mid-dolly | detail before | after | change before | after |
|---|---|---|---|---|
| wall | 22.493 | 22.660 | 18.614 | 18.783 |
| floor | 9.032 | **9.535** | 6.019 | 6.391 |
| poles (metal) | 13.660 | **14.240** | 8.401 | 8.778 |
| car (metal) | 10.156 | **10.866** | 4.974 | 5.291 |

**Settled at 0.25, not 0.15** (owner's eye, 2026-09-07): 0.15 read a shade too
sharp to them. The numbers above are 0.15's; 0.25 keeps most of the detail for
about half the extra frame-to-frame change.

**Noted, not a regression:** the red fringe along the underside of the ceiling
tubes is the fixtures' own housing, present at 0.5 as well (0.97% of the band's
pixels against 1.07% at 0.25 -- a sharper filter keeps an edge that was always
there). Not introduced by this change.

Detail up 0.7–7%, frame-to-frame up in proportion -- and the crop
(`build/rt3/rt64_compare.png`) says the gain is texture, not grain: the car's
livery text is legible where it was mush, the wheel spokes are separate, the wet
floor's gravel is crisp, and nothing has picked up aliasing.

**The tests fade instead of snapping.** The accumulator's normal and plane tests
were hard cutoffs -- a history whole at dot 0.801 and gone at 0.799. Nothing in
a picture changes that sharply, so what the eye reads as the camera turns is the
memory stepping out rather than a reflection catching up. The tests **stay
binary where they gate the neighbour search**, because that search needs a yes or
a no; what became smooth is how far the winner is then trusted --
`smoothstep(0.8, 0.94, facing)` times the plane's, folded into the memory exactly
the way RT-6.3's direction confidence is, and multiplied rather than min'd so a
candidate marginal on two counts is worth less than one marginal on either. This
is §4A/B of the owner's specular specification.

**Cost:** none measurable. Garage 11.21 ms, bridge Headland 16.26, Pier 14.57,
camp 5.89; every AA mode clean.

**Still open from that specification:** the material-*id* test (the accumulator
checks roughness, not the id, so a metal/dielectric boundary passes),
confidence-driven ray allocation (§10), and the confidence debug views (§11).

## The S series, for the record (owner asked 2026-09-06)

WR-16's own build sequence (`docs/RAY-BUDGET-DESIGN.md` Part IV), the oldest of the lists, and where it stands:

| step | what | status |
|---|---|---|
| S0 | the ray counters, the report lines, the debug views `rays` / `lights` / `confidence` / `importance`, the calibration | ✅ done 2026-09-04 (0.85 ms, bit-identical) |
| S1 | the pre-check: K shadow rays a pixel by weighted reservoir sampling, no reuse | ✅ measured 2026-09-04 (the verdict that shaped S4); **its instrument `--shadow-budget` removed in RT-1** |
| S2 | the cheap hit walk: 16-byte cull records, the per-cell live sublist | ✅ done 2026-09-04 (diff of zero; the bridge −8–9%) |
| S3 | the budget: the allocator's stability (averaged demand, dead band, dwell) | ✅ first half built and passing 2026-09-05 (`50e11b7`); **the eight lanes and the controller not built → RT-9** |
| S4 | the shadow spender: the sampler (a), the water's choose/shade passes (b), the light averaged (c), the world-space lamp grid | ✅ complete 2026-09-04 on the water (the choice reuse measured as a loss and off); **on land it is T5 + RT-1 now; the reuse done properly is RT-10** |
| S5 | the water's mirror and refraction rays at half resolution with a hit-distance reconstruction | ✅ built (the sea's reflection at half resolution, four taps weighted by ray distance; WR-18's rays); **folds into RT-8 when the water joins the G-buffer** |

Nothing in the S series is left to build on its own: what remains of it lives in RT-8, RT-9 and RT-10.

## After the RT series: the WR items to revisit for the G-buffer

WR-13 (specular AA — the widened roughness now rides the G-buffer, so every pass shades with it); WR-15 (soft shadows — the disc sample is the direct pass's now, and the contract, not TAA, integrates it); WR-17 (retire the preset's thinning columns once RT-1 ships); WR-18 (the water's rays — RT-8); WR-16 (S3 → RT-9, S4 → RT-10, S5 → RT-8, the counters honest); WR-8 and WR-9 (RT-7); WR-10 (the world grid at hits — unchanged, serves RT-11). WR-1, 2, 5, 11, 12, 14 are untouched by any of this.
