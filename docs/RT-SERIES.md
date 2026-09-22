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

**Thirty-two of thirty-six items are closed and four are open -- RT-9, RT-15 and RT-13 all closed 2026-09-20, the last two on the owner's word ("speckle, grain and spoiler trace are gone"; "leave it and close RT-13"). RT-10 was DROPPED and RT-21 closed on 2026-09-21 (see their rows); RT-11 closed 2026-09-21; RT-23 closed 2026-09-22 (evening); what is left is RT-22 and RT-2.2, which is deliberately last. Every RT-6.x sub-item is finished.** (2026-09-14: RT-5 and RT-16 closed by measured change and the retired young blur; RT-7 closed on the owner's word once tube length reached the ray-traced lamp pass; RT-4 closed on the owner's word with R11 carried into the new RT-21.) Effort is solo days at this week's pace; the detail behind each number is the complexity table below.

| # | status | effort | risk | in a line |
|---|---|---|---|---|
| RT-1 | ✅ done 2026-09-06 | — | — | the lit shader stops walking lights under RT |
| RT-2 | ✅ done 2026-09-06 | — | — | ambient occlusion as a signal |
| RT-2.1 | ✅ done 2026-09-07 | — | — | the parallax march at mip 0, not the raster |
| RT-2.2 | open, **at the end of the series** | 1 d | low | the deferred resolve; the albedo lane widens first |
| RT-3 | ✅ done 2026-09-07 | — | — | GI as a signal of this frame |
| RT-3.1 | ✅ done 2026-09-07 | — | — | the contract at each signal's own resolution |
| RT-4 | ✅ **done 2026-09-14 (owner's call)** — the trace, resolve and accumulate run before the lit pass and the lit shader reads this frame's picture at the pixel; R5's same-surface rule is RT-6.10's per-texel test; R11 measured (the resolve's weighting, through DistributionGGX's cap, settles the floor 2.5 levels under a per-texel reference; the reference itself is not exact) and carried into RT-21; SSAA's traced reflections fixed on the way. Record below | — | — | reflections as an instance of the shared code |
| RT-5 | ✅ **done 2026-09-14** — parts 1-3 as recorded; **part 4, the anti-lag, replaced by measured change** (docs/RT-MEASURED-CHANGE.md); **part 5, the young blur, retired everywhere**: the exact copies skipped (8d2a318), the occlusion and bounce blurs measured off against on (dolly, cube, lights button parked and moving: pictures matched) and off by default, 0.54 ms a frame | — | — | the contract validates by the G-buffer; the blur goes |
| RT-6 | ✅ **done 2026-09-09** — both halves; the still rule needs no per-project value now the sea reports its own motion | — | — | TAA on the G-buffer |
| RT-6.1 | ✅ done 2026-09-07 | — | — | the reflection's virtual-image motion lane |
| RT-6.2 | ✅ **re-run 2026-09-07: the negative result expired** | — | — | the material-aware clamp, live on RT-6.8's box |
| RT-6.3 | ✅ done 2026-09-07 | — | — | the reflection-direction test |
| RT-6.4 | ✅ done 2026-09-07 | — | — | the current-sample filter down; the tests fade |
| RT-6.5 | ✅ **done 2026-09-07** — the metallic **and** the object id | — | — | the accumulator tests the material and the identity, not just roughness |
| **RT-6.6** | ✅ **done 2026-09-07** | — | — | the moments follow the texel the colour came from |
| **RT-6.7** | ✅ **done 2026-09-07** — correct, and measurably unreachable | — | — | the sky/geometry transition is a disocclusion |
| **RT-6.8** | ✅ **done 2026-09-07** | — | — | the colour box is built from this surface only |
| **RT-6.9** | ✅ **done 2026-09-07** | — | — | a camera cut throws the history away |
| **RT-6.10** | ✅ **done 2026-09-07** | — | — | the accumulator validates what the ray hit |
| **RT-6.11** | ✅ **done 2026-09-07** — **the biggest sharpness win of the day** | — | — | Catmull-Rom; its negative result had expired |
| RT-7 | ✅ **done 2026-09-14 (owner's call)** — tube lights: `SourceLength` and `SourceRadius` shade as a tube in the raster loop (2026-09-08) and now in the ray-traced lamp pass too, with soft shadows along the tube's length. **The tubes' mirror reflections stay ray traced** (rays see the glowing bars). Tried and removed the same day, on the owner's rejection: linking a light to its glowing mesh so the light, not the rays, drew the reflection, and taking the light's brightness from the mesh — the floor reflections vanished, because the light's highlight cannot draw a mirror (DistributionGGX's 1e-4 denominator cap leaves a sharp highlight a few percent of its brightness; measured against a brute-force tube). The 16-byte light record's cost on the bridge was not measured | — | — | the tubes as line lights |
| RT-8 | ✅ **closed 2026-09-09 by the owner** — job 1 shipped (the sea's own choose-and-shade retired for the shared pass); **jobs 2 and 3 dropped**, not deferred: job 2 regressed the bridge and its approach is wrong, job 3 measured no gain | — | — | the water on the G-buffer |
| RT-9 | ✅ **built 2026-09-16/20** -- the confidence lane (the accumulator's own history, read by the trace) and tile allocation for **both** signals: one count per 16x16 tile, earned only where young texels sit together, nothing at all where nothing moved, and the level's own count as the floor. Both on by default. **It earns nothing measurable in this project**: the reflections' half is free and changes nothing in the garage; the direct half cannot change anything at Quality (eight lamps is the shader's ceiling, so the pass is skipped) and reads as a coin flip against the reference at four, for +0.15 ms. The counters are honest for every pass (the sea's mirror pass never flushed at all), and "rays per pixel" was already one dial across land and water -- records below | — | — | the budget's shadow lane, and confidence drives allocation |
| RT-10 | ❌ **DROPPED 2026-09-21 (owner: "we have tried everything in the book for RT-10 and things just get worse")** -- built, measured, and taken back out. The three stages (choose-then-shade, neighbours' picks borrowed, picks kept across frames) were on by default for one day. Measured against every lamp shaded, in the garage: stage 1 identical, stage 2 and stage 3 each **further from the correct picture**, for about +3 ms a frame; on the bridge no stage changed the picture at all and they still cost 1.3 to 1.7 ms. The owner then watched the driving car on the commit before the stages beside the tree with them in, and called the earlier one plainly better. Everything RT-10 -- the stages, the lamp picks walking with the accumulate, the visibility check on a reused pick -- is parked on `wip/2026-09-21-rt10-and-sea`, not deleted. **Worth coming back to, and the branch is the starting point.** What is missing is a mechanism, not a dial: every dial was tried and measured (ray count 4 to 32, the parallax distance, the fallback branch, the curvature classification, the blur width) and none of them moved it. The two things never built are the reprojection by what the ray actually struck (RT-17 already records the hit's instance and its previous transform) and a correct weight for a pick whose visibility has changed since it was chosen. Anyone picking this up starts by reading the branch's `docs/` notes and the truth-render harness in `tools/scripts/garage/session_2026_09_21/`. | dropped | -- | ReSTIR DI on the G-buffer |
| RT-11 | DONE 2026-09-21 | 1 d | low-moderate | next-event estimation at GI and reflection hits |
| RT-12 | ✅ **done 2026-09-07** | — | — | the signal debug views, complete |
| RT-13 | ✅ **done 2026-09-20 (owner's call)** — **skinned + layered done inside RT-2**; transparent: all three stages (the glass layer, its lamp light, its reflections) **on by default from 2026-09-20**, `--glass-layer=off` restores the old path. Costs **+0.45 ms** at the owner's shot (0.54 ms of new passes, 0.09 given back where the nearest pane stops casting its own rays) for the reflection look the owner approved. Validation clean under TAA/MSAA/SSAA, scenetest green on both backends, the bridge byte-identical, and only glass pixels change (0.06% of the wide shot, 0.82% of the close-up, against a 0.000% noise floor). **Moving glass does enter the layer** -- `DrawKind::Static` is the vertex layout, not "does not move". **The panes behind the nearest stay on the old path, by the owner's decision**: a second layer only moves the wall to the third pane, depth peeling costs a full set of passes per pane *and* hands each layer's memory to whichever pane is that far away this frame, and borrowing the nearest pane's picture is a reflection from the wrong angle. Glass's own rays (1.3 ms of the close-up's 2.0 ms transparent pass) are the optimisation pass's | — | — | every opaque surface in the G-buffer; glass through the shared passes |
| **RT-14** | ✅ **done 2026-09-07** — and it says do not pack the G-buffer | — | — | the G-buffer's bandwidth, measured before anything is packed |
| **RT-15** | ✅ **done 2026-09-20 (owner's call: "speckle, grain and spoiler trace are gone")** — built 2026-09-15/16 (`8d7741a`, pushed): the cube reflects the room again (metal hits were shaded Lambert-only and came back black), the whole reflection is composited **before** TAA again (`--reflection-moving-layer`, off), the object check is hard on the specular instance, and the moving cube's bands are gone (no surface-history fallback on a flat mover; the ray rotation is R2 at the texel's index). The three the owner had left open were answered by the four fixes after it: the young blur on any self-moving reflector, the firefly clamp in the accumulator and again in the resolve, the blur width driven by the texel's own noise, and the direct light's object lane -- which is what ended the streaks under the wing. **Carried elsewhere, not dropped:** the blobs on the cube's approach are RT-10's (the owner's call: ReSTIR should own the ray spend), and the cube reading softer than the chrome bars is unmeasured against a truth render. Records below | not priced | moderate | the reflection accumulator reprojects by object motion |
| **RT-16** | ✅ **done 2026-09-14** — measured change: the lights button's light 90% gone after 10 frames (145 before), 15 with the camera moving. A *baked* light switched off still fades slowly, because its bake is thrown away and rebuilt -- accepted as a known thing (docs/BAKING-ROADMAP.md, docs/manual/lighting.md) | — | — | a reflection takes seconds to leave the floor when its light goes out |
| **RT-17** | ✅ **built 2026-09-15** (owner: built to take a variable out, measured small here) -- the struck instance and normal in a transient trace lane, kept in the id lane's spare channels, tested only where something moved; the floor under the driving car 3.37% -> 2.30% ghost pixels, everything still identical (record below) | — | — | the accumulator tests what the ray *hit*, by identity |
| **RT-18** | ✅ **built 2026-09-15** -- a moving silhouette's history is tested like any other: the cube's stripes and the car's trail gone, everything still identical; the band the cube uncovers each frame goes to RT-9 (more rays, owner); the older face's speckles and the bottom bar's banding are noted for later (record below) | — | — | history cannot outlive the silhouette it belongs to |
| **RT-19** | ✅ **done 2026-09-08** | — | — | the refusal reasons, totalled per frame |
| **RT-20** | ✅ **done 2026-09-13** — RT-6's surface test fired on the jitter at every edge; a still edge now keeps its own history unless what it showed was moving | — | — | edges flicker on a parked camera while the jitter is on |
| **RT-21** | ✅ **done 2026-09-21 (owner's call)** — the floor in `DistributionGGX` was on the *divisor*, so it was a ceiling on the highlight: `max(PI * denom * denom, 0.0001)` stood in for a true divisor of about 3e-8 at roughness 0.1, holding a peak of roughly 3000 down to 1, and every polished surface read flat. **The floor is now 1e-9** — the owner's pick of the two options (the other was a minimum on roughness) — which is as safe from dividing by zero and far below anything a real material reaches. Checked at the same pose with the camera script swapped out: the garage and the close-up show brighter, tighter lamp reflections in the car's paint, the chrome poles and the wet floor; the bridge's pier and headland were looked at for the water's glitter and the tower lights and passed on the owner's eye. `prefilter.rvshader` already carried its own 1e-6 and is untouched. | — | — | the highlight formula's safety cap |
| **RT-23** | ✅ **FIXED 2026-09-22 (evening), owner-judged "99% gone" on the slow car pass.** The cause was never the estimator: the measured change's re-light did not draw what the trace drew (no aimed rays, one ray against an averaged count, the hit's lamp draw seeded by the wrong frame), so while the car moved it called 29% of the floor "changed" every frame and restarted the reflection average there; `--debug-view=reflection-change` is the picture, HANDOFF's top entry the record. Fixed in reflection_trace.rvshader, pbr_fragment.glsl (hitSeed by RV_TRACE_FRAME) and Renderer3D (rows and the count reach the re-light). The last 1% (the streak edges under the car's moving reflection) was RT-9's allocator judging "young" by the alpha the anti-lag never resets, so a restarted texel rebuilt at one ray; it reads the blend count now, owner-judged "gone completely". Also fixed on the way: the tubes had no length in the scene since the 09-14 rollback; watch_arm played the committed scene without the bake. **The earlier record follows.** open, **bisected to a source and two spreaders 2026-09-22**. Every arm below was judged live by the owner on a slow car pass (`tools/scripts/garage/watch_arm.py`, 0.35 m/s, default camera, nothing captured -- a still cannot show a speckle and writing a frame per PNG flatters the accumulator). **The source is bright outlier rays where the tubes and the car are reflected.** **Two passes spread them over the whole floor:** the resolve's neighbour gather and the three-pass blur chain. Disabling *either* collapses the artefact back onto the tube and car reflections -- `--reflection-moving-blur=0` (which zeroes MovingRadius, and both the movingBlur and varianceFilter conditions require >= 0.5, so the whole chain goes) and the staged `taps = 0` give the same picture. **Neither is the cause; both are amplifiers**, and a whole evening went into testing amplifiers. **Ruled out, each watched live and each leaving it unchanged:** the reflection history (off), the ray count (16 a texel only makes it fainter), the firefly clamp (off), that clamp's second-brightest floor, that clamp's emitter exemption, tap qualification by what the neighbour struck (built, RT-17's lane wired into the resolve), the lobe-over-pdf tap weighting (flattened), the gather's per-frame disc rotation (frozen), RT-17's identity confidence scaling, RT-11's one-light-per-hit, RT-11's aimed emitter sample, and the confidence-ray allocation. **Made it worse:** the VNDF sampler. **Timing note:** the chain stays scheduled a frame past the stop -- `AnyInstanceMoved()` is `RayAnyMoving || RayAnyMovingLast` -- which is the 'fraction of a second' tail the owner sees. **Where to go next:** bound the outlier at the source, before either spreader sees it, and measure against the 16-ray truth so it does not eat the lamp light RT-11 just recovered. **Original filing follows.** | not priced | high | the reflection accumulator on movers |

| RT-23 (earlier records) | **narrowed to one symptom 2026-09-22**. **Re-measured on main after RT-11 landed, judged live by the owner, with the car's motion run uncaptured (writing a PNG a frame gives the accumulator time the real thing does not have).** *The car's ghost on the floor:* **very faint** -- the floor had to be swapped to a plain white material (`assets/materials/white_floor.rmat`) before it could be seen at all, which the owner called a good sign. *The moving cube's blobs:* **gone**, and note the cube that carried them had been rendering the engine's default matte material for its whole history (the fixture's material was written in a schema the loader does not read -- fixed at 1d7d45b), so part of that symptom was never the renderer. *The chrome poles' speckle:* **still there.** So what is left of RT-23 is the poles, and the first thing to try is the VNDF sampler already in the tree and still off by default, which cuts the reflection's sparkle 630 -> 538 a frame and is correct regardless. **Original filing follows.** -- **new 2026-09-21 (owner)**. **2026-09-21, later: the sparkle is the rays, not the accumulator.** Frame-to-frame jumps on the driving car are the same with the reflection's history on (12,480 pixels a frame over 40 levels) and off (12,157); with `--rt-reflections=off` 11,851 remain, which is the car's own edges moving. The reflection's real share is about 630 pixels a frame, all of it right of screen centre on the chrome poles and the floor beside them. **That retires five reconstruction attempts**, each measured against a 16-ray truth: the full variance + edge-stopping + a-trous filter (inert); following the reflected object's travel in one history (car body 2.84 -> 3.18, floor 2.77 -> 4.59); two layers after the resolve (3.62 / 4.47); two layers before it (3.12 / 3.47); the history refused outright. The shipped single-layer path beat all of them. **What does help:** sampling the visible normals rather than the whole distribution (`RV_REFLECTION_VNDF`, in the tree, off by default) -- the old sampler returned the exact mirror direction for every below-horizon draw, piling every rejected sample onto one direction, and it cuts the reflection's sparkle 630 -> 538 a frame. Noted by the owner as a potential cause while other areas are explored. **Four arms tried the same night, judged live by the owner on the driving car:** (a) *two layers, the moving part added after the temporal resolve* (`--reflection-moving-layer=on`, the scheme RT-15 built and switched off) -- **the ghost is gone**, and the car carries speckles, because that layer's own grain is added after the resolve and nothing averages it; (b) the same composited *before* the resolve -- worse on both counts; (c) one layer reprojected by what the ray struck (`MoverTravel` into `ImageThen`'s `seenTravel`, which the moving layer already uses) -- worse, and the reason is written in the accumulator's own comment: one colour cannot move two ways, so shifting a rough pixel's whole memory by the car's travel drags the still room with it; (d) the memory refused outright on movers -- looks clean, loses the averaging. **Arm (a) is the shape that works and its one defect is that layer's unaveraged grain**, so the next thing to try is smoothing that layer itself (the young-texel blur it already carries a mark for, or more rays where it is thin), not moving where it is composited. **The reflection's memory on a surface that moves on its own.** Three faces of one defect, all seen on the tree with RT-10 removed: the driving car ghosts on the floor behind it, the moving chrome cube carries random blobs, and the chrome poles' reflection speckles. Proven this night to be the kept memory read from the wrong place -- refusing it outright removes all three and costs the averaging. **Ruled out, each measured:** the ray count (4, 8, 16 and 32 rays give identical speckle), the parallax distance it reprojects by (forcing this frame's own: no change), the camera-only fallback (always reprojecting: no change), the young blur (4 to 24 texels never reaches it), and the uncovered fallback branch (named as a real but invisible contributor). **Unbuilt, and where to start:** reproject by *what the ray actually struck* -- RT-17 already records the hit's instance and the ray instances carry their previous transform -- instead of the virtual-image distance the accumulator guesses at today. The truth-render harness (a mover stopped at the same pose and settled) is on `wip/2026-09-21-rt10-and-sea` under `tools/scripts/garage/session_2026_09_21/`. | not priced | high | the reflection accumulator on movers |
| **RT-22** | open. **2026-09-23 (b30d5cd): the window streak FIXED by the owner's eye, and the pane's grain after it** -- the frame filter's temporal floor (the box widened by the pixel's own recent swing) kept a stale glow on still pixels; it now applies only at outlines and never under a see-through surface (the OIT revealage), and the pane's reflection has the floor's measured change and RT-9 ray plan, with the power heuristic for the aimed ray. **That frame filter rule regresses the bridge's water glitter (0.80% -> 0.97% blinking); a water exemption was rejected by the owner as a bad fix.** Parked by the owner: a general rule (floor only while a pixel keeps swinging, closed once it stays on one side a jitter cycle), the delayed reflection stop (the reflection accumulator's own memory), the cable flicker. HANDOFF's top entry has the record. -- **half taken 2026-09-22 (night)**: the frame filter now reads the reflections' change map as well as the direct light's (it kept every still pixel for fifty frames and was never told a reflection had changed there) -- floor beside the moving light 9.2 -> 4.8 levels, 19.5% -> 7.1% off by >16, car side 7.5 -> 7.0, flicker unchanged, floor smooth by eye. **Measured and rejected the same night:** a restart on any change above a tenth (halves the trail on paper, but a moving reflection changes every frame and went raw and shaky -- `watch_arm.py --stage=restart`); recording what the floor *shows* instead of the fresh ray so a stale image counts as change until it is gone (floods the change map while anything moves -- the shown picture is a clamped, gathered average and a fresh ray is not, a bias not a noise). **Still open, next to try (owner):** (1) the glow of the moving light on the car's window -- the glass pane's light is never measured; try a re-light for the glass pane, the direct light's record/re-light run on the glass layer's surface, its map fed to the pane's accumulator and the frame filter (about a day); (2) the cube's reflection stopping a fraction of a second after the cube -- try refusing the reflection memory where the ray struck a mover, now that a refused texel gets the allocator's extra rays (a staged arm, judged by eye). HANDOFF's top entry has both. -- **re-measured 2026-09-22 (late) after the RT-23 fixes, same harness (`session_2026_09_15/emitter_lag.py`):** unchanged to slightly better with the scene as it was then (floor beside the car >16 levels 11.9% -> 11.1%, car side 12.3% -> 12.1%; anti-lag off 42.1%, reflections off 6.4%). **With the tubes as line lights the floor reads 19.5%:** not noise (two consecutive drive frames change by the same amount with and without the lengths, 1.37 vs 1.34 levels) -- a 3 m tube throws a wide soft shadow of the moving cube, so the same lag covers more floor. **new 2026-09-15 (owner)**. **A moving light's lighting trails behind it.** A Realtime point light carried across the garage with a glowing cube: the pool it throws on the floor and the glow on the car stay where the light was for a few frames and arrive late where it is -- against the same pose settled, 11.9% of the floor beside the car off by more than 16 levels, 12.3% of the car's side. Measured change already takes two thirds of it (41.5% without); of what is left, the traced reflections hold about a third (7.1% with them off) and the direct light's history the rest. No bounce light from a moving light where the bounce is baked (expected). Record below | not priced | moderate | a moving light's lighting keeps up with it |

**Three new items on 2026-09-08 (RT-17..RT-19)**, from the owner's *Object-Aware Temporal Rendering* document; the section below the reviews records what that document proposed that this engine already had, what was taken, and what was rejected and why.

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
| **RT-10 (dropped)** | **ReSTIR DI on the G-buffer.** The choose/shade split (the water had it) for every opaque pixel, temporal and spatial reuse of the choice validated by id, depth and normal, feeding the same contract. | T10, S4, WR-16 M4 | The many-light scenes (the bridge: 78 lights a pixel) are where K = 4 is not enough. | large |
| **RT-11** | **Next-event estimation at GI and reflection hits** with the resampled light (the 7 M rays the hits trace today, chosen by importance). | T11 | The last place a ray shades every light. | medium |
| **RT-12** | **Signal debug views, complete:** history length, refusal, reach, the K choice, the raw fresh picture, per signal, on one log ramp (the direct-light view saturates at any linear scale). **Plus the confidence set** (owner's spec §11, filed here 2026-09-07): the combined history confidence, the reflection direction as RGB and its frame-to-frame difference, and the rejection reason split by which test refused it -- depth, normal, material, disocclusion -- and the rays actually allocated per pixel. **This session hit plumbing that was declared, bound, read and never connected three separate times**, each caught only by staging an absurd constant and checking the frame moved; a refusal-and-confidence view would have caught all three in one look. **And the rejection reason as an enum, per test** (both reviews, 2026-09-07): the reflection accumulator already writes one (`g_Refusal`, in the integer part of `o_Extra.b`) and TAA writes none, so a TAA ghost cannot be traced to the test that let it through. One small integer lane -- off screen, id, depth, normal, sky transition, material, direction, hit -- coloured by reason. | R6 remainder | Tune with views, not the final image. | small |
| **RT-13** | **Surfaces outside the G-buffer join it: skinned, layered (terrain), and transparent (the car's glass, OIT).** RT-1 found the first two on the bridge -- the G-buffer pass draws only the plain and masked kinds, so under the direct-light signal the terrain and the characters had no light in the pass and none from the loop; they keep the loop for now (`RV_SKINNED` / `RV_LAYERED` compile without the signal's inputs). The fix is a G-buffer variant per kind and their draw in the G-buffer pass; transparent surfaces decide between a thin layer and the in-shader path. | RT-1's finding, new | Every opaque surface must be in the G-buffer or every signal skips it. | **skinned + layered ✅ done inside RT-2** (see its record); transparent: decide first, small |
| **RT-16** | **A reflection takes seconds to leave the floor when its light goes out.** Owner-reported 2026-09-07: switch the car's lights off and their reflection lingers on the wet floor for a few seconds. **Two temporal filters now run in series on that pixel** -- the reflection accumulator's own memory (64 frames) and then TAA's (still-feedback 0.98 in the garage, about 50 frames), because RT-6.1 moved the composite *above* the resolve -- and memories in series compound rather than add. **RT-6.10 will not catch it**: a light going out changes the reflected *brightness*, not the hit *distance*. What should catch it is the accumulator's bound (the neighbourhood clamp, which sees a sudden darkening) and the evidence-driven anti-lag that is RT-5's subject -- so this is partly a measurement of whether those two are doing their job, and partly the question of whether one signal should pass through two filters at all. **The arm already exists**: `burst.py`'s `BURST_SWITCH` turns lights off mid-capture (`"Tube ,Bottom light bars|1.328"` switches the tubes at frame 80), so "frames until the reflection is gone" is directly countable. | owner-reported, 2026-09-07 | A light switching off is the plainest possible temporal test, and the engine fails it visibly. | medium |
| **RT-17** | **The accumulator tests what the ray hit, by identity -- not only how far away it was.** Every test the reflection history has is about the *reflector*: the same object, the same plane, the same facing, the same roughness, the same metallic, and (RT-6.10) how far the reflected thing stood. Nothing tests **what** it was. So a polished wall that never moves, a camera that never moves, and a car driving past in front of it: every test passes at full confidence, the history is kept whole, and the car's reflection smears along the wall. RT-6.10 cannot catch it -- a car crossing at a roughly constant distance does not change the hit *distance*. The trace writes the hit's **instance id and normal** into the payload it already fills, the accumulator keeps them beside the reflector's, and a change **scales the confidence rather than refusing**: a distant environment changes which triangle a ray lands on every frame without changing what it looks like, so an equality test there would refuse a history that was perfectly good (the document's §62, and it is right). The hit normal is nearly free once the lane exists and catches the constant-distance case the distance test misses. **Precondition:** the reflection trace has no payload lane for either today, and RT-14 measured the persistent histories at 128 B/pixel -- so the lane is sized and measured before it is written, on RT-14's own terms. | owner's OATR document §15, §17, §61, §62; the half of RT-6.10 that was filed and not built | The one axis of a reflection's history that has never been validated, and the only one that sees a moving *reflected* object. | medium |
| **RT-18** | **History cannot outlive the silhouette it belongs to.** A moving object leaves no trail today because the per-pixel tests all fire correctly in the band it has vacated -- the id, the depth and the normal there describe the wall behind, so the object's history is refused. That is a guarantee by argument rather than by construction, and every gap found this week (the missing motion vector of RT-15, the plane residual of a moving reflector) was a case where one of those tests silently agreed with a history it should have refused. A coverage mask for **this frame** multiplied into the history weight makes the vacated band empty by construction. **Take the mask and not the isolated pass**: the document's two-pass form (§4, §26) invents the double-lighting hazard it then warns about in §47, and this engine composites the reflection above the resolve already (RT-6.1). | owner's OATR document §26-27, §46 | The cheap structural guarantee behind the per-pixel tests, on the class of defect this week kept producing. | small |
| **RT-19** | **The refusal reasons, totalled.** Every temporal pass already writes *why* it refused a history per pixel -- `g_Refusal` in the accumulator, and RT-12's reason enum in the resolve -- and nothing ever adds them up. So the question "is this smear a history wrongly kept, or a signal too thin to average" is answered with an afternoon of staged probes, which is exactly what 2026-09-08 spent before finding that the objects were reporting no motion at all. One line beside the ray counters: acceptance rate, the split by which test refused (id, depth, normal, material, direction, hit, disocclusion, off screen), and the average history length. **Not a fix, an instrument** -- and the cheapest item on this list. | owner's OATR document §56 | The numbers exist per pixel and are thrown away every frame. | small |
| **RT-20** | **Edges flicker on a parked camera while the jitter is on.** Owner-filed 2026-09-13 after watching a test run with `TemporalJitterScale` at 0: "the edge jitteriness is gone, it looks very stable". Measured that day on the garage, parked, per-frame change on edge pixels (frames 150-169, `edge_shake.py`'s regions): **car 7.92 levels (bright edges 11.43), wall 4.54, poles 4.04, tubes 11.71 (bright edges 20.63) with the jitter; about 1 everywhere without it.** The jitter is the anti-aliasing -- eight sub-pixel positions averaged into a coverage-weighted edge -- and a parked camera is exactly where that average should have converged, so a pixel still swinging 8-20 levels a frame is one whose history is being discarded or clipped every cycle and shows the raw sample. **Measure before touching anything**: which resolve step does it -- the geometric test refusing at silhouettes (RT-6, `--taa-geometry=off`), the box built from this surface's taps only (RT-6.8, `--taa-box-geometry=off`, whose one-sided box at an edge excludes exactly the other side the averaged colour is made of), the Catmull-Rom fetch (RT-6.11), or the tubes' HDR contrast through the compressed blend -- with `taa-refusal`, the temporal counters and `--capture-signals=taa` with a crop on an edge, frame by frame. **Not a fix by switching the jitter off**: that trades the flicker for aliasing, and MSAA does not run under TAA unless `--msaa` asks. | owner, 2026-09-13 | The anti-aliasing is the one filter every pixel passes through, and on the owner's scene its edges are the least stable thing in a still frame. | small to measure; the fix depends on what the measurement finds |
| **RT-15** | **The reflection accumulator reprojects by object motion, not only the camera's.** It finds last frame's texel by pushing this frame's world position through last frame's view-projection -- which asks where the point *would* have been if it had not moved. For anything that moves it was somewhere else, so every history test refuses, `frames` falls to one, and the pixel shows a **single ray**: noise on a mirror, and the ghosting the owner sees while driving the car. `u_Velocity` is already bound to the pass and used only for the silhouette test. The fix is to reproject by that lane where it describes object motion, and to decide what a *rotating* reflector does, which a screen-space velocity cannot express. **Measure on `showroom_moving.rage`**, which is the only scene that shows it. | found 2026-09-07 by the moving-object scene | Every reflection on anything that moves is currently a one-sample estimate. | medium — **do early; it is the largest visible defect found this week** |
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
| RT-10 (dropped) | 5–7 d | **high** | Bias control (M caps, the MIS weights for spatial reuse, visibility reuse or not); the water's version measured as a loss for a physical reason, and land must be shown to differ; the garage shows little at K = 4, so the bridge's 78-light pixels are the test throughout. |
| RT-11 | 1–2 d | low–moderate | Mostly RT-1's score applied at the hit; the noise it moves into GI and reflections must be absorbed by their contracts, measured on the reflection arms. |

### RT-11, done 2026-09-21

**Both halves built, and the frame came out faster than it went in.**

**(a) The reflections learned to aim at the emitters.** The bounce pass has done it
since it was built -- for every ray it casts it also picks a point on a fitting and
fires one ray straight at it -- and this pass never did. A reflection ray found a tube
only by luck and brought back its whole radiance when it did: hit or miss by chance at
one ray a texel, which is the sparkle measured on the driving car. The aimed sample is
combined with the lobe ray by the balance heuristic, so a mirror still takes its lamp
from the mirror ray and a rough floor takes it from the aim, and neither counts it
twice. The emitter is picked by what it is worth to the texel -- brightness, size,
facing, distance, and how much of the texel's lobe points at it, probed at the centre
and both ends of the rectangle's long side -- because a uniform pick over sixteen rows
made the aim a lottery of its own and the caps downstream ate it (0.10 display levels
delivered against 1.27 removed).

**(b) A traced hit keeps one light instead of shading them all.** The loop walks the
lights to weigh them, keeps one by weighted reservoir with a chance proportional to
what it is worth, casts a single shadow ray after the loop and scales the answer back
up. `--hit-light-sampling=off` restores the old walk. Not in the bake, which keeps
shading them all because bake time is the cheap currency.

**Measured at one ray, against the 16-ray truth** (`truth_test.py`, distance then
bright specks, the truth's own count in brackets): garage wet floor 13.31 -> 9.69 and
7014 -> 5372 (4509); cube face 5.05 -> 2.92 and 507 -> 282 (289); cube floor
12.77 -> 9.93 and 3213 -> 2096 (1214); car body 2.84 -> 2.73 and 3259 -> 2965 (3159);
car floor 2.71 -> 2.46 and 271 -> 218 (229). Every region closer on both counts, which
nothing else in this series has managed.

**Unbiased, and that is the test that matters.** At sixteen rays HEAD and RT-11
converge to the same picture -- back wall 42.84 vs 42.25, floor 86.45 vs 85.03, cube
region 37.29 vs 37.74. Different estimator, same answer.

**Cost: -1.75 ms** (18.88 -> 17.12 ms, 1600x900, interleaved eight runs). (a) costs
0.70 and (b) saves 2.25.

**Three engine defects found on the way, all older than RT-11:**

- **A lamp shadowed every ray aimed at it.** The area-emitter rectangle is built from
  the mesh's bounding box and sits at its *centre*, so for a tube or a panel it is
  inside the housing and the near half of the shell stopped the ray. Every aimed sample
  this engine has ever cast was blocked by the lamp it was aimed at -- the bounce pass
  included, since it was built. The sampler now steps its point out to whichever face
  is turned toward the surface being lit (`AreaEmitter::HalfThickness`), so the ray
  arrives without anything being made transparent. Worth eleven display levels on the
  garage floor.
- **A hit could not tell which listed emitter it struck.** It matched by position
  against a rectangle the ray never lands on, found nothing, and kept the hit's whole
  glow on top of the aimed sample. It matches by entity identity now
  (`GiEmitter::UvToSurface0.w`), which was worth seventeen display levels of
  double-counting at sixteen rays.
- **A swallowed shader compile failure.** `GiHash`/`GiRandom` sat inside
  `#ifdef RV_RAY_GI` while `TraceSurface`, which every traced pass calls, is compiled
  for variants that define no such thing. A reflections-only variant that reached for
  them failed to compile, **nothing was logged**, the pipeline fell back, and the
  picture moved fourteen display levels on the back wall with every RT-11 switch turned
  off. It wasted most of an evening's measurements. The helpers are shared now, and
  **the engine silently swallowing a shader compile failure is still open** -- there is
  no defect more expensive than one that reports nothing.

**Switches:** `--reflection-nee=on|off`, `--hit-light-sampling=on|off`, both on by
default. `truth_test.py` now restores `pbr_fragment.glsl` and `ray_shadow_trace.glsl`
as well; while it did not, its truth and shipped arms rendered with the working tree's
copies and a run scored a change against itself.

| RT-12 | 0.5–1 d | low | Plumbing; the log ramp is the only design. |
| RT-13 | 0.5 d to decide; 2–3 d if a layer | low | Deciding is most of it; a thin transparent layer is a bounded copy of the water's prepass. |
| RT-16 | 1-2 d | moderate | Half of it is a measurement -- count the frames, and find which of the two filters is holding the light -- and half is the design question the measurement will force: whether a signal should pass through its own accumulator *and* TAA, which is a consequence of RT-6.1 that was never priced. Shortening either memory trades against the noise it exists to hide. |
| RT-15 | 2-3 d | moderate | The velocity lane is per pixel and screen-space, which describes a translating reflector and not a rotating one -- a turning mirror's history moves in a way no screen velocity encodes, and deciding what to do there is most of the design. The accumulator already reprojects by the *virtual image* for the camera's motion, so object motion has to compose with that rather than replace it. `showroom_moving.rage` is the arm. |
| RT-14 | 1 d to measure | low | The measurement *is* the item; whether anything follows is what it decides. It collides with RT-2.2, which widens the albedo lane rather than narrowing it -- the two are decided together. |
| RT-20 | 1-2 d | moderate | Every pixel in every scene passes through the resolve, so any change to how it keeps an edge is visible everywhere -- and the trade is the one this resolve was tuned against for weeks: a history kept through an edge is a ghost when the edge moves. The measurement has to separate the four suspects on a still camera *and* hold the dolly and the moving-object arms where they are, and the bridge's sub-pixel cables are the case that punishes a wrong answer. |

**Dependencies:** RT-9 wants RT-1's cheap score; RT-10 builds on RT-9's lane and RT-5's validation; RT-6's velocity rule wants RT-8 or a guard; RT-7 carries WR-9 inside it. **The whole series at this pace: roughly seven to eight weeks of solo days**, front-loaded with the medium items so the high-risk ones (RT-5, RT-8, RT-10) land on a validated contract.

## The two outside reviews (2026-09-07)

Two independent reviews of the codebase, read against the code rather than taken on their word. Roughly fifty suggestions between them: **five are real defects, verified at the line**; about twenty describe work already built (both were written against a picture of the engine that predates most of RT-6.x); the rest restate this list back to it.

**The five, and where each is filed:** the moments read at a different texel than the colour (RT-6.6), the sky/geometry transition always passing the history test (RT-6.7), the colour box spanning two surfaces at a silhouette (RT-6.8), no camera-cut invalidation anywhere in the frame graph (RT-6.9), and the reflection accumulator never validating what the ray hit (RT-6.10). The second review found the first four; both found the fifth independently.

**Two of them are this series' own defects, and that is the useful part.** RT-6.6 is RT-6's: it added the neighbour search and did not move the moments with it, so the search mis-weights every pixel it recovers. RT-6.8 is RT-6's other half: the history side learned to ask whether a texel is this surface and the box side was never asked the same question. Neither would have been found by looking at a frame — both need someone reading the shader against what it claims to do.

**What they asked for that already exists,** recorded so it is not re-litigated: variance-based history clamping (`ClipToBox` plus the `kTemporalSigma` moments floor, and the accumulator's own spread bound), reflection-direction validation and its roughness scaling (RT-6.3), disocclusion detection (RT-6), material-aware reflection validation (RT-6.5, filed), confidence-driven ray allocation (RT-9), the debug views (RT-12), ReSTIR DI, NEE and line lights (RT-10, RT-11, RT-7), the water's integration (RT-8), and per-signal temporal parameters (the contract's `SignalParams`).

**Where they were argued with.** The first review's central proposal is a unified `RTConfidence` built as its own phase before anything consumes it. The pieces already exist per signal; what is missing is a *lane*, not an architecture, and coupling four signals' tuning together before there is a consumer is a cost with no return. Filed as RT-9's precondition instead. The second review's Catmull-Rom suggestion is a result this repository already measured and reverted — but its stated reason stopped being true when RT-6 landed, which is why RT-6.11 re-measures rather than dismisses it.

**One thing both reviews are right about that is not a code change:** they independently rate the water's G-buffer integration P0, above most of what is above it in this order. That is the owner's call and the argument is now on the record in RT-8's row.

## 2026-09-07: **the moving-object scene, and what it found in one run**

`SampleProject/assets/scenes/showroom_moving.rage` -- the garage plus one entity:
a **near-mirror chrome cube** (metallic 1, roughness 0.12) on a `Slider`,
crossing at 3 m/s in front of the matte graffiti wall and over the wet floor,
with the camera standing still (`--speed=0`). Authored by `make_moving_scene.py`.

**Metal on the owner's instruction**, and it matters: an emissive white cube was
tried first and is the *easy* case, because its colour does not change with the
view, so a stale history is nearly right. A mirror's is entirely a function of
view direction, which is why smearing is worst on shiny surfaces.

**Not the car**, and the reason is a trap worth keeping: `BURST_SLIDE` can drive
it, but the model is 49 entities and a tag prefix matches the root *and* its
parts, so each part takes its own Slider **and** its parent's translation -- the
body separates from the rest at double speed. Two entities share the exact tag
`porsche_992_gt3_r`, so even the `=tag` form is ambiguous.

### It answered RT-6.2's open value immediately: **the risk is not real**

`kStableWiden` was held at 2 because a looser clamp might smear behind a moving
object and no scene could show it. Frame 80, the band the cube has just vacated,
measured against the `--aa=none` truth:

| widen | residue left behind |
|---|---|
| 1 | 8.56 levels |
| **2 (shipped)** | **8.56** |
| 4 | 8.56 |
| 8 | 8.56 |

**Identical.** The arms are live -- widen 1 against 8 differs by 0.108 levels
across the frame and on 0.97% of pixels -- but in the trail they differ by
**0.021**. The clamp width changes the picture elsewhere and does not touch the
ghost, so the reason for holding at 2 is gone and 4 (+1.09% detail) or 8
(+1.43%) is safe. **The value is the owner's; nothing measured now argues
against the higher ones.**

### And it found something bigger: **a moving glossy object has no reflection history at all**

The owner saw it first -- *"the cube looks really messy while moving, there's a
lot of artifacting, noise"*. **It is not TAA**: the `--aa=none` truth is just as
noisy, so the noise is in the reflection layer, and
`--debug-view=reflection-choice` shows the cube's pixels holding **no history**
while the wall around them holds a healthy image-reprojected one.

**The cause, at the line.** `reflection_accumulate.rvshader` finds last frame's
texel by projecting this frame's **world position through last frame's camera**:

```glsl
const vec4 clipThen = u_Reflection.PreviousViewProjection * vec4(world, 1.0);
```

That asks *"where would this point have been on screen last frame **if it had not
moved**"*. For a moving object it was somewhere else, so every test refuses,
`frames` falls to one, and the pixel shows a **single ray's** estimate -- which
on a near-mirror is noise. `u_Velocity` **is** bound to this pass, and is used
only for the silhouette test (`neighbourMotion`), never to reproject.

**So the accumulator handles camera motion and not object motion, and every
reflection on anything that moves is a one-sample estimate.** That is the
mechanism behind the owner's report of heavy ghosting while driving the car, and
it is invisible in every other scene here because nothing else moves.

**Filed rather than patched in passing:** the fix is to reproject by the scene's
velocity lane where it describes object motion, which is RT-4/RT-5 territory and
wants its own measurement on this scene.

### Also reported, not yet investigated

**A light's reflection takes seconds to leave the floor** when it is switched
off. Worth noting that since RT-6.1 moved the composite above the resolve the
reflection passes through **two** temporal filters in series -- its own
accumulator (64 frames) and then TAA (still-feedback 0.98 in the garage, about
50 frames) -- and memories in series compound. RT-6.10's hit test will not catch
it: a light going out changes the reflected *brightness*, not the hit *distance*.

## 2026-09-08: **the OATR document, against what this engine already has**

The owner's *Object-Aware Temporal Rendering* proposal, read against the code. It is a
general framework for temporal reuse, aimed at smearing caused by movement, and the
honest summary is that **most of what it specifies is already built here** -- which is
worth recording so it is not proposed again.

**Already built, point for point:** object identity validation (§8, RT-6.5), depth and
normal validation (§10, §11, RT-6), motion validation (§12), material validation (§13,
RT-6.5's metallic), reflection-direction validation (§16, RT-6.3 -- including the
roughness-scaled cosine it proposes), hit validation by distance (§17, RT-6.10), the
reflection-cone idea that hit tolerance should widen with roughness (§63, already in the
lobe-scaled tolerances), continuous confidences multiplied rather than binary rejects
(§21-22, RT-6.4), the 3x3 neighbourhood search (§42, RT-6), disocclusion rejection (§41),
history reset on a camera cut (§51, RT-6.9), variance from stored moments (§25),
per-effect histories (§31), and the debug views (§53-54, RT-12's 27 of them). Its §39
warning -- that a generic final TAA can undo the work -- is the exact defect this engine
hit and fixed by moving the composite above the resolve (RT-6.1).

**Taken, as RT-17, RT-18 and RT-19** (rows in the build order above): the hit's identity,
the current-frame coverage mask, and the totals.

**Rejected, with reasons:**

- **Per-material temporal policies (§32, §33, §67).** A dial per material, against the
  owner's own standing rule that every quality lever is one render setting. The axes it
  wants -- roughness, metallic -- are already read per pixel from the G-buffer, which is
  the same information without the authoring surface.
- **Primitive id and barycentrics (§36).** The document calls it advanced itself. Nothing
  measured here needs triangle-level identity.
- **The isolated render pass (§4, §26).** Its useful half is the mask, which is RT-18. The
  other half invents the double-lighting problem §47 then warns about.
- **The framework and architecture chapters (§58, §68).** This is already the engine's
  shape: one reconstruction contract, four signals, per-effect histories.

**And the limitation worth keeping in mind:** every validation in that document would have
passed on the defect found the same day. A motion vector of zero is a perfectly valid
motion vector, and the engine was reporting zero for everything moved by a fixed-step
script (RT-15's record below). The document validates history *against* motion; it cannot
tell you the motion itself is a lie. That is what RT-19's totals and a staged constant are
for.

## Records

### RT-9 — 🔨 the confidence lane and the reflection's own tile allocation, built 2026-09-16

**What was built, in the order it was measured.**

**1. The lane (the item's own precondition).** Every filter works out how much it trusts its
memory and throws the answer away at the end of the pass. The reflection trace now reads the
accumulator's own history at each texel: the frames behind the picture, the refusal code, the
reflector kept and whose it was. Costs nothing; nothing consumes it unless the allocation is on.

**2. Per-texel allocation: measured, and wrong.** Rays keyed on each texel's own history cost
**+1.05 ms driving and +1.33 ms parked for no gain** (blobs on the moving cube 11.0% against the
motion rule's 10.6%). Two reasons, both worth keeping:
- **The young texels are not only the mover's strip.** Five per cent of a *parked* garage reads
  young -- thin edges, and glossy rays that keep landing on something new -- while its median texel
  has the full sixty-four frames behind it.
- **Scattered rays are paid for by their neighbours.** A group of pixels runs at the pace of its
  greediest member, so 5% of texels asking for four rays cost 1.33 ms where their share is 0.55.

Two of my own mistakes on the way, both now comments in the shader: keying on the refusal code
(it fires at every edge even when a neighbour was accepted and the picture is fine) and treating
object id zero as "new" (the garage box *is* id zero). Each cost about 2 ms for nothing.

**3. Per tile, which is the item's own design.** A small pass (`reflection_budget.rvshader`,
0.09 ms) reads the history at 16x16 tiles and writes one ray count per tile, held by the ray
budget's own dead band and dwell; the trace reads its tile's number. The map is a second
tile-sized history, because the budget's four lanes are full (two counts, the averaged demand, the
packed dwells) -- a repack has nowhere to put a third dwell inside a half float.

**4. Only where the young texels are *together*.** A tile earns rays when a quarter of it is young
*runs* -- young texels beside young texels, which is what an uncovered band looks like -- and
nothing when it is a scattering of thin edges. Cost fell from +0.63/+0.77 ms to +0.25/+0.56.

**5. And nothing at all where nothing moved** (owner, 2026-09-16). `Renderer3D::CameraStill`
against the signal's own motion record -- on the eye and its facing, not the view-projection,
which carries the jitter and so differs every frame on a camera that has not moved. **The
reflections' motion record never wrote a facing**, so the first version of this test reported
"turned" on every frame of a still garage and the pass ran anyway; found with a probe line, fixed
by recording the facing beside the eye.

**Where that leaves it, honestly.** With the still gate in, RT-9 changes nothing measurable in
this scene:

| | before RT-9 | RT-9 as shipped |
|---|---|---|
| camera dolly, wet floor speckle (f70..f115) | 0.82 → 0.71% | 0.82 → 0.71% (identical) |
| moving cube, blobs on the face | 10.62 → 4.95% | 10.93 → 4.75% |
| parked garage | — | 0.024% of pixels differ by more than 8 levels |
| cost, driving / parked | — | **-0.05 / +0.09 ms** |

**The one improvement measured on the way was in a still scene** -- the garage floor's speckle fell
from 0.82% to 0.73% when tiles bought rays for thin edges -- and that is exactly what the owner
asked to stop paying for. During the dolly itself the uncovered bands are thin enough at 0.6 m/s
that no tile reaches the quarter bar, so the picture is the motion rule's.

So the item stands as **machinery without a bill**: on by default, free, and ready for a scene with
real disocclusion bands (a fast camera, or a fast mover) -- with the numbers above saying it earns
nothing in the garage as it is. `--reflection-confidence-rays=off` restores RT-15b's motion rule
(four rays wherever a surface moves on its own).

#### RT-9's other three, 2026-09-20

**1. The counters, honest for every pass.** `water_trace` -- the sea's own mirror pass -- **never
flushed its counters at all**, so its rays and the shadow rays its hits walked were counted by
nobody; and had it flushed, they would have landed in the bounce's lane, because the lane is chosen
by a compile flag and that pass shares the bounce's. Exactly the reflection pass's own bug (WR-16
R2, when its lane read 0.04 M). It now says `RV_WATER_TRACE`, which the lane chain maps to the
water's, and flushes once at the end of main -- the sky-hit early return became an `else`, since the
rule is one call every live lane reaches.

On the bridge at the pier camera, 60 frames, with that pass switched on: water **0.16 -> 0.31 M**
rays a frame, shadow 2.01 -> 2.02 M (the mirror hits' rays, never counted), bounce still 0.00 M --
so they land where they belong. The picture is byte-identical.

**And the reason I could not see it at first:** `--water-reflection=full|half|quarter` parses its
value, returns success, and **never set the flag the frame graph reads**, so since that pass was
made opt-in (2026-09-09) no typed flag could turn it on. One line.

Audited the rest rather than assumed: the direct light, the bounce, the reflections and the sea's
lamp shading all flush and all land in the right lane. The probe fill traces without counters on
purpose (bake time, its own set) and says so.

**2. "Rays per pixel", one dial across land and water: already true.** The direct-light pass and the
water's lamp sampler both read `RayOptimisationPreset::RaysPerPixel`, and `--rays-per-pixel`
overrides both. The row was written before T5 and S4 landed. What is *not* on the dial is the sea's
mirror **size** (`ReflectionScale`): nothing reads that column, deliberately -- the half-resolution
pass was demoted on 2026-09-09 because its picture smeared the deck's white lights into the tower's
red, and the sea traces its mirror in the water draw instead. Left as it is.

**3. The direct pass reading a tile map.** The same pass, rule and holds as the reflections' half,
run on the direct light's own history: `--direct-confidence-rays` (on by default, owner), the
level's count as the floor so no tile ever loses lamps, up to the shader's eight where a tile's
young texels sit together, and nothing while the scene is still.

**What it buys, measured honestly: nothing here.**

| the garage's dolly, 120 frames | flat 4 lamps | tiles allocate |
|---|---|---|
| distance to the reference (every lamp shaded), while moving | 0.5355 | 0.5353 |
| the same, after the camera stops | 0.5514 | 0.5514 (identical -- the still gate) |
| frames closer to the reference | — | 61 of 120 |

A coin flip, changing 0.01-0.03% of pixels while the camera moves. **And at the project's own
setting it cannot change anything at all**: Quality keeps eight lamps a pixel, which is the shader's
ceiling, so every tile's answer is the count it already had -- the pass is skipped there rather than
paying 0.09 ms to write a constant. Where it does run (a moving scene at four lamps) it costs
**+0.15 ms** (A B B A: off 7.011/7.074, on 7.168/7.224).

**The shape not built, and why.** The row's "like GI does" means joining the S3 allocator, where
tiles *trade* a fixed screen average rather than adding to it -- which is the specification's own
constraint (do not raise the count globally). That needs a third count and a third dwell in a map
whose four lanes are full, and the reduce chain widened with it. This half adds where reconstruction
cannot answer and takes nowhere, which is the reflections' shape and what was approved. If the
average-preserving form is wanted, it is a build of its own.

### RT-15 — ✅ closed 2026-09-20 by the owner; built 2026-09-15/16 (`8d7741a`, pushed): the moving chrome cube

**Closed on the owner's word, 2026-09-20: "speckle, grain and spoiler trace are gone."** The
three they had left open were answered by the four fixes recorded after this section. What
travels on rather than closing with it: the blobs on the cube's approach (RT-10's, the owner's
call) and the cube reading softer than the chrome bars, which has never been measured against a
settled truth render.

**The owner's four complaints, in the order they were answered.** "The cube just doesn't reflect
the environment correctly"; speckles on the floor under it and flicker on the chrome pipes; the
vertical bands on its face while it moves; the streaks under the car's wing.

**1. The cube was black because every metal in a reflection was black.** `ShadeTraced` lit a hit
with its Lambert half alone, and a metal has no Lambert half -- the garage box (floor, walls and
ceiling are one mesh, `pbr_Cube_0`, Metallic 1) came back black, so the cube's rays returned
black however correct they were. A hit now takes **its specular half and the probe at the hit**
(`RV_HIT_SPECULAR`, `ProbeSlotAt`; the reflection and water traces define it, the bounce passes
its own slot). Before this, "the rays and normals are fine" was measured against a settled
reference that was itself black -- a reference has to be shown to be right before it is trusted.

**2. The reflection of a moving thing may not be added after TAA.** RT-15's item 2 split the
picture into a still layer and a moving layer and composited the moving one *after* the temporal
resolve, so TAA could not smear it into a trail. What goes around the resolve also goes around
its smoothing: that layer's grain reached the screen. Frame-to-frame change in levels, the cube
crossing the car at roughness 0.12, frames 111-130 (`rt15_scene_arms.py`, `pole_check`):

| arm | floor under the cube | a chrome pole at the cube's height |
|---|---|---|
| HEAD (`3432e08`) | 0.98 | 1.02 |
| the moving layer after TAA | 1.27 | **2.88** |
| **the whole reflection before TAA (shipped)** | **1.04** | **1.12** |
| hit specular off (reference only -- the cube goes black) | 0.98 | 1.02 |

So `--reflection-moving-layer` is **off by default** and the composite adds the whole picture
before the resolve, as it did before RT-15. The two-layer machinery stays behind the flag.
**A trap worth keeping:** the first pipe measurement was taken on the pole at x=629, which the
cube crosses -- it read the cube's own bright edge passing and said the flicker came from the hit
shine. Measure a pole the mover never crosses.

**3. The bands were the surface history, one frame of travel out of place.** A flat mirror
sliding in its own plane shows a *still* picture, so its image history is exact and its **surface**
history is not: the surface carries its own old place, whose reflection stood one frame of travel
away. Where the image history was missing -- the strip the leading edge covers each frame -- the
accumulator fell back to the surface history and the strip started as a copy of the picture shifted
by ~4 texels, kept for as many frames as that history claimed. Hence detail repeating every four
texels across the face, smears along the path, and the owner's "it takes a few seconds for the
banding to disappear when the cube stops".

- **Fix A:** no surface-history fallback where the reflector moves on its own and is flat
  (`curvature <= kCurvedMover`). A curved mover keeps it -- item 1 chooses between the two there.
- **Fix B:** `GlossyReflectionLD`'s per-texel rotation was `fract(vec2(texel) * vec2(a1, a2))`:
  its x followed the column and its y the row, and x is how far the ray tilts from the mirror
  while y is which way. `fract(a1 x)` comes round about every four columns. A still texel averages
  all 64 frames of the sequence and the pattern cancels; a young history keeps part of it, which
  on a flat face is a grid of faint bands and lines. It is now the R2 sequence at the texel's own
  index (`x + 1601 y`, in fixed point).

**What the pictures show and the numbers did not.** The band metrics (column and row detail of
the moving frame against the same cube stopped in the same place) moved by tenths and ranked the
arms differently from the eye; the strips do not
(`build/rt15/objectaware/5_bands_fix.png`, `7_moving_cube_r012_stills.png`). **More rays are not
the answer here:** at 16 rays a texel the grid was still there. The owner judged the clip and the
stills: "the vertical bands seem to be gone".

**4. The object check was made hard, and it did not fix the streaks.** On the specular instance:
another object's history is refused outright (refusal code 6), the texel's own history is
filtered only within its object, and the bound reads only its object's neighbours -- the owner's
*Object-Aware Temporal History* document, §4 and §10-11. RT-15b's silhouette-mover mark, which had
hidden the streaks, is gone. **The streaks came back with it**, so their cause is still open:
they are bars under the wing, on the strip the cube uncovers, and the object check cannot see
them because both sides are the same cube.

**Checks.** `scenetest` green on Vulkan and OpenGL (exit 0 both). The car driving at the close-up
with `--validation=on`: **0 validation messages, 0 shader compile errors**. The bridge, three
cameras, 48 frames each, against the pre-RT-15 frames (`build/rt17/bridge`): mean brightness
within 0.22 levels, and by size of difference --

| camera | pixels differing by more than 4 levels | more than 16 |
|---|---|---|
| deck | 0.59% | 0.11% |
| pier | 0.29% | 0.05% |
| glitter | 1.99% | 0.14% |

-- which is the new ray pattern's grain on dark water, not a visible change
(`build/rt15/objectaware/bridge_before_after.png`).

**Cost (2026-09-16, laptop on mains; `rt15f_cost.py`, A B B A, 300-frame benchmarks at the
owner's shot, HEAD's showroom with the harness's cube).** The `before` arm has every RT-15 dial
off -- item 1's choice, the hit's specular half and the probe at the hit, `--reflection-moving-blur=0`,
`--reflection-moving-rays=1`; the object check and the band fixes stay in, being a fetch or two
a texel. Milliseconds:

| scene | frame before | frame ship | difference | where it is |
|---|---|---|---|---|
| the car driving | 14.15 | 16.23 | **+2.08** | ReflectionTrace 3.47 → 5.39; the three blur passes 0.28; accumulate +0.03 |
| parked | 13.02 | 14.31 | **+1.29** | ReflectionTrace 3.58 → 4.86 |

Parked there is no mover, so that 1.28 ms is the hit's shading alone: a probe fetch and a BRDF at
every reflection hit, which is the price of a metal in a mirror not being black. The moving
extras (four rays on the mover's texels, the young blur) are the rest of the driving number.
**Both are for the optimisation pass after the RT series** (owner's rule); the candidates are the
per-hit probe fetch and the blur's three passes.

### RT-15, the four fixes after it (2026-09-16): speckles, grain, and the spoiler streaks

The owner asked the design question plainly -- *"are we doing something wrong? how do the industry
giants do it?"* -- and the answer was that this engine had four of the six steps a production
reflection denoiser runs. The two it was missing are the two things they were looking at.

**1. The history fix.** A moving surface covers screen texels that have no history, and what those
texels hold is this frame's few rays. Every production denoiser answers that with a spatial blur of
**this frame's** picture, on the same surface, fading out as the history rebuilds (NVIDIA's NRD
calls it exactly that; Lumen and FidelityFX have the same step). This engine had it for curved
movers alone -- a flat one, the chrome cube, got nothing. The accumulator now marks any texel whose
surface moved on its own, and the blur reads that mark. A texel young because the *camera* moved
stays unmarked, so RT-5 part 5's measured refusal still stands. `--reflection-moving-blur`, **4 at
the owner's word** (12 was the curved-mover number): the cube's face went from 3.8% of its pixels
in speckle to 0.7%.

**2. The firefly clamp, twice.** A ray that lands on a ceiling tube brings back tens of times what
the rest of the lobe does, and a running average keeps that one draw at one part in n for the life
of the history -- the bright dots, still as much as moving. The sample is now scaled back to what
its neighbours say (`--reflection-firefly`, three spreads).
- **In the accumulator**, against this texel's eight neighbours on the same object, the centre left
  out so a firefly cannot raise its own ceiling. Stopped cube 1.42% of the face in speckle -> 0.38%.
- **And in the resolve, which is where it had to be.** That pass shares a texel's rays with its
  neighbours over a disc, so one wild ray reaches the accumulator already spread over twenty texels
  with its neighbours lifted around it, where no clamp against neighbours can see it. Clamped
  against the taps it is about to average, it never spreads: moving face 0.70% -> **0.06%**,
  stopped cube **0.00%**.

**3. The blur's width from the texel's own uncertainty.** The accumulator already stores the two
moments of what arrives, so the error left in an average of `frames` samples is the spread over the
root of the count. Measured on the moving cube's face: 0.011 at the quarter point, 0.030 at the
middle, 0.24 at nine tenths -- and the bright bumps the owner kept pointing at sat at 0.18 and
above. The radius now fades in across 0.02 to 0.20 (`--reflection-noise-blur`), so the settled parts
of a moving face keep their detail. The brightest bump against the settled cube: **+60 levels
before, +18 after**.

**4. The spoiler streaks: the direct light had no object test at all.** The bars under the car's
wing are not reflections -- with the direct light's memory off they vanish, with the traced
reflections off they are still there and stronger. The pair's id binding fell back to the surface
attachment, so its "same object" test compared a normal with a normal, and the light gathered on
the wing was kept on the cube crossing behind it; the captured lane shows those columns holding
**21 frames of a value 3.4x brighter** than the columns beside them. The direct light now keeps its
own object lane (a fifth attachment, two channels) and refuses another object's light, exactly as
the reflections do -- the owner's rule: *"an object's memory should only be limited to itself"*.
**Streaks gone**, and with only this change switched on and off the parked garage differs on
**0.004%** of its pixels and the dolly on **0.002%**.

**Cost of the four, together** (A B B A, 300 frames, on mains): **+0.40 ms with the car driving,
+0.33 ms parked** -- the resolve's extra pass over its taps (+0.2), the blur's three passes while
something moves, and +0.07 in the accumulate.

**What is left, and where it goes.** The moving cube still carries soft blobs on its approach: the
face is 10% blobs there against 7% at its calmest moment. Measured against a 16-ray settled truth,
our one-ray picture sits 8.6 levels from it and a 16-ray one 3.8 -- **sample count is the lever, not
smoothing**, and the owner has put ray allocation for near-mirror surfaces in **RT-10 (ReSTIR)**
rather than spending rays here. The clamps cost about 3 levels of accuracy at one ray, which is the
price of the dot-free look.

**Three traps this day paid for.**
- **Measure the whole run, not one frame.** Every number above frame 120 was taken at the calmest
  moment of the cube's crossing; the approach is half again as noisy, and the owner saw it first.
- **A reference has to be shown to be right.** The settled cube was used as truth for hours and had
  its own spots; a 16-ray settled render is the truth (`manyrays` in rt15e/rt15f).
- **A target grown alone is silent; a pipeline grown alone is not.** The direct light's fifth
  attachment needed `Renderer3D`'s pipeline list too -- ten validation errors a frame until it did.

**Open, owner-listed 2026-09-16:** the blobs on the moving cube's approach (RT-10's ray allocation),
and the cube reading softer than the chrome bars beside it (the resolve shares rays across a flat
surface and cannot on a round one; not yet measured against the truth).

### RT-20 — ✅ done 2026-09-13 (`182f422`): the edge flicker was RT-6's surface test firing on the jitter

**The cause, by taking each suspect out on its own** (`rt20_edges.py`; the garage parked at
the owner's shot, frames 150-189, per-frame change on edge pixels in levels):

| arm | car | wall | poles | tubes |
|---|---|---|---|---|
| as shipped | 7.97 | 4.73 | 4.21 | 11.86 |
| RT-6's surface test off (`--taa-geometry=off`), jitter on | **1.04** | **0.98** | **1.08** | **0.99** |
| RT-6.8's same-surface box off | 7.88 | 4.64 | 4.09 | 11.81 |
| RT-6.11's Catmull-Rom made bilinear | 7.97 | 4.71 | 4.20 | 11.88 |
| the neighbourhood clip off | 7.87 | 4.46 | 3.86 | 11.62 |
| the jitter off (the floor) | 0.97 | 0.86 | 0.98 | 0.82 |

**The mechanism.** With the camera parked, a pixel on an edge has its jittered sample on one
side of the edge in some frames and on the other side in the rest. The test compares this
frame's sample with last frame's, so it read every flip as a different surface, and the
nine-tap search then replaced the pixel's own history -- the coverage blend the jitter exists
to build -- with a neighbour's, which is pure object or pure background. The blend never
formed. Counted with a probe: **2.3% of the garage's pixels a frame had their history replaced
that way, parked.** It has done so since RT-6 (`d34c905`, 2026-09-07), whose record judged a
single frame -- "visibly sharper" -- and set frame-to-frame measures aside for history
changes. That was right for ghosting and blind to this: a parked edge cannot ghost, and no one
measured one. Part of that sharpness was the unblended edge.

**Two fixes, the owner's call "measure both ways and keep the better", and then a third.**

- **A: a still edge keeps its own history.** The centre refused, a neighbour in this frame's
  identity lane is another surface, nothing in the 3x3 moved: keep the centre. The reflection
  accumulator's silhouette rule, gated on stillness.
- **B: last frame's surface is still here.** The centre refused, look for the surface it held
  last frame among this frame's eight neighbours; found, keep the centre.
- **Kept: A, and what the texel showed last frame was not moving either.** A leaked behind fast
  objects (below). Every path of the resolve now writes whether the pixel's surface moved, in
  the sign of the count `o_Moments.x` already stores, and the rule reads it a frame later. The
  first build of it (C) read motion from the resolve's velocity input -- which is the reflection
  composite's lane where that ran, where a mirror moves by its image, and a flat mirror sliding
  along itself has an image that stands still: most of the chrome cube's face read as not
  moving. The kept version reads the scene's own velocity lane, bound as a new input.

**Parked garage** (per-frame change on edges; the 40-frame mean against the test-off arm's, at
its edges):

| arm | car | wall | poles | tubes | mean vs test off |
|---|---|---|---|---|---|
| as shipped | 7.97 | 4.73 | 4.21 | 11.86 | 2.57 |
| A | 1.19 | 1.00 | 1.15 | 1.08 | 1.15 |
| B | 2.06 | 1.02 | 1.17 | 1.27 | 1.60 |
| **kept** | **1.19** | **1.00** | **1.15** | **1.08** | **1.15** |

B left flicker on the headlamps and bumper, where the car is many small parts and last frame's
surface is often not among this frame's neighbours.

**Behind a moving object.** HEAD's garage plus make_moving_scene.py's chrome cube, camera
parked, measured against `--aa=ssaa --ssaa=2` (no history in the resolve) and binned by how many
frames ago the cube left each pixel -- the cube's footprint is where that truth differs from the
same truth with no cube. The cube at 3 m/s, about 3.9 px a frame, error in levels:

| frames since the cube left | 1-2 | 3-5 | 6-10 | 11-20 | 21-40 |
|---|---|---|---|---|---|
| edges: shipped | 23.94 | 20.34 | 18.83 | 18.84 | 13.49 |
| edges: A | 28.35 | 22.82 | 20.43 | 19.76 | 15.03 |
| edges: B | 25.51 | 22.15 | 20.61 | 20.14 | 15.54 |
| edges: first build (C) | 25.29 | 20.80 | 18.83 | 18.57 | 13.54 |
| **edges: kept** | **23.47** | **19.05** | **17.27** | **17.18** | **11.54** |
| wall: shipped | 16.82 | 12.97 | 15.47 | 16.86 | 8.19 |
| wall: A | 19.88 | 15.27 | 17.15 | 18.12 | 10.69 |
| wall: B | 18.67 | 15.38 | 17.43 | 18.18 | 11.35 |
| wall: first build (C) | 18.01 | 14.06 | 16.42 | 17.68 | 10.19 |
| **wall: kept** | **16.80** | **12.94** | **15.46** | **16.89** | **8.26** |

A's leak: an object that clears an edge by two pixels in one frame leaves that edge's 3x3
still, and A kept the history there -- which was the object. The kept version matches shipped on
the wall and is nearer the truth on the edges at every age. At 0.4 m/s (about 0.5 px a frame)
the kept version equals A on the edges (10.65 against shipped's 17.10 at 6-10 frames) and
shipped on the wall to 0.01; B trailed a faint sparkle there. `build/rt5/rt20/final_fastcube.png`:
A minus shipped has the graffiti's outline in green where the cube passed; the kept version has
only the pipe edges the flicker fix steadies.

**The camera dolly** (0.6 m/s for 1.5 s, then still): moving, the kept version is shipped to the
digit (10.45 levels of change beyond the truth's on its edges) -- nothing is still, so the rule
never runs; stopped, 3.23 -> 0.72. B changed 4.6% of the pixels while moving and was further
from the truth where it did.

**The bridge, Deck camera, parked:** top third 12.36 -> 4.60, middle 14.70 -> 3.12 (test off 4.26
and 2.91; B 5.88 and 8.64). `build/rt5/rt20/final_bridge.png`: the tower, lamp standards, railing
and road markings stop shimmering; a single frame is softer than shipped, which is the edges
blended again.

**Counters** (probes moving each kind of pixel into the empty "sky" refusal lane, parked):
neighbour substitutions 2.3% -> 0.1% of pixels a frame; the rule keeps 2.5%.

**Cost:** the resolve 0.367 +- 0.014 ms -> 0.380 +- 0.008 (A,B,B,A twice, 240 frames a run); the
frame 14.53 +- 0.66 -> 14.62 +- 0.50, inside the noise. The rule runs only where the centre was
refused.

**Verified:** the rebuilt engine with HEAD's resolve renders the parked garage bit-identical to
the old build over 18 frames (binding 10 is written only where a layout declares it). Parked,
the kept version equals the first build bit for bit over the ten frames compared (150-159). Release builds clean. scenetest fails the
same 3 checks on Vulkan and 2 on OpenGL as every run of the day on HEAD's shaders -- the AA
switch's descriptor check on bindings 6 and 7, the layered PBR's 33 samplers, "with bodies in
it" -- and no message names binding 10.

**The change.**
- `taa_resolve.rvshader`: `JitterCrossing()` and `SurfaceMotionTexels()`; `MatchingTexel` keeps
  the centre when `JitterCrossing` says so, before the search; `o_Moments.x` is negative where
  the surface moved, on every path, and the count is read through `abs()`; binding 10,
  `u_SurfaceVelocity`. A kept pixel reads 0 in `taa-refusal`, as any kept history does.
- `PostProcess.h/.cpp`: `Dispatch` gains binding 10, written only where the pipeline's reflection
  declares it (the occlusion accumulator shares the shader, and measurements stage older
  copies); `TemporalResolve` gains `surfaceVelocity` -- null means `velocity` already is it.
- `FrameGraphBuilder.cpp`: the resolve passes the scene's own velocity lane.

**Left standing, found on the way.**
- **A thin rim round each ceiling tube reports motion on a parked camera** -- 0.08% of pixels
  flagged moving at frame 400, the same in the composite's lane and the scene's -- so the rule
  does not run there. Not explained. It is why the kept version differs from A on 0.09% of
  pixels at frame 400; the tubes' flicker is 1.08 for both.
- **An object that starts moving two pixels a frame from a standstill** was not moving last
  frame, so the edges it uncovers on its first frame keep its colour. RT-18's coverage mask is
  the structural answer.
- **Sub-pixel members** flipping out of the sample are not a silhouette in this frame's 3x3:
  the bridge's middle third sits 0.21 above the test-off arm.
- **The chrome cube shows vertical stripes once it passes the car** (owner-spotted), in the
  shipped resolve too. Not investigated.

**Instruments** (`tools/scripts/garage/session_2026_09_13/`): `rt20_edges.py` (the cause),
`rt20_arms.py` (A, B, C and the probes, staged on HEAD's resolve whatever the source holds),
`rt20_measure.py` (parked, the cube at two speeds, the dolly, the truth arms, the trail by age),
`rt20_bridge.py`, `rt20_counters.py`, `rt20_cost.py`, `patch_rt20.py`. `stage_run.py` gained the
moving cube and whole-file variants.

### RT-5 part 3 — ✅ closed 2026-09-13: the darkening was the histories' rounding, not the bound

**The row's diagnosis did not survive measurement.** It said the bound clips a skewed
K-sample estimate and leaves the floor and the car -0.16 levels dark. Re-measured at the
preset's K = 8 (the -0.16 was taken at K = 4 on 2026-09-06; it came out the same), sampled
lights against every light, 64-frame parked means of the final picture, and every candidate
taken out on its own and together:

| arm | floor | car |
|---|---|---|
| as shipped | -0.157 | -0.157 |
| the bound off, both payloads | -0.140 | -0.153 |
| the bound off, the first payload only | -0.157 | -0.157 |
| TAA's clip off | -0.158 | -0.159 |
| TAA's compressed blend made linear | -0.156 | -0.156 |
| every clamp and the compressed blend off | -0.139 | -0.154 |
| TAA's still feedback 0 | -0.155 | -0.168 |
| no anti-aliasing at all | -0.012 | -0.037 |
| TAA with the jitter off, so the picks stay fixed per pixel | -0.005 | -0.038 |
| the jitter off and the picks re-drawn every frame | -0.168 | -0.299 |
| no anti-aliasing and the picks re-drawn every frame | -0.180 | -0.285 |

**What it needs is picks that change from frame to frame, and nothing else in the list.** A
CPU replica of the reservoir's hash and acceptance loop put the estimator's mean within 0.3%
for one pixel over its 1024 salts, and on the GPU the raw per-frame estimate (every history
off, 100 frames) matched every light to 0.01%.

**The instrument that found it: `--capture-signals=direct,taa[,crop=x:y:w:h]`** (runtime
only). The named histories read back as float arrays at the screenshot frames and written as
their mean, plus every frame of a small crop -- the signal as the next pass reads it, before
the tone curve and the eight bits. Linear, sampled against every light:

| direct light | whole | floor |
|---|---|---|
| the raw estimate, every history off | 0.000% | +0.002% |
| after the accumulator, diffuse | -1.692% | -1.807% |
| the same, the bound off | -1.691% | -1.807% |
| after the accumulator, specular | -0.748% | -1.502% |
| the same, the bound off | -0.006% | -0.018% |

Frame by frame on a 64x64 floor patch the accumulator lost the same -0.00052 on every texel
every frame, uncorrelated (0.006) with whether the new estimate sat above or below the
history, and **the stored value equalled the exact update rounded down to the half-float grid
in 99.9% of 1.3 million texel-frames** -- rounded to nearest in 50.2%, which is chance.

**The mechanism.** Every temporal history here is RGBA16F. An average that forgets at 1/n
moves less than one half-float step a frame, so the stored result is almost never
representable, and this GPU's float-to-half conversion rounds toward zero: half a step lost a
frame, settling about n/2 steps low. That is 1.6-3.1% of the value depending on where it sits
in its power-of-two octave, which is why the loss map follows the brightness of the light
pools rather than any geometry. A still input is never rounded, because a converged average
stops changing; so the loss appears only where the input moves -- sampling noise, detail the
jitter walks across -- which is why fixed picks showed nothing and every clamp ablation said
nothing.

**The fix, in the engine: `include/half_float.glsl`.** The accumulator and the temporal
resolve -- and, below, three more histories -- round their running averages onto the half grid themselves, up with the chance of how
far past the step below the value sits, so the stored value is exactly representable and the
hardware has nothing left to round on any vendor. The accumulator's picture, twin, image
distance and moments; the resolve's colour, alpha and moments, with the frame number added to
its push block for the draw (the jitter repeats every eight frames). Adding half a step would
have been right only on hardware that truncates; 32-bit histories would double every one of
them to fix a rounding. **Owner-approved 2026-09-13 after before/after pictures**
(`build/rt5/rounding_*.png`).

**Measured with it**, the garage parked, 64-frame means, eight-bit levels:

| | whole | floor | car | wall |
|---|---|---|---|---|
| sampled - every light, before | -0.123 | -0.157 | -0.157 | +0.005 |
| sampled - every light, after | -0.023 | -0.019 | -0.004 | +0.011 |
| after - before, the picture | +0.686 | +1.333 | +0.721 | +0.474 |

Linear: the final colour +3.4% on the floor and +2.8% on the car, the direct light's diffuse
+1.98%. The engine build matches the staged arm the owner looked at to 0.001 levels.
Validation under the same build is the identical set of messages with the committed shaders
staged instead, and the resolve's 56-byte push is flagged only against the old 52-byte block.

**What the row's own change would have done, measured and not built.** A 4x wider bound for a
history settled over 8-32 frames: on a still, 4.8% of pixels change at all and 0.2% by more
than two levels, the floor 0.01 closer to every light and the poles 0.05 further; with the
owner's lights button pressed off, the car's light leaves the floor later -- half gone at 44
frames as shipped against 53, 95% gone at 189 against 205. **Dropped**: nothing gained on a
still, a real cost on a switch.

**And the same rounding in three more histories, owner-approved the same day:**
`water_accumulate` (the sea's own lamp average, both halves -- the default arm on the bridge),
`gi_denoise` (the indirect buffer's temporal stage, colour and moments; it gained a frame number
in its push block) and `tile_budget` (the ray allocator's averaged demand). The raster SSAO's
temporal pass needed nothing of its own: it runs through the temporal resolve.

- **The bridge barely moves, in the right direction.** 64-frame means, after against the
  committed shaders: the pier -0.002 levels overall, 0.09% of pixels brighter by more than a
  level and none darker; the glitter camera +0.018, 0.11% brighter and 0.01% darker. A night
  scene is dark, so there was little light for the rounding to take. Diff images
  `build/rt5/bridge_{pier,glitter}_3_diff.png`: black at x8 but for a faint green on the lamp
  pools and the tower.
- **The allocator must not dither, and the still test is what said so.** Stochastic rounding
  of the averaged demand took the sixty-second still test on the bridge from **0.0082 changes
  per tile per second (2 tiles) to 0.0203 (7 tiles), over the 0.01 bar**, on both lanes: a
  thresholded value wobbling at a dead band's edge is a crossing. So it rounds to the nearest
  step instead (`StoreAsHalfNearest`): **0.0095, the same 2 tiles, PASS** on both lanes -- the
  committed shader measured 0.0082 twice, to the transition, so the difference is real and is
  the one already-restless tile toggling 684 times against 539. A picture wants the average
  exact; a threshold wants nothing that moves.
- scenetest after all five: Vulkan and OpenGL fail only the checks that fail with the committed
  shaders (below); `--validation=on` gives the committed shaders' identical set.

**Left standing:**
- The highlight half's bound has no noise floor and costs 0.75% of the direct specular, 1.5% on
  the floor -- the one real effect the row named, too small to see, and widening it is the lag
  trade above.
- **Two more writes of the same kind, found by the sweep and not touched**: `water_foam` keeps
  its foam in an `rg16f` image and integrates it a small step a frame, and `irradiance_fill`
  blends each sweep into the previous one in an `rgba16f` field. Both round toward zero on the
  same write. Auto exposure does not: its state is a float buffer.
- **The bridge crashes under `--validation=on`**, committed shaders or these: the transparent
  draw (`Renderer3D::FlushBlended`) commits binding 7 into a set whose layout has none, and the
  Khronos layer dereferences null inside `vkUpdateDescriptorSets`. Without validation the frame
  renders; with it no bridge validation run can finish. Worth an item.
- **Two identical runs of the garage are bit-identical to frame 167 and no further**: from 168
  up to 0.035% of pixels differ by up to 17 levels, with no change to the frame's mean (the
  owner's GPU was also running a model). A bit-identical null test in this scene is good for
  frames before 168 only.

### RT-16 — corrected 2026-09-13: the car's lamps do linger, and nothing about them is baked

The 2026-09-09 records put the tubes' 1.84 s down to "a baked light behaving like one" and gave
the car's lamps 0.41 s. **The second number was of nothing**: ShowroomLights starts the car's
lamps off (`StartOn: false`), so `BURST_SWITCH="Headlamp,Tail|1.328"` switched off lights that
were already off, and the pixels it "changed" were run noise. Re-measured with the lamps started
on and the owner's lights button replayed at 8.3 s -- the four Realtime spot lamps to intensity
0 and the twelve lens parts to emissive 0, exactly what `Toggle()` writes
(`session_2026_09_13/stage_run.py`, `lamps_off_scene`):

| after the switch | the lamps' light still on screen |
|---|---|
| 0.05 s | 83% |
| 0.5 s | 60% |
| 1 s | 40% |
| 2 s | 16% |
| 3 s | 6% |

**So the owner's report reproduces with no bake anywhere.** The reflections of the headlamps are
plainly on the wet floor half a second after the switch and faintly at a second and a half
(`build/rt5/fade_fadeL_ship_vs_fadeL_part3.png`, left column). Through the tone curve, so
these compare fades rather than measure a filter's constant; `--capture-signals` reads the
histories linearly and is the instrument for the rest of this item. Taken before the rounding
fix and not re-measured after it.

### RT-16 — the measurement half, done 2026-09-09; the lag is the series and no single term owns it

**Confirmed, and it is what the owner reported.** Tubes switched off at frame
80 of a parked burst, counting frames until the floor is within 5% of where it
settles: **111 frames, 1.84 seconds.**

**Every single term was ablated on its own and none of them owns it:**

| what was turned off | frames | change |
|---|---|---|
| nothing (shipped) | 111 | — |
| TAA's still feedback | 110 | −1 |
| the direct-light signal | 111 | 0 |
| traced reflections | 108 | −3 |
| the accumulator's history | 101 | −10 |
| the accumulator's smooth-surface bound ramp | 111 | 0 |
| its moments floor | 96 | −15 |
| the light field (baked and realtime) | 111 | 0 |
| **TAA's still feedback *and* the accumulator's history** | **31** | **−80** |

**That is the compounding the item predicted, measured.** Removing either
filter leaves the other holding the light; removing both frees it. No term is
responsible, so no term can be tuned to fix it -- which is exactly the design
question the item said the measurement would force: *whether one signal should
pass through two temporal filters at all.* It now has a number behind it.

**Ruled out on the way**, each with a run: auto exposure (the dark regions get
*darker* through the fade, and an opening exposure would brighten them), and
the harness (`Switcher.cpp` sets the intensity in one tick and does not ramp).

**No fix landed.** The measurement is half the item by its own definition; the
other half is the design change, and the honest state is that it needs the
owner's call on the two-filter question rather than another dial.

**Two traps paid for.** `Sample.dll` crashed at the moment the switch fired --
the project's script DLL is compiled against the engine's headers and `Light`
gained members yesterday, which is the second item in the stale-artefacts note;
`cmake --build SampleProject/bin/module` for both configs fixes it. And a burst
of 150 frames sorted by filename puts frame 100 before frame 60: the first
reading of this measurement was of a sequence in the wrong order.

### RT-5 — 🔨 part done 2026-09-09, and two of its five parts were already finished

**Measured before building, which is what shrank the item.** The garage, 120 frames:

    reflection history:  100.0% of glossy pixels kept one, 51.0 frames deep
    reflection refusals: off screen 0.0%, none there 0.0%, normal 0.0%, plane 0.0%, roughness 0.0%

**Part 2 is done.** "The ceiling and far pipes are refused every frame today" was written
from T5's refusal view, before RT-6.3..RT-6.11 landed. The plane test now refuses **0.0%**.
**Part 1 is done** — rejection by id, depth and normal came with RT-6 and RT-6.5.

**Part 4, the evidence-driven anti-lag, is built and off.** In both the reflection
accumulator and the temporal resolve: where every surface test has already agreed the
surface is the same and the pixel did not move, a history whose mean sits more than N of the
pixel's *own* standard deviations from what its neighbours report now is news, and the
memory restarts. The noise estimate is the moments both filters already keep — which is
exactly what R4 lacked when it fired on stills twice. `--anti-lag=N`, zero being off, with
`--anti-lag-floor` under the noise estimate.

**It is unproven, and that is the honest state**: it was built against RT-16's 1.84-second
number, and that number turned out to be a half-baked light behaving like one, measured
through a tone curve. It moved it by 7%.

**Part 3 is the live one.** 100% of pixels keep a history 51 frames deep, and every one of
those frames the neighbourhood bound drags it back toward a four-sample estimate — the
-0.16 levels on the floor the item names. A converged history does not need protecting from
its own noise; a young one does. The counters above are the instrument: relaxing the bound
should raise the depth and leave the refusals at zero.

**A real defect fixed on the way.** `BlurSignal` pushes the whole constant block and
`reflection_blur.rvshader` declared six of its eight vectors, so every draw after it was a
validation error — silently, unless a run asks for `--validation=on`. **Two dead lanes cost
nothing; a short layout costs the command buffer.**

**Two pre-existing validation defects left standing**, both in the blur path this item
exists to retire: a pass pushing 24 bytes to a layout with no push-constant range (10 a
frame), and `Renderer3D.signal.blur.pair`'s descriptor set rewritten while still bound,
which invalidates the command buffer (180 a frame). The second is undefined behaviour and
deserves an item of its own.

### RT-6 — ✅ the still-feedback half, closed 2026-09-09

**What it was waiting for.** The rule -- a pixel that did not move keeps a much
longer feedback (0.98 against 0.9), which is what stops a parked edge crawling
on the jitter's own period -- has been per-pixel and geometrically validated
since RT-6's first half. What kept its *value* a per-project setting was that
**the sea reported no motion at all**, so a long feedback meant for parked steel
was handed to water whose sparkle changes every frame, and smeared it into
horizontal bands. RT-8's first half gave the sea its own motion, and the resolve
has read it since (`taa_resolve`, the water lane). So the test can now tell
still steel from moving water by itself.

**Measured, clean build, frame 200** -- late enough that a band would have
built, which a frame-60 still cannot show:

| camera | banding, off → on | contrast | speckle |
|---|---|---|---|
| headland | 0.0706 → 0.0707 | 13.99 → 13.98 | 0.8513 → 0.8526 |
| pier | 0.0469 → 0.0468 | 14.67 → 14.67 | 0.6790 → 0.6806 |
| glitter | 0.1931 → **0.1917** | 11.90 → 11.87 | 0.7715 → **0.7684** |
| deck | 0.0936 → 0.0938 | 25.37 → 25.37 | 0.5719 → 0.5723 |

**No banding anywhere**, contrast unmoved, and the glitter camera slightly
better with it on. **One value works on both scenes**, which is what the item
asked for; the project's 0.98 stands and needs no per-scene exception.

**Switch:** `--taa-still-feedback=N` (negative leaves the project's number),
because a quality lever without a measurement flag cannot be re-judged.

**A stale comment fixed on the way.** `taa_resolve`'s note above the water lane
says the lane "stays bound and unread" -- the reasoning that stopped it for an
afternoon on 2026-09-08. The decision was reversed later the same day (the sea
got its own average, its glint memory went two frames to sixteen, the pier
ended at 0.915 against 1.160) and the comment did not follow the code.

### The stale build that moved the garage (2026-09-09)

**The garage's mean went 44.533 to 44.590 across two builds of the same source,
and a clean rebuild put it back.** Not a code change -- an incremental build
after a header layout change, which is the third item in this project's
stale-artefacts note and *is not a compile error*. `EngineConfig` gained fields
repeatedly through the day; some translation units kept the old layout.

**What that costs: any measurement taken from an incremental build after a
header change is suspect.** Re-taken from a clean build and confirmed: the
garage and the deck are bit-identical to the morning, the three water cameras
show job 1's change at the same percentages as before (9.00%, 70.22%, 21.82%),
and the still-feedback comparison above. **After adding a member to a widely
included header, rebuild clean before believing a picture.**

### RT-8 closed (2026-09-09)

**Closed by the owner with two of its three jobs dropped.** Recording the state
plainly, because a closed item is read later as a description of the engine.

* **Job 1 — the sea's direct light — shipped and on.** `WaterChooseLamps` and
  `WaterShadeLamps` are retired; the shared `DirectTrace` does the work in two
  halves, choosing on the lamp-choice block and shading per pixel, borrowing and
  re-scoring three neighbours' choices. Verified at four bridge cameras and the
  garage. The pier is the one camera where the change is not symmetric: 29% of
  pixels rise 1.4 levels, 8.6% fall 5.4, net brightness +0.02, and the brightest
  half-percent drops 129.4 → 126.9. It takes the tops off the sparkle and
  spreads it. Cost 1.43 ms against the private pair's 1.21.
* **Job 2 — the sea's rays on the contract — dropped.** The approach is wrong,
  not merely untuned: it reads the half-resolution reflection where the water
  draw traces a sharper one per quad, and then blurs it. That is what turned the
  bridge red. Anything that revisits this must keep the full-resolution trace.
* **Job 3 — the sea's frame averaging on the contract — dropped.** Built,
  correct, and measured to buy nothing: 9% of the headland shot changes and the
  water comes out marginally noisier.

**The code for 2 and 3 is still in the tree behind `--water-ray-contract` and
`--water-contract`, both off.** It is dead weight; deleting it loses nothing
this record does not hold.

**What the item taught, which outlasts it:** the sea's private passes were not
duplication to be removed, they were tuning. Every one of them beat the shared
version until the shared version learned what it was doing -- its lobe, its
block-rate choosing with neighbour borrowing, its full-resolution reflection.
**A fold like this has to buy the tuning back before it is worth doing, and the
only instrument that shows whether it did is a per-pixel diff at the camera the
scene is composed for.**

### RT-8 job 1, done after reading what it replaces (2026-09-09)

**The failure was not the code, it was not reading `water_shade` before
claiming to replace it.** Three things it does that the shared shade pass did
not, and every one of them matters when the lamp choice is made once per block:

1. **It re-scores its own picks at the shading pixel.** The choice was made for
   the block's centre and the weight it carries is only right there.
2. **It borrows three neighbours' picks** off a ring turned by a per-pixel
   angle, validated by depth and normal -- never by a distance in metres,
   because the sea is seen nearly edge on and twelve pixels toward the horizon
   is hundreds of metres of water.
3. **It merges them as reservoirs**, re-scoring each candidate here, keeping
   one in proportion to its share, carrying the sample counts so the
   denominator matches the numerator. The cap goes on each input, never on the
   sum.

The choose pass therefore has to write what a merge needs -- the lamp, **its
sample count**, and its weight, in the sea's own packing. It had been writing
one number per lamp: enough to shade, not enough to merge.

**Measured, at the settings the project ships** (bridge, 2560x1600, frame 60):

| | pixels differing | brightness | contrast | speckle |
|---|---|---|---|---|
| the sea's own two passes | — | 16.22 | 13.93 | 0.834 |
| shared, borrowing off | 18.8% | 16.18 | 13.83 | — |
| **shared, borrowing 3** | **9.0%** | **16.23** | **13.95** | **0.836** |

**The same picture, through shared code.** What differs is balanced sampling
noise -- 2.49% of pixels brighter and 2.41% darker, scattered along the light
streak, with nothing on the bridge and no shift in the mean. The pier agrees:
16.99 → 17.01 and 15.04 → 14.86.

**Cost: 1.43 ms against the private pair's 1.21** -- choose 0.630 + shade 0.800
against 0.595 + 0.614. Eighteen per cent more in those two passes, 0.22 ms on a
twelve-millisecond frame, and that is what folding the sea's lighting into the
shared pass costs today.

**Two patches had silently not applied, and both were measured before that was
noticed.** A patch script exited early on a failed match and the edits after it
never ran, so the graph was still passing one block where two were needed and
never passing the neighbour count at all -- which is why borrowing 0, 1 and 3
gave byte-identical frames. The project's own note already records this class
(`project_ragev_editing_traps`): **a scripted edit that did not apply produces a
measurement of an unchanged build.** The check is one grep after the patch, and
it costs nothing.

**Still off by default.** The picture is a match and the cost is real; whether
0.22 ms is worth removing the sea's private copy is the owner's call, not a
number that decides itself.

### A regression the metrics missed, and what it cost (2026-09-09)

**The owner looked at the headland shot and saw it immediately: the deck's
white lights blending into the tower's red, so the whole bridge read as though
a red light were in the scene.** Bisecting today's commits at that camera:

| commit | what landed | pixels changed vs the morning |
|---|---|---|
| `ab3a96a` | job 3, the sea averaged on the contract | 9.3% |
| **`95dae20`** | **job 2, the sea's mirror ray as a signal** | **46.0%** |
| `af00f8c`, `331f2f8` | job 1 and the split | no further change |

**Job 2, and two things inside it.** Reading the half-resolution reflection
pass at all -- which I had "fixed" as a discarded pass -- and then averaging and
blurring it. Both smear a reflection, and a smeared reflection mixes the white
deck lights into the red tower.

**The "discarded pass" was the wrong reading of a real finding.** The pass ran
off the preset while the draw was told the config override, so its picture went
unread -- and what the draw did instead is *better*: it traces the sea's mirror
once per 2x2 quad at full resolution, where the pass traces at half and
reconstructs. The waste is now removed the other way: **the pass is not built
unless a run explicitly asks**, which is what `--water-reflection` means.

**What this cost, and it is a lesson about the metrics.** The speckle and
contrast numbers called job 2 "a clear win at glitter, about a wash at the
pier". They were measuring the sea's *noise*, and the defect was in the
reflection's *shape* -- a quantity no scalar in the harness looks at. **A
per-pixel diff against the morning's build, at the camera the scene is composed
for, would have caught it in one frame.** The project's own rule already says
render comparisons are judged by diff images; I judged this one by means.

**Every switch is now off by default and the five reference frames -- all four
bridge cameras and the garage -- are bit-identical to the commit before today.**
Two fixes that were not behind switches were scoped to where they were needed
rather than applied to every signal: the twin's own memory now only when it
wants a *longer* one than the first half (the ceiling is exactly right when it
wants a shorter one, which is the direct light's case), and the flat history
bound only on a layer that supplies its own position.

### RT-8 job 1 — 🔨 built and measured 2026-09-09, **off by default**

**What it replaces.** `WaterChooseLamps` and `WaterShadeLamps` are the sea's
private copy of what `DirectTrace` has done for every other surface since
RT-first T5: score the lamps that reach a point, keep K by reservoir sampling,
shade them, trace their shadow rays. The one thing that is genuinely the sea's
is the **lobe** — anisotropic Beckmann about the wind rather than GGX, because
Cox and Munk fitted the sea's slope distribution twice, along the wind and
across it, and that is what turns a highlight from a disc into the streak a low
light lays on water. That is a branch inside `DirectTerm`, not a pass.

So `direct_trace.rvshader` gains an `RV_DIRECT_WATER` variant: the same pass,
compiled again, reading the sea's layer in the four slots the G-buffer uses and
taking the sea's lobe. Everything the two shared is shared in fact now.

**Three defects came out of building it, and two of them were mine from earlier
today.**

1. **The sea's normal is not octahedral.** `FetchWaterPoint` stores the two
   horizontal components and recovers the vertical, because a sea's normal
   always points up — nothing lost, nothing quantised, and a *different*
   encoding from the rest of the frame. Every place this session pointed the
   shared machinery at the sea's surface lane had been running `OctDecode` over
   it since job 3. It shows exactly as a wrong normal should: mean 16.76 →
   14.41 and the 99th percentile 112 → 75 at the glitter camera. `Probe.w` is a
   bitfield now — bit 0 the position lane, bit 1 the up-normal lane — so a
   layer describes itself in one number, and `SignalGuidance` carries it.
2. **The sun was being shaded twice.** The pair this pass writes replaces the
   sea's lamp pair, which the water draw *adds* to a lit loop that already
   walked the directional lights — `ShadeLamp` has no directional branch and
   never had one. Fixed by leaving directional lights to the draw.
3. **The score has to be the term's shape, or the sampler is worse than
   uniform.** `ScoreDirect` scores with a GGX peak at `alpha = roughness²`. The
   sea's roughness *is* the RMS slope, 0.05 to 0.24, so alpha² lands near a
   millionth and the score calls every lamp but the one dead on the mirror
   direction black. The sampler then picks that one almost always, pays it an
   enormous weight, and a four-sample estimate of a heavy tail reads dark and
   spiky however unbiased it is in the limit. Measured before the fix: the
   sea's contrast down a fifth at the pier. `ScoreLamp` in `water_lamps.glsl`
   already makes this argument in its own comment; the shared pass now does the
   same thing.

**Measured, with all three fixed** (bridge, sea band from the mask view):

| camera | speckle, private → shared | sd (detail) |
|---|---|---|
| glitter | 1.362 → 1.356 | 19.52 → 19.56 |
| pier | 1.279 → **1.174** (−8.2%) | 19.76 → 19.80 |

**A cleaner sea with its contrast intact** — which is the result the item wanted.

**And it ships off, on a measurement rather than a doubt.** `DirectWaterTrace`
costs **2.78 ms against the two private passes' 1.18**, because it casts about
**0.86 M more shadow rays** for the same picture. Fourteen per cent of an
11.5 ms frame for eight per cent of one noise metric is the wrong trade until
that ray count is understood. **That is what is left of job 1** — and it was chased the same day. See
below: the rays were never extra, and the answer turned out to be an
architecture question rather than a bug.

**Switch:** `--water-direct=on|off`, **off** by default.

#### Where the extra rays went (2026-09-09, the same day)

**They were not extra.** The sea's private path is hard-capped at four samples
(`RV_LAMP_RESERVOIRS = 4`); the bridge's preset asks for eight. The comparison
was eight samples against four. Swept at matched K:

| samples | private | shared |
|---|---|---|
| 1 | 0.59 M | 0.60 M |
| 2 | 1.19 M | 1.11 M |
| 4 | 2.38 M | **1.96 M** |
| 8 | 2.38 M (capped) | 3.23 M |

**At four the shared pass casts 18% fewer rays** and still cost 2.25 ms against
the private pair's 1.26. So the cost was never the rays.

**It is the scoring, and the reason is the split.** `WaterChooseLamps` runs at
**half the width and half the height** — one lamp choice per 2×2 block
(`EngineConfig::ChooseBlock`, which returns 2 when the reuse is off, and off is
the measured default) — and `WaterShadeLamps` then shades **every pixel** from
that one choice. The expensive part, walking the cluster list and scoring every
lamp, runs at a quarter rate; the cheap part runs at full rate. **The sea's two
passes are not a redundant copy of DirectTrace. The split is why they are
cheap.**

An ablation confirmed the scoring is where the time is: replacing the sea's
lobe in `ScoreDirect` with irradiance alone took the pass from 2.25 ms to 1.90.

**Both halves were then measured, and neither is a win:**

| arm | pass cost | pier speckle | pier sd (detail) |
|---|---|---|---|
| private choose + shade | 1.29 ms | 1.279 | 19.50 |
| shared, per pixel | 3.27 ms | **1.174** | 19.80 |
| shared, per block | **0.86 ms** | **1.092** | **12.32** |

Per pixel: a cleaner sea at its own contrast, for about two milliseconds.
Per block: cheaper than the pair it replaces *and* the frame drops 12.8 → 11.4
ms — and the sea loses **37% of its contrast**, because shading at block rate is
precisely what destroys a glitter track. (A trap on the way: a pass smaller than
the layer it reads must scale its own `gl_FragCoord` by the block, the guidance
pass's `src = dst * d + d/2` rule. Read as its own texel it sampled a corner of
the frame, the sea came back empty, and the pass measured 0.014 ms and no rays.)

**So job 1 stays off, and what is left is not a day's work.** Having both means
splitting the shared `DirectTrace` into a choose pass and a shade pass, the way
the sea already is — an architecture change to a pass four signals use, and one
to put to the owner before building rather than after.

**Switch:** `--water-direct-block=on|off`, off by default, so the trade can be
re-taken without re-deriving it.

#### The shared pass splits, and what is left has a name (2026-09-09, owner-approved)

**`direct_trace.rvshader` now compiles three ways out of one file** -- choose,
shade, and the fused pass exactly as before -- so there is one copy of the
score, the term, the visibility and the field handling rather than three.
Choose walks the cluster list and keeps K lamps by reservoir sampling; shade
reads that and shades them. The blast radius is smaller than it first looked:
`direct_trace` serves the direct light alone, not four signals.

* **What the choose pass writes** is `total / weight[r]` -- one over the
  probability that lamp had of being chosen -- rather than the weight and the
  total apart, so the shade pass needs one number per lamp and no attachment
  for a per-pixel scalar. Two `R32G32B32A32_UINT` attachments: an index is not
  a thing to interpolate, and a weight through a half float would quantise the
  one number the estimate divides by.
* **Four lamps, not eight**, because the reservoir is two uvec4s and the sea's
  own path is capped at four (`RV_LAMP_RESERVOIRS`) -- so the split is
  like-for-like with what it replaces. Above four the fused pass still runs.
* **A split pass always samples.** The fused path's "few enough, shade them
  all" shortcut cannot be written into four slots; where the cell is small the
  reservoir picks those same lamps with weights that still sum right.

**Measured at a matched choice grid** (`--water-lamp-reuse=on`, so both arms
pick per pixel):

| | pass cost | shadow rays | pier speckle | pier sd |
|---|---|---|---|---|
| the sea's own two passes | 3.04 ms | 2.39 M | 1.279 | 19.50 |
| the shared split | **2.80 ms** | **1.97 M** | **1.236** | 19.65 |

**Cheaper, 18% fewer rays, and slightly cleaner at the same contrast.** The
architecture question is answered: the split is the right shape and it works.

**And at the project's default it still loses, for a reason with a name.** With
the choice on the 2×2 block the split costs 1.24 ms against the private pair's
1.29 -- and the sea's contrast falls to 13.75 against 19.50. The cause is not
the split: forcing the choice back to full resolution restores it to 19.65.
**It is that `water_shade` does something the shared pass does not.** It merges
*neighbouring blocks'* reservoirs, validates each by depth and normal, and
**re-scores every candidate at the shading pixel** before using it. That
spatial reuse is where a block choice gets its per-pixel variety back, and
without it one choice really does serve four pixels.

**That is ReSTIR's spatial pass, and it is already filed as RT-10.** So what is
left of job 1 is not job 1: it is RT-10 reaching the shared pass. Until then
`--water-direct` stays **off**, because at the settings the project actually
ships the private path is still the better picture.

**Verified:** with the defaults the whole build is **bit-identical** to the
commit before it at all three bridge cameras -- the fused path, and with it the
opaque direct light, is untouched.

**A trap, and it cost the contrast twice before it was named:** *two* different
blocks meet in this pass -- how many layer texels sit under one of the pass's,
and how many of the pass's sit under one of the choice's. They are different
numbers (the shade pass reads a full-size layer at full rate and a choice made
on the block grid), and sharing one push-constant lane for both made it read
the layer at block rate as well.

**Switch:** `--water-direct-split=on|off`, on whenever `--water-direct` is.

### A fourth, found chasing the sea's red speckles (2026-09-09)

The owner reported speckles in the water, red because the bridge's lamps are.
**They are not new** — 0.142% of sea pixels spiking more than 25 levels above
their neighbours, against 0.142% with every one of the day's switches off — but
chasing them found this: the contract opens its history bound to twelve
neighbourhood spreads on a smooth surface, on the argument that a mirror's rays
hardly scatter so its history needs no bounding. That is true of a reflection
and **false of every diffuse-kind signal**, whose estimate is stochastic
however smooth the surface under it is. The sea shows it because its roughness
of about 0.05 reads here as a mirror. `gloss` is already forced to one for
these signals two lines below, for the same reason and in nearly the same
words; the bound now follows it.

**Measured neutral, and kept for the argument:** pier 0.139% → 0.130% of sea
pixels above 25 levels, glitter 0.509% → 0.514%, **garage bit-identical**. So
it is not where the fireflies come from. They survive a tight bound as well as
a loose one, which rules out the temporal clamp and leaves the estimator's own
tail — the same heavy tail defect 3 above describes, one step further along.

### RT-8 job 2 — ✅ done 2026-09-09: the sea's mirror ray becomes a signal

**What it had.** WR-16 S5 traces the sea's reflection in a pass of its own at a
fraction of the width and height, and the water draw reconstructs it with four
taps weighted by how far each ray went. That is a *spatial* reconstruction of
one frame's rays. There was **no temporal average anywhere in the chain** --
every frame's reflection was that frame's rays and nothing else, which is why
the sea's mirror has always been the noisiest thing on the bridge.

**A defect found while wiring it, and it was free work thrown away every
frame.** The pass ran off the preset's block size and the *shader* was handed
`EngineConfig::WaterReflectionScale`, which is **zero unless a run asks for an
override**. So on a level whose preset traces the sea's reflection separately,
the pass ran, its picture was bound, and the water draw cast its own quad rays
anyway and read none of it. Resolved once at the top of the frame now, and told
to the renderer there -- which it has to be, because the scene block that
carries it to the shader is filled the first time a pass executes.

**What was built.** The traced picture goes through the contract's accumulate
and blur at the trace's own resolution, and the four taps become the joint
bilateral upsample at the end of a reconstruction instead of the whole of one.

* **The specular kind, deliberately.** Every specular test is the right
  question for a sea: has the reflected direction swung (the wave turns), has
  what the ray hits moved (the bridge above it has), and where does the image
  sit now the surface has risen — RT-15's mirror rule, which is what a wave
  does to a reflection every frame. The trace already writes its hit distance
  in alpha with a negative for "no sea", which is the contract's own sentinel.
* **A guidance at the trace's grid**, selected from the sea's layer and never
  averaged: the average of two positions across a wave crest is a point in
  neither. `gbuffer_guide`'s first lane is a whole texel now, so one shader
  serves both a clip depth and the sea's position-and-mask.
* **`SignalParams::NoObjectId`.** The contract weighs a history by whether it
  came from the same object, which needs a lane saying which -- the G-buffer
  has one, the sea's layer does not, because the sea is one surface. Without
  the flag the test falls back to comparing this frame's packed normal against
  last frame's, which on a turning wave differs every frame and would shorten
  the memory for a reason that is not there. Told in `PreviousEye.w` as a third
  value (0 no eye, 1 an eye, 2 an eye on a layer with no ids), so the direction
  test's `> 0.5` is untouched -- a sea wants that test more than most surfaces.
* **The ray distance moved.** The contract takes the picture's alpha for its
  frame count, so the distance the four taps weigh by now comes from the
  accumulate's surface attachment, handed to the draw beside the picture. One
  bit of `WorldGridScale.w` says which.

**Measured** (bridge, sea band from `--debug-view=water-mask`):

| camera | speckle | sd (detail) | p99.9 | mean |
|---|---|---|---|---|
| glitter | 1.404 → **1.354** (−3.6%) | 19.61 → 19.52 | 217.6 → 217.2 | 16.48 → 16.76 |
| pier | 1.315 → **1.285** (−2.3%) | 20.22 → 19.75 | 197.2 → 195.5 | 23.63 → 23.46 |

Re-taken after the normal decode was fixed: **1.405 → 1.363 and 1.309 → 1.279**,
the same result to within a few thousandths.

**Read honestly: a clear win at the glitter camera and about a wash at the
pier.** Glitter loses 3.6% of its speckle for 0.5% of its contrast; the pier
loses 2.3% of each, which is noise and detail going out together. The mean
barely moves, so neither is a haze. The case this is actually for -- a moving
camera, where a signal with no history at all has nothing to fall back on --
has no harness here.

**Cost:** the five new passes total **0.16 ms** at the pier (accumulate 0.100,
guidance 0.035, three blurs 0.027), against a frame of 11.7.

**Switch:** `--water-ray-contract=on|off`, on by default.

### RT-8 job 3 — ✅ done 2026-09-09, and the argument against it was wrong

**The claim, and it was mine.** Folding the sea's frame averaging into the
signal contract was argued against for most of a session on this: the
contract's geometric gate cannot hold a sea, because a wave lifting the surface
a metre slides the point seen at one pixel by tens of metres at a grazing
angle, so a test asking "is the surface still the distance it was" would refuse
the water constantly. The number offered in support -- switching the sea's
averaging off costs 1.160 → 1.609 of speckle -- measures *the sea with no
averaging*, which is a different thing. **Nobody had measured the gate.**

**The measurement.** The water's own accumulate was made to run the contract's
geometric tests beside its own -- the same tolerances, the same nine-texel
search, the same reasons in the same order -- and act on none of them. Bridge,
120 frames, three cameras:

| camera | the gate keeps | without RT-15's exemption | the plane test refuses |
|---|---|---|---|
| glitter | **98.6%** | 98.6% | 1.1% |
| pier | **98.7%** | 98.7% | 1.1% |
| deck | **92.9%** | 92.9% | 5.1% |

**The claim was false.** The plane test measures how far the surface moved
*along its own normal* between frames -- a couple of centimetres for a wave --
against a tolerance of `0.05 + 0.01 × eye distance`, which is metres out on a
bay. The tens of metres in the argument are the point sliding *across* the
screen, and RT-8's own motion attachment already cancels that in the
reprojection. **The argument was true of the sea before the same session
taught it to report its own motion, and nobody re-took it afterwards.**

Verified before it was believed: shifting last frame's stored plane by 100 m
drives the strict arm to **0.0% kept and ~97% plane refusals**, so the test is
live and reading real history; and the whole measurement is **bit-identical**
at all three cameras against a full rebuild of HEAD, so it decides nothing.

**A trap worth the entry on its own.** The first null test said the picture had
moved, and it had not. Staging the committed shader against the patched engine
runs a pass whose resource set is written past its own layout -- six bindings
declared, eight bound; two attachments written, three declared -- which is not
"the engine as it was". The committed *logic* under the patched *layout* was
bit-identical. **A shader-only A/B is only a control while the layout is the
same on both sides.**

**What was then built.**

* **The contract reads a position lane.** `SurfaceAt` rebuilt P from the depth
  buffer, which assumes the signal sits on a surface that buffer describes --
  and under the sea it describes the seabed. That, not the plane test, is what
  kept the sea out. The depth binding now takes a second meaning rather than a
  second binding (`Probe.w`, `SignalParams::PositionLane`): the layer's own
  position in xyz with a mask in w. It is the more exact of the two, and any
  future layer that knows where it is can take the same route.
* **`SignalGuidance` carries lane indices.** The sea's surface pass already
  writes the three lanes the contract validates against -- normal at 0,
  position at 2, motion at 3 -- so the sea needs no downsample and no copy to
  join. The guidance points straight at them.
* **The twin got a memory instead of a ceiling**, and this was a real defect
  in the contract, not a water problem. `frames2 = min(frames, PairMemory)`
  can only ever make a pair's second half *shorter* than its first. The one
  pair before the sea -- the direct light -- wants exactly that, so it was
  never noticed. The sea runs the other way: what enters the water forgets in
  four frames, the glint off it wants sixteen. **Measured: the sea held four
  frames where it wanted sixteen, and read noisier than the pass it replaced.**
  The shortening chain is now a function of a base memory (`ShortenedMemory`)
  and each half goes through it with its own, sharing every reduction. The
  glint now holds **13.3 frames of its sixteen** at the pier.
* **The counters split by signal.** Acceptance and depth had been counted for
  the specular instance alone since RT-19, because one set of lanes summed
  over four signals describes none of them. The sea is a fifth and has its own.

**Measured, final** (bridge, water band taken from `--debug-view=water-mask`,
so the metric is where the water is):

| camera | speckle, own → contract | sd (detail) |
|---|---|---|
| glitter | 1.340 → 1.363 | 19.46 → 19.53 |
| pier | 1.215 → 1.279 | 19.63 → 19.50 |

**Re-taken 2026-09-09 after the sea's normal decode was fixed** (see job 1's
first defect): the gap is 1.7% and 5.2%, against 1.6% and 5.6% before, so the
wrong normal was **not** what this cost. **The contract is a few per cent
noisier on speckle at the same contrast**, and that is stated rather than
tuned away. It is not the memory:
at equal history depth (14.9 frames against a flat 16) the gap holds, and
sweeping every travel cap from 1 to 16 moves it by 0.6%. The travel caps were
**left at the contract's own values**: they exist to stop an average smearing
under a moving camera, there is no bridge dolly in any harness here, and taking
a gain nobody can see for a defect nobody can test is the wrong trade. The dial
(`--water-lamp-slack`) stays so it can be re-taken the day a dolly exists.

**What the sea gains for that 5%:** one code path instead of two, disocclusion,
the silhouette rule, the object-id and material tests, the young-history blur
machinery, and — the part the private pass never had — a memory that shortens
when the camera moves. The private pass kept its full memory however fast the
view travelled, which is precisely why nobody could say what it did on a dolly.

**Regression:** the pair-memory change touches the direct light, the contract's
only other pair. Garage at its benchmark pose: **max 1 level on 0.000% of
pixels.**

**Switch:** `--water-contract=on|off`, on by default.

### RT-8 — 🔨 part done 2026-09-08 (uncommitted)

**What the sea now has.** Its surface pass was already a G-buffer of its own in
all but name -- position in full floats with a mask, the normal with roughness
and wind, the colour with the specular dial. It now also carries **the wave's own
screen motion and the object id**, in a fourth attachment. The motion was never
missing: `water_vertex.glsl` has always evaluated the wave at last frame's time
and built `v_PrevClipPos` from it, with a comment saying why. There was simply
nowhere to write the difference.

**The bug that hid it, and the class to check first.** `Renderer3D`'s water
surface pipeline hard-codes `surface.ColorFormats` and `BlendPerAttachment` with
**three** entries. The target grew a fourth attachment and the pipeline did not,
so the shader's write to location 3 went **nowhere, in silence** -- no validation
message, no warning. `--debug-view=water-mask` (new, below) showed the layer
empty where the sea plainly was, and that was the whole diagnosis. **A pipeline
carries its own attachment count; growing a target is never enough.**

**The finding that reordered the item: the sea reading zero velocity was
load-bearing.** Handing the temporal resolve the wave's true motion made the
water *specklier* -- 0.653 → 0.763 at the glitter camera, 1.160 → 1.432 at the
pier, and **0.787 / 1.503** when the motion drove only the stillness test. The
owner saw it before the metric did. A wave carries glitter, which is not attached
to the water, and `TemporalStillFeedback = 0.98` was averaging that sparkle over
about fifty frames **because** the seabed's velocity under the sea said nothing
had moved.

**Resolved by giving the sea an average of its own instead of a borrowed one.**
The water accumulate already keeps the lamp light's glint on a separate memory
and it was **two frames** -- short for a good reason at the time, since the sea
also had TAA's fifty behind it. At the pier with the motion live: speckle 1.432 at
two, 1.019 at eight, **0.912 at sixteen**, 0.829 at sixty-four, against **1.160**
for the shipped build with no water motion at all. **Not haze:** contrast rises
(sd 17.28 → 18.46) and the peaks brighten (99.9th percentile 151.8 → 155.2) all
the way up the sweep. Landed at sixteen -- the curve is nearly flat past it and a
shorter memory follows a light that goes out sooner.

**Measured, final:** pier speckle **1.160 → 0.915**, glitter **0.653 → 0.591**,
the deck bit-identical, the garage bit-identical. The sea is smoother than it was
*and* now tells the truth about moving.

**Also landed:** the water accumulate reprojects by the wave rather than through
the previous camera alone -- the same defect RT-15 fixed for reflections, and the
sea is the one surface that always moves. Measured **neutral** on both static
cameras (0.915 → 0.924, 0.591 → 0.594, inside the noise); kept because it is the
quantity that actually describes what moved, and it costs one fetch. It wants a
camera dolly on the bridge to be judged properly, which no harness here has.

**New instruments:** `--debug-view=water-motion` and `--debug-view=water-mask`.
Every question about the sea before these existed cost a hand-staged probe.

**What is left, and it is most of the item:** the water's direct light through
`DirectTrace`, and its mirror and refraction rays as signals. **And one piece
this session argues against on evidence:** folding the choose/shade/accumulate
passes into the shared contract. The water accumulate's own header explains why
the contract's geometric validation cannot work for a sea -- a wave lifting the
surface a metre moves the point seen at one pixel by tens of metres at a grazing
angle -- so it validates by the neighbourhood's spread instead, and measuring it
off costs 1.160 → 1.609 of speckle. Folding it in would replace a test that works
with one its own record says fails. **Raise it with the owner before building.**

**Method notes.** `context.Color(...)` and `SetTexture(slot, ...)` were both
proved good by binding the G-buffer normals to the slot and watching them
arrive -- do that before suspecting the plumbing. And the runtime **must** be run
from `build/bin/Release/RageVRuntime`: from the repo root it silently compiles no
shaders and every frame comes back black, which reads as a broken change.

### RT-19 — ✅ done 2026-09-08 (uncommitted)

**What was built.** The ray-counter block widened from 16 lanes to 32
(`RayCounters::Count`; the shaders' stride `RayCounterSlot() * 32u` in
`pbr_fragment.glsl`, `taa_resolve` and `rtao_compute` must agree, and the CPU
buffer size follows `Count` on its own). Fifteen new lanes: the temporal
resolve's six refusal reasons and its summed history length, and the reflection
accumulator's pixels, kept, five refusal reasons and summed history length.
`CountTemporal` now takes the reason and the frame count; the accumulator gained
`CountSignal`, counted on the **specular instance only** -- the same shader also
runs for occlusion, the bounce and the direct light, and one set of lanes summed
over four signals would describe none of them. **No plumbing was needed:** the
counter buffer is already declared at set 0 binding 21 under `RV_RAY_SHADOWS`
(which the accumulate pipelines compile with) and already bound to the pass with
the lamp set. Four lines print beside the ray counters.

**What it says on the garage with the moving panel** (120 frames, 1600x900):

```
temporal confidence: 99.7% of pixels reused their history, 55.5 frames deep on average
temporal refusals: off screen 0.0%, no history 0.0%, sky 0.0%, object id 0.2%, depth 0.0%, normal 0.2%
reflection history: 99.9% of glossy pixels kept one, 50.4 frames deep on average
reflection refusals: off screen 0.0%, none there 0.0%, normal 0.0%, plane 0.0%, roughness 0.0%
```

**And that is the finding.** With a near-mirror crossing the frame, the reflection
accumulator refuses **essentially nothing** and holds **fifty frames** on average.
Whatever the smearing on a moving reflector is, it is not a shortage of
refusals -- the filter is keeping almost every history it is offered. The same
question cost an afternoon of staged probes earlier the same day.

**Read them as whole-frame averages**, which is what this item builds: static
pixels dominate the denominator, so a per-region or per-object split is a
separate piece of work and is not here.

**Verified two ways.** The refusal percentages sum to the complement of the
acceptance rate by construction (0.4% against 0.3%, rounding), which is the
self-check to make if a line ever looks wrong; and with `--aa=none` the temporal
lines report "no temporal resolve ran" while the reflection lines still report
real numbers -- so the two sets are live and independent rather than stuck
constants, which is the test this codebase has learned to run before believing a
counter (RT-3's record).

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
*(Done the same day as RT-6.1.)*

### RT-22 — open, filed 2026-09-15 (owner): a moving light's lighting trails behind it

**The owner's question:** what happens when the moving object is an emitter and a source of
light. **The test** (`emitter_mover.py`, `emitter_lag.py`): a 0.4 m cube, emissive [8, 5, 2.5],
with a Realtime point light 0.45 m under it (warm, intensity 25, range 10, casting shadows),
both driven by the Slider at 1.5 m/s across the owner's shot and the close-up; each arm against
its own settled pose (the drive stopped where frame 100 has it, frames 150-169).

**What works.** The light pool, the car's lit side and the car's shadow on the floor move with
the light; the glow shows in the wet floor, the car's side glass and its door.

**What trails.** Red where the moving frame still holds the light's old place, blue where the
new place has not arrived (`build/emitter/close_vs_settled.png`):

| arm | floor beside the car, px off by > 16 | the car's side |
|---|---|---|
| as shipped | 11.9% | 12.3% |
| measured change off | 41.5% | 14.5% |
| the direct light's history off (the lit loop; takes measured change with it) | 40.7% | 12.4% |
| traced reflections off | 7.1% | 10.6% |
| the bounce signal off | 11.9% (identical: the garage's bounce is baked) | 12.3% |

**By design, not part of the item (owner):** a glowing object alone lights nothing -- only 16
glowing surfaces get shadow rays aimed at them and the garage's tube bars fill the list, so the
cube with no point light shows only in reflections -- "that's how our tube lights work too". A
moving light's bounce is absent where the bounce comes from a bake.

**A harness defect found on the way, fixed:** `burst.py` plays a renamed copy of the scene,
the engine looks for a bake by the scene's stem, and it never found the showroom's -- every
garage measurement through the harness until this day was of realtime bounce light
("Falling back to Realtime" in the log). Comparisons between arms of one run stand; absolute
pictures differed from the editor's. `sync_bake()` now copies the bake beside the copy.

### RT-17 — ✅ built 2026-09-15 (uncommitted): the reflection history tests what its rays struck

**Built on the owner's word after measurement said it would change little here** ("build
RT-17 anyway, let's eliminate all the unnecessary variables so that we can actually pin
point the issue later"). The case it answers -- something moving through a still
reflector's picture at about the distance the ray already went, which RT-6.10's hit
distance cannot see -- was measured first against a settled reference per pose (the car
stopped where frame 75 has it, frames 150-169): on the garage's floor and poles the history
was worse than one frame's rays by 16 levels on 0.00-0.35% of pixels.

**What was built.**
- The trace writes a third lane (R16G16, transient): r the struck instance's identity, g the
  struck normal in ten bits plus 1024 where that instance moved this frame.
- The identity is stable across frames: `RayInstance::Identity` (the `_pad1` word) is the
  caster's entity folded into 1..1021 (1022 for no entity, zero a miss) -- the row index is
  rebuilt every frame in arrival order. `RayCaster::Moving` / `RAY_INSTANCE_MOVING` (16): posed,
  or `World` differs from `PreviousWorld`.
- The accumulator keeps the identity in the id lane's spare b and a (no new history) and,
  on the specular kind, scales the match confidence to 0.2 where the history's struck object
  is no longer struck by any ray in the fresh 3x3, or is struck with its face turned by more
  than 25 degrees -- whole on a mirror, gone by roughness 0.3.

**Trap paid, the first version: the test fired on 9% of a parked garage.** A glossy
floor's ray lands on a tube one frame and on the ceiling the next for no reason but its
sampling. The test now counts a change only when an instance that moved this frame is
involved, and the engine skips it altogether on a frame where nothing moved this frame or
the last (`Change.y`). Painted red where it fired: 0.000% of the parked garage, 0.34-0.56% of
the driving car's frames -- the floor under the car, the poles' reflections of it, and the
car's panels reflecting its own moving parts.

**Measured** (`rt17_test.py`, `rt17_ref.py`; off = the test switched off in a staged copy):
- Parked garage, camera dolly, the bridge's deck, pier and glitter: identical (at most 1 level).
- Cube crossing: mean 0.02 levels apart, at most 23. Car close-up: 0.35% of pixels over 4
  levels. Car wide: 0.05%.
- **The floor under the driving car at the close-up**, against the settled pose: error 6.05 ->
  5.68, pixels worse than one frame's rays by 16 levels 3.37% -> 2.30%. Poles and the rest of
  the floor unchanged.
- Cost: nothing parked (the accumulate 0.599 -> 0.595 ms); about 0.06 ms while anything moves.
  Validation clean under TAA, MSAA and SSAA with the car driving; scenetest Vulkan exit 0.

### RT-18 — ✅ built 2026-09-15: a moving silhouette's history is tested like any other

**The owner's order: RT-18 and RT-17 before RT-15**, because ghosting makes false
positives of everything measured after it. A first proposal to lengthen the memory on
moving objects instead (option B) rested on a wrong claim -- that the memory falls to one
frame there -- and measurement took it apart the same morning: on the driving car the
reflection holds about 5 frames (the image moves 5-7 texels a frame and the motion rules
cap it), on the cube about 15, and a staged floor of 16 frames changed nothing. The owner's
order was right.

**The defect.** `HistoryAt` keeps a silhouette texel's own history with no test at all, so
a parked edge whose side flips with the jitter averages into its coverage (WR-16 R5). While
an object moves, that history is the other side's picture and the edge's highlight -- and a
frame later the texel is inside the object, where the history passes every test and stays.
Rendered with the averaging off, the moving cube's face is dark and clean; averaged, it
filled with bright vertical stripes, one column of its own edge per frame. The car left a
grainy trail behind its rear the same way.

**The fix.** The exemption holds only where nothing in the 3x3 moves on its own
(`ObjectMovesAround`: `ObjectShift` on each neighbour, the velocity lane read first so a
still texel costs a fetch). A camera move alone keeps it. Reading any motion instead -- the
velocity lane's length -- changed 3% of the dolly's pixels by up to 241 levels for nothing
the dolly needed, so it was measured and not taken.

| case | as shipped | RT-18 | this frame only, no averaging |
|---|---|---|---|
| cube: grain (px > 16 from their 3x3 median) | 9.47% | 4.66% | 2.02% |
| cube: mean level | 33.9 | 22.1 | 17.4 |
| cube: frame-to-frame | 2.81 | 1.73 | 1.72 |
| car: grain | 3.57% | 2.79% | 2.71% |

Parked garage, camera dolly and the bridge's deck, pier and glitter cameras: identical to
shipped (at most 13 levels, in a handful of pixels). Cost within this machine's drift (the
four accumulate passes together under 0.1 ms). Validation clean under TAA, MSAA and SSAA
with the cube moving; scenetest Vulkan exit 0. The shared code means the lamp light, the
bounce and the occlusion signals take the same rule.

**Left on the cube, and not RT-18's:**
- **The strip the cube uncovers each frame starts from one ray**, about four texels wide, so
  neighbouring strips differ and read as bands. No history exists for those texels. The
  young blur smooths them (`--reflection-blur=6`: cube grain 4.66% -> 3.16%, car 2.79% ->
  1.39%) and costs a moving camera's floor 19% of its detail (12: 30%) -- the reason it was
  retired. More rays where history is young is RT-9's subject.
- **The older part of the face keeps bright speckles, and the bar along its bottom bands
  vertically** (owner, marked on the young-blur picture): the young blur does not reach
  them, and neither is explained yet.

**The owner's decisions (2026-09-15):** the uncovered strip is to be answered with **more
rays where history is young** -- RT-9's confidence-driven allocation -- not with the young
blur. **The speckles on the older face and the bottom bar's banding are a known issue for
later**, noted here and in RT-9's row; nothing is built for them yet.

**Instrument added:** `--capture-signals=reflections` now keeps all five lanes (the
picture, the reflector, what was learned, the image motion, the id), so a staged
accumulator can write its memory's steps where the capture reads them back
(`refl_memory_diag.py`).

### RT-13 — ✅ closed 2026-09-20: on by default, and the panes behind stay as they are

**The default, 2026-09-20 (owner: "turn it on by default and run the full check set").**
`GlassLayer` is `true`; `--glass-layer=off` is the old path.

- **Validation** 0 messages and no shader failure on the garage under TAA, MSAA and SSAA, on the
  close-up and on the bridge. **scenetest** OK on Vulkan and OpenGL. **The bridge is
  byte-identical** -- it has no glass.
- **What the default changes**, parked: the owner's shot 0.057% of pixels over 2 levels (0.021%
  over 16), the close-up 0.819% (0.231%), a 60-frame dolly 0.086% (0.034%) and only in the band
  the car passes through. **Two runs of the same arm differ by 0.000%**, so every one of those
  numbers is the glass and nothing else.
- **Cost +0.45 ms** at the owner's shot: eight runs of 200 frames, palindrome order, off
  15.569/15.595/15.598 against on 16.008/16.022/16.023/16.086, spread under 0.09 inside an arm.
  (The session's first off run read 14.754 -- a cold GPU, not counted.) The new passes are
  0.544 ms of it -- trace 0.146, direct trace 0.132, reflection accumulate 0.120, direct
  accumulate 0.080, resolve 0.052, the layer 0.014 -- and about 0.09 comes back where the nearest
  pane stops casting its own rays. **The record's earlier +0.35 ms was measured before RT-15's
  four fixes and RT-9 landed**, so the baseline itself moved.

**Moving glass does enter the layer**, which I had doubted from this record's own wording. The
layer takes `DrawKind::Static` draws, and in this engine that names the *vertex layout* -- plain
rigid meshes as against skinned, terrain-layered and water -- not whether the thing moves. The
car's glass is in the layer while it drives, and the silhouette moves with it. What cannot enter:
glass on a skinned mesh, glass on terrain, and water.

**The panes behind the nearest stay on the old path (owner, 2026-09-20: "leave it and close
RT-13").** One pixel holds one note, so the layer describes the nearest pane only; every pane
behind it keeps the pre-RT-13 path, walking the lamps and casting its own rays as it is drawn.
That path has no limit and its picture is right at any number of panes. The three answers and
why two were rejected:
- **A second layer** covers pane 2 and leaves pane 3 exactly where pane 2 is now (the owner's own
  objection). Another full set of passes and a second note per pixel, and a cap to decide anyway.
- **Depth peeling** generalises it -- draw the glass again, keeping only what is further than the
  last pass kept -- at a full set of passes *per pane* (0.54 ms each here, ~1.6 for three), paid
  over the whole screen, plus the old path past whatever cap is chosen. **And the serious part is
  not the cost: "layer 2" is a position, not a surface.** Swing the camera and the side window
  becomes nearer than the windscreen, so layer 2's memory is handed to a different pane -- which
  is the wrong-surface memory this whole series exists to stop. Buildable with a per-pane identity
  test (RT-6.5's machinery), but that is a build of its own.
- **Borrowing the nearest pane's settled picture** saves nearly the whole 1.3 ms and shows a
  reflection from the wrong angle; plausible on flat side glass, wrong on curved.

**What travels to the optimisation pass:** glass casting its own reflection rays, 1.3 ms of the
close-up's 2.0 ms transparent pass when that split was measured (before RT-15 and RT-9 moved the
baseline). That is a cost question for every pane, not a structural one for the second.

### RT-13 — the glass joins the shared passes: stages 1, 2 and 3 built 2026-09-14 (`--glass-layer=on`, now the default)

**The owner's choice (option 2).** Glass is lit and reflected by the same ray-traced
passes as every opaque surface, through a G-buffer layer of its own, in three stages:
1 the layer, 2 the lamp light, 3 the reflections. Stages 1 and 2 are built; stage 3
waits for the owner's word.

**Stage 1, the layer.** `--glass-layer=on` (`EngineConfig::GlassLayer`) draws the
blended static meshes with the G-buffer variant (`RV_GBUFFER` + `RV_GLASS_LAYER`) into a
"GlassLayer" target after "Scene": the G-buffer's four lanes in its order (velocity,
surface, albedo, id), a D32 depth of its own so the nearest pane wins, one sample. Each
fragment discards itself behind the opaque depth (set 0 binding 29). **Depth is reversed
-- nearer is larger** -- and the first version's test was the wrong way round.
`Renderer3D::FlushGlassLayer`, `--debug-view=glass-layer`. Picture pixel-identical to
off; 0.015-0.02 ms.

**Stage 2, the lamp light.** The DirectTrace pass runs again on the layer
("GlassDirectTrace", on an input set of its own so the opaque trace's recorded bind is
never rewritten), settles on the direct light's contract ("GlassDirectAccumulate",
`GlassDirectSignal()` on slot 6, guided by the layer's own depth attachment --
`SignalGuidance::DepthIsAttachment` -- normal and motion; history `GlassDirectLight`),
and the transparent variant (`RV_GLASS_SIGNAL`, set 3) takes it on the nearest pane: a
fragment whose depth matches the layer's to 1e-5 relative and whose object id matches
reads the settled pair instead of walking the lamps, and every pane behind it keeps the
loop. Only where the opaque direct light is a signal: `--direct-signal=off` puts glass
back on the loop with everything else. The glass history has no measured change (the
change map describes the opaque surfaces).

**Trap paid: a render-graph target's depth is not sampleable unless `SampleDepth` is
set.** Without it every reader got no depth -- the trace returned early, the pane test
read zero -- and the first measurement came back pixel-identical because nothing ran.
Found by staging a shader that painted each test's result on the glass.

**Measured** (`tools/scripts/garage/session_2026_09_14/rt13_stage2.py`; HEAD's garage,
the close-up `-4.3,1.0,-2,3.2,25,8` and the owner's shot; sheets in `build/rt13/s2/`):

| | shared lamp light vs today | lamp light removed vs today (the term's size) |
|---|---|---|
| close-up, parked, mean of 186-249 | 0.1% of glass pixels over 1 level, max 18 | 6.5%, max 53 |
| owner's shot, parked | 1.2%, max 13 (faint, on the headlamp lenses) | 19.1%, max 41 |
| the car driving (root only, 1 m/s for 2 s) | over 4 levels on 0.06% of glass pixels | 24% |
| MSAA 4x / SSAA 2x, parked | 0.2% / 0.0% | 6.2% / 6.4% |

Glass flicker parked 0.789 -> 0.789 and 0.724 -> 0.725 levels a frame; everything that is
not glass unchanged. Validation clean under TAA, MSAA and SSAA; scenetest Vulkan 2494
passes, no failures.

**Cost: more, not less.** Close-up: the transparent pass 1.88 -> 1.82 ms, the new passes
+0.64 (trace 0.49, accumulate 0.13, layer 0.02), frame 17.9 -> 18.4. Owner's shot: 0.64 ->
0.62, +0.25, frame 13.9 -> 14.5. Walking the lamps was never what glass cost: **its own
reflection rays are 1.3 ms of the close-up's 2.0 ms transparent pass** (with them removed,
2.00 -> 0.70). The owner: optimisation later.

**Stage 3, the reflections -- built 2026-09-14, the owner's look.** The reflection chain
runs again on the layer after the lamp light's passes: "GlassReflectionTrace" and
"GlassReflectionResolve" (`TraceReflections` / `ResolveReflections` with `glassLayer`, on
input sets of their own), "GlassReflectionAccumulate" (`GlassReflectionSignal()` on slot 7,
guided by the layer's depth, normal, motion and -- `SignalGuidance::Id` -- its object id;
history `GlassReflections`). The nearest pane reads the settled picture (set 3 binding 4)
and weighs it as the opaque hook does -- its frames against the lift, inside the gloss
window -- instead of casting its own rays; a one-texel stand-in means the passes did not
run and the pane traces as before, as every pane behind it does. Only where the lamp light
signal and the opaque traced reflections both run. No measured change.

- **Look:** the windscreen's tube reflections come out brighter and wider, with more of the
  ceiling in them (close-up: 21% of glass pixels over 1 level, glass mean 36.4 -> 39.4).
  **The owner: "the brightness and bleed look more close to a real glass reflection."** Two
  guesses at why were tested and were wrong (today's 8x cap on a ray; the ray offset's
  normal) and the question was dropped on the owner's word.
- **Driving:** grain on the glass 1.71% of pixels against today's 1.84%.
- **Cost:** owner's shot frame 14.36 -> 14.71 ms (glass passes 0.60 -> 1.14); close-up
  17.90 -> 18.81 (1.93 -> 2.87: the transparent pass 1.65, trace 0.30, resolve 0.16,
  accumulate 0.13, plus stage 2's). The transparent pass keeps most of its cost because the
  panes behind the nearest still cast their own rays. Optimisation later (owner).
- **Validation** clean under TAA, MSAA and SSAA; scenetest Vulkan exit 0, no failures.
- **Lights switched off: not a test in this scene** -- the garage's tubes are baked, so their
  fade is the bake's rebuild and says nothing about the glass (owner).
- The counters' `CountsAsWater` now takes slots 4-5 only, so the glass layer's 6 and 7 are
  not counted as the sea.

**Found on the way -- not stage 2's, the same with the glass layer off:**

- **The driving car's body sparkles and trails.** Isolated one signal at a time, each set
  to its reference form: only the traced reflections off removes it (body pixels over 16
  levels from their neighbourhood 2.71% -> 1.02%, the rest decal detail); the lamp light,
  the bounce and the occlusion change nothing. The windscreen, which casts its own rays
  with no memory, stays clean. **This is the defect RT-15 names** ("noise on a mirror, and
  the ghosting the owner sees while driving the car"), closed 2026-09-08 on the moving
  cube; on the car it does not hold. Open, and stage 3 would move glass onto this path.
- **MSAA and SSAA speckle where TAA does not.** MSAA 1.96% of glass pixels and 1.23%
  elsewhere against TAA's 0.63 / 0.61; the glass's reflection rays removed, glass 0.85%;
  the traced reflections off, 0.50 / 0.59. It is the reflections' noise, which only TAA's
  frame average takes away.
- **`BURST_SLIDE` with the car's bare tag prefix moves 49 entities**, 25 of them children
  of others, so parts move twice and the car comes apart -- HANDOFF's item 6, repeated
  here. `=porsche_992_gt3_r` moves the root alone.

### RT-4 — ✅ done 2026-09-14 (owner's call): the trace before the lit pass; R11 measured and carried into RT-21; SSAA fixed on the way

**The move.** The reflection chain -- trace, resolve, measured change, accumulate
-- runs between the G-buffer and the lit pass like every other signal
(`FrameGraphBuilder.cpp`, above "Scene"), and the lit shader's hook reads **this
frame's** accumulated picture at the pixel (`hookNDC` in `pbr_fragment.glsl`,
the texel `reflection_composite` adds) instead of last frame's reprojected.
The composite stays after the lit pass for RT-6.1's motion choice. Measured on
the garage against the committed build: parked, the changes are one-pixel
silhouette lines (the share and the added picture now describe the same texel;
by eye identical at 4x); edge flicker parked unchanged (car 1.19 -> 1.24, poles
1.15 -> 1.12 levels a frame); the moving chrome cube shows slightly less
speckle, its stripes unchanged. Validation clean on the garage and in every AA
mode, the bridge's two known messages only; scenetest Vulkan 2491 / OpenGL
2432, no failures.

**R5's same-surface rule: nothing to build.** RT-6.10 compares each texel's own
image distance with a roughness-scaled tolerance, which is the rule without the
3x3 minimum that fired on parked silhouettes.

**R11, re-measured** (`rt4_r11.py`, `rt4_r11b.py`; the garage parked, mean of
frames 360-399, 9x9 low-frequency luma against a *truth* of no resolve, the
accumulator unbounded, memory 400): shipped floor **-2.50** (bright -3.05);
the accumulator's bound and memory change nothing (-2.46, -2.48); the resolve's
weighting is the whole of it -- Gaussian-only +3.22, the ratio at cap 16 -4.48,
uncapped -5.93. With the pdfs computed without DistributionGGX's denominator
floor: +2.69 (the solid-angle Jacobian adds nothing, +2.70; the neighbour's own
draw pdf +3.14, uncapped +2.36). **The truth is not exact either**: the trace
does not weight by NoL and replaces a draw under the horizon with the mirror
ray, so which brightness is right is open. Carried into **RT-21** with the cap.

**SSAA's traced reflections were broken, and not by this** (the committed build
rendered the same picture bit for bit). The reflection history was allocated
at the output size and read the supersampled G-buffer texel for texel, so the
accumulate saw the top-left quarter of the frame at twice the size and the floor
took the ceiling's reflections (`build/rt5/ssaa_reflection_picture.png`). The
history is now at the scene's resolution for the traced form, and the composite
runs before the SSAA resolve. TAA renders pixel-identical to before the fix,
parked and dollying (`build/rt5/ssaa_fixed_sheet.png`).

**A harness trap that read as engine nondeterminism.** `stage_run.restore()`
copied back three named shaders; R11's arms staged `reflection_resolve`, which
stayed modified for every later run, and two shipped runs a sequence apart
differed by up to 233 levels. `restore()` now also restores every shader a
variant names and any staged file that differs from its source; shipped runs
are identical whatever runs between them (`rt4_nondet.py`).

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

### RT-6.9, RT-6.10, RT-6.11 — ✅ done 2026-09-07. **The RT-6.x series is closed.**

**RT-6.11 -- Catmull-Rom history, and the expired negative result was worth
re-opening.** It was built once, measured flat and reverted, with the reason
recorded: *"at this scene's speed the neighbourhood clip is discarding the
history the sharper kernel would have preserved, so the kernel has nothing to
do."* RT-6 taught the resolve to keep history the clip used to throw away and
RT-6.8 narrowed the box to one surface, so the premise was gone.

| region | detail off → on | change off → on |
|---|---|---|
| **wall** | 11.815 → **13.556** (+14.74%) | +6.91% |
| **floor** | 8.464 → **9.315** (+10.05%) | +7.18% |
| poles | 9.977 → **10.860** (+8.84%) | +6.87% |
| car | 8.865 → 9.536 (+7.57%) | +8.44% |
| ceiling | 10.520 → 11.173 (+6.21%) | +2.52% |

**Detail up between six and fifteen per cent on every region**, and rising faster
than frame-to-frame change everywhere but the car -- **the largest sharpness
change measured all day**, several times RT-6.8's. The crop
(`build/garage_burst/rt611_crop.png`) shows the chrome poles' highlights tighter
and the tube ends crisper, with no ringing halo. Applied only where the
reprojected texel itself matched: a nine-tap kernel across a silhouette is the
blend RT-6's surface test just refused, four times wider, so a neighbour-served
history stays point-sampled. Floored at zero, because Catmull-Rom's negative
lobes across a bright edge make a negative radiance the tone curve renders as a
black rim. **Cost about +0.02 ms on the resolve** (0.2805 against 0.2575, A,B,B,A;
the frame numbers straddle).

**RT-6.10 -- the accumulator validates what the ray *hit*.** The one hole RT-6.3
cannot close by construction: a static polished wall, a static camera, a moving
object in the reflection. Every reflector test passes and the direction test
passes too, because the eye did not move. The virtual image distance -- how far
behind the surface the reflected picture sits -- **was already being computed
every frame and *smoothed* into the history rather than compared**, so a moving
reflected object dragged the stored distance along instead of being noticed. The
fresh value is now kept before that blend and tested, with a tolerance scaled by
roughness: a mirror's hit distance is steady so a few per cent means the scene
changed, while a rough lobe's swings between frames for no reason but the
sampling.

Live: 0.068 levels mean, 0.63% of pixels beyond two. **75.7% of its effect lands
on the floor -- a 4.43x concentration** -- and nothing else is above 0.39x, which
is right: the wet floor is the smooth surface whose reflected content actually
moves under a dolly. Region detail and change do not shift (±0.08%), the same as
RT-6.5's metallic and for the same reason -- it fires on a small share of pixels.

**RT-6.9 -- a camera cut throws every history away.** Nothing detected a
discontinuity in the camera; reprojecting across a teleport or a scene load
produces a frame of smear that then takes thirty frames to fade. A **speed**
rather than a distance, because a dolly and a jump cover the same ground given
enough frames: over **100 m/s** (360 km/h) or **45 degrees in one frame** drops
the colour, guide, reflection, direct, GI, occlusion and budget histories. The
exposure is deliberately left alone -- a cut into a brighter room should adapt
rather than snap, and how fast is a look decision. `CameraMotion` gained a
`Forward` beside RT-6.3's `Eye`, since a previous facing is not recoverable from
a view-projection without inverting it.

**Proven live by lowering its own threshold**, which is the only honest way to
test a branch that should never fire in a normal scene: at 0.5 m/s the garage's
1.5 m/s dolly trips it every frame and the picture moves **16.8 levels across
61% of pixels** -- every history dropped, exactly as designed. Restored to 100
m/s the same capture is **bit-identical** to the pre-change build, so there are
no false positives on ordinary motion.

### RT-6.5, the fourth check — ✅ **built 2026-09-07** (owner-instructed, after it was wrongly argued away)

**It should not have needed instructing twice.** The first pass at RT-6.5 shipped
the metallic and *argued* the object id away instead of building it, on the
reasoning that two patches agreeing on position, facing and shininess reflect the
same image. **That ignored the tolerance.** The plane test accepts anything within
`0.05 + 0.01 * eyeDistance` metres -- **25 cm at twenty metres**. Two different
flat objects 20 cm apart, same facing, same material, pass every other check and
reflect **different** images because they sit at different depths. Nothing else
here can see that.

**What was built.** The G-buffer's object id is bound into the accumulator, kept
per texel in a **fifth attachment** on the reflection history, and compared in
`HistoryAt`. The penalty is **shaped by how marginal the plane test was**: a
different object that is genuinely coplanar keeps its history -- there the old
argument does hold -- while one out near the tolerance edge drops to
`kIdAgree = 0.25` of its memory, because that is the case the plane test was
never tight enough to catch. Multiplied into RT-6.4's match confidence like every
other term.

**Measured, and the honest result is that this scene barely exercises it.**

| arm | vs the test neutralised in-place |
|---|---|
| shipped (soft, plane-weighted) | mean 0.000, max **0.7** |
| probe: **hard refusal** on any id mismatch | mean 0.012, max 33.3, **0.05% of pixels** |

The probe is the liveness proof -- the plumbing is connected, and even at maximum
severity only **one pixel in two thousand** carries a history from a different
object that passed all the other tests. The garage is a room of large coplanar
surfaces; a scene of many small separate objects at similar depths is where this
earns its place.

**The cost, which is the real question.** `scene/ReflectionAccumulate` reads
**0.380 ms against 0.3155** before the attachment (different builds, same
machine, so treat it as about **+0.06 ms**), plus **16 bytes a pixel** on a
history budget RT-14 measured at 128. So: a fifth of that pass, and an eighth of
the history budget, for a test that moves 0.05% of pixels here.

**Shipped because the owner asked for it, and the trade is now measured rather
than argued.** If the numbers say remove it, the removal is the same five files.
What is not in question any more is whether it works: it does, and the reason it
looks quiet is the scene rather than the code.

**The method note worth keeping.** Both arms were produced by neutralising the
test *inside the current shader*, so they shared one binary and one descriptor
layout. Staging an older shader against a newer binary -- which is what was done
an hour earlier while debugging the metal tint -- produces a broken arm, not a
reference, and nearly buried a working fix.

### 2026-09-07: **a light at zero intensity stops participating**, and the studio rig follows the car

**Two owner-asked fixes, from reading the garage's light list.**

**Nothing filtered on intensity, anywhere.** Not when the scene collected lights,
not when they were binned into clusters. A light at zero was uploaded, occupied
cluster cells across its whole `Range`, and was walked and scored by **every
fragment** -- always losing the reservoir draw, because its contribution is zero.
It cost the scan and bought nothing.

| garage, 1600x900 | before | after |
|---|---|---|
| lights in the scene | 30 | **24** |
| lights per fragment | 26.4 avg, 29 max | **22.1 avg, 23 max** |
| at traced hits | 29.9 avg | **23.9 avg** |

Six of the seven the scene file writes as zero. The seventh is non-zero at
runtime -- `ShowroomMode` and `ShowroomLights` set intensities from script, so
**the filter reads the live value and not the file's**, which is the right way
round and was worth confirming rather than assuming.

**The picture moves slightly, and it is noise rather than bias.** 0.212 levels
mean absolute -- but **+0.044 signed on a frame mean of 44**, which is +0.1%,
with 8.7% of pixels brighter against 6.4% darker and the floor and wall signed
at −0.001 and −0.012. The direct pass draws K lights a pixel by weighted
reservoir, so shortening the candidate list changes *which* lights are drawn
even where the removed ones had zero weight: the estimator lands differently,
with the same expectation. Checking the *signed* mean is what separates that
from a real shift, and mean-absolute alone would not have.

**Safe for the bake, which was the one thing it could break.** `CollectLights()`
also feeds `LightingHash()`, and a changed hash invalidates the baked field --
1283 frames for this scene. It does not change: the hash already skips Realtime
lights, every zero-intensity light here is Realtime, and every baked light here
is non-zero. A *baked* light at zero would change the hash, and should, since a
light contributing nothing is not part of the lighting the hash names.

**And the studio rig follows the car.** `Key Light`, `Kicker Left` and `Kicker
Right` are a three-point product rig -- a key above and in front, two rim lights
on the flanks -- built centred on **x = 0**. The car moved to **x = −4.3** this
morning and the rig did not, which left **`Kicker Right` 7.7 m from a car it has
a range of 7 to reach**: lighting nothing. The key was 4.3 m off centre and the
left kicker had crossed to the car's inboard side. All three take the same −2 m
the car, the headlamps and the tail lights already took. It was flagged when the
car moved and then not done, which is how it survived a day.

### ✅ Fixed 2026-09-07: **coloured metals reflected in grey**

**The defect, which the engine's own comment admitted.** The traced reflection
reaches the frame through a **single number** in the scene's alpha, and the lit
shader made it by collapsing a colour to its brightness:

```glsl
// One channel, so a coloured metal's tint is not carried.
reflectionWeight = reflectionShare * dot((F0 * envBRDF.x + envBRDF.y) * occlusion, luma);
```

`F0` is what a surface reflects head-on and **for a metal it is the metal's own
colour** -- gold's F0 is gold. Collapsed to a luminance, it was thrown away, so
every coloured metal reflected the room in **grey**. The probe's half kept its
tint (that multiply is a colour, one line above), which is why the two halves
disagreed quietly for so long.

**The fix: the trace applies the tint's hue, and nothing else.**
`reflection_trace` already reads the surface, rebuilds the world point and
includes `pbr_fragment.glsl`, so it needed one binding -- the albedo lane -- to
build the same `F0` and multiply by `reflectance / luminance(reflectance)`: **a
tint whose brightness is exactly one.** The lit shader keeps its weight
untouched, so the amount of reflection is bit-for-bit what it was and only the
colour moves. Measured: frame mean **−0.08%**, regions 0.005 to 0.268 levels.

**Two wrong turns, both worth keeping.**

1. *The full-colour version.* Multiplying by the raw `reflectance` and reducing
   the lit shader's weight to its scalar part is algebraically the same and
   physically cleaner. It measured **four times brighter on the garage floor**
   and was reverted -- then the revert was itself wrong (below). Re-measured
   honestly it is about **7% darker** than the baseline, which is the
   colour-versus-luminance difference plus the two sites evaluating `envBRDF`
   from different normals and roughnesses. **Left for later**, because the
   hue-only fix removes the defect without depending on reconciling them.
2. **The four times was a broken baseline, not a broken fix, and that is the
   lesson.** The "before" arm staged the *old* trace shader against a **new
   binary** that binds a fourth texture the old shader never declares. That arm
   read floor 16/21/26 and frame mean 26.4 where every committed-state capture
   reads 68/90/95 and 43.6 -- it was not a reference at all. **Staging a shader
   whose bindings do not match the binary does not give you the old behaviour;
   it gives you nonsense.** The A/B that settled this instead neutralised the
   tint *inside the current shader*, so both arms shared one binary and one
   descriptor layout.

**Live but nearly invisible here, and the reason is the scene.** The garage's
metals are chrome (neutral albedo, so a neutral tint) and the car's clearcoat
(mostly probe-lit); its floor and walls are dielectrics, whose F0 is grey by
definition. A scene with gold, copper or brass is what would show it. Shipped
anyway: it is the right physics, it costs one texture fetch in a pass already
90% pixel-bound, and it is not a thing to rediscover later.

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


**The other half -- the object id. Nothing was built, and here is why.**

**In plain words.** This pass takes each pixel that shows a reflection and
reuses last frame's answer for it, so the reflection is not rebuilt from
scratch every frame and does not boil with noise. Before reusing, it asks: *is
this the same mirror as last frame?* Today it checks three things -- is the
surface in the same place, does it face the same way, is it about as shiny --
and RT-6.5 added a fourth, is it metal or not.

The plan was to also check *is this the same object?* Every object carries a
number, so the two numbers could be compared.

**It would not help, and it would hurt.** What this pass remembers is **the
picture in the mirror**, not the mirror itself. What shows up in a mirror
depends on exactly three things: where the mirror is, which way it faces, and
how shiny it is. Nothing else. So if two patches of surface agree on all of
those, they show **the same reflection** -- even when they belong to two
different objects, like two panels of the same wall, or a floor tile and the
tile beside it. Comparing object numbers would throw away an answer that was
correct, and the pass would fall back to a noisier one.

**The same check is right elsewhere, which is what made it look right here.**
TAA -- the anti-aliasing filter, RT-6 -- does need the object number, because
there the pixel's colour is *the object's own* colour and lighting. Two objects
that happen to line up in space still look different, so identity matters. Here
it does not. One check, correct in one pass and wrong in the other.

**What is actually still missing.** Take gold and chrome, polished to exactly
the same degree. Both are metal, so the new metal check passes. Both are
equally shiny, so the shininess check passes. They sit in the same plane facing
the same way, so those pass too. And yet **gold tints everything it reflects
yellow and chrome does not** -- the two show different pictures, and this pass
cannot currently tell them apart. That is the real remaining gap, and it is
about *the colour a metal casts on its reflection*, not about which object it
is. It is also what the second reviewer actually asked for: they wrote "a
compact BSDF identity/hash", meaning a short summary of how a surface reflects
light, rather than a list of separate comparisons. Shininess and metal-or-not
are most of that summary; the tint colour is the part still absent. The engine
already stores it -- for a metal, the tint is the surface's own colour, which
the G-buffer's colour lane holds.

**What not building it saves.** There is no spare room left in what this pass
stores -- all four of its storage slots are full -- so an object number would
need a fifth. RT-14 measured what that costs: 16 bytes for every pixel on
screen, added to a per-pixel budget already at 128, feeding a pass whose cost
grows directly with pixel count. Not free, and spent on a check that would make
the picture slightly worse.

**Honest limit: this half is reasoning, not a measurement**, and is marked so.
To *measure* that the object check helps nothing, the object number would have
to be plumbed into this pass and stored per pixel -- which is the same work as
simply building it. So the choice was between an argument and paying the cost to
disprove the argument. If the number is wanted rather than the reasoning, that
is what it costs.

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
