### WR-16 R · The reflection reconstruction's follow-ups — the owner's list of 2026-09-06, in the order they are to be built

> **Owner-set 2026-09-06 (night).** Ten items came out of the engine review at the end of the reflection reconstruction work (HANDOFF ninth entry), plus one rule the owner asked to have written down (R5). **Process, owner's words: report after each task, and start the next only on a green signal.** Every brief below is written to be picked up cold: what, why with the measured numbers, where in the code, how, how to verify, and the traps. The measuring scripts all live in `tools/scripts/garage/session_2026_09_06/` and read frames from `build/garage_burst/`; the burst protocol is `python tools/scripts/garage/burst.py <tag> --speed=1.5 --stop=2.0 --frames=160 --from=30` (the dolly), `... --speed=0 --stop=0.1 --frames=20 --from=150` (parked stills), and `... --frames=1 --from=400` for a converged still (writes `<tag>.png`; its analysis then crashes on the name, harmless). **Always rebuild both `RageVRuntime` and `RageVEditor`** — the editor has its own staged shader folder, and the owner tests in both.

**The state these start from** (HANDOFF ninth entry): one reflection ray per texel from a low-discrepancy sequence, its direction and pdf in a second trace attachment; one resolve pass gathering 24 neighbours' hit points re-aimed from each texel over a disc a fifth of the lobe's footprint; the accumulator with a 3x3 history search and the silhouette rule; TAA with a filtered current and `RenderSettings::TemporalStillFeedback` (0.98 in the garage). Parked edge flicker at the no-AA floor (wall edges 1.46 / bright 1.05 against 0.99 / 1.03 with no AA); 10.9 ms at the owner's camera; the floor 3 levels darker than the unclamped truth (R11).

---

#### R1 · Delete the stale staged shaders — ✅ done 2026-09-06 night

`gi_spatial.rvshader`, `reflect_despeckle.rvshader`, `rtreflect_trace.rvshader` sat in every `build/bin/Release/*/assets/shaders` folder with no source behind them (leftovers of 2026-08-28/09-01 builds; nothing loads them). Deleted from all six staged folders (18 files). If they reappear, something re-stages from an old tree — find it, do not delete again blindly.

---

#### R2 · Point the benchmark's per-pass counters at the renamed passes

**What.** `tools/scripts/garage/bench_reflection.py` prints `trace / resolve / accumulate` columns that read `0.00` since the reflection passes were reshaped; only the whole-frame numbers are live.

**Why.** The frame is 10.9 ms and nobody can say what the trace, the resolve and the accumulate cost inside it; R3 and R8 need those columns.

**Where.** The script's `parse()` matches `[benchmark]\s+<name>\s+<cpu>\s+<gpu>` for `ReflectionTrace`, `ReflectionResolve`, `ReflectionAccumulate`, `ReflectionComposite`, `Scene`, `TAA resolve`. The engine prints the by-pass table from `RageV/src/RageV/Core/FrameProfiler.cpp` (~line 764, `--- render graph, by pass ---`, `{name:<34} {cpu} {gpu}`); the seventh HANDOFF entry recorded the names as `scene/<Pass>`, which the regex's leading `\s+` cannot match.

**How.** Run one arm by hand and read the raw lines: `RageVRuntime.exe --project=SampleProject --scene=scenes/showroom.rage --rhi=vulkan --render-defaults=off --vsync=off --width=1600 --height=900 --benchmark=100 --import-cache=off --camera=-2.3,0.72,-2,11,0,4` from `build/bin/Release/RageVRuntime`, grep `[benchmark]`. Fix the regex to the printed names (accept an optional `scene/` prefix), add `ReflectionBlur`, drop nothing. The `unscaled` arm edits a line of the trace that no longer matters (`count` is forced to 1 after it): make the arms `scaled` and `off` only, or turn `unscaled` into a second `scaled` run so the A/B/B/A pattern survives.

**Verify.** All three per-pass columns non-zero and their sum within a millisecond of the frame's GPU time minus the scene pass; run twice, the spread under 0.3 ms.

**Effort.** An hour. **Traps.** The GPU drifts a millisecond over a session (memory `project_ragev_build_and_run`): interleave arms, never compare single runs.

---

#### R3 · The preset's MirrorRays column sets the gathered neighbours

**What.** Each texel fires one reflection ray now; quality comes from how many neighbours' rays the resolve gathers (24, `kTaps` in `reflection_resolve.rvshader`). The RT optimisation preset's `MirrorRays` column (Quality 4, Balanced 2, Performance 1, Off 1 — `RayOptimisationPresetFor` in `RenderSettings.h`) still reaches the trace as `u_Scene.Indirect.w` (via `Renderer::SetMirrorRays` in `FrameGraphBuilder.cpp` ~643 and `Renderer3D.cpp` ~3366) and does nothing there. Make it set the taps.

**Why.** Owner: "make it change the gathered neighbours instead". A quality dial that does nothing is worse than none.

**Where.** `reflection_resolve.rvshader` (`kTaps` becomes `taps`, read from `u_Scene.Indirect.w`), `reflection_trace.rvshader` (the `count` computation before `count = 1` is dead: delete it and the budget-map read with it, leaving the comment that says why), `RenderSettings.h` (the column's comment), `ComponentRegistry.cpp` (the preset's label/description if the column is exposed there), `docs/RENDERING-REVAMP.md` WR-17's preset table.

**How.** Keep the field name `MirrorRays` (saved projects carry it) and redefine its meaning as "reflection samples gathered per pixel, in eights": taps = `clamp(int(Indirect.w + 0.5), 1, 8) * 8` → Quality 32, Balanced 16, Performance 8; Off keeps today's 24 by setting its column to 3. The Vogel spiral's loop bound becomes `taps`; the spiral's radius formula already divides by the count. Say in the column's comment that a tap costs about 13 texture reads.

**Verify.** `parked_stats.py` and `smear_metric.py` on a burst per preset, and R2's per-pass cost per preset: taps 8 / 16 / 24 / 32 should give resolve cost roughly proportional and the floor's parked drift over 16 frames roughly halving per doubling until the disc runs out of texels (it is 10 x 1 texels on the floor at 1600x900: past ~16 taps they land on the same texels and the gain stops — say so in the table).

**Effort.** Half a day with the measurements. **Traps.** The allocator (`u_Lamps.Trace.w`, the ray budget map) scaled the ray count per tile; with one ray it is dead for reflections and stays dead until someone scales taps by it — note, do not build.

---

#### R4 · Confidence-driven memory (anti-lag) in the accumulator

**What.** The accumulator's memory is time-driven: `memory = 64 / (1 + moved / slack)`, floor `fewest` (`reflection_accumulate.rvshader`, ~line 410). Make it confidence-driven: a texel whose fresh sample keeps landing outside its own history's spread has a changed signal and forgets fast; one whose fresh samples fit the spread keeps the full memory.

**Why.** Owner: "Alright make it confidence driven." The design notes (`RT_Temporal_Reconstruction_Anti_Smearing.md` §14-16, 19) want the history weight from evidence, not from a clock. Today a reflection that changes while its surface stands still (a light switching, an object moving in the mirror) lingers for a 64-frame time constant; the bound clamps it toward the neighbourhood but the clamp is wide on a rough floor. ReBLUR calls the same mechanism anti-lag.

**Where.** The block that computes `memory`, `frames`, `kept` (~408-420); the moments are already there: `c.extra.g` (mean luma), `c.extra.b` (mean square), `sigma = sqrt(max(b - g*g, 0))`, and `luma` of the fresh sample.

**How.** `deviation = abs(luma - c.extra.g) / max(sigma, kNoiseFloor * max(c.extra.g, 0.01))` — the noise floor keeps a converged, near-noiseless texel from firing on a one-level flicker. `confidence = 1 - smoothstep(2.0, 4.0, deviation)` (within two sigmas: full trust; beyond four: none). `memory = max(memory * confidence, fewest)` — and when confidence is 0 the frames count restarts (`frames = 1`), which the doc calls the hard reset. Do not touch the `moved` term: motion and change are different evidence and multiply. Keep the silhouette rule (R5 extends it).

**Verify.** Three things, all with the current scripts: (1) `parked_stats.py` on a parked burst must not change (the floor's parked change 1.09 and drift 1.09/1.44/1.81/2.32 — a rise means the noise floor is too low and Monte Carlo variation is being read as change, which the notes warn against); (2) `smear_metric.py` settle after the dolly (floor vs its own converged still, 5/20/60 frames: 10.8/6.6/3.3 now) must not get slower; (3) a **change test**, which does not exist yet: a burst in which a tube's intensity is switched at frame 80 (a `Slider`-style native script that sets a light's intensity at a frame — `SampleProject/Source/Slider.cpp` is the template), measuring how many frames the floor takes to reach the new converged still. Today it is ~64; the target is under 10.

**Effort.** A day with the change test. **Traps.** The moments are of luma only and are themselves an EMA with `momentAlpha = max(1/frames, 1/16)`: right after a reset they are unreliable for ~8 frames — gate the confidence by `frames > 8`. Fireflies: a single bright hit is a deviation of many sigmas on a dark texel; the noise floor and the two-to-four-sigma ramp are what keep one hit from resetting the memory — measure the parked drift before and after.

---

#### R5 · At a silhouette, forget fast when the other side is moving — the rule the owner asked to have written

**What.** The silhouette rule (HANDOFF ninth entry, `AtSilhouette` + `HistoryAt(..., silhouette, ...)` in `reflection_accumulate.rvshader`) keeps a silhouette texel's own history across the jitter flip with the full memory, so the two sides average into the coverage-weighted picture and the edges stop flickering. That is right while both sides stand still. When one side is **an object moving on its own** with the camera still — a car driving past a wall — the wall's edge texels keep averaging in the car's reflection from where it was, a 64-frame time constant: a faint trail behind the moving object's reflection edge. Rule: **at a silhouette, the memory is shortened by the faster of the two sides' motion**, not just by this texel's own reprojection shift.

**Why.** Owner's question: "so you are telling me that either I can pick between smearing or flickering edge?" — no, and this rule is the reason. Flicker needs the long memory across the flip; the trail needs a short memory only when the flip is not the jitter's but a real motion.

**Where.** `AtSilhouette` sees the 3x3 surfaces; bind the scene's velocity attachment to the accumulate pass (`FrameGraphBuilder.cpp` accumulate pass: `builder.Sample(sceneHDR)` already; pass `context.Color(sceneHDR, velocityIndex)` into `Renderer3D::AccumulateReflections` as a new texture at set 3, binding 6; the velocity is in clip units, y-flipped on Vulkan the way `taa_resolve.rvshader` handles it).

**How.** In `AtSilhouette` also return `neighbourMotion = max over the 3x3 of length(velocity * 0.5 * size)` in texels. In the memory block: `if (silhouette) memory = min(memory, max(64 / (1 + neighbourMotion / slack), kSilhouetteFloor))` with `kSilhouetteFloor = 4` — the same shape as the `moved` term, so a jitter-only flip (velocity exactly zero: still geometry reprojects within 1e-5 texel, HANDOFF WR-13) keeps 64 and a texel-per-frame motion drops to ~30, four texels to ~13. Do not use the neighbour's *depth* change as the signal: a parallax edge under camera motion is already covered by `moved`.

**Verify.** Parked stills (`edge_shake.py v9_noaa_still <new>_taa_still`): the wall's edge and bright-edge numbers must stay at 1.46 / 1.05 (no regression from the zero-velocity case). A **moving-object test**, which does not exist yet: a copy of the garage with the car on a `Slider` (Speed 1 m/s along X, camera fixed), burst 160 frames; the wall band the car's reflection crosses is compared with a converged still at each frame's pose of the car (render the car parked at the frame's position, 400 frames) — the trail is the error behind the car's reflection edge; target: gone by 8 frames.

**Effort.** Half a day plus the test scene. **Traps.** The water writes no velocity today (R7); until it does, a water/pole silhouette reads still. The velocity attachment is written by opaque draws only (`#ifndef RV_TRANSPARENT` in `pbr_fragment.glsl` ~3720).

---

#### R6 · Debug views for the reflection pipeline

**What.** Three views exist: `--debug-view=reflection` (history length /64), `reflection-image` (image distance /20), `reflection-choice` (surface vs image candidate). Add: **`reflection-refusal`** (why a texel refused its history), **`reflection-reach`** (the resolve's disc, per axis), and **`roughness`** (the surface roughness the disc is sized from — the floor's is unknown, HANDOFF eighth entry).

**Why.** Owner: "add debug views." The design note §23: tune with views, not from the final image. Every measurement this week that could not be explained (the -3 darkening, R11; the doubled band edge; the silhouette "ghost" that was the metric's) would have taken minutes with these.

**Where.** `EngineConfig.h` `DebugViewMode` enum and `EngineConfig.cpp` key parsing (~749); `FrameGraphBuilder.cpp` ~3215-3281 (`reflectionView`, `auxAttachment`, the scale per view, the `PostProcess::DebugView` call); `debug_view.rvshader` (`mode >= 5` shows `u_Aux`'s alpha scaled). The accumulator writes `o_Extra = (roughness, momMean, momMeanSq, choice)`.

**How.** *Refusal:* the accumulator packs a reason into `o_Extra.a` as `choice + 2 * reason`, reason ∈ {0 kept, 1 no history/off-screen, 2 normal, 3 plane, 4 roughness, 5 silhouette-kept, 6 bound-clamped-hard}; the `reflection-choice` view shows `mod(value, 2)` and the new view `floor(value / 2) / 6`. *Reach:* the resolve writes `(reach.x / 24, reach.y / 24, taps used / taps, distance)` into its output when a push flag says so (`u_Reflection.Probe.w`, set from `config.DebugView == ReflectionReach`), and the debug pass binds the **resolved** target instead of the history (a fourth `auxResource` case) and shows rgb; the accumulator's input is garbage in that mode, which is fine for a debug frame. *Roughness:* `o_Extra.r` already; a view of `u_Aux.r` with scale 1 (add a "channel" to the debug params, or write roughness into `.a` of a fourth mode). Add each key to `EngineConfig.cpp`, the enum, the scale table, and to `docs/ENGINE-NOTES` wherever the other views are listed (grep `reflection-choice`).

**Verify.** One parked frame per view; `reflection-refusal` must be near-black on the floor with the silhouette code along every pole edge; `reflection-reach` must show the floor's disc as ~0.4 red (10/24) and ~0.04 green (1/24), the poles near zero, the wall larger; `roughness` answers the eighth entry's open question in one frame.

**Effort.** A day. **Traps.** The debug view pass reads attachments of the *previous* history for the reflection views (`reflections.Previous()`); the reach view reads the current resolved target — a different resource with a different lifetime in the graph; declare the read in the pass builder or the graph will not order it.

---

#### R7 · The water writes its motion

**What.** The water surface writes **no velocity**: `pbr_fragment.glsl` ~3720 writes `o_Velocity` under `#ifndef RV_TRANSPARENT`, and `water_surface.rvshader` / `water.rvshader` define `RV_TRANSPARENT`. So TAA reads whatever the opaque behind the water wrote (the far backdrop: zero), and every water pixel counts as "did not move at all" — which is why the still feedback (0.98) smeared the bridge's sparkle into bands and had to be switched off (2026-09-02), and why `TemporalStillFeedback` is per project.

**Why.** Owner: "make water write its motion buffer so that its speed is known, this seems like a better option." With the water's own motion in the buffer, still pixels are still and the water is not, and 0.98 can be the default for every project.

**Where.** The water's vertex path (`water_surface.rvshader`, `include/water_waves.glsl`: Gerstner waves as a function of position and time); `v_PrevClipPos` (location 8 in `pbr_fragment.glsl`, ~877) which the opaque vertex shader fills from the previous view-projection; the `WaterSurface` pass's target in `FrameGraphBuilder.cpp` (~1296 onward), which must carry the scene's velocity attachment for the write to land.

**How.** (1) In the water vertex shader evaluate the wave displacement twice: at `Time` and at `Time - DeltaTime` (both are in the scene block or must be added), transform the second by `PreviousViewProjection`, write `v_PrevClipPos`. (2) In `pbr_fragment.glsl` write `o_Velocity` for `RV_WATER` too (`#if !defined(RV_TRANSPARENT) || defined(RV_WATER)`), same formula, same jitter subtraction. (3) Give the water surface pass the velocity attachment (the scene target's `velocityIndex`) as a write, blend opaque for that attachment (the surface is drawn where it covers; its motion overrides the backdrop's). (4) Then, if far water still reads under `kStillWithinTexels` (0.003 texels — the swell a kilometre out really moves a few thousandths of a texel a frame, HANDOFF WR-13), the water declares a floor: `velocity magnitude at least 0.01 texel` for water pixels, with the comment that says why — the sparkle changes every frame regardless of how far the crest moved, so water is never "still" for TAA's purposes. Set the bridge project's `TemporalStillFeedback` to 0.98 and retest.

**Verify.** The bridge at the sea cameras (`docs/RENDERING-REVAMP.md` WR-13's flicker protocol and the "horizontal bands" the owner saw twice): 100 frames parked at 0.98 must show no banding on the far water (per-frame change on the water region must stay at its 0-feedback value), and the steel's edge flicker must drop as the garage's did (edge_shake on a bridge still). The garage is unaffected (no water) — confirm with `edge_shake.py`.

**Effort.** A day. **Traps.** The transparent (OIT) resolve pass runs after the water; make sure the velocity write is on the *surface* draw, not the OIT accumulation target; `DeltaTime` must be the frame's real step (the burst pins it with `--frame-time=0.0166`); a wrong sign in the previous-position transform makes the water report double motion — check with `--debug-view=velocity` if there is one, else add it (R6's pattern).

---

#### R8 · Shadows get their own buffer and a small temporal + spatial denoiser

**What.** Ray-traced soft shadows (WR-15) are traced inside the lit fragment header at four call sites (`pbr_fragment.glsl` ~4983, 5005, 5049, 5170: `TraceShadowSoft` per light inside the light loop), with a per-frame random offset, and the visibility is multiplied straight into the colour. Nothing denoises it but TAA, and TAA rejects its history under motion — so shadows are grainy in motion and settle with TAA's own speed. Give the shadow visibility its own signal and denoiser, the way the reflections now have one.

**Why.** Owner: "Give shadows and GI their own buffer and small temporal and spatial denoiser." The wall's per-frame change under the dolly (5.5 levels, HANDOFF eighth entry, attribution) is not the reflections; the soft-shadow shapes on the wall are what slides and what stays grainy.

**Where.** Read first: `pbr_fragment.glsl` ~5040-5060 — the `fresh` at 5049 suggests a shadow history already exists in-shader for some path (the WR-15 / WR-17 work); establish what it is before designing. Then: the frame graph (a new `ShadowVisibility` pass before `Scene` or split out of it), `Renderer3D` (a pipeline + resource set like `ReflectionTrace`'s), a new `shadow_accumulate.rvshader` modelled on `reflection_accumulate.rvshader` (history + surface attachment + moments; normal/plane tests; a `moved` memory; a bound), a `shadow_blur.rvshader` modelled on `reflection_blur.rvshader` (radius from history length, plane/normal weights), and the lit shader reading the denoised visibility instead of tracing.

**How — the shape to confirm with the owner before building.** *Signal:* one visibility value per pixel per light *group* is too many textures for 128 lamps; the proven shape is a single **"sun-and-lamps visibility" pair**: (a) the dominant light's visibility (the light the pixel receives most from — WR-10's grid or the lamp chooser already ranks lamps) as one channel, and (b) the sum of the remaining lights' shadowed contribution *ratio* (lit / unshadowed) as another — i.e. denoise the ratio, multiply the analytic lighting by it (the "ratio estimator for shadows", Heitz 2018, "Combining analytic direct illumination and stochastic shadows"). Two channels, RG16F, full or half resolution. *Trace:* one shadow ray per pixel per frame toward a light chosen by the existing chooser (this is also where WR-16's ReSTIR DI plugs in later). *Denoise:* the accumulator's exact contract — reproject by the surface, refuse on normal/plane, memory 64 shortened by `moved`, a bound from the 3x3 spread; then a 5x5 bilateral blur faded by history length. *Consume:* the lit shader multiplies its per-light sum by the denoised ratio, tracing nothing.

**Verify.** The attribution arms already exist: `attr_norefl` (reflections off) under the dolly measured the wall at 5.53 a frame; the same arm after R8 must fall toward the no-AA parked floor (~1.0 on flat pixels), and the bridge's shadow cost (~15 ms of ~50, WR-16's evidence box) must fall to about one ray's worth. Correctness: a still frame against a brute-force all-lights reference (WR-16's verify), per-pixel diff image (memory `feedback_diff_images_not_means`).

**Effort.** Two to four days. **Traps.** The ratio estimator's division: clamp the unshadowed sum away from zero; flashing emitters (beacons) must reset the history (Ground rules); two temporal systems on one image (this and TAA) — measure the pair; the `fresh` history at 5049 may already be half of this — do not build a second one beside it.

---

#### R9 · GI: audit what it already has against the same contract

**What.** GI is *not* in the lit shader the way shadows are: `rtgi_trace.rvshader` traces to its own target, `PostProcess::GiDenoise` (`gi_denoise.rvshader`, feedback `desc.Post.GiDenoise`, `FrameGraphBuilder.cpp` ~2038/2132) denoises it, and the lit shader reads it back reprojected (`pbr_fragment.glsl` ~5270-5280, `u_Scene.Indirect`). The owner's item 2 asked for buffer + denoiser for GI too; they exist. The task is an **audit**: does the GI denoiser have what the reflection accumulator has — a surface test (normal, plane) before it reuses history, a memory shortened by motion, a bound from the neighbourhood, and a history-length-faded blur — and where it does not, add it.

**Why.** Under the dolly the wall's grain was attributed to neither reflections nor TAA (HANDOFF eighth entry); after R8 whatever remains is GI's.

**Where.** `gi_denoise.rvshader`, `rtgi_trace.rvshader`, the `Indirect` read in `pbr_fragment.glsl`, `PostProcess::GiDenoise`.

**How.** Read the shader against the four properties above and write the gaps as a list; build only the gaps; the reflection accumulator is the template for each.

**Verify.** `attr_norefl`-style dolly arm with shadows already denoised (after R8): the wall's flat-pixel change is the GI's share; parked drift on the wall over 16 frames (`parked_stats.py`, add a wall column) before and after.

**Effort.** Half a day to audit; the gaps unknown. **Traps.** WR-16 rejects ReSTIR GI precisely because a second temporal reuse fights this denoiser — the audit must not add one.

---

#### R10 · The tubes as line lights for the specular term

**What.** The garage's fluorescent tubes are emissive geometry; a wet floor only shows them when a random GGX reflection ray hits the thin tube, so the floor's reflection of the tubes is built from rare bright hits — the grain, the blotches, the slow moving-floor convergence (floor error under the dolly 19-20 levels against the converged still, unchanged by every reconstruction change this week). Compute the tubes' specular contribution **analytically per pixel** with linearly transformed cosines for line lights (WR-8, second half; Heitz 2016 for the rect/polygon LTC, and the line-light extension in "Real-Time Line- and Disk-Light Shading", Heitz & Hill 2017), and make the traced reflection ray **ignore tube emission** when it hits a tube so nothing is counted twice.

**Why.** Owner's own line-light design; owner confirmed on the review: "this is good". Everything the reconstruction can do is bounded by the variance of the estimate it reconstructs; this removes the variance at its source. It is also the realism item: a tube's reflection on a rough floor is a soft streak whose width is the lobe's, exactly what LTC gives in closed form.

**Where.** The light loop in `pbr_fragment.glsl` (the lamps' specular term; WR-7's capsule specular is the cheap version already there for source length), the tube entities' light definition (they must be lights with two endpoints and a radiance, not only emissive meshes — the luminaire binding WR-9 links the two), `reflection_trace.rvshader` / `ShadeTraced` (skip the emissive term for surfaces flagged as tube lights, or for any surface that is a light's lens), the LTC lookup tables (two 64x64 textures, generated offline by the fitting code from the papers; the engine has `u_BRDF` for the split-sum table — the same loader).

**How.** (1) Tube = line light: endpoints, radius, radiance, per tube entity (the garage has ~a dozen). (2) LTC line-light specular in the lit shader for glossy pixels (roughness above the mirror window's `gloss.x`, where the traced ray no longer handles the lobe well); below it the traced mirror ray keeps the tube's emission (a chrome pole reflects a tube crisply and the ray does that right). (3) The traced ray's `ShadeTraced` drops the emission of tube-lens surfaces for pixels that took the LTC term — pass the pixel's roughness window into the trace so the rule is one function of roughness, not two. (4) The diffuse term from the tubes stays as it is (the bake / probes carry it).

**Verify.** Bias first: the converged unclamped truth (`endpose_old_unclamped.png`'s recipe, 400 frames, clamp off) *with the old trace including tube emission* against the new build's converged still — the tube bands' brightness and width must match within the truth's grain (`bias_check.py`, `freq_split.py`); then the grain: HF energy of the converged floor (16.9 truth / 12.0 now) should fall to the material's own detail; then the settle and the moving-floor error (`smear_metric.py`: floor f70 ~19 today) — the target is the floor under motion within a few levels of parked. Cost: R2's columns.

**Effort.** Two days, more if the tubes are not yet lights with endpoints. **Traps.** Double counting (the ray still sees the tube's lens) is the classic failure — the sealed-room rule: a pixel that takes the LTC term must never receive the tube's emission from a ray; the roughness window boundary must be a smooth blend or a seam appears on the floor where the two methods meet; LTC's fit is for GGX with the same alpha convention as `DistributionGGX` (alpha = roughness squared) — check before trusting the tables.

---

#### R11 · The floor's -3 level darkening (added by Claude; not one of the owner's ten)

**What.** The current build's converged floor is 3.2-3.5 levels darker than the unclamped 4-ray truth (`bias_check.py endpose_v15.png ref=endpose_old_unclamped.png`: floor mean -3.5, bright -4.2, dark -3.1; car, wall, poles within a level). Not the history clamp (clamp on/off identical); not the weight formula (v8 and v9 identical to the hundredth); a disc the size of the whole footprint has no darkening (-0.2) but blurs; no resolve at all (one ray, no disc) has -1.5.

**How.** In order: (1) `w = Gaussian only` (drop the pdf ratio and the cap) — if the darkening goes, the ratio's cap is dropping bright tail draws; (2) compare in **linear** through the `reflection` debug view (the tonemap's concavity darkens a grainier signal — the one-ray no-resolve arm's -1.5 hints at it); (3) the 3x3-minimum hit distance: a texel next to a car-reflection texel gets a tiny disc — count how many floor texels gather fewer than 8 taps (R6's reach view). Fix what the test names.

**Verify.** Floor mean within a level of the truth, `|.|` under 4, HF not above 12.

---

#### Withdrawn · `SupersampleFactor: 2`

Listed on the review as "four times the pixels for every pass"; wrong — the factor applies only when the AA mode is SSAA (`FrameGraphBuilder.cpp` ~377), and the project runs TAA. Nothing pays it. Do not re-litigate.
