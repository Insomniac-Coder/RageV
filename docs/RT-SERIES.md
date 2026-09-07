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

## Status at a glance

**Thirteen of twenty-eight items are closed, three more are part-done, twelve are open.** (2026-09-07 night.) Effort is solo days at this week's pace; the detail behind each number is the complexity table below.

| # | status | effort | risk | in a line |
|---|---|---|---|---|
| RT-1 | ✅ done 2026-09-06 | — | — | the lit shader stops walking lights under RT |
| RT-2 | ✅ done 2026-09-06 | — | — | ambient occlusion as a signal |
| RT-2.1 | ✅ done 2026-09-07 | — | — | the parallax march at mip 0, not the raster |
| RT-2.2 | open, **at the end of the series** | 1 d | low | the deferred resolve; the albedo lane widens first |
| RT-3 | ✅ done 2026-09-07 | — | — | GI as a signal of this frame |
| RT-3.1 | ✅ done 2026-09-07 | — | — | the contract at each signal's own resolution |
| RT-4 | **partial** — the composite measurement answered (it stays after the resolve); the trace's move and R11 open | 2 d | moderate | reflections as an instance of the shared code |
| RT-5 | open | 3-4 d | **high** | the contract validates by the G-buffer; the blur goes |
| RT-6 | ✅ geometric half done 2026-09-07; **the still-feedback half waits on RT-8** | — | — | TAA on the G-buffer |
| RT-6.1 | ✅ done 2026-09-07 | — | — | the reflection's virtual-image motion lane |
| RT-6.2 | ✅ **re-run 2026-09-07: the negative result expired** | — | — | the material-aware clamp, live on RT-6.8's box |
| RT-6.3 | ✅ done 2026-09-07 | — | — | the reflection-direction test |
| RT-6.4 | ✅ done 2026-09-07 | — | — | the current-sample filter down; the tests fade |
| RT-6.5 | ✅ **done 2026-09-07** — the metallic; the id deferred to RT-14 | — | — | the accumulator tests the material, not just roughness |
| **RT-6.6** | ✅ **done 2026-09-07** | — | — | the moments follow the texel the colour came from |
| **RT-6.7** | ✅ **done 2026-09-07** — correct, and measurably unreachable | — | — | the sky/geometry transition is a disocclusion |
| **RT-6.8** | ✅ **done 2026-09-07** | — | — | the colour box is built from this surface only |
| **RT-6.9** | open — **new** | 0.5 d | low | a camera cut throws the history away |
| **RT-6.10** | open — **new** | 1 d / 2-3 d | low-moderate | the accumulator validates what the ray hit |
| **RT-6.11** | open — **new** | 0.5 d | low | Catmull-Rom re-measured; its negative result expired |
| RT-7 | open | 4-5 d | moderate-high | the tubes as LTC line lights |
| RT-8 | open | 4-6 d | **high** | the water on the G-buffer |
| RT-9 | open | 2-3 d | moderate | the budget's shadow lane, and confidence drives allocation |
| RT-10 | open | 5-7 d | **high** | ReSTIR DI on the G-buffer |
| RT-11 | open | 1-2 d | low-moderate | next-event estimation at GI and reflection hits |
| RT-12 | ✅ **done 2026-09-07** | — | — | the signal debug views, complete |
| RT-13 | **skinned + layered ✅ done inside RT-2**; transparent open | 0.5 d to decide | low | every opaque surface in the G-buffer |
| **RT-14** | ✅ **done 2026-09-07** — and it says do not pack the G-buffer | — | — | the G-buffer's bandwidth, measured before anything is packed |

**Six new items on 2026-09-07**, all from two outside reviews of the codebase — five verified defects and one expired negative result. The section after the complexity table records what those reviews got right, what they asked for that already exists, and the one place they were argued with.

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
| **RT-6.6** | **The moments follow the texel the colour came from.** RT-6's neighbour search picks a history texel and reads the colour there (`texelFetch(u_History, historyTexel)`), while the moments are still read at the reprojected uv (`texture(u_Moments, historyUV)`). So on every pixel the search recovers, the blend weight and the variance floor belong to a **different surface** than the colour being blended: `prevMoments.x` sets the alpha and `.y`/`.z` set the box's floor, and both are wrong exactly where the search fired -- 1.1-1.7% of the garage's pixels a frame, 3.5-4.2% of the bridge's. Return the resolved texel from `MatchingTexel` and point-sample both there. | second review P0 §2, verified in the shader | **RT-6's own defect:** it added the search and did not move the moments with it. | small -- **no trade-off to measure, do it first** |
| **RT-6.7** | **The sky/geometry transition is a disocclusion, and today it is the one thing that always passes.** `Matches()` opens with `if (now.x >= 1.0 \|\| was.x >= 1.0) return true;` -- the history is accepted whenever *either* side is sky. That is right when both are sky and wrong at the transition, which is exactly where geometry appearing inherits stale sky and geometry leaving inherits a stale object. Both sky, accept; one of the two, refuse; otherwise the id, depth and normal tests unchanged. **The garage cannot show this** (interior, no sky); the bridge is nothing else -- cables, suspenders, lamp standards and tower ribs against sky, and RT-3.1 already measured that band as this engine's worst thin-geometry case. **Two-sided, and that is the whole item:** refusing sends the pixel to the current frame whole when the nine-tap search also fails, trading a ghost for aliasing on the members that already flicker. | second review P0 §1, verified in the shader | The header argues for the current form (*"the sky reprojects perfectly well"*) and is right about one of the two cases it covers. | small to write; **a measurement, not a landing** |
| **RT-6.8** | **The colour box is built from this surface only.** The exact complement of RT-6: the *history* side learned to ask whether a texel is this surface and the *box* side never did. The 3x3 takes all nine taps whatever they are, so at a silhouette the box spans two surfaces and is **widest precisely where disocclusion happens** -- too wide to catch the wrong history it exists to catch. Gate each tap with the `Matches()` the resolve already has; fall back to a narrower neighbourhood or the centre sample where too few agree. **It may reopen RT-6.2**, whose flat result rested on *"on a detailed surface the box is already wide"* -- part of that width at an edge is a foreign surface, not detail. | second review P0 §3, verified in the shader | Eight guide fetches and their decodes on every pixel; the fallback rule is the design. | small-medium |
| **RT-6.9** | **A camera cut throws the history away.** Every `Invalidate()` in the frame graph means "this filter did not run this frame"; nothing detects a discontinuity in the camera itself -- a teleport, an editor-to-game camera switch, a scene load, an FOV jump. Reprojecting across one of those is meaningless and what it produces is a whole frame of smear. RT-6.3 already records the previous eye in `CameraMotion`, so the position test is a subtraction. **Prefer the explicit signal where the engine knows** (scene load, camera replacement) and keep a distance/angle heuristic only for what it cannot. | second review P1 §9, verified (no such check exists) | The one temporal failure with no gradual version: it is a whole frame or nothing. | small |
| **RT-6.10** | **The accumulator validates what the ray *hit*, not only what it bounced off.** Both reviewers found this independently, and it is the one hole **RT-6.3 cannot close by construction**: a static polished wall, a static camera, a moving object in the reflection. Every reflector test passes -- same plane, same normal, same roughness -- and the direction test passes too, because the eye did not move and so `R` did not swing. The memory stays full and the moving object smears. The quantity is already carried: `o_Surface.a` is the virtual image distance, and today it is **smoothed across frames** (`image = mix(atSurface.reflector.a, image, 1/n)`) rather than compared. Small version: a confidence on that comparison, roughness-scaled like RT-6.3's, folded into RT-6.4's `matchConfidence`. Full version: the hit's instance id, which needs a payload lane the reflection trace does not have. **Do the small one first and measure whether the id is still wanted.** Pairs with RT-6.5 -- that is the reflector's identity, this is the reflected content's. | first review §10, second review P0/P1 §5, verified | The reflection tests every property of the mirror and none of the picture in it. | small (the distance); medium (the id) |
| **RT-6.11** | **Catmull-Rom history, re-measured -- an expired negative result.** It was built, measured flat (19.830 against 19.774 moving, 3.817 against 3.816 still) and reverted, and `taa_resolve.rvshader`'s header keeps the reasoning: *"the neighbourhood clip is discarding the history the sharper kernel would have preserved, so the kernel has nothing to do."* **RT-6 changed exactly that** -- the geometric test and the neighbour search keep history the clip used to throw away, so the premise the measurement rested on is gone. Point-sample where a neighbour served (a sharp filter across an edge is the blend the surface test just refused) and leave disocclusions on the current frame. | second review P1 §8, against this repository's own record | A reverted result whose reason stopped being true is worth one afternoon. | small -- a re-measure, not a feature |
| **RT-7** | **The tubes as lights the rays can sample.** Emissive geometry becomes a light with endpoints (luminaire binding, WR-9); LTC line lights for the specular term (WR-8); the direct pass shades them like any lamp, so the floor's tube reflections stop depending on rare hits; rays skip the lens emission (no double count). | T9, R10, WR-8, WR-9 | The variance the reconstruction has been fighting all week is removed at its source. | large |
| **RT-8** | **The water on the G-buffer.** The water's surface prepass becomes a G-buffer layer (position, normal, roughness, wind, id, velocity — R7 lands here); its direct light comes through `DirectTrace`, its choose/shade/accumulate passes fold into the contract, its mirror and refraction rays are signals (WR-18's half-res pass); TAA gets its motion. **Both outside reviewers rate this P0 independently** (2026-09-07), on the argument this record already makes: a water pixel's colour and its temporal metadata describe different surfaces, and no temporal filter can reconstruct that. It stays where the owner put it in the order -- the note is here so the priority argument is on the record. | T12, R7, S4, S5, WR-18 | The sea is the one surface with its own copy of every system. | large |
| **RT-9** | **The budget's shadow lane and the one dial.** The allocator widened to K per tile (S3's widening), the direct pass reading the tile map like GI does, "rays per pixel" the one setting across land and water; the counters honest for every pass. **And the allocation driven by temporal confidence** (owner's spec §10, filed here 2026-09-07): fewer rays where the history is trusted, more where it was refused, most where a pixel was just disoccluded or its reflection direction swung -- all three of which are now measured per pixel (RT-6, RT-6.3, RT-6.4) and thrown away. The constraint is the specification's: **do not raise the ray count globally**, spend the same budget where reconstruction cannot answer. **Its precondition, named by both reviews and filed here 2026-09-07: the confidence exists and is thrown away every frame.** RT-6.3's direction agreement, RT-6.4's match confidence, RT-6's found-or-refused and the moments' validity lane are each computed and discarded at the end of the pass that made them. One lane storing the number the filters already produce is what makes this item buildable -- **the larger "unified `RTConfidence`" architecture the first review proposes is not needed to start**, and building it before there is a consumer is how four signals' tuning gets coupled for nothing. | S3, WR-16, owner's "rays per pixel" | Now there is a consumer to size the lane for (Part IV decision H). | medium |
| **RT-10** | **ReSTIR DI on the G-buffer.** The choose/shade split (the water had it) for every opaque pixel, temporal and spatial reuse of the choice validated by id, depth and normal, feeding the same contract. | T10, S4, WR-16 M4 | The many-light scenes (the bridge: 78 lights a pixel) are where K = 4 is not enough. | large |
| **RT-11** | **Next-event estimation at GI and reflection hits** with the resampled light (the 7 M rays the hits trace today, chosen by importance). | T11 | The last place a ray shades every light. | medium |
| **RT-12** | **Signal debug views, complete:** history length, refusal, reach, the K choice, the raw fresh picture, per signal, on one log ramp (the direct-light view saturates at any linear scale). **Plus the confidence set** (owner's spec §11, filed here 2026-09-07): the combined history confidence, the reflection direction as RGB and its frame-to-frame difference, and the rejection reason split by which test refused it -- depth, normal, material, disocclusion -- and the rays actually allocated per pixel. **This session hit plumbing that was declared, bound, read and never connected three separate times**, each caught only by staging an absurd constant and checking the frame moved; a refusal-and-confidence view would have caught all three in one look. **And the rejection reason as an enum, per test** (both reviews, 2026-09-07): the reflection accumulator already writes one (`g_Refusal`, in the integer part of `o_Extra.b`) and TAA writes none, so a TAA ghost cannot be traced to the test that let it through. One small integer lane -- off screen, id, depth, normal, sky transition, material, direction, hit -- coloured by reason. | R6 remainder | Tune with views, not the final image. | small |
| **RT-13** | **Surfaces outside the G-buffer join it: skinned, layered (terrain), and transparent (the car's glass, OIT).** RT-1 found the first two on the bridge -- the G-buffer pass draws only the plain and masked kinds, so under the direct-light signal the terrain and the characters had no light in the pass and none from the loop; they keep the loop for now (`RV_SKINNED` / `RV_LAYERED` compile without the signal's inputs). The fix is a G-buffer variant per kind and their draw in the G-buffer pass; transparent surfaces decide between a thin layer and the in-shader path. | RT-1's finding, new | Every opaque surface must be in the G-buffer or every signal skips it. | **skinned + layered ✅ done inside RT-2** (see its record); transparent: decide first, small |
| **RT-14** | **The G-buffer's bandwidth, measured before anything is packed.** Both reviewers raise it and both put it last, which is right: the lanes have grown with every item in this series -- surface, id, velocity, RT-6's TAA guide pair, RT-6.1's fourth reflection attachment -- and nothing has ever measured what they cost. **Profile first:** write and read bandwidth per pass, cache behaviour, where the pass time actually goes. Then A/B any packing against a diff image. Candidates: the id lane's width, the velocity format, the TAA guide's sixteen bytes, and the albedo lane -- which **RT-2.2 wants widened, not narrowed**, so the two are decided together or not at all. **Do not pack on theory:** RT-2.1 measured a "doubled rasterisation" that turned out to be a parallax march, and the same mistake inside a format change is a picture regression bought for nothing. | first review §20-21, second review's ordering | The one item both reviews agree comes after everything else. | small to measure; unknown to act on |

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
| RT-6.5 | 0.5-1 d | low | The id lane is already kept for TAA; the work is deciding how a metal/dielectric boundary should weigh against a normal that still matches, and it folds into RT-6.4's confidence rather than adding a cutoff. |
| RT-6.6 | 0.5 d | low | Nothing to trade -- the moments must describe the colour they weight. The only care is that both reads move together; a second `texture()` left behind reintroduces the same bug at a different texel. |
| RT-6.7 | 1 d, most of it measuring | moderate | Three lines to write. The risk is entirely that it buys a ghost fix with aliasing on the bridge's sub-pixel members, and **the garage cannot participate in the measurement at all** -- it has no sky. Three cameras under the flicker protocol. |
| RT-6.8 | 1-1.5 d | moderate | The fallback where too few neighbours match is the design: the centre sample alone reintroduces flicker, a wide fallback reintroduces the problem. Eight fetches on every pixel of every frame is a real cost to measure, and RT-6.2 may need re-running afterwards. |
| RT-6.9 | 0.5 d | low | Choosing which engine events fire it explicitly is most of it. The heuristic thresholds are the part that can be wrong, and one set too tight throws the history away during a fast pan. |
| RT-6.10 | 1 d the distance; 2-3 d the id | low-moderate | The image distance is a *derived* quantity -- curvature and the lobe are folded into it -- so its tolerance is not the ray's own and needs sweeping. The id version needs a payload lane through the reflection trace and its history. |
| RT-6.11 | 0.5 d | low | A re-measure of code that already existed once. The only new work is the point-sample rule where a neighbour served. |
| RT-7 | 4–5 d | moderate–high | A new light type (endpoints, length) through the scene data, the light record and the editor; the LTC fit tables (or the representative-point capsule first, which WR-7 half has); the sealed-room rule — a pixel that takes the analytic term must never also receive the lens emission from a ray — and a seam-free roughness window between the analytic term and the traced mirror. Verified against the 400-frame unclamped truth. |
| RT-8 | 4–6 d | **high** | The sea has bitten every session (the prepass crossing, lamps that cast nothing, the choice reuse); it is a second G-buffer layer, not a lane, with its own BRDF; the Gerstner velocity needs the wave evaluated twice per vertex; every check is the bridge at three cameras under the flicker protocol. |
| RT-9 | 2–3 d | moderate | The tile map's four lanes are full (a second map or a repack); the allocator's restlessness has a history — the sixty-second still test at 0.01 changes per tile per second is the bar, and the direct signal's own temporal moments are the new importance input to get right. |
| RT-10 | 5–7 d | **high** | Bias control (M caps, the MIS weights for spatial reuse, visibility reuse or not); the water's version measured as a loss for a physical reason, and land must be shown to differ; the garage shows little at K = 4, so the bridge's 78-light pixels are the test throughout. |
| RT-11 | 1–2 d | low–moderate | Mostly RT-1's score applied at the hit; the noise it moves into GI and reflections must be absorbed by their contracts, measured on the reflection arms. |
| RT-12 | 0.5–1 d | low | Plumbing; the log ramp is the only design. |
| RT-13 | 0.5 d to decide; 2–3 d if a layer | low | Deciding is most of it; a thin transparent layer is a bounded copy of the water's prepass. |
| RT-14 | 1 d to measure | low | The measurement *is* the item; whether anything follows is what it decides. It collides with RT-2.2, which widens the albedo lane rather than narrowing it -- the two are decided together. |

**Dependencies:** RT-9 wants RT-1's cheap score; RT-10 builds on RT-9's lane and RT-5's validation; RT-6's velocity rule wants RT-8 or a guard; RT-7 carries WR-9 inside it. **The whole series at this pace: roughly seven to eight weeks of solo days**, front-loaded with the medium items so the high-risk ones (RT-5, RT-8, RT-10) land on a validated contract.

## The two outside reviews (2026-09-07)

Two independent reviews of the codebase, read against the code rather than taken on their word. Roughly fifty suggestions between them: **five are real defects, verified at the line**; about twenty describe work already built (both were written against a picture of the engine that predates most of RT-6.x); the rest restate this list back to it.

**The five, and where each is filed:** the moments read at a different texel than the colour (RT-6.6), the sky/geometry transition always passing the history test (RT-6.7), the colour box spanning two surfaces at a silhouette (RT-6.8), no camera-cut invalidation anywhere in the frame graph (RT-6.9), and the reflection accumulator never validating what the ray hit (RT-6.10). The second review found the first four; both found the fifth independently.

**Two of them are this series' own defects, and that is the useful part.** RT-6.6 is RT-6's: it added the neighbour search and did not move the moments with it, so the search mis-weights every pixel it recovers. RT-6.8 is RT-6's other half: the history side learned to ask whether a texel is this surface and the box side was never asked the same question. Neither would have been found by looking at a frame — both need someone reading the shader against what it claims to do.

**What they asked for that already exists,** recorded so it is not re-litigated: variance-based history clamping (`ClipToBox` plus the `kTemporalSigma` moments floor, and the accumulator's own spread bound), reflection-direction validation and its roughness scaling (RT-6.3), disocclusion detection (RT-6), material-aware reflection validation (RT-6.5, filed), confidence-driven ray allocation (RT-9), the debug views (RT-12), ReSTIR DI, NEE and line lights (RT-10, RT-11, RT-7), the water's integration (RT-8), and per-signal temporal parameters (the contract's `SignalParams`).

**Where they were argued with.** The first review's central proposal is a unified `RTConfidence` built as its own phase before anything consumes it. The pieces already exist per signal; what is missing is a *lane*, not an architecture, and coupling four signals' tuning together before there is a consumer is a cost with no return. Filed as RT-9's precondition instead. The second review's Catmull-Rom suggestion is a result this repository already measured and reverted — but its stated reason stopped being true when RT-6 landed, which is why RT-6.11 re-measures rather than dismisses it.

**One thing both reviews are right about that is not a code change:** they independently rate the water's G-buffer integration P0, above most of what is above it in this order. That is the owner's call and the argument is now on the record in RT-8's row.

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

### RT-12 — ✅ done 2026-09-07 (owner-directed: *"finish the whole task, coming back to it again and again doesn't seem like a good approach"*)

**Twenty-five views, all verified to render what their name says.** Eleven are
new, three were **wrong**, and the frame itself is bit-identical (mean 0.000,
max 0.0, on the garage at frame 50 with both signals on).

**The defect, and it is the reason this item earned its place.** `debug_view.rvshader`
chose how to display a value with `if (mode == 8 || mode == 10 || mode == 12)`,
commented *"reflection-picture, direct-light, ao"*. The modes at those numbers
are reflection-picture, **direct-refusal** and **gi-light**. Found by capturing
every view at `--debug-view-mix=1.0`, where a ramp view collapses to the plain
frame and a picture view does not:

| view | showed | should have shown |
|---|---|---|
| `direct-light` | the frame **count** over 64 -- near black | the accumulated picture |
| `ao` | the frame count over 1 -- **near white** | the occlusion picture |
| `direct-refusal` | raw radiance over 6 | its refusal ramp |

**One of them had already cost something.** RT-2's record files *"the AO debug
view reads near-white on a linear ramp"* as evidence for RT-12's log ramp. It
was never the ramp. The view was reading the wrong channel, and a wrong
instrument produced a wrong open item that sat in the list for a day.

**So the fix is not the off-by-one.** Renumbering would leave the next person to
make the same mistake, and this item adds eleven views. The shader is now *told*
which channel and which display to use instead of deriving them from an ordinal,
and the four facts about a view -- source, attachment, channel, display -- are
one switch case each in the frame graph. A view can now only be wrong if its own
row is wrong.

**The eleven new ones cost no bandwidth.** The reconstruction contract gives all
four signals the same attachments and only the reflection's were ever looked at:

- `taa-refusal` -- **the temporal resolve now says why**. `o_Moments.w` was a 0/1
  validity flag that WR-16 S0 wrote for a consumer that does not exist yet; it
  now carries the clause that refused the reprojected texel, in the reflection
  accumulator's own encoding (0 kept, 1 off screen, 2 no history, 3 sky
  crossing, 4 object id, 5 depth, 6 normal) with a half added where the nine-tap
  search found nothing either. Zero still means "reused", so the future validity
  consumer is intact. On the garage mid-dolly it is green on every silhouette
  (object id), red on the car's and the pipes' edges (normal), yellow on the
  near pole (depth), and it draws the graffiti **decals' own id boundaries** --
  which is the instrument confirming a thing the RT-6 record had guessed at.
- `direct-history`, `gi-history`, `ao-history` -- frames behind each texel. The
  reflection has had this since T4; the other three write the identical lane and
  nobody had ever bound it. **It doubles as the confidence as applied**: the
  memory is exactly what RT-6.3's direction test and RT-6.4's match confidence
  scale, so a shortened bar is those tests doing their work.
- `reflection-sigma`, `direct-sigma`, `gi-sigma`, `ao-sigma` -- the pixel's own
  temporal spread from the two stored moments. §11's "temporal variance", and
  the number every bound in the contract is floored at.
- `ao-refusal` -- the one signal whose refusals were never exposed.
- `reflection-normal`, `reflection-motion` -- the stored reflector normal, and
  the virtual image's motion. **RT-6.1 has written that motion lane since it
  landed and nothing had ever looked at it.**

**`--debug-view-log`**, anchored so zero stays zero and one stays one. RT-12
filed it because the direct light saturates at any linear scale.

**A finding from the views themselves, on their first run.** `gi-history` and
`ao-history` come out **byte-identical**, and so do `gi-refusal` and
`ao-refusal`, while `gi-sigma` and `ao-sigma` differ by 63 levels. That is not
misrouting: both signals default to `Memory = 64` and the contract's refusal
tests are **purely geometric**, so two half-resolution signals sharing one
guidance grid refuse exactly the same pixels. The sigma views differ because
that is the only part that depends on the signal's own values. Worth knowing
before either number is read as independent evidence.

**Not built, and the reason rather than a silent omission.** The specification's
§11 asks for the reflection direction as RGB and its frame-to-frame difference
in world space. Reconstructing it in this pass needs an inverse view-projection
(64 bytes) plus the camera, and the debug block's push constants sit inside the
**128 bytes every Vulkan device guarantees** -- it does not fit without a uniform
buffer or a second pass. What is delivered instead comes from stored data and
answers the same questions: `reflection-normal` is the direction test's input,
and `reflection-motion` is what a swinging reflection actually does on screen.


**§11 delivered after all, and the deferral's reason was wrong** (owner: *"build
the piece that you didn't build"*). The first reading was that a world-space
reflection direction needs an inverse view-projection and a camera in a
push-constant block already at the 128 bytes Vulkan guarantees. Two observations
remove the problem entirely: **`o_Motion` has two spare channels** -- RT-6.1
sized it `R16G16B16A16_SFLOAT` for a two-channel motion and writes zero into zw
-- and **a unit vector is exactly two channels octahedrally**. So the
accumulator, which computes `reflect(sight, N)` for RT-6.3's test anyway, stores
the direction; and the previous frame's copy of the same lane makes
`1 - dot(R_now, R_prev)` -- the specification's `reflectionDifference` -- a
difference of two stored vectors instead of a reconstruction from two cameras.
No matrix, no uniform buffer, no bandwidth. `reflection-direction` and
`reflection-direction-delta`, twenty-seven views in all.

**And on its first honest look the new view found a real defect.** It showed a
hard vertical seam down the middle of a flat floor, where a reflection direction
must vary smoothly; `reflection-normal` over the same band was uniform to within
a level, so the normal was innocent and the decode was not.

`include/octahedral.glsl` encodes to **[0,1]²** and its header says the users
"must agree exactly ... a second copy of either half is how those stop
agreeing". `taa_resolve.rvshader` made exactly that second copy: a local
`DecodeOct` missing the `e * 2 - 1` step, reading a [0,1] encoding as [-1,1].
The G-buffer writes `OctEncode(N)`, `taa_guide` copies it through untouched, and
**the temporal resolve has decoded it wrongly since RT-6 landed**.

*What that did and did not break.* Both sides go through the same wrong
function, so `dot(f(a), f(b)) >= 0.9` still tested whether two normals are
similar -- which is why RT-6 measured a sensible refusal rate and a sharper
bridge. What it was not is the test its constant describes: `kNormalTolerance =
0.9` is documented as "about twenty-five degrees" and under the wrong fold it
was some other angle, distorted worst near the octahedron's diagonals -- where a
sideways-pointing normal lands, which is walls and the flanks of pillars.
Corrected in both shaders. **The frame moves by mean 0.086 levels, p99 1.00, max
165, 0.69% of pixels beyond two** -- small, real, and now the tolerance means
what it says. The floor seam went from 84 levels to 20, and the residual is a
pure red-channel gradient with green and blue constant, which is exactly a flat
reflector's `R.x` varying across the image.

**The lesson, and it is the item's own justification:** RT-12 was built to make
the instruments trustworthy, and the first thing the finished instrument did was
find a two-year-old convention mismatch in the pass RT-6.7 is about to change.

**Verified:** all twenty-five views render (each differs from the plain frame),
each takes the branch its name implies (the mix=1.0 test, now a repeatable
check), the log ramp moves the picture (ao-history 161.7 -> 186.8 mean), and the
rendered frame is unchanged. `--debug-view=taa-refusal` mid-dolly:
`build/garage_burst/rt12_taa_71.png`.

### RT-14 — ✅ done 2026-09-07. **The G-buffer is not where the memory is, and packing it would be work for nothing.**

**Method, and its limit stated first.** This machine has no GPU profiler, so
"bandwidth" is not directly readable and pretending otherwise would produce a
number nobody can check. Two things that *are* measurable: the inventory is
exact (every format is a constant in `FrameGraphBuilder`, so bytes per pixel is
arithmetic), and the traffic is inferred from **how each pass's time scales with
pixel count** -- a bandwidth- or fill-bound pass costs a constant time per
pixel and is linear through the origin; one bound by ALU, latency or setup has a
large constant term. RT-2.1 used exactly this to prove the terrain's cost was
not the raster. Three resolutions: 0.36, 1.44 and 3.24 megapixels.

**The inventory.**

| scene target | | | histories (both halves of each pair) | | |
|---|---|---|---|---|---|
| colour | RGBA16F | 8 B | TAA colour + moments | RGBA16F x2 | 32 B |
| OIT accumulation | RGBA16F | 8 B | **TAA guide** | **RGBA32F** | **32 B** (RT-6) |
| OIT revealage | R8 | 1 B | **reflection history** | **RGBA16F x4** | **64 B** (RT-6.1 added the 4th) |
| velocity | RG16F | 4 B | | | |
| surface | RGBA16F | 8 B | | | |
| indirect | RGBA16F | 8 B | | | |
| albedo | RGBA8 | 4 B | | | |
| surface id | RG32F | 8 B | | | |
| depth | D32 | 4 B | | | |
| **total** | | **53 B** | **total** | | **128 B** |

**The headline: the persistent histories are 2.4x the scene target**, and *this
series put 48 of those 128 bytes there* -- the guide's 32 and the reflection's
fourth attachment's 16. At 2560x1600 the pair is 741 MB; at 3840x2160, 1.5 GB.
Every review's instinct was to look at the G-buffer. The G-buffer is the small
half.

**The traffic, and it settles the packing question.**

| pass | 0.36 MP | 1.44 MP | 3.24 MP | pixel-bound share |
|---|---|---|---|---|
| ReflectionTrace | 1.141 | 2.281 | 5.304 | 90% |
| ReflectionResolve | 0.727 | 2.003 | 5.293 | 99% |
| DirectTrace | 0.974 | 2.106 | 5.230 | 93% |
| Scene (lit) | 0.561 | 1.017 | 2.160 | 85% |
| ReflectionAccumulate | 0.114 | 0.315 | 0.729 | 96% |
| **GBuffer** | **0.168** | **0.231** | **0.431** | **70%** |

**`scene/GBuffer` is 0.431 ms at 3.24 megapixels and only 70% pixel-bound** --
1.8% of that frame, with a third of it fixed cost that no format change touches.
**Narrowing the G-buffer's lanes would attack a pass that is not the problem.**
What *is* pixel-bound is the ray-traced passes, and their cost is rays, not lane
width: they are 90-99% linear in pixels and together 16 ms of a 24 ms frame.

**RT-6's geometric test, priced for the first time:** the guide pass 0.015 /
0.040 / 0.162 ms and the resolve 0.059 → 0.098, 0.155 → 0.258, 0.369 → 0.610.
**0.403 ms at 3.24 MP, about 1.7% of the frame**, for the sharper bridge and
everything RT-6.6 and RT-6.8 are built on. Its 32 B/pixel is the item's other
half: the guide exists **only because the G-buffer is single-buffered and
transient**, and it duplicates depth, normal and id that the G-buffer already
wrote. Keeping those three lanes for one frame instead of copying them would
remove a pass and 32 B/pixel -- filed as the one real packing opportunity this
audit found, and it is an architecture change rather than a format change.

**What this decides for the two items waiting on it:**

- **RT-2.2 is free.** Its precondition wants the albedo lane widened from 8-bit
  linear, and it names "sRGB8 or 16F". **`R8G8B8A8_SRGB` is the same four bytes**
  as the `R8G8B8A8_UNORM` there now, so choosing sRGB8 over 16F costs nothing at
  all. The precondition was never a bandwidth question.
- **RT-6.5's surface id is not free.** A fifth attachment on the reflection
  history adds 16 B/pixel to a budget already at 128, and traffic to
  `ReflectionAccumulate`, which is 96% pixel-bound. Measured in RT-6.5's record
  rather than estimated here.

**Nothing was packed, which is the correct outcome of an audit.** The candidates
that looked obvious before the measurement -- the id lane's 8 bytes, the
velocity's format, the guide's 16 -- all sit on the *small* half of the memory
and behind a pass that is 1.8% of the frame.

### RT-6.5 — ✅ done 2026-09-07 (§4C of the owner's specular specification)

**The gap.** Every history test the reflection accumulator had asked about the
reflector's geometry -- same plane, same facing -- with one material question,
roughness, at a tolerance of 0.5. **Roughness does not separate a metal from a
dielectric.** Brushed steel and painted plaster at the same roughness pass every
test the accumulator has and shade nothing alike: one is a mirror of the room,
the other is mostly its own albedo. At the boundary the history was accepted
whole.

**Where it went, and the shape that was rejected.** The contract's three
attachments are full and RT-12 took the last two spare channels for §11. So:

- *A fifth attachment* would hold a surface id **and** the metallic honestly --
  and the contract is **shared by four signals**, so it is bandwidth on all four
  every frame for one signal's test. Both outside reviews warn about exactly
  this lane growth (§20), and **RT-14 is filed to measure the G-buffer's
  bandwidth before anything is added to it.** Adding a lane the week before
  measuring whether the existing lanes are affordable is the wrong order.
- *The metallic packed into the roughness channel's integer part* is free.
  `o_Extra.r` has exactly one reader and one writer, roughness lives in [0,1],
  and a half float at magnitude two still resolves about 0.002 -- far finer than
  a test whose tolerance is 0.5.

The second. **The surface id is deferred to after RT-14, with its reason
recorded, rather than dropped.** A mismatch scales the memory to `kMaterialAgree
= 0.15` instead of refusing the history -- RT-6.3's argument, that refusing
trades a smear for the noise of a one-frame estimate on the surfaces that show
noise worst.

**It lands exactly and only where it claims to.** Garage dolly, on against the
pre-change shader: mean 0.032 levels, 0.23% of pixels beyond two, max 73.

| where it lands | concentration |
|---|---|
| **wall** (the chrome poles against the graffiti) | **4.55x** |
| **poles** | **3.15x** |
| car | 0.68x |
| ceiling | 0.40x |
| **floor** (uniform dielectric, no boundary) | **0.00x** |

**And only 0.88x on geometric edges** -- below their share of the frame. So this
is a *material*-boundary change and not an edge change, which is the distinction
the item is about and the sharpest targeting of anything measured this session.
The floor reading exactly zero is the control: one material, no boundary,
nothing to do.

**The region averages do not move** (detail and change within 0.04% everywhere),
because 0.23% of pixels cannot shift a regional mean -- and **the crop is
inconclusive at this magnitude**, which is said plainly rather than dressed up.
What is demonstrated is that the mechanism fires precisely at metal/dielectric
boundaries and nowhere else; what is not demonstrated is a visible improvement,
and the garage may simply not have many such boundaries that a reprojection
crosses.


**The other half -- the object id, argued out rather than deferred, and the axis
that is genuinely missing named instead.**

RT-6.5 was filed as "add the surface id **and** the metallic". With RT-14's
price in hand (a fifth attachment is 16 B/pixel on a history budget already at
128, feeding a pass that is 96% pixel-bound), the question is whether an object
id earns it *in this accumulator*. It does not, and the reason is specific:

**A reflection history describes the reflected image, not the reflector's own
shading.** That image is a function of the reflector's position, normal and
BRDF -- nothing else. Two texels that agree on normal, on plane, on roughness
and now on metallic **reflect the same image**, whatever objects they belong to,
so an id test there would refuse a history that is correct. This is exactly
where the accumulator differs from RT-6's TAA test, which *does* need identity:
there the pixel's colour is the surface's own shading, which depends on its
albedo and its lighting, so two surfaces agreeing geometrically still differ.
The same test is right in one pass and wrong in the other.

**What is genuinely missing is not an id, and the second review named it: a
compact BSDF signature** (*"long-term, a compact BSDF identity/hash is cleaner
than adding individual comparisons indefinitely"*). Roughness and metallic are
most of that signature; the piece still absent is **F0's colour** -- gold and
chrome at the same roughness are both metal and reflect the room in different
colours, and today they pass every test this accumulator has. For a metal, F0 is
the albedo, and the G-buffer's albedo lane already carries it.

So the honest state of §4C: **its named case is closed** (*"at minimum
distinguish metallic vs non-metallic"*), the object id is closed as
not-applicable-here with the reason, and **the coloured-metal boundary is filed
as the one real remaining axis** -- to be carried by a BSDF hash if a future lane
is ever bought, priced by RT-14 at 16 B/pixel.

**This half is reasoning, not measurement, and is marked as such.** Measuring it
would need the id bound into the accumulator and stored per texel -- the same
plumbing as building it. If the owner wants the number rather than the argument,
that is the cost.

**Cost: +0.009 ms on the reflection accumulate pass** (0.3155 against 0.3065,
A,B,B,A, and both on-runs sat above both off-runs, so this is a real ~3% of that
pass rather than noise). The frame is unmoved at 10.63 against 10.61. No new
texture fetch -- a pack, an unpack and a compare.

### RT-6.2 re-run — the negative result has expired (owner-asked, after RT-6.8)

**RT-6.2 measured the material-aware clamp as doing nothing, and gave the
reason: *"on a detailed surface the 3x3 neighbourhood box is already wide,
because the neighbours genuinely differ. The clamp only bites where the
neighbourhood is flat -- where there is no detail to lose."* RT-6.8 removed the
premise.** Part of that width at an edge was a *foreign surface* rather than
detail, and the box now contains only this surface. So the clamp bites where it
was always supposed to, and widening it does something.

Swept again on the new box, garage dolly, frames 62-99, all five regions:

| kStableWiden | detail vs off | frame-to-frame change vs off |
|---|---|---|
| 1.0 (clamp hardest) | — | — |
| **2.0 (shipped)** | **+0.70%** | **−0.06%** |
| 4.0 | +1.09% | −0.12% |
| 8.0 | +1.43% | −0.14% |

**Detail rises and change *falls* at every step**, which is not a trade: the
clamp had been throwing away history that was *correct* on view-stable surfaces,
and letting it through both keeps the detail and steadies the pixel. Region by
region the gain is where the theory says -- the poles band +2.23% at the shipped
setting and +4.53% at 8x, the floor +0.68% to +1.98%, the wall flat to slightly
negative (it is the flattest surface in the scene and its box was tight either
way). Compare RT-6.2's original sweep, which read "under one per cent
everywhere".

**Parked flicker is unchanged** (−2.1% to +0.2% across the regions), so the one
metric that is not confounded by history-keeping says the widening costs
nothing. The crop at 1x against 8x shows no ghosting appearing; the widened arm
is cleaner in the wet floor's dark band.

**Kept at 2.0, and the sweep is the owner's.** 4 and 8 measure better on both
dolly proxies -- but **both of those reward keeping history, which is what a
ghost is**, and parked flicker is neutral here so it cannot discriminate either.
Widening a ghost-protection mechanism eight-fold across every rough dielectric,
on metrics that cannot see a ghost, for 1.4% of detail, is not a trade to make
on numbers alone. The value is one constant if a different one is wanted.

**What this retires:** RT-6.2's standing conclusion that the material-aware
clamp "measured flat ... it cannot be what is blurring the texture". It was
true of the box it was measured on and is not true of this one. **A negative
result is only as durable as the thing it was measured against** -- the same
lesson RT-6.11 files for Catmull-Rom, now with a second instance.

### RT-6.8 — ✅ done 2026-09-07

**The complement of RT-6, and the numbers say so.** The resolve's two defences
are the geometric history test and the 3x3 colour box. RT-6 taught the first to
read the G-buffer; the box took all nine taps whatever surface they sat on, so
at a silhouette it spanned two surfaces, described an enormous range, and passed
almost any history through -- **widest exactly where disocclusion happens, and
tightest on the flat wall where the history was right anyway.**

Each tap is now gated by the same `Matches()` the history uses, both sides read
from *this* frame's guide.

**Live, and concentrated where it should be.** Garage dolly, on against
`--taa-box-geometry=off`: mean 0.249 levels, 2.05% of pixels beyond two (RT-6.6,
for scale, was 0.104 and 0.85%).

| where it lands | concentration |
|---|---|
| ceiling (the tube band) | 2.70x |
| wall | 1.87x |
| car | 1.67x |
| poles | 1.11x |
| floor | **0.20x** |

**The strongest 5% of edges carry 23.0% of the difference -- 4.60x their area**,
against RT-6.6's 2.43x. This is an edge change and nothing else.

**Detail rises on every region and change rises two to three times less**, which
is the signature of removed ghosting rather than added grain:

| region | detail off → on | change off → on |
|---|---|---|
| ceiling | 10.020 → **10.513** (+4.91%) | +1.86% |
| wall | 11.478 → **11.964** (+4.24%) | +1.46% |
| car | 8.550 → **8.816** (+3.11%) | +1.85% |
| poles | 9.825 → **10.059** (+2.38%) | +1.37% |
| floor | 8.423 → 8.460 (+0.44%) | **−0.47%** |

The crop (`build/garage_burst/rt68_crop.png`) shows the tube ends crisper and
their fringes tighter, which is the ghost coming off them.

**The honest cost.** Parked, pixels swinging more than four levels between
frames rise 0.7-2.6% on the edge-heavy regions and **not at all on the floor** --
a tighter box clips more history, so the pixel shows more of this frame. That is
the trade, and it is small against the detail.

**The fallback is the design, and the bar is a dial.** On a one-pixel member few
neighbours match, and a box from one sample is a point -- clipping a history into
a point discards it and the pixel flickers. The temporal floor cannot cover that
either: `temporalSigma` is multiplied by `stillness` and is exactly zero for
anything moving. So the narrow box is taken only with enough same-surface
evidence, and the full neighbourhood stands otherwise. Swept:

| bar (of 8) | detail vs off | parked flicker vs off | detail per flicker |
|---|---|---|---|
| 2 | +5.57% | +3.13% | 1.78 |
| **3 (shipped)** | **+3.97%** | **+1.75%** | **2.27** |
| 5 | +3.05% | +1.38% | 2.21 |

Three has the best ratio and is what shipped; the owner has the sweep.

**Cost: none measurable.** TAA resolve 0.264 ms on against 0.260 off, each arm
varying 0.04 on its own (A,B,B,A); the frame numbers straddle.

**The bridge's static-camera arm cannot show this, and that is a measurement
about the method rather than the change.** On a converged still the history
equals the accumulated value, so the clip never bites and a narrower box has
nothing to do: both arms are bit-identical at frame 60. The mechanism is live
there -- at frame 12, before the history converges, they differ by 0.022 -- and
RT-6's own test moves that frame by 0.483, so the machinery is fully connected.
**A moving bridge camera is the arm this wants and `burst.py` cannot drive the
bridge.** Filed as a gap in the measurement kit, not as a result.

**RT-6.2 should be re-run and has not been.** Its flat result rested on "on a
detailed surface the box is already wide, so the clamp only bites where there is
nothing to lose". Part of that width at an edge was a foreign surface, and is
now gone. The material-aware clamp deserves its sweep again on this box.

**Not changed on purpose:** the current-sample filter still averages across
silhouettes. It is RT-6.4's, its width was set on the owner's eye at 0.25, and
gating it is a separate change with its own picture to judge.

### RT-6.7 — ✅ done 2026-09-07. Correct, free, and it fires on zero pixels -- with the reason measured rather than assumed

**RT-12 settled in one run what four staged probes could not.** Yesterday's
record concluded the bridge was not running the geometric test at all, on the
strength of a hand-placed probe. **That conclusion was wrong.** `--debug-view=taa-refusal`
shows the test refusing **8.31% of the Deck view's pixels and 4.27% of
Headland's**. The probe misled; the lane the pass writes itself did not. This is
the whole argument for the owner's instruction to build RT-12 first.

**The change.** `Matches()` accepted the sky/geometry transition in both
directions (`now.x >= 1.0 || was.x >= 1.0`). It now accepts only when both sides
are sky and refuses the crossing, with its own reason code, letting the nine-tap
search recover a neighbour before the pixel falls through to the current frame.

**And it fires on nothing.** Decoding the refusal ramp band by band:

| reason | bridge Deck | garage mid-dolly |
|---|---|---|
| kept | 92.20% | 94.52% |
| off screen | 0.00% | 0.18% |
| **sky crossing** | **0.00%** | **0.00%** |
| object id | 4.18% | 3.37% |
| depth | 1.51% | 0.10% |
| normal | 2.12% | 1.17% |

The picture is bit-identical (mean 0.000, max 0.0).

**Why, and this is the finding worth keeping.** **The bridge draws its sky as
geometry.** A skybox writes real depth and carries an object id, so `now.x >= 1.0`
is never true there -- an earlier probe had already measured *no bridge pixel
above 0.99* and that reading was right even though the conclusion drawn from it
was not. The sky/geometry transition **is** being caught on the bridge, correctly,
and it is the **object id** test doing it: a good part of that 4.18% sits on the
sky silhouette. The garage does have genuinely undrawn pixels (0.56% at depth
>= 1.0, measured), but they are an enclosed static region whose boundary never
crosses, so both sides are sky and the early-out accepts as it always did.

**So the outside review's P0 is real as code and not as a defect in practice.**
The clause it names guards a condition this engine's scenes do not reach, because
the engine gives its sky an id like any other surface. **Kept anyway**: it costs
nothing, it makes the code say what it means, and it is a genuine latent fix for
any scene that ever presents true undrawn sky -- which a scene without a skybox,
or one with a failed skybox draw, immediately would.

**What it cost to learn:** yesterday, four probes and a wrong conclusion.
Today, three runs of a view.

### RT-6.7 (yesterday's parked record, kept for the wrong turn it took)

**The code is done** (`tools/scripts/garage/session_2026_09_07/patch_rt67.py`,
not applied): `Matches()` accepts only when *both* sides are sky and refuses the
transition, letting the nine-tap search recover a neighbour before the pixel
falls through to the current frame. **Not committed, because RT-6.7 is written
and unmeasured** and a temporal change with no valid arm is not a result.

**Both arms of the A/B were worthless, for different reasons, and only a probe
said so.** The diff was bit-identical on the bridge *and* the garage, which is
this session's fourth encounter with the same signature.

- **The garage control was parked, which cannot show this change.** The probe
  confirms the convention is real there -- **0.56% of the garage's pixels sit at
  depth >= 1.0** -- but parked, a sky pixel was sky last frame too, so
  `nowSky && wasSky` returns true exactly as `now || was` did. Only a *moving*
  sky boundary can differ, and no dolly arm was captured.
- **On the bridge the `Geometry`-gated probe never fired at all**, so
  `u_Params.Geometry <= 0.5` there. What that is *not*: TAA is on (an
  unconditional probe turns the whole bridge frame red, so `taa_resolve` runs),
  the TAA guide pass runs (`scene/TAA guide 0.037 ms` in the bridge benchmark),
  `GBufferPassAvailable()` is a global rather than per scene, `TaaGeometry`
  defaults true, and the runtime does set `desc.TaaGuide`
  (`RuntimeLayer.cpp:319`). **`--aa=taa` produces a byte-identical bridge frame**,
  so the mode is not being changed by it either. The remaining candidate is the
  history validity that gates the two lanes into `Dispatch`
  (`taaGuideHasHistory`, and TAA's own `hasHistory`), and one-off probes could
  not separate them.

**This is why RT-12 comes first, and the owner called it** (2026-09-07): *"I
think you should finish the debug views one, it might help you with other things
on the list."* Four probes were spent this session asking questions a refusal
and confidence view answers by being looked at -- is the test running here, on
which pixels, and which of its clauses refused. RT-6.7's measurement is a
half-hour once that view exists and was not converging without it.

### RT-6.6 — ✅ done 2026-09-07 (committed, `1058ff6`)

**The defect, from the second outside review, verified at the line.** RT-6 taught
the resolve to search the eight neighbours for a history belonging to this
surface and to fetch the colour from whichever texel won. It did not move the
*moments* with it: they stayed at `texture(u_Moments, historyUV)`, and
`u_Moments` is bound `Sampling::Point`, so that resolves to the **centre texel
-- the one the search had just rejected as a different surface**. Never a
filtering question; two different texels.

Both of the things the moments exist for were therefore wrong on every pixel the
search recovered: `prevMoments.x` is the frame count that sets `alpha =
max(1/frames, 1 - feedback)`, and `.y`/`.z` set the temporal sigma the box may
not narrow below.

**The fix is one bool.** `neighbourServed` is computed once and used by the
colour read and the moments read both, because the defect was exactly the second
copy of that condition going missing. Three lines of code; the rest is comment.

**It fires where the search fires, and only there.** Mean |d| **0.104 levels**
over a 100-frame dolly, with **1.5-2.0% of pixels off by more than one level** --
against RT-6's independently measured neighbour-recovery rate of **1.1-1.7% of
the garage's pixels a frame**. Those two numbers matching is the liveness proof
this session's three dead-plumbing defects taught us to demand.

| where it lands | concentration (share of the difference over share of the frame) |
|---|---|
| wall | 2.34x |
| ceiling (the tube band) | 2.19x |
| car | 1.61x |
| poles | 1.50x |
| floor | **0.80x** |

The strongest 5% of edges carry 12.2% of the difference, 2.43x their area.
`build/garage_burst/rt66_where.png` is the frame beside the map: it is the tube
fixtures, the ceiling beams, the car's silhouette and the vertical pipes, and
**nothing on the flat floor or the flat wall**.

**Parked is not inert, and that was not expected.** 0.059 levels mean, 0.54% of
pixels off by more than two -- about 60% of the dolly's effect, with a camera
that is not moving. **The jitter is why:** it moves the projection a fraction of
a pixel each frame, so at a silhouette the *coverage* flips between frames, the
guide lane genuinely describes a different surface, and the search fires. The
same mechanism as the suspender rope the shader header describes, seen from the
other side. So this fix reaches parked thin geometry, which is where the moments
floor was designed to matter in the first place.

**And that is where the win shows.** Parked, pixels swinging more than four
levels between consecutive frames -- the flicker that the temporal sigma floor
exists to stop:

| region | before | after | |
|---|---|---|---|
| ceiling | 5.690% | **5.434%** | −4.5% |
| car | 8.179% | **7.921%** | −3.2% |
| wall | 6.789% | 6.748% | −0.6% |
| poles | 3.830% | 3.811% | −0.5% |
| floor | 0.163% | 0.163% | 0.0% |

Mean per-frame change is flat (−1.5% to +1.4%), so this is fewer *hard* swings
rather than a general smoothing -- and the floor, which has no thin geometry,
does not move at all. **The mechanism reads straight through:** the sigma floor
keeps a thin member's history when the jitter misses it, and it was being
computed from the fluctuation of the pixel on the *other side of the edge*.

**Mid-dolly, the two proxies, as hints only.** Change −0.09% to −0.74%; detail
−0.26% to −1.41% on four regions and +0.50% on the floor. RT-6's record already
warns that both reward keeping history, and this fix does keep more of it (the
recovered surface's count is generally higher than the rejected centre's), so
neither number can settle direction. **The parked hard-swing count above is the
one that can**, because parked with a pinned timestep the true picture is
static, so a swing is flicker and nothing else.

**Cost: none found.** Frame mean, A,B,B,A: after 10.70 ms, before 10.54, with
each arm varying 0.13-0.28 ms on its own; the TAA resolve's own pass went the
other way (0.203 ms after against 0.225 before). Two runs an arm rather than the
four-run palindrome, so the honest claim is **no cost above about 0.3 ms**, not
zero -- proportionate for one `texelFetch` replacing one `texture` on under two
per cent of pixels.

**Where the review was right and this record was not.** RT-6's own record
describes the neighbour search and says "the history fetch turns point where a
neighbour served, because the texels between belong to the other side of the
edge" -- the exact argument for moving the moments, applied to the colour and
not to them. The defect was one sentence away from being written down, twice,
and was found by someone reading the shader from outside.

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
