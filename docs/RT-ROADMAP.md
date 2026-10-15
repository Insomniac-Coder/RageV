# RT roadmap

*The single RT roadmap, 2026-09-25. It replaces RT-SERIES-2 (`docs/RT-SERIES-2.md`, kept in docs/ as history, unchanged) and carries every item, ruling, budget line, finding and audit row that document had. Why a new document: RT-SERIES-2 was built from RageV's own code reviews. On 2026-09-25 every area of the renderer was compared with what the top engines ship, in three phases: a survey of the shipped engines and the research frontier; a comparison of each topic against RageV's code, with a verdict per RT2 item; and a verification that re-checked every claim against the code and the primary sources (four talk texts are kept in `build/rt_research/sources/`). The owner then answered the 19 questions the comparison raised (rulings D12 to D30, section 5). This roadmap is what came out: one plan in which every design choice is decided. The only open choices left are the owner's own live reviews, each named in its item, and measured arms whose adopt-or-reject rule is written as a gate. The comparison's verdicts are in section 11; the full evidence (sources, code citations, the verification's verdicts, the research gaps) is in `docs/RT-ROADMAP-RESEARCH.md`. The evidence behind the older finding and issue ids (file and line, the skeptic's verdict) is still `docs/RT-SERIES-2-FINDINGS.md`. RT-24's work is uncommitted (RT2-0's progress note lists it). Section 10 places every fixed value the two hard-coded-values audits found.*

## The short version

- **Why this replaces RT-SERIES-2.** RT-SERIES-2 planned the work from RageV's own findings. The comparison with the top engines (2026-09-25) showed where RageV is behind what they ship, where it is level, and where the plan had left a choice open. It also checked RageV's own "we are better" claims against the sources: none survived in full (section 11). So the plan now follows the owner's first standing rule: build what the top engines ship as the baseline, and adopt anything better only when it measures better against that baseline in RageV's own scenes.
- **RT-24 still comes first** (RT2-0). What is left is attribution: one switch at a time at the owner's swing speed, to find where the remaining speckle and edge jitter come from. They are now at the level of the 16-ray reference. If the leftover comes from outside the reflections, RT-24 is committed and the leftover is written under the item that fixes it (the owner's ruling D17). Nothing else starts until RT-24 is committed.
- **Next: trustworthy instruments, correct inputs and early wins** (RT2-1 to RT2-9, milestones M0 and M1).
  - Failures become loud, the engine reports its VRAM, and there is a validation-clean gate.
  - A reference renderer that does not share the filters it judges, now including tinted glass, water, fog along every ray and lit particles. A harness that renders every test camera and diffs it against a stored picture. A "signal stands alone" command that proves a signal converges with TAA off. New test fixtures (white-furnace spheres, a normal-mapped plane at 10 km, glass over glass).
  - Object ids become exact and stable, in integer lanes.
  - Random numbers move in every anti-aliasing mode, then become blue noise (the same kind MegaLights uses).
  - Cheap fixes land early: the flicker floor that repairs the bridge water, the shader and pipeline caches, the TLAS build skipped when nothing changed, and two new picture fixes (window glass hiding the car interior by brightness; fog stopping at the far clip for the sky).
- **Then one light model** (M2): one BSDF for every path first (RT2-21b), because the area-light tables depend on it; one light library with area lights shaded as areas; glowing meshes as light sources, with an optional field on a Light naming its glowing mesh so the fixture's shine is counted once (D13); lamp shells no longer slowing every ray (RT2-46); one light tree (a light BVH) serving every ray hit, measured against the camera-centred cascaded light grid Doom: The Dark Ages ships; and primary direct light guided by last frame's visible lights (RT2-12b), with the owner's 2x2 design as the cost tier if full resolution is too slow (D14).
- **Then a real frame** (M3): three measurements settle compute against fragment passes; the render graph grows barriers, memory sharing and compute resources in place; views own their histories; only AO moves onto the signal tables now (the others move in their own rewrites); bindless binding; a memory budget; compute ports of the passes that survive, and compacted ray lists.
- **Then the signals rebuilt** (M4):
  - a designed G-buffer that carries the tangent direction for brushed metal (D27);
  - one compute composition pass, with the raster drawing kept only when MSAA is selected (D26);
  - reflection denoiser v2 built on NRD's design, with NVIDIA's own NRD denoiser as a comparison arm (D15);
  - direct light on the MegaLights-shaped denoiser first, then visibility reconstructed apart from shading as a measured arm;
  - one shared rule set that lets every diffuse signal stand alone without TAA (RT2-47);
  - bounce light: the bake for baked lights plus a live pass that, at High quality, runs in every view and also reads the bake where its rays land (D18), within 2.0 ms and aiming for 1.5 (D20);
  - a sparse, automatically placed bake that flags which objects made it stale;
  - a lighting cache for particles and see-through surfaces (RT2-48);
  - reflections whose ray rate follows the lobe's footprint;
  - then deterministic hit shading on mirror-like surfaces (RT2-23f) and one specular signal that counts each lamp once (RT2-23g), split from RT2-23 because they need the ray-rate tiers, the hit-shading contract and the direct light's new denoiser first;
  - a final temporal pass that only anti-aliases, with the reflection added after it (D23).
- **Then glass and water inside the ray-traced world** (M5): tinted shadows in one ray query, the nearest see-through surface composited exactly, refraction in screen space with a traced fallback, the panes behind the nearest on one fixed mirror ray each (D28, which ends the last TAA exception), the sea's white shine reached only through physical causes (D29), FFT waves as an arm the owner judges (D30), fog along every ray, and TAA no longer forced.
- **Then scale** (M6), with the TLAS policy decided, the artists' own model structure kept (D25), and tiny instances culled only inside foliage and debris sets (D24).
- **Last, the fallback and the post chain** (M7), with the tone curve and the bloom form picked by the owner's eye (D21, D22).
- **Size.** 53 items (the 46 of RT-SERIES-2; RT2-12b, RT2-21b, RT2-23f and RT2-23g split off; RT2-46 to RT2-48 new), roughly 276 to 360 working sessions, plus 1-2 if RT2-12b's 2x2 tier is needed (a session is about one solo working day). The growth over RT-SERIES-2's 235 to 303 is the work the comparison found missing: hit shading, one specular signal, the NRD arm, the shared diffuse rules, the translucency cache, the bake gathered at hits, the BSDF, the tangent lane, the guided direct sampling, and the cascaded light grid Doom ships, built as the light BVH's baseline arm. Every milestone leaves the engine working. A new path lands behind a global setting or a measurement flag, is proven against the old path, and the old path is then deleted in a named step.
- **Target.** Unchanged: 60 FPS at the laptop's native 2560x1600 (D8), with about 2 ms kept free for the GPU's drift. The live bounce pass gets 2.0 ms, aiming for 1.5 (D20); the extra time comes out of other passes' shares (section 6). Every per-pass number is a proposal until the item's first arm measures it.
- **Owner decisions.** All 31 are answered: D0 to D11 on 2026-09-24, D12 to D30 on 2026-09-25 (section 5). D3, D4, D10 and D11 are updated by the new rulings. No question is left open except the live reviews each item names.

The working protocol stays as it is: one item, or one named step, per green signal; a report after each; one arm at a time for anything the owner judges live; solo coding (RT-FIRST §4, answer 6). A live review named in an item is a step of the plan, not a licence to open a window: each one is asked for first. Costs are measured and recorded as each item lands; the dedicated optimisation work stays inside this series (D0). No third-party type appears in a public engine header (the owner's API-wrapping rule): Jolt's job interface, VMA's budget types, the NRD, DLSS, FSR and opacity-micromap SDKs all stay inside private implementation files. An item is committed only when it is complete.

The owner's standing rules, which every item obeys (an item names the rule when it decides something):
1. **Top-dog baseline, then better.** Build what the top engines ship as the baseline; a better, or equally good and more performant, technique is adopted only when it measures so against that baseline.
2. **General solutions only.** Nothing tuned to a scene, a camera speed, a frame rate, a world scale in metres, a brightness, a surface type or one past measurement.
3. **No reliance on TAA.** Every lighting signal has its own accumulator; the final temporal pass does anti-aliasing only.
4. **Measure cost during RT work; optimise in the dedicated pass.** Costs are recorded at every step. By D0 this series is also that dedicated speed pass, so an optimisation lands as its own named item, step or arm (RT2-20's experiments, for example), measured, and never folded silently into a picture change.
5. **Never change the scene or its materials to fix an engine problem.**
6. **Diff images, not mean levels.** Render comparisons are judged by per-pixel diff images; a look the owner judges is shown live, one arm at a time.

## Words this document uses

Each term is defined here, the first time it appears.

- **G-buffer**: a per-pixel record of the visible surface (depth, normal, roughness, colour, identity, motion), written before any lighting.
- **Signal**: one noisy per-pixel lighting estimate that gets its own filtering, such as direct light, reflections, GI (bounce light) or AO (ambient occlusion, the darkening of creases).
- **Accumulator / history**: a pass that averages a signal over past frames. The texture holding the past frames is the history.
- **Reprojection**: finding where this pixel's surface, or its reflection, was last frame, so its history can be read there.
- **Virtual image**: the point a reflection appears to sit at behind the mirror. A reflection moves with its virtual image, not with the surface.
- **TAA (temporal anti-aliasing)**: the frame filter that blends each frame with earlier ones. Each frame the camera is shifted by a tiny sub-pixel amount, the **jitter**, so edges average out.
- **Measured change**: RageV's anti-lag. It re-shades a sparse sample of pixels using last frame's random numbers, so any difference is real change, and shortens memories where it finds some.
- **NEE (next-event estimation)**: at a ray hit, aiming a ray at a chosen light instead of hoping a random ray finds it.
- **MIS (multiple importance sampling)**: a weighting that combines two ways of sampling the same light so the light is counted once.
- **Resampling / reservoir**: picking a few lights out of many by their estimated contribution, and weighting each pick so the average stays correct.
- **Light BVH**: a tree of lights with bounds on their power and direction, walked per pixel to pick lights in logarithmic time.
- **ReGIR**: a world-space grid where each cell holds a few light samples drawn fresh each frame, so every pixel picks from a fixed-size set.
- **Radiance cache**: light stored in world-space cells, so a ray hit can read multi-bounce light in one lookup instead of shading everything again.
- **Spherical harmonics (SH)**: a few numbers per cell that describe how light varies with direction.
- **BLAS / TLAS**: the bottom-level acceleration structure (one mesh's ray-search tree) and the top-level one (the tree of all placed meshes).
- **Refit / rebuild / compaction**: updating a tree's boxes in place; re-sorting it from scratch; copying a finished tree into the smaller size the driver reports afterwards.
- **Render graph**: the frame's list of passes, with what each pass reads and writes.
- **Barrier**: a GPU command that makes one piece of work finish before the next touches the same memory.
- **Aliasing (of memory)**: letting two short-lived targets share one piece of memory, because they are never alive at the same time.
- **Descriptor set / bindless**: a small table telling a shader which textures and buffers to read / one big table that every shader indexes itself.
- **Push constants**: a few bytes handed straight to a shader with each draw or dispatch.
- **Timeline semaphore**: a GPU counter that other queues and the CPU can wait on without stalling.
- **Async compute**: a second GPU queue that runs compute work at the same time as the graphics queue.
- **Compute pass / fragment pass**: a GPU program run over a grid of threads / over the pixels a drawn triangle covers. Only compute has on-chip shared memory and a dispatch size the GPU can decide.
- **SER (shader execution reordering)**: hardware regrouping of ray-hit threads so that similar shading runs together.
- **OMM (opacity micromap)**: a small per-triangle opacity map stored in the ray structure, so the hardware settles cutout transparency without running shader code.
- **LOD (level of detail)**: simpler versions of a mesh, used at a distance.
- **Render scale / internal resolution**: rendering and tracing fewer pixels than the display has, then rebuilding the display resolution from them.
- **TAAU (temporal upscaling)**: a TAA that outputs a higher resolution than the one it renders at.
- **WBOIT**: weighted blended order-independent transparency, a way to blend see-through layers without sorting them.
- **Ray cone**: an estimate of how wide a ray's footprint has become, used to pick a texture's mip level (a pre-shrunk copy) at a hit.
- **Fast history**: a second, short history of a few frames that bounds the long one, so an old image cannot outlive the scene.
- **History fix**: a spatial fill for pixels whose history has just restarted, fading out as the history builds up.
- **Baked light / Realtime light**: a light whose bounce (and, for Full and Hybrid bakes, its far direct light) is stored in the bake, against one lit entirely live. Only Realtime lights can move or be switched without a re-bake.
- **Half float (RGBA16F)**: a 16-bit float. This GPU rounds writes to it toward zero, and above 2,048 it can no longer hold every whole number.
- **BSDF**: the maths for how a surface reflects and transmits light.
- **A,B,B,A palindrome**: timing two builds in the order A, B, B, A, so the laptop's drift cancels out.
- **Froxel**: a small box in a 3D grid aligned to the camera, used to light fog.

Added with the comparison (2026-09-25):

- **The top engines**: the engines and SDKs the comparison checked: Unreal Engine 5 (Lumen, MegaLights, TSR), id Tech 7 and 8 (Indiana Jones, Doom: The Dark Ages), Snowdrop (Avatar), Anvil (Assassin's Creed Shadows), Frostbite (Battlefield V), Northlight (Alan Wake 2), REDengine (Cyberpunk 2077), Unity HDRP, and the public SDKs from NVIDIA (NRD, RTXDI, SHaRC) and AMD (FidelityFX).
- **Baseline / arm**: the baseline is the way the top engines ship something, built first; an arm is an alternative built behind a switch and measured against it.
- **Gate**: an item's measurable exit condition. **Live review**: a look the owner judges in a running window; each one is asked for before the window opens.
- **DirectTrace**: the pass that traces direct light: it picks K lamps per pixel and casts one shadow ray to each. **K**: the number of lamps picked per pixel each frame (8 at the Quality preset).
- **HalfBake, FullBake, HybridFullBake, Realtime**: a light's mobility. HalfBake: live direct light, baked bounce. FullBake: both baked. HybridFullBake: live near the lamp, baked beyond a radius. Realtime: everything live.
- **The bake**: the stored field of light, a few spherical-harmonic numbers per cell, solved ahead of time for baked lights. **Stored light**: light already computed and kept (the bake, a cache), read by a ray instead of lighting its hit again.
- **pdf**: how likely a sampler was to pick a given direction or point; an estimate divides by it. **Power heuristic**: the standard MIS weighting; each sampler's share comes from how likely it was to find that light, and the shares sum to one.
- **Emitter**: a glowing surface the engine aims rays at. **Emitter cluster**: a group of a mesh's emissive triangles, with its bounds, its facing cone and its power.
- **Representative point**: shading a sphere or tube light at the one point on it nearest the reflected direction, with an energy correction. **LTC** (linearly transformed cosines): a fitted table that shades an area light's shape exactly, with no noise.
- **VNDF sampling**: drawing reflection directions only from the part of the roughness lobe the viewer can see. The **bounded** form wastes fewer draws below the surface.
- **Split-sum / EnvBRDF / DFG table**: the precomputed table saying how much light a surface reflects in total, for its roughness and the viewing angle.
- **Smith term**: the part of the BRDF that accounts for tiny facets hiding each other; the **height-correlated** form is the accurate one. **Energy compensation**: a factor that puts back the light a rough metal loses in the single-bounce GGX model.
- **Demodulation**: dividing a signal by the surface's own colour before denoising and multiplying it back afterwards, so texture detail stays out of the filter.
- **Geometric specular AA**: widening roughness by how much the normal changes inside one pixel, so tiny highlights do not flicker. **Toksvig**: the same idea for normal-map detail, baked into the roughness mips.
- **Temporal gradient**: another name for measured change (the A-SVGF method); NRD documents it as its history-confidence input.
- **Variance clip**: clamping the history to the neighbourhood's mean plus or minus a number of standard deviations, in YCoCg (a colour space that separates brightness from colour).
- **Blue noise (STBN)**: random numbers arranged so that neighbouring pixels and frames differ as much as possible, which averages out faster than plain random numbers.
- **NRD (ReBLUR, ReLAX, SIGMA)**: NVIDIA's open-source denoisers, which run on Vulkan and any GPU. **MegaLights**: Unreal's stochastic direct lighting (2025). **ReSTIR / RTXDI**: reuse of light samples across pixels and frames / NVIDIA's SDK for it.
- **Spatial hash**: a cache addressed by a hash of the cell's position, so only the cells rays actually use exist. **DDGI**: grids of probes updated by rays. **Sparse bricks**: blocks of 4x4x4 bake cells placed only near geometry.
- **Clipmap**: nested grids around the camera, each with twice the cell size of the one inside it.
- **Footprint**: the size of what one pixel covers on a surface, or what one ray covers where it lands.
- **Russian roulette**: skipping some work with probability p and dividing what is kept by (1 - p), which keeps the average right.
- **Partitioned TLAS**: a TLAS rebuilt piece by piece; NVIDIA-only on Vulkan today.
- **AVBOIT / MBOIT**: order-independent transparency that integrates transmittance per pixel (Call of Duty / Alan Wake 2). **Exact front-layer composite**: the nearest see-through surface drawn exactly over what lies behind it.
- **PSTR**: primary surface replacement, which swaps the surface seen through glass into the G-buffer (RTX Remix).
- **FFT waves**: ocean waves made from a wave spectrum by a fast Fourier transform, thousands of waves at once. **Gerstner sum**: today's sea, a sum of 32 waves.
- **Reactive mask**: a per-pixel input telling the final temporal pass that a pixel's history is unreliable (particles, transparent surfaces).
- **2.5D motion**: a pixel's screen motion plus how its view depth changed since last frame.
- **Exposure-scaled**: multiplied by the frame's exposure, so that a weight means the same in a dim scene and a bright one.

The words the exit gates measure with (added 2026-09-25, after the second critic's review), each tied to its instrument:

- **Parked**: the camera and the scene both still.
- **Spread**: one standard deviation of a value over time, over its neighbours, or over repeated timing runs (the A,B,B,A spread).
- **Parked noise floor**: `parked_stats.py`'s frame-to-frame spread on a parked camera, measured on the same build as the thing it judges.
- **Display level**: one 8-bit output step, after exposure and the tone curve.
- **Within grain**: the per-pixel diff image against the reference shows no spatial structure (no 16x16 tile whose mean difference is above the parked noise floor), and its per-region magnitude is at or below the parked noise floor measured on the same build.
- **Near zero**: within grain, and the per-region mean difference below one display level.
- **Grain**, used alone in a gate ("no structure beyond grain", "off by more than grain"): the parked noise floor. (spin_measure's "grain score" is a different thing: its own per-spot spread across a swing.)
- **Young pixel**: a pixel whose history is only a few frames long (just revealed, just cut, or just changed).

Other terms that appear in the items:

- **Texel**: one cell of a signal's own grid (which may be coarser or finer than the screen's pixels).
- **Lobe**: the cone of directions a surface reflects light into: narrow on a mirror, wide on a rough surface.
- **Disocclusion**: a pixel whose surface was hidden last frame, so it has no history.
- **SVGF / a-trous**: multi-pass, edge-aware spatial filters (each pass samples neighbours at a wider spacing).
- **Alias table**: a lookup that picks an item in proportion to its weight in one step.
- **Octahedral encoding**: a normal packed into two numbers.
- **Catmull-Rom**: a sharp 4x4 interpolation filter, used to fetch a history between texels.
- **CellPixels**: the realtime cache's cell size, in pixel footprints at the cell's distance from the camera (RT2-25c).

## 1. In plain words: where the engine stands

**The engine has been RT-first since 2026-09-06.** RT-series 1 has closed or dropped every item except RT-22, RT-2.2 (deliberately last) and RT-24 (filed 2026-09-23). The frame today runs in this order:

1. A G-buffer raster of every opaque kind of surface.
2. Separate passes that trace direct light (DirectTrace), AO, GI and reflections. GI is traced only where no bake matches, which in practice means the camp. Each signal is averaged by the shared accumulator, with measured change.
3. A second full copy of the reflection and direct passes for the nearest glass pane.
4. The forward lit pass, which rasterises the scene again and combines everything.
5. The sea and the other transparent surfaces, drawn forward with their own rays.
6. The reflection added on top, then TAA, which is forced on whenever rays are on.
7. Post-processing.

The frame measured on this laptop (fresh benchmark: 3 runs each, validation off, on mains power):

| Shot (1600x900, rays on, the project's settings) | Frame (mean) | Where the time goes (GPU ms) |
|---|---|---|
| Garage | 18.25 ms | ReflectionResolve 3.91, ReflectionTrace 3.82, DirectTrace 3.18, lit pass 1.29, reflection accumulate plus three blurs 2.91 |
| Bridge, Headland | 17.14 ms | sea forward shading (Transparent) 5.58, lit pass 2.28, ReflectionTrace 1.47, the sea's direct passes 1.67 |
| Bridge, Glitter | 13.21 ms | Transparent 3.32, lit pass 1.65, DirectWaterShade 1.06, ReflectionTrace 1.05 |
| Camp | 7.60 ms | GI trace 1.22, lit pass 0.60, ReflectionTrace 0.59, DirectTrace 0.59 |
| Scale scene, 60,000 objects, rays on (1280x720, one run) | 40.76 ms, limited by the CPU | CPU: shadow phase 19.98 (what inside it costs the time is not profiled yet), G-buffer recording 11.53; GPU work 7.54 |

These carry over:
- the G-buffer-first shape;
- measured change;
- the reconstruction contract as an idea (accumulate each signal on its own grid, guided downsample, one joint upsample);
- the half-float rounding fix;
- NEE with MIS at reflection hits;
- the shape of RT-9's tile allocation;
- the GPU crash diagnostics;
- above all, the measurement discipline: reference arms, diff images, palindromes.

The comparison of 2026-09-25 placed these carry-overs honestly (section 11). Measured change is the same method NRD documents as its history-confidence input and Quake II RTX shipped, so it is level with the shipped practice, not ahead of it. NEE with MIS at reflection hits is ahead of Lumen 2022 and Snowdrop but still aims at rectangle stand-ins for the lamps, so it is behind the RTXDI-class path tracers until RT2-11 samples the real triangles. The half-float rounding fix is the same idea MegaLights ships. (Whether the measurement discipline has a published equal among the engines checked was not verified by the comparison, so no claim is made.)

Since the benchmark above, RT-24 added two costs to the garage's reflections that have not yet been benchmarked together: the 8-neighbour lamp pool (+2.1 ms at 1600x900) and the one-pass pick among every emitter (about +0.5 ms). The garage's reflection chain is therefore about 12.7 ms at 1600x900 today, about 36 ms at native (inferred).

**Out-of-date facts** (each checked in the working tree):
- The GPU-driven lit path (`--gpu-lit`) is **on** by default, and its defect was fixed on 2026-08-24 (`EngineConfig.h:990`; HANDOFF.md:6549). Only its help text (`EngineConfig.h:51`) still says "unfinished, off".
- The transform walk is no longer the CPU wall. Since roadmap 8.15 it takes 2.6 ms at 60,000 objects. With rays on, the wall is the shadow phase: about 20 ms of CPU at 60,000 objects. Reading the code points at the ray-tracing instance work, but that is inferred, not profiled. The phase also holds the draw-list rebuild and cull-table upload (3.07 ms with rays off), a per-instance hash for measured change (`RayShadows.cpp:443-466`), emitter extraction, the TLAS pack and the build recording. RT2-2's CPU zones split it before RT2-35 is designed.
- The TLAS is refit in place when the instance count has not changed, and rebuilt every 64 frames. It is not rebuilt from scratch each frame. What *is* rebuilt every frame is the CPU instance list and the hit table.
- Texel emitters were built and merged on 2026-08-24. They do nothing on the garage's real content.
- The project has run TAA since 2026-08-27, by the owner's own settings. `MsaaSamples: 4` is still stored, and with rays on the engine forces TAA regardless.

**Facts the comparison's verification corrected** (2026-09-25; each checked in the working tree, details in `docs/RT-ROADMAP-RESEARCH.md`):
- The TLAS is one per frame in flight, each with its own refit counter, so a slot is fully rebuilt only every 64 of its own builds (64 x frames in flight). Every build carries ALLOW_UPDATE. The measured-change content key is computed after the build, and any skinned mesh changes it every frame.
- The instance masks are used: the bake solve traces the static half only and the subtractive shadow the moving half only. `RT-SERIES-2-FINDINGS.md:4392` says the opposite and is wrong (GEO-16 at line 2949 is right).
- The project's RTGI High is up to 4 rays per traced pixel at width/2 x height/2: what RageV calls "half resolution" is what the industry calls quarter resolution, and the 4 is a per-tile ceiling under the ray budget.
- DirectTrace's K light picks, and not only its soft-shadow points, are frozen per pixel when TAA's jitter is zero, so under `--aa=none` no accumulator can converge them today.
- The traced reflection, the probe term and SSR read unfiltered roughness; only the analytic lights see the geometric-specular-AA roughness.
- Glass and water are not in the ray structure at all; the glass histories (168 bytes a pixel) are allocated even in scenes with no glass.
- The reflection is composited before TAA and TAA fetches it again by the surface's motion. The headers of `reflection_composite.rvshader` and `reflection_accumulate.rvshader` still say the opposite.
- `AutoExposure` never reads its SpeedDown setting, so exposure adapts at one speed in both directions.
- The forward lit pass still traces shadow rays itself (the subtractive shadow), from raster fragments.

### The seven structural problems

**P1. The temporal reconstruction gets wrong inputs and quietly depends on TAA.**
- *What is wrong.*
  - Every random draw in the traced shaders changes from frame to frame only while TAA's jitter is non-zero (`pbr_fragment.glsl:4729-4731`, `ray_shadow_trace.glsl:24-25`). So the engine forces TAA whenever rays are on (`FrameGraphBuilder.cpp:197-206`), and no signal can be tested without it.
  - The reflection history finds last frame's picture using a depth that is smoothed at the surface's *old* position and averaged with 10 km sky misses (`reflection_accumulate.rvshader:1502`, `reflection_trace.rvshader:826`). It also takes mirror curvature from the normal-mapped normal.
  - When only the camera moves, almost nothing checks the reflected content, so the only lever left is keeping fewer frames: ghosts or grain.
  - Object ids sit in half-float history lanes, which are exact only up to 2,048 (`FrameGraphBuilder.cpp:1012-1017`). Objects drawn from the CPU list get a new id whenever culling changes.
  - RT-9's motion test compares last frame's camera with last frame's camera. The eye is written during execution (`Renderer3D.cpp:3900`, inside `Scene::OnRender`, which is a graph pass's body) and the signal's record is stamped at `7584`; both are read while the next frame is still being declared (`FrameGraphBuilder.cpp:2401-2403`). This is read from the code; RT2-0 confirms it with pass timings before changing it.
- *What it causes.*
  - RT-24's spread lives in TAA: turning its memory off gives a floor as sharp as the settled picture (HANDOFF.md:56-61).
  - RT-22's frame-filter rule raised the bridge's water glitter from 0.80% to 0.97% of pixels blinking, because it decides by where a pixel is.
  - At 60,000 objects only 10.1% of glossy pixels kept their reflection history (99.9% at 1,000). The refusal counters explain only 22.5% of the rest, because the id refusal has no counter lane (`reflection_accumulate.rvshader:1319-1323`). That is exactly what the half-float failure predicts.
- *Direction.* RT2-0, RT2-5, RT2-7, RT2-8, RT2-23, RT2-24, RT2-47, RT2-28.

**P2. Light is modelled differently in every path, and picking lights gets more expensive as lights are added.**
- *What is wrong.*
  - The same light maths is typed out eight times in three files, and the copies have drifted (direct-06).
  - At ray hits every lamp is a point with a hard shadow, so tubes show as pinpoints in reflections (`pbr_fragment.glsl:2971-3096`).
  - Only the first 16 glowing surfaces in scene order get aimed rays (`Renderer3D.h:76`), and each is approximated by its bounding rectangle.
  - A fixture made of a light plus a glowing mesh is counted twice in the bounce and in the bake (gi-s1).
  - Probes are captured before the ray structure exists, so every shadow ray in them answers "lit" (GI-04).
  - A surface is lit four different ways depending on what is looking at it (GI-05).
  - DirectTrace scores every light in the pixel's cluster cell (`direct_trace.rvshader:1171-1193`).
- *What it causes.*
  - The baked probe reads about a third brighter than the traced reflection (HANDOFF.md:35).
  - The garage has more tube bars than aim slots.
  - On the bridge a pixel sees 67 to 118 lights on average (190 at most), and the busiest cluster holds 144 to 150 of the 189 lights.
  - Every reflection hit walks all 24 lights in the garage, and 58 to 70 on the bridge.
- *Direction.* RT2-6, RT2-21b, RT2-10, RT2-11, RT2-46, RT2-12, RT2-12b, RT2-25.

**P3. Reconstruction and the sea cost more than the rays, and all of it is full-screen, per-pixel work at native resolution.**
- *What is wrong.*
  - Every ray and filter pass is a full-screen fragment pass at output resolution.
  - There is no internal render resolution and no upscaler (`RenderGraph.h:78-82`).
  - The reflection chain runs a 24-to-64-tap resolve and three 49-tap blurs on every pixel, settled or not.
  - The nearest glass pane runs a second full-resolution copy of that chain.
  - The sea is a parallel renderer of its own.
- *What it causes.*
  - In the garage, reflections take 10.64 of 18.2 ms (59%).
  - The resolve alone (3.91 ms) costs more than the trace (3.82 ms), and the three blurs (1.97 ms) cost more than the whole lit pass (1.29 ms).
  - The camp spends 2.46 ms on reflection passes for 0.14 M rays: a full-screen fixed cost.
  - At Headland the sea takes about 8.4 of 17.1 ms (49%).
  - RT-14 measured the traced passes as 90-99% linear in pixel count. Rendering fewer pixels would be the biggest lever, but the owner's target is native resolution (D8), so it stays a fallback setting, not part of the plan.
- *Direction.* RT2-14, RT2-20, RT2-22, RT2-27, RT2-29, RT2-31, RT2-32.

**P4. The frame is wired by hand, on a render graph that cannot schedule.**
- *What is wrong.*
  - One 5,521-line function describes every frame (`FrameGraphBuilder.cpp:385-5906`).
  - The render graph is an ordered list. It works out no barriers, shares no memory, and does not let compute passes write its images (`RenderGraph.h:38-46`, `RenderGraph.cpp:37-52`).
  - Renderer3D is 9,900 lines of static state.
  - The applications own 16 histories per view (32 in the editor) and pass them in through 20 named fields.
  - Declaring the frame changes state that execution depends on.
- *What it causes.*
  - Adding a signal touched 12 to 14 files (RT-13, RT-9, RT-22).
  - Wiring that gets left out becomes a silent defect: water passes ran in the garage, the camera-cut list misses histories, and framegraph-s1 is declaration reading state that execution wrote.
  - Histories have grown to about 440 bytes a pixel and are never released.
- *Direction.* RT2-15 to RT2-19.

**P5. The scene is rebuilt from scratch every frame on one thread, and the ray-traced world lives inside the shadow code.**
- *What is wrong.*
  - Every frame, for every mesh, the engine rebuilds a draw-list entry (two to four times), a cull record, a 272-byte instance row, a TLAS entry, a CPU caster record and a 144-byte hit row. Each has its own copy of the world matrix and its own index (GEO-01).
  - The transform walk visits every entity three to seven times per frame.
  - BLASes are built the first time they are needed, each with a blocking GPU round trip, and are never compacted.
  - The TLAS is packed on the CPU inside `Scene::RenderShadows`, and switching shadows off switches all ray tracing off (`FrameGraphBuilder.cpp:212-218`).
  - One thread records everything.
- *What it causes.* At 60,000 objects with rays on, the frame takes 40.8 ms and is limited by the CPU, against 7.5 ms of GPU work: 20.0 ms of CPU in the shadow phase, 11.5 ms in G-buffer recording. At that scale, rays add about 27 ms of CPU per frame. (One run each; RT2-2 re-measures them as palindromes and splits the phases before anything is designed against them.)
- *Direction.* RT2-4 (the TLAS skip), RT2-34 to RT2-37, RT2-39, RT2-41.

**P6. There is no memory model: no budget, no streaming, no levels of detail, and uploads that block.**
- *What is wrong.*
  - The engine never asks the driver for its memory budget (`VulkanDevice.cpp:707-723`).
  - Every upload, texture mip and static BLAS build stops the CPU until the GPU has finished all queued work (`VulkanDevice.cpp:2086-2118`).
  - Assets load synchronously, the first time any code asks for them (`AssetManager.cpp:759-828`).
  - Meshes have no LOD chain, and textures stay fully resident with every mip.
- *What it causes.*
  - A texture with 12 mips costs 13 blocking submits.
  - The first ray-traced frame after a load builds every BLAS while blocking.
  - The histories alone come to about 1.8 GB at native 2560x1600 (inferred from the formats).
  - A world larger than the 12 GB of VRAM cannot exist.
- *Direction.* RT2-1 (measuring), RT2-18, RT2-19, RT2-35, RT2-38, RT2-40.

**P7. The instruments can lie.**
- *What is wrong.*
  - A shader variant that fails to compile silently drops or swaps its pass. Only screenshot and benchmark runs are guarded (`Entrypoint.h:77-91`).
  - The "16-ray truth" is the shipped chain with more rays, so it shares every filter it is used to judge (`truth_test.py`).
  - The scale benchmark measures with rays off, and its draw counter no longer parses the log.
  - The scale scene is four primitive meshes with no hierarchy.
  - The bridge still has two Vulkan validation errors, and their fixes sit on a parked branch.
  - The test movers (the `Slider` script that drives the car and the moving light in `watch_arm.py`) step on the fixed 60 Hz simulation tick. On the laptop's 240 Hz display the live arms therefore show a mover that stops about every other frame. The owner's own mouse camera and `spin_measure.py`'s pinned clock are not affected.
  - The ray counters are compiled into every ray-traced frame.
  - Around 30 exit gates need diffs over the same camera set, and no harness renders that set and diffs it against a stored picture.
- *What it causes.*
  - One swallowed failure moved the picture 14 levels and cost an evening (RT-SERIES.md:199-206).
  - Scale with rays on had never been measured before this week.
  - The counters cost 0.26 ms (the showroom, 2026-09-05) and 0.85 ms (Headland, 2026-09-04) when last measured. Both predate RT-1, which took the light walk the counters sit in out of the lit shader, so both are stale.
- *Direction.* RT2-0 (movers and spin_measure's scores), RT2-1, RT2-2 (the harness and the reference).

## 2. The target

### 2.1 The frame at the end of this roadmap

The owner's target (D8): 60 FPS at native 2560x1600. A lower render scale with upscaling stays available as a setting, but nothing below counts on it. Updated 2026-09-25 with the decided designs and the owner's rulings D12 to D30.

**On the CPU** (threading is in 2.4): wait for the frame's GPU slot; read input; run the fixed simulation steps; update only the transforms that changed; copy what changed into the GPU scene; refit the light BVH from the light and emitter change lists on a worker thread; compile the frame graph (reused while the frame's shape stays the same); record the passes on several threads; submit.

**On the GPU**, in order:
1. **World update** (on the async compute queue, where that measures faster):
   - one skinning pass for every character, writing posed position, normal and previous position for raster and rays;
   - BLAS builds and refits, batched under a per-frame triangle budget; a skinned BLAS is refit only when its pose changed (RT2-35);
   - the TLAS instance buffer written from the GPU scene; the TLAS skipped when nothing changed and otherwise fully rebuilt, with no refit (RT2-35);
   - the light and emitter buffers updated where they changed, and the light BVH's changed nodes uploaded;
   - the realtime bounce layer's cache updates: every frame at High, and at lower settings only while a realtime-owned light or moving emitter reaches the view (D18).
2. **Cull and G-buffer.** GPU culling of instances (later, clusters), then one single-sampled raster of all opaque geometry into the G-buffer contract (RT2-21): depth; motion; the depth part of the motion (the point's view depth in last frame's camera); shading normal, footprint-filtered roughness and metalness; the smooth vertex normal and the face normal; the tangent angle (for brushed metal, D27); albedo (sRGB, 8 bits) with the specular scalar; an integer lane with the stable object id, the material index, the shading model, occlusion and the Static flag. Emissive goes straight into the colour target. Then the first transmissive surface (glass or water) goes into its own layer, with its coverage and RGB transmittance.
3. **Classification.** For each tile, which signals have work: glossy or mirror surfaces, young history, glass, water, sky. Each signal writes a ray count per texel from its own rule; the lists of rays are compacted on the GPU and dispatched indirectly (RT2-20).
4. **Signals.** Compute passes over the ray lists, each on its own grid with its own history:
   - direct light: guided by last frame's visible lights per tile, hidden lights from the light BVH, one ray per distinct light, reconstructed by its own denoiser (RT2-12b, RT2-24);
   - reflections: a ray rate that follows the lobe's footprint; deterministic hit shading on mirror-rate tiles and stored light at wide footprints; one specular signal that also carries the lamps' shine, shared with the direct pass by the power heuristic (RT2-23, RT2-27, RT2-23f, RT2-23g, D13);
   - the realtime bounce layer: one ray per traced pixel at the trace grid, reading the realtime cache and, for baked lights, the bake where its rays land (D18); at High it runs in every ray-traced view. AO at half resolution;
   - the transmissive layer's direct light and reflections, and refraction (screen space first, a traced fallback with its own accumulator);
   - the translucency lighting cache for particles and the panes behind the nearest (RT2-48), only while such draws are in view.
5. **Composition** (compute; D2): emissive + direct diffuse x diffuse albedo + the specular signal x the specular albedo (full colour, from the same function the traces use) + (the baked bounce + sky light) x AO x diffuse albedo + the realtime layer x diffuse albedo + the baked far direct light + fog, per segment. AO touches stored light only. When MSAA is selected, a raster drawing composes the frame instead, calling the same shading functions (D26).
6. **Transparent composite.** The nearest see-through surface is composited exactly over what lies behind it (its radiance + RGB transmittance x the backdrop). Particles, smoke and the deeper panes go behind it by weighted blending, lit from the translucency cache; the deeper panes carry one fixed mirror ray each (D28). Lamp glows and particles are split at the layer's depth.
7. **Final temporal pass.** It anti-aliases geometry only, at native 2560x1600 (and upscales when a lower render scale is chosen). Its inputs: the colour without the reflection; the 2.5D motion; the identity lanes from the G-buffer's own history; one generic per-pixel lane saying how much of the history is invalid, written by the composition; one motion lane per see-through layer; last frame's exposure. No input is specific to a surface type. The reflection is added after this pass (RT2-28, D23).
8. **Post and UI**, at output resolution, with every brightness weight on exposure-scaled values.

**Removed from the frame, compared with today:**
- the second raster of the scene (kept only when MSAA is selected, D26);
- the per-fragment probe, field and split-sum composition inside the lit shader, and the probe used as a diffuse "sky" term;
- the second glass reflection chain and its always-allocated histories;
- the sea's private passes, and its rays traced inside draws with no accumulator;
- the rays traced inside the draw for the panes behind the nearest (replaced by one fixed mirror ray, D28);
- the fog pass after TAA (fog is applied per segment before the final pass);
- the surface-specific inputs to TAA and the TAA-guide copy of the G-buffer;
- the TLAS refit and the instance and hit tables built on the CPU.

### 2.2 How each lighting signal rebuilds itself without leaning on TAA

| Signal | How it is traced | How it reconstructs itself | What the final temporal pass does for it |
|---|---|---|---|
| Direct light and shadows | K lights picked per pixel: the tile's list of lights that were visible last frame, evaluated exactly, plus hidden candidates drawn from the light BVH; one ray each to a sampled point on the source; the analytic light without shadows times that ray (what MegaLights ships). If full resolution misses its cost bar, the owner's 2x2 tier shares picks within a block and cycles the firing pixel (D14) | Step 1, the baseline: MegaLights-shaped, with a 5x5 variance clip, a fast-history clamp, measured change filtered as RTXDI does, and a spatial pass whose radius is the penumbra width. Step 2, an arm adopted only if it wins: the diffuse visibility ratio denoised apart from the light without shadows (RT2-24). The lamps' shine goes to the specular signal (RT2-23g) | Nothing. Its change map feeds the generic lane |
| Reflections | A ray rate per tile from the lobe's footprint; bounded visible-normal sampling with the true pdf; hits shaded deterministically on mirror-rate tiles and from stored light where the footprint is wide; the lamps' shine from the light candidates, weighted against the lobe ray by the power heuristic (one specular signal) | Two reprojection candidates (surface and virtual image) blended by confidence; the struck identity checked under any motion; one history counter; a fast-history clamp; a history fix; a blur that uses the propagated variance; temporal stabilisation (RT2-23) | Nothing: the reflection is added after the pass (decided, RT2-28; D23) |
| Bounce light (GI) | Two layers (D4, updated by D18). Baked lights: the bake. The realtime layer: one ray per traced pixel; its hits read the realtime cache for realtime-owned light and, at High, the bake for baked lights (the bake gathered at hits). At High it runs in every ray-traced view; at lower settings only while a realtime-owned light is near | The shared diffuse rules (RT2-47): measured change, a relative outlier stage, a young-pixel fill; L1 spherical harmonics output evaluated with the full-resolution normal (kept only if it beats plain colour) | Nothing |
| AO | Short rays at half resolution | The shared diffuse rules, with measured change added for AO; applies to stored light only (the bake and the sky) | Nothing |
| Transmissive layer (glass, water) | The same direct, reflection and refraction passes, run over the layer's points; refraction in screen space first, traced where screen space fails | The same accumulators, with histories shared by glass and water and allocated only while a layer is in view | Its coverage feeds the generic lane |
| Water surface | A spectrum-driven surface on a camera-centred clipmap (FFT waves as an arm the owner judges, D30), drawn as the transmissive layer; its rest surface in the ray structure | As above; the foam keeps its own memory | As above |
| Particles, smoke | Weighted blending, lit from the translucency lighting cache (RT2-48); no random term | The cache's own world-space accumulator | Ordinary anti-aliasing; particles write the reactive mask |
| Glass panes behind the nearest | Weighted blending, lit from the translucency cache; one fixed mirror ray each, its hit shaded by stored light and emissive (D28) | None needed: the mirror ray has no random term | Ordinary anti-aliasing. The named TAA exception D11 is gone |
| Fog | Analytic height fog applied to every segment of every path, aware of the medium each segment runs through; froxels come with WR-11 | None needed | Nothing |

### 2.3 How geometry, acceleration structures and lights scale

- **Geometry.**
  - Every renderable object owns a stable slot in a GPU-resident instance table, which is updated only where something changed. Culling, raster, TLAS instances, hit shading and all ids use that slot.
  - Meshes arrive cooked, with separate position and attribute streams, 16-bit indices where they fit, clusters and LOD chains.
  - Models keep the structure the artist built (D25): the importer creates the entities and meshes the model has, and the engine does not merge them. Per-part CPU costs fall with the GPU scene (RT2-34).
  - Scattered things (foliage, rocks, debris) are instance sets: one entity holding thousands of placements, so a million-instance forest does not hit the ECS's cap of 1,048,575 entities (RT2-39).
  - Positions are handled relative to the camera wherever precision matters (G-buffer reconstruction, ray origins, cache cells), so a world 10 km across renders as cleanly as one at the origin (RT2-41).
- **Acceleration structures.**
  - One BLAS per mesh LOD, built when the geometry arrives: batched, compacted, pooled, under a build budget.
  - The TLAS is written by a compute pass from the GPU scene, then skipped when nothing changed and otherwise fully rebuilt with the fast-trace flag and no refit (the shipped policy). A partitioned TLAS is measured only as an arm at a million instances (RT2-39).
  - The ray LOD is chosen once per frame for every view, by projected size in pixels (the engine's rule that the ray world is world state, not view state).
  - Far-field stand-ins, if a measurement ever needs them, go in a second TLAS, not under a mask bit (Lumen measured a 44% near-field cost from the mask). Cutouts get opacity micromaps if measurement supports them.
  - Lamp shells are opaque to rays once emitters are sampled on their own triangles (RT2-46).
  - Glass and water enter the TLAS as a transmissive class with a mask bit of their own (RT2-31).
  - Skinned meshes are posed once per frame; a refit is skipped when the pose did not change, and a rebuild follows a large deformation.
  - Everything is streamed in, and evicted together with its geometry, under the VRAM budget.
- **Lights.**
  - One persistent GPU light buffer for the analytic types: directional (with an angular radius), sphere, tube, rectangle and disk, with spot cones as modifiers. One emitter table of emitter clusters built from emissive triangles. Both are updated only where they change.
  - One light BVH (two levels, over the analytic lights and the emitter clusters), built and refit on a worker thread from the change lists, gives every ray hit a pick whose cost grows with the logarithm of the light count. Primary pixels add last frame's visible-light list per tile (RT2-12b).
  - Each fixture carries its light through exactly one representation in each path. A Light may name its glowing mesh in an optional, hand-set field (D13); the pair's shine is then counted once by the power heuristic.

### 2.4 Threads and GPU queues

- **CPU.**
  - One job system: work-stealing workers (hardware threads minus one) plus I/O threads.
  - The game thread runs scripts; Jolt runs on the same workers.
  - Each per-frame system is a task with declared inputs and outputs.
  - Extraction produces a snapshot, so the simulation of frame N+1 runs while frame N is recorded.
  - Recording is split by groups of passes into command buffers on several threads, submitted in graph order (Vulkan only; OpenGL is frozen, D1).
  - Time is kept as 64-bit ticks, and the frame waits for its GPU slot before it reads input.
- **GPU.**
  - A graphics queue: raster, composition, post, and the signal passes by default.
  - An async compute queue: acceleration-structure work, cache updates, and any signal pass shown to overlap usefully.
  - A transfer queue: uploads and streaming.
  - The queues synchronise with timeline semaphores. The render graph assigns passes to queues and inserts the waits.
  - Each use of async compute is adopted only after an A,B,B,A gain on this laptop, because a laptop's power limit can eat the overlap.

### 2.5 Keep, refactor or rewrite

| Subsystem | Verdict | Why |
|---|---|---|
| Vulkan device, swapchain, crash diagnostics (checkpoints, device fault, address registry), deferred deletion, VMA, layouts built from shader reflection | Keep; refactor | Sound. They gain a memory budget, extra queues and timeline semaphores |
| RHI binding (descriptor sets per pass, rewritten every frame) | Rewrite: descriptor indexing extended to storage images, samplers and TLAS slots, buffers by device address, and a per-frame constant ring (the descriptor-heap extension is out of scope, RT2-18) | At least five recorded bugs. It blocks GPU-driven and multi-threaded work |
| RHI synchronisation (implicit; each texture tracks its own layout) | Rewrite: barriers worked out by the render graph | Needed for compute passes, memory aliasing and extra queues |
| Uploads (a blocking submit for everything) | Rewrite: an upload ring on the transfer queue | Stalls; no streaming possible |
| Pipelines and shader compilation | Refactor | Caches, nothing created inside a frame, loud failures |
| Render graph | Evolve in place, one capability behind its own flag at a time (RT2-15, decided 2026-09-25) | An ordered list that cannot express compute writes. Evolving it reaches the same end state with less code than a parallel executor |
| FrameGraphBuilder (the 5.5k-line function) | Rewrite: tables of signals and layers, plus RT and raster recipes. Only AO moves onto the tables in RT2-17; every other signal moves in its own rewrite | Wiring by hand produces silent defects |
| Renderer3D (all static) | Rewrite the frame and pass half; keep the shader maths | There is no per-view context |
| The signal contract (accumulate, measured change, RT-9's allocator shape, guided downsample, half-float rounding) | Keep the ideas; refactor into one shader per kind of signal | Measured and principled |
| Reflection chain | Rewrite the denoiser on NRD's design, with NRD itself as a comparison arm (D15). Rewrite hit shading (deterministic on mirror-rate tiles) and the lamps' shine (one specular signal). Keep NEE and MIS, the virtual-image idea, the demodulation and RT-24's lobe-agreement weight (re-derived and gated) | Its inputs and checks are wrong, its hits are shaded with one random light, and it costs about 59% of the garage frame |
| Direct lighting | Rewrite the light model (one library, analytic light x one ray) and the selection core (one light BVH, guided primary sampling). Keep the pass layout, measured change and the every-light arm (as a check of sampling error; the reference renderer is the truth) | Scaling and duplication |
| GI (bounce light) | Two layers (D4, updated by D18): the bake for baked lights, stored as today, plus a live layer that at High runs in every view and also reads the bake where its rays land. The live layer is one ray per traced pixel with a spatial-hash cache (decided). The bake becomes sparse, automatically placed bricks. Keep the shared hit shader, the NEE machinery, the bake stamps and the fill solver's parts | Four different answers today, and Realtime lights never bounce in baked scenes |
| Reflection probes | Refactor: captured after the TLAS is built (RT2-6); in RT mode they lose their diffuse role and their role at hits. Captured through the ray-traced world only if the rough tier still reads a probe (RT2-27) | Captured without shadows today, and a third too bright |
| Lit shader and materials | Refactor: modules, one BSDF (with energy compensation and one Smith term), and a compute composition in RT mode. The forward path stays for the raster recipe and for MSAA in RT mode (D26) | Double raster; three Smith terms and five BSDFs |
| G-buffer | Refactor: a contract described once as data, with integer ids, a geometry lane, the tangent angle (D27) and the depth part of the motion | The contract was never designed |
| TAA | Refactor into an anti-aliasing pass for geometry only (upscaling when a lower render scale is chosen) | Today it is where everything gets patched |
| Transparency (WBOIT, the glass layer) | Refactor: one transmissive layer composited exactly, a ray traversal class with tinted shadows, and refraction in screen space with a traced fallback. WBOIT stays behind the front layer (AVBOIT as a measured arm) | Outside the ray-traced world today |
| Water | Refactor into a material, a spectrum-driven surface generator on a clipmap (FFT waves as an arm, D30) and a medium; delete the private passes | A parallel renderer |
| Scene extraction and GPU cull tables | Rewrite: a GPU scene | Rebuilt every frame |
| The ray-traced world (RayShadows, BLAS/TLAS) | Rewrite: its own module, with the shipped policy (TLAS skipped or fully rebuilt; BLASes built at load, batched, compacted, pooled) | Lives inside the shadow code; builds block; nothing compacted; the TLAS refits with changed contents |
| Mesh cooking | Refactor: separate streams, LOD chains, clusters; the model's structure stays as the artist built it (D25) | |
| Scattered instances (none today: every rock is an entity) | New: instance sets (RT2-39) | The ECS caps a world at about a million entities |
| World precision (float32 world positions everywhere) | Refactor: camera-relative reconstruction and scale-aware ray offsets (RT2-21, RT2-41) | Precision degrades kilometres from the origin |
| Frame loop and threading | Rewrite: a job system and frame pipelining | One thread |
| ECS | Keep the storage; refactor: per-component change versions, parallel ranges | Sound |
| Transform system | Refactor: flat arrays, writes that record the change | Walks every entity 3 to 7 times per frame |
| Asset loading | Rewrite into streaming. Keep the import-cache format and the virtual file system | Synchronous on first use |
| Physics glue | Refactor | Private thread pool; hard caps |
| C# bridge | Keep | Fine |
| Measurement harness | Keep and extend | It is the owner's method |
| Light selection (a CPU grid per view; every light walked at hits) | New: one light BVH, world state (RT2-12), measured against the cascaded light grid Doom ships (rule 1) | Cost grows with the light count; the world grid is shaped for one scene |
| Lighting for particles and back panes (none: unlit particles; an in-shader loop for panes) | New: the translucency lighting cache (RT2-48) | The shipped baseline (UE's translucency volume, MegaLights, id Tech 8) |
| OpenGL backend | Freeze (D1): the raster path keeps working as today, with a shader set that stops changing; new features are Vulkan-only | It cannot ray trace, and parity would hold the new design down |

## 3. Current issues and their fate

Every issue in the inventory, with one of three fates: fixed directly by an item; absorbed by a rewrite item (and why the rewrite removes it); or left parked (and why).

**RT-24, symptom by symptom.** RT2-0 finishes it inside today's architecture. The table as RT-SERIES-2 planned it is kept below the status column, so the history of each arm stays readable. (The step letters are RT2-0's.)

| Symptom | Removed by (as planned 2026-09-24) | Status 2026-09-25 | Proven by |
|---|---|---|---|
| Ghosts behind reflected poles, the bumper, a car afterimage | Fix 2 (built), then arms A (the frame filter's rules), F (this frame's image distance), G (the curvature check), I (the struck-object check under camera motion); fallback H (a fast-history clamp) | Change 2 (the lobe-agreement check) removed the prime suspect; the owner judged the ghosting "not noticeable". H was dropped (parked flicker doubled). F and I move into RT2-23 (23a, 23b); G stays as attribution arm (f) | `spin_measure.py --stage=many16`, the pole and bumper spot scores (added to the script in step 0) |
| The blur that spreads | Arm A (the frame filter's motion lane, colour box including the material widening, and moving feedback); fallback J (the filter's memory on a shiny pixel set by the reflection's own history length) | A's motion lane done (the filter follows the surface's motion). J dropped (its motion gate broke the no-guessed-values rule). The colour box and the material widening move to RT2-28 (28b); attribution arm (d) measures what is left | `spin_measure.py`, against the frame-filter-memory-off reference |
| Reflections settle late after a stop | Arms C (the motion test reads this frame's camera) and D (extra rays stay on for a bounded number of frames after motion) | C done. D closed: the owner judged the settle "almost real time"; its structural form (young texels, camera-made ones included, keep their rays for the settle length) moves to RT2-27 | spin_measure's after-stop rows |
| The car's shine settles late | Arms C and D, then E if the analytic highlight carries it | E becomes attribution arm (c) (DirectTrace's specular half zeroed); the structural cure is the one specular signal (RT2-23g) | After-stop rows over the car; live |
| Tube reflections noisy while moving | Arms C (the extra rays really run during a swing), E (which copy of the tube carries the noise) and K (whether tube bars left out of the 16 aimed-emitter slots cause it). If K shows they do, the cure is RT2-11, and the owner decides whether RT-24 waits for it or is committed with that symptom recorded | K superseded: every emitter is now aimed at (the 16-slot cap is gone), and the 8-neighbour lamp pool cut tube grain further. Motion speckles 513 against the 16-ray reference's 494 | spin_measure's grain score over the tube patch, without many16; `speckle_count.py`; live |
| Fix 1 hands a young picture's grain to TAA | Arm H (a young-pixel fill in today's accumulator) aims to remove the dependency inside RT-24. If it cannot, RT-24 is committed with the reliance written down (D9), and RT2-23 and RT2-28 remove it | H dropped; RT-24 commits with the reliance written down (D9). RT2-23's history fix (23c) and RT2-28 remove it | The grain score with the frame filter's memory off |
| Speckle and edge jitter at fast moves (found during RT-24) | not planned | Attribution arms (a) to (f) at the owner's speed. A leftover from outside the reflection chain is written under its owning item and RT-24 is committed (D17) | `speckle_count.py` dots over 32 and 64 levels, spin_measure's edges score, against the 16-ray reference |

**All issues:**

| Issue | What it is | Fate | Item | Why |
|---|---|---|---|---|
| RT-24 | Fast swing: ghosts, a spreading blur, late settle, noisy tubes | Fixed directly | RT2-0 (structure: RT2-23, RT2-27, RT2-28) | See the table above. A leftover from outside the reflection chain is written under the item that fixes it, and RT-24 is committed (D17) |
| RT-22 | A moving light's lighting trails behind it | Split | RT2-23 (the reflection third), RT2-24 (the direct history), RT2-31 (the glass pane's lamp light gets measured change) | Each remaining part lives in a different history; `emitter_lag.py` gates each one |
| RT-2.2 | No deferred resolve; opaque surfaces rastered and materials sampled twice | Fixed directly | RT2-22, step 1 | Built exactly as filed, with the albedo lane moved to sRGB8 first. Then, per D2, one compute composition replaces the second scene draw in RT mode, except when MSAA is selected (D26) |
| ISS-1 | The bridge water glitter blinks more since RT-22's rule (0.80% to 0.97%) | Fixed directly | RT2-8 | The behaviour-based floor replaces the rule keyed on "covered" pixels, so the sea gets its floor back with no water exception. It lands in M1, right after RT-24, not at the end of the series |
| ISS-2 | The general "how a pixel changes" rule for TAA's floor is not built | Fixed directly | RT2-8 | Built as the owner's parked proposal; it needs nothing from the later rewrites |
| ISS-3 | A reflection keeps moving after its object stops | Absorbed | RT2-23 (RT2-0 fixes the one-frame motion tail) | The new denoiser checks what the ray struck whenever anything moves, instead of closing its gates two frames after a stop |
| ISS-4 | Bridge cables, tower and thin deck members blink under TAA | Stays parked | (direction: RT2-38) | Not a ray-tracing defect: 0.00-0.01% with no AA or MSAA. The owner's look rules forbid fading the key members. RT2-29 must be checked on the cable band |
| ISS-5 | Lights at ray hits are points, even tubes | Fixed directly | RT2-10 | The one light library shades tubes and spheres as areas at every hit |
| ISS-6 | The GPU-driven lit path is "unfinished" in the docs but on in the code | Fixed directly | RT2-1 (text and a parity check with rays on), RT2-37 (replaced by compacted draws) | |
| ISS-7 | Two half-float histories still round toward zero | Fixed directly | RT2-6, arm 8 | Recorded as the owner's call; shown as an arm |
| ISS-8 | `kSettledBound` reads the wrong counter | Fixed directly | RT2-6 (arm 12) | One history counter. It changes pixels, so it is an owner-judged arm, not a silent patch |
| ISS-9 | The baked probe is a third brighter than the traced reflection | Fixed directly, then absorbed | RT2-6 (probes get shadows; the reference renderer settles which is right), RT2-27 (rough reflections from stored light, not the probe) | |
| ISS-10 | Switching a baked light throws the bake away | Absorbed | RT2-25 | Switchable lights are Realtime (the owner's rule), and with RT2-25's realtime layer they now bounce light too. Switching a baked light still means a re-bake, by design (D4: baked lights stay baked). After RT2-12, an arm tests moving FullBake and HybridFullBake lamps to HalfBake, which would make switching their direct light free; the owner decides from it (D12) |
| ISS-11 | The sea work the owner accepted on 2026-09-20 ("looks perfect to me, make it default") is on a parked branch; main's sea mirror has no accumulator | Absorbed | RT2-1 (its validation fixes), RT2-31 and RT2-32 | The owner chose to bring it back with the water rework in M5 (D10). Costs recorded then at 1440p: the pier +1.7 ms, Glitter +1.2 ms, Headland 1.3 ms faster. It was never checked with a moving camera, so RT2-32 checks that before it becomes the default. Its white shine came from an absolute clamp; it is replaced by the general relative clamp (D29) |
| ISS-12 | Random draws move only with TAA's jitter; rays force TAA | Fixed directly | RT2-7; the force is lifted at the end of M5 (D3, RT2-33) | |
| ISS-13 | Texel emitters: inert on real content, unexplained cost | Absorbed | RT2-11 | The emitter table is rebuilt from emissive triangles, GPU-resident, with alias-table sampling |
| ISS-14 | Cutout limits (solid for AO, material alpha in shadows, the texture read during traversal) | Stays accepted; one part measured | RT2-38 | The limits are deliberate trades. Opacity micromaps are measured against the traversal-cost cliff |
| ISS-15 | Leftover ray-budget code and legacy sea paths; nine unverified review findings | Fixed directly | RT2-3, RT2-32 | Deleting the code makes the findings moot. The reuse verdict is not reopened |
| ISS-16 | The showroom slowdown, only partly recovered | Absorbed | RT2-1 (counters), RT2-13 and RT2-22 (register pressure), RT2-26 (field lookup per fragment) | The showroom became the garage, so its numbers are out of date. Section 6's budget replaces them |
| ISS-17 | The opaque pass is bound by per-pixel work, not the BRDF | Absorbed | RT2-22, RT2-29 | One composition with no second material evaluation, and fewer pixels. The BRDF finding stands |
| ISS-18 | No many-light direct lighting since RT-10 was dropped | Fixed directly | RT2-12, RT2-12b | One light BVH at every ray hit, and primary sampling guided by last frame's visible lights (MegaLights' front end). The only reuse arm is spatial-only pooling; temporal reservoir reuse stays out (section 7, item 5) |
| ISS-19 | At most 16 glowing surfaces get aimed rays | Fixed directly | RT2-11 | |
| ISS-20 | Reflections on moving objects read memory from the wrong place | Absorbed | RT2-23 | Checking the struck object under any motion, plus a history-length counter |
| ISS-21 | Clean-up passes spread bright outliers; visible-normal sampling ships off | Absorbed | RT2-6 (clamp scaled by lobe width), RT2-23 (outliers bounded before any spatial pass; visible-normal sampling re-measured with honest densities) | |
| ISS-22 | Reflections pass through two frame filters in a row | Fixed directly | RT2-0 (bisection), RT2-28 | |
| ISS-23 | See-through surfaces are second class for ray tracing | Absorbed | RT2-31 (with RT2-13's second half and the translucency cache, RT2-48) | The panes behind the nearest were a named TAA exception (D11). The owner reversed it on 2026-09-25 (D28): each gets one fixed mirror ray, its hit shaded by stored light and emissive, judged live on the garage close-up |
| ISS-24 | A failed shader variant quietly swaps paths | Fixed directly | RT2-1 | |
| ISS-25 | Two validation errors on the bridge, fixed only on a parked branch | Fixed directly | RT2-1 | |
| ISS-26 | Per-frame scene walks grow with object count | Absorbed | RT2-34, RT2-43 | Tracked writes and change lists replace the walks |
| ISS-27 | No mesh levels of detail | Fixed directly | RT2-38 | |
| ISS-28 | Area lights: no LTC, no luminaire binding, the capsule highlight too bright | Fixed directly | RT2-10 (areas, rectangles, LTC as a measured option, tube energy), RT2-11 (a Light may name its glowing mesh in an optional, hand-set field, D13) | |
| ISS-29 | Night-realism items not built (WR-5 remainder, WR-6, WR-11, WR-12, WR-14) | Stays deferred | (prerequisites: RT2-33) | They stay in the WR series. RT2-33 does only fog in the composition and fog at ray hits |
| ISS-30 | Moving chrome cube: stripes, banding, speckle, softer look | Absorbed | RT2-23 (with RT2-21's curvature and the RT2-2 reference) | Measured against a truth render for the first time |
| ISS-31 | Tube rims report motion while parked | Fixed directly | RT2-6 (arm 14) | It changes pixels, so it is an owner-judged arm |
| ISS-32 | The sea's lighting gaps, and unconfirmed owner reports | Absorbed | RT2-32 | Lighting per crossing through the layer. The blur's camera, given 2026-10-13: Headland under the swing, the reflection patchy while turning (RT2-31d owns it) |
| ISS-33 | Per-pixel memory grows with resolution | Fixed directly | RT2-19, RT2-29 | |
| ISS-34 | The optimisation list lives only on the parked branch | Absorbed | This roadmap | Glass rays go to RT2-31; the probe fetch at every hit to RT2-25 and RT2-27; the sea mirror's accumulate to RT2-32 (D10); RT-10's +8 ms is dropped |
| ISS-35 | No pipeline or shader cache | Fixed directly | RT2-9 (D6: approved) | |
| ISS-36 | Raster fallback defects | Partly fixed | RT2-1 (re-check the device loss with rays on and reflections off), RT2-4 (name the demoted light) | The SSR smear and the voxel GI noise stay parked: the raster fallbacks are kept at parity, not improved, in this series |
| ISS-37 | Baked-lighting limits the traced picture now depends on | Absorbed | RT2-25 (step 25e: the bake gathered at the live rays' hits, D18), RT2-26 (contact-scale bleeding and the leak fixtures in its exit gate), RT2-40 (BC6H in the texture cooker) | Baked lights stay baked (D4), so reflections of Static surfaces keep taking baked lamps' light from the field, by design. Nested volumes blend through sparse bricks; the light that bleeds at contact scale (about 1.9% of pixels) is measured against the reference renderer and must fall, first through RT2-25e. Cells inside walls: dilation is the first arm, and moving cells out of walls (relocation) is a measured arm, because RageV measured it helping one fixture and hurting the sun-lit ones (corrected 2026-09-25); a stale bake is flagged in the editor and names what changed. The bake on OpenGL stays unsupported (OpenGL is frozen, D1). Lightmaps are not planned (meshes carry one UV set) |
| ISS-38 | Terrain: no shadow offset for LOD error, no triplanar mapping, macro breakup not exposed | Partly | RT2-21 (terrain layers at hits), RT2-38 (shadow offset) | Triplanar mapping and the macro inspector rows stay parked as materials work outside this series |
| ISS-39 | The visual-script loader drops unknown nodes and saves the loss | Stays outside | none | Not renderer work. It loses user data, so a small separate task is recommended now |
| ISS-40 | RT-9 earns nothing measurable | Fixed directly | RT2-0 (the motion test), RT2-27 (rays set by the lobe's footprint within a fixed budget; RT2-23's step 23d merged there), RT2-12b and RT2-24 (the direct half re-measured) | Part of the "nothing measurable" was probably a pass that never ran during camera motion. That is read from the code; RT2-0's pass-timing check confirms it before anything changes |
| ISS-41 | One 6,500-line lit shader and layouts mirrored by hand | Absorbed | RT2-1 (checks), RT2-13 and RT2-22 (dead arrays out), RT2-17 and RT2-21 (modules, one layout definition) | |
| ISS-42 | Measurement gaps that confound arms | Fixed directly | RT2-2 | |

## 4. The items, in order

Each item lands behind a flag or a reference arm, is measured, and is reported before the next one starts. Anything the owner judges live is shown one arm at a time. Costs are recorded as each item lands, even where the optimisation belongs to a later item.

---

### RT2-0 · Finish RT-24 (done, committed 2026-10-12)

- **In plain words.** RT-24 is the fast camera swing in the garage that left the reflections smeared, ghosted and speckled. Most of it is fixed and waiting to be committed; the remaining speckle and edge jitter are now about as bad as in the 16-ray reference picture. What is left is to find where that remainder comes from, one switch at a time. If it comes from outside the reflections, RT-24 is committed and the remainder is written down under the item that fixes it (the owner's ruling D17).

> **Closed, 2026-10-12 (the owner's live checks; committed under D17 and D9).**
> - *The window drive* (`watch_arm.py light 8`). The first window showed the cube only where it ended: OnFrame
>   got the raw frame time, and the first frames compile shaders for seconds each, so one frame carried the whole
>   drive. Fixed in the engine: the measured step is capped at the fixed step's `MaxFrameTime` (0.25 s) before
>   anything sees it (`Application.cpp`), the way Unity's maximum allowed timestep works. The second window: no
>   smear, but the car's reflections got noisy while the cube moved. **An RT-24 regression in measured change**,
>   found headless (the car's fine-detail flicker 2.48 with the cube moving against 0.99 still, at the live
>   window's camera): fix 1 (2026-09-24) writes the aimed samples' light apart (`o_Aimed`), the record still read
>   only `o_Reflection`, and the re-light compared the full light, so every texel that saw a lamp read as changed
>   on every frame anything moved (the key mixes every ray instance's flags, so even a plain cube moving 50 m
>   under the floor did it). Proven both ways: `--change-force=on` with the cube still 2.43, `--measured-change=off`
>   with it moving 1.06. Not the cause: the cube's light, its shadows, its glow, the lamp pool, the GI, the
>   confidence rays, the identity test, TAA's still feedback. Fixed: `WrittenLobe`, `WrittenAimed` and
>   `RecordedLight` in `reflection_trace.rvshader` serve the trace's two lanes, the record (which now binds the
>   aimed lane) and the re-light, so the sides cannot drift a third time; glass reflections too. After: 1.07
>   moving against 0.99 still, and the change view fires only round the moving cube. The owner: "very VERY
>   clean"; the cube's reflection arrives about 200-250 ms behind the cube (recorded as a target in RT2-23's 23b
>   gate and RT2-28's gate).
> - *The car's stop* (`watch_arm.py car 8 --cam=-2.3,0.72,-2,5,0,4`, closer at the owner's ask): clean, no
>   smear; the reflection takes "a very very small amount of time to stop" (RT2-23, RT2-28; the settle after a
>   stop is RT2-27's young-texel rays).
> - *The owner's spin* (`watch_arm.py spin 0`): "a very small amount of speckles that show up on the ground but
>   overall it's an improvement". The speckle is TAA restarting pixels on the shiny floor, outside the
>   reflection chain (this item's attribution), so under D17 it lives in RT2-28, RT2-23, RT2-47 and RT2-24.
> - *Headless gates* (2026-09-25, re-run where today's fixes could move them). The slow 16-ray swing: memory on
>   still ghosts more than off (pole 6.49 against 4.40; RT2-23's structural ghost). After a stop, frame 5 is not
>   yet at the parked floor (the owner judged the settle "almost real time"; RT2-27). The budget pass runs in
>   the camp, not in a camera-only swing (by design). The bridge parked: 0.983% blinking (0.969% before RT-24, the
>   gate 0.97%), cables 3.54% (3.47%): missed by about 200 pixels of 1.44 million, carried to RT2-8's gate. Cost
>   against a build of 76ab6aa, palindromes: garage 19.06 to 19.90 ms at 1600x900 and 48.55 to 53.66 at
>   2560x1600, Headland unchanged; the lamp pool alone +1.45 ms (the optimisation pass after the series).
> - *Also in the commit:* the repository can move: every tool script works out the repo root from its own
>   place (`_RAGEV`), and the engine finds its sample project and its export file above its own executable
>   (`Process::FindAboveExecutable`), clearing a game-module cache made in another folder
>   (`ModuleBuild::CachedElsewhere`; scenetest `CheckRelocation`).
>
> **Attribution, 2026-09-25 (the six arms at the owner's speed, headless).** Not the cause: the
> reflection's rays (the 16-ray picture has the same speckle: 482 dots against 513), one random lamp
> per reflection hit (`--hit-light-sampling=off`: no change), the floor's normal map read as mirror
> curvature (`--debug-view=reflection-image`: the floor's image distance is smooth), the reflection's
> own memory (off: more speckle, 612). The speckle is where the frame filter restarts a pixel from
> this frame's raw light: its memory off nearly doubles it (908); its 2% depth test off cuts it to 396
> but raises the pole's ghost 9.51 -> 12.85 and the edges 14.87 -> 17.10; at 1% it rises to 665. On
> the shiny floor the filter fetches its history by the reflection's motion, so the 2% test is really
> "the reflection moved too far, start fresh". DirectTrace's specular off leaves the floor sharper
> while moving (0.82 -> 0.96): the highlight's four-frame memory softens it. **Pulled in and switched
> off (owner):** NRD's same-surface test, built as `--taa-plane-test` (last frame's depth predicted
> through last frame's camera, compared along the surface's slope, 1% of the frustum): it measured
> the same as the depth test off (393 dots, pole ghost 12.85), so it is off by default and returns in
> RT2-28. **The leftover, per D17:** the speckle and ghost trade on reflective pixels -> RT2-28 (the
> reflection added after the filter) with RT2-23; the raw light a restarted pixel shows -> RT2-47; the
> highlight's softening -> RT2-24.

> **Progress, 2026-09-24 (built, not committed; RT-24 commits only when fully fixed).** Done: arm 0
> (instruments: `spin_measure.py` scores, `--pan`, the owner-speed swing, `spin_timings.py`); arm C
> (the camera test fixed, and camera-only extra rays behind `--confidence-rays-camera`, off: 6.6 ms a
> frame and no visible gain); the prime suspect of the ghosting found with a pan control (the
> floor's roughness pattern dragged with the reflection) and fixed by a lobe-agreement check;
> the frame filter follows the surface's motion; the speed-guessed camera cut replaced by reported
> cuts (it fired 6-8 times a minute on the owner's drags); the frame filter's no-history pixels
> rebuilt at the pixel centre; the tube lamps' length energy; the resolve no longer spreads aimed
> lamp light. Dropped: H (parked flicker doubled), J (its motion gate breaks the owner's
> no-guessed-values rule; ungated it raised parked flicker), and the reflection memory set by its
> lookup's own uncertainty (built as change 3; it made the orbits worse and was reverted). Owner, live: ghosting "not noticeable", reflections settle "in almost real time".
> Open: speckle and edge jitter at fast moves, the lights' reflections fatter than on 8 Sept (bisected
> to RT-21's highlight fix; engine fixes only, no scene changes). A hard-coded-values audit of the
> reflection chain and the frame filter read 262 fixed values; section 10 places every one it found.

> **Progress, 2026-09-25 (built, not committed).** Every listed emitter is now aimed at by one pass of
> weighted reservoir sampling, so the 16-slot cap is gone and arm K is superseded (RT2-11's note). The
> resolve pools up to 8 neighbours' lamp points the way ReSTIR does (+2.1 ms at 1600x900; the owner
> confirmed lower grain; RT2-12's note). At the owner's swing speed: motion speckles 513 (85 above 64
> levels) against the 16-ray reference's 494, and pole edge jitter 9.51 against 8.83. Arm D is closed
> (the owner judged the settle "almost real time"); its structural form moves to RT2-27. Arm I moves to
> RT2-23 (23b).

> **Owner's ruling D17 (2026-09-25).** If the attribution arms show that the leftover comes from outside
> the reflections, commit RT-24 and note the leftover in this roadmap under the item that fixes it.

> **Changed by the comparison (2026-09-25).** The remaining arm list is replaced by six attribution arms
> run at the owner's speed. The leftover is at the 16-ray reference's level, so no new reflection
> mechanism can remove it; the useful step is to find its cause. Arms F and I move to RT2-23 (23a, 23b),
> arm D's structural half to RT2-27, arm E becomes attribution arm (c), arm G becomes (f), and the rest of
> arm A (the colour box and its material widening) moves to RT2-28 (28b).

- **What is wrong now** (2026-09-25).
  - At the owner's swing speed the reflections are close to the 16-ray reference, but speckle and edge jitter remain: 513 motion dots against 494, pole edges 9.51 against 8.83.
  - The lights' reflections are fatter than on 8 September (bisected to RT-21's highlight fix). Only the reference renderer (RT2-2) can say whether today's look is right for the floor's roughness.
  - Fix 1 hands a young picture's grain to the frame filter; its own comment says so (`pbr_fragment.glsl:6193-6195`). That is a reliance on TAA, which D9 lets RT-24 be committed with, written down; RT2-23 (23c) and RT2-28 remove it.
  - Everything above is built and uncommitted.
- **What was wrong when this item was written** (2026-09-24, kept for the record).
  - A fast camera swing in the garage still leaves a blur that spreads over the wet floor; fix 2 turned most of the ghosts into this.
  - Reflections, and the car's shine, settle a fraction of a second after the camera stops.
  - The ceiling tubes' reflections are very noisy while moving.
  - Fixes 1 and 2 are built and uncommitted.
- **What changes.** One arm at a time, each measured and then shown live, with a report before anything is changed.
  - **The remaining work (decided 2026-09-25): six attribution arms.** Each runs at the owner's speed (`spin_measure.py --swing=45 --rate=3`) and is scored by `speckle_count.py` (dots above 32 and above 64 levels), spin_measure's edges score, and the pole, car and tube grain:
    - (a) the reflection history off;
    - (b) `--hit-light-sampling=off` (every light at each reflection hit instead of one random one);
    - (c) DirectTrace's specular half zeroed (the old arm E: which copy of a lamp's shine carries the noise);
    - (d) the frame filter's memory off;
    - (e) a staged `taa_resolve` whose 2% same-surface depth test is replaced by its general form. The shipped form first (rule 1): NRD's, the point's depth predicted through last frame's camera, with a tolerance from the frustum's size and the facing angle. The one-texel form is measured against it. These are RT2-28a's arms, run here early for attribution only;
    - (f) the old arm G: `--debug-view=reflection-image` on the floor under the tubes. If the image distance collapses toward zero, the normal map is being read as mirror curvature (REFL-04).

    A cause inside the reflection chain that one rule removes is patched here, as its own arm. A cause outside it (the frame filter's depth test, the direct highlight's four-frame memory) is written under its owning item (RT2-28, RT2-24), and RT-24 is committed (D17).
  - **The arms as planned on 2026-09-24, with their status** (kept for the record):
    - **0. Instruments first (no picture change)** (done). `spin_measure.py` gained spot scores for the pole (x 1400-1560, y 560-760), the bumper (660-940, 560-700) and a tube patch, and a grain score: over each spot, the per-pixel spread across the swing's centre-crossing frames, averaged over the spot. The live movers: the `Slider` script, which drives the car and the moving light in `watch_arm.py`, moves in `OnFrame` instead of the fixed 60 Hz `OnTick` (`Slider.cpp:22`); `Orbiter.cpp` follows for consistency. `watch_arm.py` refuses to show an arm whose log reports a shader or pipeline failure.
    - **A** (partly done). Test `taa_resolve` on the clean 16-ray test, one rule at a time: (1) the reflection motion lane (done: the filter now follows the surface's motion); (2) its colour box, including the material widening, which opens the box 1.5x on the floor (roughness 0.21) and up to 5.2x at roughness 0.35 (moved to RT2-28b; attribution arm (d) measures what is left); (3) the moving feedback. The two feedback values (0.9 moving, 0.98 still) are the owner's own project settings (`SampleProject.rvproject`); any change to them goes to the owner as a choice.
    - **C** (done). RT-9's "has anything moved" test reads this frame's camera. It compared last frame's camera with last frame's camera (`FrameGraphBuilder.cpp:2401-2403` against `Renderer3D.cpp:3900` and `7584`), so in the runtime a camera-only swing never counted as motion. Probe captures also overwrote the stored eye for every face.
    - **D** (closed by the owner's judgement; structural form in RT2-27). Keep the extra rays on after a stop, for a bounded number of frames since the last motion (the settle length). Not "while tiles hold young pixels": thin edges always read young.
    - **E** (becomes attribution arm (c)). Split the tube noise between the tube's two copies: the analytic highlight, re-sampled every frame and averaged over four frames by surface reprojection, and the traced image of the glowing bar.
    - **F** (moved to RT2-23's step 23a). This frame's image distance, in bounded form, with sky misses flagged instead of averaged in as 10 km (`reflection_accumulate.rvshader:1502`, `reflection_trace.rvshader:826`, now lines 703 and 835). It helped before (pole 17.1 to 13.6).
    - **G** (becomes attribution arm (f)). The curvature check.
    - **H** (dropped: parked flicker doubled). A fast-history clamp and a young-pixel fill. The comparison found the likely reason: the clamp's width was sized from a noisy one-ray neighbourhood instead of the fast history's own spread. RT2-23 (23b) builds it that way.
    - **I** (moved to RT2-23's step 23b). The struck-object check under camera motion (REFL-05).
    - **J** (dropped). The frame filter's memory on a pixel set by the reflection's own history length.
    - **K** (superseded 2026-09-25: every emitter is aimed at). Log which glowing surfaces get the 16 aimed-emitter slots and run the tube patch with the cap raised.
- **Owner sees.** The floor stays sharp through a swing. Reflections and the car's shine settle within a few frames of a stop. Tube reflections are calmer while moving. Live movers glide instead of stepping. The first four were judged live on 2026-09-24; what is left is the fast-swing speckle and edge jitter.
- **Touches.** `taa_resolve.rvshader`, `reflection_accumulate.rvshader`, `reflection_trace.rvshader`, `reflection_resolve.rvshader`, `reflection_budget.rvshader`, `include/reflection_emitters.glsl`, `pbr_fragment.glsl`, `FrameGraphBuilder.cpp` (2396-2405), `Renderer3D.cpp` (`CameraStill`, `AnyInstanceMoved`), `SampleProject/Source/Slider.cpp`, `SampleProject/Source/Orbiter.cpp`, `tools/scripts/garage/watch_arm.py`, `spin_measure.py`, `spin_timings.py`, `speckle_count.py`.
- **Scope.** Patch. **Depends on.** Nothing. Nothing else in this roadmap starts until RT2-0 is committed, so its measurements are never mixed with other changes. **Risk.** Medium: TAA rules touch every pixel of every scene, so every change is re-checked on the bridge.
- **Exit gate.**
  1. Owner-speed swing (`spin_measure.py --swing=45 --rate=3`): `speckle_count.py` dots (above 32 and above 64 levels) and spin_measure's edges score within 5% of the 16-ray reference (`--stage=many16`), or the remainder attributed by arm to a named item outside the reflection chain and written there (D17). (Replaces the 2026-09-24 gate 2, the tube patch's grain below arm 0's number, which is now a row of this gate.)
  2. `spin_measure.py --swing=15 --rate=2 --stage=many16`: the pole, car and tube ghost and grain, moving against settled, at or below their reflection-memory-off numbers (the pole's is 7.8; the others were recorded in arm 0) with the memory on. No other region gets worse. (The 2026-09-24 gate 1, widened to the car and the tube.)
  3. spin_measure's after-stop rows: the frame five after the stop within the parked noise floor (`parked_stats.py`).
  4. Parked garage frames: diff 0 against the committed-candidate build, or within the parked noise floor where a TAA rule changed.
  5. The four-way check every TAA change needs: the window drive (`watch_arm.py light 8`) and the car's stop (`watch_arm.py car 20`) by the owner's eye; the bridge's `check_glint_flicker.py` no worse than 0.97%; the cable band unchanged.
  6. `--pass-timings` shows ReflectionBudget running during a runtime spin and absent on parked runtime frames, in the garage and in the camp.
  7. Cost: A,B,B,A timings on the swing and on parked frames, in the garage and at Headland, before and after, at 1600x900 and at 2560x1600. The lamp pool (+2.1 ms) and the one-pass emitter pick are recorded on their own. Arm C spends rays on purpose; the cost goes to the owner with the picture.
  8. With the frame filter's memory off, young reflection pixels settle within the parked noise floor (fix 1 no longer leans on TAA), or the remaining reliance is written down (D9).
  9. The owner, live on `watch_arm.py spin 0` with no time limit: fixed. Then commit everything (`Orbiter.cpp`, `Slider.cpp`, `spin_measure.py`, `spin_timings.py`, `speckle_count.py`, `ghost_map.py`, `watch_arm.py`, `include/reflection_emitters.glsl`, the rebuilt module, the docs) and delete the captures. `sharp_study.py`, `drift_field.py` and `render_authored.py` exist only in the RT-24 session's scratchpad; they are copied into `tools/scripts/garage` in the same commit. If the scratchpad has been cleared, floor sharpness is spin_measure's own "sharp" score, which every later gate uses anyway.
- **Size.** 5-8 sessions in total; about 1-2 remain.
- **Closes.** RT-24, framegraph-s1, ISS-40 (the motion test), CORE-08 (the test-mover half, through `Slider`). Also the measured arms of REFL-01, REFL-03, REFL-04, REFL-05 (the camera-motion check), REFL-07, REFL-10 and direct-s2, and the diagnostic half of REFL-06 (done early: every emitter aimed at); their structural parts are later items.
- **If a symptom survives every arm.** A cause inside the reflection chain that one rule fixes is patched here first. Otherwise RT-24 is committed with that symptom recorded against the item that removes it (D17): RT2-28 for the frame filter's depth test, RT2-24 for the direct highlight's memory, RT2-23 for the structural ghost fix.

### RT2-1 · Fail loud, validate clean, and count VRAM (parts 1, 2a and 2b built 2026-10-13)

- **In plain words.** Today a shader that fails to build can silently swap in a different picture, and the engine does not know how much video memory it uses. This item makes every failure visible, makes the bridge run without Vulkan warnings, and adds a VRAM report by category to the HUD and to every benchmark. Nothing on screen changes unless something breaks.

> **Part 1 built, 2026-10-13** (mapped first by five read-only agents: `build/rt2-1/map.json`).
> - *Fail loud.* `RHI::Failures` counts and names every GPU-layer failure next to ShaderCompiler's: a
>   pipeline the driver did not build (the command list then refuses to bind it and skips its draws, instead
>   of binding a null handle), a descriptor write the shader's layout lacks (refused, never sent -- the Intel
>   crash), a push past its block (sent up to the block's end). A measuring run with any failure exits 3,
>   **writes no screenshot and prints no benchmark numbers** ("NO NUMBERS -- ..."), so the 107 scripts that
>   never read the exit code still notice -- that, not a lost log line, is how RT-11's failure escaped (the
>   harness piped the log and ignored exit 3). A banner over the editor and the runtime names the failures. A
>   failed lit recompile clears the lit shaders and pipelines instead of leaving the old variant drawing.
>   Verified headless: a staged compile error gives exit 3, no PNG, "NO NUMBERS", the variant named. Not
>   built: painting each failed pass a fixed colour (the banner and the refusals carry it).
> - *Validation fixes.* The water lamps' push block has its `vec4 Trace` (the 112-into-96 error; this fix
>   was NOT on the wip branch, as this item claimed -- only the sampler fix was); `PointSampler` is nearest
>   between mips; the swapchain is a copy source where the surface allows (a capture is refused, loudly,
>   where not); acceleration-structure scratch is aligned to `minAccelerationStructureScratchOffsetAlignment`.
>   The layer's notes now log as `[Vulkan note]`, so any `[Vulkan]` line is a problem.
> - *Ray counters* only under `--ray-counters=on` (and any `--debug-view`): pixel-identical off against on
>   (garage, Headland); the counters cost 0.61 ms in the garage and 0.10 ms at Headland (palindromes),
>   which every frame now gets back. scenetest turns them on for its own counter checks.
> - *Instruments.* `tools/scripts/vulkan_messages.py`; the validation pre-pass in `bench_night.py`,
>   `bench_scale.py` and `watch_arm.py`'s arm check; `check_gpu_lit.py --rays`; `ray_counters_ab.py`.
> - *Text and dead code.* RTGI and AO resolution texts (incl. the editor tooltip) now match the code; the
>   instance-mask finding; HANDOFF's shader-cache claims; the emitter-owner comment. `LiveRayGpuMs` and the
>   whole chain that only fed it (RayGpuScope, ~8 GPU timestamps a frame) deleted. The bake reader's fixed
>   1 GB cap replaced by "the payload is exactly the rest of the file" (callers already check the size their
>   dimensions imply).
> - *Gates met:* the four shots (garage, Headland, Glitter, camp) byte-identical to c4b66f8; scenetest green.
> **Part 2a built, 2026-10-13: validation is silent.**
> - *The PRESENT_AFTER_WRITE message was not the engine's.* Traced through the API dump to a legacy render
>   pass recorded by third-party **implicit layers** -- RivaTuner's overlay here; the OBS hook, the Steam
>   overlay and RenderDoc's are the same kind -- which draw into the swapchain image just before the present,
>   outside the engine's synchronisation. With `--validation=on` the engine now switches third-party
>   implicit layers off (`VK_LOADER_LAYERS_DISABLE=~implicit~`, the GPU makers' own kept; never over a
>   choice already in the environment; `RV_KEEP_IMPLICIT_LAYERS=1` keeps them all). The loader's own
>   reports log as `[Vulkan note]`.
> - *A use-after-free it had been hiding.* `VulkanResourceSet` kept a list of every write since its last
>   commit; the scene's ten lit-family sets are filled every frame but committed only when their family
>   draws, so the lists grew frame after frame and a late commit sent the handles of light-grid buffers
>   (`cellIndices`, `worldCellIndices`) that had grown and been freed since. Now a set keeps **one record
>   per binding**, the record **holds its resource** (a descriptor never outlives what it names), and a
>   frame's set is written whole whenever it is behind the records -- never written, or written before
>   one changed; an unchanged set is not rewritten at all.
> - *The meshlet lit path* drew alpha-masked runs without their cutout (the showroom's graffiti decals as
>   solid dark sheets: 11% of the frame) -- it now refuses masked runs, the rule its shadow twin already
>   had -- and never wrote the G-buffer object id its fragment reads (now written).
> - *Gates met:* the four shots byte-identical before/after and silent under validation; `check_gpu_lit.py`
>   green (all three paths 0 px); scenetest green. `--gpu-lit`'s texts no longer call it unfinished.
> - *Part 2b, VRAM by category.* Every lasting GPU allocation is counted under a category when made and
>   taken off when the allocator frees it (`RHI/RHIMemory.h`; `BufferDesc`, `TextureDesc` and
>   `RenderTargetDesc` carry a `Category`, Auto choosing from the usage; `TemporalHistory` marks histories).
>   `VK_EXT_memory_budget` is on where offered, so the driver's own usage and budget are read through VMA.
>   The benchmark report ends with three `vram` lines, and the editor's Statistics panel has a Memory
>   section. Measured at 1600x900 (driver usage / engine allocations, MB): garage 1994 / 1664 (textures
>   249, targets 617, **histories 716**, geometry 31, uploads 7, static BLAS 44); Headland 2857 / 2621
>   (textures 527, targets 950, histories 841, geometry 147, static BLAS 150); Glitter the same scene;
>   camp 1227 / 905 (targets 390, histories 441). The categories sum to the allocator's own count exactly
>   on all four (a gap would print as "other"), and the allocator's blocks sit under the driver's figure;
>   the rest is the driver's (swapchain, pipelines, descriptor pools). Pixels byte-identical to before;
>   validation silent; scenetest green. Not built: histories *per view* (one line for all views; the
>   runtime has one) -- the editor's two viewports are not split. **Finding for RT2-19:** the histories
>   are the largest category in the garage, larger than every render target together.
> - **Fixed 2026-10-13 (the owner's call):** `check_gpu_lit.py --rays` -- gpu-lit-off and meshlets pass (23 px
>   at 1 level), depth-sort-off misses (106 px, one at 4 levels, on the poles' edges and their floor
>   reflections). **It is the reflection probe's capture, and only that.** The "Showroom Probe" (0, 1.6, 18)
>   is drawn once, between frames 1 and 2, in one forward pass with no prepass and no blending -- so the
>   draw order can change a texel only where two different objects' surfaces sit at exactly the same depth,
>   and with `GreaterOrEqual` the last one drawn wins. Such ties exist on the ceiling as the probe sees it
>   (three faces, ~2,500 texels, all within 0.25% of each other: two near-identical surfaces). The probe then lights every frame, and the screen-space reflections and TAA carry the
>   rounding-sized change into the picture; with rays off the same probe change stays under one display
>   level. Proof: with the probe drawn in one fixed order in both runs, the runs match exactly (0 px at
>   frame 60, rays on); with only the probe's order changed, all 79 px come back. Measured on the way:
>   frame 1 identical everywhere; the G-buffer, depth, direct light, AO, indirect, budgets and measured-change
>   records byte-identical on every frame; the blended order irrelevant. Not an engine path defect.
>   **The surfaces, named** (the probe made to write object ids, both orders compared; temporary code,
>   removed): the garage model's ceiling beam grid is placed twice -- `Plane.003` (z 0.84..29.99) and
>   `Plane.010` (z -27.54..1.62), one mesh, one material, both at y 6.5819 -- so a 49 m x 0.78 m strip of
>   beams (z 0.84..1.62) exists twice in the same place: ~975 of the disputed texels. The rest are faces
>   flush with those beams: `pipe.020` (~450), `pipe.008` (~140) and the pillars' `Cylinder.013.1` (~130).
>   **The fix, in the scene:** `Plane.010` moved by (0.5, 1.5, 1.0) mm -- chosen so every face direction
>   in the beam grid (the axes and the diagonal braces, normal (+-0.789, +-0.614, 0)) parts by at least
>   0.5 mm; a (1, 1, 1) mm move left the braces 0.175 mm apart. A whole-model scan for same-facing,
>   coplanar, overlapping faces between different objects (`coplanar_all.py`, in the scratchpad) found
>   17.5 m^2 before and none for the beams after. The pipe and pillar ids had come from blended texels,
>   which mix ids; the scan finds no coinciding faces there. **Now:** `check_gpu_lit.py` green with and
>   without rays (rays: all three paths 35 px at 1 level, the GPU path's own; depth-sort-off equals
>   gpu-lit-off exactly); scenetest green; the garage shot changes in 6% of pixels, 2,373 by more than
>   two levels (max 53, a pole's rim beside a tube) -- the strip now shows one copy, and the rays' noise
>   lands slightly differently. Left as found: neighbouring ceiling light bars share small faces (4-33 cm^2
>   each, 38 pairs) -- too small for the probe to have shown, not changed without the owner.
> - **The three runs that could fault the GPU, done 2026-10-13 (each asked for):**
>   - *The garage on the Intel iGPU* (`--gpu=intel --validation=on`): every traced effect on, no crash,
>     **no Vulkan message**, the picture right.
>   - *ISS-36's device loss* (rays on, traced reflections off -- "reliably lost the device" 2026-08-29):
>     **gone.** 290 frames of the garage, clean exit; under validation, no message.
>   - *GPU-assisted validation on the water* (`--validation=gpu`): on the bridge cut to its sea, its 191
>     lights and its geometry -- the eight irradiance volumes left out, the likeliest cause of the old
>     load-time device loss -- at 640x360: no loss, and **no out-of-bounds read in any water shader**
>     (the open question). It found one elsewhere: **the lamp glow bound `lightCount * 80` bytes of a
>     96-byte record** (stale since RT-7 added `Extent`), so every light past 159 read beyond its range
>     -- and on this NVIDIA card those lamps had no halo. The glow now binds the whole buffer (its
>     shader reads only below the count); the check finds nothing after. Headland and Glitter gain the
>     missing halos (32k and 39k px, all brighter: the near tower's base, a few on the deck); the garage
>     and the camp are byte-identical (fewer than 160 lights). GPU-assisted validation on the full bridge,
>     volumes included, is not retried.
>
> **Found on the Intel iGPU (RT2-9, 2026-10-13; `--gpu=intel`).** Two defects NVIDIA's driver lets pass:
> - `VulkanResourceSet::Commit` writes descriptors for bindings the pipeline's layout does not declare (a set
>   filled by the source's bindings, while the reflected layout lacked some): Intel's driver crashed inside
>   `vkUpdateDescriptorSets`. General form: a write to a binding the layout lacks is refused and reported once
>   per pipeline and binding, never sent.
> - `water.rvshader` faults the GPU (device lost in the see-through pass, many page faults) when optimised by
> spirv-opt `-O` -- bisected to that shader alone, NVIDIA, the bridge. Either the optimiser miscompiles it or
>   the shader reads out of bounds and the unoptimised build survives it; GPU-assisted validation lost the
>   device before reporting. Find which with the layers on the unoptimised shader first (a read past an
>   array would hit other vendors without any optimiser). Every run of the faulting form resets the driver
> and blanks the owner's screen: ask before running it.
> Gate: the garage and the bridge run on the Intel iGPU with the validation layers silent.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** The bake file
> loader rejects a file above a fixed "sane" size. General form: check the payload against the size
> its own header implies, and against the file's length.

> **Changed by the comparison (2026-09-25).** The VRAM categories split the ray-tracing memory
> (static BLAS, dynamic BLAS, TLAS, scratch), so that RT2-35's compaction and pooling can be proven.
> The scratch fix aligns to the device's `minAccelerationStructureScratchOffsetAlignment`. The
> benchmark's validation pre-pass runs with sync validation on, because every later render-graph step
> leans on it. Three stale texts found by the verification join the stale-text list.

- **What is wrong now.**
  - A shader variant that fails to compile makes its pass return early, or makes a different variant run. A pipeline that fails to build leaves a null handle in Release.
  - Only screenshot and benchmark runs exit with code 3. The editor and the owner's live arms are not covered, and one failure once escaped the log entirely.
  - The bridge has two Vulkan validation errors: a 112-byte push into a 96-byte block (WaterAccumulateLamps), and integer images sampled through a linear-mip sampler (DirectWaterShade). Only the sampler fix existed, on `wip/2026-09-21-rt10-and-sea`; the push fix was planned, never written (corrected 2026-10-13; both fixed in part 1).
  - The screenshot path copies from a swapchain image created without the usage flag that allows it. Acceleration-structure scratch memory ignores the device's alignment rule.
  - Push-constant and buffer sizes are never checked against the sizes the shader compiler already reflects.
  - The ray counters, a measurement instrument, are compiled into every ray-traced frame.
  - The engine knows only the card's total memory (`VulkanDevice.cpp:707-723`), yet several later exit gates ask for VRAM before and after.
- **What changes.**
  - A missing shader or pipeline is loud in every run mode. `CreatePipeline` returns null on failure, the pass paints a fixed debug colour, the HUD names the variant, and pipeline failures count toward exit code 3. First, find out how the RT-11 failure escaped the log.
  - Re-land the two bridge validation fixes. Add TRANSFER_SRC to the swapchain usage where the surface supports it. Align acceleration-structure scratch to `minAccelerationStructureScratchOffsetAlignment`.
  - In debug builds the RHI checks push-constant sizes, bound buffer ranges, sampler and format pairs, and unwritten bindings against the reflected shader.
  - The benchmark scripts first run a short `--validation=on` pass per scene, with sync validation on, and refuse to report numbers while any Vulkan message remains.
  - The counters compile only under `--ray-counters=on`, and are taken in a separate run so that benchmarks time the shader that ships. Their cost is re-measured first: the recorded 0.26 ms and 0.85 ms predate RT-1 and are stale.
  - **VRAM is measured** (the measuring half of the memory budget; degrading under a budget stays in RT2-19). The engine asks the driver for its budget (`VK_EXT_memory_budget`, through VMA's budget query) and reports VRAM by category in the HUD and in every benchmark report: static BLAS, dynamic BLAS, TLAS, acceleration-structure scratch, histories per view, short-lived targets, textures, geometry and uploads.
  - Fix stale text: the `--gpu-lit` help, RTGI High's resolution (it is up to 4 rays per traced pixel at a quarter of the pixels, the 4 a per-tile ceiling), the traced AO resolutions, HANDOFF's shader-cache claim, `RT-SERIES-2-FINDINGS.md:4392` (it says the instance masks are ignored; they are used), and the comment at `Renderer3D.cpp:5621` ("at most sixteen owners", stale since RT-24 removed the cap). Delete `FrameProfiler::LiveRayGpuMs`, which nothing calls, and its comment.
  - Run `check_gpu_lit.py` with rays on. Re-check the recorded device loss with rays on and reflections off.
- **Owner sees.** Nothing, until something breaks; then a coloured pass and a banner instead of a believable wrong picture. A VRAM figure in the HUD. With the counters off, some time back per frame (re-measured).
- **Touches.** `VulkanDevice.cpp` (and the budget query), `VulkanPipeline.cpp`, `VulkanCommandList.cpp` (`PushConstants`), `VulkanResources.cpp` (scratch), `Renderer3D.cpp` (pass guards; `PointSampler` at 1643-1649), `water_accumulate.rvshader` (`LampParams`), `ShaderCompiler.cpp`, `Entrypoint.h`, `pbr_fragment.glsl` and `PostProcess.cpp` (the counter define), `RayCounters.cpp`, `EngineConfig`, `FrameProfiler.cpp`, `RenderSettings.h`, `PostSettings.h`, `BakedLighting.cpp` (the file-size check), `tools/scripts/bench_night.py`, `bench_scale.py`, `watch_arm.py`, `docs/RT-SERIES-2-FINDINGS.md` (line 4392).
- **Scope.** Patch. **Depends on.** RT2-0 committed. It changes timings (the counters) and the bridge's picture (the validation fixes), so it must not mix into RT-24's uncommitted measurements. **Risk.** Low.
- **Exit gate.**
  - A deliberately broken variant (a staged compile error) shows the banner in the editor and makes the benchmark exit 3, naming the variant.
  - `--validation=on` runs of the garage, Headland, Glitter and the camp, with sync validation on, print no Vulkan message.
  - With the counters off, every scene is pixel-identical to counters-on apart from the HUD (diff 0), and an A,B,B,A run shows the time saved.
  - VRAM by category reported for the four shots, with the total agreeing with the driver's budget query.
  - `--screenshot` output is byte-identical to before.
  - `check_gpu_lit.py` is green with rays on.
- **Size.** 2-3 sessions.
- **Closes.** rhi-s1, GPU-13, GPU-15, rhi-s2 (the checking half), ISS-24, ISS-25, ISS-36 (the device-loss re-check); the measuring halves of GPU-09 and CORE-05; parts of ISS-6, GI-13, CORE-10 and frame-13; the comparison's finding E-N5 (section 8).

### RT2-2 · Instruments that can settle questions (2a built, 2b built and checked 2026-10-13)

- **In plain words.** Most later decisions are "which picture is right" or "does this signal work without TAA". This item builds the instruments that answer those questions: a reference renderer that shares none of the shortcuts it judges, one command that renders and diffs every test camera, a command that proves a signal stands on its own, and new test scenes. Nothing on screen changes.

> **2a part 1 built, 2026-10-13.**
> - *Settled, not a frame number.* `TemporalMemory` (Renderer): every pass that blends into a history
>   notes the most frames it keeps (the shared accumulator's Memory, Pair and Blur frames; TAA 64; the
>   reflection accumulate's settle count 32; GI denoise 16; the sea's lamp cap). The three that lived as
>   shader constants are defined there once and handed to every shader as defines (a new
>   `ShaderCompiler::SetGlobalDefines`). `--screenshot-frame=settled` captures at the first frame the
>   camera has stood still -- with the scene's bakes, probes and solves done (`BakedLightingSettled`) --
>   for longer than the longest memory in force; a scene that never settles fails loudly (exit 3, no
>   file) after 20,000 frames. The garage, the bridge and the camp settle at frame 66 (64 in force).
> - *The measurement camera.* `--camera` now holds the scene's camera every frame (its scripts removed
>   from the loaded copy), and `--camera-swing=deg[,rate[,seconds[,delay]]]` is RT-24's Orbiter swing
>   on any scene; settled waits for the swing to end. Each capture logs the pose it was taken from, as
>   `--camera=...`, so a shot a script frames (the garage's, the camp's) can be repeated and swung.
> - *`--frame-cap=FPS`:* sleep, then spin the last 2 ms; holds 33.333 ms exactly at 30 (the garage
>   uncapped: 20.4). Its effect on the warm-laptop drift is still to measure.
> - *`tools/scripts/baseline.py record|check`:* garage, Headland, Deck, Glitter and the camp, parked
>   (settled) in all six AA modes and over a 60-frame 15-degree swing, against a stored baseline in
>   build/baseline/NAME: a count per scene and a diff picture per differing frame. Swing frames are
>   compared by pixel hash, every tenth kept as a picture (all of them would be ~600 MB).
> - *Gates so far:* the four shots byte-identical before and after; scenetest and `check_gpu_lit.py`
>   green; parked frames reproduce their baseline exactly.
> - **Found, open (blocks 2a's "reproduces its own baseline" gate):** with the scene's clock running
>   (1/60 s), two runs of one build differ in the ray-traced reflections -- tens of thousands of
>   speckles on the floor by frame 123, up to ~40 levels. Not with traced reflections off, **not with
>   `--frames-in-flight=1`**, not when every frame is captured (each capture waits for the GPU), and not
>   with the clock pinned near zero (nothing changes frame to frame). So the reflection path reads
>   something the next frame in flight is changing, in a way the layers do not see: sync validation
>   with the clock running reports nothing, every CPU-written buffer it uses is per frame slot, and its
>   atomics are the counters' only. **Narrowed further (same day):** identical through frame 66 -- the
>   frame everything first settles -- and differing by frame 72; already present at dc64669, before
>   today's resource-set change (smaller there). One frame in flight is repeatable every time (3 of 3);
>   two never are. A full GPU barrier at the start of each frame does NOT make it repeatable (one lucky
>   pair said so; three repeats did not), so it is not a missing barrier between two frames' GPU work
>   either: what remains is the CPU, with two frames in flight, changing something a frame's GPU work
>   still uses, or a decision taken on the CPU from timing. A dump of every history at frame 70 (one
>   GPU wait) kept both runs identical to frame 72, which fits a timing race. Open; parked until the
>   owner chooses.
> - **Cause found, same day, later: not the CPU and not RageV's code -- for a few frames the driver
>   runs a differently compiled copy of a shader.** (The "CPU-side" reading above was wrong.)
>   Everything the CPU hands the GPU is byte-identical in the two runs through frame 120: every buffer
>   upload, push constant, draw and dispatch, every pipeline bound and every resource each descriptor
>   set names. GPU-assisted validation over frames 1-90 reports nothing. The first pass to differ is
>   DirectTrace, somewhere in frames 64-76. Run twice in one frame it is bit-identical to itself, and
>   every bit of every input to its position reconstruction (depth, the snapped coordinate, the
>   jitter, the inverse projection) is identical between the runs -- yet `ndc * invProjection * depth`
>   comes out different in ~147,000 pixels. The same product computed again in the same invocation
>   under `precise` is identical (2 of 2), while the plain copy still differs; the pipeline is one
>   object all run. So in one run the GPU executes the same SPIR-V compiled with other fusing and
>   reordering choices, which Vulkan allows for any arithmetic not marked `precise`. NEE at
>   reflection hits magnifies those last-bit differences into the speckle (reflection NEE off: 90 px,
>   max 1 level). It needs the GPU to start a frame straight after the last: a 5 ms idle gap before
>   each submit (frames 20-123 checked), `--frame-cap=30` (3 of 3 pairs) or one frame in flight makes
>   every run identical. Marking every float operation NoContraction at load does not cure it (other
>   instruction choices still differ), so there is no engine-side fix for bit-repeatable runs at full
>   speed. **Owner's decision (2026-10-13):** RT2-2a's harness runs under `--frame-cap` (below).
>   The Intel iGPU differs too, but differently: ~4,000 px, max 8 levels, even at one frame in flight
>   or with the gap -- a separate source that does not depend on timing; not looked into.
> - **Fixed on the way:** `reflection_blur.rvshader` left `o_Variance` unwritten on its two early
>   returns, so that lane held whatever the register did and differed between runs every frame.
>   Nothing reads those texels, so the picture is unchanged (the four shots byte-identical), but every
>   comparison of the frame's targets is clean now.
> - **Recorded, not changed:** GpuCull's object and template tables are one buffer shared by both
>   frames in flight, rewritten every frame while the previous frame may still read them. The bytes
>   are the same every frame today, so it is harmless; a scene whose objects move would race.
> - **2a done, 2026-10-13.** `baseline.py` runs every capture at `--frame-cap=30` (the idle gap the
>   driver issue above needs) and refuses to run on battery: on battery the GPU is too slow to finish
>   a garage frame inside the cap's 33 ms, the gap disappears, and the garage swing differed in all 60
>   frames (its MSAA parked frame by 154 px, max 2). On mains: record then check, all five cameras
>   in all six AA modes and over the swing, **330 of 330 frames identical**; a deliberate one-pixel
>   change in the tone map (+0.02 at one texel) was flagged as exactly 1 px, 5 levels, with its diff
>   picture, and reverting it put the check back to identical. **The cap and the drift:** eight
>   back-to-back garage benchmarks each, from the same 60-62 C start: uncapped, GPU work per frame
>   19.01-19.41 ms (spread 0.40 ms) with the GPU reaching 84 C; capped at 30, 18.55-18.79 ms (spread
>   0.24 ms, 0.10 ms over the first seven) with the GPU at or below 74 C. The cap keeps the laptop
>   about 10 C cooler and halves the spread; a capped run's GPU time is lower than an uncapped one's
>   (cooler clocks), so A/B timings compare capped with capped. The "up to a millisecond" drift was
>   not reached in this uncapped series (0.40 ms over eight runs).
> - **2b step 1 built, 2026-10-13: the reference renderer's skeleton.** `--reference-render=N` runs a path
>   tracer (`reference_trace.rvshader`) at a fixed pose: one path per pixel per frame into a full-float
>   running mean that takes the frame's place before bloom and the tone map; `--screenshot-frame=settled`
>   means "N samples", `--capture-signals=reference` writes the mean as floats (alpha counts samples that
>   came back not a number). Its own shading on the lit shader's model (GGX, Smith-Schlick, Schlick
>   Fresnel, diffuse x (1 - F)(1 - metallic)), GGX visible-normal sampling, one lamp per vertex by
>   next-event estimation, the sky and glowing surfaces reached by the paths, Russian roulette, no clamp.
>   Owner's choices: baked AO maps ignored; a lamp with a glowing mesh counted both ways until RT2-11.
>   The mean restarts whenever anything the paths read changes (lights, ray instances and their
>   structure, materials, emitters, sky): the first frames, while the scene loads, had put one bad
>   sample in every pixel. Garage, 1600x900: 1024 samples in 28 s; the mean is flat from one sample
>   (1.0867) to 64 (1.0869). Still to come: the full material (step 2), the lamps' real shapes and
>   glowing-mesh sampling (step 3), the furnace and garage checks (step 4).
> - **2b glass built, 2026-10-13 (owner: the path tracer showed no car-glass reflections).** Blended
>   materials join the ray structure only under `--reference-render`, on a mask bit no real-time ray
>   carries (`kMaskTransmissive` 0x04; `RV_RAY_MASK_SCENE` is now static | moving, so real-time frames
>   are unchanged: the four shots byte-identical, scenetest and check_gpu_lit pass). The reference walks
>   the structure itself (no thin-member fade, no fitting let through). A pane is thin glass: its opacity
>   (the blend alpha) is ordinary surface; the clear rest reflects by the dielectric's Fresnel through
>   GGX at the pane's roughness, F0 = 0.08 x specular, and passes the remainder straight through. A
>   lamp's shadow ray through panes keeps the product of their transmittances, each crossing counted
>   once. Untinted, as the frame's glass is (RT2-31 adds tint). Garage close-up of the car at 1024
>   samples: the windscreen and headlight covers reflect the tubes. Seen, for step 4 to measure: the
>   real-time floor mirrors the car more sharply than the reference does.
> - **2b step 2 built, 2026-10-13: the full material at every path vertex.** The lit shader's model
>   term by term: normal maps through the raster's own tangent construction, split out as
>   `TangentFrameOf` and fed the triangle's edges (the same frame as the screen's derivatives on a flat
>   triangle; a three-agent review checked the algebra), the same parallax march (`ParallaxMarch`,
>   `ParallaxLayer`, now shared), the macro variation with its normal flatten, terrain's four layers
>   through the weight map with the cliff projection (handed to the reference alone: a per-row table
>   and the chunks' `LayeredParams` at set 3, filled only under `--reference-render`), anisotropic GGX,
>   clearcoat, sheen and wrap. Sampling: a mixture of GGX visible normals (anisotropic), the coat's
>   lobe and the wrapped cosine, weighed by the whole mixture density. Textures at a ray cone's level
>   (Akenine-Moller 2021), the cone's angle the pixel's over sqrt(samples), never below an eighth
>   (pbrt-v4); alpha at level zero. Checked: the headland's first-hit albedo with all four layers shows
>   the beach sand and the cliff rock, layer 0 alone is brown everywhere; normal-map relief on the
>   garage pillars and floor; zero invalid samples at 1024 on the car. Real time: headland, glitter and
>   camp byte-identical; the garage differs in 11 dark pixels by one level, all from the new guard in
>   `TangentFrameOf` -- a triangle with no uv area made a not-a-number tangent in the frame too (a frame
>   with an absurd value there moves only those pixels), now a zero frame. The stored `after_garage.png`
>   gate image is replaced by the guarded frame (the old one deleted with the other captures).
>   - *Found by the review and fixed:* the scene clock now stands still in reference mode (the bridge's
>     flashing tower beacons restarted the mean every frame, so 512 samples never settled; now 18 s),
>     with zero-length fixed steps so scripts are still made; a posed caster's per-frame buffer address
>     no longer restarts the mean; a not-a-number sample is counted and left out of the average instead
>     of averaged in as black; a pane is split by one Fresnel against its triangle for both the path and
>     the light ray; a pane glows by its opacity share; the height map reaches the shared march as a heap
>     slot, because a heap texture passed as a sampler2D parameter loses nonuniformEXT in glslang (the
>     real-time march had become a merged-run hazard on hardware that needs the decoration);
>     `RAY_INSTANCE_LAYERED` 128.
>   - *Found by the review, a frame defect, fixed with the owner's OK (2026-10-13):* `DistributionGGX`'s
>     1e-9 divisor floor still capped the peak below roughness 0.065 (at the 0.045 floor to a twentieth
>     of its value, about 60% of the lobe's energy; RT-21's comment that it capped nothing real was wrong
>     there). The floor is now 1e-30, past the peak of any roughness above 0.0003, and the reference
>     calls the same function again. Shown to the owner on the garage's chrome pole: a lamp's highlight
>     becomes a small bright point instead of a dim smear. Real time: headland and glitter
>     byte-identical; the garage changes in 5.6% of pixels and the camp in 4.7%, almost all by one or
>     two levels (the stored gate images are replaced). Left alone: `prefilter.rvshader`'s own
>     DistributionGGX floors at 1e-6, which only steers which mip a prefiltered environment sample
>     reads (the raster recipe's probes), noted here for RT2-21b.
>   - *Found by the review, decided by the owner (2026-10-13):* which Smith term is the material's. The
>     light loop uses Schlick's remap k = (r+1)^2/8, the frame's reflections (the EnvBRDF table)
>     k = r^2/2; at roughness 0.045 and a grazing view the two differ by about 2x. Shown both in the
>     reference on the garage's wet floor, the owner chose the exact one: the reference now uses the
>     height-correlated GGX Smith term (Heitz 2014; Unreal's, Frostbite's, Filament's), anisotropic
>     where the lobe is, for every light and bounce. With it the car's reflection in the puddles is 58%
>     brighter and the frame as a whole 5-7%, which is the gap step 2 had seen between the real-time
>     floor and the reference. RT2-21b moves the frame onto the same term (its "Smith swap" arm).
>   - *Still missing, owner's report 2026-10-13 ("the bridge reflection looks wrong with path
>     tracer"):* the sea. A WaterComponent is not in the ray structure, so the reference's rays pass
>     through it and light the seabed. Water is the next item (its own plan to the owner).
> - **2b water built, 2026-10-13 (owner's choice: "the engine's water, traced").** Under
>   `--reference-render` each body of water joins the structure as its displaced surface:
>   `water_positions.rvshader` runs the same `WaterSurfaceEval` the water pass's vertex stage runs, once
>   (the clock stands still), writing the positions the structure is built from and, beside them, the
>   per-vertex normal, Jacobian and crest the vertex stage hands its fragments
>   (`RayShadows::AddWater`, mask bit `kMaskWater` 0x08, a reference-only water table at set 3). At a
>   hit the surface is the water pass's: the wave normal plus the two detail ripples faded with the
>   camera's distance, the wet-crest smoothing, the footprint roughness toward Cox-Munk, and the
>   shallow-to-deep colour by the water behind the surface along the view. Its light: the Beckmann lobe
>   about the wind for the reflection; for what enters, the refracted ray (eta 0.7519) to the bottom
>   through Beer-Lambert at the gradient depth, and the lit body colour taking over as that dies.
>   Lights' rays pass through the surface as the frame's do, and so does a path once under water (the
>   frame absorbs along its one refracted ray only). Foam and caustics left out at first (in the option the owner picked; added 2026-10-15, below). Headland at
>   1024 samples in 29 s: the bridge's light track and the south tower's reflection on the water, close
>   to the real-time frame in place and shape; the reference's track is broader and brighter, for step
>   4 to measure. Real time: the four shots byte-identical, scenetest and check_gpu_lit pass.
>   - *Two departures from the frame's arithmetic, recorded as frame findings:* the frame adds the
>     refracted scene at full strength beside a full Fresnel reflection (no 1 - F on what enters), which
>     the reference applies; and the water lobe's masking is height-correlated (Lambda = 1/G1 - 1), the
>     owner's exact masking for every lobe, where the frame multiplies the two G1.
>   - *Foam and caustics added, 2026-10-15 (owner: "add both").* The water pass's foam at a hit: the
>     trails read from the engine's own trail buffer at the grid's rest position (empty while the clock
>     stands still, as at the scene's first instant), the live crest foam where the surface folds, the
>     contact foam within a metre of anything the water meets, each through the foam pattern at the
>     body's wavelength and scaled by its foam dial; it whitens and roughens the surface and hides the
>     bottom (transmittance times 1 - foam). The caustic web (the frame's WaterCaustics, term for term)
>     multiplies what the path sees of the bottom where the refracted ray lands. Checked with
>     `--reference-debug=albedo`: at Headland foam covers 0.02% of the sea, all of it along the
>     headland's shoreline (contact foam); open water carries none at the first instant, as the frame's
>     live crest term is rare by design. The trail buffer is a ping-pong pair swapped every frame, so
>     the reference's restart key hashes the water without its slot (the mean never settled until it
>     did).
>   - *The sea against the frame, Headland (2026-10-15, owner: "the path traced headland view doesn't
>     look right").* The reference shows the deck lamps' reflection as one broad bright band between the
>     bridge and the headland, where the frame shows separate narrow streaks. Measured over that band
>     (linear radiance): the reference 0.0247, the frame 0.0090 -- 2.7x; over the whole sea 1.7x. Not
>     the seabed (with the water made opaque the band is unchanged) and not the glowing lamp heads (with
>     them off it is 0.0246); it is the 120 sodium deck lamps (with the lamps off, 0.0016). The
>     reference's estimate holds: the lamps found by their samples alone give the same band as the
>     weighted pair (0.0247 both), so its water lobe's sampling and density agree. The frame's sea
>     therefore returns about a third of the deck lamps' light in that band, through its own water-lamp
>     passes (WR-16 S4b) and their streak model -- where the shortfall is lost is not yet found. It
>     belongs to RT2-32 (ISS-32, the sea's lighting gaps), which can now be judged against this.
>   - *Found the same day (owner: "compare it with real picture and make both come closer").* Real
>     night photographs settle the look: from Battery Spencer (about 90 m up, our Headland camera) the
>     deck lamps lay one smooth sheet on the bay; from the beach at Kirby Cove the same lamps lay separate
>     columns. Both follow from the sea's own lobe -- at grazing view a glint is roughly 1/elevation
>     times taller than wide -- and the reference, with every lamp on, draws the sheet (with every eighth
>     lamp on, separate streaks). Two frame-side causes, each measured against it, both fixed:
>     1. *The streak frame* (in the lit shader's water branch, ShadeLamp and DirectTerm, and their
>        scores): a sized lamp's lobe turned to the view, 3x longer toward the camera at half the
>        renormalisation, held at the narrow Cox-Munk width across. With it switched off the frame's
>        streaks merged into the reference's sheet. Removed; every lamp takes the wind-frame lobe, a
>        sized one widened by its angular radius as before.
>     2. *Soft shadow rays ran into the lamp's own fitting.* TraceShadowSoftFromMasked aimed at a disc
>        point and ran the ray on to the centre plane (past it, by design, to dodge the source's far
>        edge), so a lamp head or housing modelled inside the source radius blocked it. It hid 22-25% of
>        the deck lamps' light from the sea. The ray now ends where it meets the source's sphere, as the
>        reference's does. With lamp shadows off in both, the two already agreed (0.97-1.0 per row).
>     After both, the frame's lamp light on the sea is within 6% of the reference's, row for row. Gates:
>     camp byte-identical; garage 0.83% of pixels move more than 8 levels (the shadow fix, a little
>     brighter); Headland and Glitter change (new baselines); scenetest OK; check_gpu_lit OK; Vulkan
>     validation 0 messages. Glitter (2.5 m up) still lays a separate pool per lamp.
>     *Still apart from the photographs, the owner's call:* the deck lamps' Range of 600 m. Each lamp's
>     mirror point on the sea is about 550 m from it, so the window leaves the true reflection a few
>     percent and the sheet hugs the bridge, where the photograph's starts further out and reaches the
>     foreground. At 3000 m (a test copy of the scene) the sheet spreads toward the camera like the
>     photograph; cost +3.7 ms at Headland (18.1 to 21.8, A,B,B,A), of which the sea's forward draw
>     (scene/Transparent) +2.3 ms.
>     *Raised to 3000 m the same day (owner)*, in the scene and in `make_bridge_scene.py`, and the
>     bridge re-baked for the new lighting (with each bake in place, Headland 17.2 -> 20.1 ms, two
>     runs each). *The +2.3 ms, measured (it is not the sea draw scoring its
>     cell, as first said -- that sampler already skips when the sea's passes run):* it goes with the
>     sea's mirror (`--water-ablate=reflection` removes it) and with the traced hit's light walk
>     (`--hit-lights=off` removes it). Each mirror hit weighs every lamp in reach to keep one (RT-11);
>     at 600 m the cull record dropped most of the 117 it walks, at 3000 m all pass. That is ISS-18,
>     RT2-12's light tree; the owner left it there ("leave it for now"). The other +1.4 ms is the
>     lamps' real reach in DirectTrace, the sea's choose pass, ReflectionTrace and GI.
>   - *Found on the way:* the camp never settled in the reference -- its characters are posed every
>     frame into the frame-in-flight's buffer and structure, and the geometry key took a fresh value
>     on any frame with a posed caster. The reference now has its own key
>     (`RayShadows::GetReferenceGeometryKey`): the instances by field, a posed caster by its bones.
>     Camp at 64 samples: 7 s.
> - **2b step 3 built, 2026-10-13: the lights.** Every path vertex takes one lamp sample and one
>   glowing-surface sample, and every path that has scattered can also meet both; the two strategies
>   are weighed by Veach's power heuristic (MIS). *Lamps* have the frame's shapes -- a sphere where
>   SourceRadius is set, a capsule with SourceLength along `Light::Axis` -- sampled by the cone a sphere
>   subtends and over the part of a capsule a vertex can see (its side within the tangent angle, its
>   end caps by area). Their brightness is the frame's total light: the frame says a lamp's size
>   changes only its image, so the shape emits a uniform 4 I / A over its area A (I / (pi r^2) for a
>   sphere, the point again from far off), with the range window and the spot's ramp taken from the
>   centre as the frame takes them. A lamp of no radius, and a sun, stay deltas. The shapes are not
>   geometry: they hide nothing, and the camera does not see them, as the frame's does not. A vertex
>   picks a lamp in proportion to its light there as a point (window, ramp, inverse square to the
>   centre, never nearer than its own size), so lamps out of range cost nothing. *Glowing surfaces*
>   are their own triangles: Renderer3D builds a reference-only table of every glowing instance (its
>   world bounding sphere, mean radiance, area) and its triangles in world space, rebuilt only when a
>   glowing instance changes; a vertex picks an instance by power over distance squared, a triangle by
>   area, a point uniformly, and reads the glow exactly as a path meeting it would. Glass, posed meshes
>   and the camera's own view are found by the path alone. Checked: the combined estimate against each
>   light found by one strategy only (lamps by their samples, glowing surfaces by the path) on the
>   garage at 2048 samples -- frame mean 1.60701 against 1.60704, 90% of 25-pixel blocks within 1.5%,
>   the largest block differences under two standard errors. Grain at 1024 samples is far lower on all
>   three scenes (garage 45 s, bridge 53 s, camp 22 s, zero invalid samples); the garage's tubes now
>   reflect in the wet floor as streaks. Real time: the four shots byte-identical, scenetest and
>   check_gpu_lit pass. The owner's D13 rule still waits for RT2-11: a lamp with a glowing mesh is
>   counted both ways.
> - **2b step 4 run, 2026-10-13: the reference against known answers, and the garage against the
>   16-ray truth.** Three checks, two of them kept as scripts.
>   - *The white furnace* (`check_reference_furnace.py`, scene `furnace_spheres.rage`): 22 spheres --
>     metal with F0 = 1 and white dielectrics, roughness 0 to 1 -- under a sky of radiance one, a
>     hundred metres apart. A convex surface under a uniform sky reads its material's directional
>     albedo, which the script computes independently (Monte Carlo with GGX's visible normals and
>     the cosine, two hundred thousand samples per N.V) and compares block by block against the
>     reference at 4096 samples, N.V per pixel from the new `--reference-debug=nv`. All 22 within
>     2% (worst 1.32%, the mirror 0.17%); the mean error, -0.15% to -0.30%, is the sphere being
>     triangles (directions below a facet's plane are refused). The furnace found one defect in the
>     reference, fixed: GGX's D was evaluated through `DistributionGGX`, whose divisor cancels near
>     the peak in float (the peak itself up to 3% off at roughness 0.045) while the sampler's
>     density used the tangent form, so a mirror sphere read 2% dark in places; the reference now
>     uses the tangent form for both. **Not 1.0, and that is the material, not the renderer**: the
>     single-scattering GGX loses energy at roughness (a metal at roughness 1 reads 0.31 head-on --
>     checked by hand: half the reflected directions go under the horizon at alpha 1), and the
>     model *gains* it at grazing on a smooth dielectric (a white sphere at roughness 0 reads 1.30
>     at N.V 0.2: the diffuse is weighted by 1 - F(H.V), which does not fall as the specular's
>     grazing energy rises). Both are RT2-21b's energy compensation to close; the frame shares them.
>   - *Lamp shapes against their point* (`check_reference_lamps.py`, scenes `lamp_point.rage`,
>     `lamp_sphere.rage`): a sphere lamp lights a matte floor exactly as its point would (a
>     Lambertian sphere's irradiance is the point's) -- every lit 8x8 block within 0.06%.
>   - *The parked garage against the 16-ray truth* (truth_test's camera; classes by the reference's
>     own `--reference-debug=surface`). Frame-mean radiance per class, truth / reference: matte
>     0.096 / 0.195, mid-rough 0.123 / 0.254, glossy 0.232 / 0.394, metal 4.74 / 4.93, glass 0.059 /
>     0.094. Attributed with arms on both sides: the lamps' direct light on matte surfaces is the same
>     in both -- the frame without GI 0.0509, the reference with its lamps as points and one bounce
>     0.0507 (0.2%); as capsules 0.0533, the shape's own 5%. The whole matte excess is the garage's
>     glowing tube meshes (emissive 34/63/100 on twenty tubes) and the bounces: in the reference they
>     add +0.11 directly and +0.03 by bouncing (with them off, matte falls to 0.060), while the
>     frame's bake and its traced bounce each add about +0.045 -- a third. The reference's glow is
>     the same total whether found by its samples or by the path alone (step 3's check), so this is
>     the frame's indirect light under-delivering bright glowing meshes, plus the D13 double count
>     (each tube lit by its Light and its mesh). Glossy floor, glass and the rest follow: they reflect
>     the brighter walls. Metal (mostly the glowing tubes seen directly) within 4%. This is what the
>     reference exists to show, reported and not tuned away; it belongs to RT2-11 (glowing meshes as
>     light sources), RT2-25 and RT2-26 (what the bounce and the bake take from them).
>   - *The literal "closed white room converges to uniform within 1%"* cannot be built with this
>     material model: a uniform room needs one directional albedo at every angle, and Schlick's
>     Fresnel makes every surface reflect more at grazing. The furnace spheres measure the same
>     thing per pixel against exact values, which is the stronger form of the check.
>
> **Added from RT-24's hard-coded-values audit (2026-09-24; section 10 lists every value).** A
> ground-truth render of the garage's wet floor under the tubes, to settle how sharp the lights'
> reflections should be: they fattened when RT-21 removed the highlight cap (bisected: 39% of the near
> floor washed out on 20 Sept, 45% at 50c678f, 48.5% now), and only a reference can say whether today's
> look is right for the floor's roughness (47% of it at 0.1-0.2) or a remaining defect.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** The capture
> harness takes its picture at a fixed frame number. General form: capture once the histories report
> settled, or after the longest memory in force.

> **Changed by the comparison (2026-09-25).** A new step 2h (the "signal stands alone" command); the
> swing metrics also at 2560x1600, because the target is native (D8); new fixtures that the materials,
> glass and bake gates need; and a wider reference renderer (tinted glass, water, fog along every
> segment, lit particles, the fixture-link rule of D13). The every-light arm (K=0) checks light
> selection only: it keeps one frozen soft-shadow point per light and point-like diffuse, so it measures
> sampling error, not the true picture. Every "truth" gate uses `--reference-render`.

- **What is wrong now.**
  - Questions of bias (the probe against the trace, the floor's darkening, whether the clamps eat light) are judged against references built from the shipped chain. The 16-ray truth keeps every filter it is used to judge, and the every-light arm keeps the shipped shading.
  - `bench_scale.py` runs with rays off, and its draw counter no longer parses the log.
  - The only scale scene is four primitive meshes, all Static and untextured, with no hierarchy, no emissive surfaces and nothing moving.
  - There is no many-light ray-traced scene: the bridge's lamps are baked.
  - The GI test scenes show less than one display level of GI (their skies are black). The measured worth of the traced bounce predates the fix that made its aimed rays work. The leak fixtures have black skies and zero ambient, so they cannot see two known leaks at bake hits (C-N3).
  - The profiler cannot see simulation, extraction or uploads.
  - Several switches have side effects. `--reflection-history=off` also switches TAA to surface motion and turns the noise blur off; `--rt-reflections=off` brings SSR back. There is no moving-camera harness for the bridge.
  - Around 30 later exit gates ask for "diff 0" over the garage, the three bridge cameras and the camp, over parked frames, a swing and every AA mode, and no harness does that in one command.
  - No command proves that a signal converges without TAA: every "no reliance on TAA" gate would be run by hand.
  - The swing metrics run at 1600x900 only, and captures are taken at a fixed frame number.
  - Back-to-back benchmark runs drift by up to a millisecond as the laptop warms, and there is no frame cap to hold the load steady.
- **What changes.** Eight named steps, each ending in its own report.
  - **2a. The baseline harness and a frame cap.** One command renders the standard camera set (garage, Headland, Deck, Glitter, the camp) parked, over a 60-frame swing, and in every AA mode, and diffs each frame against a stored baseline, printing a per-pixel diff image and a count per scene. Every "diff 0" gate after this uses it. Captures are taken once the histories report settled, not at a fixed frame. An optional frame cap for measurement runs (`--frame-cap`) is added and its effect on the drift is measured.
  - **2b. A reference mode (`--reference-render`).** Progressive accumulation at a fixed pose, unclamped and unfiltered: no resolve, no colour box, no firefly clamp, no TAA. Thousands of samples per pixel, every light with many area samples, multi-bounce paths. It evaluates the **full** material at every path vertex: normal maps, the full BSDF (coat, sheen and anisotropy included), terrain layers, and textures at a mip chosen by the ray's footprint. It must not share the shipped hit shading's shortcuts (no normal maps, mip 0, Lambert-only bounces, terrain layer 0 only; MAT-02), or it would bless the simplifications it is meant to judge, which is REFL-14's flaw in the 16-ray truth. Before RT2-31 to RT2-33 are gated against it, it gains RGB thin-glass transmission, water refraction and absorption, height fog along every path segment, and lit particles. It applies the fixture-link rule of D13 (a linked Light's shine comes from its glowing mesh), so it judges sampling and filtering, not an authoring convention.
  - **2c. The scale benchmark.** `bench_scale.py` gains ray-traced arms at the project's settings, still and moving pairs, TLAS and BLAS timings, CPU phases and A,B,B,A order, and its parser is fixed. The 60,000-object figure (40.76 ms, one run) is re-measured as a palindrome first; that becomes the baseline. `make_scale_scenes.py` gains scenes that look like real content, at 20k, 60k and 120k: imported hierarchical models (a car of 50 to 150 parts), hundreds of distinct textured meshes, emissive fittings, a share of moving objects, and an alpha-tested foliage scene for RT2-38 and RT2-39.
  - **2d. A many-light scene**, starting from `lights256.rage`: 1k to 10k realtime lights of mixed sizes and ranges including tubes, moving lights, emissive meshes, rooms whose light ranges cross walls, a scripted camera path, and an every-light (K=0) arm.
  - **2e. GI scenes where bounce light carries energy**: a bright-sky enclosed room and a daylit version of the garage. Then GI on against GI off is re-measured by diff image, with realtime GI forced in the garage and the camp. The leak fixtures gain variants with a non-black sky and a non-zero ambient. The showroom's bright bay (mode 2) gets a stored camera, because the one real-scene measurement of the bake gathered at hits lost there (C-N1).
  - **2f. CPU timing zones** (nestable, aware of threads, near-free when off), with phases for simulation, scripts, physics, animation, transforms, extraction, TLAS instances and uploads. The shadow phase is split into its parts: the draw-list rebuild, the cull-table upload, the measured-change hash, emitter extraction, the TLAS pack and the build recording. Report counters: blocking submits and their milliseconds, bytes uploaded, asset-cache misses, allocations per frame.
  - **2g. Cleaner switches.** The confounded switches are split into single-purpose ones, a "no reflections" arm that does not bring SSR back is added, and spin_measure's method is applied at the bridge cameras.
  - **2h. The "signal stands alone" command and the test fixtures** (added 2026-09-25). For each signal, the command runs `--capture-signals` under `--aa=none` and under TAA with `--no-frame-memory`, parked and through the owner's swing, and prints convergence (frame 400 against frame 200, against the parked noise floor), the grain across centre crossings, `speckle_count.py` on the signal's own capture, and a histogram of history ages. Every "no reliance on TAA" gate uses it. `spin_measure.py`, `speckle_count.py` and `ghost_map.py` also run at 2560x1600, with their spots scaled by resolution. New fixtures, as new scenes (rule 5: no shipped scene or material is edited): white-furnace spheres (F0 = 1 and albedo 1, roughness 0 to 1); a strongly normal-mapped plane under grazing light, at the origin and 10 km away; a coat, sheen and anisotropy parity scene; a curved chrome pole; a roughness-ramp scene (0.05 to 1.0, lit by a tube and a Realtime light); a tinted pane over a floor; glass over glass with a particle in front. The primary-ray parity check (RT2-21d) becomes a scenetest claim, and each scene gets a triangles-per-pixel histogram.
- **Owner sees.** Nothing in the frame. Numbers that can finally answer "which one is right".
- **Touches.** New reference passes; `FrameGraphBuilder.cpp`; `EngineConfig`; `FrameProfiler.cpp`; `Application.cpp` (the frame cap); `EngineConfig.h:218` (`ScreenshotFrame`); `tools/scripts/bench_scale.py`, `make_scale_scenes.py`, a new baseline harness, the signal-stands-alone command, new scene scripts, `garage/spin_measure.py` (its 1600x900-only spots, lines 233-239), `speckle_count.py`, `ghost_map.py`, `watch_arm.py`.
- **Scope.** Refactor. **Depends on.** RT2-1. **Risk.** Medium: a reference renderer has to be shown to be right before it judges anything.
- **Exit gate.**
  - 2a: the harness reproduces its own baseline (diff 0) on an unchanged build, and flags a deliberately changed pixel.
  - 2b: a white-furnace test (a closed white room under uniform light converges to uniform within 1%, and the white-furnace spheres read 1.0 within 2%). On the parked garage, the reference and the 16-ray truth agree region by region where both are unbiased (the matte walls and ceiling): a diff image with no structure beyond grain, and the per-region mean difference recorded. Where they disagree (the glossy floor, the chrome), that difference is what the reference exists to show, and it is reported, not tuned away.
  - 2c: the palindrome baseline at 60,000 objects recorded with its spread.
  - 2d: the many-light scene runs with its every-light arm.
  - 2e: GI on against off differs by at least two display levels over the lit region of each new GI scene (the number recorded); the garage and camp GI worth re-recorded.
  - 2f: the shadow phase's parts add up to its total within the spread.
  - 2g: each split switch changes only its own pass (`--pass-timings` shows no other pass appearing or vanishing); the "no reflections" arm leaves SSR off, unlike today's `--rt-reflections=off` (no SSR pass in `--pass-timings`, and the SSR target's diff is 0); `spin_measure.py` runs at the three bridge cameras.
  - 2h: the command reproduces its own numbers on an unchanged build (twice, within the palindrome spread), and a staged change (the young blur forced on for AO) moves AO's numbers and no other signal's.
- **Size.** 11-14 sessions.
- **Closes.** REFL-14, GEO-03, core-s4, direct-14, gi-s4, CORE-10, ISS-42; the frame-cap part of CORE-11.

### RT2-3 · Delete the arms already decided dead

- **In plain words.** Old code paths that were measured and rejected are still compiled and wired to switches, and every change has to keep them working. The owner confirms each one from a list, and it is deleted. Nothing changes at default settings.

> **Changed by the comparison (2026-09-25).** One more dead switch joins the list: the RT optimisation
> presets' ReflectionScale column (Off 1, Quality and Balanced 2, Performance 4) is never read
> (`FrameGraphBuilder.cpp:797-799` uses 1 unless `--water-reflection` is given). It is deleted here;
> RT2-31 introduces the general ray-rate setting that replaces what it was meant to do.

- **What is wrong now.** Old paths stay compiled and wired to switches after their verdict:
  - the sea's retired choose-and-shade pair;
  - the water contract paths RT-8 called "dead weight";
  - the lamp reuse S4b, measured as losing, with its history still allocated every frame in water scenes;
  - the reflection follow-hit arm, measured worse (it is not deleted here: by D16 it is tested against denoiser v2 first and deleted in RT2-23's step 23e);
  - the traced sky visibility, which leans on TAA;
  - a dead `GpuCull::HasObjects`, and stale comments;
  - the unread ReflectionScale column of the RT optimisation presets (`RenderSettings.h:238-244`).

  Every change must keep all of them compiling, and the water code alone carries nine unverified review findings.
- **What changes.** The owner confirms each deletion from a list that gives its verdict and record. Confirmed arms are deleted with their shaders, settings and histories. Reference arms still in use (`--direct-signal=off`, `--gi-signal=off`, `--ao-signal=off`, the opaque moving layer) stay until the items that replace them.
- **Owner sees.** Nothing at default settings. Fewer switches, and fewer variants compiled at launch.
- **Touches.** `Renderer3D.cpp` (the water variants at 1737-1757), `FrameGraphBuilder.cpp` (3142-3350, 3157), `EngineConfig`, the water_choose and water_shade shaders, `pbr_fragment.glsl` (the sky visibility at 1437-1463), `RenderSettings.h`, `GpuCull.h`, comments in `Scene.cpp`.
- **Scope.** Deletion. **Depends on.** RT2-1, and RT2-2a (the baseline harness). **Risk.** Low.
- **Exit gate.** Default-settings frames pixel-identical before and after on the garage, the bridge's three cameras and the camp (diff 0, through the harness); scenetest green on both backends; the launch compile count lower (in the log).
- **Size.** 1 session.
- **Closes.** GI-15, ISS-15; parts of MAT-10, direct-15, REFL-12, GEO-18 and frame-13; the comparison's finding G-N2 (section 8).

### RT2-4 · Patches that should not change the picture, and small wins that need no rewrite

- **In plain words.** A dozen small defects and missed savings, each found in a review, that can be fixed today without a rewrite and should not change the picture. The biggest saving is skipping the TLAS build on frames where nothing moved. If a fix turns out to change pixels, it moves to RT2-6 and the owner judges it.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Found by the
> skeptics: a shadow ray fetches a cutout texture even when the cutoff cannot fail. Guard it (cost
> only, no change to the picture).

> **Changed by the comparison (2026-09-25).** The TLAS skip is specified so that it can work: its key is
> computed every frame before the build, outside `--measured-change`, and compared with that frame
> slot's own last build (the TLAS is one per frame in flight, so the slot's last build is two frames
> back). Today's measured-change key cannot serve as-is: it is computed after the build, and it changes
> every frame in any scene with a skinned mesh (E-N2), so on its own the skip saves time only on fully
> still frames until RT2-35's pose-hash skip lands. One measured arm is added: build the TLAS with the
> fast-trace flag only (no ALLOW_UPDATE) and rebuild on any change, against today's refit, which keeps
> a tree fitted to where things were for up to 64 builds per slot even when the contents changed (the
> shipped policy, rule 1). The ray-budget passes are skipped when rays are off.

Only fixes whose picture should not change are here. Any fix that turns out to move pixels in the harness moves to RT2-6 and is shown to the owner as its own arm. The five the critic identified as picture-changing (the tile boxes, the particle velocity and weights, the history counter, the tube rims, and the world-grid lookup) are already in RT2-6.

- **What is wrong now.** Ten defects and five missed wins, each found in a review:
  - LightGlow binds 80 bytes per light against the 96-byte light record, so it reads the last sixth of the lights outside its bound range (`LightGlow.cpp:240`). (If those lamps' glows turn out to be missing today, the fix changes pixels and goes to RT2-6.)
  - The GPU cull's object and template tables, and each GPU particle emitter's parameter buffers, are single copies shared by two frames in flight. The CPU can overwrite them while the previous frame is still reading them (`GpuCull.cpp:84-98`, `GpuParticles.cpp:205-227`).
  - An entity spawned during a frame is drawn on its first frame with an identity previous transform, which gives it a false motion vector from the world origin (`Components.h:97`).
  - Prefab spawning reads the disk directly, so it fails in every packaged build (`AssetManager.cpp:1717-1727`).
  - Camera-cut detection only works while TAA runs (`FrameGraphBuilder.cpp:545`).
  - The scene pass's indirect-light pointer is never cleared, so probe captures read the camera's buffer (`FrameGraphBuilder.cpp:1229-1271`). (A scene with a realtime probe, such as the camp, may change; if so, it goes to RT2-6.)
  - The GPU particle path forces a full transform walk every frame, even with no emitters (`GpuParticles.cpp:404-415`).
  - The irradiance field allocates and zero-fills a same-size solve texture even when it never solves: about 92 MB of VRAM on the bridge. Every field creation also reads both identical 92 MB bake files in full.
  - A fifth shadow-casting point light in the raster fallback is demoted without being named.
  - The main loop spins without sleeping while the window is minimised (`Application.cpp:747-756`).
  - **Missed wins:** the TLAS is rebuilt or refit every frame even when nothing in it changed; every TLAS build carries ALLOW_UPDATE, and a refit keeps contents that changed under it (`VulkanResources.cpp:1126-1127`, `1308-1329`); the TLAS pack loop copies a shared pointer per instance (`VulkanResources.cpp:1237`); the ray-budget passes run in the raster scale scene with rays off; and asset change detection re-reads and re-hashes every source file at every launch and every editor refresh, on the main thread (CORE-09).
- **What changes.** One fix per item, in the same order:
  - bind the right size;
  - one copy per frame in flight, plus a debug check that flags any upload into a buffer a running frame still reads;
  - set `PreviousWorld` on the first derive;
  - read prefabs through the virtual file system;
  - keep each view's previous camera independently of TAA;
  - clear the pointer after the draw;
  - add the particle guard;
  - allocate the solve texture only when a solve is requested, and read only the twin file's header;
  - name the demoted light;
  - sleep, or wait on window events, while minimised;
  - the TLAS skip: a content key over the instance list (BLAS references, transforms, masks and flags) computed every frame before the build, compared with that frame slot's own last build, and the build skipped when they match; a plain cast in the pack loop;
  - one measured arm: the TLAS built with the fast-trace flag only and fully rebuilt whenever its key changed, against today's refit. Adopted if every ray-traced pass's time after 64 moving frames is no worse beyond the spread (A,B,B,A, `bench_scale.py`'s ray-traced arms plus the garage);
  - skip the ray-budget passes when rays are off;
  - detect asset changes by file size and modification time kept in the `.meta` sidecar, and hash only when those move, on worker threads.
- **Owner sees.** Nothing on screen. 92 MB of VRAM back on the bridge. Prefabs work in packaged builds. Faster launches and editor saves. Some CPU and GPU back on parked frames (measured).
- **Touches.** `LightGlow.cpp`, `GpuCull.cpp`, `GpuParticles.cpp`, `Components.h`, `Scene.cpp`, `AssetManager.cpp`, `AssetRegistry.cpp`, `FrameGraphBuilder.cpp`, `IrradianceVolume.cpp`, `ShadowMap.cpp`, `Application.cpp`, `RayShadows.cpp` (the key, 428-467), `VulkanResources.cpp` (the build flags and the refit rule), `ray_shadow_trace.glsl` (the cutout guard).
- **Scope.** Patch. **Depends on.** RT2-1 and RT2-2a. **Risk.** Low.
- **Exit gate.**
  - Default frames diff 0 through the harness on the garage, the bridge's three cameras and the camp. Anything that moves a pixel leaves this item for RT2-6.
  - A scenetest claim that an entity spawned in `OnTick` has `PreviousWorld` equal to `World` on its first frame.
  - A packaged build spawns a prefab.
  - A teleport under MSAA with the AO signal on drops every history: the frame after the cut equals a cold start.
  - Bridge VRAM read before and after (RT2-1's report).
  - CPU zones (RT2-2f): the shadow phase on parked frames, before and after the TLAS skip; launch time and single-file save time before and after the change detection.
  - `--pass-timings` on a fully still frame: no TLAS build. On a frame where one object moved: one build.
  - The rebuild arm: every ray-traced pass's GPU time after 64 moving frames, rebuild against refit, A,B,B,A; the result and the adopt-or-reject decision recorded.
- **Size.** 3-4 sessions.
- **Closes.** direct-17, GEO-12, core-s1, core-s2, CORE-14, framegraph-s2, framegraph-s3, gi-s5, CORE-09 (the change-detection half); parts of MAT-16, CORE-07, GI-12, GEO-04 (the patches available today), CORE-11 (the minimised loop) and ISS-36; the comparison's findings E-N1 and E-N3 (section 8).

### RT2-5 · Stable object identity in exact lanes

- **In plain words.** Every history in the engine asks "is this still the same object?" and today the answer is often wrong: ids are stored in half floats, which are exact only below 2,048, and many objects get a new id whenever culling changes the draw order. This item gives every object one stable id, stored in integer lanes. Parked pictures do not change; on pans and at scale, histories stop being thrown away.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Confirmed
> independently: CPU-path ids follow submission order, so when the camp's fox or a terrain chunk
> crosses the frame edge every later id shifts, and both the reflection memory and TAA refuse good
> history (live on the camp and the bridge, first to fix); and the struck identity folds to 1..1021.

> **Changed by the comparison (2026-09-25).** The G-buffer contract's identity lane lands here, so that
> RT2-21 does not reformat it again: `R32G32_UINT`, x = the full 32-bit entity handle (index and
> version, never folded), y = the filtered roughness as 16 bits, occlusion 8 bits and the Static flag 1
> bit, until RT2-21 gives roughness its own lane. Terrain chunks and a model's sections share their
> entity's id, not an id per chunk: adjacent chunks are one continuous surface, and an id per chunk
> would refuse good history at the seams. (RT-SERIES-2 folded the id to 24 bits and keyed chunks by
> their index; the uint lane has room for the whole handle, which also removes GEO-17's reuse ghost.)
> The TLAS's own 24-bit custom index is a separate thing: it becomes the GPU-scene slot in RT2-34.

- **What is wrong now.**
  - The G-buffer's object id is exact, but the reflection, glass and direct histories keep it in half-float lanes, which hold whole numbers exactly only up to 2,048 (`FrameGraphBuilder.cpp:1001-1017`, `1802`, `2788-2793`). Above 2,048 a half float holds only every second whole number, above 4,096 every fourth, and so on, so most ids never match, and the accumulator refuses the history outright (`reflection_accumulate.rvshader:1126`). The arithmetic predicts about 11.7% of ids staying exact at 60,000 objects, which fits the measured 10.1% of glossy pixels keeping their history.
  - For everything drawn from the CPU list, the id is this frame's position in the submission list after culling (`Renderer3D.cpp:9705`, `9756`, `9839`, `9892`; the GPU path is `9177`). That covers terrain chunks, skinned meshes, water, and every mesh in the editor's second view or under `--gpu-lit=off`. An object entering or leaving the view renumbers every later one, and TAA and the accumulators throw their histories away. The reflection history's same-object test compares these per-frame rows, so "same object" is unreliable even below 2,048 (F-N3).
  - RT-17's struck identity folds the entity index into 1..1021 and drops the version (`Renderer3D.cpp:5662-5670`).
  - The reflector's plane offset is stored in world units in a half float. Its rounding error reaches 0.25 m for surfaces 256 to 512 m from the world origin (`reflection_accumulate.rvshader:1955`).
- **What changes.**
  - One stable id per object: the entity's full 32-bit handle (index and version), written at all five places where instance data is filled. Terrain chunks and model sections share their entity's id.
  - The G-buffer's identity lane becomes `R32G32_UINT` (x the id; y the filtered roughness 16 bits, occlusion 8 bits, the Static flag), decoded exactly as today's float lane, so parked frames stay at diff 0. The TAA guide and every history keep the id in an `R32_UINT` lane.
  - The struck identity carries the same 32-bit handle, and the id refusal gets its own counter lane.
  - The plane offset is stored relative to the camera.
- **Owner sees.** The garage and the camp unchanged when parked. On bridge pans, terrain stops losing its history for a frame when chunks cross the edge of the view. At scale, reflections accumulate again.
- **Touches.** `Renderer3D.cpp` (the fill sites; the struck identity at 5662-5670), `FrameGraphBuilder.cpp` (lane formats), `reflection_accumulate.rvshader`, `taa_guide.rvshader`, `taa_resolve.rvshader`, the direct and glass accumulate variants, `pbr_fragment.glsl` (4505-4507).
- **Scope.** Patch. **Depends on.** RT2-2. **Risk.** Low; the history lanes grow a little, which RT2-19 accounts for.
- **Exit gate.**
  - Parked frames on the garage, the bridge's three cameras and the camp: diff 0.
  - A Headland pan: the object-id refusal share, now counted, falls, and a diff of the pan shows no new structure.
  - The 60,000-object scale scene with rays on keeps reflection history on more than 95% of glossy pixels (10.1% today); the realistic-content scenes likewise.
  - The G-buffer and accumulate passes' times unchanged within the spread (the bytes are unchanged), A,B,B,A.
- **Size.** 2 sessions.
- **Closes.** geometry-s1, geometry-s2, materials-s3, REFL-02, GEO-17, REFL-17; the integer-id part of MAT-12; the comparison's finding F-N3 (section 8).

### RT2-6 · Correctness patches that do change the picture, one at a time

- **In plain words.** Sixteen known defects whose fixes change the picture on purpose: probes captured before shadows exist, tube reflections that lose a third of their blue, window glass that hides the car's interior only when its reflection is bright, fog that stops at the far clip for the sky, and more. Each is shown to the owner as its own diff image, one at a time, and lands only when the owner has judged it.

> **Added from RT-24's hard-coded-values audit (2026-09-24; section 10 lists every value).** The motion
> lane's own precision. It is half-float, so its rounding grows with speed, while two tests treat
> anything above a fixed 1/8 texel as real motion (`kMotionFloor` in the accumulator, the change record's
> tolerance): at high speed a still surface reads as moving on its own. General form: the tolerance is the
> lane's representable error at that magnitude (the half-float step of the velocity, in texels), or the
> prediction is rounded to the lane's precision before the difference -- so rounding can never fire it.
> The same rule for two more: the velocity lane's format itself (two half floats; general form: a
> format whose rounding stays far under a texel at any on-screen motion, such as two 32-bit floats),
> and the re-light's change test (0.004 of the value plus 1e-4; general form: half a step of the
> stored half float at that value, plus the float path's own step).

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Two more arms,
> both defects rather than tuned values: local probe cubes are sampled in the sky's rotated frame
> (wrong local reflections whenever the sky is rotated), and the reflection ray budget reads its
> history through a flipped coordinate, so each tile's rays follow a mirrored tile (first to fix).
> Arms 8, 11 and 13 were confirmed; arm 13's general form: one weight definition for every see-through
> writer, derived from the projection. The direct light's and GI's re-light tolerances join the
> storage-format rule in the note above.

> **Changed by the comparison (2026-09-25).** Each note above now has one decided form. The motion lane
> stays `RG16F`; every "moved" test uses the lane's own half-float step at that magnitude, and an
> `RG32F` arm at the owner's swing proves it (speckle and edge jitter equal within noise). The re-light
> tolerances use the stored format's own step. Arm 3 becomes the exact patch: the glow, particle and
> world-UI pipelines stop writing the colour's alpha (a colour write mask without A); RT2-22 then removes
> the alpha channel's role for good. Arm 6's ceiling is the half float's own maximum (65504), a format
> guard and not a tuned value. Two new arms move forward from later items, because each is a live
> defect with no dependency on a rewrite: glass coverage from its own reflectance (arm 15, from RT2-31)
> and fog to infinity for the sky (arm 16, from RT2-33).

- **What is wrong now, and what changes.** Sixteen arms, one at a time. Each is shown to the owner as its own diff image, with the mean brightness and the bridge's red-to-blue ratio checked before anything is shown. Any fix from RT2-4 that turned out to move pixels joins this list the same way.
  1. **Probes captured after the ray structure is built.** Today every shadow ray in all six faces answers "lit". Probes are captured before `RenderShadows` (`RuntimeLayer.cpp:493`, `EditorLayer.cpp:562`), and `RayShadows::BeginFrame` has already cleared the structure's Active flag. The garage probe is baked, so this needs a re-bake, committed with the work. Afterwards the reference renderer settles ISS-9: which of the probe and the trace is right.
  2. **A field with no loaded or solved answer owns no light** (it publishes a "field valid" flag). Today a baked-lamp switch, or an edited baked light, zeroes the field while DirectTrace and ray hits still skip those lamps' far share. So on the bridge, every hybrid lamp's light beyond 2 to 3 m of its head disappears until the next bake (inferred from the code).
  3. **Lamp glows, sorted and additive particles, and world-space UI stop raising the traced reflection's weight**, which the lit shader keeps in the colour's alpha channel. The glow writes alpha 1 with additive blending, and the composite multiplies the reflection by that alpha (inferred: squares of reflected light round lamp glows on glossy surfaces). The patch: those pipelines write colour with a write mask that excludes alpha.
  4. **Glows and non-weighted particles drawn after glass and water**, not before. Today the sea owns its pixels outright and erases any lamp glow in front of it (inferred).
  5. **The reflection trace's absolute cap of 64 per colour channel removed.** The tubes emit 34/63/100, so their mirror reflections lose 36% of their blue. Outliers stay bounded by the accumulator's relative clamps. One risk: until RT2-11 aims a ray at every emitter, a lucky hit on a bar that is not in the aimed list returns up to 100 at a single ray's weight, and the resolve already lifts its bright-sample cap where emission dominates (`reflection_resolve.rvshader:362-364`). That is RT-23's speckle mechanism. So this arm's gate includes `truth_test.py`'s bright-speck count, and if the count rises, the arm waits until after RT2-11. (Since 2026-09-25 every listed emitter is aimed at; emitters with a strength of 1 or less are still unlisted until RT2-11.) The same cap on the 8-neighbour lamp pool's result (`reflection_resolve.rvshader:456`) goes in the same arm.
  6. **A finite ceiling on specular before it is stored in half floats, plus an inf/NaN counter.** Since RT-21, a zero-radius light on a polished metal can overflow the half float. The ceiling is the half float's own maximum, 65504.
  7. **The firefly clamp scaled by the lobe width**, and off where one ray covers the whole lobe. Today a sub-pixel light seen in a mirror is clamped down to its dark neighbours.
  8. **The two remaining half-float histories rounded through `half_float.glsl`** (water_foam and irradiance_fill; recorded as the owner's call).
  9. **One GI intensity for every GI source, or none.** Today the bridge profile's 2.6 scales the realtime bounce but not the baked one. Both arms are shown, and the owner decides.
  10. **The world-grid lookup compiled into the trace-only passes**, which today walk every light because it is compiled out of them (`pbr_fragment.glsl:2851-2941`). This is exact only on average, not pixel for pixel: the grid's cell lists are sorted by brightness (`LightGrid.cpp:194-215`), and RT-11's reservoir draws one random number per candidate in walk order (`pbr_fragment.glsl:3098-3125`), so the same seed picks a different light. The bake's walk over baked lamps keeps its own list. This is RageV's interim world grid at hits, used only until RT2-12 lands; it is not Doom's form (its fixed 8x4x32 cell count and bridge-shaped axes are scene-shaped, rule 2). RT2-12's rule-1 comparison arm is the camera-centred cascaded grid Doom: The Dark Ages ships.
  11. **The AO and GI ray budget's 2x2 boxes put back on the pixel grid.** Today they sit half a pixel off, so rows are skipped or counted twice (`tile_reduce.rvshader:52-61`). The rays move to different tiles.
  12. **One history counter.** `kSettledBound` and the image-distance blend read the trust count, which the anti-lag never resets; both move to the blend count. The stale "composite after TAA" headers (`reflection_composite.rvshader:1-31`, `reflection_accumulate.rvshader:49-54`, D-N1) are rewritten and TAA's dead alpha path removed. If RT2-0's arm F already replaced the image-distance smoothing, only `kSettledBound` is left (arm F moved to RT2-23's step 23a, so both stay here).
  13. **Particles' velocity masked, and one transparency weight.** Sorted and additive particles blend their velocity output by an undefined alpha, and meshes and particles use different weights (20 against 90).
  14. **The tube rims' false motion.** A thin rim round each garage tube reports motion while the camera is parked (about 0.08% of pixels); its source is found and fixed.
  15. **Glass coverage from its own reflectance** (added 2026-09-25, moved forward from RT2-31). Today the glass hides what is behind it by how bright its reflection is: the car's windows hide the interior behind bright reflections and show it behind dim ones, and the split moves with the light level (`pbr_fragment.glsl:6313-6316`, audit 2's live, first-to-fix row). The fix: reflectance = the largest channel of (F0 x envBRDF.x + envBRDF.y) x occlusion, independent of what is reflected.
  16. **Fog integrated to infinity for the sky** (added 2026-09-25, moved forward from RT2-33). Background pixels are fogged as if at the far clip today (`fog.rvshader:130-133`, audit 2's live row). The fix: the closed-form limit where the ray climbs, or the floor crossing that `fog.rvshader:174-213` already solves where it descends; and the horizontal-ray branch decided on the dimensionless product of climb and travel, with a series below the float cancellation point. RT2-33 later moves the same function into the composition.
- **Owner sees.** A darker, shadowed garage probe, which changes the fallback past the gloss window and the lighting at reflection hits. No reflection squares round lamp glows. Glows visible over the sea. Bluer tube reflections. Point highlights on chrome that survive. The bridge keeps its far lamp light after a baked lamp is switched. Particles stop smearing under TAA. Cheaper ray hits. Car windows whose see-through share no longer follows the light level. A night sky fogged the same as distant ground.
- **Touches.** `RuntimeLayer.cpp`, `EditorLayer.cpp`, `Scene.cpp` (3493, 4014-4099), `Renderer3D.cpp` (4134-4141, 4419), `direct_trace.rvshader` (463, 1406), `pbr_fragment.glsl` (2851-2941, 2955-2960, 5695, 5717, 5763, 5769, 6313-6316, 6492), `LightGlow.cpp`, `light_glow.rvshader`, `ParticleRenderer.cpp` and the particle shaders, `UIRenderer.cpp`, `VulkanCommon.cpp` (blend presets and write masks), `reflection_trace.rvshader` (900-902), `reflection_resolve.rvshader` (456), `reflection_accumulate.rvshader` (1426-1449), `reflection_composite.rvshader`, `taa_resolve.rvshader`, `tile_reduce.rvshader`, `fog.rvshader`, the water_foam and irradiance_fill shaders.
- **Scope.** Patch. **Depends on.** RT2-1 and RT2-2 (the reference renderer, for arms 1, 5 and 7). **Risk.** Medium: these change the look on purpose, and the owner judges each.
- **Exit gate.** For each arm, the diff image against before, judged by the owner. In addition:
  - Arm 1: the frame-mean gap between probe and trace, and a diff of each against the reference renderer.
  - Arm 2: the bridge with one lamp switched, diffed against the unswitched frame; the far lamp light must be kept.
  - Arm 3: Headland with glows on against glows off, restricted to glossy opaque pixels: diff 0 after.
  - Arm 5: the tube reflection's hue against the reference, and `truth_test.py`'s bright-speck count no higher.
  - Arm 6: the inf/NaN counter at zero on every scene.
  - Arm 7: a sub-pixel light seen in a mirror against the unclamped reference.
  - Arm 10: a converged diff within grain against today; a parked camera leaves the reflection-change map black; the hit-record counters drop.
  - Arm 11: a diff within grain, judged as a diff image.
  - Arm 14: the taa-refusal view's count on the parked garage's tube rims drops to zero.
  - Arm 15: the same garage lighting at two exposures: the windows' coverage does not change.
  - Arm 16: fog off, byte-identical; the sky's fog against the reference renderer's height fog to infinity, within grain.
  - The motion-lane note: an `RG32F` velocity arm at the owner's swing gives speckle and edge jitter equal within noise to the `RG16F` lane with the format-step tests.
- **Size.** 5-7 sessions.
- **Closes.** GI-04, gi-s2, materials-s1, materials-s2, REFL-16, MAT-17, reflections-s4, GI-10, direct-18, REFL-13, ISS-7, ISS-8, ISS-9 (which estimate is right), ISS-31; parts of MAT-16, direct-02 and GI-06; the comparison's finding D-N1 (section 8).

### RT2-7 · Random numbers that move in every anti-aliasing mode

- **In plain words.** Almost every random choice in the ray-traced shaders only changes from frame to frame while TAA shakes the camera, so without TAA the noise freezes and no signal can be shown to stand on its own. Step 7a ties each signal's random numbers to its own frame counter, with no visible change under TAA. Step 7b then replaces the assorted random numbers with blue noise, which averages out faster; that one changes the picture and the owner judges it.

> **Added from RT-24's hard-coded-values audit (2026-09-24; section 10 lists every value).** The
> reflection trace indexes each texel's random rotation as x + 1601 y, so on any target wider than
> 1601 texels (the reflection grid is 3200 wide at 2x supersampling, the native target 2560) the texel
> 1601 columns to the right shares its random numbers with the texel one row down. The two are far
> apart, so no filter mixes them today, but the number is tied to a resolution (first pass only).
> General form: index by the target's own width, so no two texels share a rotation.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Confirmed: the
> direct light's random draws move only while TAA jitters (this item). Added: on the ray side, the
> thin-member fade's dither is keyed to a world lattice instead of the ray's own pixel and frame.

> **Changed by the comparison (2026-09-25).** Split into two steps. 7a is this item as written: it must be
> pixel-identical under TAA. 7b is new: one blue-noise service for every screen-space random draw, the
> spatiotemporal blue noise MegaLights uses engine-wide. The masks are the filter-adapted ones the FAST
> paper (Donnelly et al. 2024) publishes, which MegaLights cites for its own noise, so they are the
> baseline (rule 1), not a later improvement; masks tuned to RageV's own filters are only a measured arm,
> because those filters adapt (their memory runs from 1 to 64 frames). The verification also found that
> DirectTrace's K light picks, not only its soft-shadow points, freeze without jitter (A-N1).

- **What is wrong now.**
  - Every random choice in the traced shaders changes from frame to frame only while TAA's jitter is non-zero: lamp picks (DirectTrace's K picks included, `direct_trace.rvshader:944`, `1171-1180`), soft-shadow points on discs and tubes, shadow-ray thinning, the thin-member fade, the sea's quad lane, `LampFrameSalt` (`pbr_fragment.glsl:4729-4731`, `1380`, `4075`, `4192`, `4819`; `ray_shadow_trace.glsl:24-25`; `water_lamps.glsl:453`; `Renderer3D.cpp:7987`, `8097`, `8131`, `8267`).
  - Without TAA, each signal's accumulator averages the same frozen sample. That is why the owner forced TAA whenever rays are on (`FrameGraphBuilder.cpp:197-206`).
  - As a result, no signal can be shown to stand on its own, MSAA cannot run with rays, and a signal can come to lean on TAA unnoticed. RT-24's fix 1 does.
  - The draws that do move use white-noise hashes and interleaved gradient noise; the reflection lobe uses a 64-frame Halton sequence rotated per texel, which is stratified for a 64-frame average but not for the 1-to-4-frame memory the reflection has at speed.
- **What changes.**
  - **7a. A frame-level sample service.** Each signal's random sequence is indexed by that signal's own frame counter, and it advances whenever the signal's accumulator runs, whatever the AA mode. The jitter stays, for raster anti-aliasing only. The 1601 stride becomes the target's own width. The thin-member fade's dither on the ray side is keyed to the ray's own pixel and frame.
    - Rays traced inside draws with no accumulator of their own (the sea's mirror and refraction, the panes behind the nearest) hold still per pixel when no temporal filter runs, following the engine's WR-15 rule. RT2-31 gives them accumulators, or (for the back panes) one fixed mirror ray (D28).
    - A measurement flag lets rays run under `--aa=none` and MSAA for testing.
    - The forced TAA is not lifted here. Young and moving pixels still lean on TAA through the sea's in-draw rays until RT2-31 and RT2-32, so the owner's answer to D3 is carried out at the end of M5 (RT2-33).
  - **7b. One blue-noise service** (after 7a; an owner-judged arm). Spatiotemporal blue noise for every screen-space draw: lamp and emitter picks through sample warping (a weighted reservoir pick consumes one noise value), several draws from one value by dithered sampling, and 2D blue noise for soft-shadow points, shadow thinning, the reflection lobe and the AO and GI directions. It replaces interleaved gradient noise, the ad-hoc hashes and the reflection's Halton sequence with its per-texel rotation. The published FAST masks are the baseline. A hash mode stays behind a flag, for the DLSS Ray Reconstruction arm (RT2-29), which asks for decorrelated hashes. Masks tuned to RageV's own filters are a later arm, adopted only if they win both 7b gates.
- **Owner sees.** After 7a, under TAA at the project's settings, nothing: pixel-identical, because the jitter is non-zero there (`TemporalJitterScale: 1`) and every draw already moves. That holds only while the jitter scale is above zero: at 0, TAA runs with zero jitter and every ray-traced draw freezes (`FrameGraphBuilder.cpp:142-160`). So the owner's RT-20 note that the picture looked "very stable" at jitter scale 0 was probably taken with the ray noise frozen. In the new no-AA and MSAA arms, noise that converges instead of frozen dots. After 7b, finer, more even grain that settles faster; the owner judges it.
- **Touches.** `pbr_fragment.glsl`, `ray_shadow_trace.glsl`, `water_lamps.glsl`, the direct, reflection, GI and AO trace shaders, `reflection_trace.rvshader` (259-280), `Renderer3D.cpp`, `FrameGraphBuilder.cpp` (716-725, 197-206), a new noise-texture service and its masks.
- **Scope.** Refactor. **Depends on.** RT2-1. **Risk.** Medium. Outside TAA, the direct light's four-frame specular memory will show its sampling noise. That is recorded and handed to RT2-24, not patched with TAA.
- **Exit gate.**
  - 7a, under TAA: garage, bridge and camp parked frames and a spin_measure swing pixel-identical (diff 0, through RT2-2a's harness).
  - 7a, with rays under `--aa=none` (RT2-2h's command):
    - each signal's own output (`--capture-signals`) at frame 400 against frame 200 falls within the parked noise floor (it converges);
    - frames 200 and 201 differ in the random lanes (nothing is frozen);
    - with the frame filter's memory off, each signal's settled picture is within grain of its TAA-on settled picture (diff image);
    - moving and young states too, not only settled ones: a spin_measure swing and the frames after its stop converge per signal, and the grain score of young pixels is recorded;
    - the bridge's glint-flicker number is recorded.
  - 7a: with `TemporalJitterScale` at 0 under TAA, frames 200 and 201 still differ in the random lanes.
  - 7b: at equal rays, the per-pixel variance after 4 and after 32 frames, per signal (parked, `--aa=none`), lower than 7a's, shown as a diff image plus the number.
  - 7b: at the owner's swing, speckle (above 32 and 64 levels) and grain no worse than 7a's (today 513 and 85).
  - 7b: cost at most +0.05 ms at 2560x1600, A,B,B,A.
- **Size.** 4-5 sessions (7a 2-3, 7b 2).
- **Closes.** reflections-s1, direct-07, ISS-12; the sample-service half of frame-03; the interim half of MAT-06; the comparison's finding A-N1 (section 8).

### RT2-8 · A flicker floor that decides by behaviour, not by place

- **In plain words.** TAA gives a pixel extra room when it flickers, but today it decides which pixels get that room by where they are, and the sea lost it: the bridge's water glitter blinks more since RT-22. This item makes the decision by how the pixel behaves (does it keep swinging back and forth?), which is the owner's own parked proposal, and the same idea Unreal's TSR ships. The bridge water returns to its old steadiness.

> **Changed by the comparison (2026-09-25).** The design is kept and stays in M1. The jitter cycle's
> length and the flicker moments' memory are read from the owner's `TemporalJitterPhase` setting instead
> of the constants 8 and `kMomentFrames` 16, so the rule holds the day the setting changes; a gate at
> phases 4 and 16 checks it. Until RT2-31 and RT2-32, the floor still averages the sea's in-draw
> glitter; that interim reliance is already recorded under RT2-7.

- **What is wrong now.**
  - TAA's flicker floor (the extra room its colour box gets from a pixel's own recent swing) is decided by where a pixel is: only at outlines, and never under a see-through surface (RT-22's two rules in `taa_resolve.rvshader`). The sea is drawn by the transparent pass, so it counts as covered and lost the floor that kept its lamp glitter steady. The bridge's blinking share rose from 0.80% to 0.97% of the frame (ISS-1).
  - Rules based on place cannot tell coverage flicker (an edge or sub-pixel detail swinging back and forth on the jitter's 8-frame cycle) from a real change (which moves one way and stays).
  - The cycle length (8) and the moments' memory (`kMomentFrames` 16) are constants, not read from the project's jitter setting.
- **What changes.** The owner's parked proposal (ISS-2), built as filed. Per pixel, TAA keeps which side of its history the new input fell on and for how many frames running (one small lane). The floor, the pixel's own temporal spread, opens only while the input keeps swinging, and closes once the input has stayed on one side for one full jitter cycle. The cycle length and the moments' memory are read from `TemporalJitterPhase`. It replaces both place-based rules and `kMomentFrames`, needs no water rule, and needs nothing from the later rewrites, so it lands here, right after RT-24, instead of at the end of the series.
- **Owner sees.** The bridge's water glitter back to its steadiness from before RT-22. The car window's streak stays fixed.
- **Touches.** `taa_resolve.rvshader` (679-686, 1101-1104), `PostProcess.cpp` (one small history lane), `FrameGraphBuilder.cpp`.
- **Scope.** Patch. **Depends on.** RT2-0 committed, RT2-1, RT2-2a. **Risk.** Medium: it touches every pixel under TAA.
- **Exit gate.**
  - The four-way test: the window drive (`watch_arm.py light 8`) and the car's stop (`watch_arm.py car 20`) by the owner's eye; the bridge cables unchanged; the bridge water at 0.80% or better (`check_glint_flicker.py`, clock pinned). RT-24 leaves the bridge at 0.983% (0.969% before it, its own gate 0.97%) and the cables at 3.54% (3.47%): this item takes the miss (RT2-0's close, 2026-10-12).
  - RT-24's swing gate re-run; the parked edge shake at RT-20's numbers.
  - The rule holds at `TemporalJitterPhase` 4 and 16: the bridge water no worse than at 8.
  - Cost at most +0.05 ms at native, A,B,B,A.
- **Size.** 2-3 sessions.
- **Closes.** ISS-1, ISS-2.

### RT2-9 · Pipelines built at load; shader and pipeline caches (moved early; built 2026-10-13)

- **In plain words.** Every launch compiles about 36 versions of the big lit shader on one thread, and some ray-tracing pipelines are first built in the middle of a frame, which causes a hitch. This item builds everything at load on worker threads and keeps shader and pipeline caches on disk (the owner approved caches, D6). Launches and ray-tracing toggles get faster; the picture does not change.

> **Built and measured, 2026-10-13 (owner: "we desperately need shader caching").** Where a garage launch
> went, timed first: 29 s, of which glslang 3 s (265 compiles) and the driver building pipelines 20.5 s (96
> builds, one thread, the lit family 1.3-1.7 s each); nothing carried between launches. Built:
> - **The SPIR-V cache on** (D6), in `ShaderCache/spirv/` beside the executable, keyed on the expanded
>   source, defines, stage and the compiler's identity (glslang version, options, revision); a non-SPIR-V
>   file is a miss; written beside and renamed; trimmed past 512 MB, least recently used first.
>   `--shader-cache=off` compiles everything and keeps no pipeline cache at all.
> - **A persistent `VkPipelineCache`**, one file per GPU (`pipelines-<vendor>-<device>.bin`), its header
>   checked against the GPU and driver before the driver sees it; saved at shutdown.
> - **Pipelines built on worker threads** (`VulkanPipelineCommon::Launch`), waited for at first use, so a
>   run of requests (the lit family's fifteen) builds side by side.
> - **Instruments:** `Shaders this run` and `Pipelines this run` log lines; `tools/scripts/launch_time.py`
>   (palindrome, pixel check) and `shader_cache_stale_check.py` (an edited shader is recompiled).
> - **`--gpu=<name>`** (e.g. `--gpu=intel`) to measure another vendor on a two-GPU laptop.
> Measured (garage, 1600x900, RTX 5070 Ti laptop): launch 27.9 s -> 5.1 s with the caches (pipelines 20.2 s
> -> 0.15 s); a cold launch 27.9 -> 15.6 s from the parallel builds; the bridge 6.5 s; 0 pixels differ cache
> on against off in both scenes; the stale check passes. (NVIDIA's own driver cache later began serving the
> unchanged SPIR-V, which Windows cannot switch off, so a later "off" launch read 7.6 s; 15.6 s stays the
> cold figure.) Not built: one up-front list of every variant -- with the caches a warm launch builds all 96
> pipelines in 0.15 s and a cold one in parallel, so nothing worth a hitch is left to build mid-frame.
>
> **spirv-opt tried and dropped (owner: "give it a shot ... our engine will not just run on NVIDIA").**
> SPIRV-Tools at glslang's known-good commit, `-O` after glslang (glslang 16 optimises HLSL only), every
> binding preserved. GPU time on, against off: NVIDIA garage 20.74 -> 20.49 ms (1%), Intel iGPU garage at
> 1280x720 112.41 -> 112.45 ms (none); pictures the same as each GPU's run-to-run noise (NVIDIA 1 pixel by
> 1 level; Intel within its own off-against-off spread, 2 pixels beyond it); glslang 3 s -> 33 s cold; the
> driver's build about the same on both. And it faults the GPU on the bridge: bisected to `water.rvshader`
> alone (RT2-1 carries it). Removed with its libraries; the 30 s and the fault buy nothing measurable.

> **Checked by the comparison (2026-09-25): kept unchanged.** Pipeline caches and building every
> permutation at load are what every shipped engine does, with loud failures.

- **What is wrong now.**
  - Every launch compiles about 36 variants of the 6,500-line lit include on the main thread. The SPIR-V disk cache exists but is never switched on, by the owner's earlier choice.
  - There is no Vulkan pipeline cache, and the SPIR-V optimiser is not built.
  - Ray-trace pipelines are created the first time they are used, in the middle of a frame.
  - Changing an RT setting recompiles the lit shaders mid-frame.
- **What changes.**
  - Every variant is built at load, on worker threads, from a list that states its variant count. Nothing is created inside a frame.
  - A persistent Vulkan pipeline cache.
  - Per D6 (approved), the SPIR-V cache is switched on with a key that includes the compiler version and the build options, plus a `--shader-cache=off` flag so measurement runs always compile from source.
  - `spirv-opt` is built and compared pixel for pixel.
- **Owner sees.** A faster launch and faster RT toggles, and no hitch the first time a pass runs.
- **Touches.** `ShaderCompiler.cpp`, `VulkanPipeline.cpp`, `Renderer3D.cpp` (`CompileLitShaders`; the pipelines created at first use at 7924-8046), the build scripts.
- **Scope.** Patch. **Depends on.** RT2-1 (D6 approved). It needs none of the rewrites (the first draft had it wait for the RHI rewrite for no reason). The load-time builds use plain worker threads until RT2-36's job system takes them over. **Risk.** Low.
- **Exit gate.**
  - Launch time and RT-toggle time, before and after, as a palindrome.
  - The first ray-traced frame's time, from the slow-frame log.
  - Frames with and without the cache, and with `spirv-opt`, pixel-identical; otherwise `spirv-opt` is dropped.
- **Size.** 2-3 sessions.
- **Closes.** GPU-08, ISS-35; the cache part of MAT-11.

### RT2-21b · One BSDF for every path (split from RT2-21, moved to M2)

- **In plain words.** Today the same surface is shaded by slightly different maths depending on which pass looks at it: three different versions of one BRDF term, no energy compensation (so rough metals lose light), and the extra lobes (clearcoat, sheen, brushed metal) exist only in raster mode. This step writes one BSDF module that every pass uses. It comes first in M2 because RT2-10's area-light tables are fitted to one BRDF. Each visible change is shown to the owner as its own arm.

> **Split from RT2-21 by the comparison (2026-09-25).** RT2-21 bundled five independent changes with
> different dependencies. The BSDF has to precede RT2-10's step 2, whose fitted LTC tables assume one
> BRDF, so it becomes this separate step in M2. RT2-21 keeps steps 21a, 21c, 21d and 21e in M4.

> **Owner's ruling D27 (2026-09-25).** Brushed-metal (anisotropic) materials in RT mode now, without
> waiting for an asset: the tangent direction is carried in the G-buffer and through DirectTrace, ray
> hits and the composition. This module carries the anisotropic model in every path; the tangent lane
> arrives with RT2-21a and the hit frames with RT2-21d.

- **What is wrong now.**
  - Five BSDFs depending on the path (MAT-02): DirectTrace uses GGX plus Lambert only; clearcoat, sheen, anisotropy and wrap exist only in the raster loop, so with the direct signal on they drop out of every analytic light (direct-10); GI and bake hits are Lambert-only, so metals bounce nothing (GI-09).
  - Three Smith terms (F-C3): Karis's k = (r+1)^2/8 for analytic lights, DirectTrace and hit specular (`pbr_fragment.glsl:3433-3444`, `direct_trace.rvshader:455-456`); k = alpha/2 in the split-sum table (`Cubemap.cpp:252-261`, used through `EnvironmentIBL.cpp:311`); the exact GGX term in the reflection's sampling density (`reflection_emitters.glsl:62-86`). Besides them: Kelemen (coat), Ashikhmin (sheen) and Beckmann (water). The reflection's MIS, the table's weight and the lamp highlight are three different BRDFs.
  - No energy compensation anywhere (F-C4): every split-sum use is F0 x envBRDF.x + envBRDF.y with no correction, so rough metals lose energy in raster, DirectTrace, hits and the composition alike. HDRP and Filament ship the correction.
  - Hit diffuse is albedo x (1 - metallic) with no (1 - F) (F-C5, `pbr_fragment.glsl:2802`), while raster and DirectTrace use (1 - F)(1 - metallic); with the hit specular on top, a hit double counts at grazing angles.
- **What changes.** One module, `include/bsdf.glsl`. Per shading model (standard; coat, sheen and anisotropic moved as they are from the raster loop; water's Beckmann lobe as a model RT2-32 owns), it provides: Eval (diffuse and specular), Sample (visible-normal sampling), Pdf (the exact term), and MaterialFactors(N, V, roughness, F0, albedo, metallic), which gives the diffuse albedo and the specular albedo S.
  - Specular: GGX with alpha = roughness squared, and the height-correlated Smith term with the exact Lambda, one form for evaluation and pdf, with the split-sum table (same 128x128 `R16G16F`) regenerated from it.
  - Energy: the lobe scaled by 1 + F0 (1/E - 1), with E read from that table (Turquin 2019; the same form Filament and HDRP ship), applied to analytic lights, hit lighting and the split-sum weight.
  - Diffuse: Lambert with kD = (1 - F)(1 - metallic) in every path, hits included.
  - Kelemen, Ashikhmin and the water's Beckmann term stay as the coat, sheen and water models' own forms.
  - Every consumer includes it: the raster lit pass (so the raster recipe stays on par, RT2-44), DirectTrace, TraceSurface and ShadeTraced (reflection, GI, bake, water and glass hits), the resolve, and later the composition (RT2-22).
  - GI and bake hits can take the specular term behind a flag (`--hit-specular`), off by default. Whether they use it (GI-09) is decided only in RT2-25, which owns the hit-shading contract; RT2-26's re-bake then bakes whatever RT2-25 adopted.
  - Each look change is its own arm, shown one at a time: the Smith swap, the energy compensation, the hit kD.
- **Owner sees.** Rough metals a little brighter (the energy they lost is back). Highlights that agree between raster and RT mode, and between a surface and its reflection. Coated, sheen and brushed materials shaded the same way in RT mode as in raster. (Metals that bounce light in GI come with RT2-25, if its GI-09 gate adopts the hit specular.)
- **Touches.** New `include/bsdf.glsl`; `pbr_fragment.glsl` (3433-3444, 2802, 5326-5392, 5352-5363), `direct_trace.rvshader` (453-471), `Cubemap.cpp` (252-261), `EnvironmentIBL.cpp` (311), `reflection_emitters.glsl` (62-86), `rtgi_trace.rvshader`, `irradiance_fill.rvshader`.
- **Scope.** Refactor. **Depends on.** RT2-2 (the reference renderer and the white-furnace spheres). It must land before RT2-10's step 2. **Risk.** Medium: the look changes on purpose, and the bake is re-run (not hand-edited, rule 5).
- **Exit gate.**
  - The white-furnace spheres (F0 = 1 and albedo 1, roughness 0 to 1 in 0.1 steps, a uniform unit environment) read 1.0 within 2% in the lit pass, in a mirror reflection of the fixture (the hit path), and in the reference renderer.
  - Each look arm (the Smith swap, the energy compensation, the hit kD, each with a re-bake where it changes the bake's hits) as a diff image against the reference renderer, judged by the owner; the bridge's red-to-blue ratio and mean checked first.
  - The hit specular flag: off, pixel-identical to the build without it; on, its term matches the lit pass's specular for the same surface (the white-furnace spheres through the hit path). Its adoption is RT2-25's gate, not this item's.
  - RT-24's numbers at the owner's swing no worse: speckle 513 and 85 dots over 32 and 64 levels, edge jitter 9.51.
  - A,B,B,A: each traced pass and the lit pass within +0.05 ms at 2560x1600.
- **Size.** 2-3 sessions.
- **Closes.** The five-BSDF half of MAT-02, direct-10, the BSDF term of GI-09 (RT2-25 decides whether hits use it); the comparison's claims F-C3, F-C4 and F-C5 (section 8).

### RT2-10 · One light library; area lights shaded as areas everywhere

- **In plain words.** The same light maths is typed out eight times and the copies have drifted; at ray hits every lamp is a single point, so a 3-metre tube shows up as a pinpoint in the chrome. This item puts all light maths in one library, then shades tubes and spheres as real areas everywhere, adds rectangle and disk lights and a soft sun, and picks lamps by exactly the light they will give. The form is decided: the light without shadows, computed exactly, times one ray to a point on the lamp, which is what MegaLights ships.

> **Part done early, in RT-24 (2026-09-24):** the tube highlight's length energy -- the radius was
> renormalised and the length was not, so a 3.07 m tube drew its full peak along its whole length.
> Now the same rule as the radius, in the one dimension the length widens (DirectTrace and the lit
> loop). The garage's tube shine fell to about a third; the owner judged it good. **Still owed:** the
> brute-force-tube comparison this item asks for.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** The sized-lamp
> highlight. On the sea the direct trace stretches a lamp's streak by fixed factors and renormalises it
> by a square root (live on the bridge's water, first to fix); the water's own lamp shading and the lit
> water each add a stretch factor of their own (the first is live); the direct light
> scores a lamp's highlight with a different lobe than it shades with (live, first to fix); and a hard
> shadow is recognised by a ray length of 10 km instead of the light's kind. General form: the lobe
> evaluated against the lamp's real extent with an exact energy normalisation, the score from the
> shading's own term, the light's kind passed explicitly.

> **Changed by the comparison (2026-09-25).** Step 2 stops being a choice between two forms. The
> MegaLights talk shows that the shipped form is the analytic light without shadows times the
> visibility of one ray ("pre-integrated irradiance x sample weight"), which is RageV's form too, so it
> is the baseline (rule 1) and the sampled-point estimator stays as the reference arm. The library also
> owns the source-point sampler with its pdf, the shadow query returns the occluder's distance (RT2-24
> sizes its spatial pass from it), and the light's kind is passed explicitly. The score becomes the
> shaded term itself; the verification found the mismatch covers sized spheres too, not only tubes
> (A-N3). Hits also get the widened lobe, because their specular is point-like today (A-N4). The every-light
> arm (K=0) checks light selection only; the truth for every picture gate is `--reference-render`.

- **What is wrong now.**
  - The range window, spot cone, sized-source highlight and water lobe are typed out eight times in three files, and the copies have drifted. The tube's length reaches the lit loop and DirectTrace but not ray-hit shading or the sea's fallback, and three comments contradict the code.
  - At ray hits every lamp is a point with a hard shadow (`pbr_fragment.glsl:2971-3096`), so a 3.07 m tube is a pinpoint in the chrome and the car. Hit specular is point-light GGX with no widening (`pbr_fragment.glsl:3127-3136`, `3155-3160`; A-N4).
  - Sized lights are treated as points for diffuse, cone and range.
  - A tube's single 0/1 shadow sample scales its whole highlight, so a partial occluder re-rolls the highlight every frame (under TAA's jitter; without it the point is frozen, which is a fixed bias instead).
  - The tube highlight's energy grows with the tube's length: only the radius is renormalised. It has not been compared with a real tube since RT-21 removed the highlight cap.
  - DirectTrace's score uses the unwidened lobe with no radius or length renormalisation, aimed at the light's centre, while the shading uses both (A-C1, `direct_trace.rvshader:586-591` against `396-414`): sized spheres and tubes are picked by a different light than they give (live, first to fix).
  - The sea lobe's stretch factors and square-root renormalisation are hand-set (audit 2, live).
  - The shadow ray returns only 0 or 1, with no occluder distance, and a hard shadow is recognised by a 10 km ray length.
  - There are no rectangle or disk lights, and the sun is hard.
- **What changes.**
  - **Step 1, pixel-identical.** One include, `include/lights.glsl`, holding per light type (sphere, tube, rectangle, disk, directional with an angular radius; spot cone and range window as modifiers): (1) the light without shadows; (2) a function that picks a point on the source, with its pdf; (3) the selection score; (4) the shadow query, which takes the light's kind as an argument and returns visibility and the occluder's distance. DirectTrace, ray-hit shading, the water, glass and the bake all call it.
  - **Step 2, one arm at a time** (after RT2-21b):
    - the decided form: the analytic light without shadows times the visibility of one ray to a sampled point on the source, with the sampled-point estimator (the light evaluated at the traced point, with that point's pdf) kept as the reference arm. The parts: tube diffuse by the closed-form line integral; sphere and tube specular by a representative point with exact energy normalisation (the radius in both axes, the length in one, as RT-24 built); rectangles and disks by LTC; spheres sampled by solid angle; the sun by a cone of its angular radius;
    - the score becomes the luminance of that shaded term (sized spheres included), which removes the live pick-shade mismatch; this audit row moves here from RT2-12 because the score is the library's third function;
    - ray hits shade tubes and spheres as areas, with the widened lobe and a soft ray to a sampled point;
    - the sea lobe's stretch factors give way to the exact normalisation (ax0 x ay0) / (axS x ayS), the same rule as the land path, applied once in the wind frame;
    - the tube length's energy is fixed after a comparison with a brute-force tube;
    - rectangle and disk types, and an angular radius for the sun.
  - **Later arms**, each adopted only if it wins its gate: an LTC line integral for tube specular, if it lands nearer the brute-force tube than the representative point; highlight-aware points on area lights (projected-solid-angle sampling of tubes and rectangles, Peters 2021, with one-sample MIS between a diffuse-shaped and a lobe-shaped point), judged under `--aa=none` after RT2-7, because today's frozen point is a fixed bias rather than flicker.
- **Owner sees.** Tube reflections in the chrome and the paint become streaks. Highlights near partial occluders stop flickering. Soft sun shadows where a scene has a sun. Less noise on glossy floors under tubes, because lamps are picked by the light they really give.
- **Touches.** New `include/lights.glsl`; `direct_trace.rvshader` (329-593, 440-442), `pbr_fragment.glsl` (2971-3096, 3127-3160, 4652-4895, 5102-5242, 5293-5302), `water_lamps.glsl` (192-404, 265), `ray_shadow_trace.glsl` (232-256, 340-404), `Light.h`, `Renderer3D.cpp` (light packing, 4037-4060), `LightGlow.cpp`, the light inspector.
- **Scope.** Refactor. **Depends on.** RT2-2 (the reference renderer, the brute-force tube), RT2-7 (the sample service), RT2-21b (before step 2). **Risk.** Medium: step 2 changes the look on purpose.
- **Exit gate.**
  - Step 1: the every-light arm (K=0) and the default frames diff 0 on the garage, the bridge's three cameras and the camp. Drifts fixed on purpose are shown separately.
  - Step 2: a single tube and a single sphere over a glossy plane at roughness 0.1, 0.3 and 0.6 against `--reference-render`: a diff image with no structure beyond grain, integrated energy within 2%. On the garage, region by region (floor, car, poles; `truth_test.py`'s distance and bright-speck count) no worse; tubes seen in the chrome and the car at ray hits are streaks, diffed against the reference at the hit region. On the bridge: per-pixel diffs at the three cameras with the mean brightness and the red-to-blue ratio within 1%, and `check_glint_flicker.py` at or below 0.97%. A,B,B,A cost per consumer at 2560x1600.
- **Size.** 5-6 sessions.
- **Closes.** direct-06, direct-05, direct-s1, ISS-5; the area-light half of ISS-28; the comparison's claims A-C1 and A-C8 and findings A-N3 and A-N4 (section 8).

### RT2-11 · Emissive geometry as sampleable lights; every fixture counted once

- **In plain words.** Glowing surfaces (tube bars, a lit screen, the car's lamps) are today stood in for by their bounding rectangles, so a merged mesh radiates over empty space, and a lamp made of a Light plus a glowing mesh is counted twice. This item turns every glowing surface into a proper light built from its own triangles. By the owner's ruling D13, a Light can name its glowing mesh in an optional field set by hand; the pair's shine on shiny surfaces is then shared between the direct pass and the reflection pass by the standard weighting, so each fixture is counted once and shows its real shape everywhere.

> **Part done early, in RT-24 (2026-09-25):** the 16-slot cap is gone. The reflections now pick among
> every listed emitter in one pass, with weighted reservoir sampling and nothing stored per row. The
> bounce pass picks evenly among all of them, where before it subtracted the glow of every flagged
> emitter but aimed at only 16. In the garage, 7 of the 23 fittings had never been aimed at; their lucky
> hits were most of the fast-swing speckle. Owner-speed swing: motion speckles 861 -> 513 (white
> 117 -> 85), pole jitter 15.2 -> 9.5, pole grain 13.9 -> 7.7. That is close to the 16-ray reference
> (494 speckles, 8.8, 8.1). The parked floor is now 1.4 levels from the 16-ray reference, where it
> was 3.5. Cost: about +0.5 ms in the trace pass, within the frame's noise overall. **Still owed here:**
> emitters from triangles instead of bounding rectangles, the luminaire link, D7's glowing surfaces
> without lights, and a picking cost that does not grow with the count (RT2-12).

> **Added from RT-24's hard-coded-values audit (2026-09-24; section 10 lists every value).** Already
> here: the 16-slot cap (the audit found 7 of the garage's 23 glowing fittings never aimed at, whose
> lucky hits then skip the resolve's clamp) and the 12-step search. Added: each glowing surface is
> scored for aimed rays at three fixed points (the centre and both ends of its long axis). A poor score
> costs only noise and no visible effect was found, but a count of points is a guess at the shape.
> General form: score at the point on the emitter's axis nearest the lobe's main direction (a
> closed-form closest approach), plus the centre.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Confirmed: the
> first 16 emitters in scene order, each as its bounding box, and the same cap in GI (this item).
> Added: a surface counts as an emitter only above an emission of 1.0 (general form: it emits at
> all), and the ray-structure code repeats that threshold as a second literal.

> **Owner's ruling D13 (2026-09-25).** A fitting's shine is counted once by the research fix: glowing
> meshes are light sources (this item), and the direct pass and the reflection pass share the shine by
> the standard weighting (each pass's share from how likely it is to find the mesh from that pixel,
> shares always summing to one), so every surface shows the mesh's real shape. To combine a Light with
> its glowing mesh, the Light gets an OPTIONAL field naming its linked glowing mesh, set by hand; there
> is no automatic pairing.

> **Changed by the comparison (2026-09-25).** Emitters become clusters of emissive triangles (what RTXDI
> does), each with bounds, a facing cone and power, split until their area fills their bounds; an alias
> table serves only the pick of a triangle inside a cluster. A surface is an emitter if it emits at all.
> The cluster index goes into the ray-instance record, so finding a hit's emitter is one read. Clusters
> are built as leaves for RT2-12's light BVH. DirectTrace samples emitters too, first as D7's arm. The
> GI pass's even pick becomes the reflections' weighted pick. **Rejected from the comparison's
> proposal:** the luminaire link derived automatically from where a Light sits inside a glowing mesh.
> The owner ruled for a hand-set optional field instead (D13), so no pairing is inferred. The
> verification added the reason a single representation is required: MIS counts a fixture once only if
> the light sampler and the mirror ray see the same surface, and an analytic Light has no surface a
> mirror ray can strike (B-BB3).

- **What is wrong now.**
  - Until RT-24 (2026-09-25), only 16 glowing surfaces got aimed rays in GI and reflections, taken first come, first served in scene order. The garage's 20 tube bars and the car's lamp parts competed for those 16 slots (`Renderer3D.cpp:3118-3121`). Now every listed emitter is aimed at, but only those with an emission above 1.0 are listed (`Scene.cpp:2224`, repeated at `RayShadows.cpp:287`), and the GI pass picks among them evenly (`rtgi_trace.rvshader:383-386`).
  - Each surface is reduced to its bounding rectangle, its thinnest box axis taken as the normal. A merged mesh, such as all of the bridge's lamp lenses as one entity, therefore radiates over empty space. In the realtime GI fallback that is orders of magnitude too much orange light, which may be the cause of the recorded red bridge. The reflections' MIS pairs that rectangle's pdf with lobe-ray hits on the real mesh, so it is only approximately MIS (B-N2).
  - A fixture made of a light plus a glowing mesh is counted twice. On glossy surfaces the analytic highlight adds to the traced image of the lens. On diffuse surfaces the live direct light adds to the lens emission stored in the bake (`irradiance_fill.rvshader:279`).
  - The texel-emitter tables engage only on flat, untransformed planes. Their sampler is a 12-step search through host-visible memory, re-uploaded every frame.
  - DirectTrace never samples emitters.
- **What changes.**
  - **Emitter clusters.** A persistent GPU emitter table, built from a mesh section's emissive triangles, grouped into clusters that split until their emitting area fills their bounds. Each cluster carries its bounds, a facing cone and its power (area x mean emission, texture included); an alias table picks a triangle inside a cluster by power; texel tables stay where a map varies inside a triangle set. Membership: the surface emits anything (strength above 0), decided once and passed to the ray-structure code (no second literal). The table is updated when emissive geometry or an emissive texture changes; an animated emissive texture (a screen, a sign) refreshes its cluster's power on the frames it animates.
  - **Picks without a search.** The ray-instance record carries the cluster index. Until RT2-12 lands, hits pick a cluster by resampling a few candidates drawn by power from the alias table (the reflections' weighted pick, now also in GI); after it, clusters are leaves of the light BVH. The 3 fixed score points become the closed-form closest point on the emitter's axis to the lobe's direction, plus the centre.
  - **DirectTrace samples emitters**, first as D7's arm: every glowing surface without a Light of its own becomes a light source, shown to the owner before it becomes the default.
  - **The optional link (D13).** A Light gains an optional field naming its glowing mesh, set by hand in the inspector. For a linked pair:
    - the fixture's shine on glossy surfaces comes from the mesh: DirectTrace samples a point on the mesh's clusters with the light library's pdf, the reflection's lobe ray may strike the same mesh, and the power heuristic between the two pdfs gives each pass its share, so the pair is counted once and every surface shows the mesh's real shape (RT-7's rule that the lens stays visible to mirror rays holds). The reflection pass's own aimed lamp sample skips a linked mesh, because DirectTrace's sample is that share. The brightness of that shine is the mesh's own emission, as mirror reflections show it today;
    - the Light carries the diffuse direct light, its cone, range, shadows and bake share, and its own analytic specular is dropped. The mesh is excluded from diffuse next-event estimation, the diffuse bounce and the bake; it is sampled only for the glossy shine, as above;
    - the mesh does not block its own Light's shadow rays (the ray ends at the lens, as MegaLights recommends).

    **What this item delivers in M2.** The link field; the power-heuristic shares between DirectTrace's mesh sample and today's reflection lobe ray, each share still kept in its own signal (DirectTrace's specular and the reflection), with the lobe's pdf taken from today's reflection sampler until RT2-23's step 23a writes the exact one; the Light's own analytic specular dropped; the mesh left out of diffuse next-event estimation. RT2-23g later merges the two shares into one specular signal with the same weights. Keeping the mesh out of the diffuse bounce lands with RT2-25's layering rule, and out of the bake with RT2-26, each checked by that item's gate.

    An unlinked Light and an unlinked glowing mesh are two sources. Linking the demo scenes' fittings is the owner's authoring step, not part of this item; the editor lists every Light whose source lies inside an unlinked glowing mesh, as a report only, so none is missed.
- **Owner sees.** Calmer tube reflections, because every bar is aimed at by its real shape. Glowing objects with no light of their own, like RT-22's cube, light what is around them (once the owner has judged that arm). Linked fixtures whose shine on the wet floor is the lens's real shape, counted once: on the linked fixture scene at once, and in the demo scenes once the owner has linked their fittings. On the bridge, the realtime fallback without its saturated orange (if the bounding rectangle was the cause). (The slightly dimmer garage floor where the bake counted the bars twice comes with RT2-26's re-bake, after the owner links the fittings.)
- **Touches.** `Scene.cpp` (2167-2260), `Renderer3D.cpp` (1505-1540, 3108-3135, 5604-5637), `RayShadows.cpp` (287), `rtgi_trace.rvshader` (365-587), `reflection_trace.rvshader` (343-589, 683, 829-860), `include/reflection_emitters.glsl` (157-180), `irradiance_fill.rvshader`, `pbr_fragment.glsl` (3177, 3326), `direct_trace.rvshader`, `Light.h` (the link field), the light inspector, the texel-emitter machinery.
- **Scope.** Rewrite of the emitter model. **Depends on.** RT2-2, RT2-5, RT2-10 (the light-sampling pdfs the weighting needs). Later items finish the single count: RT2-23g merges the two specular shares into one signal; RT2-25 keeps linked meshes out of the bounce and RT2-26 out of the bake, each with its own gate. **Risk.** Medium.
- **Exit gate.**
  - Every emitter cluster in the scene reachable by aimed samples (a count in the log).
  - A linked tube fixture (a new fixture scene with the link set in the fixture itself, rule 5) over a mirror plane and a rough plane at three roughnesses, against `--reference-render` (which applies the same link rule): the glossy shine (DirectTrace's mesh share plus the reflection's lobe share) within grain, which proves the single count in the specular path; the diffuse direct light, carried by the Light alone, within grain.
  - The garage's own fittings are linked only by the owner's authoring step. Until then the same check runs on a copy of the garage made by the harness, whose tube fittings the harness links; the copy is never saved over the scene or committed (rule 5): the floor's glossy region against `--reference-render` with the link rule, within grain, and the size of the specular double count removed (the linked copy against the unlinked garage) recorded. (The bake and bounce halves of the double count are measured in RT2-26's and RT2-25's gates.)
  - Reflections: `truth_test.py` no worse in any region, and the tube patch's grain score at the owner's swing lower than today's.
  - D7's arm: a glowing object with no light against the reference renderer, within grain, judged by the owner.
  - The bridge's realtime fallback (bake invalidated) diffed against the baked frame, with the mean and the red-to-blue ratio reported.
  - A,B,B,A.
- **Size.** 5-7 sessions.
- **Closes.** direct-03, gi-s1 (the link and the specular half; RT2-25 and RT2-26 close its bounce and bake halves), gi-s3, GI-08, REFL-06, ISS-13, ISS-19; the luminaire half of ISS-28; the comparison's claim A-C10 and finding B-N2 (section 8).

### RT2-46 · Lamp shells opaque to rays again (new, 2026-09-25)

- **In plain words.** Every ray that crosses any lamp fitting today runs slow extra shader code, only because the aimed rays had to pass through the lamp's glass shell to reach the rectangle standing in for the lamp. Once RT2-11 aims at the lamp's own triangles, the shells can be solid to rays again, and every ray pass gets cheaper. The picture should not change.

> **New, from the comparison (2026-09-25).** Proposed as "RT2-11b"; numbered RT2-46 because new items
> take numbers from 46 upward (only splits take letters). No shipped engine forces its lamp shells
> through the slow path. This is not section 7's item 10 (taking the shells out of shadow rays by
> instance mask, measured wrong in RT-11): here the shells stay in and block light, and only the aimed
> ray stops short of its own sample point.

- **What is wrong now.**
  - Every emitter instance is marked non-opaque (`RayShadows.cpp:285-289`), so any ray crossing one of the garage's 23 fittings runs the candidate loop and reads the ray-instance row (`ray_shadow_trace.glsl:100-117`), in every variant that carries the instance table.
  - A pass-through test compares the folded 1..1021 identity (`g_ShadowThroughEmitter`, `ray_shadow_trace.glsl:89-110`, `rtgi_trace.rvshader:517-520`).
- **What changes.**
  - Emitter instances become opaque again; `g_ShadowThroughEmitter` and its identity comparison are deleted.
  - An aimed ray ends at the distance to its sample point on the emitter's triangle, shortened by today's ray-origin offset (the fixed lift the shadow rays already use), so it never strikes its own emitter. In M4, RT2-21e replaces that offset with the float-precision one, here as everywhere else.
  - Only alpha-masked instances stay non-opaque.
- **Owner sees.** Nothing on screen. Some time back in every ray pass (measured).
- **Touches.** `RayShadows.cpp` (283-289), `ray_shadow_trace.glsl` (89-117), `rtgi_trace.rvshader` (517-520), `reflection_trace.rvshader` (the aimed ray's length), `direct_trace.rvshader`.
- **Scope.** Patch. **Depends on.** RT2-11 (emitters sampled on their own triangles). **Risk.** Low.
- **Exit gate.**
  - Garage parked and swing diffs within the parked noise floor against before.
  - The RT-11 back-wall check: no wall converging brighter than the reference.
  - DirectTrace, ReflectionTrace and GI trace times, A,B,B,A: lower or equal.
- **Size.** 1 session.
- **Closes.** The comparison's claim E-C5 (section 8).

### RT2-12 · Light candidates at a fixed cost per pixel, shared by every pass

- **In plain words.** Today the cost of picking lights grows with the number of lights: DirectTrace scores every light near a pixel (up to 190 on the bridge) and a reflection hit walks every light in the scene. This item builds one light tree (a light BVH) over all lights and glowing surfaces, which every ray hit uses to pick a light at a cost that barely grows with the light count. It is measured against the light grid Doom: The Dark Ages ships (nested grids around the camera, built on the GPU), which is built first as the baseline (rule 1). The picking at the camera's own pixels moves to RT2-12b. By the owner's ruling D12, an arm here then tests moving the FullBake and HybridFullBake lamps to HalfBake, and the owner decides from it.

>  **Measured case, 2026-10-15 (the bridge's deck lamps raised to 3000 m, owner).** With every deck
> lamp in reach of every sea mirror hit, the hits' weigh-every-lamp walk costs 2.3 ms at Headland
> (scene/Transparent 5.7 -> 8.0; `--hit-lights=off` removes the difference). Left for this item by the
> owner; it is the number this item's light tree should take back.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** The Hybrid
> lamps' cull distance is packed in eighths of a metre in 13 bits and capped at about 1 km by rounding
> down, so a lamp reaching further loses its light beyond that, a dark ring (the bridge's lamps are far
> from it; general form: all ones means "never reject", or a rounded-up half float); the world
> grid's fixed 8x4x32 cells (this item); the orthographic cluster range; and the neighbour-sharing
> rings of the direct light and the water (a fixed spacing and a water-only normal tolerance).

> **Part done early, in RT-24 (2026-09-25):** the reuse arm's spatial half, for the reflections'
> aimed lamp samples. Each reflection texel pools its neighbours' lamp points the way ReSTIR does:
> each point is weighed again from the texel's own position, with pairwise MIS over the samplers' own
> densities, one is kept, and one shadow ray is traced (`reflection_resolve.rvshader`, behind
> `--reflection-lamp-pool=<n>`, 0 = off). This replaces fix 1's unshared aimed light without bringing
> back the halo. Owner-speed swing, 4 / 8 neighbours: car grain 7.01 -> 6.01 / 5.84, tube grain
> 10.57 -> 9.35 / 9.07, floor sharpness while moving 1.12 -> 0.95 / 0.91. The parked floor is 3.2-3.5
> levels closer to the 16-ray reference. Cost at 1600x900: +1.0 ms (4) / +2.1 ms (8), left for the
> optimisation pass. **Still owed here:** temporal reuse, and reuse for the direct light and GI.
> *(Corrected 2026-09-25: temporal reservoir reuse is not planned, section 7 item 5. Spatial pooling
> for the direct light is RT2-12b's later arm. The reflections' lamp pool is replaced by the one
> specular signal, RT2-23g, which samples each lamp's shine once from this item's structure.)*

> **Owner's ruling D12 (2026-09-25).** Baked direct light for FullBake and HybridFullBake lamps: keep the
> question. "Maybe we will move to half baked once choosing becomes cheaper": after this item makes
> choosing the K lamps cost the same at any lamp count, test moving those lamps to HalfBake (live direct
> light, baked bounce); the owner decides from that.

> **Changed by the comparison (2026-09-25).** Split: this item keeps the structure and every ray hit; the
> primary-pixel front end becomes RT2-12b. The choice "a light BVH or ReGIR, the owner picks" is decided:
> one world-state, two-level light BVH, because it needs no cell size in metres or camera-relative view
> state (rule 2) and one structure serves every view (the ray world is world state). ReGIR stays only as
> the named fallback if the BVH misses its cost gate. The world-grid lookup of RT2-6 arm 10 is only
> RageV's interim at hits (not Doom's form: its grid is scene-shaped). *(Corrected after the second
> critic's review: the rule-1 baseline is what Doom: The Dark Ages actually ships, a camera-centred,
> cascaded world light grid built on the GPU, 16^3 cells per cascade with cells growing with distance and
> no fixed box. It is built here as the comparison arm, and the BVH is gated against it at equal time.)*
> Verification corrections:
> Moreau 2019's light-BVH figures are update 0.18-0.89 ms and two-level sampling 2.2-2.6 ms (12.0-12.6 ms
> frames), on a GPU the slides do not name, so the cost at 4.1 megapixels on this laptop is unknown until
> measured; 150 lights is the bridge's busiest cluster, not every cell; the world grid's box does follow
> the lights' reach, with a fixed 8x4x32 count and axes shaped to the bridge.

- **What is wrong now.**
  - DirectTrace scores every light whose range box touches the pixel's cluster cell, so its cost grows with the number of overlapping lights: 22 per pixel in the garage; 67 to 118 on average (190 at most) on the bridge, where the busiest cluster holds up to 150 of the 189 lights. (This half moves to RT2-12b.)
  - Ray hits walk every light in range: the trace-only passes compile the cluster and world-grid lookups out, so every hit visits every light's 16-byte cull record, and RT-11 keeps one light by reservoir after walking them all (`pbr_fragment.glsl:2851-2941`, `3098-3130`). RT2-6 (arm 10) lets them use the world grid, but the cost still grows with the lights in range.
  - The world grid has a fixed 8x4x32 cell count with axes shaped for the bridge, which breaks the rule against tuning for one scene. Both grids are rebuilt with a sort on the CPU, per view, every frame.
  - Baked lamps with any moving object in range trace one extra ray each, outside the K budget.
  - The "moving object in range" flag costs lights x non-static meshes on the CPU, per view.
  - The Hybrid lamps' cull distance is capped at about 1 km by its packing.
- **What changes.**
  - **One light BVH, decided.** One persistent light buffer (analytic lights plus RT2-11's emitter clusters), updated from change lists. Over it, a two-level light BVH: a top level over lights and emitter clusters, a bottom level per emitter mesh. Each node holds its bounds, a facing cone and its power, and nodes are scored by power x the orientation bound / distance squared (Conty-Estevez and Kulla 2018). It is built and refit on a worker thread from the change lists and uploaded incrementally, one structure per frame for every view; RT2-35 and RT2-36 later take over the build.
  - **Every hit draws from it**: reflections, GI, water and glass, and the transparent surfaces RT2-13 and RT2-48 move over. A hit draws one light by a stochastic descent (one blue-noise value with sample warping, RT2-7b) and traces one shadow ray. This replaces the trace-only every-light walk and RT-11's reservoir over the walked list. The structure exposes the pdf of any point on any light as seen from a shading point, which the one specular signal (RT2-23g) and the fixture link (D13) need.
  - **The baseline arm (rule 1): the cascaded light grid Doom: The Dark Ages ships**, behind `--light-candidates=grid`. Camera-centred cascades of world-space cells, 16^3 per cascade, each cascade's cells twice the size of the one inside it, built each frame on the GPU by one compute pass that culls every light's and emitter cluster's bounds against the cells and writes each cell's light list. The base cell size comes from the projection's pixel angle (a cell spans a fixed number of pixel footprints at its cascade's inner distance), not from metres (rule 2), and nothing is a fixed box. A hit reads its cell's list and picks one light by resampling the listed lights (RT-11's reservoir), with one shadow ray. The BVH is adopted at hits only if it matches or beats this arm at equal time (the gate below).
  - The bake keeps its exact walk over baked lamps, since bake time is cheap.
  - `WorldLightGrid` (8x4x32) is deleted; the per-view cluster grid survives only for the raster recipe once RT2-13 lands.
  - The baked-lamp loss candidates enter the same sampler. "Moving object in range" is worked out once per frame, only for FullBake and HybridFullBake lamps, by testing moving objects' boxes against BVH nodes.
  - The Hybrid reject distance uses all ones for "never reject", or a half float rounded up.
  - **The D12 arm.** With lamp choosing flat in the light count, the FullBake and HybridFullBake lamps are moved to HalfBake in an arm (their direct light live, their bounce still baked). It is measured on the bridge with moving cars: diff images against the full bake and the reference renderer, and cost, A,B,B,A. It would delete the loss rays outside K, the field-share logic in DirectTrace and the Hybrid transition bands, and a switched lamp would no longer need a re-bake for its direct light. The owner decides from the measurement; until then D4's baked direct light stands.
  - **Fallback, named only for a failed gate.** If the BVH misses its gates (its cost, or its grain against the cascaded-grid arm at equal time), the hits use the cascaded grid with ReGIR's slots in its cells (RTXDI's shipped form for secondary hits), whose cell budget is split across the lights' own bounds and which keeps the pdf of every pick for RT2-23g and D13.
- **Owner sees.** Cheaper ray hits on the bridge. The garage's converged picture identical to the reference. The many-light scene becomes affordable at ray hits. Then, for the owner to judge, the bridge with every lamp's direct light live.
- **Touches.** `LightGrid.cpp` and `LightGrid.h`, `Renderer3D.cpp` (104-122, 4289-4355), new light-BVH build code and upload, the cascaded-grid arm's compute pass, `pbr_fragment.glsl` (2808-2946, 3098-3130), `water_lamps.glsl`, `reflection_trace.rvshader` (the emitter walk), `Scene.cpp` (3052-3102, 2124-2127).
- **Scope.** Rewrite. **Depends on.** RT2-2 (2d: the many-light scene with rooms whose ranges cross walls), RT2-7, RT2-10, RT2-11 (clusters as leaves). **Risk.** High: the structure does not see walls, so the many-light scene must include rooms whose light ranges cross them (RT2-12b's guiding answers that at primary pixels).
- **Exit gate.**
  - The many-light scene at 1k, 5k and 10k lights: hit-lighting time grows from 1k to 10k by no more than the palindrome spread (the curve recorded), and the number of nodes visited per hit stays at or below 2 x ceil(log2 N) + 4 (the hit counter; two children are scored per level, and the 4 covers the two BVH levels' roots and the rounding of splitting the depth across two levels).
  - The converged diff against `--reference-render` shows no structure beyond grain on the garage, Headland and the many-light scene; at equal time at hits, the BVH's converged grain at or below the cascaded-grid baseline arm's (Doom's form, rule 1), on the same three scenes.
  - The bridge with every lamp live: a lamp whose range exceeds 1 km still lights at 1.2 km (no dark ring).
  - The BVH build plus refit at most 0.5 ms of CPU at 10k lights with 10% moving; upload bytes in proportion to the changes.
  - Garage and bridge: `smear_metric.py` (the dolly) and `emitter_lag.py` no worse. A,B,B,A on the garage, Headland and Glitter.
  - The D12 arm: its diff images and cost reported to the owner.
- **Size.** 7-9 sessions (one more than first planned, for the cascaded-grid baseline arm).
- **Closes.** direct-02 (the final half), direct-08, direct-12, direct-s3, GEO-15, ISS-18 (with RT2-12b); the final half of GI-06; the comparison's claims A-C2, A-C5 and A-C14 (section 8).

### RT2-12b · Primary direct light guided by last frame's visible lights (split from RT2-12)

- **In plain words.** At the camera's own pixels, DirectTrace should spend its K picks on the lamps that actually light the pixel. This step copies MegaLights' front end: each 8x8 tile remembers which lamps were visible last frame, the next frame evaluates those exactly and draws the hidden ones from the light tree, and one blue-noise value drives all the picks. Its cost stops growing with the number of lights. If full-resolution sampling is still too slow at 2560x1600, the owner's own 2x2 design becomes the cost tier (D14).

> **Split from RT2-12 by the comparison (2026-09-25).** Visibility guiding is the one piece that lets a
> bounded set of candidates keep the important lights (MegaLights talk 290-409), and it answers
> RT2-12's recorded risk of lights behind walls. It has its own baseline arm, so it is its own step.

> **Owner's ruling D14 (2026-09-25).** If per-pixel direct lighting is too slow at 2560x1600: the owner's
> own design. Share the lamp picks and shadow rays within each 2x2 block, and cycle which of the block's
> 4 pixels fires them, one per frame, so a still camera settles to the exact per-pixel result after 4
> frames. Softer shadow edges while moving are accepted ("while moving things are blurry anyways"). The
> 4 frames are combined in the direct light's own accumulator, not TAA. It becomes the default if it is
> good enough (the owner judges). *(This replaces the comparison's proposal of MegaLights' 2x2
> downsampled sampling, which its own talk says does not converge to the full-resolution result;
> section 7.)*

- **What is wrong now.**
  - DirectTrace sweeps every light in the pixel's cluster cell, so its cost grows with overlapping lights (22 per pixel in the garage, up to 190 on the bridge; direct-01).
  - K independent reservoirs cost K random draws per candidate and pick with replacement (direct-16).
  - The score has no visibility term, so lamps behind walls receive picks and rays.
  - Picks use white-noise hashes, frozen without TAA's jitter (RT2-7 fixes the freeze).
  - The extra lamps for young pixels use a fixed 6-frame threshold and tile (`FrameGraphBuilder.cpp:1845`, audit 2).
  - The sea's neighbour merge (`direct_trace.rvshader:990-1020`) combines reservoirs without MIS, which is biased where neighbours' candidates differ (A-N5).
- **What changes.**
  - (a) A tile-reduce pass writes, per 8x8 tile, a sorted list of the lights whose shadow rays came back unoccluded this frame (its length a global setting).
  - (b) The next frame, each pixel reprojects, picks among the 4 nearest tiles' lists by a stochastic bilinear draw, evaluates every listed light exactly, and draws hidden candidates from RT2-12's light BVH. The hidden candidates' weight is clamped to a defensive share of the total (a global setting, 20% by default after MegaLights); where reprojection fails (no guide list for the pixel), the hidden share is 100%, so every pick comes from the light BVH; the final pdf carries the clamp, so the estimate stays unbiased.
  - (c) One sweep, and K stratified picks from one blue-noise value (dithered sampling), with the linear score equal to the shaded term (RT2-10). MegaLights' log-luminance weighting is not adopted: it depends on absolute brightness (rule 2).
  - (d) One ray per distinct light; lights whose penumbra falls below the pixel's footprint share one ray.
  - (e) Directional lights keep a dedicated ray per pixel, now soft (RT2-10).
  - (f) Extra picks for young pixels come from the history's shortfall against its own memory, replacing RT-9's fixed 6-frame DirectBudget rule.
  - **The baseline arm** (rule 1): MegaLights' own hidden candidates, every N-th light of the froxel cell, against the BVH's.
  - **A later arm: spatial-only pooling** of light samples between neighbouring pixels, with pairwise MIS (RT-24's lamp pool generalised), with no temporal reuse. It needs each pixel's exact pick pdf (the candidate total, the clamped hidden share included) stored for its neighbours, and it replaces the sea's biased merge. Adopted only if it wins at equal time. Its evidence so far is the reflections' aimed lamps in the garage only.
  - **The owner's 2x2 tier (D14)**, built only if the full-resolution path misses its cost bar (the direct-light line of section 6 at native): the block's picks and shadow rays are shared by its 4 pixels, the pixel that fires them cycles once per frame, and the direct accumulator combines the 4 frames. It becomes the default if the owner judges it good enough.
- **Owner sees.** A direct light whose cost stays flat as lights are added, and less noise in rooms whose neighbours' lights are behind walls.
- **Touches.** `direct_trace.rvshader` (944-1300), a new tile-reduce pass, `FrameGraphBuilder.cpp` (1795-1856), `Renderer3D.cpp` (the direct signal's parameters).
- **Scope.** Rewrite of the primary sampling. **Depends on.** RT2-7 (blue noise, per-signal frame counters), RT2-10 (the score equal to the term), RT2-12 (the light BVH). **Risk.** Medium: the guiding lags one frame behind a light that turns on.
- **Exit gate.**
  - The many-light scene with rooms whose ranges cross walls: at equal time, the converged-diff grain after the same number of frames lower than the unguided arm's.
  - A teleport (cold start): back within `parked_stats.py`'s noise floor within the direct history's own memory.
  - The owner's swing with the direct signal isolated (`--capture-signals`): speckle over 32 and 64 levels at or below the direct signal's own reference, captured the same way at the same swing from the every-light arm (K=0) with 16 area samples per light (the 16-ray reference's method applied to this signal; the 16-ray reference's 494 is a whole-frame number of the reflection chain, not this signal's bar).
  - DirectTrace at or below 2.0 ms at native in the garage (section 6's proposal) and flat from 1k to 10k lights.
  - The converged diff against `--reference-render` within grain, and against the every-light arm (sampling error only) within grain; frames 200 and 201 differ under `--aa=none`.
  - The 2x2 tier, if built: a still camera within the parked noise floor of the full per-pixel result after 4 frames, and the owner's live judgement of the moving picture.
- **Size.** 4-5 sessions (plus 1-2 if the 2x2 tier is needed).
- **Closes.** direct-01, direct-16; the primary-pixel half of ISS-18; the comparison's finding A-N5 (section 8).

### RT2-13 · Transparent surfaces on the shared light code; the old in-shader light loop deleted

- **In plain words.** The lit shader used in ray-traced mode still compiles the old per-light loop, its sampler and its in-draw mirror rays, all switched off by settings, which costs registers. This step removes them from the opaque ray-traced shader. The second half, lighting the see-through surfaces a new way, moved to RT2-31, because it needs the translucency lighting cache (RT2-48) first.

> **Split by the comparison (2026-09-25).** 13a (this item) stays in M2: the opaque clean-up. 13b moves into
> RT2-31's back-layer step (M5): the panes behind the nearest and the particles are lit from the new
> translucency cache (RT2-48) instead of a per-pixel sampler, and only then is the loop deleted from the
> transparent variants. The see-through surfaces need the loop until a replacement light source
> exists, and that arrives only with RT2-25 and RT2-48.

- **What is wrong now.**
  - Every see-through surface except the nearest glass pane is lit by the old loop inside the lit shader. Outside the baked field it uses the S4 sampler, whose random draws move only with TAA and nothing accumulates. Inside the field it traces one soft-shadow ray per casting lamp. Its WR-17 thinning relies on a list order that the brightness sort broke. (Moved to RT2-31, step 13b.)
  - The opaque RT variant still compiles the loop, the S4 arrays (declared outside any define) and the mirror rays traced inside the draw, all switched off by uniforms.
- **What changes.**
  - **13a.** The loop, the S4 arrays, WR-17 and the in-draw mirror are compiled out of the RT-mode opaque variant. The raster recipe keeps the loop. Cell lists are ordered by a documented key.
  - **13b (moved to RT2-31).** See-through surfaces are lit from the translucency cache (RT2-48), which draws from RT2-12's light BVH; the panes behind the nearest get one fixed mirror ray each (D28); then S4, WR-17, the light loop and the in-draw lobe rays are deleted from the transparent variants.
- **Owner sees.** Nothing on screen. Possibly faster opaque shading, from fewer registers (measured).
- **Touches.** `pbr_fragment.glsl` (1329-1384, 4597-4978, 5425-5454, 6093-6116), `LightGrid.cpp` (194-215), `Renderer3D.cpp` (variant defines).
- **Scope.** Refactor and deletion. **Depends on.** RT2-12. **Risk.** Low.
- **Exit gate.**
  - Opaque pixels: diff 0.
  - Register counts per pipeline (read through `VK_KHR_pipeline_executable_properties`) recorded before and after.
  - A,B,B,A.
  - (13b's gates, the garage close-up's glass judged by the owner, are RT2-31's.)
- **Size.** 1-2 sessions.
- **Closes.** The opaque half of MAT-09 and ISS-41's part; direct-s4 and direct-15 close with 13b in RT2-31.

### RT2-14 · The compute question, answered first

- **In plain words.** Every shipped ray-tracing engine runs its ray passes as compute passes; RageV runs them as full-screen pixel-shader passes. Before rebuilding anything, three measurements say what compute would buy here: DirectTrace ported to compute, a blur ported to compute with shared memory, and the reflection trace split into "finding the hit" and "lighting the hit" to see which one costs the time. Nothing on screen changes.

> **Changed by the comparison (2026-09-25).** Three measurements instead of one, and the item no longer
> gates the render graph: RT2-15 gains storage and compute resources regardless, because every shipped
> RT engine traces in compute with indirect dispatch (Lumen, Snowdrop, id Tech). What stays open is the
> per-ray cost of the port, where compute's claimed win lives (neighbour gathers with shared memory), and
> where ReflectionTrace's time goes: it costs about 5 times a shadow ray per ray (3.82 ms for 1.39 M rays
> against DirectTrace's 3.18 ms for 5.85 M), which suggests hit shading dominates (inferred). The answers
> set RT2-20's port list and decide whether deferred hit shading is tried.

- **What is wrong now.** Every ray pass is a full-screen fragment pass, and whether compute passes would be faster here has never been measured. No published fragment-against-compute measurement exists either. The gain could be zero: RT-14 found the traced passes 90-99% bound by pixel count, and a fragment pass can also skip idle tiles by drawing one quad per tile. The forward lit pass also traces shadow rays from raster fragments (the subtractive shadow, `pbr_fragment.glsl:5413-5420`; E-N6).
- **What changes.** Three measurements, each output pixel-identical, timed A,B,B,A:
  1. **The trace port.** DirectTrace as a compute pass in 8x8 tiles against today's fragment version. The compute version owns its output outside the graph, as voxel GI and the water foam already do, so nothing else has to change first. Texture reads during its shadow rays (cutouts) use an explicit level of detail, because compute has no screen derivatives.
  2. **The gather port.** ReflectionBlur as a compute pass with a shared-memory tile and thread-group swizzling (NVIDIA measured +47% on Battlefield V's denoiser from swizzling alone).
  3. **The split.** A visibility-only ReflectionTrace arm whose hit shading is replaced by writing a hit record, to split traversal from shading.
- **Owner sees.** Nothing; three numbers that decide how RT2-20 is built.
- **Touches.** `direct_trace.rvshader` (a compute variant), `reflection_blur.rvshader` (a compute variant), `reflection_trace.rvshader` (the visibility-only arm), `ray_shadow_trace.glsl` (explicit level of detail), `Renderer3D.cpp`, `FrameGraphBuilder.cpp`.
- **Scope.** Measurement. **Depends on.** RT2-1 and RT2-2a. **Risk.** Low.
- **Exit gate.** Output diff 0 through the harness for the two ports, and A,B,B,A timings at 2560x1600 on the garage and at Headland. The split's traversal and shading times recorded. The results set RT2-20's port list (a port is kept only if it is no slower beyond the spread) and decide whether RT2-20c tries deferred hit shading (only if shading is above half of ReflectionTrace). RT2-15 no longer waits for them.
- **Size.** 2 sessions.
- **Closes.** The A/B half of GPU-02; the comparison's finding E-N6 (recorded here; the lit pass's own traces leave with RT2-12's loss candidates and RT2-22's composition).

### RT2-15 · A render graph that schedules

- **In plain words.** The frame is a hand-ordered list of passes: it works out no memory barriers, shares no memory between short-lived targets, and forbids compute passes from writing its images. This item grows the existing render graph, one capability at a time, into one that declares what each pass reads and writes, works out the barriers, shares memory, skips idle passes, and holds the ray-tracing structure builds as passes of their own. The picture does not change.

> **Changed by the comparison (2026-09-25).** The existing RenderGraph is evolved in place, each capability
> behind its own flag, instead of a parallel "v2" executor: it reaches the same end state with less code,
> and keeps the owner's flag-then-delete protocol. The TLAS, BLAS and skinning builds become graph passes
> here (moved from RT2-17's step 17d), because barriers cannot be derived while those builds sit outside
> the graph. Barriers are batched; split barriers only where a pass-gap measurement shows a gain. Each
> pass carries a queue field, unused until RT2-20c's async arm.

- **What is wrong now.**
  - The render graph is an ordered list with pooled targets. It works out no barriers: the RHI issues one unbatched barrier per texture at every render-pass edge.
  - It shares no memory between short-lived targets.
  - Passes with no work still run: fifteen measured-change passes clear their targets and draw nothing whenever nothing changed. Dropping passes whose results nothing reads would not remove them, because their change maps are read.
  - It uses one queue, and it forbids compute and standalone passes from writing its images, while voxel GI and the water foam own storage images outside the graph. The TLAS is not a graph resource, and its build is recorded before the graph.
  - Declaring the frame swaps histories, stamps camera records and sets renderer globals before the frame is even checked.
  - The graph is rebuilt every frame, with heavy allocation. The frame's builder has 84 `AddPass` call sites, and 61 to 72 passes ran in the benchmark.
- **What changes.** Five named steps, each capability behind its own measurement flag, each ending in a report:
  - **15a.** Typed resources: attachments, storage images, buffers, indirect arguments and the TLAS, each with a resolution class (output, internal, half, quarter), which is cheaper to add now than to retrofit in RT2-29 (render scale stays an optional setting under D8's native target). Every pass declares its reads and writes, with the pipeline stage and the kind of access. Declaration has no side effects: swaps, invalidations and camera records are committed after a successful execute. The TLAS, BLAS and skinning builds become graph passes.
  - **15b.** Compiling the graph works out one batched barrier set per pass boundary. Split barriers only where a measurement of the gaps between passes shows a gain. Proven with sync validation.
  - **15c.** Short-lived memory aliased by lifetime, and passes whose results nothing reads dropped.
  - **15d.** A pass can report that it has no work this frame, and its readers then get a constant cleared resource: the mechanism that actually removes the idle measured-change passes. The compiled graph is cached per frame shape; only resources are rebound each frame. Each pass carries a queue field (graphics by default).
  - **15e.** The old implicit transitions and the per-capability flags are deleted.
- **Owner sees.** Nothing: the picture is identical. Less VRAM for short-lived targets (measured). Possibly some time back from batched barriers and skipped idle passes (measured, not promised).
- **Touches.** `RenderGraph.h` and `RenderGraph.cpp` (evolved in place), `FrameGraphBuilder.cpp` (declarations), `VulkanCommandList.cpp` (render-pass transitions), `VulkanResources.cpp` (`TransitionTo`; the acceleration-structure barriers at 1342-1353), `RayShadows.cpp` (the build becomes a pass), `TemporalHistory.cpp`.
- **Scope.** Rewrite. **Depends on.** RT2-1 to RT2-4 (D1 and D8 answered). No longer on RT2-14. **Risk.** High: a long pole with nothing visible to show for it until it is done.
- **Exit gate.**
  - Diff 0 through the harness, over 20 parked frames and a 60-frame swing, after every step.
  - Per-pass GPU timings in an A,B,B,A run, with the gaps between passes measured before and after: no regression beyond the spread.
  - VRAM of short-lived targets, before and after (RT2-1's report).
  - Validation clean with sync validation on, on the garage, the bridge and the camp.
  - The CPU time of building and compiling the graph per frame, before and after.
  - scenetest green on Vulkan, and on OpenGL's frozen raster path.
- **Size.** 8-10 sessions.
- **Closes.** frame-01, frame-09, frame-17, GPU-07; the pass-culling part of frame-11 and the aliasing parts of GPU-09 and frame-05.

### RT2-16 · Views own their histories

- **In plain words.** Each application (the runtime, and each editor view) keeps 16 histories of its own and hands them to the renderer through 20 named fields, filled in line by line in three near-copies; histories are never released. This item gives each view one object that owns its histories and its frame counter, with generic rules for when to swap, drop or release them. The picture does not change; memory comes back when a panel closes.

> **Part done early, in RT-24 (2026-09-24):** a camera cut is reported, never guessed. The speed and
> 45-degree rules are gone; `FrameDesc::CameraCut` comes from the scene (its camera changed, it was
> loaded, or `Scene::RequestCameraCut` / C# `Graphics.CameraCut()`), and from the editor camera's own
> jumps. The hand-kept list of histories to drop is unchanged and still this item's.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** The frame counter
> every traced pass seeds from advances once per scene draw, not per view. The camp's realtime
> reflection probe draws the scene again several times a second, so the main view skips values (live
> on the camp); a second viewport, or a probe updating every frame, leaves each view only every other
> value, so the water's quad lanes visit 2 of 4 and the reflection sequence keeps one parity. General
> form: one frame index per view, kept with the view's own history state.

> **Checked by the comparison (2026-09-25): kept unchanged.** Per-view state is standard (Unreal's view
> state), and the memory budget's release rule (RT2-19) needs it.

- **What is wrong now.**
  - The applications own 16 temporal histories and 4 measured-change records per view (32 and 8 in the editor), and pass them in through 20 named FrameDesc fields. The runtime and each editor view fill those fields line by line, in three nearly identical blocks.
  - Histories are invalidated but never released: `Release` has no caller, and the target pool only grows.
  - The camera-cut list is maintained by hand and misses some histories.
  - The editor's game view repeats the shadow walk and the cull for its camera.
- **What changes.**
  - A View object owns one view's persistent resources: histories, exposure, ray budgets, change records and its own frame index, registered with the graph.
  - Generic rules: swap after a successful frame; drop on a camera cut, a resize or a feature being switched off; release when unused.
  - Applications create a view with one call. Work shared per frame is done once, and every view reads it.
- **Owner sees.** Nothing: the picture is identical. VRAM freed when a feature is switched off or a panel closes.
- **Touches.** `FrameGraphBuilder.h` (FrameDesc), `RuntimeLayer.h` and `RuntimeLayer.cpp`, `EditorLayer.h` and `EditorLayer.cpp`, `TemporalHistory.h` and `TemporalHistory.cpp`, `Renderer3D.cpp` (3944, the frame counter), a new ViewRenderer.
- **Scope.** Refactor. **Depends on.** RT2-15. **Risk.** Low.
- **Exit gate.**
  - Diff 0 in the runtime and in the editor with both views open.
  - A teleport arm under each AA mode drops every history.
  - VRAM falls by the view's history total when the game panel closes.
  - Editor CPU zones show no second shadow walk.
- **Size.** 3-4 sessions.
- **Closes.** frame-15; the release part of frame-05; the history half of frame-02; the generic half of framegraph-s2.

### RT2-17 · Signals and layers as data; Renderer3D split; RT and raster recipes

- **In plain words.** Each lighting signal is wired by hand inside one huge function, so adding one touches a dozen files and forgetting a line is a silent bug. This item describes signals and layers as tables and builds their passes from the tables, splits the static renderer into parts, and makes ray tracing independent of shadows. Only AO moves onto the tables now; every other signal moves in its own rewrite later, so no signal is migrated twice. The picture does not change.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** The measured-change
> filter's plane test is in metres and its floor is an amount per frame (so it follows the frame
> rate); the joint upsample's depth tolerance; the guide's texel choice through a coordinate that
> lands between texels. General form: plane tests in pixel units from the view depth and pixel angle,
> the floor judged on the change the history carries, one integer rule for a texel's representative.

> **Changed by the comparison (2026-09-25).** Step 17b migrates only AO into the signal table, plus the
> dummy-signal test. Reflections, direct light, GI and the glass lane land on the table in their own
> rewrites (RT2-23, RT2-24, RT2-25, RT2-31), each deleting its hand-wired version there; migrating them by
> hand now, only to replace them in M4, would be double work with no gain, and each M4 item's diff-0 gate
> is against the old path either way. The TLAS graph pass moved to RT2-15. Size 10-12 becomes 6-8.

- **What is wrong now.**
  - Each signal is written out by hand inside `BuildFrame`. The glass reflection lane is a near copy of the opaque one, and the measured-change block exists four times.
  - Renderer3D is all static: about 37 shader and pipeline pairs, and 45 named descriptor sets plus 18 buffers for each view slot. Frame inputs reach draws through static setters, and switches are packed as bits into a float of the scene uniform.
  - Ray tracing is treated as a side feature of shadows. With shadows off, rays are off (`FrameGraphBuilder.cpp:210-218`; `Scene.cpp:2540-2541`). The RT mode is resolved, and the lit shaders recompiled, inside `Scene::RenderShadows`, which also builds the TLAS outside the graph.
  - The G-buffer's shape is declared in three places, and they disagree.
  - About 40 of EngineConfig's roughly 150 fields are render switches, read deep inside passes.
- **What changes.** In stages, one at a time. Each stage sits behind a flag, is diff 0 through the harness, and ends in a report.
  - **17a. The tables.** A descriptor per signal: its inputs and layer, grid divisor, history lanes and formats, kind of reconstruction, consumers and debug views. A descriptor per layer: the opaque G-buffer and the transmissive layer. One generic builder emits the passes, histories, camera-cut handling, budgets and change maps from those tables.
  - **17b. AO migrates**, as the first and only signal moved here, with the hand-written version as its reference, which is then deleted. A dummy signal added in a test build with one descriptor and nothing else proves the tables. Direct light, reflections, GI and the glass lane migrate in RT2-24, RT2-23, RT2-25 and RT2-31.
  - **17c. Renderer3D split** into four parts: scene submission; a library of pipelines and variants; one module per signal; an explicit per-view render context passed to every pass. No statics remain.
  - **17d. Recipes and settings.** An RT recipe (the default wherever rays are available) and a raster recipe, as separate builders. Ray tracing becomes independent of shadows (the TLAS graph pass is RT2-15's). One step resolves project settings and flags into a per-view settings struct, and the renderer reads only that. Measurement arms are registered in a list that shipping builds compile out. Structures shared between C++ and GLSL get one definition that both sides include.
- **Owner sees.** Nothing: the picture is identical. Adding a signal becomes one descriptor plus its shaders. A scene with shadows off and reflections on still traces its reflections.
- **Touches.** `FrameGraphBuilder.cpp` and `FrameGraphBuilder.h`, `Renderer3D.cpp` and `Renderer3D.h`, `Renderer.cpp`, `Scene.cpp` (the rendering half), `RenderSettings.h`, `EngineConfig`, `scene_block.glsl`, `pbr_fragment.glsl` (the uniform mirrors).
- **Scope.** Rewrite. **Depends on.** RT2-15, RT2-16. **Risk.** High, which is why it is staged.
- **Exit gate.**
  - Diff 0 across all scenes and AA modes after every stage, with the flag on and off.
  - scenetest green.
  - A dummy signal added in a test build with one descriptor and nothing else.
  - A scene with shadows off and reflections on: the reflections are still traced.
- **Size.** 6-8 sessions.
- **Closes.** frame-02, frame-07, frame-08, frame-13, frame-16, CORE-06 (the render-world half), CORE-15, rhi-s2 (the single-definition half), the structural half of framegraph-s3, the ownership half of direct-13; the comparison's claim E-C7 (section 8).

### RT2-18 · RHI core: bindless, a per-frame ring, the missing commands, uploads off the frame path

- **In plain words.** How shaders find their textures and buffers is today rewritten per pass every frame, which has caused at least five bugs, and every upload stops the CPU until the GPU is idle. This item makes every resource reachable from one table and a per-frame constant ring, adds the missing GPU commands (copies, indirect dispatch), and moves uploads to their own queue. The picture does not change; loads get faster and the first ray-traced frame stops hitching.

> **Changed by the comparison (2026-09-25).** The binding model is named: descriptor indexing extended to
> storage images, samplers and TLAS slots, at the device's real limit; every buffer reached by device
> address; one push-constant block per draw or dispatch pointing into a per-frame constant ring. The
> descriptor-heap and descriptor-buffer extensions are out of scope (section 7): descriptor indexing plus
> device addresses is portable, shipped and already half-used, and the heap extension's driver status on
> this laptop is unverified. RT2-35's BLAS manager uses the new ring and pool for its scratch and
> compaction copies.

- **What is wrong now.**
  - Pass inputs go through descriptor sets that are rewritten every frame and must not be rewritten after they are bound. That rule has caused at least five recorded bugs (one produced 576 validation reports), and in Release a violation is silent undefined behaviour.
  - Only sampled textures are bindless, with 4,096 texture-and-sampler pairs.
  - Every upload, texture creation, texture mip and static BLAS build is a separate blocking submit on the graphics queue.
  - There is no copy from a buffer to a buffer or a texture, no indirect dispatch, and no draw count decided by the GPU.
  - The transfer queue is found at start-up and never created, and there are no timeline semaphores.
  - RHI objects hold layout state at record time, which blocks recording on several threads. Some GPU work never enters the graph and relies on that tracking: probe capture, ImGui, the loading screen and the bakes.
- **What changes.** Five named steps, so the two binding models coexist while passes migrate. Each ends in a report.
  - **18a. The missing commands and the upload ring** (no graph needed). New commands: copy buffer, copy buffer to texture, update buffer, clear texture, indirect dispatch, indexed indirect draw with a GPU count, and mesh-task indirect draws. A persistent mapped upload ring per frame in flight; blocking submits remain only for tools and bakes.
  - **18b. The transfer queue and timeline semaphores.** Each frame's uploads are batched on the transfer queue and handed over with a timeline semaphore.
  - **18c. The bindless tables and the constant ring**, migrated pass by pass. Descriptor indexing extended to sampled images, storage images, samplers and TLAS slots, at the device's real limit; buffers reached by device address; resources as 32-bit handles, retired by TextureHeap's frame-slot scheme. Each draw or dispatch gets one push-constant block that points into a per-frame constant ring, so no descriptor is written per pass. Old-style sets and new-style handles work side by side until the last pass has moved.
  - **18d. The work outside the graph moves onto explicit barriers**: probe capture, ImGui, the loading screen and the bakes.
  - **18e.** Only then does the RHI stop tracking layouts at record time; the graph owns them.
- **Owner sees.** Nothing: the picture is identical. Shorter load times and no hitch on the first ray-traced frame (measured).
- **Touches.** `VulkanDevice.cpp`, `VulkanPipeline.cpp`, `VulkanCommandList.cpp`, `VulkanResources.cpp`, `TextureHeap.cpp`, `RHICommandList.h`, `RHIDevice.h`, `RHITypes.h`, every pass that commits a descriptor set, the probe capture, `VulkanImGui.cpp`, the loading screen, the bake passes.
- **Scope.** Rewrite. **Depends on.** RT2-15 (which moves the TLAS build into the graph) and RT2-17. **Risk.** High.
- **Exit gate.**
  - Diff 0 through the harness after every step.
  - Cold and warm load times, and the worst frame while 200 new meshes spawn, before and after.
  - Blocking submits per frame (the counter): 0 in steady state and at spawn.
  - CPU time of descriptor work, before and after.
  - Validation clean with sync validation on, including a probe capture, a bake and the loading screen.
- **Size.** 11-13 sessions.
- **Closes.** GPU-01, GPU-04, GPU-10, CORE-03, frame-12, GPU-12 and MAT-14 (OpenGL frozen, D1), the queue and timeline part of GPU-05, the record-time state part of GPU-06.

### RT2-19 · A memory budget and lane budgets

- **In plain words.** The engine never asks the driver how much memory it may use, a failed allocation carries on with a null handle, and the histories alone would take about 1.8 GB at native resolution. This item makes every allocation respect the driver's budget and degrade (fewer mips, lower internal resolution) instead of crashing, deletes the lanes nothing needs, and writes down how many bytes each rebuilt signal may use. Memory use falls; the picture does not change.

> **Seen on the Intel iGPU (RT2-9, 2026-10-13).** The bridge at 1280x720 on Intel: `vmaCreateImage` for the
> irradiance volume's atlas (`IrradianceVolume::CreateAtlas`) returned VK_ERROR_OUT_OF_HOST_MEMORY, the
> engine logged it and carried on, and crashed on the null image a moment later. This item's degrade-don't-
> crash rule, on a real device: the atlas must fit what the device can allocate, or degrade. Gate: the
> bridge loads on the Intel iGPU.

> **Changed by the comparison (2026-09-25).** Keep the budget, the degrade-don't-crash rule, the forced
> low-budget arm and the release rules. Today's lanes are not repacked: M4 rewrites every history, so
> repacking the old ones would be thrown away. Instead the known-dead lanes are deleted, and the per-signal
> lane budgets become a contract the M4 signals must meet. One storage arm is added: lighting histories
> in `R11G11B10F` with the existing stochastic rounding, in pre-exposed space (the layout MegaLights ships,
> checked against a 64-bit reference), adopted only by diff image; id Tech 8 avoids that format for colour
> shift, so it is an arm, not a default.

- **What is wrong now.**
  - A failed allocation in Release carries on with a null handle, and nothing reacts to the budget RT2-1 now reports.
  - Histories have grown to about 440 bytes a pixel, 3.4 times RT-14's inventory. They include:
    - full-frame glass histories (168 bytes a pixel for the pair of direct and reflection histories), allocated even in scenes with no glass (G-N4), although glass covers an unmeasured, probably small, share of the frame (the 0.06-0.82% often quoted is the share of pixels that changed when the glass layer was switched on, not its coverage);
    - a moving-layer lane written every frame although the feature is off;
    - a 32-byte copy of the G-buffer, made only because the G-buffer does not survive the frame (the TAA guide);
    - a history for a retired water pass.
  - The format list has no packed formats.
- **What changes.**
  - VMA's allocation priorities, and the budget RT2-1 measures, drive decisions: a failed allocation, or one that would pass the budget, degrades quality (fewer mips, lower internal resolution) instead of crashing. Null handles in Release become loud failures.
  - Glass coverage is measured on every test camera (the garage close-up and the bridge included), so the layer histories' cost is known; RT2-31 reopens sparse histories only if every camera's layer coverage is below 25%.
  - The known-dead lanes are deleted: the moving-layer lane written while its feature is off, the retired water history, and the TAA-guide copy once last frame's G-buffer is kept (RT-14's own proposal).
  - A lane budget per signal, written as a contract the M4 rewrites must meet: exact integer lanes instead of values packed into floats; nothing allocated for switched-off arms; packed formats added to the RHI and adopted only by diff image.
  - The `R11G11B10F` pre-exposed storage arm for non-negative lighting histories.
- **Owner sees.** Less memory used. A forced-low-memory test that degrades instead of crashing.
- **Touches.** `VulkanDevice.cpp`, `VulkanCommon.cpp`, `RHITypes.h`, `FrameGraphBuilder.cpp` (history formats), `TemporalHistory.cpp`, the accumulate shaders. VMA's types stay inside the Vulkan backend's private files.
- **Scope.** Refactor. **Depends on.** RT2-1 (the measurement), RT2-15, RT2-16. **Risk.** Low.
- **Exit gate.**
  - VRAM by category reported for the garage, the bridge and the camp, at 1600x900 and at 2560x1600, before and after.
  - A table of history bytes per pixel, by RT-14's method, before and after.
  - Diff 0 where a lane is deleted or a format change is exact; within grain, judged by the owner, where packing or `R11G11B10F` is adopted.
  - An arm with a forced low budget degrades without crashing.
- **Size.** 2-3 sessions.
- **Closes.** GPU-09, CORE-05 (the budget half), frame-05, reflections-s3, ISS-33.

### RT2-20 · Ray passes in compute; ray lists; queue and pipeline experiments

- **In plain words.** Ray passes move to compute where they survive the M4 rewrites, and one general mechanism replaces the fixed tile budgets: each signal says how many rays each texel needs, the rays are packed into a dense list, and the list is traced with a dispatch the GPU sizes itself. Textures read at ray hits get the right mip level. Then a few speed experiments (async compute, deferred hit shading, reordering, half-precision maths), each adopted only on a measured gain.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** The tile budgets:
> importance smoothed over a fixed number of frames, motion saturating at 4 pixels a frame, fixed
> material weights, dwell frames, and the AO budget read without reprojection (a moving object gets
> another tile's rays). General form: averages over exactly one jitter period (the owner's
> TemporalJitterPhase), the young share from the accumulators' own counts, reprojected reads, weights
> from each estimator's contribution.

> **Changed by the comparison (2026-09-25).** Split into three steps. 20a: the hit-shading library usable
> from compute, with ray cones, and compute ports of only the passes that survive M4 (the DirectTrace
> trace core and the AO trace); the ports of `reflection_accumulate` and `reflection_blur` are dropped,
> because RT2-23 writes them in compute directly. 20b: ray lists (a per-texel demand of 0 to N rays,
> order-preserving compaction, indirect dispatch) and GPU tile classification, delivered as the mechanism
> the M4 signals are written on. 20c: the experiments, each with a measurable entry condition. The
> verification corrected the case for ray lists: RT-9's per-texel allocation was measured as giving no
> picture gain (blobs 11.0% against 10.6% for the motion rule), and about 0.55 ms of its 1.33 ms was the
> rays themselves, so compaction can recover at most the divergence share (about 0.78 ms). The gates
> therefore demand a picture gain from per-texel demand before any cost comparison.

- **What is wrong now.**
  - Every ray pass is a full-screen fragment pass. That means no shared memory for neighbour gathers and blurs, no dispatch the GPU sizes to cover only the texels with work, and no async compute.
  - The hit-shading include samples textures with implicit level of detail, which needs screen derivatives, so it cannot run in compute.
  - Every texture read at a ray hit is at mip 0, because nothing tracks the ray's footprint.
  - Rays are allocated per 16x16 tile by budgets with fixed weights, dwell frames and dead bands, and only ever added; the AO budget is read without reprojection.
  - There is only one graphics queue.
  - Only ray queries are used: no ray-tracing pipelines, no SER.
- **What changes.**
  - **20a. The hit library and the surviving ports.** A hit-shading library usable from compute, with explicit level of detail from ray cones (RT2-21d uses it for the full material). Compute ports of the DirectTrace trace core and the AO trace, kept only where RT2-14 showed them no slower; thread-group swizzling for every pass that gathers neighbours.
  - **20b. Ray lists.** Each signal's classification writes a ray count per texel, 0 to N, from that signal's own rule (the accumulator's history length, and later its variance), never from content weights or metres. An order-preserving compaction (a prefix sum inside large Z-order groups, atomics only across groups, as Lumen measured 21% faster than local atomics) writes a dense list and the indirect arguments; the trace dispatches indirectly over the list. Texels given 0 rays reuse their history or their neighbours, as the signal's own reconstruction decides. GPU tile classification lives here too, and every M4 signal (RT2-23, RT2-24, RT2-25, RT2-27) is written on this mechanism.
  - **20c. Experiments, in order, each adopted only on a gain beyond the spread:**
    - async compute: the acceleration-structure builds overlapping the G-buffer, then one accumulator overlapping the transparent pass;
    - deferred hit shading (a 128-bit hit record, bucketed by material class, one indirect dispatch per bucket), tried only if RT2-14's split shows shading above half of ReflectionTrace; id Tech 8's per-shader dispatch is for about 20k radiance-cache entries, so the per-pixel case is unproven;
    - SER: first step, query this laptop's driver for shader execution reordering on Vulkan, with inline ray queries and with a ray-generation pipeline, and record the result (a gap the research could not close: it is not verified whether SER reaches inline ray queries); the arm runs only in the form the driver exposes, and only if Nsight shows fewer than half the threads active per warp in the hit shading that remains. If SER is exposed only to ray-generation shaders, the arm moves that hit shading into a ray-generation pipeline;
    - half-precision (fp16) arithmetic in the accumulators, watching the known half-float rounding trap (`include/half_float.glsl`).
  - The forward lit pass's own shadow traces are not ported: they leave with RT2-12's loss candidates and RT2-22's composition.
- **Owner sees.** Frame time back where the ports win. Less sparkle on distant textured ray hits, from the ray-cone mips; this is a change in look, and the owner judges it.
- **Touches.** `pbr_fragment.glsl` (hit shading), `direct_trace.rvshader`, `rtgi_trace.rvshader`, `rtao_compute.rvshader`, `tile_budget.rvshader`, `reflection_budget.rvshader`, `importance_tiles.rvshader`, new compaction and classification passes, `FrameGraphBuilder.cpp`, `VulkanDevice.cpp` (queues and extensions).
- **Scope.** Refactor. **Depends on.** RT2-14, RT2-15, RT2-18. **Risk.** Medium: the gains are unmeasured and could be zero; RT-14 found the traced passes 90-99% bound by pixel count.
- **Exit gate.**
  - Each port pixel-identical (diff 0), except the ray-cone mip arm, which is diffed against the reference renderer, and the fp16 arm, which is judged by diff image with the running averages checked for drift.
  - Ray lists, in this order: first a picture gain from per-texel demand at the owner's swing (`speckle_count.py` and `spin_measure.py` no worse than the 16-ray reference, today 513/85 against 494; the pole edge-jitter no worse than 9.51; the parked garage within the parked noise floor with fewer rays per frame); then cost at equal rays per texel, compute plus list against today's fragment trace, diff 0 and no slower beyond the spread, at 2560x1600 in the garage and at Headland.
  - A,B,B,A per pass; adoption only on a gain beyond the spread; the async arm measured on mains power with sync validation clean.
- **Size.** 7-10 sessions (20a 2-3, 20b 3-4, 20c 2-3).
- **Closes.** GPU-02 (the ports), GPU-11 (the experiments, fp16 included), MAT-05, frame-11, the async experiment of GPU-05; the comparison's claims E-C4 and E-C8 and finding E-N7 (section 8).

### RT2-21 · The G-buffer as a designed contract

- **In plain words.** The G-buffer (the per-pixel record of the visible surface) was never designed: values share bits inside floats, there is no geometric normal and no material id, and ray hits see a poorer version of each material than the camera does (no normal maps, the sharpest mip, one terrain layer). This item designs the lanes once, including the tangent direction brushed metal needs (D27), filters roughness by what one pixel covers so the smoothest surfaces stay stable without a fixed floor, gives ray hits the full material, and starts rays from positions that stay precise anywhere in the world. The one BSDF moved to RT2-21b.

> **Added from RT-24's hard-coded-values audit (2026-09-24; section 10 lists every value).** Every surface is
> forced to at least roughness 0.045 (`pbr_fragment.glsl`, "fully smooth aliases badly"), so no puddle or
> mirror can be smoother. General form: no floor on the material; any aliasing guard comes from the
> pixel's own footprint (the variance a pixel actually covers), not a fixed number.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Ray origins are
> lifted by fixed metres (traced hits, shadow rays, AO rays; general form: by the origin's own float
> precision); parallax step counts fixed at 28/10 and 24/8 (general form: from the texels the ray
> crosses); the terrain's wall-projection blend tuned on one cliff; the terrain weight map without
> mips; ray hits reading cutout textures at a fixed mip (general form: ray cones).

> **Owner's ruling D27 (2026-09-25).** Brushed-metal (anisotropic) materials in RT mode now, without
> waiting for an asset: the tangent direction is carried in the G-buffer and through DirectTrace, ray
> hits and the composition. *(This rejects the comparison's recommendation to reserve the shading-model
> id and add the lane only when the first anisotropic asset lands.)*

> **Owner's ruling D26 (2026-09-25), as it touches this item.** In RT mode the frame is composed in compute;
> a raster drawing is added only when MSAA is selected (RT2-22). So the RT recipe's G-buffer is
> single-sampled in every AA mode, and the MSAA edge defects of today's lanes leave RT mode by
> construction.

> **Changed by the comparison (2026-09-25).** Split into five steps: 21a the lane contract, 21b the one BSDF
> (moved to M2 as RT2-21b, because RT2-10's fitted tables need it first), 21c the roughness pipeline, 21d
> the material at ray hits, 21e the ray origins. Corrections from the verification: the indirect lane
> the geometry lane replaces is dead only while ray-traced GI is on, because SSGI reads it otherwise, so
> the RT recipe drops it by making SSGI and voxel GI raster-recipe features (the RT recipe's bounce light
> is the bake plus the realtime layer, D4). The half-float normal is coarser than HDRP's 12-bit lanes over
> half its range (F-OB2), so the move to 16-bit UNORM is justified by precision arithmetic, not by the
> in-code chrome comment (which is about roughness packing). The roughness floor is also an aliasing
> guard (Filament documents both reasons), so only the `--aa=none` gates can justify removing it. The
> specular-AA constants come from Tokuyoshi and Kaplanyan's JCGT 2021 paper, not the 2019 one. Ray hits
> today are behind Battlefield V and Unreal's hit lighting (no normal maps, mip 0), so step 21d closes a
> gap rather than keeping a lead.

- **What is wrong now.**
  - The G-buffer's lanes were never designed as a contract:
    - albedo is linear 8-bit, which RT-2.2 names as its precondition;
    - there is no geometric normal, so shadow rays leave along the normal-mapped normal and mirror curvature comes from normal-map detail;
    - flags and two values share float bits (the Static flag in the sign, roughness and occlusion packed into one float);
    - there is no material or shading-model id;
    - "no surface" is marked by roughness 0, which only works because every surface is clamped to at least 0.045: 16 lines in 12 shaders and the include test it, and seven non-PBR shaders (sky, grid, particle, light glow, world UI, quad, debug) write 0 there in the Scene pass (F-C1);
    - there is no tangent, so no anisotropic shading reaches RT mode;
    - under MSAA every lane is averaged at edges (Vulkan allows the sample-zero resolve only for integer formats).
  - The traced reflection, the probe term and SSR read unfiltered roughness; only analytic lights see the geometric-specular-AA roughness (F-C2).
  - The texture cook renormalises normal-map mips (discarding the variance a shortened normal carries) and box-filters roughness maps in raw perceptual space, with no coupling between the two; a roughness map whose name misses the suffix list is cooked as colour (F-C6, F-N6).
  - Ray hits get no normal map and read mip 0 (GEO-10, MAT-05), terrain hits see layer 0 only, and the alpha test at hits reads mip 0.
  - The vertex has no tangent, and a hit has no screen derivatives.
  - Ray origins are lifted by a fixed 0.002 m x (1 + slope) along the shading normal (`pbr_fragment.glsl:2641-2643`, `ray_shadow_trace.glsl:237-239`), and AO by `kLift` and `kLiftPerMetre`.
- **What changes.** Four steps here (21b is RT2-21b in M2), each ending in a report.
  - **21a. The lane contract, described once as data** (after RT2-17). The RT recipe draws the G-buffer single-sampled, in these lanes:
    - depth, `D32F`;
    - motion, `RG16F` (kept, with RT2-6's format-step tests);
    - the depth part of the motion, `R32F`: the point's view depth in last frame's camera, written by the raster from the previous transform (the 2.5D motion RT2-28a's disocclusion test needs);
    - surface, `RGBA16_UNORM`: the shading normal (octahedral, 16:16), the footprint-filtered roughness (21c), metalness;
    - geometry, `RGBA16_UNORM`: the smooth vertex normal and the face normal (both octahedral 16:16; the face normal from the derivatives of the camera-relative position, exact inside a triangle);
    - tangent, `R16_UNORM`: the tangent's angle around the shading normal (D27);
    - albedo, `RGBA8_SRGB`: albedo, with the specular scalar in A;
    - identity, `R32G32_UINT` (landed early in RT2-5): x = the entity's full handle; y = the material index (16 bits), the shading model (4), material occlusion (8), the Static flag (1), 3 spare. Terrain chunks and model sections share their entity's id. "No surface" is id 0.
    - Emissive is written into the colour attachment by the G-buffer pass.
    - F0 is never stored: one function MaterialF0(albedo, specular, metallic) derives it everywhere (the "F0 and metalness" lane RT-SERIES-2 planned is dropped as redundant).
    - The indirect lane is dropped from the RT recipe; SSGI and voxel GI are raster-recipe features. Before the shared SSR, SSAO, SSGI and voxel-GI readers switch to the new lanes and the id sentinel, they fork from the frozen OpenGL shader set (D1).
    - Bytes per pixel: today 53 with the transparent pass (44 without); the contract is 59 (50 without): the geometry lane replaces the dead indirect lane, and the tangent (2 bytes) and the motion depth (4 bytes) are new. MSAA multiplies every lane; in RT mode the G-buffer stays single-sampled (D26). The raster recipe keeps its own lanes; under MSAA there, integer lanes resolve by sample zero and float lanes by a small shader that takes sample zero.
    - A format check on the device: `R16G16B16A16_UNORM` as a colour attachment with blending.
  - **21c. The roughness pipeline** (after RT2-5). The 0.045 floor is removed from the material; only numerical guards stay (the existing 1e-9 divisor floor, and a lobe too narrow to represent treated as a mirror in sampling, as the code already does).
    - The pixel's footprint: Tokuyoshi and Kaplanyan's filter (JCGT 2021; sigma^2 = 1/(2 pi), kappa = 0.18, the paper's pixel-filter constants, stated to the owner as such), taken from the derivatives of the smooth vertex normal, not the normal-mapped one. It is written once into the surface lane and read by every consumer: analytic lights, reflection rays, the probe and environment term, the composition.
    - Normal-map variance: at cook, each material's pair (normal map, roughness map or value) gets a derived roughness mip chain, filtered in alpha-squared space plus the Toksvig variance from the unnormalised mean normal's length at that mip. This is a new asset kind, keyed per material rather than per file; the case of a roughness map with an unrecognised name and the uncooked fallback's blit mips are handled.
    - At hits, the ray cone's mip reads the same derived chain.
  - **21d. The material at ray hits** (after RT2-10). Hits call the same SampleMaterial as the raster, at a footprint from a ray cone (Akenine-Moller 2021, isotropic). The cone starts from the pixel's angular footprint and the geometry lane's curvature and is widened by the lobe (one standard deviation, the paper's bound); the mip is log2 of the cone's width at the hit times the triangle's texel density. For textured hits only, the triangle's three positions are loaded; from them come its world area (for the texel density), its face normal (for offsets), and a per-triangle tangent frame equivalent to the raster's, so the normal map and the anisotropy direction (D27) apply with the raster's frame. The alpha test uses the cone's mip, and the cook builds coverage-preserving alpha mips (Castano 2010) so cutout edges do not thin. Terrain hits blend the four layers through the weight map at the cone's mip, once the weight map gets a mip chain (`Material.cpp:341`). Cone widths are written as mip feedback for RT2-40. A primary-ray parity test (traced primary rays through the hit path with analytic ray differentials, diffed against the G-buffer) keeps the one-material rule honest.
  - **21e. Ray origins** (after 21a). Every ray leaving a surface is offset along the face normal, on the ray's side, by Wachter and Binder's offset in units of the origin's own float precision (Ray Tracing Gems ch. 6). For points rebuilt from depth, the `D32F` reconstruction error at that depth is added (the RTAO texel-slack idea), because the Wachter-Binder offset covers only positions computed from the triangle. Hits use their triangle's face normal. Positions are rebuilt from depth relative to the camera. The same offset shortens RT2-46's aimed rays, which end just before their sample point on an emitter's triangle (M2 uses today's fixed lift there). This step is a hard prerequisite for RT2-41's removal of the metre offsets.
- **Owner sees.** Normal-mapped walls keep their detail in reflections. Chrome poles and puddles stay stable without a fixed roughness floor. Distant textured surfaces in reflections sparkle less. Terrain looks right in reflections. Brushed metal shades the same in RT mode as in raster. (Coat, sheen and anisotropy are latent today: none of the 458 sample materials uses them.)
- **Touches.** `FrameGraphBuilder.cpp` (lanes; 56-82, 578-594), `pbr_fragment.glsl` (`SampleSurface`, `TraceSurface`, `ShadeTraced`; 108, 131, 2639-2648, 2717-2791, 4203, 4445-4507, 5374), `view_reconstruction.glsl` (79-89), the 12 shaders that test the sentinel, `direct_trace.rvshader`, `reflection_trace.rvshader`, `rtgi_trace.rvshader`, `rtao_compute.rvshader` (137-138), `irradiance_fill.rvshader`, `ray_shadow_trace.glsl` (178-181, 237-239), `ssr_trace`, `ssao_compute`, `ssgi_compute`, `voxelgi_gather` (the forked Vulkan copies), `TextureCook.cpp` (297-356, 458-506), `Material.cpp` (341), `MeshCook`, `Mesh.h`.
- **Scope.** Refactor. **Depends on.** RT2-5 (the identity lane), RT2-17 (lanes as data; frame-16), RT2-20 (the hit library), RT2-10 (before 21d), RT2-21b. **Risk.** Medium.
- **Exit gate.**
  - 21a: a pixel-identical step first, with the new lanes added and the consumers unchanged (diff 0 on the garage, the bridge's three cameras and the camp); the sentinel moved to id 0 while the floor still exists, diff 0, including pixels where particles, the grid or world UI cover a surface; each consumer then switched over separately, diffed against the reference renderer; the `RGBA16F` to `RGBA16_UNORM` arm: a curved chrome pole's reflection against a float32-normal build, at most 1 display level; albedo to sRGB8: a dark-tone banding diff against a float-albedo build, judged by the owner; G-buffer pass time at 2560x1600 within today's +5%, and bytes per pixel as stated, by RT-14's method.
  - 21a, for D27: the anisotropy parity scene (RT2-2h), RT against raster, within grain.
  - 21a, the split geometry normals: once the reflection's curvature reads the smooth-normal lane, `--debug-view=reflection-image` on the garage's normal-mapped flat floor gives an image distance within 5% of the flat-plane value (the reflector-to-hit distance), so the normal map is no longer read as mirror curvature (REFL-04; RT2-0's attribution arm (f) measures today's value first).
  - 21c: with `--aa=none` at the owner's swing, the pole edge-jitter and speckle no worse than the 16-ray reference (today 8.83 and 494), measured only after RT2-28a separates the frame filter's depth test from specular aliasing; the converged `--aa=none` picture equals the parked reference within grain; tube reflections on the puddle against the reference renderer, diff near zero (floor sharpness by spin_measure's "sharp" score); a 16x SSAA reference of a slow garage orbit, diffed per frame: flicker on the poles and floor no higher than today; G-buffer pass at most +0.1 ms at 2560x1600; cook time recorded.
  - 21d: the primary-ray parity test on all five test cameras, the largest decoded difference at most one quantisation step per lane (albedo 1/255, roughness 1/65535, normal 0.01 degrees), parallax the documented exception; the garage's normal-mapped wall in the floor reflection, diffed against the reference renderer, closer than today; `speckle_count.py` on the moving-car harness and `truth_test.py`'s bright-speck count no higher; ReflectionTrace and GI-trace times at 2560x1600 within +10% of today (the owner sets the final bar).
  - 21e: the acne fixture (a strongly normal-mapped plane under grazing light) at the origin and 10 km away: zero self-shadowed texels; a thin-wall leak fixture: zero leaking pixels; the garage's default frame against today within grain (T5's parity was 0.034 levels); RT2-46's aimed rays on the new offset: the RT-11 back-wall check (no wall converging brighter than the reference) still passes.
- **Size.** 7-9 sessions (21a 2-3, 21c 2, 21d 2-3, 21e 1).
- **Closes.** The hit-detail half of MAT-02, MAT-04, MAT-12, GPU-14, direct-09, direct-11, GEO-10, the geometric-lane part of REFL-04, the shading-model-id groundwork of MAT-15 and its anisotropy (D27), the terrain-at-hits part of ISS-38; part of ISS-41; frame-16 (with RT2-17); the comparison's claims F-C1, F-C2, F-C6 and B-C7 and findings F-N4 and F-N6 (section 8).

### RT2-22 · One composition pass: RT-2.2, then compute; the second raster deleted in RT mode

- **In plain words.** In ray-traced mode the scene is drawn a second time just to combine the lighting, and the material is sampled twice. This item first makes that drawing read the G-buffer (RT-2.2, as filed), then replaces it with one compute pass that combines every signal with one shared function, so a reflection's brightness comes from one formula. By the owner's ruling D26, the raster drawing stays only for when MSAA is selected; it calls the same functions, and a test proves both give the same picture except on the outlines MSAA smooths.

> **Owner's ruling D26 (2026-09-25).** MSAA in RT mode after this item: compute composition is the default;
> the second (raster) drawing is added only when MSAA is selected, so MSAA 4x stays selectable in RT
> mode (keeps D3). Both paths call the same shading functions (RT2-21), and a test checks they give the
> same picture everywhere except the object outlines MSAA smooths. Frame cost only when MSAA is on.
> *(This rejects the comparison's recommendation to offer MSAA in the raster recipe only, which is what
> Unreal's and HDRP's deferred paths do.)*

> **Changed by the comparison (2026-09-25).** D2's order is kept: step 1 as filed, step 2 in compute. Now
> specified: the composition takes the diffuse albedo and the full-colour specular albedo from the one
> BSDF module's MaterialFactors, energy compensation included, the same function the traces and
> denoisers use; the reflection accumulates demodulated incident light with no hue tint, and its weight
> is applied once; emissive reaches the composition through the colour attachment the G-buffer pass
> writes; the reflection's weight never travels in the colour's alpha (it gets its own RGB lane, because
> RT2-28 adds the reflection after the final pass); no shading-model tile classification until a second
> model has content. The environment mip chosen from the reflection vector's screen derivatives is kept,
> computed in compute by finite differences of neighbouring G-buffer normals and depth (the verification
> found this is an equivalent guard, not a lead, and that the filtered roughness cannot reproduce it).

- **What is wrong now.**
  - In RT mode the opaque frame is still assembled by the forward lit shader. It rasterises every opaque draw a second time and samples the material again: about 0.6 ms of Headland's 2.2 ms lit pass (RT-SERIES.md:1692).
  - Per fragment, it combines probes, the field, the split-sum weight and emissive.
  - It writes an indirect lane that nothing reads while ray-traced GI is on.
  - It keeps the reflection's weight in the colour's alpha channel.
  - The weight's magnitude and hue come from two different shaders: the trace applies a hue-only tint (`reflection_trace.rvshader:857-884`), the lit shader sends a luminance weight through the alpha (`pbr_fragment.glsl:6252-6257`), and the resolve repeats the tint for pooled lamps. An honest re-measure found the full-colour form about 7% darker; the in-code "4x" and "tint lost" comments are stale.
  - The lit pass also traces the subtractive shadow's rays itself (E-N6).
- **What changes.**
  - **Step 1** is RT-2.2 exactly as filed: the lit pass reads the G-buffer, with the albedo lane moved to sRGB8 first, and is diffed against the path that samples the material again.
  - **Step 2** (approved by the owner for RT mode only, D2, and kept only if it measures faster with a near-identical picture) is a full-screen compute composition in 8x8 tiles, with an early out where the id is 0. The colour is: the colour already in the target (emissive or the background) + direct diffuse x diffuse albedo + the specular signal x S + (the baked bounce + sky light) x AO x diffuse albedo + the realtime layer x diffuse albedo + the baked lamps' far direct light + fog (RT2-33). The diffuse albedo and S (full colour, energy-compensated) come from MaterialFactors. AO applies to stored light only. A uniform branch on the shading model stands in for tile classification.
  - Then the opaque lit raster pass leaves the RT recipe's default path, together with the unused indirect lane, and its own shadow traces (the loss candidates are in RT2-12's sampler by then). The raster recipe keeps it.
  - **The MSAA path (D26).** When MSAA is selected in RT mode, the raster drawing composes the frame instead of the compute pass. It includes the same composition function and MaterialFactors, and reads the same per-pixel signals for every sample, as today; the G-buffer stays single-sampled. It costs frame time only while MSAA is on.
- **Owner sees.** 1 to 2 ms back at Headland (0.6 ms of it measured). Reflection brightness from a single formula, which settles the 7%. MSAA 4x still selectable in RT mode.
- **Touches.** `pbr_fragment.glsl` (5611-5925, 6184-6260), `FrameGraphBuilder.cpp` (1648-1665, 2607-2662, 3772-3826), `Renderer3D.cpp` (`DrawLit`), `reflection_trace.rvshader` (857-884), `reflection_resolve.rvshader` (447-456), `reflection_composite.rvshader`, a new composition shader and its shared include.
- **Scope.** Refactor and deletion. **Depends on.** RT2-21 (D2 approved), RT2-21b. **Risk.** Medium.
- **Exit gate.**
  - Step 1: diff near zero against the sampled path on the garage, the bridge's three cameras and the camp; the owner judges the dark tones.
  - Step 2: diff near zero against step 1, except for the documented changes to the composition. Demodulating then remodulating is an identity on a flat-lit fixture (diff 0, at most 1 level). Headland with glows on against off, restricted to glossy opaque pixels: diff 0. The composed reflection against the reference renderer, near zero (the 7%).
  - The composition pass at most 1.0 ms at 2560x1600 on the garage (section 6's proposal; the owner sets the bar from the first measurement), and the frame faster than step 1 by more than the A,B,B,A spread, or step 2 is not adopted (D2).
  - D26: with MSAA 4x selected, the MSAA raster drawing against the compute composition: a diff that is zero everywhere except on the object outlines (a mask of the pixels whose MSAA samples cover more than one id); the MSAA path's cost recorded; with MSAA off, no cost from it.
- **Size.** 5-6 sessions.
- **Closes.** RT-2.2, MAT-01 (the composition; the visibility-buffer end state is in section 7), MAT-08, frame-10, the final half of MAT-09, the structural half of materials-s1; part of ISS-17; the RT-mode half of MAT-04, GPU-14 and direct-09 (with RT2-21a).

### RT2-23 · Reflection denoiser v2

- **In plain words.** The reflection's cleaning-up chain is the most tuned part of the engine: about twenty rules that depend on camera speed, hits lit with one random lamp (so even a mirror is grainy), and a lamp's shine computed twice by two passes. This item rebuilds it on the design of NVIDIA's NRD denoiser, the shipped design that meets rule 3 (section 11): honest inputs, two ways to find last frame's picture blended by confidence, and a spatial clean-up for young pixels. NRD itself is built beside it as a comparison arm (D15); v2 must be at least as good on the same input. The two other cures, deterministic lighting at mirror hits and one specular signal that counts each lamp once, need things that land later in M4 (the ray-rate tiers, the hit-shading contract, the direct light's new denoiser), so they are their own split items, RT2-23f and RT2-23g, run after RT2-27.

> **Parts done early, in RT-24 (2026-09-24), on today's accumulator:** (23b) a content check under
> any motion -- the history is trusted by how alike the two lobes' pictures are where the reflection
> slid across the surface; (a consistent resolve) the aimed samples' lamp light kept out of the
> neighbour sharing and added back per texel. Tried and reverted: (23b) the memory from the lookup's
> own uncertainty in place of the travel-based smear cap, slack and shrinking floor (change 3; it made
> the orbits worse), so those rules are still in the source and still this item's. The rewrite itself
> is still this item's.

> **Added from RT-24's hard-coded-values audit (2026-09-24; section 10 lists every value).** Two in the resolve (the
> neighbour sharing). (1) A tap whose light is more than half a lamp's own glow skips the outlier clamp
> entirely (`kEmitterShare` 0.5): the lamp hits a rough floor catches by chance pass whole. General form:
> no threshold on where light came from -- split each tap into its lamp part and the rest and bound only
> the rest, or treat every tap by one rule. (2) The disc is a fixed fifth of the lobe (`kReuseFraction`)
> and each tap's weight is capped (`kMaxWeight`). General form: the disc as large as possible while
> blurring the reflected image by at most one texel beyond the lobe's own blur, and weights bounded by
> construction (pairwise MIS over the disc's lobes) instead of by a cap. Also here: sharing lamp light
> correctly (fix 1 of 2026-09-24 keeps it unshared, which removed the halo but costs grain in motion;
> re-aiming each pixel's lamp sample toward the lamp point it hit is RT2-12's reuse arm).

> **Added from RT-24's hard-coded-values audit (2026-09-24; section 10 lists every value).** The rest of the
> reflection chain's fixed values. v2 carries none of them over as a tuned number; each is replaced
> by the general form given.
> - *Memory (live).* RT-24's floor that shrinks as the picture travels sets the memory to one frame
>   at speed, so the one-ray grain reaches the screen unaveraged. Behind it: the travel hyperbola, the
>   smear cap (`SmearTexels` 6, which alone sets the memory at the slow start and end of a drag),
>   `slack` with its 0.25 floor and its rule that a mirror forgets four times faster (backwards: a
>   near-mirror's lookup is exact, so it should keep memory longest), the full memory (`Memory` 64),
>   `SilhouetteMemory` 4, the 0-0.3 gloss ramp, and the moving layer's own 12, slack and floor.
>   General form: one rule -- keep N frames while N times how far the old picture can be misplaced
>   stays within the picture's own blur (the lobe's footprint, never under one texel), and stop
>   adding frames once the texel's standard error is under one display step. A first build of it
>   (change 3) made the orbits worse and was reverted: the misplacement estimate needs the work, not
>   a number. `MovingMemory` (now 1 for reflections, which does nothing) is deleted, and its comment
>   in `Renderer3D.h` corrected.
> - *Trust tests (latent).* The direction test (`kMirrorSensitivity`, `kRoughSensitivity`, its
>   halfway ramp), the hit-distance test (`kMirrorHitTolerance`, `kRoughHitTolerance`, `kHitAgree`,
>   the ramp to three times, a 0.25 m floor), the struck-object test (`kHitIdentityAgree`,
>   `kHitNormalAgree`, the 0.05-0.3 ramp), the match confidence (`kMaterialAgree`, `kIdAgree`, the id
>   and plane ramps, the 0.8-0.94 facing ramp), the facing refusal, the 0.5 roughness refusal, the
>   silhouette tests, the curvature allowance (0.15 per metre), the plane tolerances in metres (0.05 m
>   plus 1% of the distance in the accumulator, 0.02 m plus 1% in the resolve and the blur),
>   `kSurfaceNearer` and `kCurvedMover` (per metre). General form: each tolerance from the spread of
>   the quantity it tests -- the lobe's angular width for directions, the lobe's hit-distance spread
>   for distances, the two lobes' overlap for roughness, one texel's depth span for planes, the
>   normal encoding's own step for curvature. Nothing in metres, no hand-set ramp.
> - *Bounds (latent).* The glossy end's 12 spreads and its 0.08-0.3 ramp, `BoundWidth` 3,
>   `kSettledBound` 2 and `kSettledFrames` 32, the moments' 16 frames. General form: the bound is a
>   stated number of the texel's own measured spreads (a convention, as the firefly clamp's 3 is), and
>   the moments' memory follows the jitter cycle.
> - *Image distance (latent).* Its memory (a cap of 8 and a 1:1 blend with the stored distance) and
>   the 1 km miss clamp; in the trace, a miss written as 10 km and, with more than one ray, a mean of
>   different surfaces' distances. General form: 23a's one real hit per texel with misses flagged,
>   blended only where the stored and fresh distances describe the same surface.
> - *Resolve (latent).* `kCredibleDensity`, `kMaxReach` (24 texels), and the texel-size fallback
>   (0.0015 of the distance) and floor (0.0002 of it). General form: no separate density bound (the
>   ratio weight already carries it), the reach from the lobe's footprint, a texel's size from the
>   projection itself.
> - *Blur (two live).* The uncertainty gate's edges (0.02 and 0.20, fitted to one measurement): at
>   speed they give each texel of the rough floor anything from no blur to full blur, independently
>   of its neighbours, which reads as speckle. `MovingRadius` (4 texels) caps how much one-ray grain
>   the blur can remove at speed. Also `BlurFrames`, `YoungOverreach`, `kSigmaLFloor` and the mean
>   floor (absolute 1e-3), and the texel-size fallback and floor. General form: the radius is the
>   lobe's footprint in texels, and only the edge stop that compares a neighbour's difference with the
>   centre's own noise decides which taps count, so a settled texel keeps itself with no threshold;
>   floors relative to the values compared; a texel's size from the projection.
> - *Rays (latent, one defect).* Four rays where a surface moves on its own; the ray budget's 6-frame
>   young threshold, 1-ray minimum, 16-pixel tiles, 0.75-ray dead band and 8-frame dwell. And a
>   defect: the budget reads last frame's counts at the same screen tile, not where the texels were,
>   so a moving object gets another tile's rays. General form (23d): rays follow each texel's own
>   measured uncertainty, looked up where the texel was last frame; 23d sets the replacements for
>   the tile size, dead band and dwell by the same rule instead of carrying them over.
>   *(2026-09-25: step 23d merged into RT2-27, which now owns these ray rows.)*

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** The reflection ray
> budget's young-share thresholds and its row-only pair counting. General form: linear in the share,
> pairs counted in both directions. *(2026-09-25: these go with step 23d to RT2-27; section 10 moved
> their rows there.)*

> **Owner's rulings (2026-09-25).** **D15:** build NRD's ReBLUR specular as a reference arm behind a switch,
> for comparison only, never the default; denoiser v2 must be at least as good on the same input. (NRD
> runs on any GPU, under NVIDIA's RTX SDKs licence.) **D16:** delete the opaque moving layer, follow-hit
> and the glass copy's forced moving layer once v2 passes its tests, but test each one against v2 first.
> **D23:** if the reflection added after the final temporal pass is still too grainy after v2, keep
> working on the reflection's own denoiser; RT2-28 stays open, and the reflection is never added
> before TAA (rule 3 holds).

> **Changed by the comparison (2026-09-25).** Re-cut into steps: 23a inputs; 23-ref the NRD arm; 23b
> temporal; 23c spatial and stabilisation; 23d merged into RT2-27 (one classification and ray-rate rule
> instead of two tile maps); 23e structure and the D16 deletions; new 23f hit shading; new 23g one
> specular signal. The old steps left out the two largest noise and cost sources the comparison found:
> stochastic hit shading and lamp sampling done twice. Verification corrections: measured change is the
> same history-confidence input NRD documents and its sample implements (B-N1), so it is the baseline,
> and it cannot replace the identity, colour-box and hit-distance tests, because it measures a change in
> lighting, not whether the history was fetched from the right place (B-BB4); RT-24's lobe-agreement
> weight was only ever shown equal to what it replaced, and it is fed today by the broken image distance,
> so it is re-derived on the fixed distance and must beat NRD's form in a gate (B-BB5); the one specular
> signal is RTXDI and NRD practice, not something beyond the top engines, and it needs a single
> representation per fixture (D13) and emitters sampled on their real surfaces (RT2-11); the resolve's
> emission exemption can go only once RT2-11 lists every surface that emits at all.

> **Split after the second critic's review (2026-09-25).** Steps 23f (hit shading) and 23g (one specular
> signal) used things that land later in M4: RT2-25's hit-shading contract, RT2-27's mirror-rate tiles
> (and RT2-27 itself depends on this item) and RT2-24's step 1. So this item could not be complete, and
> committed, in its slot. They became the split items RT2-23f and RT2-23g, run after RT2-27 with their
> designs unchanged. Step 23e's per-tile roughness classification is built here on RT2-20b's mechanism,
> and RT2-27 later adds the ray rate to it.

- **What is wrong now.**
  - The depth used to find last frame's picture is a mean that includes misses as 1e4, re-averaged by the resolve, clamped to 1e3 and blended with the value stored at the surface's old position (B-C3).
  - Mirror curvature comes from normal-mapped normals and has no sign, so there is no concave case.
  - Under camera-only motion almost nothing checks the reflected content. The colour box is 6 to 12 spreads wide on glossy surfaces, the struck-identity test is off unless an object moves, and the distance test is loose. The accumulator picks one history candidate and never blends (B-C5). Keeping fewer frames is the only lever: ghosts or grain.
  - The default sampler replaces any draw below the horizon with the exact mirror direction, which piles rejected draws onto one direction; visible-normal sampling is off. The density written for the resolve is always the old sampler's, with a mean direction for multi-ray texels, whichever sampler drew the ray (B-C4).
  - Each hit keeps one light by weighted reservoir with one shadow ray, so even a roughness-0 ray's hit shading changes every frame where two or more lights reach it; hits read mip 0 (B-C1, B-C10). (Fixed by RT2-23f.)
  - A lamp's shine is computed twice: DirectTrace's analytic highlight (with a four-frame memory reprojected by the surface) and the reflection ray striking the glowing mesh, with nothing weighing one against the other (B-C2). The lamp selection is also done twice, and the 8-neighbour lamp pool (+2.1 ms) duplicates what a shared light structure would give once. (Fixed by RT2-23g.)
  - There are two frame counters.
  - The blur uses fixed relative-error gates (0.02-0.20), a hard roughness cut of 0.2 and a normal power of 8; the propagated variance is written but never weighs a tap (B-C8).
  - One 1,973-line shader serves eight signal slots, through mode flags packed into floats and about twenty tuning constants.
  - Extra rays arrive only while something moves (RT2-0 patched part of this; the rest is RT2-27's).
  - The reflection is composited before TAA, which filters it a second time (D-C1).
- **What changes.** The new denoiser runs behind `--reflection-denoiser=v2`, with the old path as the reference arm. What landed in RT2-0 carries into v2 unchanged. The structural rewrite, in named steps, each ending in a report:
  - **23a. Inputs.** Bounded visible-normal sampling (Eto and Tokuyoshi 2023): draws below the horizon are rejected (zero contribution), never swapped for the mirror direction, and the trace writes the pdf of the sampler that actually drew each ray; a multi-ray texel keeps one representative ray with its own pdf. A tracking hit distance (the smallest in-lobe hit over the resolve's disc, misses flagged and excluded) kept apart from the filtered distance used for weights. Signed geometric curvature from RT2-21's smooth-normal lane, with the thin-mirror equation. The stable struck identity (RT2-5). One footprint-filtered roughness (RT2-21c), the same the analytic lights use. Full-colour demodulation by the specular albedo from MaterialFactors (RT2-22). The low-discrepancy sequence gives way to RT2-7b's blue noise.
  - **23-ref. NRD's ReBLUR specular as a reference arm** (D15), behind `--reflection-denoiser=nrd`, fed by 23a's inputs, inside private implementation files (the API-wrapping rule), under NVIDIA's RTX SDKs licence. It is a measurement arm for 23b's and 23c's gates, never the default.
  - **23b. Temporal.** Two candidates: surface motion, and virtual-image motion from the tracking distance and the signed curvature. Each is fetched bilinear within one object, with a 3x3 search on refusal (Catmull-Rom is not used: RT-24 measured it worse here). They are blended by a confidence that is the product of: a direction test sized by the lobe; RT-24's lobe-agreement weight, re-derived on the fixed image distance; parallax against the lobe's spread, as angles; the stable struck identity under any motion, the camera's included (the old RT2-0 arm I); the predicted change in hit depth given the camera's parallax against the observed change; object and material ids. Measured change is the history-confidence input, alongside those tests, not instead of them. One history counter, capped by keeping N frames while N times the per-frame misplacement stays within the lobe's own blur (never under a texel), and adding no more once the standard error is below one display step: no frame-rate, metre or texel-travel constant (NRD's frame-rate constant is deliberately left out, rule 2). A fast luminance history, one jitter cycle long, clamps the long one; its width comes from the fast history's own temporal spread, not from the one-ray neighbourhood (why RT-24's arm H failed).
  - **23c. Spatial and stabilisation.** The resolve: the ratio estimator over a disc sized from the lobe's footprint, weights from 23a's true pdfs (R11's density and Jacobian variants re-measured with honest densities); outliers bounded relative to the taps, a stated number of the taps' spreads in exposure-scaled luminance, applied to the lobe part only; the emission exemption (`kEmitterShare`) and the second-highest floor removed once RT2-11 lists every emitting surface; the tap count from the footprint, capped by the preset; the resolve writes the tracking distance and the filtered distance. Then a history fix: for texels younger than one jitter cycle, a sparse 5x5 rebuild whose stride is the lobe's footprint, shrinking as the history grows, with geometry, normal, roughness and tracking-distance weights, run after outliers are bounded. A blur whose radius comes from the lobe's footprint and the history length (ReBLUR), with an edge stop comparing each tap's difference with the centre's propagated variance (ReLAX), relative floors, and a texel's size from the projection. It subsumes today's always-on 4-texel variance blur (D-N5). Finally a temporal stabilisation pass with 23b's candidate blend, a luminance box from shared memory, and measured change as its anti-lag. After this the signal needs nothing from the frame filter.
  - **23d.** Merged into RT2-27 (the ray rate by the lobe's footprint, traded within one budget, young texels kept on for the settle length, read at the reprojected position).
  - **23e. Structure and the D16 deletions.** Compute passes over RT2-20's ray lists, with shared-memory neighbourhoods; one shader per kind of signal, built from shared functions; typed lanes (`R32_UINT` ids and flags, a 32-bit tracking distance, camera-relative plane offsets); histories sized to the listed points; one roughness classification per 16x16 tile, built here on RT2-20b's tile classification and read by every pass (RT2-27 later adds each tile's ray rate to the same classification). The glass pane's layer runs the same chain over its own point list (RT2-31). The signal moves onto RT2-17's tables here. Then, one at a time, the opaque moving layer, follow-hit and the glass copy's forced moving layer are each tested against v2 (the moving layer's arm (a) removed a ghost, with grain its one defect) and, where v2 is at least as good, deleted (D16).
  - **23f and 23g.** Split out, unchanged in design, as RT2-23f (hit shading) and RT2-23g (one specular signal), which run after RT2-27 (see the note above and those items).
- **Owner sees.** Under a fast swing, lower ghosting and lower grain than today; RT-24's structural fix. Faster settling. Steadier chrome cube and poles. (Mirror-like surfaces that stop sparkling when parked, and each lamp's shine drawn once, come with RT2-23f and RT2-23g.)
- **Touches.** `reflection_trace.rvshader` (284-329, 692-702, 857-911), `reflection_resolve.rvshader` (169-634), `reflection_accumulate.rvshader` (split), `reflection_blur.rvshader`, `reflection_budget.rvshader`, `include/reflection_emitters.glsl` (the sampling density), `pbr_fragment.glsl` (2831-2842, 6193-6204), `FrameGraphBuilder.cpp` (the signal descriptor), `Renderer3D.cpp` (the signal module), private NRD integration files. (The hit shading's lines, 343-589 of the trace and 3098-3171 of the lit include, and DirectTrace's specular half moved with RT2-23f and RT2-23g.)
- **Scope.** Rewrite. **Depends on.** RT2-2, RT2-5, RT2-7, RT2-11 (emitters on their real surfaces: 23c's emission exemption goes only once every emitting surface is listed), RT2-20 (ray lists, and 20b's tile classification for 23e), RT2-21, RT2-22. **Risk.** High: the most-tuned part of the engine.
- **Exit gate.**
  - 23a: a shader self-check: a million draws binned over the hemisphere match the written pdf (chi-squared within 1%) at roughness 0.05, 0.2, 0.5 and 0.9 and a grazing view; the parked garage floor at a grazing view against the reference renderer: bias no worse than v1, bright-speck count lower; the owner-speed swing's `speckle_count.py` dots no worse than v1.
  - 23b: a pan against an orbit at 16 rays: the pole score with memory on at or below memory off in both; at the owner's speed and at `--swing=15 --rate=2`: pole, car and tube ghost and grain at or below v1 at the same ray count, and at or below the NRD arm (D15); the lobe-agreement weight no worse than NRD's lobe and roughness form on the same test, or NRD's form is used; the after-stop row's fifth frame within the parked noise floor; parked within the noise floor of v1 and `edge_shake.py` no worse; `emitter_lag.py`'s reflection share lower than v1 and RT-22's floor numbers no worse; **and the owner's live reading of the moving-light drive shorter** (`watch_arm.py light 8`: on 2026-10-12, with RT-24 as shipped and its measured-change fix, the owner saw the cube's reflection arrive about 200-250 ms behind the cube, "very VERY clean" otherwise): judged live by the owner, and in frames by `emitter_lag.py` at the live window's close camera (`-4.3,1.0,-2,3.2,25,8`) against the same run on v1; accumulate at most 0.5 ms at native (proposal).
  - 23c: with the frame filter's memory off, and later `--aa=none` (RT2-2h): young reflection pixels reach the parked noise floor within one jitter cycle after a cut, the owner-speed speckle within 5% of the TAA-on count, and the lag benefit of measured change re-measured without TAA (its 9.2-to-4.8 record was taken through TAA); the parked tube and pole detail no lower than v1; `ghost_map.py` shows no halo round lamp reflections (fix 1's defect must not return); the resolve at most 0.6 ms and history fix, blur and stabilisation at most 0.8 ms at native (proposals); against the reference renderer, region by region (floor, car, cube, poles): distance, bright-speck count and bias no worse; the NRD arm no better on the same input (D15).
  - 23e: pixel-identical against v2's fragment form before the compute port is kept (diff 0, or within the half-float rounding floor); reflection VRAM per pixel reported and below today's 96 bytes for the history pair; each D16 deletion preceded by its arm against v2.
  - The bridge's three cameras (diffs, glint flicker). A,B,B,A for every step, with the owner setting the cost bar from 23a's first measurement (section 6).
- **Size.** 12-14 sessions (23a 2, 23-ref 3, 23b 3-4, 23c 2-3, 23e 2). (23f's 2-3 and 23g's 1-2 moved with them.)
- **Closes.** REFL-03, REFL-04, REFL-05, REFL-11, REFL-12, REFL-15, ISS-3, ISS-20, ISS-21, ISS-30; RT-22's reflection third; the comparison's claims B-C3, B-C4, B-C5 and B-C8, and findings B-N1 and D-N5 (section 8). (REFL-07 moved with 23d to RT2-27; B-C1 and B-C10 moved with 23f to RT2-23f; B-C2, B-C6 and B-N6 with 23g to RT2-23g.)

### RT2-24 · Direct light: visibility reconstructed apart from shading

- **In plain words.** The direct light is cleaned up over time only, its highlight keeps four stale frames when the view turns, and its young pixels are left for TAA. This item first builds the denoiser MegaLights ships (the baseline, rule 1), with a spatial clean-up sized by how soft each shadow is. Then, as an arm that must beat it, it keeps only "how much of the light is blocked" in the history and recomputes the light itself every frame, so a moving lamp's pool of light has no trail. The lamps' shine moves to the one specular signal (RT2-23g).

> **Carried from RT-24 (2026-09-25).** With DirectTrace's specular left out, the floor stays sharper
> through the owner-speed swing (sharpness 0.82 -> 0.96, edges 14.87 -> 13.91): the highlight's
> four-frame memory softens a moving floor. The visibility split here addresses it.

> **Added from RT-24's hard-coded-values audit (2026-09-24; section 10 lists every value).** The
> lamps' own highlights (the direct light's specular half) keep four frames however fast the view
> turns (`PairMemory` 4, and the diffuse kind's floor of 4): a view-dependent highlight averaged over
> views it no longer matches -- smear on the chrome poles and the wet floor in motion. General form: a
> view-dependent payload keeps history only while the view direction stays inside its own lobe (the
> reflection kind's direction test, floor one), the diffuse half keeping its surface-bound floor.
> Also: that direction test runs on this signal's view-independent half too, cutting its memory to
> four on smooth surfaces while the camera turns (it belongs to view-dependent payloads only); and
> every diffuse-kind signal's blur reaches a fixed 8 texels (`MaxRadius`; first pass only). General
> form: the reach from this item's penumbra width, in texels.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Extra lamps for
> young pixels use a fixed 6-frame threshold and tile; the shadow-ray fade uses fixed distances
> (300-600 m at Quality) and a visibility borrow that depends on the order lamps are visited. General
> form: extra lamps from the history's shortfall against its own memory; skipped rays by Russian
> roulette (unbiased, order-free); the fade by a lamp's share, not metres. *(2026-09-25: the young-pixel
> rule is RT2-12b's; the shadow-ray fade and the borrow leave RT mode with the lit loop (RT2-13, RT2-31),
> and the raster recipe takes Russian roulette in RT2-44.)*

> **Changed by the comparison (2026-09-25).** Two steps. Step 1, the baseline (rule 1): the MegaLights-shaped
> denoiser on RageV's contract. Step 2, the target, adopted only if it beats step 1 at equal time: the
> diffuse visibility ratio in the history, with the light without shadows recomputed every frame.
> Verification corrections: measured change is at parity with the gradient RTXDI ships (3x3 strata,
> the same light sample on two frames), and RTXDI filters its gradient spatially and temporally before
> using it, so step 1 copies that pipeline instead of reading the raw block value; the ratio is a
> self-normalised estimator (consistent, biased at finite K), and the light without shadows is exact only
> over the listed lights, so its hidden-candidate part needs its own accumulation; the history cannot
> converge without TAA until RT2-7 unfreezes the picks. **Partly rejected from the comparison's proposal:**
> the specular ratio (R_s). The lamps' shine goes to the one specular signal (RT2-23g), where the
> direct pass and the reflection share it by the power heuristic (D13), so this item's step 2 carries the
> diffuse ratio only. The verification had found the R_s form weakest (its weights differ from the
> weights the picks are drawn by).

- **What is wrong now.**
  - The direct light is denoised as shaded light, over time only. The young-pixel blur is off because a spatial filter that stops only at G-buffer edges smeared hard shadow edges on flat surfaces (T5, measured); the code says TAA carries the noise instead (`Renderer3D.cpp:7407-7411`).
  - The specular half is stored as shaded light, keeps four frames, is clamped by a 3x3 RGB box, and leans on TAA to converge (A-C7).
  - The shadow ray returns only 0 or 1, with no occluder distance, so a penumbra-sized radius needs RT2-10's shadow query first (A-C8).
  - Most of RT-22's remaining trail sits in this history: 19.5% of the floor beside a moving light off by more than 16 levels, with the tubes as lines.
- **What changes.**
  - **Step 1, the baseline.** On the direct signal: diffuse demodulated by albedo, and, until RT2-23g takes the shine, specular demodulated by the specular albedo. A 5x5 variance clip in YCoCg that cuts the history in proportion to the history's distance from the box. Measured change, filtered spatially and over time before it becomes the confidence (RTXDI's pipeline), plus a fast-history clamp. A variance-guided spatial pass whose radius is the penumbra width in texels (the occluder distance from RT2-10's shadow query times the source's angular size) instead of a fixed `MaxRadius` or G-buffer edges alone, with enough taps that no holes are left for TAA to fill. Young pixels get more picks (RT2-12b's shortfall rule) and more spatial taps for their first frames. `PairMemory` 4 and the diffuse floor of 4 go; the view-direction memory rule applies to view-dependent payloads only. The signal moves onto RT2-17's tables here. Where a transmissive caster exists (RT2-31), the visibility carries RGB, so tinted shadows survive.
  - **Step 2, the target (an arm).** The history holds the diffuse visibility ratio R_d: the K picks' visibility weighted by their diffuse terms. The light without shadows, U_d, is recomputed every frame at the current light positions: exactly over RT2-12b's listed lights, plus the hidden-candidate estimate, which gets its own accumulation. Output = U_d x the denoised R_d. It replaces step 1 only if it beats step 1's gates at equal time.
  - The pieces step 1 builds are generalised for every diffuse-kind signal in RT2-47.
- **Owner sees.** A shorter trail behind moving lights. Young pixels rebuilt without TAA. Hard shadow edges stay hard; soft ones stay soft.
- **Touches.** `direct_trace.rvshader`, the direct accumulate variant, `Renderer3D.cpp` (the signal parameters, 7402-7419; `Renderer3D.h` 348-367), `reflection_accumulate.rvshader` (443-463 for this signal), `FrameGraphBuilder.cpp`.
- **Scope.** Refactor. **Depends on.** RT2-7 (unfrozen picks), RT2-10 (occluder distance, the score equal to the term), RT2-12b, RT2-20. **Risk.** Medium: an exact light without shadows pulls against a candidate structure of fixed cost, so the estimator is chosen by measurement. Step 2 carries a known failure mode: MegaLights' authors tried a ratio split and dropped it (A-C4, B-N10), because with a stochastic light choice the factored term jumps between neighbouring pixels, so the filters run on values that change discontinuously where the most important light changes. Its gates therefore include a check for exactly that.
- **Exit gate.**
  - `emitter_lag.py`: the floor beside the moving light below 19.5%, and step 2 below step 1; the car's side improved. A moving light with static occluders: the lag left in R_d reported.
  - The owner's swing (`--swing=15 --rate=2`), the direct signal alone (`--capture-signals`): pole edge-jitter and speckle (over 32 and 64 levels) at or below the direct signal's own reference, captured the same way at the same swing from the every-light arm (K=0) with 16 area samples per light (the 16-ray reference's method applied to this signal). The composed frame at the same swing: speckle and pole edge-jitter no worse than today's 513 and 9.51. (The 16-ray reference's 494 and 8.83 are whole-frame numbers of the reflection chain, so they are not this signal's bar.)
  - Step 2 only: on the many-light scene (RT2-2d), the converged diff against `--reference-render` shows no pixel-scale discontinuities where the most important light changes from one pixel to the next (those boundaries found from the every-light arm's per-pixel strongest light; the diff restricted to them no worse than step 1's). This is the failure that made MegaLights drop the ratio split (A-C4, B-N10).
  - T5's shadow-edge test: the moving wall error no worse than a baseline re-measured on the current build first. (The old 2.71 levels was the shared memory-64 figure; the shipped memory of 4 measured 1.66 (`RT-FIRST.md:171-174`), and both predate the 3.07 m tubes, which now cast real soft edges.)
  - The parked converged diff against `--reference-render` within grain (and against the every-light arm, for sampling error); `smear_metric.py` (the dolly) no worse.
  - Under `--aa=none` (RT2-2h): frame 400 against 200 within the parked noise floor; young and moving grain recorded.
  - Cost: the direct light's total at or below 2.9 ms in the garage (DirectTrace at most 2.0 of it) and 1.8 ms at Headland at native (section 6's 2026-09-25 proposals, reset by the owner from step 1's measurement). A,B,B,A.
- **Size.** 6-8 sessions.
- **Closes.** direct-04, direct-s2 (the structural half); RT-22's direct-history part; the comparison's claims A-C6, A-C7 and finding A-N2 (section 8).

### RT2-47 · Every diffuse-kind signal stands alone (new, 2026-09-25)

- **In plain words.** Today none of the soft, diffuse lighting signals (direct light, AO, the live bounce, glass direct light, the sea's lamps) rebuilds its young or newly revealed pixels, and none of them bounds bright outliers: the code leaves that to TAA. This item writes one shared set of rules, used by all of them, so each stands on its own without TAA: a measured-change record for AO too, a disocclusion test with no metres, an outlier bound, and a spatial fill for young pixels that fades out as the history builds. Nothing should change when parked; young and moving pixels get cleaner with TAA off.

> **Carried from RT-24 (2026-09-25).** With the frame filter's memory off, the owner-speed swing's
> speckle nearly doubles (513 -> 908 dots): wherever the filter restarts a pixel, the raw light of the
> passes under it shows. This item is where each of those passes becomes clean on its own.

> **New, from the comparison (2026-09-25).** Every shipped denoiser rebuilds young pixels from their
> neighbours (NRD's history fix, MegaLights' disocclusion boost, Lumen's double-strength bilateral); RageV
> sets the young blur to zero on all four signals. RT2-28 cannot become an anti-aliasing-only pass until
> these signals stand alone, and no item owned their shared parts. RT2-24's step 1 builds the pieces for
> the direct light first; this item makes them the shared contract and moves AO onto it; the realtime
> bounce layer (RT2-25), the glass direct light (RT2-31) and the sea's lamps (RT2-32) are built on it.
> Verification corrections: GI already clamps each ray against a fixed absolute brightness (`kIndirectClamp`
> 4.0 and the near-panel clamp), so this outlier stage replaces them rather than adding a second one; and
> the measured-change check already costs about 0.94 ms while moving at the owner's Quality preset at
> 1600x900 (D-N4), not 0.56.

- **What is wrong now.**
  - `YoungRadius` is 0 on the direct light, AO and GI (and the water lamps), and the direct light's code says TAA carries its noise (`Renderer3D.cpp:7407-7411`, `7900`, `7922`, `7480`).
  - The diffuse kinds have no outlier stage in the accumulator (the firefly clamp is specular-only, `reflection_accumulate.rvshader:1426-1449`); GI clamps each ray against fixed absolute values instead.
  - The disocclusion test's plane tolerance is 0.05 m + 1% of the eye distance, in three copies (`reflection_accumulate.rvshader:596`, `1153`; `pbr_fragment.glsl:6224`), and the change filter's edge stop is 0.1 m per stride (`change_filter.rvshader:92`).
  - AO has no measured-change record, so an occluder's change is caught only when the receiver moves.
- **What changes.** Shared functions in the signal contract, in four steps, each ending in a report:
  1. **AO's record.** AO gains measured change: last frame's AO rays replayed. Measured change becomes the one lighting-change rule for every diffuse-kind accumulator.
  2. **Disocclusion and outliers.** The exact 32-bit id (RT2-5), and a plane distance within one texel's world footprint (view depth x pixel angle) plus the depth format's own step: no metres (the change filter's edge stop takes the same form). An outlier stage before accumulation: the fresh sample scaled back to the same-surface 8-neighbour mean plus 3 spreads, on exposure-scaled luminance (3 is the convention already accepted for the reflection's clamp); never applied to the direct light's unshadowed term, only to what is denoised. It replaces GI's fixed per-ray clamps (RT2-25).
  3. **The young-pixel fill.** After accumulation and never recurrent: one sparse, per-pixel-rotated kernel with SVGF-style edge stops (the plane distance, the normal, and the luminance difference against the pixel's own temporal spread). Its radius is the signal's own footprint: RT2-24's penumbra width for direct light, the AO ray reach projected, the GI trace grid's spacing. It is combined with the temporal mean by inverse-variance weighting (the temporal mean's variance from the moments over n; the spatial mean's over the tap count), so it fades out as the history builds, with no fade constant.
  4. **The migration.** AO and the direct light run on the shared functions; RT2-25, RT2-31 and RT2-32 build their signals on them.
- **Owner sees.** Nothing when parked. With TAA off, young and newly revealed pixels settle at once instead of showing raw grain.
- **Touches.** `reflection_accumulate.rvshader` (the diffuse variants; 596, 1153), `change_filter.rvshader` (92), `pbr_fragment.glsl` (6224), `rtao_compute.rvshader` (a record and re-light), `Renderer3D.cpp` (7402-7419, 7869-7938), `Renderer3D.h` (354-357), `FrameGraphBuilder.cpp`.
- **Scope.** Refactor. **Depends on.** RT2-2 (the signal-stands-alone command, the reference renderer), RT2-5, RT2-7, RT2-17, RT2-24 (the penumbra width). **Risk.** Medium: an outlier bound and a fill can bias; the reference renderer checks both.
- **Exit gate.**
  - Per signal, at the owner's swing, the per-pixel grain across centre crossings of the signal's own capture with `--no-frame-memory` no worse than today's same signal with TAA's memory on.
  - Per signal under `--aa=none`: converged (frame 400 against 200 within the parked floor), moving and young states included.
  - T5's hard-shadow edge test no worse than a baseline re-measured on the current build.
  - The converged diff against `--reference-render`: no structure beyond grain, so the outlier stage's bias sits within grain.
  - A lamp switched off: AO and GI reach 90% of their new value within 2 frames (`emitter_lag.py`-style, the clock pinned).
  - Cost at 2560x1600, A,B,B,A: the contract's own passes (outlier stage, fill, disocclusion) at most 0.1 ms parked and at most 0.6 ms at the owner's swing for all diffuse signals together; the measured-change records, AO's included, recorded separately (the owner's Quality preset sets their ray count). The owner sets the final bar from the first measurement.
- **Size.** 4-6 sessions.
- **Closes.** The comparison's claims D-C2, D-C10 and D-C11 and findings D-N3 and D-N4 (section 8); the young-pixel half of frame-03.

### RT2-25 · Bounce light from Realtime lights, added on top of the bake; one hit-shading contract

- **In plain words.** In a baked scene, Realtime lights give direct light but no bounce light at all, and a surface is lit four different ways depending on which pass looks at it. This item adds the owner's second layer (D4): a live bounce pass, one ray per traced pixel, with a cache so rays can stop early. By D18, at High quality it runs in every ray-traced view, and its rays also read the bake where they land, which gives baked lamps contact-scale bounce without lightmaps; at lower quality it runs only near Realtime lights. It gets 2.0 ms at native, aiming for 1.5 (D20). Every ray hit is then lit by one contract.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** The runtime
> irradiance field: a fixed blend per sweep and hysteresis, a fixed ray budget, 1 m cells in a 24x12x24
> box (this item), a lead distance and a move threshold in metres, a direction smoothing per frame,
> and a box that snaps by a step other than its cell pitch (found by the skeptics). Also GI's bound
> width, a miss read as sky even where the ray could not have left the scene, and the
> GI clamps 4 and 32 and the unoccluded sky at bake hits (this item). General form: history kept per
> world position with change measured, budgets and cell size as owner settings or from the scene's own
> volumes, one definition of the cell, a miss as sky only when the ray left the scene.

> **Owner's rulings (2026-09-25).** **D18:** when the live bounce pass runs: always at High quality, and
> only near Realtime lights at lower settings; confirmed after measuring how visible the difference is in
> the garage. With it on, the pass's rays also read the baked lamps' stored light where they land (the
> bake gathered per pixel). **D19:** moving objects blocking and bouncing baked lamps' light (step 25f):
> adopted if the diff images improve on today and it costs at most 0.3 ms. **D20:** the live bounce pass's
> budget at 2560x1600 is a 2.0 ms limit, aiming for 1.5 ms (1.0 ms is below every shipped figure); the
> extra time comes out of other passes' shares; the final number is set once the pass is measured.

> **Changed by the comparison (2026-09-25).** The two-layer design stays; the steps are rewritten
> (labelled 26a to 26d in RT-SERIES-2). The layering rule is stated per light path, with emitters and the sky as lights.
> The gather becomes 1 ray per traced pixel (the shipped pattern of id Tech 8, Snowdrop and Anvil), with
> the reach set by the scene's own bounds. The cache is decided, not optional: a spatial hash with cells
> sized by the pixel footprint, so nothing is in metres (SHaRC's and id Tech 8's structure, rule 1). One
> hit-shading contract. Probe capture through the traced world moves to RT2-27 as a conditional step.
> Two measured steps are added: 25e (the bake gathered at hit points) and 25f (the correction for moving
> objects). The old proposal of about 1 ms is below every shipped figure at about a million traced
> pixels (id Tech 8: about 1.47 ms for gather, denoise and upscale at 4K on an RTX 4080). Verification
> corrections: RageV's "half resolution" is the industry's quarter resolution, and "4 rays" is a
> per-tile ceiling; the "better at anti-lag" and "better emitter sampling" claims did not survive (the
> anti-lag is NRD's documented recipe; the emitter sampling still has two open defects and clamps too);
> "SH output recovers normal-map detail" is id's rationale, not yet shown in RageV, so it is gated; the
> bake-gathered-at-hits evidence is mixed (it won on the fixture and lost on the showroom's bright bay)
> and its cost must be priced from today's L2 field read (about 20 fetches on the fast path, about 150 on
> the careful one), not from a stale L1-era note; the cache's change test is compared against DDGI's
> shipped per-probe threshold, not presented as unshipped.

- **What is wrong now.**
  - In a baked scene the engine switches the live bounce pass off completely once the bake is ready. So Realtime lights give direct light but no bounce light at all: the garage has 7 of them and the bridge 14. The floor under RT-22's moving cube lights up, but none of that light reaches the car or the walls. The one leftover path is stale, not live: the garage's baked reflection probe froze whatever Realtime lights were on when it was captured into every baked frame's ambient, unshadowed (C-N10).
  - A surface is lit four ways depending on who looks at it: the lit pixel; a traced hit, using probe irradiance with no occlusion (frame hits also discard the field's bounce and sky and keep only its baked direct light, C-C3); a bake hit, using the sky with no occlusion and the flat ambient added unoccluded (C-C8, C-N3); a probe capture.
  - The live bounce, where it runs, casts up to 4 rays per traced pixel at a quarter of the pixels, shades every hit in full while walking every light, and costs 2.8 to 3.0 ms of trace time (+3.6 ms of frame) at 1600x900 when forced on in the garage; it cannot serve the bridge (only the sky as second-bounce light, a 12 m reach). Its reach is a fixed 250 m, and a first-bounce miss adds nothing while the 2-bounce path's miss returns raw sky.
  - The one world-space light store that exists (a 24 m box that follows the camera) is off in every shipped scene and read by no ray hit; its texture does not move with it, so misplaced light takes seconds to wash out.
  - Fixed clamps tuned on the showroom (4 and 32) cut real energy in brighter scenes, and the accumulator's diffuse kind has no stage for outliers.
  - The upsample weighs taps by depth only and carries plain colour for one normal, so normal-map detail inside each 2x2 block is lost.
- **What changes.** The owner's design (D4, updated by D18): bounce light in two layers that add up. Light adds up, so bounce from the baked lamps plus bounce from the live lamps equals bounce from all of them, and the two layers are exact together as long as the Static scene has not changed since the bake. Six named steps, behind `--realtime-bounce`, each ending in a report:
  - **25a. The layering rule, per light path.** The baked layer owns every path from a HalfBake, FullBake or HybridFullBake light (and from an emitter a baked light is linked to, or a static emitter marked baked) that bounces off Static geometry; in a scene with a bake it also owns the sky's bounce, and the sky's direct term at a pixel is the live sky colour times the baked sky visibility. The realtime layer owns every path from a Realtime light, every path from a moving or animated emitter, and, in a scene with no bake, the sky (escaped gather rays return sky light, and the pixel's separate sky term is dropped wherever the gather has an estimate). When the layer runs (D18): at High, in every ray-traced view; at lower settings, while a realtime-owned light's or emitter's influence bound meets the bound of last frame's gather hits or the view frustum (both measured, no metres). In a scene with no bake (the camp), every light is Realtime, so the realtime layer carries all of it.
  - **25b. The gather.** One cosine ray per traced pixel, on the trace grid (a divisor of 2 per axis at High and 4 at Low); the 4-ray level goes. Samples from RT2-7's sequence. The reach is the distance to leave the TLAS's bounds, so a miss means the ray left the scene. At a hit it reads the realtime cache when the hit is more than one cache cell away; otherwise, or on a cache miss, it shades the hit through the contract (25d) with realtime-owned light only, and writes the result into the cache. Emitter sampling covers realtime-owned emitters only, through the weighted pick (after RT2-11 fixes the double count and the rectangle power). The output is L1 spherical harmonics (the shipped baseline, rule 1: colour L0 plus luminance L1, two `RGBA16F` lanes), accumulated on the trace grid by the shared diffuse rules (RT2-47) and upsampled by the existing depth-weighted pass, which evaluates the SH with the full-resolution normal. Today's plain-colour output stays as the comparison arm: SH is kept if it lowers the per-pixel error in normal-mapped regions at no more than +0.3 ms; if the two are equal within grain, plain colour (cheaper) is kept. RT2-47's relative outlier stage replaces the clamps of 4 and 32, the emitter term included, and the history box's width is re-measured on the trace grid. The per-tile budget map stays as a map of skips and extra rays.
  - **25c. The realtime cache** (decided, written for RageV after SHaRC's and id Tech 8's structure; SHaRC's shaders are a reference, not a dependency). The key is a power-of-two level, the position quantised at that level, and the normal's octant. A level's cell size is CellPixels pixel footprints at the cell's distance from the camera, CellPixels a quality setting in pixels, so scaling the world shifts levels and nothing else. The entry count and the per-frame update budget are RenderSettings values (proposal: 2^20 entries of 16-32 bytes, 32k updates a frame). Each entry keeps the point and normal of its first hit (so an update re-lights it without re-tracing), the outgoing diffuse light from realtime-owned lights, a sample count, a variance, and last-used and last-updated stamps. Updates pick the cells queried this frame by priority (last used minus last updated, as Lumen does); each re-lights its cell with realtime-owned lights through RT2-12's candidates (one shadow ray) and adds one cosine ray that reads the cache at its own hit, which gives multi-bounce. Each cell accumulates up to a sample cap and restarts on a change test: DDGI's shipped per-probe ratio threshold is the baseline arm (rule 1), and a test against the cell's own noise is adopted only if it is no worse. Least-recently-used eviction once the table's load factor passes 0.5 (the same bound the gate checks). Readers: gather hits, reflection and glass hits through the contract, and RT2-27's rough tier.
  - **25d. One hit-shading contract**, used by gather misses and dynamic hits, cache updates, reflection hits, water and glass hits, and bake-solve hits, with a mask saying which layers it may read. Direct light comes from RT2-12's candidates with one shadow ray, with area lights as areas (RT2-10) and emissive counted once (RT2-11): a glowing mesh linked to a Light (D13) is never a second source at a gather, cache or bake hit, because its Light carries that light. Indirect light: the baked field at Static hits (bounce plus far direct), read through the visibility-aware path, when the mask includes the bake; the realtime cache when it includes the realtime layer. Sky light: the live sky colour times the hit's own sky visibility (the field's alpha lanes inside the bake's cover; the cache's escaped-ray fraction with no bake). Every branch on `RV_IRRADIANCE_FILL` in the hit shader is listed and either removed or kept on purpose; the unoccluded sky (probe slot 0) and the unoccluded flat ambient at bake hits are removed; the solve's shadow rays that see only the static half stay (bake semantics, as Lightmass does). The hit's specular term (GI-09) is decided here, and only here: RT2-21b's term, behind its flag, is measured on the realtime layer and on a test re-bake against the reference, and adopted only if the diff favours it (the owner sees the diff pair). RT2-26's committed re-bake uses whatever this item adopts. The bake shares this shader, so it inherits the frame's approximations too; it is consistent with the frame by construction, not a reference baker.
  - **25e. The bake gathered at hit points** (D18: on at High). The gather's hits also read the baked field (the careful, visibility-aware path), and the result replaces the pixel's own field read wherever the gather has an estimate. Baked lamps then get contact-scale bounce and indirect shadows from a volume-only bake, with no lightmaps. The bake's content is unchanged; only where it is read moves, from the pixel's cell to the hit one bounce away. Gated below, including the showroom's bright bay, where RageV's one real-scene measurement had it losing (1.00 against 0.51 for realtime alone), and the cost priced from today's field read. **Live review:** the garage with the layer always on at High against near-only, shown to the owner; D18 is confirmed from how visible the difference is. If the owner judges the difference not visible, High uses the near-only rule as well, and section 6's realtime-layer lines apply only while a realtime-owned light is near; the 2.0 ms limit (D20) stands for that case. If 25e loses on a gate scene, the owner sees the diff pair and decides, and until then the pixel's own field read stays the default for baked lamps.
  - **25f. Moving objects over the bake** (D19). When a gather ray hits a dynamic surface, it subtracts the light the baked field says arrived from that direction (both the bounce set and the far-direct set, turned back into low-order radiance) and adds the dynamic surface's own light under all lights. So a moving car blocks and bounces baked lamps' light. This widens D4's "hits lit by Realtime lights only" with one exception: dynamic hit surfaces, which are lit by all lights. Differences that ring negative are clamped, and the count of clamped pixels is recorded. Adopted only if the diff images improve on today's AO-only result and it costs at most 0.3 ms (D19).
- **Owner sees.** Realtime lights fill the room around them with bounce light, the way baked lamps do. Metals that bounce light, if the GI-09 gate adopts the hit specular. Baked lamps' bounce gains contact detail at High. A moving car darkens and colours the floor under a baked lamp, if 25f is adopted. The camp's live bounce is cheaper. Probes agree with the traced picture.
- **Touches.** `rtgi_trace.rvshader` (155-688), `pbr_fragment.glsl` (`TraceSurface`, `ShadeTraced`, 2615-3327), `irradiance_fill.rvshader` (229, 279, 357-361), `signal_upsample.rvshader`, `reflection_trace.rvshader`, `ReflectionProbe.cpp`, `RuntimeIrradianceField.*` (replaced by the cache), `Renderer3D.cpp` (2940-3000, 7869-7938), `FrameGraphBuilder.cpp` (363-383, 866-913), `Scene.cpp` (1887-1912, 3579-3600; the GI source switch, the field, cache and probe ownership), `PostSettings.h`, `RenderSettings.h`.
- **Scope.** Rewrite. **Depends on.** RT2-2 (2b, 2e and the bright-bay camera), RT2-6 (GI-10, gi-s2), RT2-10, RT2-11, RT2-12, RT2-20, RT2-21, RT2-21b, RT2-47. **Risk.** Medium: every light must be counted in exactly one layer, including Hybrid lights, whose near direct light is live while their bounce and far direct light are baked.
- **Exit gate.**
  - `--pass-timings`: at lower settings, the realtime layer absent in a fully baked view with no realtime-owned influence, present otherwise; at High, present in every ray-traced view.
  - The garage with its Realtime lights on, converged with `--aa=none`: the baked layer plus the realtime layer against `--reference-render` with every light: near zero, or favouring baked (the owner's bar). A probe captured with a Realtime light on no longer leaks that light into baked frames.
  - RT-22's moving cube past a baked lamp: its bounce follows it (`emitter_lag.py`), and no region is brighter than the reference (nothing counted twice).
  - The camp (all Realtime): distance to the reference renderer within grain, and no worse than today's 4-ray gather.
  - The leak scenes in `check_gi.py` stay at 0.000, including the non-black-sky and non-zero-ambient variants (RT2-2e); its bands are updated only as RT2-26 changes the model.
  - One wall seen four ways (directly, in a mirror, through a bounce, in the bake) against the reference: all four agree within grain. GI-09 (decided here): with and without hit specular against the reference on the garage ceiling, on the realtime layer and on a test re-bake; adopted only if the diff favours it.
  - The SH arm: in normal-mapped regions, the per-pixel error against the reference lower than plain colour, at no more than +0.3 ms.
  - The cache: against full shading at every gather hit, a converged diff no worse and the gather at least 30% cheaper at native; its VRAM within the owner's setting (RT2-1's counters); at the owner's swing on the garage and Headland, a load factor at most 0.5 and no evictions of cells queried this frame; the bridge at kilometre distance, no rings at level changes in the diff against the reference; frames to converge after a Realtime light switches (`emitter_lag.py`, `--frame-time` pinned to 0.000001) no worse than the pixel signal's, reported for both change-test arms.
  - 25e: on the garage and the RT2-2e bright-sky room, converged with `--aa=none`, the share of pixels off the reference by more than grain falls from today's 1.9% contact bleed; the showroom's bright bay no worse than realtime alone; the leak fixtures at 0.000; the field read at hits adds at most 0.3 ms on top of the gather, measured on today's L2 read.
  - 25f: RT-22's moving cube and the garage car through a baked lamp's pool, converged with `--aa=none` against the reference with the object in place: the diff around the object better than the AO-only result, no region brighter than the reference, the clamped-pixel count recorded, and at most 0.3 ms at native while a dynamic object is near (D19).
  - The owner's swing on the gi-light debug view: `speckle_count.py` dots over 32 levels no worse than the same view captured from a run of the gather at 16 rays per traced pixel at the same swing (the 16-ray reference's method applied to this signal; the 16-ray reference's own 494 is a whole-frame number of the reflection chain, not this view's bar).
  - A linked fixture bounces once (D13): on RT2-11's linked fixture scene, the realtime layer against `--reference-render` with the link rule, within grain (the mesh adds no second bounce).
  - Cost, A,B,B,A with `--pass-timings` at 2560x1600 on the garage, Headland and the camp: gather, cache update, denoise, upsample and 25e's reads together at most 2.0 ms, aiming for 1.5 (D20); the owner sets the final bar from 25b's first measurement.
- **Size.** 14-18 sessions (25a-25d 11-14, 25e 1-2, 25f 2).
- **Closes.** GI-02, GI-05, GI-07, GI-11, ISS-10; the Realtime-light half of GI-01; the bounce half of gi-s1 (D13); the traced-capture half of GI-04 (with RT2-27's conditional step); the hit part of GI-06; the decision of GI-09 (the term from RT2-21b; RT2-26 bakes it); the comparison's claims C-C1 to C-C5, C-C8, C-C11 and C-C17 and findings C-N1, C-N3, C-N5 to C-N10 (section 8).

### RT2-26 · One rule for indirect light, and a bake that knows when it is stale

- **In plain words.** The pixel adds indirect light from overlapping sources (a probe's light on top of the bounce), AO darkens light it should not touch, the bake allows at most 8 hand-placed boxes, and a bake goes silently wrong when a wall is moved. This item writes one rule for combining indirect light, replaces the hand-placed boxes with small bricks placed automatically wherever there is Static geometry, and makes the editor say "the bake is stale" and name the objects that changed.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** The Hybrid lamps'
> baked-to-live transition bands (a fixed band in metres, in three places; general form: the field's
> own cell spacing, the narrowest transition it can hold); the bake settling, writing and giving up
> after fixed frame counts (general form: on events -- the solve for this lighting hash finished
> unchanged); the probe "moved" tolerance; volume padding; BC6H encoding by a fixed box fit (banding);
> the stamp's epsilon; the CPU probe irradiance's sample count; the 8-volume cap (this item); and the AO
> signal: a fixed blur reach and no measured-change record, so an occluder's change is caught only by
> motion.

> **Changed by the comparison (2026-09-25).** Automatically placed sparse bricks of 4x4x4 cells (what Unity's
> APV and Unreal's volumetric lightmaps ship, rule 1) replace the hand-placed volumes and the 8-volume cap;
> authored volumes become optional overrides; blending comes from the bricks' borders. Dilation is the
> first leak arm. Moving cells out of walls (relocation) becomes a measured arm with an adoption rule,
> no longer a promised fix: RageV measured it on 2026-08-26 fixing the point-light sealed room (8.4 to 0.2
> levels) and worsening the sun-lit ones (3.2 to 15.6), and Snowdrop shipped Avatar without it. Bake hits'
> sky is occluded (with RT2-25d). The stamp becomes a per-object and per-light hash list, and a stale bake
> warns rather than being refused. The cell format (two separate L2 sets, bounce and baked-lamp direct)
> is kept only if it measures better than one merged L2 set, the way Unreal's volumetric lightmaps store
> it: the verification found the separate set is a different trade (twice the memory and fetches), not a
> lead. AO's measured change moved to RT2-47.

- **What is wrong now.**
  - The lit pixel adds a probe-or-sky term beside the traced bounce, and the two overlap: a local probe's irradiance already contains the room's light. The field's sky fraction is applied only where the bounce gave no answer. AO, which reaches 0.4 to 0.5 m, multiplies the traced bounce and the stored direct light.
  - The bake's stamp covers the volume layout and a light and environment hash only; it cannot see a moved Static object or an edited material (C-C10), so a stale bake is silently wrong until someone re-bakes.
  - At most 8 volumes can be placed by hand, and the bridge uses all 8; Static surfaces outside every box get no baked bounce at all.
  - The field stores fully baked lamps' direct light at cell resolution, where AO darkens it (it does not darken the same lamp's live share).
  - Nested volumes do not blend, the rt and ss files are identical, and there are eleven indirect-light systems.
  - A sealed room still leaks 3.4 levels where cells are coarser than the wall.
  - The code comments describe an older field (first-order SH, Chebyshev tiles, "tile 9 carries the visibility bits"; `IrradianceVolume.h:25-44`, `pbr_fragment.glsl:1645-1653`, `1660-1666`, `1702`, `2033-2041`; C-C7, C-N2, C-N4), and so does `BAKING-ROADMAP.md`.
- **What changes.**
  - **One composition rule** (in RT2-22's composition): ambient = diffuse albedo x [(flat ambient + sky light) x sky visibility x AO + baked bounce x AO + the realtime layer] + diffuse albedo x the baked far direct light. Sky visibility is the field's (the bake's cover) or, with no bake, carried inside the gather. AO multiplies stored light only (the baked bounce and the sky), never the live gather and never direct light, stored or live. Probe irradiance is never a diffuse term in RT mode. With RT2-25e on, the baked bounce comes from the gather's hits wherever the gather has an estimate. One indirect scale for every source (GI-10).
  - Baked lights stay baked (D4): their bounce, and the far direct light of FullBake and HybridFullBake lamps, keep coming from the bake at almost no cost (subject to RT2-12's D12 arm).
  - **The stale bake.** The bake writes a list of input hashes: per Static object its transform, mesh asset id and version, and material parameter hash; per baked light and the environment, today's lighting-hash fields. The editor compares them and lists the changed objects and lights as "bake stale (N)". A stale object keeps the bake bound and warns; a changed baked light still refuses the bake, with RT2-6's validity flag so that a refused field owns no light. The settle, write and give-up rules move from frame counts to events (the solve for this lighting hash finished unchanged). The stamp is compared exactly, or in units of float precision.
  - **Sparse bricks.** Bricks of 4x4x4 cells placed automatically over every Static surface; a brick's level from its distance to the nearest surface relative to the cell size; the total capped by a memory-budget RenderSettings value. An indirection texture holds a one-cell border per brick, so blending across bricks and levels is trilinear by construction. The 8-volume cap and the resident atlas go; authored volumes stay valid as density or extent overrides (rule 5: the scene is untouched). One file per bake. Bricks stay resident until RT2-40 streams them. The Hybrid transition band becomes the field's cell spacing at the lamp.
  - **Leaks.** Dead cells are filled from their live neighbours (dilation, as APV does) as the first arm. Relocation is a second arm, including the anchor and alive-flag rework the solve's own comment asks for (`irradiance_fill.rvshader:138-148`); it is adopted only if every leak fixture, the sun-lit ones included, is at or below today.
  - **The cell format.** Kept (L2 bounce with sky visibility, L2 baked-lamp direct light, the visibility and alive tile) unless one merged L2 set, measured on the fully baked showroom where the ambient-cube deficit was found, is as good for half the memory and fetches.
  - The raster recipe reads the same format. The reference arms move behind engine flags; RT2-30 deletes them. The settings documentation, the stale comments and `BAKING-ROADMAP.md` are rewritten.
  - The committed re-bake uses the hit specular RT2-25 adopted (GI-09); it is not decided again here.
  - A glowing mesh linked to a Light (D13) is kept out of the bake as a second source; its Light carries the bake share (RT2-11's link, through RT2-25d's contract).
- **Owner sees.** The baked and realtime layers agree where they meet. A stale bake is flagged in the editor, naming what changed, instead of being silently wrong. AO no longer darkens baked lamps' direct light. Every Static surface gets baked bounce, with no seams where boxes used to nest. Once the owner has linked the garage's fittings and re-baked, a slightly dimmer floor where the bake counted the bars twice.
- **Touches.** `pbr_fragment.glsl` (295-307, 1645-2329, 5611-5925), `IrradianceVolume.*`, `BakedLighting.cpp` and `BakedLighting.h` (48-72, 157, 332), `Scene.cpp` (the bake orchestration and its stamp, 3823-4259, 4624), `irradiance_fill.rvshader`, `Renderer3D.h` (86), `scene_block.glsl` (110), `LightGrid.cpp` (231), `check_gi.py`, the editor's bake panel, `RenderSettings.h`, `PostSettings.h`, `docs/BAKING-ROADMAP.md`.
- **Scope.** Refactor. **Depends on.** RT2-25, RT2-19 (the memory budget), RT2-4 (the solve twin), RT2-6 (gi-s2, GI-10), RT2-22. **Risk.** Medium.
- **Exit gate.**
  - The baked layer plus the realtime layer against the reference renderer, diffed on the garage, the camp, the bridge cameras and the bright-sky scene (realtime only, then bake plus realtime): near zero, or favouring baked.
  - `check_gi.py` updated for the new rule (its gi_skylit band re-derived from the reference) and green.
  - The leak scenes at 0.000, including the sun-lit fixtures, a wall thinner than a cell, and the non-black-sky and non-zero-ambient variants; relocation adopted only if every fixture is at or below today.
  - Contact-scale bleeding: the share of pixels off the reference by more than the grain falls below today's 1.9%.
  - Moving or recolouring a Static object in the editor raises the warning and names it; moving a dynamic one does not. Editing a baked light's colour without re-baking lights that lamp live (gi-s2), diffed against the frame before the edit.
  - A diff where AO is below 1 shows baked lamps' direct light no longer darkened.
  - The double count D13 removes from the bake (moved here from RT2-11): on a copy of the garage made by the harness, whose tube fittings the harness links (never saved over the scene or committed, rule 5), the bake with the linked meshes kept out against the bake with them in: the field-lit diff recorded (the size of the double count), and the linked bake against `--reference-render` with the link rule, near zero or favouring baked.
  - The bridge: every Static surface inside a live brick (coverage report 100%), field memory at most 91.8 MB at an equal or better diff, and no seams at brick or level edges in the diff image.
  - The lit pass's (then the composition's) baked read at most 0.3 ms at native. Load time with an unchanged bake does not regress. A,B,B,A. Bake time and disk size recorded.
- **Size.** 8-11 sessions, in named steps: the composition rule; the stamp and the editor warning; sparse bricks, dilation and the relocation arm; the cell-format arm; the raster recipe and the docs.
- **Closes.** GI-01, GI-03, GI-12 (sparse bricks included), GI-13, GI-14, ISS-37; the bake half of gi-s1 (D13); the field-lookup part of ISS-16; the comparison's claims C-C6, C-C7, C-C10 and findings C-N2 and C-N4 (section 8).

### RT2-48 · A lighting cache for particles and see-through surfaces (new, 2026-09-25)

- **In plain words.** Particles are unlit today, and the glass panes behind the nearest one light themselves with an old per-light loop whose noise only TAA averages. The top engines light both from a small grid of light around the camera. This item builds that grid: cells only where lit see-through things are drawn, each holding the light arriving there and its main direction, lit from the same light tree as everything else, with its own history. It costs nothing when no such surface is in view.

> **New, from the comparison (2026-09-25).** The shipped baseline for lighting particles and deeper layers
> (Unreal's translucency lighting volume with MegaLights' sampling, and id Tech 8's froxel volume), and
> the replacement RT2-13's second half needs before the old loop can be deleted from the transparent
> variants. MegaLights tried reusing its fog froxels for translucency and rejected it (the grid's
> structure shows when the camera moves, and reprojection smears), so this is a world-space grid, not
> froxels. **Rejected from the comparison's proposal:** merging this grid with RT2-25's cache. That proposal
> was written against the old RT2-25's optional camera-centred box; RT2-25's cache is now a spatial hash
> of light leaving surfaces (keyed by a surface's normal), which cannot answer what light arrives at a
> point in the air, so the two stay separate structures. This one reads RT2-25's cache for its bounce.

- **What is wrong now.**
  - Particles are unlit by design (texture x vertex colour), write zero motion, and GPU simulation is off by default (G-N7).
  - The panes behind the nearest are lit by the in-shader loop (S4 outside the field, one soft-shadow ray per casting lamp inside it), whose random draws move only with TAA.
- **What changes.**
  - A camera-centred world-space clipmap: cascades double in cell size, and the base cell size comes from the projection's pixel angle, so a cell spans a fixed number of pixels at its cascade's inner distance. Each cell holds second-order spherical harmonics of RGB irradiance, plus a dominant light direction and colour (for the glass's specular, as MegaLights does).
  - Only cells marked by a low-resolution raster of this frame's lit translucent draws are lit (Lumen's marking). Direct light: K picks per marked cell from RT2-12's light BVH, one shadow ray each, transmittance-aware once RT2-31 lands. Bounce: read from the stored layers (the bake and RT2-25's cache).
  - Its own temporal accumulation in world space, with measured change taken from the light and instance change lists, plus a 3x3x3 spatial filter; none of it depends on jitter.
  - The pass is skipped when nothing lit and see-through is in view.
  - When the owner resumes WR-11, the froxel fog shares this grid's sampling and shadow rays, as MegaLights' hybrid does (about 25% saved).
  - A per-particle light accumulator (each particle lit at its centre, averaged in its own GPU state slot) is an arm only if this grid shows cell structure on the camp's smoke; it would cover GPU-simulated emitters only.
- **Owner sees.** Nothing until RT2-31 and RT2-33 use it: then lit smoke and particles, and back panes lit like everything else.
- **Touches.** A new cache pass and its marking raster, `ParticleRenderer.cpp`, `particle*.rvshader` (the read), `pbr_fragment.glsl` (the transparent variants' read), `FrameGraphBuilder.cpp`.
- **Scope.** New capability. **Depends on.** RT2-12, RT2-15, RT2-25. **Risk.** Medium: world-space grids show their cells if too coarse; the gate checks it.
- **Exit gate.**
  - The camp's smoke and the garage's back panes diffed against the reference renderer: no cell structure at the owner's swing.
  - `emitter_lag.py` no worse than the opaque direct signal.
  - Converges under `--aa=none`.
  - At most 0.3 ms at 2560x1600 when active and 0 when no lit see-through draw is in view (`--pass-timings`); VRAM reported.
- **Size.** 4-6 sessions.
- **Closes.** The lighting half of MAT-16 (with RT2-33); the comparison's finding G-N7 (section 8).

### RT2-27 · Reflections tiered by roughness; rough reflections from stored light

- **In plain words.** Every reflective pixel today gets a full-resolution ray and the full clean-up chain, even on a rough, settled surface; above roughness 0.6 the reflection comes from a probe that is a third too bright. This item spends rays by how wide the reflection's cone is (a sharp mirror gets a ray per pixel, a rough surface far fewer), lets a wide cone read stored light where it lands instead of lighting its hit again, and removes the probe cross-fade, so there is no brightness seam across roughness. It is section 6's largest single saving.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** The probe parallax
> proxy takes its radius from the blend radius instead of the capture; 16 probe slots. General form:
> the proxy from the capture, slots from the scene's probe count within a memory budget.

> **Changed by the comparison (2026-09-25).** The three fixed tiers become one estimator. RT2-23's step 23d
> (rays traded within a budget) is merged here, so one classification and one ray-rate rule serve
> reflections (its audit rows moved here in section 10). Per 16x16 tile the ray rate follows the lobe's
> footprint, quantised to 1, 1/2, 1/4 or 1/16 for coherent dispatch. At every roughness a ray that hits
> something whose cone footprint is wider than the stored-light cell there reads that cell instead of
> lighting the hit (SHaRC's rule). The probe cross-fade and the 0.6 cutoff leave RT mode, and the gloss
> window survives only as a cost cap (a global setting). RT2-25's former step (26d in RT-SERIES-2's labels; probes captured through
> the traced world) arrives here as a conditional step. Verification corrections: the store must answer
> a glossy lobe, and the bake field stores irradiance only, so the rough tier reads RT2-25's cache and the
> bake through the hit contract at its hit (a store of light leaving surfaces, read at hits), with
> SHaRC's rule applied as written (at hit points, with its minimum-segment rule), not as a mid-air cut;
> Snowdrop is not discontinuous in roughness (it fades and dithers), so the argument against it is its
> metre-tuned ray lengths and its darkening bias; the footprint rate is an argument, not a result, so it
> is a measured candidate with a gate for blotchy, correlated resolve noise at 1/4 and 1/16.

- **What is wrong now.**
  - Every reflection pixel inside the gloss window is traced at full resolution and gets the full resolve and three blurs, even when glossy and settled. In the garage that is about 12.7 ms at 1600x900, with the lamp pool.
  - RT-9 only ever adds rays, runs only while an instance moves, and reads last frame's tile at the same screen position, never reprojected (B-N8), so a moving object gets another tile's rays; the per-texel form was measured as giving no picture gain.
  - Above roughness 0.6 the reflection comes only from the baked probe, which is about a third brighter than the trace.
  - Between 0.25 and 0.6, two estimates that disagree are cross-faded.
  - The reflection rays reach a fixed 10 km.
- **What changes.**
  - First arm: measure how much of each garage, Headland and camp frame falls in each rate class, per tile, and report it before anything is built.
  - One classification per 16x16 tile, read by every reflection pass: the roughness classification RT2-23's step 23e built on RT2-20b's mechanism gains the ray-rate class here (the mirror-rate tiles RT2-23f reads). Tiles with no glossy surface or only sky are skipped.
  - The ray rate: rays per texel = min(1, T / (pi rho^2)), where rho is the resolve disc's radius in texels, taken from last frame's reprojected image distance, and T the resolve's tap count, quantised to 1, 1/2, 1/4 or 1/16, tiles compacted in Z-order. Rays are traded between tiles within one fixed average budget (a global setting). Texels whose history is younger than the settle length get extra rays inside that budget, including texels made young by camera motion, and keep them after a stop (RT2-0's arm D, structurally). The rate map is read at the reprojected position. Reduced-rate tiles reconstruct with RT-3.1's guided downsample and joint upsample.
  - **Stored light at wide footprints.** At every roughness, a ray that hits something whose cone footprint exceeds the stored-light cell there takes the hit's light from RT2-25's cache and the bake through the hit contract, instead of lighting the hit; a near-mirror ray almost never does. The rough tier traces one GGX ray at reduced rate and reads stored light at its hit.
  - In RT mode the probe cross-fade and the 0.6 cutoff are removed; the gloss window survives only as a cost cap (a global setting).
  - The blurs and the history fix run only on the noisy or young tile lists; compute with Z-order compaction.
  - **Probes (conditional step, formerly RT2-25's step 26d in RT-SERIES-2's labels).** Probes are captured through the ray-traced world by the hit contract only if the rough tier still reads a probe anywhere in RT mode; otherwise they serve only off-screen hit specular and the raster recipe. The probe rows of audit 2 (the proxy radius, the slot count) stay here.
  - Whole-scene rays reach the scene's own bounds (RT2-41).
- **Owner sees.** Section 6's largest single saving. Rough surfaces stop taking their reflection from a probe that is a third too bright. No brightness seam across roughness.
- **Touches.** The reflection shaders, a classification pass, `FrameGraphBuilder.cpp` (266-273, 2356-2472), `reflection_budget.rvshader` (95-162), `reflection_trace.rvshader` (692-693, 777-794), `pbr_fragment.glsl` (the gloss window, 5985-5989, 6204-6241; the probe proxy, 2452), `ProbeArray.h` (28), `ReflectionProbe.cpp`.
- **Scope.** Refactor. **Depends on.** RT2-20 (compute passes and ray lists), RT2-23 (the denoiser and 23e's tile classification), RT2-25 (the stored light rays read). **Risk.** Medium: RT-3.1 measured half resolution darkening the bridge's cables by 0.58 levels.
- **Exit gate.**
  - The first arm's coverage per rate class per tile, reported before anything is built.
  - The roughness-ramp scene (RT2-2h) against the reference renderer: no step larger than 1 display level between adjacent roughness bands from 0.05 to 1.0. The garage and bridge regions against the reference: bias no worse than v1.
  - At 1/4 and 1/16 rate: no blotchy, correlated noise in the resolve beyond v1's (a per-pixel diff of the parked frame's spatial spectrum against v1's full-rate picture, judged as a diff image).
  - The owner's swing (`spin_measure.py --swing=45 --rate=3`): `speckle_count.py` motion dots and grain and edges no worse than v1 at full rate. The parked garage within the parked noise floor of v1's full-rate picture (`parked_stats.py`).
  - The bridge's cable band no worse than v1 (the 0.58-level darkening must not return).
  - Cost at 2560x1600, A,B,B,A: garage reflections at or below the section 6 line in total, split as proposals of classification 0.2, trace with hit shading 1.5, resolve 0.6, temporal 0.5, history fix, blur and stabilisation 0.8 ms (3.6 in all); Headland's reflections at or below their line. The owner sets the bar from the coverage arm. (Anchors: Snowdrop's reflections cost 2.13 ms at 2160p on an RTX 4080, traced at quarter resolution; Lumen's Epic reflections 4.62 ms at 1080p on PS5.)
  - If traced capture is built: the probe against the reference at the probe's position, within grain.
- **Size.** 6-8 sessions.
- **Closes.** REFL-07 (moved here with 23d), REFL-08, reflections-s2; the fallback part of ISS-9; the traced-capture half of GI-04 (conditional); the comparison's claim B-C9 and findings B-N3, B-N5 and B-N8 (section 8).

### RT2-23f · Reflection hits lit deterministically (split from RT2-23)

- **In plain words.** Today a reflection ray that hits something lights that hit with one randomly chosen lamp, so even a perfect mirror is grainy. This item lights hits on mirror-like tiles with every lamp that reaches them, one shadow ray each, and reuses last frame's finished picture where a hit is on screen and the change of view cannot matter. It was step 23f of RT2-23 and became its own item because it needs RT2-27's ray-rate tiers and RT2-25's hit-shading contract, which land after RT2-23. Mirrors and chrome stop sparkling when parked.

> **Split from RT2-23 after the second critic's review (2026-09-25).** Step 23f used RT2-27's mirror-rate tiles and RT2-25's hit-shading contract, both later in M4, and RT2-27
> itself depends on RT2-23, so the order was circular. It runs after RT2-27; its design is unchanged.

- **What is wrong now.** Each reflection hit keeps one light by weighted reservoir with one shadow ray, so even a roughness-0 ray's hit shading changes every frame where two or more lights reach it; hits read mip 0 (B-C1, B-C10).
- **What changes.** A hit that reprojects into last frame's view, with depth and normal there matching within the texel's footprint, takes last frame's lit colour before TAA at that point, where the hit's own specular share is small against the view change (a split-sum test at the hit from its roughness and F0), as Snowdrop and AMD's GI-1.1 do. Otherwise the hit uses RT2-25's hit-shading contract. On mirror-rate tiles (RT2-27) the hit's lights are shaded deterministically: every light RT2-12's light structure returns as reaching the hit, one shadow ray each (today's path with `--hit-light-sampling=off`); on shared-rate tiles the one-light pick stays, because the lobe's own sampling noise dominates there. Texture mips at hits come from RT2-21d's ray cone. Hit shading moves to its own pass over a compacted hit list only on a measured gain (RT2-20c).
- **Owner sees.** Mirror-like surfaces that stop sparkling when parked; chrome poles and the car window as steady as the surfaces around them.
- **Touches.** `pbr_fragment.glsl` (the hit shading, 3098-3171), `reflection_trace.rvshader` (the hit-shading call, 343-589), the hit-list pass only if RT2-20c adopts it.
- **Scope.** Refactor. **Depends on.** RT2-23 (the denoiser the hits feed), RT2-12 (the light structure), RT2-21 (21d's ray cones), RT2-25 (25d's hit-shading contract), RT2-27 (the mirror-rate tiles); RT2-20c only for the hit-list trial. **Risk.** Medium: reusing the screen can hand stale light to a hit after a disocclusion; the gate checks it.
- **Exit gate.**
  - Parked, with the reflection history off: grain on the chrome poles and the car window at or below the parked noise floor of raster surfaces on mirror-rate tiles.
  - Region bias against the reference no worse than v1 (RT2-23's denoiser before this item), including a mirror seen in a mirror.
  - `watch_arm.py car 20` and the moving glowing cube: no stale screen-reused light after a disocclusion (`ghost_map.py` shows no trail).
  - Trace plus hit time no worse where the deterministic walk costs more than the screen reuse saves, A,B,B,A on the garage and Headland; the bridge's three cameras (diffs, glint flicker).
- **Size.** 2-3 sessions.
- **Closes.** The comparison's claims B-C1 and B-C10 (the mip half, with RT2-21d) (section 8).

### RT2-23g · One specular signal: each lamp's shine counted once (split from RT2-23)

- **In plain words.** Today a lamp's shine on a glossy floor is computed by two passes: DirectTrace draws it as a highlight, and the reflection ray sees the glowing lamp. Since RT2-11 the two shares are weighed so the lamp is counted once, but they still live in two signals, and the highlight keeps four stale frames when the view turns. This item samples each lamp's shine once, from the light tree, weighs it against the reflection ray by the standard weighting, and cleans both halves up as one signal. It was step 23g of RT2-23 and became its own item because it needs the direct light's new denoiser (RT2-24's step 1) first.

> **Split from RT2-23 after the second critic's review (2026-09-25).** Step 23g had to run after RT2-24's step 1, which comes after RT2-23 in M4. It runs after RT2-27 and
> RT2-23f; its design is unchanged.

- **What is wrong now.**
  - A lamp's shine is computed twice: DirectTrace's analytic highlight (with a four-frame memory reprojected by the surface) and the reflection ray striking the glowing mesh, with nothing weighing one against the other (B-C2). RT2-11 (M2) adds the power-heuristic shares for linked fixtures, but the two shares stay in two signals.
  - The lamp selection is done twice, and the 8-neighbour lamp pool (+2.1 ms) duplicates what a shared light structure would give once (B-C6, B-N6).
- **What changes.** A lamp's shine at the reflector is sampled once, from RT2-12's light structure, which replaces the reflection's private emitter scoring and the 8-neighbour lamp pool. It is weighted by the power heuristic against the reflection's lobe ray (whose pdf is analytic); the lobe ray that strikes an emitter keeps the complementary weight, using the light structure's pdf for that point. For a Light linked to its glowing mesh (D13) the light sample is taken on the mesh, so both halves see the same surface; an unlinked analytic Light cannot be struck, so its direct sample keeps weight one. Both halves go into one demodulated specular signal, denoised by RT2-23's steps 23b and 23c with virtual-image reprojection, tracking by the lobe ray's hit distance as NRD advises. The diffuse half of direct light stays with RT2-24. DirectTrace's analytic specular and its four-frame memory are retired. If this item misses its gates, the direct pass's weighted share stays in RT2-24's accumulator: the fixture is still counted once, because the weights, not the denoiser, do the counting.
- **Owner sees.** Each lamp's shine on the wet floor drawn once, by its real shape, with no smear from the highlight's four-frame memory.
- **Touches.** `direct_trace.rvshader` (the specular half), `reflection_trace.rvshader` (the emitter scoring), `reflection_resolve.rvshader` (the lamp pool, 447-456), `include/reflection_emitters.glsl`, `FrameGraphBuilder.cpp` and `Renderer3D.cpp` (the specular signal's descriptor).
- **Scope.** Refactor. **Depends on.** RT2-11 (emitters on their real surfaces; the D13 link and its M2 weights), RT2-12 (the light structure's pdf), RT2-23 (steps 23b and 23c denoise the signal), RT2-24 (step 1, whose specular half this item retires). **Risk.** Medium.
- **Exit gate.**
  - Under the tubes, the direct-specular-only arm plus the reflection-emitter-only arm equals the full picture (diff near zero), and the full picture against the reference renderer is near zero or favours baked.
  - Owner-speed speckle at or below 513/85 and pole edges at or below 9.51; `emitter_lag.py`'s reflection and floor shares no worse.
  - Cost below today's DirectTrace specular plus lamp pool, A,B,B,A at native; the bridge's three cameras (diffs, glint flicker).
- **Size.** 1-2 sessions.
- **Closes.** The reflection side of D13's single count; the comparison's claims B-C2 and B-C6 and finding B-N6 (section 8).

### RT2-28 · The final temporal pass does anti-aliasing only

- **In plain words.** TAA has become the place where every signal's leftover problems are patched: it takes twelve inputs, some specific to the sea or to reflections, and it filters the reflection a second time. This item turns it into a pass that only smooths geometry edges, built the way the top engines build a good TAA, with every lighting signal arriving already clean. The reflection is added after it, which is the only arrangement where the reflection provably does not lean on TAA; if that is still too grainy after denoiser v2, the work stays on the reflection's own denoiser (D23).

> **Carried from RT-24 (2026-09-25).** RT-24's leftover speckle and ghosting on reflective pixels
> belong here (D17): the filter fetches a shiny pixel's history by the reflection's motion, so its
> depth test decides "reflection moved too far" rather than "same surface" -- loose, ghosts; strict,
> the pixel restarts and speckles. Adding the reflection after this pass (28's design) removes the
> choice. NRD's same-surface test is already built for step 28a: `--taa-plane-test=on`, off by default
> (it measured the same as the 2% test switched off while reflections still pass through the filter).
> Also found: the filter's sky clause tests `depth >= 1.0`, but the depth is reverse-Z (sky 0), so it
> never fires; sky crossings are caught by the object id instead. Fix it with the depth test.

> **Part done early, in RT-24 (2026-09-24):** the frame filter fetches every pixel's history by the
> surface's own motion; the brightness-chosen reflection motion is gone (the reflection keeps its own
> memory). Measured on the garage's slow orbit: pole ghost 10.5 -> 7.7 at 16 rays, parked flicker
> unchanged. Separately, its no-history pixels (first frame, off screen, another surface) are now the
> current frame rebuilt at the pixel centre instead of the raw jittered sample.

> **Added from RT-24's hard-coded-values audit (2026-09-24; section 10 lists every value).** Three fixed values in the
> frame filter. (1) "Same surface" allows a 2% depth difference (`kDepthTolerance`), which on a thin
> chrome pole or a grazing floor passes or fails by on-screen size: edge jitter. General form: predict
> the depth this point had last frame (its world point through last frame's camera) and allow one
> texel's worth of depth change on this surface. (2) "Still" is a fixed 0.003 texel of motion
> (`kStillWithinTexels`): at speed, rounding flips pixels between the still and moving handling. General
> form: the motion lane's own precision, as in RT2-6. (3) The brightness curve used to average
> (`Compress`/`Expand`, 1/(1+Y)) is tied to absolute brightness rather than the exposure the tone curve
> will use. General form: weight on the exposed value, so the knee sits at display white.
> (4) The material widening listed below is a factor of 8 picked from one measurement
> (`kStableWiden`). General form: extra room only for the part of the pixel's light that does not
> change with the view, which the composite already separates. (5) The box's own flicker memory is 16
> frames (`kMomentFrames`). General form: two jitter cycles, from the project's jitter setting.
> Checked and fine: the 26-degree normal test (world-space normals do not change with speed or scale).

> **Owner's ruling D23 (2026-09-25).** If the reflection added after TAA is still too grainy after
> denoiser v2: keep working on the reflection's own denoiser; this item stays open, and the reflection
> is never added before TAA (rule 3 holds).

> **Changed by the comparison (2026-09-25).** "Measured, the owner picks" is replaced by one decided design
> in five steps. The primary talks show that Lumen and MegaLights both leave their last noise to their
> final temporal pass, so rule 3 needs a structure the industry does not ship: the reflection added after
> the pass. The rest is the shipped baseline (a variance clip, brightness weighting on exposed values,
> dilated velocity) or the audits' general forms. Verification corrections: predicting last frame's depth
> is already what NRD ships (with a tolerance scaled by the frustum's size and the facing angle), so NRD's
> form is built first and the one-texel form is measured against it; the reflection's weight rides
> through the pass in its own lane, so "the reflection region's diff with the frame filter's memory off is
> zero" can hold only away from the weight's own edges, which the pass legitimately anti-aliases; every
> after-the-resolve arm measured so far was grainier (RT-6.1: 8.86 against 5.98 frame-to-frame change;
> RT-15's moving layer, removed for grain), which is the risk the grain gate must beat; the change check
> costs about 0.94 ms moving at the owner's Quality preset, not 0.56.

- **What is wrong now.**
  - With rays on, TAA is forced on, and it has become the place where signal and surface problems get patched. It takes 12 inputs, including the sea's motion, two change maps and the see-through revealage.
  - The reflection passes through two memories in a row: it is composited before TAA, and TAA fetches it again by the surface's motion (D-C1). (Since RT-24 the motion is the surface's, no longer chosen by comparing brightness.)
  - A changed reflection restarts the whole pixel, however little of the pixel the reflection is: TAA takes the larger of the direct and reflection change maps (D-N6).
  - The box is the min/max of the 3x3, widened up to 8x on rough dielectrics (`kStableWiden`), which assumes rough dielectrics look the same from every angle (D-C5); the same-surface subset needs at least 3 of 8 neighbours (`kLeastSameNeighbours`).
  - "Same surface" is a fixed 2% of raw reverse-Z depth; the sky test is dead under reverse-Z (sky clears to 0), so sky crossings are refused under the object-id reason instead (D-C6).
  - Velocity is read at the pixel, with no nearest-depth dilation (D-C7).
  - Brightness weighting works on absolute radiance (D-C8).
  - It keeps a 32-byte copy of the G-buffer (the TAA guide), 64 bytes a pixel of history in all, about 262 MB at 2560x1600 (D-C17).
  - (The place-based flicker rules behind the water regression are already replaced by RT2-8's behaviour-based floor.)
- **What changes.** Five named steps, each ending in a report:
  - **28a. Disocclusion by predicted previous depth.** The depth this point had last frame (from RT2-21a's motion-depth lane) against the stored previous depth at the reprojected texel. NRD's shipped form first (rule 1): a tolerance of 1% of the frustum's size over the facing term. Then the one-texel form (one texel's depth span on this surface plus the depth format's own step) measured against it, and adopted only if it wins the gates. The exact id test, the 3x3 neighbour search and JitterCrossing stay. A nearest-depth dilation of motion and depth (FSR2's) is an arm for edges under motion. RT2-0 may run this step's arms early, for attribution only.
  - **28b. The box.** A variance clip in YCoCg over the same-surface taps (mean plus or minus 1 spread, clipped toward the mean; the 1 is Salvi's stated convention, flagged to the owner). Its only widening is RT2-8's behaviour floor; `kStableWiden` is deleted. `Compress` and `Expand` on exposure-scaled luminance (last frame's exposure). Stillness from the motion lane's own precision (RT2-6). The feedback values 0.9 and 0.98 stay the owner's settings, with the frame cap derived from them: ceil(1 / (1 - still feedback)). Kept: the current frame rebuilt at the pixel centre, the Catmull-Rom history fetch, the stochastic half-float rounding.
  - **28c. The reflection added after the pass.** `reflection_composite` moves after the final temporal pass: the composition writes the reflection's weight (split-sum x occlusion x window, full colour) into its own RGB lane, which the pass anti-aliases like colour; after it, colour += weight x this frame's accumulated reflection at this texel (the reflection is already on the unjittered grid). The weight is multiplied by the fog's transmittance once fog moves into the composition (RT2-33). With a render scale below 1, the reflection is upsampled on its own guide. The reflection motion lane and the moving-layer composite are deleted. If the reflection is too grainy here after v2, the work goes back to RT2-23's denoiser (D23).
  - **28d. One generic history-invalid lane**, written by the composition: the sum over signals of each signal's changed share times that signal's share of the pixel's luminance. It replaces the two change maps, the material lane and the revealage. The sea's motion input becomes a generic motion lane per see-through layer (until RT2-31 gives the layer its own accumulators, the sea still needs its motion here). The identity lanes are read from the G-buffer's own history, and the TAA-guide copy is deleted.
  - **28e. Clean-up.** The dead reverse-Z sky test fixed, with the refusal label corrected (`taa_resolve.rvshader:394-402`, `kDepthClear` 0); stale comments corrected (`taa_resolve.rvshader:1003-1018`; the `reflection_composite` and `reflection_accumulate` headers, if RT2-6 arm 12 did not already).
- **Owner sees.** RT-24's spread gone structurally. The bridge's water glitter stays at RT2-8's level. The window drive stays fixed, and the cables are unchanged. With TAA's memory off, the lighting looks the same; only the edges lose their smoothing.
- **Touches.** `taa_resolve.rvshader` (301, 310, 336, 394-414, 595, 754-763, 783-784, 860-864, 949-1001, 1003-1018, 1066-1171), `PostProcess.cpp` and `PostProcess.h` (`TemporalResolve`, 125-175), `reflection_composite.rvshader`, `taa_guide.rvshader`, `FrameGraphBuilder.cpp` (1655-1690, 3772-4047).
- **Scope.** Refactor. **Depends on.** RT2-6 (the motion lane's precision), RT2-7, RT2-8, RT2-16, RT2-21 (the motion-depth lane), RT2-22 (the composition writes the lanes), RT2-23, RT2-23f and RT2-23g (the reflection signal complete, lamps' shine included, before it is added after the pass), RT2-24, RT2-47. **Risk.** Medium.
- **Exit gate.**
  - The four-way test: the window drive and the car's stop by the owner's eye; the bridge cables unchanged; the bridge water (`check_glint_flicker.py`) at 0.80% or better.
  - At the owner's swing: speckle and edges no worse than the 16-ray reference in the same build (today 494 and 8.83); the taa-refusal view's depth refusals on the poles fall.
  - Parked: `edge_shake.py` at RT-20's numbers, and `parked_stats.py --hf` floor detail within today's parked noise (the proof that removing `kStableWiden` cost no detail).
  - With `--no-frame-memory` against memory on: every signal region within grain (diff image), and the reflection's picture term unchanged; the only differences are at geometric edges and at the edges of the reflection's weight, which this pass exists to anti-alias. The reflection arm's parked flicker of the floor and poles no worse than the before-TAA arm, shown to the owner beside it as a diff image.
  - RT-24's gate from RT2-0, re-run, including the young-pixel grain with the frame filter's memory off.
  - The moving-light drive's reflection lag (`watch_arm.py light 8`; about 200-250 ms by the owner's eye on 2026-10-12, recorded in RT2-23's 23b gate): shorter once TAA no longer carries the reflection a second time, in `emitter_lag.py`'s frames at the close camera and by the owner's eye.
  - The history-invalid lane: the wall beside the moving-light drive (`watch_arm.py light 8`): the pass restarts only in proportion to the reflection's share (`edge_shake.py` no worse); the change check's cost budgeted at the owner's Quality preset.
  - Cost at 2560x1600 at most 0.8 ms (section 6), A,B,B,A.
- **Size.** 5-6 sessions.
- **Closes.** REFL-01, REFL-10, reflections-s5, ISS-22, the mask half of frame-03; the TAA reliance of fix 1 that D9 allowed RT-24 to be committed with; the comparison's claims D-C1, D-C5 to D-C8 and D-C17 and findings D-N6 and D-N7 (section 8).

### RT2-29 · Render scale and upscaling, as an option

- **In plain words.** A setting to render fewer pixels and rebuild the full resolution from them, as every top engine offers. It is a fallback, not part of the plan: the owner's target is native resolution (D8). The engine's own upscaler is the default; DLSS and FSR are optional behind the same setting (D5), and DLSS Ray Reconstruction is only a measured arm.

> **Changed by the comparison (2026-09-25).** Three named steps, so the vendor parts are separately reportable
> and deletable: 29a the engine's own upscaler; 29b DLSS Super Resolution and FSR; 29c the DLSS Ray
> Reconstruction arm, using RT2-7b's hash noise mode, with its conflict with rule 3 written down (it
> replaces every signal's denoiser and the final pass with one network). The denoisers stay at the
> internal resolution, and the loss of lighting sharpness MegaLights reports for that is measured.

- **What is wrong now.**
  - Every target is a fraction of the output size. SSAA can only render larger, and TAA's history sits at output size.
  - There is no render scale, no upscaler and no dynamic resolution.
  - The traced passes are 90-99% linear in pixel count (RT-14: 16 of 24 ms at 3.24 megapixels).
- **What changes.**
  - **29a.** Internal and output resolutions become separate in the graph (RT2-15's resolution classes), and signal grids become relative to the internal one. The final temporal pass (RT2-28) becomes an upscaler when the scale is below 1: its history at output resolution, a kernel scaled to the input spacing, jitter covering the ratio. The denoisers stay at internal resolution. Render scale is one global setting, with `--render-scale`; optional dynamic resolution is driven by GPU time, also a global setting. Per D3 and D26, MSAA 4x stays selectable at full resolution; a render scale below 1 uses the TAA upscaler.
  - **29b.** DLSS Super Resolution and FSR behind the same setting, their SDKs inside private files (D5).
  - **29c.** DLSS Ray Reconstruction as a measured arm only, with RT2-7b's hash noise mode, its rule-3 conflict recorded.
- **Owner sees.** A fallback lever, not part of the plan, because the owner's target is native resolution (D8). At a render scale of 0.75 per axis, every per-pixel pass handles about 44% fewer pixels, for a softer image the owner judges.
- **Touches.** `RenderGraph`, `FrameGraphBuilder.cpp` (419-437), `taa_resolve.rvshader` (becoming an upscaler), `PostProcess.cpp`, `RenderSettings.h`, private DLSS and FSR integration files.
- **Scope.** Refactor. **Depends on.** RT2-15, RT2-28 (D5 answered). **Risk.** Medium: the thin bridge geometry, and the owner's eye on the water glitter.
- **Exit gate.**
  - Arms at 0.67 and 0.75 against native, at the same output resolution: per-pixel diff images judged by the owner.
  - The bridge's `check_glint_flicker.py` and cable band.
  - The garage's parked, edge and swing metrics.
  - A lighting-sharpness diff (the floor's high frequencies) at 0.75 against native, recorded for the owner.
  - A,B,B,A at 2560x1600 output.
- **Size.** 5-6 sessions.
- **Closes.** frame-06; the pixel-count part of ISS-17.

### RT2-30 · Delete the superseded reconstruction and GI paths

- **In plain words.** Once the rebuilt signals are proven, the old paths they replaced are deleted with their settings and histories, so nothing lingers behind a flag. Nothing changes at default settings.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Audit 2 found fixed
> values in paths this item deletes (the old post-chain AO accumulate; gi_denoise's clamps, curve and
> frame cap). They go with the deletion and are not fixed separately.

> **Changed by the comparison (2026-09-25).** The deletion list grows with the paths the rewritten RT2-25 and
> RT2-26 supersede, and it now depends on RT2-12 too. Voxel GI and SSGI are kept, as raster-recipe
> fallbacks (section 7, item 15). The moving layer, follow-hit and the glass moving layer are deleted
> earlier, in RT2-23's step 23e, each after its own arm against v2 (D16).

- **What is wrong now.** Once RT2-23 to RT2-29 are proven, the old paths are dead weight:
  - the old accumulator's special rules (the memory caps, the floor scaled by motion, `kSettledBound`, the untested silhouette history, the curved-mover choice);
  - whatever of the moving layer and follow-hit RT2-23's step 23e has not already removed;
  - TAA's reflection-motion lane and change-map inputs;
  - the old resolve and blurs;
  - the camera-following runtime field and its continuous solve mode (`Renderer3D::SolvePendingIrradiance`);
  - `gi_denoise` and the one-frame-late traced GI chain (`--gi-signal=off`);
  - the post-chain AO arm (`--ao-signal=off`);
  - the probe-as-sky diffuse term in RT mode;
  - the 4-ray GI level;
  - `kIndirectClamp` and `kNeeClamp`;
  - the GiProbe table and the probe-at-hit path in `rtgi_trace`;
  - the GiBounces=2 second-ray path (the cache's feedback replaces it);
  - the trace-only walk over every light (`RV_TRACE_ONLY`), after RT2-12;
  - the hand-placed volume atlas path, after RT2-26's bricks.
- **What changes.** All of it is deleted, with its settings and histories. Voxel GI and SSGI stay as raster-recipe fallbacks.
- **Owner sees.** Nothing at default settings. A smaller shader set.
- **Touches.** The reflection shaders, `taa_resolve.rvshader`, `RuntimeIrradianceField.*`, `Renderer3D.cpp` (the continuous solve), `gi_denoise`, `rtgi_trace.rvshader`, `pbr_fragment.glsl`, `FrameGraphBuilder.cpp`, `EngineConfig`.
- **Scope.** Deletion. **Depends on.** RT2-12, RT2-23 to RT2-29 (RT2-23f and RT2-23g included). **Risk.** Low.
- **Exit gate.** Default frames unchanged from the last proven state (diff 0 through RT2-2a's harness); scenetest green; compile count and binary size recorded.
- **Size.** 2-3 sessions.
- **Closes.** The final parts of REFL-12, GI-13 and frame-13.

### RT2-31 · Glass and water in the ray-traced world; one transmissive layer

- **In plain words.** Glass and water are not in the ray-traced world today: glass casts no shadow, the car's windows are missing from floor reflections, and the sea's own rays are averaged only by TAA. This item puts them in, with tinted shadows in a single ray query, composites the nearest see-through surface exactly over what is behind it, refracts in screen space first with a traced fallback, and gives glass and water one set of histories. By the owner's ruling D28, the panes behind the nearest get one fixed mirror ray each, which ends the last reliance on TAA; by D29, the sea's white shine is reached only through physical causes.

> **Added from RT-24's hard-coded-values audit (2026-09-24; section 10 lists every value).** The
> water's and glass's own mirror ray is capped at 8 times the probe plus a floor (`ReflectionFloor`
> 0.05, made a setting when it crushed the bridge's lamp reflections at night). Both are fixed
> brightness levels (first pass only). General form: once this item gives these rays their own
> accumulators, the cap becomes the accumulators' relative clamp, from the texel's own statistics,
> with no absolute floor.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Glass hides what is
> behind it by how bright its reflection is, not by its reflectance: the garage car's windows hide the
> interior behind bright reflections and show it behind dim ones, and the split moves with the light
> level (live, first to fix; general form: coverage from the glass's own reflectance). Also
> the sea's quad-shared refraction walks its lanes in a fixed order (live), the S5 tap tolerance in
> metres, the backdrop's sky test, and the glass panes' ray plan with fixed counts. (The mirror-ray cap
> was added this morning.) *(2026-09-25: the coverage fix moved forward to RT2-6, arm 15.)*

> **Owner's rulings (2026-09-25).** **D28** (D11 reversed): the panes behind the nearest get one fixed mirror
> ray each, their hits shaded by stored light and emissive, instead of TAA-averaged rays; judged live on
> the garage close-up. **D29:** the sea's white shine, which comes from an absolute cap and breaks rule 2,
> is replaced by the general relative clamp; if white is wanted, it is reached through a physical cause
> (the lamp's colour, the exposure, the tone curve).

> **Changed by the comparison (2026-09-25).** The design is decided: (1) a transmissive traversal class, with
> RGB transmittance for shadow rays in one query, no ordering needed (any opaque hit ends the ray at zero,
> so an unordered product over the transmissive crossings is exact) and the no-duplicate flag the
> product needs; closest-hit rays commit glass and take one straight continuation ray; water enters the
> ray structure as its rest surface. (2) One layer for glass and water, composited exactly as the layer's
> light plus its RGB transmittance times the backdrop, with weighted blending only behind it. (3) Full-frame
> layer histories shared by glass and water, allocated only while a layer is in view. (4) Refraction in
> screen space first, traced where screen space fails, with its own accumulator. (5) The mirror cap becomes
> a relative clamp (D29). (6) It absorbs RT2-13's second half. Verification corrections: glass and water are
> not in the ray structure at all today (G-N1), so the no-duplicate flag is not the only requirement:
> every ray type that must not see them needs the new mask bit excluded, and AO's opaque-flag rays would
> otherwise be fully blocked by glass (G-N6); the flag is set per BLAS, which a mesh shares across
> materials; the "back panes on a mirror ray" design needs stored light for Realtime lamps (RT2-25's
> cache, RT2-48), or those lamps would drop out of back-pane reflections; id Tech 8 is not a precedent for
> screen-space-then-traced refraction (Snowdrop is, and it reports seams, so the seam gate is load-bearing);
> and against RageV's own weighted blending, AVBOIT and MBOIT were cheaper in Activision's 4K PS5 table
> (G-N5), so AVBOIT stays a measured arm for the back layers instead of being dismissed on cost.

> **Owner's report (2026-10-13): the sea's reflection breaks up while the camera turns.** In the bridge's
> rotation test (the harness's swing: 15 degrees either side, twice a second) at the **Headland** view,
> the water's reflection turns unclear and patchy while the camera rotates, and clears once it stops.
> The cause today is in "What is wrong now" below: the sea's mirror and refraction rays are traced inside
> its draw with no accumulator of their own, so only TAA averages them, and under rotation TAA drops most
> of its history and the raw per-frame rays show. Owned here by 31d (the sea's rays on their own
> accumulator, with measured change and camera-motion handling); 31d's exit gate now names this camera
> under the swing. This is the camera ISS-32 was waiting for (RT2-32).

- **What is wrong now.**
  - Blended glass and all water are left out of the acceleration structure (`Scene.cpp:2892-2910`, `Material.h:80-83`; water never reaches `AddInstance`). So glass casts no shadow, car windows are missing from floor reflections, and the bridge's metal cannot reflect the sea. Every BLAS is built with only the opaque flag, so a non-opaque crossing may be reported more than once (G-C2).
  - There is no coloured transmission, so glass cannot tint what is behind it, and only water refracts.
  - Glass and water use two separate single-layer targets, in different formats.
  - Only the nearest pane uses the shared passes, and the panes behind it cast their own rays: 1 to 8 lobe rays per pixel in the draw, their lobe and hit seeds driven by the frame counter in every AA mode, so only TAA averages them (G-C7).
  - The sea's mirror and refraction rays are traced inside its draw with no accumulator, so only TAA averages them. The quad's tracing lane walks only while TAA jitters (G-C6).
  - The mirror cap (8 x the probe + `ReflectionFloor`, 0.5 on the bridge) clips each sample; it is the accepted sea's white shine, and it also clamps glass's in-draw rays (G-C4, G-N3).
  - The glass pane's own lamp light is never re-lit by measured change.
  - The whole glass reflection chain is duplicated at full frame size (96 bytes a pixel of history, 168 with the direct light's), allocated even in scenes with no glass (G-N4), although glass covers only a small share of the frame (measured in RT2-19; the old "about 1%" was a share of changed pixels, not coverage).
  - The nearest pane and every sea crossing go into the same weighted average: at Glitter three to five crossings, about 10% too bright.
  - The sea's refraction is re-lit per quad with a cheaper shade, while the lit seabed underneath is thrown away; the IOR is a literal (0.7519).
- **What changes.** Six named steps, each ending in a report:
  - **31a. The transmissive traversal class.** Glass and water instances enter the TLAS with the non-opaque flag, a transmissive instance flag and a mask bit of their own. The no-duplicate flag goes on every BLAS geometry (a BLAS is shared by every material that uses its mesh), with its trace cost measured; if it costs beyond the spread, meshes used by a transmissive material get their own BLAS. Shadow and visibility rays run one query: the candidate test multiplies each transmissive candidate's RGB transmittance ((1 - directional albedo) x tint x (1 - opacity)) into the ray's product and never confirms it; a confirmed opaque or masked hit ends the ray at zero. Closest-hit rays (reflection, GI, the refraction fallback) commit a transmissive hit like any surface and shade it with the one BSDF through the hit contract: the reflected lobe from stored light, the transmitted lobe from one straight continuation ray (thin glass). Water's stand-in is the body's rest surface, built once as world state, with the generator's analytic normal at the hit; rays that start on the water leave out its mask bit; no per-frame displaced refits. Every ray type that must not see the class excludes its bit or branches: AO (which forces opaque today), the bake solve, the water shade, the lit shader's traces, and the shared traversal.
  - **31b. One transmissive layer and an exact composite.** Right after the opaque G-buffer, the nearest transmissive surface, glass or water, is rasterised into one single-sample layer: depth, its own motion (the wave's for water), shading normal and roughness (with anisotropy and wind angle), F0 and tint, the RGB transmittance of its clear part, the stable id (RT2-5), a medium id. Coverage comes from its directional albedo (RT2-6 arm 15). The composite is exact: final = fog(camera to layer)[the layer's light + RGB transmittance x the backdrop], where the backdrop is the opaque composition plus the weighted-blend resolve of everything behind the layer. The sea's light already includes its refraction, so its transmittance is zero. Glows and particles are split at the layer's depth: behind it they go into the backdrop, in front of it they draw after the composite. AVBOIT is a measured arm for the back layers: adopted if it costs less than today's weighted blending, or if it lowers the glass-over-glass fixture's back-layer error at equal cost.
  - **31c. Histories.** One set shared by glass and water, full-frame, allocated only while a layer is in view and released afterwards, with lanes per RT2-19's contract. Sparse paged histories are rejected: the sea covers much of the bridge frames and the VRAM budget has to hold that worst case; this is reopened only if RT2-19 measures every test camera's layer coverage below 25%.
  - **31d. The signals on the layer.** DirectTrace (RT2-12's candidates, RT2-24's reconstruction, RT2-47's rules) and reflection chain v2 (RT2-23, with RT2-27's tiers) run over the layer's compacted points exactly as over the opaque G-buffer, each with its own accumulator, measured change and camera-cut handling, from the signal tables (RT2-17): the glass lane moves onto the tables here, deleting its hand-wired copy. The parked sea accumulator (D10) is the starting point for the mirror tier: its trace at a quarter of the pixels (one ray per 2x2 quad per frame, walked by the view's own frame count, never by TAA's jitter) becomes a global ray-rate setting for any mirror-tier tile (replacing the unread preset column RT2-3 deleted). The choice between the surface's own history and the virtual image comes from a measured property (the layer's normal changing by more than the lobe's width between frames), not from "is water". The absolute mirror cap becomes the accumulators' relative clamp from the texel's own statistics, with no absolute floor (D29).
  - **31e. Refraction.** A refraction pass over the layer's points. First screen space: the refracted ray (IOR from the medium, not a literal) is marched across the backdrop's depth in pixel steps, hierarchically as SSR does, and the lit backdrop is read at the hit; a point is valid when the hit is on screen, behind the surface, and inside the local depth-gradient thickness. Only invalid points trace a ray, shaded by the contract and accumulated in the layer's refraction history. Thin glass needs no pass (it has no offset). This is the screen-space-then-traced pattern Snowdrop ships for reflections, applied to refraction.
  - **31f. The back layers and particles** (RT2-13's second half). The panes behind the nearest, alpha and weighted particles, and smoke: weighted blending with one weight shared by meshes and particles, the depth normalised to the camera's own near and far clip (audit 2's general form), and RGB revealage for tinted panes. They are lit by the translucency cache (RT2-48): diffuse SH plus one dominant-direction highlight. Emissive stays as authored; each emitter gets a "lit" flag. Particle coverage goes into RT2-28's history-invalid lane (the reactive mask). Reflections on back panes (D28): one fixed mirror ray each, no lobe draw, its hit shaded by the contract's deterministic terms (stored light from the bake, RT2-25's cache and RT2-48, plus emissive); the mirror tier's threshold comes from the lobe's width against the pixel's footprint, and panes above it read stored light. Then S4, WR-17, the light loop and the in-draw lobe rays are deleted from the transparent variants. **Live review:** the garage close-up's back panes, against today and against the reference.
  - The sea's special motion lane in the final temporal pass (kept generic by RT2-28) is now fed by the layer's own motion.
- **Owner sees.** Car windows in floor reflections. Tinted glass shadows. The sea reflected in the bridge's metal and wet deck. Glass and sea noise averaged by their own accumulators. The back panes steady with TAA off. Glitter's water no longer over-bright where crossings stack. The accepted sea without its clipped white shine (judged live at the Headland towers).
- **Touches.** `Scene.cpp` (2881-2949), `Material.h` (80-83), `RayShadows.cpp`, `VulkanResources.cpp` (979), `ray_shadow_trace.glsl` (`RayTraverse`, 100-256), `rtao_compute.rvshader` (371), `irradiance_fill.rvshader` (360), `water_shade.rvshader` (383, 404), `pbr_fragment.glsl` (2647, 4071-4078, 4599-4613, 4804-4978, 5425-5454, 5998-6159, 6273, 6313-6486), `FrameGraphBuilder.cpp` (626-700, 797-799, 2729-3038), `oit_resolve.rvshader`, `particle_weighted.rvshader`, a new refraction pass.
- **Scope.** Refactor. **Depends on.** RT2-2 (the reference renderer's glass, water and fog), RT2-5, RT2-10, RT2-12, RT2-17, RT2-19, RT2-21, RT2-22, RT2-23, RT2-23f, RT2-23g, RT2-24, RT2-25 (the hit contract and the cache), RT2-27, RT2-47, RT2-48. (The transmissive instances go through today's instance path; RT2-35 later carries the class into the ray-world module.) **Risk.** High.
- **Exit gate.**
  - 31a: the camp (no transmissive material) byte-identical; garage DirectTrace within +5% with the glass in the TLAS (A,B,B,A at 2560x1600); the tinted-pane scene's floor shadow against the reference, near zero; the garage's car windows in the floor reflection, within grain of the reference; the bridge's sea in the deck and metal reflections, against a reference with a one-off displaced BLAS, within grain; AO, GI and reflection images unchanged except where intended; validation and sync validation clean.
  - 31b: the glass-over-glass fixture with a particle in front, against a sorted reference: front-layer error 0, back layers no worse than today; Glitter's over-brightness gone, with the mean and the red-to-blue ratio shown to the owner; the composite and the new layer lanes at most 0.1 ms at 2560x1600 plus the layer's shading, measured A,B,B,A; the AVBOIT arm's result recorded.
  - 31c: the layer histories' VRAM reported, below today's glass 168 bytes a pixel plus the sea's private histories, and zero in a scene with no glass in view.
  - 31d: every layer signal converges under `--aa=none`, still and moving; `speckle_count.py` on layer pixels at the owner's swing no worse than the 16-ray reference; `check_glint_flicker.py` at 0.80% or better; the pier and the garage close-up against the reference within grain; `emitter_lag.py` over glass; D29's look: a per-pixel diff at the Headland towers, judged live; the sea at the Headland view under the harness's swing, steady and not patchy while the camera turns, judged live by the owner (their report of 2026-10-13).
  - 31e: the pier, calm and wavy, against the reference (refraction and absorption) within grain, with no structure along the boundary between screen-space and traced points; the share of traced fallback points reported; converges under `--aa=none`.
  - 31f: the garage close-up's back panes judged live by the owner against today and against the reference (D28); under `--aa=none` and MSAA 4x, no frame-to-frame change on a still camera; register counts per pipeline recorded.
  - Costs (proposals, the owner sets the bars from the first measurement): the garage's glass layer at most 0.5 ms at native, against today's +0.45 ms at the owner's close-up (where glass rays are 1.3 of the transparent pass's 2.0 ms). A,B,B,A.
- **Size.** 10-13 sessions, in the named steps above.
- **Closes.** MAT-03, MAT-06, REFL-09, ISS-23, the mirror half of ISS-11 (D10, D29); RT-22's glass-pane part; direct-s4 and direct-15 (with RT2-13); the comparison's claims G-C1 to G-C4 and G-C6 to G-C8 and findings G-N1, G-N3 to G-N6 (section 8).

### RT2-32 · Water as a material, a surface generator and a medium

- **In plain words.** The sea is a renderer of its own: its own grid with no levels of detail, its own lamp passes, its own flags, and hand-set numbers everywhere. This item makes water an ordinary material drawn through RT2-31's transmissive layer, with its waves, roughness and caustics all derived from one wave spectrum set by the body's own dials, on a grid that follows the camera. By the owner's ruling D30, FFT spectrum waves are built as an arm and judged live at the pier and Headland against today's 32-wave sum.

> **See RT2-1 (2026-10-13):** `water.rvshader` faults the GPU once spirv-opt optimises it; RT2-1 finds
> whether that is a read out of bounds in the shader. A rewrite here must not carry it over.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Twenty-three values
> in the sea: the lamp glint memory of 16 frames and scatter memory of 4 (live, first to fix) and their
> bound; the detail normals' tile, scroll, fade distance and strength (the fade is live); the pixel footprint on the
> water, the Cox-Munk slope ceiling and the footprint saturation (live, first to fix); caustic cells in
> metres read at an undefined mip (live); the contact-foam band from screen depth instead of vertical
> depth (live); foam drift, clamp and resolution; the grid's division cap and band; the water's
> roughness; the detail tile's fixed wave fan (live); the refraction reach. General form: everything
> from the body's own dials and the wave spectrum (dispersion, Cox-Munk from the implied wind),
> footprints from the pixel, memories from the estimate's own statistics.

> **Owner's rulings (2026-09-25).** **D30:** sea waves: FFT spectrum waves as an arm, judged live at the pier
> and Headland against today's 32-wave sum. **D29** (with RT2-31): the white shine is reached only through
> physical causes.

> **Changed by the comparison (2026-09-25).** Added: the generator as a spectrum on a camera-centred clipmap,
> with exact deep-water dispersion (no 0.32 speed factor, a value neither audit had listed; section 10);
> sub-pixel roughness from the spectrum's unresolved slope variance under the pixel's footprint or the ray
> cone, replacing the fitted constants; caustics from the finest band's normals, as HDRP computes them; the
> medium's IOR and absorption as dials; a skip rule for any covering layer from one definition of
> "invisible", which supersedes "fully covering" (the lit seabed is still used where screen-space
> refraction can see it). The sea mirror re-lands on the shared layer chain (D10, RT2-31d), with its
> private-path heuristics made general. The raster refraction's push and occluder tolerance are raster-only
> and belong to RT2-44.

- **What is wrong now.**
  - Water is a parallel renderer. It has its own grid (a uniform grid at the component's spacing, 3 m on the bridge, capped at 1024 divisions a side, with no LOD: 933 x 933 quads over 2800 m), its own lobe inside the lit shader, its own surface layer, three implementations of lamp light, 19 flags, and refraction inside its draw.
  - The waves are a 32-wave Gerstner sum whose speed is scaled by a fixed 0.32 (`water_waves.glsl:165`).
  - The footprint roughness uses constants fitted to Cox-Munk, and the detail normals fade by distance; secondary rays get none of it. The caustics are two drifting tiles of the foam texture, unrelated to the waves.
  - At Headland the sea costs about 8.4 of 17.1 ms: Transparent 5.58, DirectWaterShade 1.09, DirectWaterChoose 0.58, WaterSurface 0.75, WaterFoam 0.22, WaterAccumulateLamps 0.17.
  - The seabed under the sea is G-buffered, ray traced and lit every frame, then covered entirely.
  - The lamp passes shade one water surface per pixel, while the water draw blends three to five crossings at 400 m (about 10% too bright at Glitter).
  - The sea work the owner accepted on 2026-09-20 is on the parked branch; per D10 it comes back here.
- **What changes.**
  - **The generator.** One per water body. Displacement and normals on a camera-centred geometry clipmap (view state); the ray stand-in stays the rest surface (world state, RT2-31a). The spectrum comes from the body's dials: the wind speed and fetch implied by WaveLength and WaveHeight, a Pierson-Moskowitz/JONSWAP spectrum, and exact deep-water dispersion (omega^2 = g k).
  - **D30's arm.** FFT cascades built from that spectrum, against today's accepted 32-wave sum moved onto the same clipmap. The owner judges them live at the pier and Headland and picks one. The 0.32 speed factor goes with the owner's pick: the FFT arm uses exact dispersion; if the owner keeps the 32-wave sum, its 0.32 is replaced by exact dispersion as its own owner-judged arm (the waves' speed changes).
  - **Roughness and detail.** The sub-pixel roughness is the spectrum's unresolved slope variance between the finest carried wave and the footprint (from the pixel's own derivatives for primary hits, from the ray cone for secondary hits), through Cox-Munk's formula rather than its constants. Detail normals and caustics come from the finest band; caustics from its normals, refracted and projected as HDRP does. Foam keeps the Jacobian compute, with its drift from the spectrum's own Stokes drift, no step clamp, and a texel equal to the grid's quad.
  - **The material and the medium.** The tuned lobe (anisotropic Beckmann with the footprint roughness) becomes material parameters of the one light library and BSDF. The medium: per-channel Beer-Lambert along the refracted path, with dials for absorption and IOR (today's clear-ocean shape, sigma = (4.6, 1.9, 1.3) / GradientDepth, as the default). The refraction reach and the skip threshold come from one definition of invisible: transmittance under half an output level after exposure.
  - The block-rate choosing (the sea's lamp choice made once per block of pixels rather than per pixel) stops being a water feature. It becomes a feature of the shared light-candidate structure, driven by one global setting, so any surface can use it. A cost strategy attached to one material would be the kind of surface-type rule the owner rejected.
  - **Skipping covered work.** The layer is known before the opaque signals; where its transmittance to the opaque surface is below the invisibility threshold, DirectTrace, AO, GI, reflections and composition skip the pixel through tile lists. This applies to any covering layer, not only water.
  - Per D10, the parked branch's sea work is re-landed where it maps, as the starting point, on RT2-31's shared layer chain. It is checked with a moving camera (it never was) and judged live by the owner. Its white shine goes (D29).
  - Deleted: the private accumulator and its 4- and 16-frame memories, water_choose, water_shade, water_accumulate and water_trace, the separate WaterSurface target, the in-shader lamp sampler, the uniform grid and the water-only flags.
- **Owner sees.** A faster bridge frame (the proposal is the sea at Headland at most 3.6 ms at native resolution in total, section 6's 2026-09-25 proposal; the owner sets the bar from the first measurement), no private sea behaviour, waves that reach the horizon, caustics that follow the waves, and the pier look the owner accepted, judged live against the FFT arm.
- **Touches.** `Water.cpp` (126-229, 350, 480-660), `water_waves.glsl` (20-31, 160-175), `pbr_fragment.glsl` (3467-3476, 4205-4422, 5246-5315, 6334-6477), `FrameGraphBuilder.cpp` (3063-3643), `Renderer3D.cpp` (1737-1757), `water_*.rvshader`, `water_lamps.glsl`, `EngineConfig` (607-744).
- **Scope.** Refactor and deletion. **Depends on.** RT2-20 (ray cones), RT2-31. (The water's clipmap is its own geometry, not RT2-38's mesh LOD chains, so it does not wait for M6.) **Risk.** High: RT-8 found the sea bit every session.
- **Exit gate.**
  - Per-pixel diffs at the bridge's three composed cameras (RT-8's lesson), with the mean brightness and the red-to-blue ratio unchanged unless shown to the owner.
  - `check_glint_flicker.py` at 0.80% or better, also under `--aa=none`; no aliasing band at the horizon.
  - The owner's live checks on the pier and at Headland: the accepted look, and D30's FFT arm against the 32-wave sum.
  - Skipping on against off: a per-pixel difference of no more than half an output level.
  - The sea at Headland at most 3.6 ms at native in total (section 6's 2026-09-25 proposal); A,B,B,A at Headland and Glitter.
  - The blur report's camera (ISS-32), given by the owner 2026-10-13: the Headland view, under the swing, where the sea's reflection turns patchy while the camera rotates. Owned by RT2-31d (see the owner's report there); checked here again once the sea work is re-landed.
- **Size.** 8-11 sessions.
- **Closes.** MAT-10, materials-s4, ISS-11, ISS-32; the final parts of ISS-15 and direct-15; the comparison's claim G-C15 (section 8).

### RT2-33 · Fog, glows and particles in the composition; TAA no longer forced

- **In plain words.** Fog is painted once after TAA over the opaque depth, so reflections carry no fog and glass is fogged at the depth of what is behind it. This item applies one exact fog formula to every segment of every path (to the eye, in a reflection, through water), makes lamp glows follow the lamp's own light, lights the particles from RT2-48's cache, and, as the last step of M5, stops forcing TAA: every signal now stands alone, so TAA and MSAA 4x become choices like any other AA mode (D3, D26). One change is shown to the owner live before it is adopted: the bridge's fog limit (0.42) applied once along each whole path, instead of only in front of the opaque surface.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Light glows: a fixed
> side-glow share and cone edges, a back cutoff and a 1 m depth pull (live: glows seen from the side of
> spot lamps). Fog: the sky treated as FarClip away (live) and a fixed horizontal-ray threshold. General
> form: glows follow the light's own emission toward the camera; fog to the sky integrates to infinity.
> *(2026-09-25: the sky's fog moved forward to RT2-6, arm 16; this item moves the same function into the
> composition.)*

> **Changed by the comparison (2026-09-25).** Fog becomes one medium-aware analytic integral applied per
> segment, and the post fog pass is deleted. Glows follow the light's own emission profile, take their
> visibility from rays in RT mode, and draw after the transmissive composite. Particles are lit from the
> translucency cache (RT2-48) and write the reactive mask, with one shared blending weight normalised to
> the camera's range. The TAA force is lifted last, and with D28 there is no TAA exception left.
> Verification correction: the bridge's `FogMaxOpacity` 0.42 caps the fog's total opacity, and a cap does
> not compose over segments (G-N8), so the per-segment form is exact only where the cap is 1. Decided,
> subject to the owner's live review below: the cap becomes the most fog the camera sees along one whole
> path, applied once per camera path to the path's total optical depth, where the path's light is
> composed; the profile's value is untouched (rule 5). The sky's in-scatter is evaluated per segment
> direction. *(Corrected after the second critic's review: this changes what the dial means, from a cap on
> the fog in front of the opaque surface to a cap on the whole path, while its value stays 0.42. That is
> not among the owner's 19 answers, so it is adopted only after the named live review.)*

- **What is wrong now.**
  - Fog is one post pass after TAA over the opaque depth. So traced reflections and refractions carry no fog along their own path: a far tower stays sharp in the near water. See-through pixels are fogged by what lies behind them; the sea relies on the profile's FogFloor of 0 so the seabed stays clear.
  - Glows and sorted or additive particles are drawn into the opaque target before glass and water (G-C9), with side visibility from taste curves and a 1 m depth pull; the bridge profile scales the glows to 0.02.
  - Particles are unlit and have no motion.
  - With rays on, the engine still forces TAA.
- **What changes.**
  - **Fog.** One include holds the analytic height fog: `fog.rvshader`'s closed form, a series branch for a small product of climb and travel, and the sky integrated to infinity (RT2-6 arm 16). It is called with an origin, a direction, a length and a medium, so that: the opaque composition fogs the air part from the nearest see-through surface (or the camera) to the pixel; the layer composite fogs from the camera to the layer; each weighted-blend fragment's premultiplied colour becomes T x C + coverage x S; every reflection segment and air-side refraction segment carries its transmittance and in-scatter; glows are multiplied by the transmittance from the camera to the lamp. Segments behind water use the water medium. `MaxOpacity` is applied once per camera path to its total optical depth; `StartDistance` and `FogFloor` apply the same way everywhere. The dial's meaning changes (the most fog the camera sees along one whole path, applied once) while its value does not. **Live review:** before this redefinition is adopted, the bridge at Headland with today's fog against the once-per-path cap, shown as a per-pixel diff and judged by the owner. Fog runs before the final temporal pass, and the post fog pass is deleted.
  - **Glows.** The glow disc carries the intensity the light emits toward the camera, from its own angular profile (the cone term, or an authored per-fixture emission profile that defaults to zero outside the cone); `kSideGlow`, the 0.34-0.77 curve and the back cutoff are gone. In RT mode its visibility comes from rays to a fixed set of points on the source disc, leaving out the owning fixture as RT-11 does, so the answer holds still; the raster recipe keeps a depth test, pulled by a multiple of the light's source radius. The glow is multiplied by the fog's transmittance, drawn after the transmissive composite (split at the layer's depth), and never writes the reflection-weight lane.
  - **Particles.** Lit from the translucency cache (RT2-48), with the reactive mask written into RT2-28's history-invalid lane, and one blending weight shared with meshes, normalised to the camera's range.
  - The froxel volume (WR-11) stays in the WR series; its design is recorded in section 7 (it shares RT2-48's sampling).
  - **The last step of M5: TAA stops being forced (D3).** Every signal, the layer's included, now has its own accumulator, and every random draw moves in every AA mode (RT2-7). With D28 the back panes need no exception. TAA becomes a choice like any other AA mode. With rays it stays the default; MSAA 4x becomes selectable at full resolution through RT2-22's raster drawing (D26).
- **Owner sees.** The far bridge fogged in its reflection on the sea as well. No haze under the sea without the FogFloor trick. Glows over the sea, and no glow from the side of a narrow spot. Lit particles in the ray-traced scene. MSAA 4x available with rays on.
- **Touches.** `fog.rvshader` (121-326), a new fog include, the composition shader, the hit shading (`pbr_fragment.glsl` 3214, 3334-3340), `light_glow.rvshader` (117-162), `LightGlow.cpp` (29, 80-90), `Renderer3D.cpp` (5074-5082), `Scene.cpp` (5476-5503), `particle*.rvshader`, `ParticleRenderer.cpp`, `FrameGraphBuilder.cpp` (197-206, the TAA force; 4834-4905, the fog pass), the render settings panel.
- **Scope.** Refactor. **Depends on.** RT2-2 (the reference renderer's fog along segments), RT2-22, RT2-25, RT2-28, RT2-31, RT2-32, RT2-48. **Risk.** Low.
- **Exit gate.**
  - Fog off: frames byte-identical. The owner's live review of the once-per-path cap against today's fog at Headland (a per-pixel diff), before it is adopted. Headland: the far bridge fogged in its sea reflection, against the reference renderer with fog along segments and the same once-per-path cap, within grain; judged by the owner's eye. A test arm ignoring FogFloor shows no haze under the sea. Fog cost no worse than today's fog pass, A,B,B,A.
  - Glows: Headland's glows over the sea present, in a diff restricted to glow pixels; a narrow flood seen from outside its cone shows no glow; no popping on a moving camera under `--aa=none`; at most 0.1 ms.
  - Particles: the camp's smoke against the reference renderer; `smear_metric.py` on moving smoke with TAA.
  - The TAA step: with rays under `--aa=none` and under MSAA 4x, every signal converges, moving and young states included (RT2-2h's command); the owner judges MSAA 4x with rays live.
- **Size.** 4-5 sessions.
- **Closes.** MAT-13, the lighting half of MAT-16, the structural half of materials-s2; the prerequisites of ISS-29; ISS-12's forced TAA (D3); the comparison's claims G-C5 and G-C9 and finding G-N8 (section 8).

### RT2-34 · A GPU scene and change-driven transforms

- **In plain words.** Every frame, for every object, the engine rebuilds half a dozen records from scratch on one CPU thread, and at 60,000 objects that is where the frame goes. This item keeps the scene on the GPU between frames: each object gets a permanent slot, only objects that changed are updated, and transforms are recomputed only where something moved. The demo scenes do not change, except one owner-judged arm that makes objects moved by the fixed simulation step glide instead of stepping.

> **Checked by the comparison (2026-09-25): kept.** This is the industry's GPU-scene baseline (Unreal's GPU Scene).
> One small fold-in: the slot's row carries the emitter flag, so the hit table's owner scan, which
> became unbounded (casters x emitters, per frame, per view) when RT-24 removed the 16-emitter cap,
> disappears (step 34c).

- **What is wrong now.**
  - The renderer keeps no scene between frames. Each frame, for every mesh entity, it writes:
    - a draw-list entry and a 96-byte cull record, two to four times;
    - a 272-byte instance row, including a matrix inverse;
    - a TLAS entry and a CPU record of about 260 bytes;
    - a 144-byte hit row.

    Each carries its own copy of the world matrix and its own index.
  - The transform walk visits every entity three to seven times per frame, with a hash lookup per child.
  - The renderer reads live component memory through borrowed pointers, so simulation cannot overlap recording.
  - Each frame's instance fill runs a per-object loop on the CPU: a material lookup, a parameter resolve, a probe-slot search and a matrix inverse per object (`Scene.cpp:5118-5159`). This, not the draw submission (already about 20 indirect draws), is most likely where the G-buffer pass's 11.5 ms of CPU at 60,000 objects goes (inferred; RT2-2f confirms).
  - The hit table's emitter-owner scan is O(casters x emitters) per frame per view since RT-24 lifted the emitter cap (`Renderer3D.cpp:5627-5637`).
  - Only physics bodies are interpolated; objects moved in the fixed step move stop-go at any display rate other than 60 Hz.
- **What changes.** Four named steps, each ending in a report:
  - **34a. Tracked transform writes.** Writes to transforms go through setters that stamp a change version. C#, physics and the serializer use the same path. This breaks the native-module ABI, so the documented rebuild step runs and is checked.
  - **34b. A flat hierarchy.** Depth-ordered and linked by parent index. World matrices are worked out once per frame, for changed subtrees only. The old compare walk stays as `--validate-transforms`.
  - **34c. The GPU instance table.** Render proxies get stable slots in one device-local instance table: world 3x4, previous world, bounds, mesh and LOD, material offset, flags (the emitter flag among them) and ray mask. Only changed rows are uploaded, so the per-object loop above runs only for objects that changed. Culling, raster, TLAS instances, hit shading and every id read that one index. Extraction produces a snapshot.
  - **34d. Fixed-step interpolation, as its own owner-judged arm.** Previous and current simulation poses are kept for anything a fixed step moved, and blended at render time. This changes what moving objects look like (every script that moves things in `OnTick` glides instead of stepping), so the owner judges it live on its own.
- **Owner sees.** At scale, CPU frame time falls. The demo scenes are unchanged until 34d, whose change the owner judges.
- **Touches.** `Scene.cpp` (`UpdateWorldTransforms`, `RefreshDrawList`, `OnRender`), `Components.h`, `ECS.h`, `Renderer3D.cpp` (instance data; the owner scan at 5627-5637), `GpuCull.cpp`, `Interop.cpp`, `PhysicsWorld.cpp`, the serializer.
- **Scope.** Rewrite. **Depends on.** RT2-5, RT2-17, RT2-18. **Risk.** High: changing the component layout breaks native modules and needs the documented rebuild step.
- **Exit gate.**
  - Diff 0 on the demo scenes after 34a to 34c.
  - `bench_scale.py`'s ray-traced arms on the realistic-content scenes at 20k, 60k and 120k: the transform-update, extraction and instance-fill phases grow with the number of changes, not the number of objects (the still arm near zero; moving arms at 1%, 10% and 100%).
  - A,B,B,A.
  - `--validate-transforms` clean on every scene and in scenetest.
  - 34d: the velocity lane of a fixed-step mover at `--frame-time=0.00833` (half a tick) is smooth instead of zero-then-a-step, and the owner judges the live movers.
- **Size.** 10-12 sessions.
- **Closes.** GEO-01, GEO-02, CORE-02, CORE-07, ISS-26, the extraction half of frame-04, the engine half of CORE-08.

### RT2-35 · The ray-tracing world as its own module

- **In plain words.** The ray-traced world lives inside the shadow code: it is rebuilt from the scene every frame on the CPU, its top-level tree is refit even when its contents changed, meshes get their ray structure the first time a ray needs it (with a stall), nothing is compacted, and animated meshes are refit forever. This item gives it its own module with the policy the top engines ship: the top-level tree skipped when nothing changed and otherwise fully rebuilt on the GPU, meshes' structures built at load, batched, compacted and pooled, and animated meshes refit only when their pose changed. Models keep the structure the artist built (D25).

> **Owner's ruling D25 (2026-09-25).** One entity and one BLAS per imported model: no. The artists decide how
> they build their models. *(This removes the step RT-SERIES-2 planned, one multi-section BLAS and one
> entity per placed model, and the comparison's proposal to do it in the importer for new placements.
> GEO-06 moves to section 7.)*

> **Changed by the comparison (2026-09-25).** "Skipped, refit or rebuilt by a measured policy" is replaced by
> the decided policy: GPU-written instances; the build skipped when nothing changed, otherwise a full
> rebuild with the fast-trace flag and no ALLOW_UPDATE (what Unreal, as its 5.8 guide still says, and
> NVIDIA's guidance do); one persistent TLAS, double-buffered only if RT2-20c's async arm is adopted;
> built whether or not shadows are on. The BLAS manager: at load, batched, compacted, pooled, with aligned
> scratch and a triangle budget; skinned meshes posed once, refits batched, skipped when the pose is
> unchanged, rebuilt when their bounds grow past a ratio. Verification corrections: the skip beats an
> every-frame rebuild only on frames where nothing changes, and any skinned refit counts as a change, so
> it is worth something only together with the pose-hash skip; today already holds one TLAS per frame in
> flight, so a single persistent TLAS halves that memory; at a million moving instances, a full rebuild
> per changed frame is what Unreal's early experiments projected would limit Lumen to about 100,000
> instances (Lumen 2022; a projection, not a hard API limit), so RT2-39 measures a partitioned TLAS as an arm;
> the "0 ms parked" claim is measured on fully still frames only; the address registry that names a freed
> BLAS runs only with validation on, so it is a debug tool, not a lead.

- **What is wrong now.**
  - The TLAS instance list is rebuilt from the ECS every frame (per view) inside the shadow code, and packed on the CPU. (RT2-4 already skips the build when nothing changed, and removed the shared-pointer copy per instance.) With shadows off, no ray is traced at all.
  - A refit is taken whenever the instance count is unchanged, even when BLAS references, masks or flags changed, and every build carries ALLOW_UPDATE; each frame-in-flight TLAS keeps its own refit counter, so a slot is fully rebuilt only every 64 of its own builds (E-C1, E-N1).
  - The hit table is rebuilt from scratch every frame in `EndScene`. That is probably the roughly 4 ms that rays add to the G-buffer pass's CPU at 60,000 objects (inferred; RT2-2f confirms).
  - Each static BLAS is built the first time it is gathered into the ray world, with its own scratch buffer and a blocking submit. It is never compacted, and nothing tracks its memory (E-C3).
  - Skinned BLASes are refit forever, one after another with a barrier each, with no check for an unchanged pose, and skinned twice (once in the vertex shader, once in compute), with no normals written (E-C9, E-N2).
  - Each material section of a model is its own BLAS and TLAS instance (a car of 150 parts). By D25 this stays as the artist built it.
  - The instance masks carry only static and moving (they are used: the bake solve traces the static half, the subtractive shadow the moving half).
- **What changes.**
  - **The TLAS.** Owned by the new module; RayShadows stops owning the ray world. One compute pass writes the instance descriptors from the GPU-scene table (RT2-34); the custom index is the stable 24-bit slot. The masks stay static and moving, with bits reserved and unassigned until an item needs them (RT2-31's transmissive bit is the first). The build is skipped when the frame's change list touched no instance, transform, mask, flag or BLAS; otherwise it is a full build with the fast-trace flag only. The refit path is deleted (in RT2-43). One TLAS, recorded as a graph pass (RT2-15), with both barriers derived by the graph; built whenever any traced signal is on, independently of shadows.
  - **The BLAS manager.** Static BLASes are built when geometry arrives (load or stream-in), never at first trace; several builds per call, recorded into the frame under a per-frame triangle budget (one global RenderSettings value; it only delays when new geometry appears in rays and never changes the steady picture, which is stated as a known limit); fast-trace plus compaction flags, the compacted size read one frame later from a query ring, copied into a pooled acceleration-structure arena (sub-allocated, 256-byte aligned), and the original freed; scratch from one pooled buffer aligned to the device's rule; bytes reported by category (static, dynamic, TLAS, scratch). One BLAS per mesh LOD, as the artist built the mesh (D25).
  - **Skinned and dynamic BLASes.** One skinning compute pass per frame writes posed position, normal and previous position for raster and rays alike (no more double skinning). All refits go in one batched build call with one barrier. A refit is skipped when the pose (the bone matrices' hash) is unchanged (Battlefield V went from 400 to 50 rebuilds a frame this way). A rebuild follows when the refit bounds' surface area passes the build-time bounds by a global dimensionless ratio, chosen by a measurement of trace drift.
  - **The hit table** becomes a view of the GPU scene, updated only where rows change.
  - A mask policy per ray type in RenderSettings, adopted by diff image.
- **Owner sees.** At scale, the share of the shadow phase that RT2-2f attributes to the ray-tracing world falls toward 1 ms at 60,000 objects (a proposal; the draw-list and cull-table share, about 3 ms with rays off, belongs to RT2-34 and RT2-43). No hitch on the first ray-traced frame. Less BLAS memory. Reflections still traced with shadows switched off.
- **Touches.** `RayShadows.cpp` and `RayShadows.h` (replaced), `Scene.cpp` (2525-2951), `VulkanResources.cpp` (903-1353), `Mesh.cpp` (209-231), `Renderer3D.cpp` (5563-5704), `skin_positions.rvshader`.
- **Scope.** Rewrite. **Depends on.** RT2-15 (graph resources for the TLAS), RT2-18, RT2-2c, RT2-34. **Risk.** High.
- **Exit gate.**
  - `bench_scale.py`, still: no TLAS build, and the shadow phase's ray-world CPU share under 0.2 ms at 60k (a proposal; the first arm measures).
  - Moving at 1%, 10% and 100% on the 20k, 60k and 120k scenes: the TLAS build's GPU time recorded; the ray world's CPU time grows with changes, not objects.
  - Every ray-traced pass's GPU time after 64 moving frames: the full rebuild no slower than today's refit beyond the spread, A,B,B,A.
  - BLAS megabytes before and after compaction on the garage, the bridge and the camp.
  - Blocking submits per frame (RT2-2f's counter): 0 in steady state and while 200 new meshes spawn; the worst spawn frame recorded against steady.
  - The camp's fox and a skinned test crowd over 60 s: trace time with and without the rebuild ratio, A,B,B,A; the shadow diff against a rebuild-every-frame reference within grain.
  - Diff 0 on the demo scenes; sync validation clean.
  - A scene with shadows off and reflections on: reflections still traced.
- **Size.** 7-9 sessions, in named steps: the TLAS written on the GPU with its policy; the BLAS manager; the hit table; skinning.
- **Closes.** GPU-03, GEO-04, GEO-05, GEO-13, GEO-16, rhi-s3, direct-13, the TLAS half of frame-04, the TLAS and bake half of CORE-06; the comparison's claims E-C1 to E-C3 and E-C9 and findings E-N1, E-N2 and E-N4 (section 8). (GEO-06 moved to section 7 by D25.)

### RT2-36 · A job system, frame pipelining and parallel recording

- **In plain words.** The whole frame runs on one CPU thread, physics runs its own private pool of threads, and input is read before the frame waits for the GPU. This item adds one job system that every system (physics included) runs on, lets the next frame's simulation overlap this frame's recording, and records on several threads only if timing shows recording is worth splitting. The picture does not change; at scale the CPU time spreads across cores, and input latency falls.

> **Checked by the comparison (2026-09-25): kept unchanged.** A job system and frame pipelining are standard,
> and the item already times recording before it parallelises it.

- **What is wrong now.**
  - The whole frame runs on one thread.
  - Jolt has its own private pool of 23 threads per play session.
  - Input is read before the frame waits for its GPU slot.
  - Time is kept in float seconds: 0.98 ms of resolution after 2.3 hours.
  - There is no frame-pacing mode (lining frames up with the display to keep latency low).
  - Command recording has never been timed on its own, apart from the renderer's per-object work.
- **What changes.** Named steps, each ending in a report:
  1. First, time recording alone, at scale, with rays on. If it is small, parallel recording (step 7) is skipped.
  2. One engine-owned job system (hardware threads minus one) with I/O threads, and Jolt on it through its job-system interface. Jolt's types stay inside the physics module's private files (the API-wrapping rule).
  3. Each per-frame system becomes a task with declared inputs and outputs.
  4. Wait for the frame's GPU slot first, then read input, simulate and record. A frame-pacing mode (`VK_KHR_present_wait`, or NVIDIA's low-latency mode where present) as a global setting.
  5. Time kept as 64-bit ticks.
  6. The frame pipelined: frame N+1's simulation overlaps frame N's recording.
  7. Recording split by groups of passes into command buffers on several threads (Vulkan only; OpenGL is frozen, D1).
- **Owner sees.** At scale, CPU time spread across the cores. Lower input latency.
- **Touches.** `Application.cpp`, `FixedStep.h`, `PhysicsWorld.cpp`, `VulkanDevice.cpp` (a command pool per thread, present modes), `RenderGraph`, new job-system code.
- **Scope.** Rewrite. **Depends on.** RT2-18, RT2-34. **Risk.** High.
- **Exit gate.**
  - Realistic-content scenes with rays on, at 60k and 120k objects: the main thread at most 8 ms at 60k (a proposal), A,B,B,A.
  - Demo scenes pixel-identical, with no frame-time regression.
  - Input latency measured before and after with a named method: engine timestamps from input sampling to present, cross-checked with PresentMon.
  - Validation clean with threading on.
- **Size.** 10-12 sessions.
- **Closes.** CORE-01, CORE-11 (the input order, time and pacing parts), GPU-06, the threading half of frame-04, the thread-pool half of CORE-12.

### RT2-37 · GPU-driven draws and compact mesh data

- **In plain words.** Each pass issues one draw per distinct mesh, vertices are stored uncompressed, and every mesh keeps a CPU copy. This item moves static geometry into shared pools, lets the GPU write and count the draws, measures occlusion culling before adopting it, and compacts the mesh data. The picture does not change.

> **Checked by the comparison (2026-09-25): kept unchanged.** This is the GPU-driven draw baseline; occlusion
> culling is measured before it is adopted.

- **What is wrong now.**
  - Each pass issues one indirect draw per distinct mesh, binding that mesh's buffers each time, so command counts grow with distinct meshes times passes.
  - Culling tests the view frustum only, and slot lookups are linear scans.
  - Vertices are 32-byte floats, indices are always 32-bit, and every mesh keeps a CPU copy for editor picking.
  - There is a dormant meshlet path.
- **What changes.**
  - Static geometry lives in pooled arenas.
  - Culling writes compacted draw commands, drawn with one indexed indirect draw with a GPU count per pipeline. Instances are culled first, then clusters.
  - Two-phase depth-pyramid occlusion culling (Hi-Z: testing boxes against a small depth pyramid of what was already drawn) is measured on an occluded scene and kept only if it pays.
  - Cooked, separate position and attribute streams; 16-bit indices where they fit.
  - No CPU geometry copies in the runtime (editor picking by ray query or an id buffer).
  - Meshlets either folded into the cooked cluster pipeline or deleted.
- **Owner sees.** Fewer commands and buffer binds per pass as distinct meshes grow; less geometry memory; occlusion culling where it pays. This item is not credited with the G-buffer pass's 11.5 ms of CPU at 60,000 objects: the scene already goes out in about 20 indirect draws, and that time is most likely the per-object instance fill (RT2-34) and the per-frame hit table (RT2-35).
- **Touches.** `Renderer3D.cpp` (4840-4879, 6099-6157, 9261-9302), `cull_lit.rvshader`, `GpuCull.cpp`, `Mesh.cpp`, `Mesh.h`, `MeshCook`, `ScenePicking.cpp`.
- **Scope.** Refactor. **Depends on.** RT2-18, RT2-34. **Risk.** Medium.
- **Exit gate.**
  - `bench_scale.py`: G-buffer pass CPU time and draw count, A,B,B,A.
  - Diff 0 on the demo scenes.
  - Geometry VRAM before and after.
  - `check_gpu_lit.py` green.
- **Size.** 6-8 sessions.
- **Closes.** GEO-09, GEO-14, GEO-18 (meshlets), the final part of ISS-6.

### RT2-38 · Levels of detail for raster and rays; opacity micromaps

- **In plain words.** Every mesh is drawn and traced at full detail at any distance, and terrain rays always trace the finest level. This item cooks simpler versions of each mesh, chooses the detail for rays by how many pixels an object covers (never by metres), guards against a simpler ray mesh shadowing the detailed one, and measures opacity micromaps for cutout foliage. Far-away stand-ins, if ever needed, go into a second ray structure of their own.

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Thin members (the
> bridge's cables) fade out at fixed distances in metres, on screen and in rays (live: flicker). General
> form: fade by projected width in pixels, the member's thickness over the pixel footprint.

> **Changed by the comparison (2026-09-25).** Ray LOD by projected size in pixels (the smallest footprint across
> open views), with hysteresis in the same unit. Self-intersection between a simplified ray mesh and the
> raster surface is guarded as Lumen does, with the distance set by the chosen LOD's own simplification
> error instead of a fixed epsilon: a short back-face-culled ray out to that distance, then the full ray.
> Far-field stand-ins go in a second TLAS instead of a mask bit, because Lumen measured a 44% near-field
> penalty from putting the far field in the same TLAS under a mask (PS5: 1.65 to 2.37 ms) and named multiple
> TLASes the proper fix; they are built only on the measured need stated in "What changes" below. Before calling this
> better than Unreal, check whether current Unreal (5.5 and later) already separates its far field. The
> opacity-micromap arm stays (4-state, on the foliage scene and the bridge's thin members). Stochastic ray
> LOD is not adopted (section 7).

- **What is wrong now.**
  - There is no LOD chain and no mesh simplifier, so every instance is drawn and traced at full detail at any distance.
  - Terrain rays always trace the finest level. That forces a tight raster LOD limit (0.15 ms per pass), and all four levels of every chunk are built up front: about 1.3 GB of vertex and index data at the maximum terrain size (inferred).
  - There are no far-field stand-ins.
  - Cutouts are resolved by a shader test on every candidate ray hit, reading mip 0.
- **What changes.**
  - LOD chains cooked at import.
  - Raster LOD chosen per instance on the GPU by screen size, with hysteresis.
  - Ray LOD kept stable for the whole world and chosen once per frame for every view, by projected size (the instance's world bounds over the pixel footprint from the primary camera; the smallest footprint across open views), with hysteresis in the same unit; each TLAS entry points at its LOD's BLAS.
  - Self-intersection guarded by a short back-face-culled ray out to the chosen LOD's own simplification error, then the full ray.
  - Far-field stand-ins, only on measured need: built only if Headland's reflection trace, with near-field-only content, misses its section-6 line (1.8 ms for Headland's reflections) and the rays that reach far-field instances (by the projected-size membership rule below) take more than half of that trace time. Then a second TLAS, queried after the near one on a miss (shadow and visibility rays query the far one first, because it is cheaper), membership by projected size, never by metres.
  - Terrain rays go through each chunk's chosen level, or a coarse ray mesh, with a shadow offset for the LOD error.
  - Opacity micromaps: first step, query this laptop's driver for opacity-micromap support with inline ray queries on Vulkan and record it (a gap the research could not close); the arm runs only in the form the driver exposes. Micromaps baked for cutout meshes, in the 4-state form that keeps "unknown" exact, measured on the foliage scene and the bridge's thin members. Any micromap SDK stays inside private implementation files; the shader alpha test stays as the pixel-identical fallback.
  - The alpha test reads the ray-cone mip, with the floor a cutout edge needs.
  - The thin-member fade by projected width (the audit row) stays as written.
- **Owner sees.** Denser assets become affordable. Possibly less sub-pixel flicker, where the owner's look rules allow fading (ISS-4).
- **Touches.** `MeshCook`, `Terrain.cpp`, `Terrain.h`, `AssetManager.cpp`, `cull_lit.rvshader`, `ray_shadow_trace.glsl`, `pbr_fragment.glsl` (4185), the RT world module.
- **Scope.** Rewrite. **Depends on.** RT2-21 (the geometry lane), RT2-35, RT2-37. **Risk.** High.
- **Exit gate.**
  - Per-pixel diffs of rays at the chosen LOD against full-detail rays, on the bridge and the garage: within grain, with no self-shadowing structure; a 60-frame swing diff shows no LOD pop beyond grain.
  - The driver's opacity-micromap support for inline ray queries recorded before the arm is built. The foliage scene from RT2-2c and the bridge's thin members: trace time A,B,B,A with opacity micromaps, and a shadow diff of exactly zero against the shader alpha test (4-state).
  - The second TLAS, only if built: near-field trace time with the far TLAS present equal to near-only within the spread; far shadows and reflections against full-detail rays within grain; total trace time lower than full detail; no seam in a 60-frame swing diff.
  - BLAS, geometry and memory before and after.
  - The bridge's cable band unchanged (ISS-4).
- **Size.** 8-10 sessions.
- **Closes.** GEO-07, GEO-11, ISS-27, the micromap part of ISS-14, the direction for ISS-4, the shadow-offset part of ISS-38.

### RT2-39 · Scattered instances and foliage

- **In plain words.** Today every rock and plant is its own entity, which caps a world at about a million objects and gives wind-animated plants no way into the ray-traced world. This item adds instance sets (one entity holding thousands of placements), wind that animates a few shared prototypes instead of every plant, and a scaling test to a million instances. If the top-level ray structure's rebuild is too slow at that count, tiny instances may be left out of the ray world, but only inside foliage and debris sets and only if shadows and reflections stay clean (D24).

> **Owner's ruling D24 (2026-09-25).** Tiny instances left out of the ray world at huge counts: only inside
> foliage and debris instance sets, and only if the shadow and reflection diffs stay clean.

> **Changed by the comparison (2026-09-25).** The TLAS's scaling is decided at the million-instance test.
> Unreal's early experiments suggested a limit near 100,000 instances for per-frame rebuilds (Lumen
> 2022). The verification added a partitioned-TLAS arm (rebuilding only the changed parts), measured at a
> million moving instances; it is NVIDIA-only on Vulkan today, so it can only ever be an optional
> accelerated path with the portable path kept pixel-identical (section 7, items 8 and 25). *(This
> rejects the comparison's "a partitioned TLAS is not used": the verification ruled that it must be
> measured.)*

- **What is wrong now.**
  - The only way to place many copies of a mesh is one entity each, so a forest, a field of rocks or scattered debris is one entity per object. The ECS caps a world at 1,048,575 entities, so a million-instance scattering cannot exist.
  - Wind-animated vegetation has no path into the ray-tracing structure: its moving vertices would need BLAS refits, and nothing handles that for thousands of plants.
  - No TLAS test goes beyond 120,000 instances.
- **What changes.**
  - Instance sets: one entity holds a compact list of placements (position, rotation, scale, a variation seed) for one mesh. The GPU scene (RT2-34) culls and draws them, and the ray-tracing world module (RT2-35) enters them into the TLAS.
  - Wind as one vertex function, evaluated the same way for raster and for rays: a compute pass animates a few prototype BLASes per species and phase, refit on a budget, instead of one deforming BLAS per plant. Distant plants use a still stand-in (RT2-38's far field).
  - Alpha-tested leaves use RT2-38's opacity micromaps.
  - A TLAS scaling test to one million instances, still and moving.
  - If a full rebuild at a million moving instances misses its budget (TLAS build GPU time above section 6's ray-world upkeep line of 0.5 ms, A,B,B,A): two measured arms. (1) D24's culling: tiny instances inside foliage and debris sets only, by projected solid angle from the primary camera, one global setting (scale-free, no metres); adopted only if the shadow and reflection diffs stay within grain against unculled. (2) A partitioned TLAS (`VK_NV_partitioned_acceleration_structure`, its types in private files): adopted only as an optional path, faster beyond the spread and diff 0 against the portable build.
- **Owner sees.** Dense vegetation and scattered detail become possible, lit and shadowed by rays.
- **Touches.** A new instance-set component, `Scene.cpp` (extraction), the GPU scene, the ray-tracing world module, a wind compute pass, the foliage scene from RT2-2c.
- **Scope.** New capability. **Depends on.** RT2-34, RT2-35, RT2-38. **Risk.** Medium.
- **Exit gate.**
  - The foliage scene at 100,000 and 1,000,000 instances: CPU frame time flat in instance count (within the palindrome spread); TLAS build time recorded, still and with a moving share.
  - Wind: the traced shadow of a swaying plant matches a full-rebuild reference within grain, per pixel.
  - D24's arm, if run: shadow and reflection diffs within grain against unculled.
  - The partitioned-TLAS arm, if run: build and trace times at a million moving instances against the full rebuild, A,B,B,A; diff 0.
  - VRAM by category.
- **Size.** 5-7 sessions.
- **Closes.** The vegetation gap the critic found (no finding id).

### RT2-40 · Streaming and residency

- **In plain words.** Assets load synchronously the first time any code asks for them, textures stay fully resident with every mip, and a game has one scene per process. This item loads everything asynchronously with placeholders and priorities, streams texture mips and mesh levels by what the GPU actually asked for, streams scene cells and levels around the camera, and adds better texture compression. No hitch when something new appears, and worlds larger than video memory become possible.

> **Checked by the comparison (2026-09-25): kept.** BLAS eviction rides on RT2-35's pool; the ray cones of
> RT2-21d write the mip feedback this item streams by; RT2-26's sparse bake bricks stream with the world.

- **What is wrong now.**
  - Assets load synchronously on first use, from inside render code in the middle of a frame.
  - On a warm cache, boot's preparation phase only reads files and throws the bytes away.
  - There are no load states, placeholders, priorities, eviction or partial (mip or LOD) loads, and the asset registry is not safe to use from several threads.
  - A game gets one start scene per process: scenes are single YAML files parsed whole, and there is no runtime level loading.
  - Textures are fully resident with every mip, compressed at BC1 quality at best, with no BC7 or BC6H, and formats are chosen from file names.
  - The file watcher polls, and a change refreshes caches unrelated to it. (RT2-4 already stopped the full re-hash at every launch.)
- **What changes.** Four named steps, each ending in a report:
  - **40a. Requests and load states.** An asynchronous request queue with priorities and cancellation: I/O threads read, jobs decode, and uploads go through RT2-18's ring. Load states with placeholders, reference-counted handles, and eviction under the VRAM and RAM budgets. The watcher moves to OS change notifications and refreshes only the paths it reports.
  - **40b. Partial residency.** Mip streaming driven by feedback from the GPU (the level each texture was asked for, written by the composition and by ray cones), and LOD streaming for meshes. BLASes are built on arrival and evicted with their geometry.
  - **40c. Levels.** Cooked binary scene cells streamed around the camera, and a runtime API to load and unload cells and levels. A binary snapshot for play mode.
  - **40d. The texture cooker** gains BC7 and BC6H, with explicit usage tags. This step depends on nothing else here and can land any time after RT2-4; it changes pixels, so the owner judges it by diff image.
- **Owner sees.** No hitch when something new appears. Worlds larger than VRAM. Sharper compressed textures (40d).
- **Touches.** `AssetManager.cpp`, `AssetRegistry.cpp`, `AssetWatcher.cpp`, `TextureLoader.cpp`, `TextureCook`, `PakFile.cpp`, `SceneSerializer.cpp`, `EditorLayer.cpp` (play mode), `Application.cpp` (boot).
- **Scope.** Rewrite. **Depends on.** RT2-18, RT2-19, RT2-35, RT2-36, RT2-38 (40d: only RT2-4). **Risk.** High.
- **Exit gate.**
  - A scene larger than VRAM: no out-of-memory, no visible pop over a scripted fly-through (diffed against a fully resident reference after it settles), and the 99th-percentile frame time recorded.
  - Boot time before and after.
  - 40d: per-pixel diffs of the demo scenes' textures, judged by the owner, and texture VRAM before and after.
- **Size.** 12-15 sessions.
- **Closes.** CORE-04, CORE-09 (the watcher half), CORE-16, GEO-08, MAT-07, the eviction half of CORE-05, the BC6H part of ISS-37.

### RT2-41 · Large-world precision

- **In plain words.** World positions are 32-bit floats everywhere, and many "whole scene" distances are fixed numbers of metres (10 km rays, a 250 m GI reach), so a world far from its origin shimmers and a world bigger than the numbers breaks. This item forms everything relative to the camera, moves the ray structure and caches with a floating origin, and takes every "whole scene" distance from the scene's own bounds. The current scenes do not change.

> **Added from RT-24's hard-coded-values audit (2026-09-24; section 10 lists every value).** Reflection rays stop at
> 10 km (`tMax` 1e4 m) and the accumulator clamps a miss to 1 km: world-size constants (first pass
> only). General form: the ray length from the scene's own extent (the ray structure's bounds), and a
> miss written as a flag (RT2-23).

> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** Eleven whole-scene
> reaches written in metres (10 km in most rays, the GI reach, the bake solve's reach), and the
> terrain's wall texture scale re-derived from screen derivatives, whose rounding grows with distance
> from the world origin (live on the bridge's Cliff and Headland cameras: the wall texture jumps).
> General form: one reach from the ray structure's bounds; the known texture mapping instead of a
> derivative ratio.

> **Changed by the comparison (2026-09-25).** Ray origins are offset by the float-precision-scaled method (Ray
> Tracing Gems ch. 6), replacing the 2 mm x (1 + slope) literals, coordinated with RT2-21e; every
> whole-scene tMax comes from the distance to leave the TLAS's bounds; the TLAS instances use the floating
> origin. The fixture measurement stays the first arm. Verification correction: that offset covers only
> positions computed from the triangle; positions rebuilt from depth carry larger errors, so RT2-21e (its
> depth-reconstruction term) is a hard prerequisite for removing the metre offsets, not a soft one.

- **What is wrong now.** World positions are 32-bit floats everywhere: in the vertex transforms, the TLAS instances (`VulkanResources.cpp:891-900`, `1256`), the light-cache cells and the histories. RT2-5 fixes the reflector's plane offset, and RT2-21 the ray origins and the position rebuilt from depth; nothing else plans for worlds far from the origin. Ray offsets are fixed metres (`ray_shadow_trace.glsl:245-247`; `pbr_fragment.glsl:2641-2648`), and whole-scene reaches are 1e4 m literals. The current scenes are safe (the bridge spans about 1.35 km either side of the origin), but open worlds span 10 km and more.
- **What changes.**
  - The first arm measures: a fixture placed 10 km and 30 km from the origin, checked for vertex shimmer, shadow acne, reflection reprojection and cache-cell stability, so the work is sized by what actually breaks.
  - Camera-relative rendering: the view and every world matrix are formed relative to the camera on the CPU in double precision, so shaders only see small numbers.
  - The TLAS instances and the cache cells are expressed relative to a floating origin that moves in whole steps with the camera; histories and caches are shifted, not dropped, when it moves.
  - Ray origins by the float-precision-scaled offset (with RT2-21e's reconstruction term for points rebuilt from depth), replacing every metre offset.
  - Every whole-scene tMax from the distance to leave the TLAS's world bounds, replacing each 1e4 m literal (the eleven audit-2 rows).
  - The terrain wall's texture scale from the known mapping, not a derivative ratio.
- **Owner sees.** Nothing in the current scenes. Large worlds render without shimmer.
- **Touches.** `Renderer3D.cpp` (view and instance matrices), `VulkanResources.cpp` (891-900, 1256), the GPU scene, the ray-tracing world module, the light caches, `ray_shadow_trace.glsl`, `pbr_fragment.glsl` (2641-2648, 2984, 3336, 4010, 5422), `direct_trace.rvshader` (494), `irradiance_fill.rvshader` (229, 321), `rtgi_trace.rvshader` (362), `FrameGraphBuilder.cpp` (881), a fixture scene.
- **Scope.** Refactor. **Depends on.** RT2-21 (21e, a hard prerequisite), RT2-25, RT2-34, RT2-35. **Risk.** Medium.
- **Exit gate.** The fixture at 10 km and 30 km: per-pixel diffs against the same content at the origin show no structure beyond grain (shadows, reflections, reprojection, cache cells); the demo scenes diff 0, or within grain where an offset changed, judged by the owner.
- **Size.** 2-3 sessions.
- **Closes.** The large-world half of direct-11; the precision gap the critic found; the comparison's claim E-C16 (section 8).

### RT2-42 · Core fixes: physics limits, skinning lookups, entity handles, script access

- **In plain words.** Small engine-core defects outside the renderer: a compile-time physics body limit that silently stops adding bodies, a skinning lookup that walks the whole asset registry twice per frame, an entity cap that silently aliases entity 0 in Release, and C# component access through text. Each gets its fix. Nothing changes in the demo scenes.

> **Checked by the comparison (2026-09-25): kept unchanged.**

- **What is wrong now.**
  - Jolt's body limit is a compile-time 8,192, and a build that reaches it silently stops adding bodies. Every fixed step scans all bodies for new ones.
  - Skinned parts that name a derived handle walk the whole asset registry twice per frame to find their skeleton.
  - Entity handles cap a world at 1,048,575 entities, and past that a Release build silently aliases entity 0.
  - Generic C# component access goes through text.
- **What changes.**
  - Physics limits come from project settings; spawn and destroy events replace the scan; the lock-free body interface is used.
  - The owning handle is cached, and pose buffers are reused.
  - Passing the entity cap is a fatal error in Release now. Widening the handle is out of scope for this series: scattered content goes into instance sets (RT2-39), and passing the cap stays fatal.
  - C# typed accessors are built only if a script-heavy test scene shows a cost.
- **Owner sees.** Nothing in the demo scenes.
- **Touches.** `PhysicsWorld.cpp`, `Scene.cpp` (1147-1162, 4790-4899), `AssetManager.cpp` (725-869), `ECS.h`, `Interop.cpp`.
- **Scope.** Patch. **Depends on.** RT2-36 (Jolt's threads). **Risk.** Low.
- **Exit gate.**
  - A 9,000-body scene builds every body.
  - Passing the entity cap reports loudly.
  - CPU time of a scene with many animated characters, before and after.
  - Diff 0 on the demo scenes.
- **Size.** 2-3 sessions.
- **Closes.** CORE-12, CORE-13, CORE-17 (measured), core-s3.

### RT2-43 · Delete the per-frame rebuild paths

- **In plain words.** Once the GPU scene and the ray-world module are proven, the old per-frame rebuild paths, the old TLAS refit and the RayShadows code are deleted. Nothing on screen changes.

> **Checked by the comparison (2026-09-25): kept, with three more deletions:** the TLAS refit path, the
> per-frame-slot TLAS array (unless RT2-20c's async arm was adopted), and RayShadows itself.

- **What is wrong now.** Once RT2-34 to RT2-37 are proven, the old paths are dead weight:
  - `RefreshDrawList`'s per-frame rebuilds and the CPU cull-table uploads;
  - RayShadows' per-frame instance list, CPU packing and per-frame hit-table rebuild, and RayShadows itself;
  - the TLAS refit path and the per-frame-slot TLAS array (kept only if the async TLAS build was adopted);
  - the CPU mesh copies in the runtime, and the meshlet path if it was not folded in;
  - the old transform walk, which survives only as `--validate-transforms`.
- **What changes.** All of it is deleted.
- **Owner sees.** Nothing.
- **Touches.** `Scene.cpp`, `RayShadows.*`, `VulkanResources.cpp` (the refit path), `GpuCull.cpp`, `Mesh.*`, `Renderer3D.cpp`.
- **Scope.** Deletion. **Depends on.** RT2-34 to RT2-37. **Risk.** Low.
- **Exit gate.** Demo scenes pixel-identical; no regression on the scale scenes; scenetest green.
- **Size.** 2 sessions.
- **Closes.** The final parts of GEO-01, GEO-02, GEO-04, GEO-14, CORE-02, CORE-07 and ISS-26.

### RT2-44 · The raster fallback on general forms

- **In plain words.** With rays off, the engine falls back to shadow maps, screen-space reflections and screen-space or voxel GI, and audit 2 found about 60 fixed values in them. This item replaces each with its general form, one owner-judged arm at a time, so the fallback behaves the same at any resolution, world size and frame rate. The fallback is kept on par with the RT mode, not improved beyond that: it shares the RT mode's one BSDF and roughness pipeline.

> **Changed by the comparison (2026-09-25).** The audit-2 arms stay as listed. Added: the raster recipe's lit pass
> includes the same BSDF module and roughness pipeline (RT2-21b, RT2-21 21c), so raster and RT stay on par
> (the RT-FIRST parity rule); where the raster recipe still thins shadow rays, it uses Russian roulette
> instead of the order-dependent visibility borrow (the ShadowRayFade rows, listed under RT2-24 in
> section 10). The switch of the shared SSR, SSAO, SSGI and voxel-GI readers to the new lanes and the id
> sentinel happens in RT2-21a, not here; per D1 those shaders fork from the frozen OpenGL set before 21a
> touches them. The raster refraction's push and occluder tolerance are this item's (RT2-32 does not claim
> them).

- **What is wrong now.** Audit 2 (section 10) found about 60 fixed values in the raster recipe, the fallback when rays are off:
  - shadow maps: at most 4 local maps and one set of cascades, not ranked by what they contribute to the view; a fixed depth bias and slope; a 10% spot frustum widening; near planes, a cascade start floor, a radius quantum and a caster pull-back in metres;
  - SSR: a fixed stride, stride growth and iteration cap, a blur disc capped in pixels, a thickness and a maximum distance in metres, a fixed Hi-Z level count, and the hit snapped to a half-resolution texel centre;
  - SSGI and SSAO: biases and a distance falloff in metres, hard normal-agreement switches, fixed blur radii and depth-ratio edge stops, an SSAO rotation that never changes from frame to frame, and taps that fall off screen counted as unoccluded (a crease brightens near the frame edge);
  - voxel GI: normals transformed without the inverse-transpose, textures read at their average, the last voxel write winning, only the sun shadowed in the grid, a cone aperture and step that double-count opacity, cascades snapped finer than the level they are read at, no conservative rasterisation, a voxel size clamp in metres, and the second bounce read from last frame's grid with this frame's origins;
  - the raster water refraction: a fixed push and a foreground test in metres;
  - the lit loop's shadow-ray thinning, which borrows visibility in the order lamps are visited (the ShadowRayFade rows).
- **What changes.** Each value is replaced by the general form in section 10: pixel and texel units from the projection, the light's own size, statistics instead of thresholds, the published algorithm where one exists. Skipped shadow rays by Russian roulette. The raster lit pass on RT2-21b's BSDF and RT2-21c's roughness. One arm at a time, as in RT2-6. Per D1 the OpenGL shader set stays frozen, so these land in the Vulkan raster recipe only.
- **Owner sees.** With rays off: shadows, reflections and ambient light that behave the same at any resolution, world size and frame rate, and materials that shade the same as in RT mode.
- **Touches.** The `ssr_*`, `ssgi_*`, `ssao_*`, `voxel*` and `shadow_depth*` shaders, `cull_casters.rvshader`, `include/voxel_cone.glsl`, `VoxelGI.cpp`, `ShadowMap.cpp/.h`, the raster shadow setup in `Scene.cpp`, the shadow pipelines in `Renderer3D.cpp`, the SSR, SSGI and voxel defaults in `PostSettings.h`, the raster refraction and the lit loop's thinning in `pbr_fragment.glsl` (1329-1360, 4700-4712, 6431-6443).
- **Scope.** Patch. **Depends on.** RT2-2, RT2-6, RT2-21 (21a's fork and lanes, 21c), RT2-21b. **Risk.** Low to medium: raster mode is a fallback, and each arm is judged by diff images.
- **Exit gate.**
  - Each arm: a diff image against before, in raster mode, on the garage, the bridge's three cameras and the camp, judged by the owner.
  - `check_ssao.py` and `check_ssr.py` pass.
  - The same frames at 1600x900 and 2560x1600 show the same effect widths, and a scene scaled by ten shows the same shadows.
  - A,B,B,A.
- **Size.** 4-6 sessions.
- **Closes.** Audit 2's RT2-44 rows (section 10); the raster half of the ShadowRayFade rows.

### RT2-45 · The post chain on general forms

- **In plain words.** Post-processing has its own fixed numbers: bloom and exposure measured in absolute brightness, motion blur that changes length with the frame rate, depth of field that changes with the resolution, and an exposure that adapts at one speed in both directions. This item puts each on a general form, one arm at a time. Two looks are the owner's to pick by eye: the tone curve, from today's ACES fit, AgX and PBR Neutral rendered side by side (D21), and the bloom form (D22).

> **Owner's rulings (2026-09-25).** **D21:** the tone curve: render the garage, the bridge and the camp with
> today's ACES fit, AgX and PBR Neutral side by side; the owner picks by eye. **D22:** bloom: build (a)
> first, today's threshold with a soft ramp, measured after exposure (on what is on screen); render (b)
> beside it, no threshold with a small energy-conserving glow (Unreal's default); the owner picks by eye.

> **Changed by the comparison (2026-09-25).** Every section 10 row stays. Added: SpeedUp and SpeedDown as two
> settings, with the adaptation picking the rate by the direction of the change (today `AutoExposure.cpp:321`
> reads only SpeedUp, so a second setting alone would stay inert; D-N2); every brightness weight (the bloom
> threshold, knee, clamp and outlier weight) reads last frame's exposure from the exposure state buffer, and
> storage is not pre-exposed (RT2-19's arm aside); a gate that exposure converges in the same wall time at
> 30 and 120 FPS; the two owner looks above.

- **What is wrong now.** Audit 2 (section 10) found fixed values in post-processing:
  - auto exposure: the histogram's range is not derived from the owner's exposure limits, and light below 0.0001 is dropped from the metering (both first to fix; the second is live); SpeedUp and SpeedDown both read one setting, and only SpeedUp is used;
  - bloom: its threshold, knee and ceiling are in absolute brightness, not the brightness after exposure (live), and so is its outlier weight;
  - motion blur: the shutter is a fraction of a frame, so the blur length changes with the frame rate (live); "in front" is decided by 0.5 m of depth (live); a fixed tap count;
  - depth of field: a fixed 48 taps, and the bokeh cap in pixels, so it changes with resolution;
  - SMAA: a diagonal margin measured as doing nothing;
  - the sky: a fixed gradient cube size and a dither compensation constant; the clear colour for a colour sky differs from what rays see;
  - the tone curve is a per-channel ACES fit, which shifts the hue of saturated lamps.
- **What changes.** The exposure range from the owner's own limits (MinLog = log2(Key / Max), MaxLog = log2(Key / Min)); bin 0 means "below the range", with no 0.0001 cutoff; SpeedUp and SpeedDown as two settings, the adaptation choosing by direction. Bloom: the Jimenez chain kept, its prefilter on exposure-scaled values (D22's arm (a)), with the thresholdless energy-conserving arm (b) rendered beside it. Motion blur: the shutter as an exposure time in seconds; "in front" decided by relative depth plus the local slope; one tap per pixel of reach. Depth of field: the tap count from the disc's area; the bokeh cap as a share of the frame's height. The SMAA diagonal pass is removed (measured as doing nothing at its margin of 8); `check_smaa.py` stays green. One background for rays and raster; the sky's gradient evaluated analytically at misses; the dither compensation removed. The tone curve: D21's three arms. One arm at a time.
- **Owner sees.** Bloom and exposure that respond the same in dim and bright scenes; motion blur that looks the same at 30 and 120 FPS; depth of field the same at any resolution. Then two side-by-side reviews: the tone curves and the bloom forms.
- **Touches.** The `tonemap`, `bloom_*`, `luminance_*`, `motionblur_*`, `dof_*`, `smaa_*` and `sky` shaders; `PostProcess.cpp`, `PostSettings.h`, `AutoExposure.cpp` (321), `Skybox.cpp`, `FrameGraphBuilder.h` (the clear colour), `FrameGraphBuilder.cpp` (5399-5407).
- **Scope.** Patch. **Depends on.** RT2-2. **Risk.** Low: each arm is a look change the owner judges.
- **Exit gate.**
  - `check_auto_exposure.py`, `check_motion_blur.py` (at 30 and 120 FPS), `check_depth_of_field.py` and `check_smaa.py` pass.
  - Exposure converges in the same wall time at 30 and 120 FPS; up and down adaptation follow their own settings.
  - Motion blur at 30 and 120 FPS gives the same blur length (diff); depth of field at 1600x900 and 2560x1600 gives the same relative blur.
  - Bloom at two light scales, with exposure matched, gives the same picture (diff near zero).
  - Diff images on the three scenes, judged by the owner. **Live reviews:** D21's three tone curves on the garage, the bridge and the camp; D22's two bloom forms.
- **Size.** 4-5 sessions.
- **Closes.** Audit 2's RT2-45 rows (section 10); the comparison's claims D-C8 (the bloom half) and D-C12 and finding D-N2 (section 8).

## 5. Milestones and decisions

No milestone is a big bang. A new path lands behind a global setting or a measurement flag, is proven against the old path by its exit gate, and the old path is then deleted in a named step. The engine works, and scenetest is green, at the end of every item. Items run in the order below, which within M2 and M4 differs from the numbering (RT2-21b opens M2; RT2-46, RT2-12b, RT2-47 and RT2-48 sit where they run; RT2-23f and RT2-23g run after RT2-27, because they need RT2-24's step 1, RT2-25's hit contract and RT2-27's rate tiers).

| Milestone | Items | Sessions | The engine at the end | Flags it brings in | What it deletes |
|---|---|---|---|---|---|
| M0 Trustworthy ground | RT2-0 to RT2-3 | 19-26 | RT-24 fixed and committed; failures loud; VRAM counted by category; validation (and sync validation) clean; one harness that diffs every test camera; a reference renderer that also renders tinted glass, water, fog along every segment and lit particles; the "signal stands alone" command; a ray-traced scale benchmark; many-light, GI, foliage and materials test scenes | `--ray-counters`, `--reference-render`, `--frame-cap`, the signal-stands-alone command | The arms already decided dead; the unread ReflectionScale preset column |
| M1 Correct inputs and early wins | RT2-4 to RT2-9 | 18-24 | Ids exact and stable in integer lanes; samples move in every AA mode, then blue noise; probes shadowed; glass coverage from reflectance; fog to infinity for the sky; no races between frames; the TLAS skipped when nothing changed; the bridge water regression fixed; shader and pipeline caches; small CPU wins | AA arms with rays on (measurement), `--shader-cache`, the blue-noise hash mode | TAA's dead alpha path; the second history counter; RT-22's place-based flicker rules |
| M2 One light model | RT2-21b, RT2-10, RT2-11, RT2-46, RT2-12, RT2-12b, RT2-13 | 25-33 (plus 1-2 if RT2-12b's 2x2 tier is needed) | One BSDF with energy compensation in every path; one light library, analytic light x one ray; tubes shaded as areas at hits; emitters from their own triangles, with the owner's optional link field (D13); lamp shells opaque to rays; one light BVH at every ray hit, measured against the cascaded light grid Doom ships; primary direct light guided by last frame's visible lights; the D12 arm measured | `--light-candidates` (the light BVH against the cascaded-grid baseline, Doom's form), `--area-light-eval`, `--direct-guiding` | S4, WR-17 and the light loop in the RT-mode opaque variant; the 16-slot emitter list; the bounding-rectangle emitters; the emitter-shell pass-through; the bridge-shaped world grid |
| M3 A real frame | RT2-14 to RT2-20 | 39-50 | The compute question answered by three measurements; a pixel-identical frame on a graph that works out barriers, shares memory and holds the ray-structure builds; views own histories; AO as data on the signal tables; bindless binding with a constant ring; uploads off the frame path; a memory budget that degrades instead of crashing; compute ports of the passes that survive M4; compacted ray lists | one flag per graph capability, `--trace-passes=compute` | The old implicit transitions; per-pass descriptor sets; blocking submits on the frame path; FrameDesc's named history fields; the renderer's process-wide statics; record-time layout tracking; the dead history lanes |
| M4 Signals rebuilt | RT2-21, RT2-22, RT2-23, RT2-24, RT2-47, RT2-25, RT2-26, RT2-48, RT2-27, RT2-23f, RT2-23g, RT2-28, RT2-29, RT2-30 | 81-106 | A designed G-buffer with the tangent lane (D27); one compute composition, with the MSAA raster drawing only when MSAA is selected (D26); reflection denoiser v2 measured against NRD (D15), then deterministic hit shading and one specular signal (RT2-23f, RT2-23g); direct light on the MegaLights-shaped denoiser (the visibility ratio as an arm); every diffuse signal standing alone; bounce light in two layers, the live one at High in every view and reading the bake at its hits (D18), within 2.0 ms (D20); sparse, self-placing bake bricks that flag staleness; a lighting cache for particles and back panes; reflections by footprint rate; a final temporal pass that only anti-aliases, with the reflection added after it; render scale with upscaling | `--composition=compute`, `--reflection-denoiser=v2` and `=nrd`, `--realtime-bounce`, `--render-scale` | The RT-mode forward lit raster (outside MSAA); the old accumulator rules; the moving layer, follow-hit and the glass moving layer (each after its arm, D16); the runtime field; the gi_denoise chain; the 4-ray GI level and its clamps; the hand-placed volume atlas; TAA's surface-specific inputs and the TAA-guide copy |
| M5 Glass and water | RT2-31 to RT2-33 | 22-29 | Glass and water in the ray-traced world with tinted shadows; one transmissive layer composited exactly; refraction in screen space with a traced fallback; back panes on one fixed mirror ray (D28); water as a material from one spectrum, the FFT arm judged live (D30); the sea's shine general (D29); fog along every segment; TAA no longer forced, MSAA 4x selectable with rays | `--transmissive-layer` | The second glass reflection chain; the water's private passes, target and flags; the in-draw lobe rays of the back panes; the fog pass after TAA |
| M6 Scale | RT2-34 to RT2-43 | 64-81 | A GPU-resident scene; the ray-tracing world as a module with the shipped TLAS and BLAS policy; a job system; GPU-driven draws; LOD; instance sets for foliage, with D24's culling and a partitioned-TLAS arm measured at a million instances; streaming; large-world precision | `--gpu-scene`, `--job-threads`, `--lod-bias` | The per-frame draw-list, instance and TLAS rebuilds; the TLAS refit path; RayShadows; CPU mesh copies; the dormant meshlet path |
| M7 The fallback and the post chain | RT2-44 to RT2-45 | 8-11 | Raster mode and post-processing on general forms, from audit 2; the raster lit pass on the same BSDF; the owner's tone curve and bloom form (D21, D22) | none | nothing |

Total: about 276 to 360 working sessions, plus 1-2 if RT2-12b's 2x2 tier is needed (RT-SERIES-2 planned 235 to 303). A session is about one solo working day (RT-SERIES's unit). Every item longer than a few sessions names its steps, and each step ends with a report and waits for the owner's green signal.

**The owner's decisions, answered on 2026-09-24 and 2026-09-25.** Each answer is written into the items it touches. D0 to D11 keep their original answer; where a 2026-09-25 ruling changes one, the change is written in its last column.

| Id | Question | The owner's answer | What it changes |
|---|---|---|---|
| D0 | Scope of this series | RT-series 1 closes once RT-24 is committed; RT-22's remaining parts and RT-2.2 move here; this series also counts as the speed pass planned for after the RT series | Speed work happens inside this series, measured at every step; there is no separate pass afterwards |
| D1 | OpenGL's future | Freeze it | OpenGL keeps running the raster path as today, with a shader set that stops changing; the core RHI becomes Vulkan-shaped and new features are Vulkan-only (RT2-15, RT2-18, RT2-36). The shared SSR, SSAO and SSGI shaders fork from the frozen set before RT2-21a changes their lanes |
| D2 | Deferred composition in RT mode (reopened RT-FIRST §4, answer 1) | Allow step 2, RT mode only | RT-2.2 first, then one compute composition replaces the second scene draw in RT mode, kept only if it measures faster with a near-identical picture; materials are not rewritten and the raster mode stays forward+ (RT2-22). **Updated 2026-09-25 by D26:** the second draw is kept for RT mode only when MSAA is selected |
| D3 | TAA and MSAA once rays no longer need TAA | Lift the force; TAA stays the default | After the glass-and-water work, TAA becomes a choice like any other AA mode; with rays it stays the default, and MSAA 4x becomes selectable at full resolution (RT2-33). **Updated 2026-09-25 by D26:** kept; MSAA 4x in RT mode works through a raster drawing that is added only when MSAA is selected and calls the same shading functions as the compute composition (RT2-22). With D28 no TAA exception is left when the force is lifted |
| D4 | How bounce light works | Two layers, the owner's design: a baked pass, with a realtime pass on top that also adds bounce light from Realtime lights | Baked lights keep the bake exactly as today ("baked means baked" stands for them); Realtime lights get live bounce light, in a pass that runs only while one is near the camera; the editor flags a stale bake when a Static object changes (RT2-25, RT2-26). **Updated 2026-09-25:** by D18 the realtime pass runs in every ray-traced view at High (near-only at lower settings), and its rays read the baked lamps' stored light where they land; by D19 it may subtract and add moving objects' effect on baked light (25f), which gives D4's "hits lit by Realtime lights only" one exception, dynamic hit surfaces; by D12 the baked direct light of FullBake and HybridFullBake lamps stays until the owner decides from RT2-12's HalfBake arm |
| D5 | Upscaler and denoiser vendors | The engine's own upscaler, with DLSS and FSR optional | Own TAAU by default; DLSS SR and FSR behind the same setting, their SDKs inside private files; DLSS Ray Reconstruction only as a measured arm (RT2-29). Added 2026-09-25 by D15: NRD's ReBLUR as a comparison arm for the reflection denoiser, never the default |
| D6 | Shader and pipeline caches | Caches on | (Owner, 2026-10-13: the test harness keeps the caches ON -- 0 pixels differ and the key cannot serve a stale shader; `--shader-cache=off` is for timing launches.) The SPIR-V cache keyed on the compiler version and build options, plus a persistent pipeline cache; every measurement runs with `--shader-cache=off` (RT2-9) |
| D7 | A glowing object with no light of its own | Yes, judged first | Every glowing surface without a light becomes a light source, shown as its own arm before it becomes the default; a surface a light owns stays counted once (RT2-11). **Updated 2026-09-25 by D13:** a Light "owns" a glowing mesh only through the optional, hand-set link field; there is no automatic pairing |
| D8 | Frame-rate target | 60 FPS at native 2560x1600 | No upscaling is counted on; the budget in section 6 is re-proposed at native, where the garage must get about 3.6 times faster; render scale stays a fallback setting (RT2-29) |
| D9 | Committing RT-24 while fix 1 leans on TAA | Commit it, with the reliance written down | If arm H cannot remove the reliance, RT-24 is committed anyway and the reliance is removed by RT2-23 and RT2-28 (RT2-0). Arm H was dropped, so the reliance is written down; D17 extends the same approach to a leftover from outside the reflections |
| D10 | The sea work accepted on 2026-09-20 | Wait for the water rework | The early re-land item is dropped; the sea work returns in RT2-32 as the starting point, checked with a moving camera and judged live. **Updated 2026-09-25:** its white shine, which came from an absolute clamp, is replaced by the general relative clamp (D29); FFT waves are an arm judged against its 32-wave sum (D30) |
| D11 | The glass panes behind the nearest | A named exception | Their in-draw reflection rays stay on the old path (the owner's RT-13 decision) and are the one accepted TAA reliance (RT2-13, RT2-31). **Reversed 2026-09-25 by D28:** each pane gets one fixed mirror ray instead; the exception is gone |
| D12 | (2026-09-25) Baked direct light for FullBake and HybridFullBake lamps | Keep the question. "Maybe we will move to half baked once choosing becomes cheaper" | After RT2-12 makes choosing the K lamps cost the same at any lamp count, an arm moves those lamps to HalfBake (live direct light, baked bounce), measured on the bridge with moving cars; the owner decides from it (RT2-12) |
| D13 | (2026-09-25) A fitting's shine counted twice (the Light's highlight plus the glowing mesh's reflection) | Adopt the research fix: glowing meshes are light sources; the direct pass and the reflection pass share the shine by the standard weighting, so every surface shows the mesh's real shape. A Light gets an optional field naming its linked glowing mesh, set by hand; no automatic pairing | RT2-11 (the link field and the specular shares, M2), RT2-23g (the one specular signal), RT2-25 and RT2-26 (the mesh kept out of the bounce and the bake), RT2-2b (the reference applies the same rule) |
| D14 | (2026-09-25) If per-pixel direct lighting is too slow at 2560x1600 | The owner's own design: share the lamp picks and shadow rays within each 2x2 block, cycling which of the 4 pixels fires them, one per frame, so a still camera settles to the exact per-pixel result after 4 frames; softer edges while moving are accepted; the 4 frames are combined in the direct light's own accumulator; it becomes the default if it is good enough | RT2-12b builds it if the full-resolution path misses its bar; the owner judges it live. MegaLights' downsampled sampling is not adopted (section 7) |
| D15 | (2026-09-25) NRD ReBLUR specular as a reference arm | Yes: behind a switch, for comparison only, never the default; denoiser v2 must be at least as good on the same input (NRD runs on any GPU; RTX SDKs licence) | RT2-23's step 23-ref and its gates |
| D16 | (2026-09-25) Delete the moving layer, follow-hit and the glass moving layer | Yes, once denoiser v2 passes its tests, but each is tested first against v2 before it is deleted | RT2-23's step 23e (and RT2-3 no longer deletes follow-hit) |
| D17 | (2026-09-25) Committing RT-24 when the leftover comes from outside the reflections | Commit RT-24 and note the leftover in the roadmap under the item that fixes it | RT2-0's attribution arms and exit gate |
| D18 | (2026-09-25) When the live bounce pass runs | Always at High quality, only near Realtime lights at lower settings; decided after measuring how visible the difference is in the garage. With it on, its rays also read the baked lamps' stored light where they land (the bake gathered per pixel) | RT2-25 (25a's run condition, 25e and its live review), D4, section 6 |
| D19 | (2026-09-25) Moving objects blocking and bouncing baked lamps' light | Adopt it if the diff images improve on today and it costs at most 0.3 ms | RT2-25 25f |
| D20 | (2026-09-25) The live bounce pass's budget at 2560x1600 | A 2.0 ms limit, aiming for 1.5 ms (1.0 ms is below every shipped figure); the extra time comes out of other passes' shares; the final number is set once the pass is measured | RT2-25's cost gate; section 6's lines |
| D21 | (2026-09-25) The tone curve | Render the garage, the bridge and the camp with today's ACES fit, AgX and PBR Neutral side by side; the owner picks by eye | RT2-45 (a live review) |
| D22 | (2026-09-25) Bloom | Build (a) first: today's threshold with a soft ramp, measured after exposure; render (b), no threshold with a small energy-conserving glow (Unreal's default), beside it; the owner picks by eye | RT2-45 (a live review) |
| D23 | (2026-09-25) If the reflection added after TAA is still too grainy after denoiser v2 | Keep working on the reflection's own denoiser; RT2-28 stays open; rule 3 holds (no reflection before TAA) | RT2-23, RT2-28 |
| D24 | (2026-09-25) Tiny instances left out of the ray world at huge counts | Only inside foliage and debris instance sets, and only if the shadow and reflection diffs stay clean | RT2-39 |
| D25 | (2026-09-25) One entity and one BLAS per imported model | No: the artists decide how they build their models | RT2-35 drops the multi-section merge and one entity per placed model; GEO-06 moves to section 7 |
| D26 | (2026-09-25) MSAA in ray-traced mode after RT2-22 | Compute composition is the default; the raster drawing is added only when MSAA is selected, so MSAA 4x stays selectable in RT mode (keeps D3); both paths call the same shading functions, and a test checks they give the same picture except on the outlines MSAA smooths; frame cost only when MSAA is on | RT2-22, RT2-21 (a single-sampled RT G-buffer), D2, D3 |
| D27 | (2026-09-25) Brushed-metal (anisotropic) materials in RT mode | Add it now, without waiting for an asset: the tangent direction carried in the G-buffer and through DirectTrace, ray hits and the composition | RT2-21 (21a's tangent lane, 21d's hit frames), RT2-21b |
| D28 | (2026-09-25) D11: the panes behind the nearest | One fixed mirror ray each (hits shaded by stored light and emissive) instead of TAA-averaged rays, judged live on the garage close-up | RT2-31 (31f), RT2-33, D11 reversed |
| D29 | (2026-09-25) The sea's white shine (an absolute cap, which breaks rule 2) | Replace it with the general relative clamp; if white is wanted, reach it through a physical cause (the lamp's colour, the exposure, the tone curve) | RT2-31 (31d), RT2-32, D10 |
| D30 | (2026-09-25) Sea waves | FFT spectrum waves as an arm, judged live at the pier and Headland against today's 32-wave sum | RT2-32, D10 |

## 6. Performance budget

**Baseline** (measured this week at 1600x900, 1.44 megapixels, rays on, the project's settings; section 1 has the table). Since then RT-24 added the 8-neighbour lamp pool (+2.1 ms) and the one-pass emitter pick (about +0.5 ms) to the garage's reflections, not yet benchmarked together.

**Target** (the owner's choice, D8): 60 FPS at native 2560x1600 (4.10 megapixels), with no upscaling counted on. That leaves a GPU working budget of about 14.5 ms, with about 2 ms kept free: this laptop drifts about 1 ms between back-to-back runs, and the camp rose from 7.19 to 7.96 ms over five minutes on the same build.

**Every per-pass target below is a proposal, not a measured basis.** Nothing in RageV has yet measured, for example, how much of the garage is glossy per tile, which is what the reflection target depends on. So each item's first arm measures, and the owner sets that item's bar from the measurement. The targets show the shape of the budget, and where the time has to come from. The column "2026-09-24" is RT-SERIES-2's proposal, kept for the record; "2026-09-25" is this roadmap's.

**What changed on 2026-09-25 (D18, D20).** The live bounce layer runs in every ray-traced view at High and gets a 2.0 ms limit, aiming for 1.5 ms: the old 1.0 ms line was below every shipped figure at about a million traced pixels (id Tech 8's gather, denoise and upscale take about 1.47 ms at 4K on an RTX 4080; Snowdrop's diffuse ray-traced GI takes 3.88 ms at 1440p on an Xbox Series X). The owner ruled that the extra time comes out of other passes' shares: at the 2.0 ms limit, the garage's reflections give 0.4 ms, its direct light 0.3 ms and the rest of the frame 0.2 ms; Headland's sea gives 0.4 ms, its composition, reflections and direct light 0.2 ms each, and its post 0.3 ms (which also makes room for the translucency cache). In the garage the other passes give back 0.9 ms for the layer's extra 1.0 ms, so the garage's total rises by 0.1 ms, from 14.4 to 14.5 ms, which is the working budget; at Headland they give back 1.3 ms, so its total falls from 14.5 to 14.2 ms (plus up to 0.3 ms while the translucency cache runs). One aim rule holds in both scenes: if the layer lands at its 1.5 ms aim, that 0.5 ms goes back to the reflections and the direct light, so the totals do not change. The final numbers are set once the pass is measured.

**Scaling assumption.** Native 2560x1600 has 2.84 times the benchmark's pixels, and RT-14 measured the traced passes as 90-99% linear in pixel count. So "today, at native" below is the measured number × 2.84. That column is inferred, not measured. At native the garage has to get about 3.6 times faster overall (about 4 times with RT-24's additions), and its reflections about 10 times. If measurement shows that cannot be reached at full quality, the fallbacks are the render-scale setting (RT2-29) and per-signal tiers (RT2-12b's 2x2 tier among them, D14), and the owner sees each trade.

**Garage** (GPU ms):

| Pass group | Measured, 1600x900 | Today at native (inferred) | Proposal 2026-09-24 | Proposal 2026-09-25 | Items that buy it |
|---|---|---|---|---|---|
| Reflections, opaque (trace 3.82, resolve 3.91, accumulate 0.94, three blurs 1.97; since RT-24 also the lamp pool, +2.1, and the emitter pick, about +0.5) | 10.64 (about 12.7 today) | 30.2 (about 36) | 4.0 | 3.6 (classification 0.2, trace with hit shading 1.5, resolve 0.6, temporal 0.5, history fix, blur and stabilisation 0.8; anchors: Snowdrop 2.13 ms at 2160p on an RTX 4080, Lumen Epic 4.62 ms at 1080p on PS5) | RT2-23, RT2-27, RT2-20 |
| Direct light (DirectTrace 3.18, accumulate 0.43) | 3.61 | 10.3 | 3.2 | 2.9 (DirectTrace at most 2.0 of it) | RT2-12, RT2-12b, RT2-24, RT2-47, RT2-20 |
| Opaque lit pass, becoming the composition | 1.29 | 3.7 | 1.0 | 1.0 (the MSAA raster drawing, when selected, costs extra, D26) | RT2-22 |
| Glass layer (eight passes) | 0.83 | 2.4 | 0.5 | 0.5 | RT2-31 |
| TAA (native, no upscale) | 0.40 | 1.1 | 0.8 | 0.8 | RT2-28 |
| Transparent, with the translucency cache when lit see-through draws are in view | 0.39 | 1.1 | 0.4 | 0.4 | RT2-31, RT2-33, RT2-48 |
| G-buffer (plus classification) | 0.26 | 0.7 | 1.1 | 1.1 (59 bytes a pixel) | RT2-21 (more lanes), RT2-20 |
| AO | 0.21 | 0.6 | 0.4 | 0.4 | RT2-20, RT2-47 |
| Bounce light, baked layer | 0 (read inside the lit pass) | 0 | 0.3 | 0.3 | RT2-26 |
| Bounce light, realtime layer (7 Realtime lights) | 0 (off in baked scenes today) | 0 | 1.0, only while a Realtime light is near | 2.0 limit, aiming for 1.5, in every view at High (D18, D20); it includes 25e's reads of the bake and 25f's correction (at most 0.3, D19) | RT2-25 |
| Ray-world upkeep and light BVH (visible time) | not split out | — | 0.5 | 0.5 | RT2-12, RT2-35 |
| Post, UI, change maps, budget chain, the rest | ~0.6 | ~1.7 | 1.2 | 1.0 | RT2-15 (skipping idle passes), RT2-33, RT2-45 |
| **Total** | **18.25** (about 20.9 with RT-24's additions) | **~52** (about 59) | **14.4 with the realtime layer running / 13.4 without** | **14.5 at High (the layer at its 2.0 ms limit, or at its 1.5 ms aim with the 0.5 ms returned to the reflections and the direct light); 12.5 at lower settings with no realtime-owned light near** | |

**Bridge, Headland** (GPU ms):

| Pass group | Measured | Today at native (inferred) | Proposal 2026-09-24 | Proposal 2026-09-25 | Items |
|---|---|---|---|---|---|
| The sea (Transparent 5.58, DirectWaterShade 1.09, DirectWaterChoose 0.58, WaterSurface 0.75, WaterFoam 0.22, WaterAccumulateLamps 0.17) | 8.39 | 23.8 | 4.0 | 3.6 | RT2-31, RT2-32 (including skipping opaque work under the sea) |
| Opaque lit pass, becoming the composition | 2.28 | 6.5 | 1.2 | 1.0 | RT2-22, RT2-32 |
| Reflections, opaque | ~2.95 | 8.4 | 2.0 | 1.8 | RT2-23, RT2-27 |
| Direct light | 1.14 | 3.2 | 2.0 | 1.8 | RT2-12, RT2-12b (67-118 lights a pixel today) |
| G-buffer | 0.74 | 2.1 | 1.3 | 1.3 | RT2-21, RT2-38 |
| TAA (native) | 0.36 | 1.0 | 0.8 | 0.8 | RT2-28 |
| Bounce light, baked layer | 0 | 0 | 0.3 | 0.3 | RT2-26 |
| Bounce light, realtime layer (14 Realtime lights) | 0 | 0 | 1.0 | 2.0 limit, aiming for 1.5 (D18, D20) | RT2-25 |
| AO, fog, glow, post, the rest | ~1.26 | ~3.6 | 1.4 | 1.1 | RT2-33, RT2-45 |
| Ray-world upkeep and light BVH | — | — | 0.5 | 0.5 | RT2-12, RT2-35 |
| Translucency cache, when lit see-through draws are in view | — | — | — | up to 0.3 | RT2-48 |
| **Total** | **17.14** | **~49** | **14.5 with the realtime layer / 13.5 without** | **14.2 at High (the layer at its limit, or at its aim with the 0.5 ms returned to the reflections and the direct light), plus up to 0.3 while the translucency cache runs; 12.2 at lower settings with no realtime-owned light near** | |

**Camp.** 7.60 ms measured, about 21.6 ms at native (inferred). Its four lights are all Realtime and it has no bake, so its bounce light is entirely the realtime layer: that goes from about 5 ms at native (trace 1.22, record 0.22, re-light 0.20 and accumulate at 1600x900) to at most 2.0 ms, aiming for 1.5 (the 2026-09-24 proposal was 1.5 as a target; D20 makes 2.0 the limit), and its reflections from about 7 ms at native to a proposed 1.5. The same items bring it under the budget.

**CPU** (the scale scene, 1280x720; the resolution does not change these):

| Case | Today (measured) | Target | Items |
|---|---|---|---|
| 60,000 objects, rays on | 40.76 ms frame (one run; RT2-2c re-measures it as a palindrome), limited by the CPU: shadow phase 19.98 ms (what inside it costs the time is split by RT2-2f; the ray-tracing work is inferred to dominate), G-buffer recording 11.53 ms, 4.69 ms unaccounted; GPU 7.54 ms | Main thread at most 8 ms; limited by the GPU | RT2-4 (the TLAS skipped when nothing changed), RT2-35 (the ray-tracing share of the shadow phase toward 1 ms; the per-frame hit table), RT2-34 (the per-object instance fill, the draw-list share and extraction by change lists), RT2-43 (the old rebuild paths deleted), RT2-36 (the rest spread across cores) |
| 60,000 objects, rays off | 13.85 ms (G-buffer recording 7.54, shadow phase 3.07); GPU 3.90 | At most 5 ms | RT2-34, RT2-36, RT2-43 |
| Realistic-content scenes at 60k and 120k (from RT2-2) | Not yet measured | Limited by the GPU | Same |
| The light BVH at 10k lights, 10% moving | Not built | Build plus refit at most 0.5 ms of CPU | RT2-12 |

**VRAM.** Histories come to about 440 bytes per pixel by format arithmetic: about 1.8 GB at native 2560x1600. Target: at most 150 bytes per pixel (about 0.6 GB at native), bought by RT2-19's lane contract (the M4 signals must meet it), the dead lanes deleted, the TAA-guide copy gone (RT2-28), and the glass and water histories allocated only while a layer is in view (RT2-31). The G-buffer contract is 59 bytes a pixel (RT2-21). The realtime cache is about 16-32 MB (2^20 entries, an owner setting); the translucency cache is reported by RT2-1's counters. Short-lived targets are aliased (RT2-15), BLASes compacted (RT2-35), and textures, geometry and the bake's bricks live in a fixed streaming pool (RT2-40). Every benchmark report carries VRAM by category from RT2-1 onward.

**Which items buy which milliseconds:**

| Item | Buys | Status of the number |
|---|---|---|
| RT2-1 | The counters' cost, with the counters off | Stale: the recorded 0.26 ms (the showroom, 2026-09-05) and 0.85 ms (Headland, 2026-09-04) predate RT-1; re-measured in RT2-1 |
| RT2-4 | CPU and GPU on frames where nothing moved (the TLAS build skipped); the rebuild-without-refit arm; launch and editor save time | Unmeasured |
| RT2-12 | Hit lighting flat in light count, at every ray hit | Target; the many-light scene decides. Moreau 2019's light BVH: update 0.18-0.89 ms, two-level sampling 2.2-2.6 ms, on an unnamed GPU |
| RT2-12b | DirectTrace flat in light count; the garage's DirectTrace at most 2.0 ms at native; the owner's 2x2 tier (D14) if needed | Target |
| RT2-15 | Batched barriers and skipped idle passes: small; less VRAM for short-lived targets | Unmeasured |
| RT2-20 | Compute ports only on a measured gain; ray lists recover at most the divergence share of RT-9's per-texel cost (about 0.78 of 1.33 ms) | Unmeasured |
| RT2-22 | At least 0.6 ms at Headland at 1600x900 (the material paid twice); the composition from 3.7-6.5 ms at native to about 1 ms; the MSAA raster drawing costs only while MSAA is on (D26) | 0.6 measured (RT-SERIES.md:1692) |
| RT2-23 with RT2-27, RT2-23f and RT2-23g | Garage reflections from about 36 ms at native (with RT-24's pool and pick) to 3.6; Headland from 8.4 to 1.8; RT2-23g removes the lamp pool (+2.1 ms at 1600x900) and the per-ray emitter scoring | Target |
| RT2-25 with RT2-26 | The realtime layer at most 2.0 ms at native, aiming for 1.5, in every view at High (D18, D20); the baked layer about 0.3 ms; the camp's all-realtime bounce from about 5 ms at native to at most 2.0 | Target; shipped anchors: id Tech 8 about 1.47 ms at 4K (RTX 4080), Snowdrop 3.88 ms at 1440p (Xbox Series X) |
| RT2-29 | Not counted: render scale is a fallback under D8's native target. At 0.75 per axis, every per-pixel pass handles about 44% fewer pixels | RT-14's linearity is measured |
| RT2-31 with RT2-32 | The sea at Headland from about 24 ms at native to 3.6; garage glass from 2.4 to 0.5; no glass histories in scenes without glass in view | Target |
| RT2-34 | The per-object instance fill and extraction grow with changes, not objects: most of the G-buffer recording's 11.5 ms and the shadow phase's draw-list share (about 3 ms) at 60,000 objects | Target; the attribution is inferred until RT2-2f splits the phases |
| RT2-35 | The ray-tracing share of the shadow phase toward 1 ms at 60,000 objects, and the per-frame hit table (probably the ~4 ms rays add to the G-buffer recording); no TLAS build on still frames | Target; the share is measured by RT2-2f first |
| RT2-37 | Fewer commands and binds as distinct meshes grow; occlusion culling where it pays. Not credited with the G-buffer recording's 11.5 ms (the scene already goes out in about 20 indirect draws) | Unmeasured |
| RT2-46 | Rays that cross a lamp fitting no longer run the candidate loop | Unmeasured |
| RT2-47 | The shared diffuse passes at most 0.1 ms parked and 0.6 ms at the owner's swing, all diffuse signals together (the measured-change records reported apart) | Target |
| RT2-48 | At most 0.3 ms while lit see-through draws are in view, 0 otherwise | Target |

The budget is a target, not a promise. Every line is measured by its item's exit gate. Where a line misses, the matching global lever (the render scale, a signal's tier, the rays per pixel) trades quality for time, and the owner sees the trade.

## 7. Deliberately not doing, and why

1. **A separate RT renderer, or tracing primary visibility.** What the camera sees first stays rasterised (RT-FIRST §0): it gives the same answer for far less cost.
2. **A path-traced game mode on this laptop.** Only the reference mode (RT2-2) is built, as a truth instrument.
3. **DLSS Ray Reconstruction as the default denoiser.** It locks in one vendor. It is a single shared temporal system, which conflicts with every signal owning its accumulator. It needs uncorrelated samples, while RageV uses low-discrepancy sequences (and, after RT2-7b, blue noise; the RR arm uses RT2-7b's hash mode). It is allowed only as a measured A/B arm (D5, RT2-29c).
4. **A neural radiance cache.** NVIDIA-only and experimental.
5. **ReSTIR as the base of direct light.** RT-10 measured its stages further from the truth. The MegaLights talk (2025) adds the shipped reasons: reuse has a fixed cost, and the correlation it brings works against the denoiser (talk lines 190-289). The only reuse arm in this plan is spatial-only pooling with pairwise MIS in RT2-12b; temporal reservoir reuse stays out. The light BVH (RT2-12) with MegaLights-style guiding (RT2-12b) is the base.
6. **ReSTIR GI in the game path.** Rejected twice as tangled up with the denoiser. No new reason was found to reopen it.
7. **A visibility buffer as the default.** RageV's scenes are measured as bound by pixels and rays, not geometry. It can be an arm on dense scenes after LOD (RT2-38), only if geometry becomes the wall. That is the end state MAT-01 proposes, which is not scheduled. Revisit trigger (2026-09-25): after RT2-38, only if RT2-2's pass timings show the G-buffer plus the composition over their section-6 lines on some scene. RT2-21's contract does not care who writes it, so a visibility buffer would change only the writer (Hable's data has it losing on large triangles, like the garage's and the camp's).
8. **Cluster acceleration structures (RTX Mega Geometry).** NVIDIA-only, and they need a cluster LOD chain first. An arm only after RT2-38, with a portable fallback kept pixel-identical. (The partitioned TLAS, the other half of Mega Geometry, is measured as an arm at a million instances in RT2-39.)
9. **Displaced micro-meshes and work graphs.** The first is deprecated by NVIDIA; the second is not available on NVIDIA's Vulkan.
10. **Keeping every emitter shell out of shadow rays by instance mask** (GPU-11's proposed test). Already measured wrong: the back wall came out seven display levels too bright. (RT2-46 is a different change: the shells stay in the shadow rays and block light; only an aimed ray stops short of its own sample point.)
11. **Any rule keyed to a surface type.** The water exemption was rejected. RT2-28's behaviour rule replaces the rules keyed on location.
12. **Packing the G-buffer on theory.** RT-14 measured that the G-buffer is not where the memory goes. Lanes change only by diff image (RT2-19, RT2-21).
13. **Hi-Z occlusion culling by default.** It was dismissed for the open bridge without a measurement. It is measured on an occluded scene in RT2-37 and kept only if it pays.
14. **Async compute, and RT pipelines with SER, by default.** Adopted pass by pass, only on a measured gain on this laptop (RT2-20).
15. **Collapsing the raster GI fallbacks (voxel GI, SSGI).** The owner's rules keep a raster source for every signal, on a par with RT (RT-FIRST §4, answers 2 and 5). Only reference arms move behind engine flags.
16. **Froxel volumetrics, night display finishers, the bloom audit, foam** (the rest of WR-5, WR-6, WR-11, WR-12, WR-14). They stay in the WR series; RT2-33 does only their prerequisites. The owner decides when the WR series resumes. WR-11's design is recorded (2026-09-25) so it can start without stopping for decisions: froxels that share the translucency cache's sampling and shadow rays (RT2-48, MegaLights' hybrid, about 25% saved), with their own jittered 3D accumulator, applied through RT2-33's fog include.
17. **HDR display output** (GPU-16). Low priority beside the RT core; it comes after this series.
18. **Hair, eyes, subsurface scattering, decals** (most of MAT-15). RT2-21 adds the shading-model id so they can come later. None is built without content that needs it.
19. **Typed C# component accessors** (CORE-17). Only if a script-heavy test scene shows a cost.
20. **Frame generation.** It raises the displayed frame rate, not the rendered one, so it is not frame time.
21. **The visual-script loader's data loss** (ISS-39). Not renderer work. Because it loses user data, a small separate task is recommended now.
22. **Rejected finding frame-14** (the target pool "reallocates whenever the camera starts or stops"). The skeptic refuted it: the measured-change passes run whether the camera moves or not, so nothing is reallocated at a start or a stop. The real, small effect is a one-off resize when a setting changes, which RT2-15's aliasing removes.
23. **Claims corrected by the skeptics, which this roadmap does not act on:**
    - the TLAS is not rebuilt from scratch every frame (it is refit);
    - the instance masks are used, by the bake solve and the subtractive shadow (`RT-SERIES-2-FINDINGS.md:4392` says they are ignored and is wrong; RT2-1 corrects it);
    - the editor's game view does not build a second TLAS;
    - MSAA 4x is dormant by the project's own choice, not because rays force TAA;
    - the 16-emitter cap is not what limits the bridge, whose lenses are all one entity; its real error is the bounding rectangle's power (gi-s3, in RT2-11);
    - the 4K reflection-cost figures are straight-line extrapolations;
    - a fragment pass can also skip idle tiles, by drawing one quad per tile, so compute is adopted on a measured benefit, not assumed.
24. **Particles and decals in the ray-traced world.** Particles stay out of the ray structure: they are lit from the cache (the translucency cache, RT2-48; read in RT2-33), and rebuilding thousands of camera-facing sprites into a BLAS every frame would cost more than smoke's reflections are worth. This is revisited if a scene needs fire that shows in mirrors. Decals come with the shading-model groundwork (RT2-21) and are not built without content that needs them.
25. **Testing on a second GPU vendor.** Only this laptop's NVIDIA GPU is available. Validation, sync validation and RT2-1's spec fixes (the swapchain usage flag, the scratch alignment) stand in for now, and every vendor-specific feature (SER, cluster acceleration structures, DLSS) keeps a portable path that is pixel-identical. A run on an AMD or Intel card is recommended whenever one is available.

Added 2026-09-25 from the comparison with the top engines; each carries the trigger that would reopen it:

26. **MegaLights' downsampled light sampling** (4 picks per 2x2 block at half resolution, a stochastic bilinear upsample). Its own talk says it does not converge to the full-resolution result and softens shadows. The owner's own 2x2 design (D14), which does converge on a still camera, is the cost tier instead (RT2-12b). Revisit trigger: the owner's tier misses its gates.
27. **MegaLights' log-luminance light weighting and its exposure-relative sample drop.** Both depend on absolute brightness (rule 2); RageV's linear score, equal to the shaded term, is scale-free. Revisit trigger: none while rule 2 stands.
28. **A screen-space ray march before the ray query in RT mode** (from the reflections comparison). Visibility stays ray-traced; only the lighting of on-screen hits is reused from the screen (RT2-23f). Snowdrop's own slides show seams between its screen and world results and false hits behind thin objects, and the march saves traversal time, which RT2-27's rate rule and stored light address without those errors. Revisit trigger: RT2-14's split shows traversal, not hit shading, dominating ReflectionTrace, and RT2-27 misses its bar.
29. **Surface caches and lightmaps for bounce light.** Lightmaps need a second UV set, which the mesh vertex does not have (`Mesh.h:17-22`); Lumen's card-based surface cache needs no UVs (cards are projections) but needs card placement and a runtime capture pass. Neither is built, for cost and scope: RT2-25e gives baked lamps contact-scale bounce from the volume bake. Revisit trigger: contact-scale bleeding stays above grain after RT2-25e and RT2-26.
30. **Radiance cascades, including Split Radiance Cascades.** Research, about 13 ms. Revisit trigger: a published real-time cost within RT2-25's 2.0 ms at about a million traced pixels.
31. **Neural irradiance volumes and Activision's Neural Light Grid.** Research, static lighting only. Revisit trigger: a shipped title with dynamic lights.
32. **Per-light bake layers for switchable baked lights.** No shipped precedent, and switchable lights are Realtime by the owner's rule. Revisit trigger: the owner asks for switchable baked lights (D12's HalfBake arm may make it moot).
33. **Linking NVIDIA's SHaRC library.** Its update pass is shaped for a path tracer and it needs fp16 and 64-bit atomics; RageV writes its own cache in the same structure (RT2-25c), with SHaRC's shaders as a reference. Revisit trigger: RT2-25c misses its gates.
34. **Screen-space tracing, or last frame's lit screen, in the GI gather.** Under D4 the previous lit frame holds the baked lights' light, so a realtime-layer hit that read it would count baked light twice. Revisit trigger: RT2-25e adopted at High (the gather then owns the baked bounce wherever it has an estimate), as a measured arm. (Reflection hits already reuse the screen, RT2-23f.)
35. **A multi-pass a-trous spatial stage (SVGF, ReLAX) for the diffuse signals.** One sparse, rotated pass instead (RT2-47), as MegaLights ships. Revisit trigger: RT2-47's young-fill gate fails with one pass.
36. **Noise masks tuned to RageV's own filters as the default.** The published filter-adapted masks are RT2-7b's baseline; tuned masks are an arm only, because RageV's filters adapt. Revisit trigger: the tuned-mask arm wins both of RT2-7b's gates.
37. **Pre-exposed history storage**, except as RT2-19's `R11G11B10F` arm. Revisit trigger: that arm is adopted.
38. **Local exposure.** Left to the WR series. Revisit trigger: the owner asks.
39. **A partitioned TLAS as the default.** NVIDIA-only on Vulkan; measured only as an arm in RT2-39. Revisit trigger: a KHR or EXT version exists and RT2-39's full rebuild at a million moving instances misses its budget (TLAS build GPU time above section 6's ray-world upkeep line of 0.5 ms, A,B,B,A).
40. **`VK_KHR_ray_tracing_position_fetch`.** Hit positions already come from the ray's distance, and normals and UVs still need the vertex buffers. Revisit trigger: a pass that needs a hit triangle's positions without its vertex buffers.
41. **`VK_EXT_descriptor_heap` and descriptor buffers.** Descriptor indexing plus device addresses is enough (RT2-18). Revisit trigger: descriptor indexing's limits are hit, or a measured CPU gain with the driver's support confirmed on this laptop.
42. **Stochastic ray LOD.** It needs accumulation to hide its dither. Revisit trigger: RT2-38's discrete LOD pops beyond grain in the swing diff.
43. **Primary surface replacement through glass (PSTR).** Thin panes already show the surface behind them lit by the full pipeline, and water uses screen-space refraction of the lit backdrop (RT2-31). Revisit trigger: a thick refractor enters a scene.
44. **MBOIT or AVBOIT as the default for the back layers.** The exact front layer handles the dominant case in RageV's content; AVBOIT is a measured arm in RT2-31 (it was cheaper than weighted blending in Activision's table). Revisit trigger: the arm wins, or the glass-over-glass fixture's back-layer error is above grain.
45. **A per-frame displaced water BLAS, or cluster acceleration structures for water.** View state inside world state, an unmeasured rebuild at kilometre scale, and NVIDIA-only. The rest surface stands in (RT2-31a). Revisit trigger: RT2-31's bridge diff shows the stand-in's error above grain.
46. **Hillaire's sky atmosphere.** Deferred until a daytime scene; its volumetric shadows lean on TAA. Revisit trigger: a daytime scene.
47. **RTX Remix's froxel ReSTIR as the base for fog.** Its 20 m range is a scale dial and its 128-frame histories lag; WR-11 shares RT2-48's sampling instead. Revisit trigger: WR-11 resumes and the shared sampling misses its gate.
48. **NRD's "denoising-free" dithered glass.** It leaves its dither to TAA (rule 3). Revisit trigger: none while rule 3 stands.
49. **Heterogeneous volumes.** No content needs them. Revisit trigger: content that does.
50. **EON diffuse (energy-preserving Oren-Nayar).** Its roughness is a separate diffuse-roughness input that RageV's materials do not have, and Lambert is the shipped baseline. Revisit trigger: a material that authors a diffuse roughness; then it is an owner-judged arm in RT2-21b's module.
51. **One entity and one BLAS per imported model** (GEO-06). The owner's ruling D25: the artists decide how they build their models. Revisit trigger: the owner asks, or RT2-35's scale runs show per-part costs dominating after RT2-34.

## 8. Appendix: every verified finding, mapped

Verdicts: C = confirmed, P = partly confirmed, S = added by the skeptic.

**GPU backend and RHI**

| Finding | V | Item | Note |
|---|---|---|---|
| GPU-01 | P | RT2-18, RT2-35 | Upload ring and transfer queue; BLAS builds through the manager |
| GPU-02 | P | RT2-14, RT2-20 | The A/B first (RT2-14), then the ports and tile lists |
| GPU-03 | C | RT2-35 | |
| GPU-04 | P | RT2-18 | |
| GPU-05 | P | RT2-18, RT2-20 | Queues and timeline semaphores; the async experiment |
| GPU-06 | P | RT2-18, RT2-36 | Record-time state removed; recording timed before being parallelised |
| GPU-07 | P | RT2-15 | |
| GPU-08 | P | RT2-9 | Caches on (D6) |
| GPU-09 | C | RT2-1, RT2-19, RT2-15 | Measuring; budget; aliasing |
| GPU-10 | C | RT2-18 | |
| GPU-11 | P | RT2-20 | Experiments, fp16 in the accumulators included; the emitter-shell mask test is not done (section 7, item 10) |
| GPU-12 | C | RT2-18 | OpenGL frozen (D1) |
| GPU-13 | C | RT2-1 | |
| GPU-14 | P | RT2-21, RT2-22 | RT mode: a single-sampled G-buffer (D26); the raster recipe: integer lanes resolved by sample zero, a sample-zero shader for float lanes |
| GPU-15 | C | RT2-1 | |
| GPU-16 | C | Not doing | Section 7, item 17 |
| rhi-s1 | S | RT2-1 | |
| rhi-s2 | S | RT2-1, RT2-17 | Checks; one definition |
| rhi-s3 | S | RT2-35 | |

**Frame orchestration**

| Finding | V | Item | Note |
|---|---|---|---|
| frame-01 | P | RT2-15 | |
| frame-02 | C | RT2-17, RT2-16 | |
| frame-03 | P | RT2-7, RT2-47, RT2-28 | Sample service; every diffuse signal standing alone; generic mask |
| frame-04 | P | RT2-34, RT2-35, RT2-36 | |
| frame-05 | P | RT2-19, RT2-16, RT2-15 | |
| frame-06 | C | RT2-29 | |
| frame-07 | C | RT2-17 | |
| frame-08 | C | RT2-17 | |
| frame-09 | C | RT2-15 | |
| frame-10 | C | RT2-22 | |
| frame-11 | P | RT2-20, RT2-15 | |
| frame-12 | C | RT2-18, RT2-15 | |
| frame-13 | C | RT2-17, RT2-1, RT2-3, RT2-30 | |
| frame-15 | C | RT2-16 | |
| frame-16 | C | RT2-17, RT2-21 | |
| frame-17 | C | RT2-15 | |
| framegraph-s1 | S | RT2-0 | |
| framegraph-s2 | S | RT2-4, RT2-16 | |
| framegraph-s3 | S | RT2-4, RT2-17 | |
| frame-14 | refuted | Not doing | Section 7, item 22 |

**Direct lighting**

| Finding | V | Item | Note |
|---|---|---|---|
| direct-01 | P | RT2-12, RT2-12b | The primary-pixel sweep moved to RT2-12b |
| direct-02 | P | RT2-6, RT2-12 | The world grid at hits is an owner-judged arm (exact only on average) |
| direct-03 | P | RT2-11 | Glowing objects with no light become lights, judged first (D7) |
| direct-04 | P | RT2-24 | Radius from the penumbra, not from G-buffer edges |
| direct-05 | P | RT2-10 | |
| direct-06 | C | RT2-10 | |
| direct-07 | C | RT2-7 | |
| direct-08 | C | RT2-12 | |
| direct-09 | P | RT2-21, RT2-22 | RT mode: a single-sampled G-buffer, MSAA through the raster drawing (D26) |
| direct-10 | C | RT2-21b | The one BSDF, moved to M2 |
| direct-11 | P | RT2-21, RT2-41 | Geometric normal, scale-aware offsets, camera-relative positions; the rest of large-world precision |
| direct-12 | P | RT2-12 | |
| direct-13 | P | RT2-35, RT2-17 | |
| direct-14 | C | RT2-2 | |
| direct-15 | P | RT2-13, RT2-31, RT2-3 | The transparent half with RT2-13's second half, in RT2-31 |
| direct-16 | C | RT2-12b | One sweep with K stratified picks |
| direct-17 | C | RT2-4 | |
| direct-18 | P | RT2-6 | It moves pixels, so it is an owner-judged arm |
| direct-s1 | S | RT2-10 | |
| direct-s2 | S | RT2-0, RT2-24 | The split arm; the structural fix |
| direct-s3 | S | RT2-12 | |
| direct-s4 | S | RT2-13, RT2-31 | Lit from the translucency cache in RT2-31 (31f) |

**GI, AO, probes and baking**

| Finding | V | Item | Note |
|---|---|---|---|
| GI-01 | P | RT2-25, RT2-26 | Two layers (D4) |
| GI-02 | C | RT2-25 | |
| GI-03 | P | RT2-26 | `check_gi.py`'s bands updated |
| GI-04 | C | RT2-6, RT2-25, RT2-27 | Capture after the TLAS is built; capture through the traced world only if the rough tier still reads a probe (RT2-27's conditional step) |
| GI-05 | C | RT2-25 | |
| GI-06 | P | RT2-6, RT2-12 | |
| GI-07 | P | RT2-25 | |
| GI-08 | P | RT2-11 | |
| GI-09 | C | RT2-21b, RT2-25, RT2-26 | RT2-21b provides the BSDF's specular term behind a flag; RT2-25 decides whether hits use it (gather and bake alike); RT2-26 re-bakes with what RT2-25 adopted |
| GI-10 | C | RT2-6 | Owner decides |
| GI-11 | C | RT2-25 | |
| GI-12 | P | RT2-4, RT2-26 | The wasted solve texture; sparse streamed bricks |
| GI-13 | P | RT2-1, RT2-26, RT2-30 | |
| GI-14 | P | RT2-26 | |
| GI-15 | C | RT2-3 | |
| gi-s1 | S | RT2-11, RT2-25, RT2-26 | The link field and the specular half in RT2-11; the bounce half in RT2-25, the bake half in RT2-26 (D13) |
| gi-s2 | S | RT2-6 | |
| gi-s3 | S | RT2-11 | |
| gi-s4 | S | RT2-2 | |
| gi-s5 | S | RT2-4 | |

**Reflections, denoising and TAA**

| Finding | V | Item | Note |
|---|---|---|---|
| REFL-01 | P | RT2-0, RT2-28 | Bisection first; structural fix later |
| REFL-02 | P | RT2-5 | |
| REFL-03 | C | RT2-0, RT2-23 | |
| REFL-04 | C | RT2-0, RT2-21, RT2-23 | |
| REFL-05 | C | RT2-0, RT2-23 | The camera-motion check as an arm; the content checks in the rewrite |
| REFL-06 | C | RT2-0, RT2-11 | The emitter-slot diagnostic; the emitter table |
| REFL-07 | C | RT2-0, RT2-27 | Patch; then trading within a budget (RT2-23's step 23d, merged into RT2-27) |
| REFL-08 | P | RT2-27, RT2-20 | |
| REFL-09 | P | RT2-31 | |
| REFL-10 | C | RT2-0, RT2-28 | |
| REFL-11 | P | RT2-23 | The resolve's weights are left alone (R11 measured them) |
| REFL-12 | P | RT2-23, RT2-30, RT2-3 | |
| REFL-13 | P | RT2-6 | It moves pixels, so it is an owner-judged arm |
| REFL-14 | P | RT2-2 | |
| REFL-15 | C | RT2-23 | |
| REFL-16 | C | RT2-6 | |
| REFL-17 | P | RT2-5 | |
| reflections-s1 | S | RT2-7 | |
| reflections-s2 | S | RT2-27 | |
| reflections-s3 | S | RT2-19 | |
| reflections-s4 | S | RT2-6 | |
| reflections-s5 | S | RT2-28 | |

**Materials, lit shader, G-buffer, transparency, water, particles**

| Finding | V | Item | Note |
|---|---|---|---|
| MAT-01 | P | RT2-22 | The visibility-buffer end state is not done by default (section 7, item 7) |
| MAT-02 | C | RT2-21b, RT2-21 | The BSDF half in RT2-21b; the hit-detail half in 21d |
| MAT-03 | C | RT2-31 | |
| MAT-04 | P | RT2-21, RT2-22 | RT mode: a single-sampled G-buffer, MSAA through the raster drawing (D26) |
| MAT-05 | P | RT2-20 | |
| MAT-06 | P | RT2-7, RT2-31, RT2-32 | The sea mirror with the water rework (D10); the back panes are a named exception (D11) |
| MAT-07 | P | RT2-40, RT2-18 | |
| MAT-08 | P | RT2-22 | |
| MAT-09 | P | RT2-13, RT2-22 | |
| MAT-10 | C | RT2-32, RT2-3 | |
| MAT-11 | P | RT2-1, RT2-9, RT2-17, RT2-21 | |
| MAT-12 | P | RT2-21, RT2-5 | |
| MAT-13 | P | RT2-33 | |
| MAT-14 | C | RT2-18 | OpenGL frozen (D1) |
| MAT-15 | C | RT2-21, RT2-21b | Groundwork, plus anisotropy in RT mode now (D27); the rest is section 7, item 18 |
| MAT-16 | P | RT2-6, RT2-33, RT2-48 | Particles lit from the translucency cache |
| MAT-17 | P | RT2-6 | |
| materials-s1 | S | RT2-6, RT2-22 | |
| materials-s2 | S | RT2-6, RT2-33 | |
| materials-s3 | S | RT2-5 | |
| materials-s4 | S | RT2-32 | |

**Geometry, acceleration structures, scale**

| Finding | V | Item | Note |
|---|---|---|---|
| GEO-01 | C | RT2-34, RT2-43 | |
| GEO-02 | P | RT2-34, RT2-43 | |
| GEO-03 | C | RT2-2 | |
| GEO-04 | C | RT2-4, RT2-35, RT2-43 | The patches available today land early |
| GEO-05 | C | RT2-35, RT2-18 | |
| GEO-06 | C | Not doing (D25) | Section 7, item 51: the artists decide how models are built; the per-part CPU cost falls with RT2-34 |
| GEO-07 | P | RT2-38 | |
| GEO-08 | C | RT2-40 | |
| GEO-09 | C | RT2-37 | |
| GEO-10 | C | RT2-21 | |
| GEO-11 | C | RT2-38 | |
| GEO-12 | C | RT2-4 | |
| GEO-13 | C | RT2-35 | |
| GEO-14 | P | RT2-37, RT2-43 | |
| GEO-15 | C | RT2-12 | |
| GEO-16 | C | RT2-35 | |
| GEO-17 | P | RT2-5 | |
| GEO-18 | C | RT2-3, RT2-37 | |
| geometry-s1 | S | RT2-5 | |
| geometry-s2 | S | RT2-5 | |

**Engine core**

| Finding | V | Item | Note |
|---|---|---|---|
| CORE-01 | P | RT2-36 | OpenGL frozen (D1) |
| CORE-02 | C | RT2-34, RT2-4, RT2-43 | The race is fixed early |
| CORE-03 | C | RT2-18 | |
| CORE-04 | C | RT2-40 | |
| CORE-05 | C | RT2-1, RT2-19, RT2-40 | Measuring; budget; eviction |
| CORE-06 | C | RT2-17, RT2-35 | |
| CORE-07 | C | RT2-34, RT2-4, RT2-43 | The particle guard is fixed early |
| CORE-08 | P | RT2-0, RT2-34 | The `Slider` test mover; the engine's interpolation (an owner-judged arm) |
| CORE-09 | C | RT2-4, RT2-40 | Change detection early; the watcher later |
| CORE-10 | C | RT2-2, RT2-1 | |
| CORE-11 | C | RT2-2, RT2-4, RT2-36 | Frame cap; the minimised loop; input order, time and pacing |
| CORE-12 | C | RT2-42, RT2-36 | |
| CORE-13 | P | RT2-42 | |
| CORE-14 | C | RT2-4 | |
| CORE-15 | C | RT2-17, RT2-1 | |
| CORE-16 | C | RT2-40 | |
| CORE-17 | P | RT2-42 | Only if a measurement shows a cost |
| core-s1 | S | RT2-4 | |
| core-s2 | S | RT2-4 | |
| core-s3 | S | RT2-42 | |
| core-s4 | S | RT2-2 | |

164 verified findings are mapped: 162 to items (four of them also partly to "not doing": GPU-11, MAT-01, MAT-15 and CORE-17), and GPU-16 and GEO-06 to "not doing" (GEO-06 by the owner's ruling D25, 2026-09-25). The refuted frame-14 is listed in section 7. The comparison's own findings follow.

### Findings from the comparison with the top engines (2026-09-25)

The verification phase re-checked every claim the comparison made. Its ids: **A-C1** and so on are its claim ids (defined in `build/rt_research/phase3_items_<area>.json`, areas A to G as in section 11, and listed with their verdicts in `docs/RT-ROADMAP-RESEARCH.md`, part 7 of each area); **A-N1** and so on are its new findings, numbered in the order `build/rt_research/phase3.json` lists them (part 9 of each area there). `build/` is not in git; the research file carries the text. Verdicts: confirmed, partly (true with a correction), refuted, plausible (needs its measurement), unsupported (the source could not be read). Claims about what the top engines ship, and the "RageV is better" and "could beat both" claims, are placed in section 11 and in `docs/RT-ROADMAP-RESEARCH.md`; the claims about RageV's own code, and every new finding, are mapped here.

**Claims about RageV's code**

| Claim | Verdict | What it says | Item |
|---|---|---|---|
| A-C1 | confirmed | DirectTrace picks sized and tube lights by a different (unwidened) lobe than it shades them with; sized spheres too | RT2-10 |
| A-C2 | confirmed | The trace-only passes walk every light; lamps at hits are points with hard rays | RT2-12, RT2-10 |
| A-C7 | confirmed | The direct light's specular is stored as shaded light with a 4-frame memory and a 3x3 box; no diffuse spatial pass | RT2-24, RT2-23g |
| A-C8 | confirmed | Shadow rays return only 0 or 1, with no occluder distance | RT2-10, RT2-24 |
| A-C9 | confirmed | Soft-shadow points are uniform on a disc and along the tube, and frozen without TAA's jitter | RT2-7, RT2-10 |
| A-C10 | confirmed | Emitters are bounding rectangles above strength 1; DirectTrace samples none; GI picks evenly | RT2-11 |
| A-C13 | confirmed | The lamp pool's MIS weights are canonical; its result is clamped at 64 per channel | RT2-6 (arm 5), RT2-23g |
| A-C14 | partly | The world grid's cell counts are fixed at 8x4x32 (its box does follow the lights); both grids are sorted on the CPU per view | RT2-12 |
| B-C1 | confirmed | Reflection hits keep one random light | RT2-23f |
| B-C2 | confirmed | A tube's shine is counted twice, with no weighting | RT2-11 (D13), RT2-23g |
| B-C3 | confirmed | The virtual image's depth is a mean with misses at 10 km, re-averaged, clamped and blended at the old position | RT2-23 (23a) |
| B-C4 | confirmed | Draws below the horizon become the mirror direction; the written pdf is always the old sampler's | RT2-23 (23a) |
| B-C5 | confirmed | The accumulator picks one history candidate; the identity test runs only when an instance moved | RT2-23 (23b) |
| B-C6 | confirmed | The lamp pool: pairwise MIS over up to 8 neighbours, +2.1 ms | RT2-23g |
| B-C7 | confirmed | The filtered roughness reaches the analytic lights only | RT2-21 (21c) |
| B-C8 | confirmed | The blur's fixed gates and hard roughness cut; the propagated variance is never read | RT2-23 (23c) |
| B-C9 | partly | The reflection passes are full-screen fragment passes (the budget and change passes are smaller); the budget runs only while an instance moves | RT2-20, RT2-27 |
| B-C10 | confirmed | Hits read every texture at mip 0 | RT2-21 (21d), RT2-20 |
| B-C11 | partly | Three audit values were found by the audit but lost before section 10 | Section 10 (rows added) |
| C-C1 | confirmed | RTGI High is up to 4 rays per traced pixel at a quarter of the pixels | RT2-25 (25b), RT2-1 |
| C-C2 | confirmed | Under a bake, Realtime lights bounce nothing | RT2-25 |
| C-C3 | confirmed | Frame hits keep only the field's baked direct light; gather hits use one unoccluded probe | RT2-25 (25d) |
| C-C4 | confirmed | Trace-only passes read every light's cull record | RT2-12 |
| C-C5 | partly | The GI upsample loses normal-map detail; SH is a gated fix, not a proven one | RT2-25 (25b) |
| C-C6 | confirmed | Relocation helped one leak fixture and hurt the sun-lit ones | RT2-26 |
| C-C7 | confirmed | The field is two L2 SH sets plus a visibility tile; the comments describe an older design | RT2-26 |
| C-C8 | confirmed | Bake hits get unoccluded sky light | RT2-25 (25d), RT2-26 |
| C-C9 | partly | One UV set rules out lightmaps; Lumen's cards need no UVs | Section 7 (item 29) |
| C-C10 | confirmed | The bake stamp cannot see moved objects or edited materials | RT2-26 |
| C-C11 | confirmed | The GI pass picks among emitters evenly | RT2-11, RT2-25 |
| C-C17 | partly | Rays ending in the field won on the fixture and lost on the showroom's bright bay | RT2-25 (25e) |
| D-C1 | confirmed | The reflection is filtered twice (its accumulator, then TAA) | RT2-28 (28c) |
| D-C2 | partly | The diffuse signals have no young-pixel blur; the reflection keeps its variance blur | RT2-47, RT2-23 (23c) |
| D-C5 | confirmed | TAA's box is min/max, widened up to 8x on rough dielectrics | RT2-28 (28b) |
| D-C6 | partly | TAA's 2% depth test; the sky test is dead under reverse-Z (crossings are refused under the id reason) | RT2-28 (28a, 28e) |
| D-C7 | confirmed | No velocity dilation | RT2-28 (28a) |
| D-C8 | confirmed | No pre-exposure; brightness weights on absolute light | RT2-28 (28b), RT2-45, RT2-19 |
| D-C9 | partly | No AO change record; the check costs about 0.94 ms moving at the owner's Quality preset | RT2-47 |
| D-C10 | partly | No diffuse outlier stage; GI clamps each ray absolutely instead | RT2-47, RT2-25 |
| D-C11 | confirmed | Plane tolerances in metres, in three copies | RT2-47, RT2-23 |
| D-C12 | confirmed | SpeedUp and SpeedDown share one setting | RT2-45 |
| D-C17 | confirmed | TAA's history is 64 bytes a pixel | RT2-28 (28d), RT2-19 |
| E-C1 | confirmed | The TLAS refits with changed contents; every build carries ALLOW_UPDATE | RT2-4, RT2-35 |
| E-C2 | confirmed | The TLAS is built every frame; the key is computed after the build | RT2-4 |
| E-C3 | confirmed | Static BLASes are built lazily with a blocking submit and never compacted | RT2-35 |
| E-C4 | partly | Ray passes are fragment passes (the lit pass also traces; more passes are compute than listed) | RT2-14, RT2-20 |
| E-C5 | confirmed | Every emitter instance is non-opaque | RT2-46 |
| E-C6 | confirmed | The instance masks are used | RT2-1 (the findings text), section 7 (item 23) |
| E-C7 | confirmed | Shadows off means no rays | RT2-17 (17d), RT2-35 |
| E-C8 | partly | RT-9's per-texel allocation gave no picture gain; about 0.55 of its 1.33 ms is the rays | RT2-20 (20b) |
| E-C9 | confirmed | Skinned BLASes are refit forever, one at a time | RT2-35 |
| F-C1 | confirmed | The roughness floor doubles as the "no surface" marker | RT2-21 (21a) |
| F-C2 | confirmed | The filtered roughness reaches the analytic lights only | RT2-21 (21c) |
| F-C3 | confirmed | Three Smith terms | RT2-21b |
| F-C4 | confirmed | No energy compensation | RT2-21b |
| F-C5 | confirmed | Hit diffuse lacks the (1 - F) term | RT2-21b |
| F-C6 | confirmed | The cook discards normal-map variance and filters roughness in the wrong space | RT2-21 (21c) |
| F-C7 | confirmed | The scene target is 53 bytes a pixel (44 without the transparent pass) | RT2-21 (21a) |
| F-C8 | partly | The indirect lane is dead only while ray-traced GI is on | RT2-21 (21a) |
| F-C9 | confirmed | Ids in half floats; per-frame pool rows; a folded struck identity | RT2-5 |
| G-C1 | confirmed | Glass coverage follows the reflection's brightness | RT2-6 (arm 15) |
| G-C2 | confirmed | No BLAS carries the no-duplicate flag | RT2-31 (31a) |
| G-C3 | confirmed | An unordered transmittance product is exact for visibility rays | RT2-31 (31a) |
| G-C4 | partly | The sea's white shine is the absolute cap (the accepted accumulator is on the parked branch) | RT2-31 (31d), RT2-32 (D29) |
| G-C5 | confirmed | Fog after TAA; the sky fogged at the far clip; no fog at hits | RT2-6 (arm 16), RT2-33 |
| G-C6 | confirmed | The sea's rays are traced per quad in the draw; the lane walks only under jitter | RT2-7, RT2-31 |
| G-C7 | confirmed | The back panes' 1-8 lobe rays are re-seeded every frame | RT2-31 (31f, D28) |
| G-C8 | partly | The glass histories are 168 bytes a pixel and allocated without glass | RT2-31 (31c), RT2-19 |
| G-C9 | confirmed | Glows and particles are drawn before glass and water | RT2-6 (arm 4), RT2-33 |
| G-C15 | partly | The 0.32 wave-speed factor is new to the audits (the refraction push was already in audit 2) | RT2-32, section 10 |

**New findings**

| Finding | What it says | Item |
|---|---|---|
| A-N1 | DirectTrace's K light picks are also frozen without jitter | RT2-7 |
| A-N2 | RTXDI ships the same temporal gradient as measured change, and filters it first | RT2-24 (step 1), section 11 |
| A-N3 | The score and shading disagree for every sized light, not only tubes | RT2-10 |
| A-N4 | Hit specular is point-light GGX with no widening | RT2-10 |
| A-N5 | The sea's neighbour merge combines reservoirs without MIS (biased) | RT2-12b |
| A-N6 | Moreau 2019's light-BVH table names no GPU | RT2-12 (its cost gate decides) |
| B-N1 | NRD's README documents measured change as its confidence input | RT2-23 (23b), section 11 |
| B-N2 | Emitters are rectangle stand-ins above strength 1; the reflection's MIS is only approximate | RT2-11 |
| B-N3 | Snowdrop fades and dithers its roughness seam; its lengths are in metres | RT2-27, section 11 |
| B-N4 | Lumen 2022's emissive future work is about its GI gather, not reflections | Section 11 |
| B-N5 | The bake field stores irradiance, so it cannot answer a glossy lobe | RT2-27 |
| B-N6 | The lamp pool is also skipped at mirror texels and tiny discs | RT2-23g |
| B-N7 | Other audit values marked "other" or "unclear" may have been lost | Section 10 (re-checked; rows added) |
| B-N8 | RT-9's tile map is read at the current texel, never reprojected | RT2-27 |
| B-N9 | Snowdrop traces its reflections at quarter resolution | Section 6 (the anchor), research document |
| B-N10 | MegaLights' authors say only that separating shadowing from shading "is not correct" | Section 11 |
| C-N1 | The one real-scene test of rays ending in the field lost to realtime | RT2-25 (25e), RT2-2 (2e) |
| C-N2 | The field-read cost notes are stale (L1 era) | RT2-26, RT2-25 (25e) |
| C-N3 | Bake hits also add the flat ambient unoccluded | RT2-25 (25d), RT2-2 (2e) |
| C-N4 | The "tile 9 carries the visibility bits" comment is stale | RT2-26 |
| C-N5 | NRD recommends the same re-traced gradient | Section 11 |
| C-N6 | DDGI ships per-probe change detection | RT2-25 (25c) |
| C-N7 | The GI ray count is a per-tile ceiling under the budget | RT2-25 (25b), RT2-1 |
| C-N8 | Under the project's baked default, GI's measured change and emitter sampling never run | RT2-25 (D18), section 11 |
| C-N9 | The GI change record costs about 0.59 ms moving at Quality | RT2-47 |
| C-N10 | The garage's baked probe froze Realtime lights into the baked frame's ambient | RT2-25 (25a) |
| C-N11 | AC Shadows ships a baked GI path as an alternative | Section 11 |
| C-N12 | The AC Shadows talk could not be read | Research document (gaps) |
| D-N1 | The reflection composite's header says the opposite of the code | RT2-6 (arm 12), RT2-28 (28e) |
| D-N2 | SpeedDown is never read | RT2-45 |
| D-N3 | GI clamps each ray against a fixed absolute brightness | RT2-47, RT2-25 |
| D-N4 | The change check costs about 0.94 ms moving at the owner's Quality preset | RT2-47, RT2-28 |
| D-N5 | The reflection's variance blur runs every frame by default | RT2-23 (23c) |
| D-N6 | TAA restarts a pixel by the larger of the two change maps | RT2-28 (28d) |
| D-N7 | NRD's disocclusion already predicts the previous depth, with a slope term | RT2-28 (28a) |
| E-N1 | The TLAS is one per frame in flight, each with its own refit counter | RT2-4, RT2-35 |
| E-N2 | Any skinned mesh changes the TLAS key every frame; no pose check before a refit | RT2-4, RT2-35 |
| E-N3 | Measured change is on by default | RT2-4 |
| E-N4 | The freed-BLAS check runs only with validation on | RT2-35, section 11 |
| E-N5 | `RT-SERIES-2-FINDINGS.md:4392` is wrong about the masks | RT2-1 |
| E-N6 | The forward lit pass traces shadow rays itself | RT2-14, RT2-12, RT2-22 |
| E-N7 | RT-9's per-texel allocation had a worse picture metric | RT2-20 (20b) |
| E-N8 | Unreal 5.8 still rebuilds its TLAS every frame | RT2-35, section 11 |
| E-N9 | Snowdrop's separate lighting passes save 2% of the whole signal | Section 11 |
| F-N1 | HDRP has ray-cone mips at hits but no specular filtering there | RT2-21 (21c, 21d), section 11 |
| F-N2 | HDRP's ray tracing supports no MSAA | RT2-22 (D26), section 11 |
| F-N3 | The reflection's same-object test compares per-frame pool rows | RT2-5 |
| F-N4 | The G-buffer variant returns before the indirect lane's write | RT2-21 (21a) |
| F-N5 | The cited error figures are for a different specular filter | Research document |
| F-N6 | A roughness map with an unrecognised name is cooked as colour | RT2-21 (21c) |
| G-N1 | Glass and water are not in the ray structure at all | RT2-31 (31a) |
| G-N2 | The presets' ReflectionScale column is never read | RT2-3 |
| G-N3 | The sea's cap also clamps glass's in-draw rays | RT2-31 (31d) |
| G-N4 | The glass histories are allocated in scenes without glass | RT2-31 (31c), RT2-19 |
| G-N5 | Weighted blending is the most expensive OIT in Activision's table | RT2-31 (31b), section 7 (item 44) |
| G-N6 | AO's opaque-flag rays would be fully blocked by glass in the TLAS | RT2-31 (31a) |
| G-N7 | Particles are unlit, and GPU simulation is off by default | RT2-48, RT2-33 |
| G-N8 | The bridge's fog cap (0.42) does not compose over segments | RT2-33 |

## 9. What changed: after the critic's review, and after the comparison

A critic read the first draft against every input and found no missing issue or finding, but a set of wrong facts, ordering problems and owner rules bent without asking. Each point was checked against the code before it was applied. All of them held up, and all were applied. The items were renumbered into the new order.

**Facts corrected.**
- RT-24's live stop-go motion comes from the `Slider` script (the car and the moving light), not from `Orbiter.cpp`, which only runs under `spin_measure.py`'s pinned clock (RT2-0).
- The world-grid lookup at ray hits is exact only on average, not pixel for pixel, because the grid's lists are sorted by brightness and RT-11's reservoir draws in walk order. It became an owner-judged arm with a statistical gate (RT2-6).
- The 20 ms shadow phase at 60,000 objects is not known to be the ray-tracing instance list; that is inferred. RT2-2 now splits it before anything is designed against it. The G-buffer pass's 11.5 ms is credited to the per-object instance fill (RT2-34) and the hit table (RT2-35), not to GPU-driven draws (RT2-37).
- The ray counters' recorded costs predate RT-1 and are re-measured. The half-float id arithmetic, the glass "coverage" figure (really a share of changed pixels), the jitter-scale-0 case, the pass count and the list of uncommitted files were corrected.

**Order changed.**
- Nothing starts until RT-24 is committed; RT2-1 no longer runs beside it.
- RT2-0 carries its own fallback arms (a fast-history clamp and young-pixel fill, the struck-object check under camera motion, TAA memory set by the reflection's history length, an emitter-slot diagnostic), so no RT-24 symptom waits for a rewrite.
- Cheap work that needs no rewrite moved early: the behaviour-based flicker floor that fixes the bridge water regression (RT2-8), the shader and pipeline caches (RT2-9), the TLAS skip and asset change detection (RT2-4), VRAM measurement (RT2-1), the compute A/B ahead of the render graph (RT2-14).
- The final temporal pass keeps a generic motion lane for see-through layers until they have their own accumulators; D3 waits until then; the RHI rewrite waits for signals-as-data and moves the work outside the graph onto explicit barriers first; the 64-per-channel cap removal is gated by the bright-speck count.

**Put to the owner instead of decided.** D9 (committing RT-24 while fix 1 leans on TAA), D10 (the sea work already accepted: now or later), D11 (the back glass panes' reliance on TAA), MSAA 4x's place (folded into D3), and the TAA feedback values, which are the owner's project settings.

**Split into reportable steps.** RT2-2, RT2-15, RT2-17, RT2-18, RT2-23, RT2-25, RT2-26, RT2-31, RT2-34, RT2-35, RT2-36 and RT2-40 now name their steps, and every step ends with a report. RT2-4 keeps only fixes that should not change the picture; five that do moved to RT2-6's owner-judged arms.

**Gates tightened.** spin_measure gains the spot and grain scores RT2-0's gate relies on, and RT2-0 gains a cost gate and a camp check. The reference renderer's scope is defined (the full material at every path vertex, transmission and media before glass and water are judged against it). The direct light's shadow-edge bar is re-measured rather than taken from an old figure. The light-count, latency, MSAA, glass-cost and bridge gates are named precisely. Section 6's targets are marked as proposals the owner sets from each item's first measurement.

**Added for AAA scale.** A baseline harness that diffs every test camera in one command (RT2-2a), instance sets and wind for foliage (RT2-39), large-world precision (RT2-41), one entity per placed model (RT2-35; removed 2026-09-25 by D25), probes captured through the traced world (RT2-25; moved to RT2-27 as a conditional step, 2026-09-25), sparse streamed bake bricks and cells that move out of walls (RT2-26; the moving of cells out of walls is now a measured arm, 2026-09-25), frame pacing (RT2-36), animated emissive textures kept current (RT2-11), and reasons for not putting particles and decals in rays or testing on a second vendor (section 7).

**After the owner's answers (2026-09-24).** The twelve answers were written into their items. Three reshaped the plan. The item that would have re-landed the sea work early was dropped, because the sea work now returns with the water rework (D10); the items after it were renumbered. Bounce light was redesigned around the owner's two-layer idea (D4): RT2-25 keeps baked lights on the bake and adds a live pass for Realtime lights on top, and RT2-26 makes a stale bake visible in the editor. The target moved to native 2560x1600 (D8), so section 6 was re-proposed at native and render scale became a fallback (RT2-29). Lifting the forced TAA became the last step of M5 (D3, RT2-33).

**After the comparison with the top engines (2026-09-25).** Every RT2 item received a verdict from the comparison, corrected by the verification and then by the owner's 19 answers. The result, item by item:
- **Kept** (unchanged apart from a plain-words opening and a note): RT2-3 (one dead preset column added), RT2-9, RT2-16, RT2-34 (the emitter flag folded into its slot), RT2-36, RT2-37, RT2-40, RT2-42, RT2-43 (three more deletions).
- **Amended:** RT2-0, RT2-1, RT2-2, RT2-4, RT2-5, RT2-6, RT2-8, RT2-10, RT2-11, RT2-14, RT2-15, RT2-17, RT2-18, RT2-19, RT2-22, RT2-23, RT2-24, RT2-25, RT2-26, RT2-27, RT2-28, RT2-29, RT2-30, RT2-31, RT2-32, RT2-33, RT2-35, RT2-38, RT2-39, RT2-41, RT2-44, RT2-45.
- **Split:** RT2-7 (steps 7a and 7b), RT2-12 (the primary-pixel sampling became RT2-12b), RT2-13 (13a stays; 13b moved into RT2-31), RT2-20 (steps 20a, 20b, 20c), RT2-21 (the BSDF became RT2-21b in M2; steps 21a, 21c, 21d, 21e stay), RT2-23 (after the second critic's review, its steps 23f and 23g became the items RT2-23f and RT2-23g, run after RT2-27).
- **Merged:** RT2-23's step 23d into RT2-27 (one classification and one ray-rate rule for reflections). No whole item was merged.
- **Deleted:** none.
- **New items:** RT2-46 (lamp shells opaque to rays again; proposed as "RT2-11b", renumbered because new items take numbers from 46 upward), RT2-47 (every diffuse-kind signal stands alone), RT2-48 (the lighting cache for particles and see-through surfaces). **New steps:** RT2-2's 2h, RT2-6's arms 15 and 16, RT2-23's 23-ref, 23f and 23g (the last two now the split items RT2-23f and RT2-23g), RT2-25's 25e and 25f.
- **Proposals of the comparison rejected, and why** (each item says so too): the automatically derived link between a Light and its glowing mesh (the owner ruled for a hand-set optional field, D13); one entity and one BLAS per imported model (D25); MSAA offered in the raster recipe only (the owner kept it in RT mode through a raster drawing, D26); waiting for an asset before carrying anisotropy (D27); MegaLights' downsampled sampling as the cost tier (the owner's own 2x2 design instead, D14); RT2-24's specular visibility ratio (the lamps' shine goes to the one specular signal, and the verification found that form the weakest); merging the translucency cache with RT2-25's cache (the two store different quantities); "a partitioned TLAS is not used" (the verification required it measured as an arm).
- **Corrected by the verification before being written in:** measured change is level with NRD's and RTXDI's documented practice, not ahead of it, and it does not replace the history tests; the "rays ending in the bake" arrangement lost on the showroom's bright bay, so its gate includes that camera and its cost is priced from today's field read; relocation is an arm, not a promised fix; the bake field stores irradiance, so reflections read stored light at their hits, not in mid-air; the reflection added after TAA cannot give a zero diff while its weight rides through the pass, so the gate allows the weight's own edges; NRD's disocclusion already predicts the previous depth, so its form is built first; the published FAST masks are the noise baseline, not a later improvement; glass and water must first enter the ray structure at all, and AO would be blocked by them without its own mask; the fog cap does not compose over segments; Moreau's light-BVH timings name no GPU; the half-float normal is coarser than HDRP's over half its range.
- **Section 7** gained entries 26 to 51, each with a revisit trigger. **Section 8** maps every claim about RageV's code and every new finding to its item. **Section 10** gained the rows the verification found missing (three values lost on the way to the table, the others found on re-reading the audit's file, and three values in the water), and two rows moved with step 23d. **Section 11** is new: how RageV compares with the top engines, area by area. The evidence is in `docs/RT-ROADMAP-RESEARCH.md`.


**After the second critic's review (2026-09-25).** A second critic read this roadmap against the same inputs and listed 29 issues; each was checked against the inputs before it was applied. The main changes:
- **Order.** RT2-23's steps 23f (hit shading) and 23g (one specular signal) became the split items RT2-23f and RT2-23g, run after RT2-27, because they need RT2-24's step 1, RT2-25's hit contract and RT2-27's rate tiers; RT2-23's step 23e's tile classification is built on RT2-20b's mechanism, and RT2-27 adds the ray rate to it. RT2-46's aimed rays use today's ray-origin offset in M2, and RT2-21e replaces it. RT2-11 now states what it delivers in M2 (the power-heuristic shares between DirectTrace's mesh sample and the reflection's lobe ray); the bounce and bake halves of D13's single count, with their gates, moved to RT2-25 and RT2-26.
- **Baseline.** RT2-12's rule-1 comparison arm is the cascaded light grid Doom: The Dark Ages actually ships (camera-centred, built on the GPU); RageV's own 8x4x32 grid is only the interim at hits. RT2-12 grows by one session for it.
- **Numbers.** RT2-24's and RT2-32's cost gates use section 6's 2026-09-25 lines (2.9 ms with DirectTrace at most 2.0, and 1.8 ms; the sea at 3.6 ms). The garage reflection line's split adds up to its 3.6 ms. One aim rule for the live bounce layer: at its 1.5 ms aim, the 0.5 ms goes back to the reflections and the direct light, and the garage's total is 14.5 ms either way.
- **Gates.** "Within grain", "near zero", "display level" and the parked noise floor are defined in "Words this document uses", each tied to its instrument. Gates on one isolated signal compare with that signal's own many-sample reference, captured the same way, not with the whole frame's 494 and 8.83. RT2-2's step 2g has a gate; RT2-24's step 2 has a gate for the failure that made MegaLights drop the ratio split; open thresholds are stated (RT2-12's node count, RT2-12b's hidden share, RT2-25c's eviction, RT2-38's far-field trigger, RT2-39's rebuild budget).
- **Decisions.** GI-09's hit specular is decided only in RT2-25; the SMAA diagonal pass is removed; widening the entity handle is out of scope; the fog cap's new meaning gets a named live review (RT2-33); SER and opacity-micromap support are queried on the laptop's driver before either arm is built (RT2-20c, RT2-38); RT2-25e's live review states what happens if the owner sees no difference.
- **Wording.** Every step label now carries its own item's number (RT2-15's 15a to 15e, RT2-17's 17a to 17d, RT2-18's 18a to 18e, RT2-23's 23a to 23e and 23-ref, RT2-34's 34a to 34d, RT2-40's 40a to 40d; the notes carried over from RT-SERIES-2 were relabelled the same way). Two claims the verification had not checked were removed or marked. RT2-25c's cell size is called CellPixels, because K is the number of lamps picked per pixel.

## 10. The hard-coded-values audit: every value, and where it goes

RT-24's audit (2026-09-24) read every fixed value in the reflection chain and the frame filter (`reflection_trace`, `reflection_resolve`, `reflection_accumulate`, `reflection_blur`, `reflection_composite`, `taa_resolve` and their C++ settings) against the owner's rule: general solutions only, nothing that depends on speed, frame rate, scale, the scene or a surface. Of 262 values, 170 are fine: maths, pixel units or numerical guards. The other 92 break the rule on paper. The 53 of those with a path to a symptom were checked a second time by a skeptic, who also found 5 the first pass missed; the other 39 were flagged by the first pass only. Evidence: `build/rt24/audit_phase1.json` and `audit_phase2.json`.

Every value found is below, with where the roadmap handles it. **Live** means it affects today's picture. **Latent** means it breaks the rule without a visible effect today. **First pass only** means it was not checked a second time. The general form for each is in the note at the top of the item named.

| Value | Where | What it does | Found | On the roadmap |
| --- | --- | --- | --- | --- |
| **Frame filter** (`taa_resolve`) |  |  |  |  |
| `kDepthTolerance` (2%) | taa_resolve | "Same surface": this frame's depth against last frame's | Live. At speed most of the floor and both poles fail it and show the raw frame: edge jitter | RT2-28 (added) |
| No-history exits | taa_resolve | A pixel with no usable history showed the raw jittered sample | Fixed in RT-24 (change 1) | RT2-28 (done early) |
| `kStillWithinTexels` (0.003 texel) | taa_resolve | "Is this pixel still" | Latent. A precision guard written as a number | RT2-28 and RT2-6 (added) |
| `kStableWiden` (8) | taa_resolve | Widens the colour box on rough non-metals | Latent. Small smear effect at fast speed | RT2-28 (the problem was listed; general form added) |
| `kMomentFrames` (16) | taa_resolve | Memory of the box's own flicker estimate | Latent | RT2-28 (added) |
| `Compress`/`Expand` (1/(1+Y)) | taa_resolve | Brightness curve used when averaging | Latent. Tied to absolute brightness, not the exposure | RT2-28 (added) |
| `kNormalTolerance` (26 degrees) | taa_resolve | Normal test for "same surface" | Fine. World-space normals do not change with speed or scale | none needed |
| **Reflection memory** (`reflection_accumulate`, `Renderer3D`) |  |  |  |  |
| Floor that shrinks with travel (RT-24 fix 2) | reflection_accumulate | The fewest frames a reflection keeps | Live. At speed it sets the memory to one frame, so the one-ray grain reaches the screen | RT2-23 (added; change 3 tried and reverted) |
| Travel hyperbola | reflection_accumulate | Shortens memory with travel | Latent | RT2-23 (added); RT2-30 deletes |
| `SmearTexels` (6) | Renderer3D.h | Memory limited to 6 texels of travel | Live at the slow start and end of a drag | RT2-23 (added) |
| `slack` (0.25 floor, 0.25 mirror factor) | reflection_accumulate | Travel that halves the memory; mirrors forget 4x faster | Latent, and the mirror rule is backwards | RT2-23 (added) |
| `MovingMemory` (8) | Renderer3D.h | Ceiling on memory while moving | Fixed for reflections (now 1, which does nothing); its comment is wrong | RT2-23: delete it (added) |
| `Memory` (64) | Renderer3D.h | Full memory | Latent | RT2-23 (added) |
| `SilhouetteMemory` (4) | Renderer3D.h | Memory floor at silhouettes | Latent (first pass only) | RT2-23 (added) |
| Gloss ramp (roughness 0 to 0.3) | reflection_accumulate | Splits mirror from rough for the memory rules | Latent | RT2-23 (added) |
| Moving layer: 12 spreads, slack, floor | reflection_accumulate | Bound and memory of the moving layer (RT-15) | Latent (first pass only) | RT2-23 (added; the layer's fate is 23e's) |
| **Reflection trust tests** (`reflection_accumulate`) |  |  |  |  |
| `kMirrorSensitivity`, `kRoughSensitivity`, halfway ramp | reflection_accumulate | How far the reflected direction may turn | Latent (the last two first pass only). Also runs on view-independent signals | RT2-23 and RT2-24 (added) |
| `kMirrorHitTolerance`, `kRoughHitTolerance`, `kHitAgree`, ramp to 3x, 0.25 m floor | reflection_accumulate | Hit-distance agreement | Latent (the last three first pass only) | RT2-23 (added) |
| `kHitIdentityAgree` (0.2), `kHitNormalAgree` (0.9), 0.05-0.3 ramp | reflection_accumulate | Struck-object test (RT-17) | Latent (first pass only) | RT2-23 (added) |
| `kMaterialAgree` (0.15), `kIdAgree` (0.25), id ramp, plane edge (0.35), facing ramp (0.8-0.94) | reflection_accumulate | Match confidence | Latent (first pass only) | RT2-23 (added) |
| Facing refusal | reflection_accumulate | Refuses a history facing elsewhere | Latent | RT2-23 (added) |
| Roughness refusal (0.5) | reflection_accumulate | Refuses a history of another roughness | Latent | RT2-23 (added) |
| Staged roughness thresholds (0.15/0.5, 0.5/1.5 texels) | watch_arm.py arm | Trust by lobe difference | Fixed in RT-24: change 2 does it with no numbers | RT2-23 (done early) |
| Silhouette tests (cosine 0.8; 0.05 m + 1%) | reflection_accumulate | Where a silhouette skips the tests | Latent. In metres | RT2-23 (added) |
| History plane tolerance (0.05 m + 1%) | reflection_accumulate | Refuses a history off the plane | Latent (first pass only). In metres | RT2-23 (added) |
| Curvature allowance (0.15 per metre) | reflection_accumulate | Absorbs normal rounding | Latent | RT2-23 (added), with RT2-21's signed curvature |
| `kCurvedMover`, `kSurfaceNearer` | reflection_accumulate | Choice between two histories on movers | Latent. Per metre | RT2-23 (added); RT2-30 deletes the choice |
| `kMotionFloor` (1/8 texel) | reflection_accumulate | "Moved on its own" | Latent. Rounding trips it at speed | RT2-6 (added) |
| **Reflection bounds** (`reflection_accumulate`, `Renderer3D`) |  |  |  |  |
| Glossy end: 12 spreads, 0.08-0.3 ramp | reflection_accumulate | Colour box width on glossy surfaces | Latent | RT2-23 (added) |
| `BoundWidth` (3) | Renderer3D.h | Colour box width | Latent | RT2-23 (added) |
| `kSettledBound` (2), `kSettledFrames` (32) | reflection_accumulate | Box widening once settled | Latent (first pass only) | RT2-23 (added); RT2-6 arm 12 fixes its counter; RT2-30 deletes it |
| `kMomentFrames` (16) | reflection_accumulate | Moments' memory | Latent (first pass only) | RT2-23 (added) |
| Firefly clamp width (3 spreads) | reflection_accumulate | Caps a sample at 3 spreads of its neighbours | Fine. Relative to the texel's own neighbours; 3 is a stated convention | RT2-6 arm 7 already reworks the clamp |
| **Reflection image distance** (`reflection_accumulate`, `reflection_trace`) |  |  |  |  |
| Image-distance memory (cap 8, 1:1 blend) | reflection_accumulate | Smooths the reflection's depth | Latent | RT2-0 arm F and RT2-23's step 23a (were there); general form added |
| Miss as 10 km, clamped to 1 km; multi-ray mean distance | reflection_trace, reflection_accumulate | A miss written as a distance | Latent | RT2-23's step 23a (was there); general form added |
| **Direct light** (`Renderer3D`, `reflection_accumulate`) |  |  |  |  |
| `PairMemory` (4) | Renderer3D.cpp | The lamps' own highlights keep 4 frames | Live. A trail of stale streaks on the chrome poles at speed | RT2-24 (added) |
| `Fewest` (1 to 4) and the diffuse floor of 4 | Renderer3D.h | Floor under the direct light's memory | Live, through `PairMemory` | RT2-24 (added) |
| `MaxRadius` (8 texels) | Renderer3D.h | Blur reach of every diffuse-kind signal | Latent (first pass only) | RT2-24 (added) |
| **Reflection sharing** (`reflection_resolve`) |  |  |  |  |
| `kEmitterShare` (0.5) | reflection_resolve | Lamp-lit taps skip the outlier clamp | Live. Lucky lamp hits pass whole: speckle | RT2-23 (added) |
| `kReuseFraction` (1/5) | reflection_resolve | Size of the sharing disc | Live. Sets how much one-ray grain survives | RT2-23 (added) |
| `kMaxWeight` (4) | reflection_resolve | Cap on a shared tap's weight | Live at puddle edges (plausible, not measured) | RT2-23 (added) |
| Lamp light re-aimed by a neighbour's ray | reflection_resolve | Spread each lamp's light in a halo | Fixed in RT-24 (fix 1). It costs grain in motion; keep or revert is open | RT2-23 (done early); RT2-12 the proper version |
| `kCredibleDensity` | reflection_resolve | Lifts the clamp for high-density taps | Latent | RT2-23 (added) |
| `kMaxReach` (24 texels) | reflection_resolve | Largest disc | Latent (first pass only) | RT2-23 (added) |
| Plane tolerance (0.02 m + 1%) | reflection_resolve, reflection_blur | Is a neighbour on the same plane | Latent. In metres (the blur's first pass only) | RT2-23 (added) |
| Texel size: fallback 0.0015 of the distance, floor 0.0002 | reflection_resolve, reflection_blur | Metres per texel when not measured | Latent (partly first pass only) | RT2-23 (added) |
| **Reflection blur** (`reflection_blur`, `Renderer3D`) |  |  |  |  |
| Uncertainty gate edges (0.02, 0.20) | reflection_blur | How much blur a texel gets from its measured noise | Live. At speed each rough-floor texel gets anything from none to full blur: speckle | RT2-23 (added) |
| `MovingRadius` (4 texels) | Renderer3D.cpp | The blur's radius | Live. Caps how much one-ray grain the blur removes at speed | RT2-23 (added) |
| `kSigmaLFloor` and the mean floor (1e-3) | reflection_blur | Absolute brightness floors | Latent (the mean floor first pass only) | RT2-23 (added) |
| `BlurFrames`, `YoungOverreach` | Renderer3D.h | The blur's frame ramp and overreach | Latent | RT2-23 (added) |
| `YoungRadius` | Renderer3D.cpp | Young-pixel blur | Fine. 0 on the floor chain, an off switch | none needed |
| **Reflection rays** (`reflection_trace`, the ray budget) |  |  |  |  |
| 16 aimed glowing surfaces (`kMaxAreaEmitters`) | Renderer3D, reflection_trace | Which lamps get aimed rays | Fixed in RT-24 (2026-09-25): every emitter is aimed at | RT2-11 (done early) |
| 12-step search of an emitter's table | reflection_trace | Picks a point on an emitter | Latent (first pass only) | RT2-11 (was there: picks without a search) |
| 3 score points per emitter | reflection_trace | Where an emitter is scored for aimed rays | Latent | RT2-11 (added) |
| Cap of 64 per channel | reflection_trace | Guards the stored moments | Live. Tube reflections come out less blue than the tubes | RT2-6 arm 5 (was there) |
| "Moved on its own" tolerance (1/8 texel) | change_record.glsl | One ray or four per texel | Live (plausible). At speed rounding flips still texels between 1 and 4 rays | RT2-6 (added) |
| 4 moving rays; budget 6 frames, 1 ray, 16-pixel tiles, 0.75 dead band, 8-frame dwell | Renderer3D, FrameGraphBuilder, RenderSettings | Ray counts | Latent (first pass only) | RT2-27 (added under RT2-23's step 23d; 23d merged into RT2-27 on 2026-09-25) |
| Budget read at the same screen tile | reflection_trace, reflection_budget | Last frame's ray counts, not reprojected | Latent defect: a moving object gets another tile's rays | RT2-27 (added under RT2-23's step 23d; 23d merged into RT2-27 on 2026-09-25) |
| Re-light tolerance (0.004 of the value + 1e-4) | reflection_trace | Did a replayed sample change | Latent | RT2-6 (added) |
| Random index stride (1601) | reflection_trace | Each texel's random rotation | Latent (first pass only). Tied to resolution | RT2-7 (added) |
| Ray length (10 km) | reflection_trace | Farthest hit | Latent (first pass only). World size | RT2-41 (added) |
| **Formats** (`FrameGraphBuilder`) |  |  |  |  |
| Object ids in half-float history lanes | FrameGraphBuilder | Is this the same object | Latent. Exact only below 2,048 objects | RT2-5 (was there) |
| Velocity in two half floats | FrameGraphBuilder | Motion vectors | Latent. Precision falls with speed | RT2-6 (added) |
| **Glass and water** (`pbr_fragment`, `PostSettings`) |  |  |  |  |
| `ReflectionFloor` (0.05) and the 8x probe cap | pbr_fragment, PostSettings | Cap on water's and glass's own mirror ray | Latent (first pass only). Absolute brightness | RT2-31 (added) |
| **Found while chasing the lights' sharpness** (not part of the audit) |  |  |  |  |
| Minimum roughness (0.045) | pbr_fragment | No surface can be smoother | Latent. No puddle or mirror sharper than it | RT2-21 (added) |
| No ground truth for the lights on the floor | the reference renderer | How sharp the tubes' reflections should be | Open. They fattened when RT-21 removed the highlight cap | RT2-2 (added) |
| **Found on re-check (2026-09-25)** (recorded in the audit's own file, marked "other" or "unclear", read once) |  |  |  |  |
| Neighbour normal cut dot(N, Nn) < 0.9 | reflection_resolve | Rejects neighbours facing another way | Latent (an angle not derived from the lobe) | RT2-23 (23c: taps weighted by the lobe's width) |
| Roughness-difference cut (0.2) | reflection_blur | Hard stop between taps of different roughness | Latent; edge jitter | RT2-23 (23c: the two lobes' overlap) |
| Normal edge-stop exponent (8) | reflection_blur | Normal weight of a blur tap | Latent; edge jitter | RT2-23 (23c: a lobe-sized normal test) |
| `kLeastSameNeighbours` (3) | taa_resolve | The box uses only same-surface taps when at least 3 of 8 match | Latent; at a pole's silhouette the box can flip sides each frame (edge jitter) | RT2-28 (28b: the variance clip over same-surface taps) |
| Stability ramp smoothstep(0.15, 0.5, roughness) x (1 - metallic) | taa_resolve | How much of `kStableWiden` applies | Latent (judgement thresholds) | RT2-28 (28b deletes `kStableWiden` and its ramp) |
| `kTemporalSigma` (2.0) | taa_resolve | The box widened to 2 spreads of the pixel's own luminance at still outlines | Fine as a stated convention; its place rule goes with RT2-8's behaviour floor | RT2-8 |
| `kTemporalSigma` (2.0) | reflection_accumulate | The bound never tighter than 2 of the pixel's own spreads | Fine. A stated convention, relative to the pixel's own spread | none needed |
| Firefly clamp's minimum of 3 neighbours | reflection_accumulate | Skips the clamp where fewer than 3 same-object neighbours exist | Latent (a sample-count guard; rare at 11 m) | RT2-23 (23c's outlier bound) |
| Mirror fallback (dot(L, N) > 0.02, else the mirror direction) | reflection_trace | Replaces below-horizon draws with the mirror direction | Live: rejected draws pile on one direction and escape the resolve's clamp (the file's own comment measured about 12k pixels a frame jumping more than 40 levels) | RT2-23 (23a: bounded visible-normal sampling; rejected draws carry zero) |
| Emission lane capped at 6e4 | reflection_trace | Tells the resolve that a bright tap is a lamp, so its cap is lifted | Live, with `kEmitterShare` | RT2-23 (23c removes the exemption once RT2-11 lists every emitter) |
| Low-discrepancy period (64 frames, per-texel rotation) | reflection_trace | Which point of the lobe each texel samples | Latent; the stratification is lost when the memory is short | RT2-7 (7b: blue noise) |
| Rotation index (frame mod 64, offset 5.588238) | reflection_resolve | Per-texel, per-frame rotation of the tap disc | Latent; the frame-to-frame change shows as speckle when the memory is short | RT2-7 (7b) |
| Second-highest floor on the cap | reflection_resolve | The cap is never below the second-brightest tap | Latent; one stray ray fetched twice defeats the clamp | RT2-23 (23c) |
| Radial falloff exp(-2 r^2) | reflection_resolve | Down-weights the disc's outer taps | Latent; lowers the effective sample count | RT2-23 (23c: the disc and its weights from the lobe) |
| Young ramp (squared; divides by zero at `BlurFrames` 1) | reflection_blur | How young the history is, for the young radius and the overreach | Latent | RT2-23 (23c: the radius from the footprint and the history length) |
| `RayDetailGloss` windows (High 0.25-0.60, Medium 0.15-0.40, Low 0.05-0.20) | FrameGraphBuilder | Which roughness gets a traced reflection | Latent; independent of motion, scale and brightness | RT2-27 (the window becomes a cost cap) |
| Reflection intensity 1/4 | FrameGraphBuilder | A leftover fade | Fine: inert since RT-24, only a >0 test | none needed |
| Lane choice (the reflection brighter than the rest) | reflection_composite | Which motion TAA used | Fixed in RT-24 (the filter uses the surface's motion) | RT2-28 (done early) |
| Struck-mover test (travel > 0) | reflection_resolve | Routes a tap to the moving lane | Fine: an exact test | none needed |

The rows under "Found on re-check" were added on 2026-09-25. The verification found three values the audit had recorded in `build/rt24/audit_phase1.json` (marked "other" or "unclear" there) but that never reached this table (B-C11), and asked whether others were lost the same way. All 63 items marked "other" and the ones marked "unclear" were re-read: most are lanes, flags and push constants (fine); the ones that are values are listed above, each with its owning item. They were read once, not checked a second time.

**The rest of the renderer** was audited the same way on the same day: audit 2, below.

### Audit 2: the rest of the renderer (2026-09-24)

Twelve read-only agents listed every fixed value in the rest of the renderer: the lit shader, the direct light, the live bounce light and AO, the bake and probes, water and glass, post-processing and its settings, `Renderer3D` and the frame setup, and the raster fallbacks (SSR, SSGI, voxel GI, SSAO, shadow maps) -- 852 values. 270 broke the rule on paper. Twelve skeptics then tried to disprove each of those, plus 25 borderline ones the first pass had not flagged, and found 5 more. 75 were refuted (fine, or inert where they sit). The other 225 survived; the same value found by two agents is listed once, so the table below has 217 rows. 30 affect today's picture (**Live**) and 13 are marked first to fix. Live is the skeptics' judgement from reading the code: none of these has been measured on screen yet. Evidence: `build/rt24/audit2_phase1.json` and `audit2_phase2.json`.

Every surviving value is below, under the item that fixes it. The note at the top of each item says the same in plain words. (2026-09-25: two rows moved from RT2-23 to RT2-27 with step 23d, and three values the comparison found that neither audit had listed were added under RT2-31 and RT2-32, each marked "new". The raster refraction's push and occluder tolerance, which the comparison also named, were already here under RT2-44.) Two new items hold what had no owner: RT2-44 (the raster fallback) and RT2-45 (the post chain), as milestone M7. Not audited: particles, UI, terrain generation, the editor grid and debug views, GPU culling and skinning.

| Value | Where | Found | General form | On the roadmap |
| --- | --- | --- | --- | --- |
| **RT2-1** · Fail loud, validate clean, and count VRAM |  |  |  |  |
| kSaneLimit | BakedLighting.cpp:485 | Latent. cost | Validate the payload against what the header's own stamp implies (Width*Height*Depth*Tiles*bytes per texel, or the BC6H block arithmetic), and against the file length. | added |
| **RT2-2** · Instruments that can settle questions |  |  |  |  |
| ScreenshotFrame (+ ScreenshotCount 1) | EngineConfig.h:218 | Latent. no visible effect | Default the capture to the longest accumulator memory in force (derived from the settings), or to a 'settled' signal from the histories, never a fixed frame | added |
| **RT2-4** · Patches that should not change the picture, and small wins that need no rewrite |  |  |  |  |
| alpha fetch when the cutoff cannot fail (new) | ray_shadow_trace.glsl:180 | Live. cost; found by the skeptic | An identity guard: if hit.AlphaCutoff <= 0 (or the base alpha alone already decides), return true before the fetch. | added |
| **RT2-5** · Stable object identity in exact lanes |  |  |  |  |
| CPU-path object id = pool row + 1 (DrawMesh; also DrawWaterMesh L9724, DrawSkinnedMesh L98 | Renderer3D.cpp:9673 | Live. flicker; first to fix | An object's id comes from the object, not from submission order: the entity handle (as the ray-instance Identity already does, Renderer3D.cpp:5666-5669) combined with the chunk index for terrain. | was there |
| ray-instance Identity fold (EndScene) and emitter identity (SetAreaEmitters L3207) | Renderer3D.cpp:5669 | Latent. wrong brightness or colour | Carry the unfolded entity id wherever the storage is 32-bit (the ray-instance buffer and the emitter row). | was there |
| **RT2-6** · Correctness patches that do change the picture, one at a time |  |  |  |  |
| Probe cubes sampled in the sky's rotated frame | pbr_fragment.glsl:3317 | Latent. wrong brightness or colour; found by the skeptic | Rotate into the sky frame only when sampling slot 0. Probe slots and the parallax take the world-space direction. | added |
| history read flipped on a gl_FragCoord uv (mirrored tile) | reflection_budget.rvshader:158 | Latent. noise; first to fix; found by the skeptic | A texture lookup from a uv built from gl_FragCoord takes no flip (or uses texelFetch at the integer tile). | added |
| re-light dead band | direct_trace.rvshader:1413 | Latent. smear | Keep the relative ulp guard. Replace 1e-4 with the half's own absolute step at that magnitude (or drop it). | added |
| relight change tolerance | rtgi_trace.rvshader:676 | Latent. smear | A pure numerical guard: the stored value's half-float rounding (relative 2^-10) plus the half subnormal step. | added |
| foam buffer format | Water.cpp:583 | Latent. wrong brightness or colour | Store with the engine's unbiased stochastic rounding onto the half grid (StoreAsHalf/HalfRoundingDraw), or use R32G32_SFLOAT. | was there |
| WBOIT unit-weight distance | pbr_fragment.glsl:6273 | Latent. wrong brightness or colour | One constant shared by both shaders, with the unit-weight distance derived from the projection (the near plane) so the clamp band spans the camera's [near, far] instead of a metre range. | problem was listed; general form added |
| kUnitWeightDistance (particles) | particle_weighted.rvshader:36 | Latent. wrong brightness or colour | One shared weight definition for every writer of the pair. Normalise the distance to the view's own depth range (the camera's near/far clip, owner settings) so the discrimination band is the camera's range at any world scale. | was there |
| 2x2 box on floor-halved sizes | tile_reduce.rvshader:59 | Latent. noise | texelFetch the exact 2x2 at 2*ivec2(gl_FragCoord), and fold an odd last row or column into the last output texel with area weights. | was there |
| **RT2-7** · Random numbers that move in every anti-aliasing mode |  |  |  |  |
| Animated lane = any jitter lane nonzero (TraceDirectLight; also TraceDirectWater L8021, Re | Renderer3D.cpp:8131 | Latent. noise | Each consumer decides from whether it feeds a temporal accumulator of its own (the signal's hasHistory/History flag, passed in the push block), not from TAA's jitter. | was there |
| fade dither lattice | ray_shadow_trace.glsl:143 | Latent. noise | Key the ray-side dither on the ray's own pixel and frame (RV_TRACE_PIXEL / RV_TRACE_FRAME, as the soft-shadow sample does at L350-360), the same domain as the on-screen dissolve. | added |
| **RT2-10** · One light library |  |  |  |  |
| sized-lamp streak widening on water | pbr_fragment.glsl:5293 | Latent. wrong brightness or colour | No artist multiplier: evaluate the lobe against the lamp's real extent (representative point with the correct energy normalisation, or an LTC/area integral). | added |
| streak half renormalisation | pbr_fragment.glsl:5302 | Latent. wrong brightness or colour | The exact, energy-conserving normalisation of the widened lobe. If the shaft reads dim, the lobe's shape (5293) is what is wrong, not its energy. | added |
| sea streak length terms | direct_trace.rvshader:440 | Live. wrong brightness or colour; first to fix | A sphere widens the slope distribution by its angular radius in both axes (0.5*angular, as the land path and L441 do), applied once, in the wind frame with the Cox-Munk axes along the wind. | added |
| sea streak half renormalisation | direct_trace.rvshader:442 | Live. wrong brightness or colour; first to fix | Use the exact normalisation (ax0*ay0)/(axS*ayS): energy conservation, the same rule as the land path's (alpha/alpha')^2. | added |
| specular half of the lamp score (non-water) | direct_trace.rvshader:586 | Live. noise; first to fix | Make the score the shading's own term without the shadow ray: the luminance of albedo*diffuse/pi + specular from DirectTerm, with the same widened, renormalised, length-normalised lobe. | added |
| hard-shadow sentinel | ray_shadow_trace.glsl:345 | Latent. wrong brightness or colour | Pass the light's kind to TraceShadowSoftFromMasked as an argument instead of inferring it from the ray length. | added |
| streak elongation | water_lamps.glsl:265 | Live. wrong brightness or colour | No stretch factor. Keep the per-pixel closest-point half vector, which carries the physical elongation. | added |
| **RT2-11** · Emissive geometry as sampleable lights |  |  |  |  |
| area-emitter membership threshold (RefreshDrawList) | Scene.cpp:2224 | Latent. noise | Membership = emits anything (strength > 0). The aimed pick weights rows by estimated power (luminance x area x geometry), which reflection_trace's EmitterScore already does. | added |
| emitter list order handed to the renderer (OnRender -> SetAreaEmitters) | Scene.cpp:5189 | Live. noise; first to fix | Choose the aimed set per shading point by contribution, not list position. | was there |
| emitter shape: thinnest local-box axis is the normal (RefreshDrawList) | Scene.cpp:2231 | Latent. wrong brightness or colour | Sample the emitting surface itself: emissive triangles with a per-mesh area x radiance CDF, as in pbrt's triangle area lights and RTXDI's emissive-triangle lights. | was there |
| emitter threshold | RayShadows.cpp:287 | Latent. noise | Compute one predicate once (emitter-list membership) and pass it to AddInstance instead of a second literal. | added |
| emitter cap | rtgi_trace.rvshader:365 | Latent. wrong brightness or colour | No positional cap. A buffer sized to the scene's emitters, with selection by an importance distribution over emitter power (a CDF or a light tree), so that no emitter is treated differently because of its list position. | was there |
| **RT2-12** · Light candidates at a fixed cost per pixel, shared by every pass |  |  |  |  |
| hybrid reject distance: eighths of a metre in 13 bits (PackLightCull) | Renderer3D.cpp:118 | Latent. wrong brightness or colour | Saturate to 'never reject' (0x1FFF meaning unbounded), or pack the reject distance as a half float rounded up, the way the range already is. | added |
| LightCullRejects hybrid reject distance | pbr_fragment.glsl:529 | Latent. wrong brightness or colour | Saturate conservatively: the all-ones code means 'never reject', as HalfUp does with infinity. | added |
| RV_WORLD_GRID_X/Y/Z | pbr_fragment.glsl:349 | Latent. cost | Derive the per-axis counts from the reach bounds' own aspect, so cells are near-cubic under one total cell budget (a RenderSettings cost value, if exposed at all). | was there |
| WorldLightGrid cells | LightGrid.h:158 | Latent. cost | Split a fixed cell budget across axes in proportion to the extent of the lights' bounds, recomputed each build and passed to the shader as a uniform instead of constants. | was there |
| orthographic cluster range | LightGrid.cpp:26 | Latent. wrong brightness or colour | Derive near and far from the orthographic matrix's own terms (exact algebra, as the perspective branch does). | added |
| neighbour ring spacing | direct_trace.rvshader:955 | Latent. noise | turn + n*2pi/N (maths), or a golden-angle step. | added |
| neighbour ring angle step and count (lane History.z) | water_shade.rvshader:117 | Latent. noise | Step = 2pi/count, or a golden-angle sequence. Either way the step comes from the count, not a count of 3 built into the constant. | added |
| neighbour normal agreement | water_shade.rvshader:144 | Latent. noise | Use the reference's normal threshold, stated as the reference constant, rather than a water-specific loosening. | added |
| **RT2-16** · Views own their histories |  |  |  |  |
| GlobalIllumination.y frame counter (BeginScene, static giFrame) | Renderer3D.cpp:3944 | Live. noise | Keep one frame index per view chain, stored with the chain's own state (the per-chain CameraMotion/TemporalHistory the engine already keeps, Renderer3D.cpp:3893-3920) and advanced once per frame of that chain. | added |
| **RT2-17** · Signals and layers as data |  |  |  |  |
| plane edge-stop | change_filter.rvshader:92 | Latent. noise | Point-to-plane distance with one normal, abs(n.(P' - P)), against a tolerance equal to the block's world footprint (view depth x pixel angle x 3 x stride), a pixel-unit definition. | added |
| Floor lane | change_filter.rvshader:106 | Latent. smear | Judge the change the history would carry, not one frame's. Keep the record while the camera is still and compare the re-light against it, so the change accumulates since the history began, and reset when it is acted on. | added |
| joint-bilateral depth tolerance | signal_upsample.rvshader:108 | Latent. banding | Weight each tap by its distance to the pixel's plane (the depth predicted from the local depth gradient or normal), which is zero for coplanar taps at any angle and resolution. | added |
| guide texel selection | gbuffer_guide.rvshader:88 | Latent. jagged or jittery edges | Every pass derives a low texel's representative full-resolution texel by one integer rule (texelFetch at dst*d + d/2 from gl_FragCoord), never through a uv that lands on a texel edge. | added |
| **RT2-20** · Ray passes in compute |  |  |  |  |
| importance smoothing lane (BuildFrame 'Budget allocate') | FrameGraphBuilder.cpp:4195 | Latent. noise | Remove the cause: evaluate importance at jitter-compensated sample positions, so the lattice does not walk and no temporal average is needed. | added |
| RayBudgetImportanceSmoothing | RenderSettings.h:542 | Latent. noise | Average the importance over exactly one jitter period, defined by the owner's TemporalJitterPhase. | added |
| AO budget read (u_Budget.r) | rtao_compute.rvshader:331 | Latent. noise | Reproject the demand history by the velocity like every other history, so the allocation follows surfaces at any speed. | added |
| motion saturation | importance_tiles.rvshader:151 | Latent. noise | Measure what the term predicts: the tile's share of texels whose accumulated history is young (the accumulators' own frame counts, as reflection_budget reads them), instead of a motion knee. | added |
| material complexity weights | importance_tiles.rvshader:120 | Latent. noise | Weight a tile by how much each estimator contributes to its pixels: for GI the diffuse weight (1-metallic) x albedo, for AO the ambient term's weight, alongside the geometric partial-occlusion term already there. | added |
| Smoothing lane | tile_budget.rvshader:203 | Latent. flicker | A box over exactly TemporalJitterPhase frames, which cancels a periodic signal exactly (as the comment itself notes). | added |
| DwellFrames lane | tile_budget.rvshader:246 | Latent. flicker | If it is meant to span a jitter cycle, read TemporalJitterPhase. Otherwise drop it and let the derived band do its job. | added |
| **RT2-21** · The G-buffer as a designed contract |  |  |  |  |
| TraceSurface ray-origin offset | pbr_fragment.glsl:2643 | Latent. noise | Lift by the origin's own uncertainty, never by metres. The reconstructed point's texel slack (the neighbour-depth difference, as RTAO does) plus the float error of the position. | added |
| shadow-ray origin push | ray_shadow_trace.glsl:240 | Latent. noise | Wachter & Binder 2019 (Ray Tracing Gems ch. 6): | added |
| kLiftPerMetre | rtao_compute.rvshader:138 | Latent. wrong brightness or colour | The offset the precision really needs: ulp-scaled on the magnitude of the world position (Wachter & Binder, Ray Tracing Gems 2019 ch. | added |
| kLift | rtao_compute.rvshader:137 | Latent. wrong brightness or colour | The same ulp-based origin offset as above, which replaces both constants. | added |
| parallax step count | pbr_fragment.glsl:3671 | Latent. banding | Set the step count from the texels the ray crosses at the read mip, ceil(abs(delta_uv) * textureSize / 2^lod), capped by a cost setting. | added |
| layered parallax step count | pbr_fragment.glsl:3824 | Latent. banding | A per-layer step count from the texels crossed at the read mip, capped by a cost setting. | added |
| terrain wall-projection blend | pbr_fragment.glsl:4003 | Latent. banding | Weights from the stretch each projection gives (Geiss, GPU Gems 3 ch.1: | added |
| terrain weight sampler single level | Material.cpp:341 | Latent. jagged or jittery edges | Build the weight map's mip chain (weights are linear and already renormalised at 3971-3972) and sample with the hardware LOD. | added |
| alpha-test mip level | ray_shadow_trace.glsl:181 | Latent. jagged or jittery edges | Ray cones (Akenine-Moller et al., Ray Tracing Gems 2019): LOD from the ray's spread angle x hit distance over the texel footprint. | added |
| **RT2-23** · Reflection denoiser v2 |  |  |  |  |
| (the two reflection-budget rows that stood here moved to RT2-27 on 2026-09-25, with step 23d) |  |  |  |  |
| **RT2-24** · Direct light: visibility reconstructed apart from shading |  |  |  |  |
| DirectBudget young threshold and tile (BuildFrame) | FrameGraphBuilder.cpp:1845 | Latent. noise | Extra lamps from the history's deficit against the signal's own memory setting (enough that history + extra reaches the accumulator's Memory), not a fixed 6 | added; built by RT2-12b's young-pixel rule |
| ShadowRayFade.y/.z start and end | pbr_fragment.glsl:1335 | Latent. noise | Retire the metre shapes in favour of the share shape (a pixel's own light budget), or express start and end as a lamp's range fraction, so no metres remain. | added; leaves RT mode with the lit loop (RT2-13, RT2-31), raster in RT2-44 |
| ShadowRayFade.w floor / share threshold | pbr_fragment.glsl:1353 | Latent. wrong brightness or colour | Replace the borrow with an unbiased estimate. Trace with probability p = 1 - skip and weight the answer by 1/p (as the RT-11 and WR-16 reservoirs already do); | added; leaves RT mode with the lit loop (RT2-13, RT2-31), raster in RT2-44 |
| skipped-ray visibility borrow | pbr_fragment.glsl:4712 | Latent. wrong brightness or colour | Russian roulette: skip with probability p and divide the traced visibility by (1-p), which is unbiased and independent of order. | added; leaves RT mode with the lit loop (RT2-13, RT2-31), raster in RT2-44 |
| **RT2-25** · Bounce light from Realtime lights, added on top of the bake |  |  |  |  |
| runtime cache blend per revisit (SolvePendingIrradiance) and its clamps (RequestRuntimeIrr | Renderer3D.cpp:2995 | Latent. smear | Let a cell's memory follow evidence, not a fixed per-sweep weight: a per-cell change test (fresh estimate against stored value, beyond the estimate's own noise) that restarts the cell's average. | added |
| runtime irradiance field request (UpdateIrradianceVolumes) | Scene.cpp:3629 | Latent. smear | Blend by the cell's own sample count (a cumulative average up to a sample cap set in RenderSettings), and reset or shorten on measured change (the lighting hash, or the MeasuredChange map) rather than a fixed per-visit EMA | added |
| kHysteresis | RuntimeIrradianceField.h:100 | Latent. smear | History belongs to a world position, not to a texel index. Scroll (toroidally address) the history with the box so a move carries no misregistration, as DDGI's scrolling volumes do. | problem was listed; general form added |
| kRayBudget | RuntimeIrradianceField.h:95 | Latent. smear | Make the per-frame ray budget an owner RenderSettings value (a cost lever). | added |
| kMovingThreshold | RuntimeIrradianceField.h:79 | Latent. no visible effect | Measure motion in the grid's own unit, not in metres per frame. The box is moving when its snapped cell changes (one cell, the field's pixel-unit). | added |
| kDirectionSmoothing | RuntimeIrradianceField.h:85 | Latent. smear | No per-frame smoothing. Make the lead a function of path, not of frames: | added |
| kSpacing | RuntimeIrradianceField.h:46 | Latent. wrong brightness or colour | Take the runtime cell size from the scene's own statement of scale: the authored volumes' Spacing, or an owner RenderSettings value. | was there |
| kCellsX/kCellsY/kCellsZ | RuntimeIrradianceField.h:43 | Latent. wrong brightness or colour | The cell count is an owner cost setting. Make the grid isotropic, or derive its aspect from the scene's own bounds, not from an assumed flat world. | was there |
| kLeadDistance | RuntimeIrradianceField.h:74 | Latent. no visible effect | Express the lead in box units (a stated fraction of the half-extent), or drop it. | added |
| runtime box extents vs cell pitch (snap unit is not the cell pitch) | RuntimeIrradianceField.cpp:58 | Latent. smear; found by the skeptic | One definition of the cell: Extents = (kCells-1)*spacing/2, so the snap and move unit equals the cell pitch the fill and the readers use, as the authored volumes already do. | added |
| GiSignal BoundWidth and MaxRadius (GiSignal) | Renderer3D.cpp:7904 | Latent. smear | Set the bound width from the actual correlation of the estimates at the accumulation grid: | problem was listed; general form added |
| miss radiance | pbr_fragment.glsl:2655 | Latent. wrong brightness or colour | A miss is sky only if the ray could have left the scene. Take the reach as the distance to exit the TLAS bounds (maths from the scene's own extent), or return 'unknown' on a cut-off (fall back to the probe or field) and keep the sky for a true escape. | added |
| probe slot 0.0 in ProbeIrradiance(hit.Normal, 0.0) / ShadeTraced(...,0.0) | irradiance_fill.rvshader:279 | Latent. wrong brightness or colour | Light the sky and ambient term at a secondary hit by that hit's own sky visibility, using the same visibility the receiver uses: | was there |
| kIndirectClamp | rtgi_trace.rvshader:369 | Latent. wrong brightness or colour | Bound each sample relative to the estimator's own statistics (k times the pixel's running or neighbourhood mean from the signal history), which is invariant to a global light scale. | was there |
| kNeeClamp | rtgi_trace.rvshader:583 | Latent. wrong brightness or colour | The same relative-to-mean bound as the hemisphere term, or removing the 'lottery' at its cause (aim only at the emitter's visible part, or MIS between NEE and the hemisphere ray) so that no absolute cap is needed. | was there |
| **RT2-26** · One rule for indirect light, and a bake that knows when it is stale |  |  |  |  |
| hybrid lamp blend band (PackLightCull) | Renderer3D.cpp:116 | Latent. wrong brightness or colour | A dimensionless band: a fixed fraction of the author's radius (an owner setting), or a band tied to the irradiance field's cell spacing at the lamp, which is what the bake can resolve. | added |
| BakedShare blend band | pbr_fragment.glsl:305 | Latent. wrong brightness or colour | The share's transition must be at least as wide as the field can represent (sampling, a Nyquist argument). | added |
| hybrid live band | LightGrid.cpp:231 | Latent. wrong brightness or colour | Make the band the field's own cell spacing at that point, the narrowest transition the bake can hold. | added |
| baked-GI settle window (RenderShadows) | Scene.cpp:1849 | Latent. no visible effect | Warn on an event, not a count: the field lookup has completed for the current lighting hash, no load or solve is pending, and the hash has not changed since that lookup | added |
| bake write settle (UpdateIrradianceVolumes) | Scene.cpp:4147 | Latent. no visible effect | Write when the solve requested for this lighting hash completed and the hash was unchanged across that whole solve (an event), with no frame count | added |
| bake stall ceiling (UpdateLightingBake) | Scene.cpp:4624 | Latent. no visible effect | Give up on an event: the lighting hash changed during N consecutive completed solves, or a probe's capture was attempted and failed, with no frame count | added |
| probe 'moved' tolerance (CaptureReflectionProbes) | Scene.cpp:3303 | Latent. cost | Exact comparison of the stored position (capture is deterministic), or a tolerance in ULPs of max(abs(position)) | added |
| volume padding (UpdateIrradianceVolumes) | Scene.cpp:3840 | Latent. wrong brightness or colour | Pad in realised cells: compute the cell count, then set pad = 2 x realised spacing, and iterate once | added |
| kMaxIrradianceVolumes | Renderer3D.h:87 | Latent. wrong brightness or colour | Size the region table from the scene (a storage buffer, the way the atlas already packs regions), so the only limit is memory. | was there |
| Renderer3D::kMaxIrradianceVolumes (UpdateIrradianceVolumes) | Scene.cpp:3823 | Latent. wrong brightness or colour | Volume rows in a storage buffer sized to the scene; if an atlas memory budget (a RenderSettings value) binds, drop by importance and say which | was there |
| irradiance volume capacity | scene_block.glsl:110 | Latent. wrong brightness or colour | Size the region table from content (a storage buffer indexed per region), keeping the warning for a real memory limit. | was there |
| BC6H mode-11 box-fit endpoints (lo>>6, hi>>6) | BakedLighting.cpp:157 | Latent. banding | Encode by minimum reconstruction error, not a fixed fit: principal-axis endpoints with least-squares refinement, and try the two-region modes when they reconstruct better (the reference encoder's criterion). | added |
| Stamp::Matches kEpsilon | BakedLighting.cpp:332 | Latent. no visible effect | Compare in ulps relative to the magnitude, or exactly, since both sides are computed deterministically from the same scene. | added |
| IrradianceFromCube samplesPerAxis | Cubemap.h:80 | Latent. banding | Tie the sample count to the source (every source texel covered, Nyquist), or sample a mip whose texel matches the grid spacing, as the GPU path does. | added |
| AoSignal MaxRadius and inherited contract (AoSignal) | Renderer3D.cpp:7869 | Latent. smear | A measured-change record for AO, like the direct light and GI have, so a change caused by an occluder is detected at the receiver by re-tracing a sampled pixel instead of waiting for the receiver to move. | added |
| AoRadius (engine default, active for the bridge's traced AO) | PostSettings.h:694 | Latent. no visible effect | No change of unit needed. Optionally have the bridge profile state its AO radius explicitly so it is an owner setting. | added |
| **RT2-27** · Reflections tiered by roughness |  |  |  |  |
| young-share thresholds | reflection_budget.rvshader:149 | Latent. noise | Linear in the share with no dead zone: target = least + share x (most - least). | added under RT2-23; moved here 2026-09-25 with step 23d, which merged into RT2-27 |
| stride-2 lattice, row-only pairs | reflection_budget.rvshader:110 | Latent. noise | Count pairs in both lattice directions (isotropic). | added under RT2-23; moved here 2026-09-25 with step 23d, which merged into RT2-27 |
| ProbeParallax proxy radius | pbr_fragment.glsl:2452 | Latent. smear | Separate the proxy from the blend radius, taking it from the capture itself. | added |
| kMaxProbes (kSlots = 16) | ProbeArray.h:28 | Latent. wrong brightness or colour | Slots = the scene's probe count, bounded by the device's cube-array layer limit and an owner memory-budget setting. | added |
| **RT2-30** · Delete the superseded reconstruction and GI paths |  |  |  |  |
| old post-chain occlusion accumulate (BuildFrame 'SSAO accumulate') | FrameGraphBuilder.cpp:4757 | Latent. smear | Route the reference arm's accumulate through the signal contract's motion-shortened memory, or keep it explicitly as a still-only reference | goes with the deletion |
| kFireflyScale + offset | gi_denoise.rvshader:263 | Latent. wrong brightness or colour | A bound in exposure-normalised units, or relative to the local mean/sigma, with no absolute offset. | goes with the deletion |
| Compress / Expand | gi_denoise.rvshader:135 | Latent. wrong brightness or colour | Compress exposure-scaled values (value x the frame's exposure), so the filter is invariant to a global light scale at matched exposure. | goes with the deletion |
| first-frame bound and blend | gi_denoise.rvshader:183 | Latent. wrong brightness or colour | The same exposure-normalised, offset-free bound as the accumulating path, in one unit. | goes with the deletion |
| kMaxFrames | gi_denoise.rvshader:115 | Latent. noise | Cap = 1/(1-Feedback), where the warm-up hands over by definition, or no cap below half-float exactness. | goes with the deletion |
| **RT2-31** · Glass and water in the ray-traced world |  |  |  |  |
| refraction IOR literal 0.7519 (new, 2026-09-25) | pbr_fragment.glsl:6354, 6426 | Latent. wrong brightness or colour; found by the comparison | The IOR from the medium (a dial of the material), in the refraction pass and the raster fallback alike. | added |
| glass pane RT-9 ray plan (BuildFrame 'GlassReflectionBudget') | FrameGraphBuilder.cpp:2826 | Latent. noise | The same as the floor's: extra rays derived from the pane history's deficit against its Memory, and the budget as a RenderSettings value | added |
| quad tracing-lane walk | pbr_fragment.glsl:4076 | Live. banding | Reconstruct the quad-shared refraction spatially, as S5 does for the mirror: | added |
| S5 tap distance-agreement tolerance | pbr_fragment.glsl:6063 | Latent. smear | Tolerance in pixel units: the spread of hit distance one S5 texel spans (ray-cone footprint at the reference distance), with no metre floor. | added |
| glass coverage from reflected radiance | pbr_fragment.glsl:6316 | Live. wrong brightness or colour; first to fix | Take coverage from the directional albedo, a true reflectance: reflectance = largest channel of (F0*envBRDF.x + envBRDF.y)*occlusion, independent of what is reflected. | added |
| sky depth cut | water_backdrop.rvshader:62 | Latent. wrong brightness or colour | Compare against the clear value exactly (raw <= 0 in reverse-Z). That is a definition, not a threshold. | added |
| kTracedBound | pbr_fragment.glsl:6155 | Live. wrong brightness or colour | Remove the cause the comment names, a mirror ray reading one mip-0 texel, with ray cones (Akenine-Moller et al., Ray Tracing Gems 2019, texture LOD for ray tracing). | was there |
| **RT2-32** · Water as a material, a surface generator and a medium |  |  |  |  |
| deep-water wave speed x 0.32 (new, 2026-09-25) | water_waves.glsl:165 | Latent. wrong brightness or colour; found by the comparison (G-C15) | Exact deep-water dispersion, omega^2 = g k, from the same spectrum as the waves; no factor. The owner judges the speed change with D30's arms. | added |
| water absorption shape sigma = (4.6, 1.9, 1.3) / GradientDepth (new, 2026-09-25) | pbr_fragment.glsl:6340 | Latent. wrong brightness or colour; found by the comparison | The medium's own dial (per-channel absorption), with today's clear-ocean shape as its default. | added |
| sea lamp accumulate memories (AccumulateWaterLamps, the live private pass) | Renderer3D.cpp:7760 | Live. smear | Keep the glint's history only while the view-to-lamp reflection geometry is unchanged within the lobe's own angular width, which the sea's Beckmann roughness gives (physics). | added |
| WaterLampMemoryGlint | EngineConfig.h:714 | Latent. smear | Shorten the memory by the picture's measured change or texel travel (the contract path, --water-contract=on, already does this in texels, a pixel-unit rule), so 16 is only a cap and never the operating length under motion | added |
| WaterLampMemoryScatter | EngineConfig.h:702 | Latent. smear | The same as the glint: memory shortened by measured change or texel travel, with the count only a cap | added |
| glint memory (lane History.z) | water_accumulate.rvshader:401 | Live. smear; first to fix | Set the memory from the estimate's own statistics: accumulate until the running mean's standard error falls below a stated fraction of the signal, rather than a frame count. | added |
| scatter memory (lane History.y) | water_accumulate.rvshader:400 | Latent. smear | The same as the glint memory: a variance-based stop plus measured change, not a frame count. | added |
| history clamp width (lane Probe.x) | water_accumulate.rvshader:386 | Latent. smear | Derive the bound from the estimator's known variance: a confidence interval on the neighbourhood mean, widened by the history's own spread, at a stated false-rejection probability. | added |
| WaterCaustics tile sizes and drift | pbr_fragment.glsl:3471 | Live. noise | Take the caustic cells from the same ripple field the detail normals use: | added |
| sea detail-normal tiles and scroll | pbr_fragment.glsl:4235 | Latent. banding | Take the tile from the body's own surface-detail dial (TextureScale) and the drift from deep-water dispersion c = sqrt(g*lambda/2pi) at the tile's wavelength. | added |
| sea detail-normal distance fade | pbr_fragment.glsl:4241 | Live. flicker | Fade as a function of pixel footprint over ripple wavelength, with the footprint from dFdx/dFdy of the world position or from the projection's pixel angle. | added |
| sea detail-normal strength | pbr_fragment.glsl:4245 | Latent. wrong brightness or colour | Set the amplitude to the share of the sea's total slope variance (Cox-Munk at the wind the body's dials imply) that lies between the grid's shortest carried wave (4 x Spacing) and the ripple tile. | added |
| contact-foam band | pbr_fragment.glsl:4338 | Live. wrong brightness or colour | Measure vertical depth: reconstruct the backdrop point in world space and take the surface's y minus its y. | added |
| contact-foam surge | pbr_fragment.glsl:4339 | Latent. flicker | Phase from vertical depth, the same quantity as 4338's fix. | added |
| sea pixel footprint | pbr_fragment.glsl:4409 | Live. wrong brightness or colour; first to fix | Take the footprint from the pixel itself: max(length(dFdx(P)), length(dFdy(P))), which also includes the grazing stretch exactly and replaces /grazing and its 0.08 floor. | added |
| Cox-Munk slope ceiling | pbr_fragment.glsl:4413 | Live. wrong brightness or colour | Keep Cox-Munk's formula, not its single value: compute the mean-square slope from the wind speed the body's WaveLength/WaveHeight imply (Pierson-Moskowitz). | added |
| refraction ray reach | pbr_fragment.glsl:6368 | Latent. wrong brightness or colour | Always reach = -ln(floor)/sigmaMin, with the floor a transmittance defined as invisible (for example under half an output level after exposure), for the reference preset too; | added |
| RV_WATER_BAND | water_waves.glsl:31 | Latent. banding | Let only the grid end the geometric band: 4 quads, the sampling limit already in the code. | added |
| foam step clamp | water_foam.rvshader:64 | Latent. smear | No clamp: one clock for waves and foam. | added |
| foam drift speed | water_foam.rvshader:73 | Latent. smear | Derive the drift from the same dispersion relation the vertex uses, for example the sum's own Stokes drift, sum of a_i^2*k_i*omega_i from the same amplitudes and speeds. | added |
| foam buffer resolution | Water.cpp:568 | Latent. jagged or jittery edges | Make the foam texel the grid's quad size (spacing), capped by the cost budget. | added |
| kMaxDivisions | Water.cpp:131 | Latent. jagged or jittery edges | Pass the shader and the foam step the built quad size (extent/divisions, the larger axis), so the clamp reads what the grid actually is. | added |
| water material roughness | Water.cpp:189 | Latent. wrong brightness or colour | Take the sub-tile slope variance from the same spectrum the waves use: | added |
| detail tile wave fan orientation | Water.cpp:350 | Live. banding | Sample the tile in the wind frame (u = dot(xz, wind), v = dot(xz, windPerp)) and rotate the decoded slope back to world. | added |
| **RT2-33** · Fog, glows and particles in the composition |  |  |  |  |
| kSideGlow (and the shader's 0.34/0.77 cone-width edges) | LightGlow.cpp:29 | Live. wrong brightness or colour | Make the side visibility a per-light (per-fixture) authored property, with the physical default of zero: | added |
| Side-glow cone smoothstep(0.34, 0.77, cosOuter) | light_glow.rvshader:126 | Live. wrong brightness or colour | The glow carries the intensity the light emits toward the camera, from the same angular profile the shading uses (the cone term alone, or an authored emission profile such as a wider outer cone or IES). | added |
| Depth pull: max(1.0 m, 2*radius), capped at dist*0.5 | light_glow.rvshader:162 | Latent. wrong brightness or colour | Pull by the light's own size only (a multiple of SourceRadius, the one authored measure of the fitting), or replace the pull with a single visibility test: | added |
| Back cutoff smoothstep(-0.35, 0.05, cosView) | light_glow.rvshader:127 | Live. wrong brightness or colour | Same as the side-glow item: the glow follows the light's own angular emission toward the camera, and no separate back-face taste curve exists. | added |
| Sky depth = FarClip | fog.rvshader:132 | Live. wrong brightness or colour | Background pixels integrate to infinity: the closed-form limit for climb > 0, or the floor crossing where the ray descends, which lines 174-213 already solve. | added |
| Horizontal-ray threshold abs(climb) < 1e-5 | fog.rvshader:235 | Latent. banding | Branch on the dimensionless x = climb*travel. Evaluate (1-exp(-x))/x with its series (1 - x/2 + x^2/6) for abs(x) below the fp32 cancellation point, and the closed form above it. | added |
| **RT2-38** · Levels of detail for raster and rays |  |  |  |  |
| thin-member fade distances | pbr_fragment.glsl:4185 | Live. flicker | Fade by projected width in pixels: the member's physical thickness (authored on the material) divided by the pixel footprint (distance x pixel angle from the projection). | added |
| thin-member fade for rays | ray_shadow_trace.glsl:136 | Live. flicker | Fade by projected size: member thickness / (distance x pixel angle) below one pixel, a pixel-unit definition, shared by raster and rays. | added |
| **RT2-41** · Large-world precision |  |  |  |  |
| solve reach (SolvePendingIrradiance) | Renderer3D.cpp:3018 | Latent. wrong brightness or colour | A miss must be a true miss: make the reach unbounded, or the scene's TLAS bounds diagonal (a geometric fact of the scene). | added |
| kGiReach (BuildFrame) | FrameGraphBuilder.cpp:881 | Latent. wrong brightness or colour | Derive the reach from the world: the diagonal of the TLAS instance bounds. | added |
| hit directional shadow reach | pbr_fragment.glsl:2984 | Latent. wrong brightness or colour | The ray's tMax is the distance to exit the scene's acceleration-structure bounds (scene maths). | added |
| TraceReflection reach (and the sky-visibility far) | pbr_fragment.glsl:3336 | Latent. wrong brightness or colour | One scene-derived reach (the TLAS bounds' exit distance) shared by every 'whole scene' ray, replacing each 1e4 literal. | added |
| ray reach 1.0e4 m | pbr_fragment.glsl:5422 | Latent. wrong brightness or colour | No limit for a directional ray: tMax as a numerical infinity (a large float the traversal accepts), or the diagonal of the scene's TLAS bounds. | added |
| directional shadow tMax / toward sentinel | direct_trace.rvshader:494 | Latent. wrong brightness or colour | Set tMax from the acceleration structure's world bounds (the distance to leave the TLAS box) or use the largest float the ray query accepts. | added |
| mirror ray length and miss distance | water_trace.rvshader:81 | Latent. wrong brightness or colour | Take the reach from the scene's own extent (the acceleration structure's bounds diagonal) or leave tMax unbounded. | added |
| reach (directional light shadow ray) | irradiance_fill.rvshader:321 | Latent. wrong brightness or colour | A ray to an infinitely distant light runs to the edge of the world: tMax = the scene's acceleration-structure bounds diagonal (or an unbounded tMax), never a metre literal. | added |
| Trace.x lane (ray reach) | irradiance_fill.rvshader:229 | Latent. wrong brightness or colour | A miss must mean a real miss: set the reach to the scene's acceleration-structure bounds diagonal, not a multiple of the volume. | added |
| bounce reach (GI.w) | rtgi_trace.rvshader:362 | Latent. wrong brightness or colour | tmax equal to the acceleration structure's world-bounds diagonal. No hit can lie beyond it, so the picture is exact at any scale and nothing past the scene is ever paid for. | added |
| wall metres-per-uv from screen derivatives | pbr_fragment.glsl:4010 | Live. noise | Use the known mapping instead of a derivative ratio. Either pass metres-per-uv (TextureScale x entity scale) as a uniform, or build the wall uv from the same terrain-local coordinate v_TexCoord comes from, so the wall's derivatives are analytic and free of rounding noise. | added |
| **RT2-44** · The raster fallback on general forms |  |  |  |  |
| SpotLookup[4] / CascadeLookup[4] (SceneUniforms; ShadowMap kMaxLocal 4, kMaxCascades 4) | Renderer3D.cpp:223 | Latent. wrong brightness or colour | Give every casting light a map from a shared atlas and allocate each light's resolution by screen-space importance, so the budget lowers resolution instead of dropping occlusion. | added |
| shadow-map depth bias (BeginShadow) | Renderer3D.cpp:9006 | Latent. jagged or jittery edges | Derive the bias from the map's own geometry: a slope term of one texel's depth footprint (receiver-plane), plus the normal offset in texel-world units (CascadeTexel already exists). | added |
| ShadowMap::kMaxLocal (RenderShadowMaps, raster) | Scene.cpp:2780 | Latent. wrong brightness or colour | Rank casters by their contribution to this view (intensity x attenuation at the frustum x screen coverage) and give the slots to the top N. | added |
| one directional light gets cascades (RenderShadowMaps, raster) | Scene.cpp:2757 | Latent. wrong brightness or colour | Give the cascades to the directional light with the largest contribution to the view (illuminance x the view's receiver coverage), or a budget of N cascade sets as a RenderSettings value | added |
| spot shadow frustum widening and clamp (RenderShadowMaps, raster) | Scene.cpp:2795 | Latent. jagged or jittery edges | Widen by the PCF kernel's footprint (kernel texels x the texel's angle), not by 10%. | added |
| kPointShadowNear as the spot shadow near plane (RenderShadowMaps, raster) | Scene.cpp:2804 | Latent. no visible effect | Near = the light's own SourceRadius (nothing inside the emitter can occlude it), floored by depth-format precision relative to Range. | added |
| cascade near distance (RenderShadowMaps, raster) | Scene.cpp:2971 | Latent. jagged or jittery edges | Use the camera's near clip (already in hand as camera.GetProjection() at L2965) as the split scheme's start | added |
| POINT_SHADOW_NEAR | pbr_fragment.glsl:465 | Latent. wrong brightness or colour | Near plane = the light's physical source radius (Direction.w, SourceRadius): | added |
| raster refraction push | pbr_fragment.glsl:6431 | Latent. smear | Iterate the screen-space refraction to the measured backdrop depth along the refracted ray (one or two secant steps), guarded by the frame edge; | added |
| raster refraction foreground rejection | pbr_fragment.glsl:6443 | Latent. smear | A relative tolerance: a few ulp of the stored depth at clip.w, or the pixel footprint times the depth slope. | added |
| raster shadow-map capacity | scene_block.glsl:46 | Latent. wrong brightness or colour | Assign the map budget per view by importance (screen-space contribution), with the budget itself an owner setting (atlas texels). | added |
| SsrMaxDistance (engine default) | PostSettings.h:534 | Latent. wrong brightness or colour | Bound the ray by the screen: its projected extent to the frame edge. | added |
| GiRadius (engine default) | PostSettings.h:571 | Latent. wrong brightness or colour | Express the gather footprint as a share of the screen (its natural domain, since the data is the screen), with the tap count as the cost control, not metres. | added |
| Voxel GI defaults (resolution / cascades / voxel size) | PostSettings.h:673 | Latent. wrong brightness or colour | Derive the cascades from the view: the outer cascade covers the far plane or shadow distance, and the voxel size follows from resolution and reach, so the clipmap spans the visible world by maths. | added |
| SsrThickness (engine default) | PostSettings.h:539 | Latent. smear | Make the thickness the depth extent of one pixel's footprint at that depth: | added |
| kSsrHiZLevels / kSsrHiZFineLevels | PostProcess.h:525 | Latent. cost | levels = ceil(log2(max(traceWidth, traceHeight))), the full mip chain, so a coarse step always spans a fixed share of the screen. | added |
| kBiasPerMetre (SSAO) | ssao_compute.rvshader:80 | Latent. wrong brightness or colour | Per-pixel slope slack from the depth texel's own span (as rtao does), plus an ulp-level precision term. | added |
| kBias (SSAO) | ssao_compute.rvshader:79 | Latent. wrong brightness or colour | The same per-pixel slope slack and ulp term as above. | added |
| SameSurface tolerance | ssao_blur.rvshader:56 | Latent. noise | Distance to the tap's plane instead of a depth ratio. | added |
| SSGI distance falloff | ssgi_compute.rvshader:315 | Latent. wrong brightness or colour; first to fix | A cosine/pdf-weighted gather with no distance term (the traced bounce's estimator), which is independent of world units. | added |
| SSGI behind-reject | ssgi_compute.rvshader:308 | Latent. wrong brightness or colour | The same per-pixel slope slack as SSAO. | added |
| kRatio (cone aperture) | voxel_cone.glsl:198 | Latent. wrong brightness or colour | The aperture the cone layout needs to tile the hemisphere, with the near-wall footprint problem it was tuned around fixed at its cause (mip selection or occlusion), not by narrowing the cone. | added |
| voxel size clamp | VoxelGI.cpp:466 | Latent. wrong brightness or colour | Guard only against non-positive or NaN values (a numerical guard), and let the grid follow the setting. | added |
| kMaxIterations | ssr_trace.rvshader:101 | Latent. wrong brightness or colour | Derive the cap from the trace itself: enough iterations for the level-0 walk to reach the trace's diagonal, plus a log2(diagonal) allowance per level change. | added |
| kStride | ssr_trace.rvshader:102 | Latent. jagged or jittery edges | A level-0 walk that visits every texel the ray crosses (a DDA with a 1-texel step, the pixel-unit definition). | added |
| kStrideGrowth | ssr_trace.rvshader:111 | Latent. jagged or jittery edges | Let the stride grow only up to the reflection lobe's projected footprint in texels: | added |
| rough-surface step budget | ssr_trace.rvshader:253 | Latent. wrong brightness or colour | Tie the budget to the same screen-derived reach as the main cap, so rough rays can reach as far on screen as the lobe's weight needs, not a fixed fraction of a fixed count. | added |
| kMaxBlurPixels (with roughness^2 at L79) | ssr_resolve.rvshader:55 | Latent. jagged or jittery edges | Size the disc from the lobe: radius_px = tan(the GGX lobe's half-angle for this roughness) x distance travelled / hit depth x focal length in pixels. | added |
| kBias | ssgi_compute.rvshader:108 | Latent. no visible effect | Delete it. The Radius bound at L299 (an owner setting) is the real limit. | added |
| kBiasPerMetre | ssgi_compute.rvshader:109 | Latent. no visible effect | Delete it along with kBias. The Radius bound decides acceptance. | added |
| kAgreement | ssgi_compute.rvshader:223 | Latent. noise | Blend rather than switch: weight the written normal by how far its angle is inside the lift bound (derived from the kernel's 0.4 lift), so there is no hard threshold to flicker across. | added |
| SameSurface relative depth tolerance | ssgi_blur.rvshader:55 | Latent. noise | A plane-aware edge stop: compare the neighbour's depth against the centre's depth extrapolated along its screen-space depth gradient (the SVGF depth weight abs(dz) / (/grad z . | added |
| normal transform = mat3(Model) | voxelize.rvshader:73 | Latent. wrong brightness or colour | Use the inverse-transpose of the model's upper 3x3 (maths), the same normal matrix the lit pass uses, sent per instance or computed in the vertex shader. | added |
| map sampled at lod 16 (the texture's mean) | voxelize.rvshader:182 | Latent. wrong brightness or colour | Sample at the voxel's own footprint. This pass rasterises one pixel per voxel, so dFdx/dFdy(uv) taken before the dominant-axis return (in uniform control flow, as planeNormal already is at L152) are the per-voxel uv footprint, and textureGrad with them gives the right ... | added |
| write policy: last writer wins | voxelize.rvshader:198 | Latent. flicker | Accumulate instead of overwrite: the atomic running average of Crassin & Green (OpenGL Insights ch. | added |
| voxel sun-shadow normal offset (slope floor 0.15, cap 4.0, Trace.w lane) | voxel_inject.rvshader:104 | Latent. wrong brightness or colour | Offset by the voxel's own half-diagonal (sqrt(3)/2 x voxel size, maths) plus the shadow-texel term, or test the position of the surface stored in the voxel instead of the voxel centre. | added |
| only the cascaded sun is shadowed in the grid | voxel_inject.rvshader:181 | Latent. wrong brightness or colour; first to fix | Shadow every injected light the way the lit pass does: the same local shadow assignment (ShadowMap::GetAssignment) looked up at the voxel. | added |
| per-voxel loop over every light | voxel_inject.rvshader:147 | Latent. cost | Cull lights per cascade or brick by range against the cascade box, as the lit pass's light list does, so cost follows the lights that can reach, not the count. | added |
| multi-bounce feedback from last frame (Grid.z >= 1.5) | voxel_inject.rvshader:190 | Latent. smear | Compute the second bounce within the frame: inject, build the mip chain, inject again reading this frame's chain. | added |
| last frame's chain read with this frame's cascade origins | voxel_inject.rvshader:194 | Latent. flicker | Keep each chain's own origins and address the previous chain with the previous frame's origins. | added |
| cone step and uncorrected opacity | voxel_cone.glsl:166 | Latent. wrong brightness or colour | Opacity correction for the step length, alpha_c = 1 - (1 - alpha)^(step / footprint), the standard volume-rendering correction used in the voxel cone tracing reference. | added |
| cascade origin snap | VoxelGI.cpp:478 | Latent. flicker | Snap each cascade to the texel of the coarsest level it is read at (2^maxMip voxels, maths). | added |
| voxeliser target = N x N, no conservative rasterisation | VoxelGI.cpp:260 | Latent. wrong brightness or colour | Conservative rasterisation (VK_EXT_conservative_rasterization, overestimate), or the published triangle dilation (Hasselgren et al., GPU Gems 2 ch. | added |
| kAgreement (voxel gather) | voxelgi_gather.rvshader:133 | Latent. noise | Derive the tolerance from this gather's own geometry (the lowest cone edge's elevation, 30 - 16 = 14 degrees), or blend by agreement instead of switching. | added |
| hit snapped to the half-res texel centre (sub-texel crossing discarded) | ssr_trace.rvshader:307 | Latent. jagged or jittery edges; found by the skeptic | Write the continuous crossing, hitUv = (P0 + D*tCross)/baseSize. The crossing is already exact for planar depth, so no quantisation is needed. | added |
| kAgreement | ssao_compute.rvshader:194 | Latent. flicker | Derive the tolerance from the kernel itself: accept the written normal while its tilt from the geometric one is below the minimum tap elevation, cos(atan(lift)). | added |
| spiral rotation seed (pixel only, no frame term) | ssao_compute.rvshader:216 | Latent. noise | Seed the rotation with the frame index whenever a temporal accumulator follows it, as RTAO already does, so noise reduction depends on accumulated sample count and not on motion. | added |
| off-screen tap policy | ssao_compute.rvshader:240 | Latent. wrong brightness or colour | Estimate from the taps that landed: ao = 1 - occ/valid, the unbiased estimator over the available samples, with the no-tap case as 1. | added |
| kRadius | ssao_blur.rvshader:49 | Latent. noise | Fixing the frame seed (ssao_compute:216) restores the premise. Beyond that the width should follow the noise the accumulator leaves, not a remembered screen width. | added |
| bilateral depth epsilon | ssao_apply.rvshader:87 | Latent. banding | The same SVGF-style gradient-normalised depth weight as the blur. A tap is judged against the depth predicted by the pixel's plane, in units of one texel of that plane's slope. | added |
| AlphaCutoff (material lane) and alpha product | shadow_depth_masked.rvshader:86 | Latent. jagged or jittery edges | Coverage-preserving alpha mips in the texture cook (Castaño 2010, rescale each mip's alpha so the fraction above the cutoff is kept), or hashed alpha testing (Wyman and McGuire 2017). | added |
| kPointShadowNear | ShadowMap.h:63 | Latent. wrong brightness or colour | Tie the near plane to the light's own physical extent, which is already authored (Light::SourceRadius): | added |
| kMaxLocal | ShadowMap.h:140 | Latent. wrong brightness or colour | Rank casting lights each frame by their contribution to the view, for example intensity × the screen coverage of the range sphere. | added |
| cascade split start floor | ShadowMap.cpp:409 | Latent. jagged or jittery edges | Anchor the log split on the camera's own near clip, as PSSM (Zhang 2006, a reference) defines it, and leave the distribution to the owner's lambda. | added |
| cascade radius quantum | ShadowMap.cpp:452 | Latent. jagged or jittery edges | Quantise relative to the radius, for example round up to a fixed fraction of its power-of-two bracket, so the guard against float jitter is scale-free. | added |
| caster pull-back margin | ShadowMap.cpp:459 | Latent. wrong brightness or colour | Pancake the casters: enable Vulkan depthClamp on the shadow pipelines, or clamp gl_Position.z in shadow_depth, and drop cull_casters' near plane. | added |
| DepthBiasSlope (adjacent) | Renderer3D.cpp:9007 | Latent. jagged or jittery edges | Derive the slope factor from the lookup's footprint (the PCF reach in texels), or use receiver-plane depth bias in the lookup, so no magnitude is tuned. | added |
| **RT2-45** · The post chain on general forms |  |  |  |  |
| FrameDesc::ClearColor default | FrameGraphBuilder.h:318 | Latent. wrong brightness or colour | The background for SkyType::Color is black, matching the black cube the same sky type gives rays, or a scene-authored colour in SceneEnvironment used by both the clear and the ray miss | added |
| AutoExposureMinLog / AutoExposureMaxLog (engine defaults, active on the bridge) | PostSettings.h:392 | Latent. wrong brightness or colour; first to fix | Derive the histogram span by maths from the owner's own settings: MinLog = log2(Key / AutoExposureMax) and MaxLog = log2(Key / AutoExposureMin), which are exactly the luminances the exposure clamp can respond to. | added |
| MaxBokehRadius (engine default) | PostSettings.h:523 | Latent. jagged or jittery edges | State the cap as a share of frame height, so the owner's value means the same blur at every resolution, and scale the gather's tap budget with it. | added |
| kGradientCubeSize | Skybox.cpp:169 | Latent. banding | The sky is analytic. Evaluate GradientAt at the reflection miss direction (sky.rvshader already has it), or size the cube from the sharpest analytic feature (texels per face >= a Nyquist count over the moon's angular diameter). | added |
| Bin-0 cutoff 0.0001 | luminance_histogram.rvshader:48 | Live. wrong brightness or colour; first to fix | Bin 0 is 'below the owner's metering range': return 0 when log2(luminance) < Range.x (the AutoExposureMinLog setting). | added |
| ceiling = max(Clamp, Threshold) | bloom_prefilter.rvshader:93 | Live. wrong brightness or colour | Threshold, knee and ceiling in exposed units: multiply the prefilter's input by the frame's exposure (the same u_Exposure buffer tonemap reads), so bloom follows what is on screen whatever the scene's absolute light level. | added |
| Shutter lane (velocity is per frame) | motionblur_gather.rvshader:50 | Live. smear | The setting is an exposure time in seconds (a shutter speed). Blur = (velocity per frame / frame delta) * exposure time, using the same deterministic delta auto exposure uses (desc.DeltaSeconds), still capped by MaxRadius. | added |
| kSoftZ | motionblur_gather.rvshader:62 | Live. smear | Judge 'in front' scale-free: by relative depth (za/zb), with the transition width taken from the local depth slope over the tap's pixel distance (what one surface's own slope explains). | added |
| KarisWeight 1/(1+luma) | bloom_prefilter.rvshader:71 | Latent. flicker | Evaluate the weight on exposed luminance (luma * frame exposure), the domain the reference constant was defined in. | added |
| kSkyDitherCompensation | sky.rvshader:268 | Latent. banding | Delete the option, since the tonemap's display-domain dither already covers it. | added |
| kTaps | motionblur_gather.rvshader:57 | Latent. banding | One tap per pixel of reach: the loop runs ceil(reach) steps per side, bounded by MaxRadius. | added |
| kTaps | dof_gather.rvshader:54 | Latent. noise | Derive the count from the disc area (about pi*R^2 taps at one per half-res pixel, rings = R), or keep N and prefilter the source to the tap spacing sqrt(pi*R^2/N), so every tap represents its share of the area. | added |
| kDiagonalMargin | smaa_weights.rvshader:84 | Latent. jagged or jittery edges | Either drop the diagonal pass (at 8 it is measured equal to off), or use the reference SMAA diagonal classification (Jimenez 2012: | added (decided 2026-09-25: the diagonal pass is removed, RT2-45) |

## 11. How RageV compares with the top engines

*The comparison of 2026-09-25, in three phases: a survey of what the top engines ship and what research has found since (with sources); a comparison of every topic against RageV's code; and a verification that re-checked every claim against the code and the primary talks (MegaLights 2025, Lumen 2022, Snowdrop GDC 2024, Hillaire 2020, kept in `build/rt_research/sources/`). This section gives the verified verdicts only; `docs/RT-ROADMAP-RESEARCH.md` has the sources, the code citations, every claim's verdict and correction, and the gaps the research could not close.*

**How to read the verdicts.** *Behind*: the top engines ship something better. *Missing*: RageV has nothing for it. *Equal*: the same method, or a different one of the same standing. *Not comparable*: no shipped equivalent, or a different tier (path tracing, research). *Better*: none. The comparison first listed fourteen topics where RageV looked better; the verification refuted eight, found four only partly true and could not support two, so no "better" verdict survived in full. Where a topic was corrected, the table says what it became.

**The candidates that could beat both** RageV and the shipped baseline are ideas the comparison proposed; the verification found 19 plausible (they need their measurement), 8 partly true (plausible after a correction) and 3 refuted (the top engines already ship them, so they became baselines). Each surviving candidate is an arm with a gate, never a default until it wins.

### A. Direct light, many lights, shadows

RageV's direct light already has MegaLights' shape: a few lights picked per pixel by what each is worth, one ray per distinct light, the analytic light without shadows times that ray, and its own accumulator. It is behind in how it picks (its cost grows with the lights near a pixel and, at ray hits, with every light in the scene), in how it treats area lights and glowing meshes, and in how it cleans up (no spatial filter, so young pixels lean on TAA). Every shipped system leans on its final temporal pass at the end (MegaLights on TSR; the RTXDI titles on DLSS Ray Reconstruction), so the owner's rule 3 has no shipped precedent here.

| Verdict | Topics |
|---|---|
| Behind | Per-pixel candidate cost with many lights; samples and rays per pixel; random numbers for picks and source points; the area-light model (no rectangles, no soft sun, points at hits); the point on the source for the shadow ray; the directional light; lighting at ray hits; the light grid's build; emissive geometry as lights; counting a fixture once; the neighbourhood clamp and demodulation; the spatial filter and young pixels; cost |
| Missing | Visibility guiding of light picks |
| Equal | The selection score (linear, term-shaped; its tube and sphere mismatch is a live defect); the shading form (analytic light x one ray, as MegaLights ships); view-dependent specular in the history (both lean on the final pass); sample reuse (RT-24's pool is RTXDI's spatial math); alpha-tested occluders; range and cutoffs. **Corrected:** history invalidation (measured change) was claimed better; it is the same gradient RTXDI's SDK ships and Quake II RTX shipped, and RTXDI filters it first (A-OB1 refuted) |
| Not comparable | Textured emitters (finer than anything shipped, on a narrow class of meshes); the ray-tracing representation near the camera; baked direct light with moving-object loss rays. **Corrected:** the every-light reference arm was claimed better; it checks light selection, not the true picture, and Unreal keeps both a non-stochastic path and a path tracer to compare against (A-OB2 refuted) |

| Candidate that could beat both | Verification | Where it is tested, and its gate |
|---|---|---|
| History of visibility only, with the light without shadows recomputed every frame | Partly: the ratio is biased (consistent), and its exact part needs an enumerated candidate set | RT2-24 step 2 (diffuse only): adopted only if it beats step 1 at equal time on `emitter_lag.py`, the owner's swing, `--aa=none` convergence, T5 and the reference |
| One world-state light BVH for every ray hit and the hidden candidates | Plausible (Moreau's timings name no GPU) | RT2-12: flat cost from 1k to 10k lights, and grain at or below the cascaded-grid baseline (Doom's form, rule 1) at equal time |
| Spatial-only pairwise-MIS pooling for primary direct light | Plausible (evidence is from reflections only) | RT2-12b's later arm: lower grain and speckle at equal time, the parked picture unchanged |
| Highlight-aware points on area lights (Peters 2021) | Plausible (papers, not shipped) | RT2-10's later arm: lower variance over the highlight at equal rays under `--aa=none`, within grain of the reference |

### B. Reflections

RageV is ahead of Lumen 2022 and Snowdrop on lamps inside reflections (every emitter aimed at, with MIS), but that MIS aims at rectangle stand-ins, so it is behind the RTXDI-class path tracers until RT2-11. It is behind the top engines on cost (every glossy pixel traced at full rate, a resolve as costly as the trace), on hit shading (one random light per hit, the sharpest mip), and on the temporal design (about twenty speed-keyed rules where NRD has one confidence blend, a fast history, a history fix and a stabilisation pass). Lumen's and MegaLights' reflections both finish in their final temporal pass; only NRD's design meets rule 3.

| Verdict | Topics |
|---|---|
| Behind | The ray direction sampler; the sampling density handed to the resolve; where rays are spent; rough surfaces and the roughness split; hit shading; each fixture counted once in specular; the spatial reuse's cost; the depth used to place the virtual image; reflector curvature; history candidates and how they combine; history length control; checking the content under camera-only motion; the fast history; young pixels; the post-temporal spatial filter; self-sufficiency (rule 3); the specular anti-aliasing input; texture LOD at hits; the pass structure and cost (the top engines cost 5 to 10 times less per pixel); the translucent layer's reflections; the raster fallback's SSR (kept at parity by rule) |
| Missing | Screen-space tracing before the ray query (a deliberate choice, section 7 item 28); temporal stabilisation after the blur |
| Equal | Spatial pooling of lamp samples; demodulation; firefly handling (relative clamps, minus the 64 cap); the history fetch filter; half-float history precision. **Corrected:** anti-lag (measured change) was claimed better; NRD's README prescribes the same re-traced gradient and its sample implements it (B-OB2 refuted) |
| Partly better | Lamps inside reflections: ahead of Lumen 2022 and Snowdrop only (B-OB1 partly) |
| Not comparable | Accumulating on the unjittered grid (RageV's own, kept); neural joint denoisers (they are the final pass, which rule 3 forbids) |

| Candidate that could beat both | Verification | Where it is tested, and its gate |
|---|---|---|
| Stored light at wide footprints: one estimator across roughness | Partly: Snowdrop is not discontinuous (it fades and dithers); the store must answer a lobe, so the rule applies at hits | RT2-27: the roughness ramp with no step above 1 level; bias no worse than v1; cost lower |
| Ray rate from the lobe's footprint | Plausible (an argument, not a result) | RT2-27: speckle and edges no worse at the owner's swing; the cable band; no blotchy resolve noise at 1/4 and 1/16 |
| One specular signal, lamps counted once by MIS | Partly: it is RTXDI and NRD practice, and needs one representation per fixture | RT2-23g: the sum of the two arms equals the full picture; speckle, `emitter_lag.py` and cost below today's |
| Measured change as the history confidence | Refuted as beyond the baseline: it is NRD's documented input | RT2-23's step 23b uses it as the baseline, beside the geometric tests |
| RT-24's lobe-agreement weight as the roughness confidence | Plausible (only ever shown equal) | RT2-23's step 23b: it must beat NRD's form on the pan and orbit test, or NRD's form is used |

### C. Bounce light (GI), radiance caches, the bake

The top engines that ship ray-traced bounce light without a bake (id Tech 8, Snowdrop, Anvil) all use one ray per traced pixel, a world cache that every hit reads, spherical-harmonic output and short rays; RageV casts up to 4 rays per traced pixel, shades every hit in full, has no cache that hits read, and switches the live bounce off entirely in baked scenes. The shipped bakes place sparse bricks automatically and track stale objects; RageV's allows 8 hand-placed boxes and cannot see a moved wall. The owner's two-layer design (D4) has only partial precedents (Unreal's Stationary lights, Unity's baked plus Enlighten), so its additivity is proven by diff images.

| Verdict | Topics |
|---|---|
| Behind | The per-pixel gather (rays and resolution); ray length and what a miss returns; shading at a gather hit; multi-bounce for the live layer; the gather's output and upsample; GI outliers and clamps; the light lookup at hits; the composition of indirect terms; the bake's placement, coverage and scaling; stale-bake detection; reflection probes as a light source; AO over indirect light; realtime GI at kilometre scale |
| Missing | A world-space radiance cache read at hits; contact-scale baked detail (lightmaps); screen-space tracing or last frame's lit screen at hits (held back by D4, section 7 item 34) |
| Equal | The bake's leak control (relocation is a trade-off in RageV's own measurement, and Snowdrop shipped without it); the per-tile ray budget. **Corrected:** anti-lag (measured change) was claimed better; NRD documents the same recipe (C-OB1 refuted), and under the project's baked default it never runs. What a cell stores was claimed better; Unreal's volumetric lightmaps also store L2 harmonics, so RageV's separate baked-lamp set is a different trade, not a lead (C-OB3 refuted) |
| Partly better | Emissive sampling in the gather: ahead of Lumen 2022, Snowdrop and id only once its own defects are fixed, and it clamps too (C-OB2 partly). The bake solved by the frame's own hit shader: consistent with the frame by construction, but less accurate than a reference baker (C-OB4 partly) |
| Not comparable | Mixed lighting (the Hybrid light has no shipped equal); path-tracing-tier GI; surface caches and surfels |

| Candidate that could beat both | Verification | Where it is tested, and its gate |
|---|---|---|
| The bake gathered at the live rays' hits | Partly: it won on the fixture and lost on the showroom's bright bay; its cost is about 20 to 150 fetches a read | RT2-25e (on at High, D18): contact bleed below 1.9%; the bright bay no worse than realtime; leaks at 0.000; at most 0.3 ms; a live review in the garage |
| Moving objects subtracting and adding baked light | Plausible (no shipped precedent; SH ringing) | RT2-25f (D19): the diff around moving objects better than today's AO-only result, no region brighter than the reference, at most 0.3 ms |
| Each cache cell restarted by a noise-relative change test | Partly: DDGI already ships a per-probe ratio threshold | RT2-25c: against DDGI's threshold as the baseline arm, `emitter_lag.py` and flicker no worse |

### D. Denoising, temporal reconstruction, anti-aliasing, post

RageV's per-signal accumulator, its demodulation and its stochastic half-float rounding are the same structure the top engines ship. It is behind on the parts that make a signal stand alone: no signal rebuilds its young pixels, the diffuse signals have no outlier stage, and its TAA is a min/max box widened by hand, tested against a fixed depth fraction, with brightness weights on absolute light. It has no upscaler (a fallback only, D8) and a per-channel tone curve. The primary talks settle that Lumen and MegaLights both rely on their final temporal pass, so rule 3 is stricter than Epic's shipped design.

| Verdict | Topics |
|---|---|
| Behind | The history fix for young pixels; the variance-guided spatial post-filter; outlier suppression before accumulation; random numbers for ray decisions; TAA's box; TAA's blend and brightness weighting; TAA's flicker handling (RT2-8 fixes it as TSR does); the tone curve |
| Missing | A fast-history clamp (NRD's choice, not universal); pre-exposure and exposure-scaled weights; velocity dilation; temporal upscaling (a fallback only) |
| Equal | The per-signal accumulator; the accumulator's disocclusion test (richer, but in metres); demodulation; history precision; TAA's history test and history fetch; the reflection composited before TAA (both lean on the final pass today); auto exposure; bloom's filter chain; motion blur and depth of field; spatial AA. **Corrected:** detecting real lighting change was claimed better; it equals NRD's documented practice and is ahead only of Epic's heuristics, which has not been measured (D-OB1 partly) |
| Not comparable | What the final pass is asked to do (the owner's generic-lane rule is stricter than anything shipped); neural joint denoise and AA; the cost of the passes (different GPUs) |

| Candidate that could beat both | Verification | Where it is tested, and its gate |
|---|---|---|
| Every signal complete before composition, the reflection added after the AA pass | Plausible: the weight rides through the pass, and every after-resolve arm so far was grainier | RT2-28c: speckle and edges no worse than the 16-ray reference; the reflection's picture term unchanged with the frame filter's memory off; D23 if it fails |
| Measured change as the one change rule, into one share-weighted lane | Plausible: equal to NRD practice except the share weighting | RT2-47 (AO's record): a switched lamp's AO and GI reach 90% of their new value within 2 frames; RT2-28d: the wall beside the moving light restarts only by the reflection's share |
| Disocclusion by the predicted previous depth | Refuted as beyond the baseline: NRD ships it with a slope-aware tolerance | RT2-28a builds NRD's form first; the one-texel form is an arm against it |
| Filter-adapted noise (FAST) | Refuted as beyond the baseline: MegaLights cites FAST for its own noise | RT2-7b uses the published masks as the baseline; masks tuned to RageV's filters are an arm |

### E. Ray-tracing infrastructure and frame architecture

RageV chose the right trace primitive (inline ray queries, which Lumen reported as a noticeable speedup on some consoles), marks geometry opaque by default, uses its static and moving masks, and traces the same geometry the raster draws (so it has none of the self-intersection Lumen and Snowdrop fight). Every other layer is behind: a TLAS refit even when its contents change, BLASes built lazily with a stall and never compacted, skinned meshes refit forever, full-screen fragment passes with no compaction, an ordered-list render graph, one queue, no memory budget, no pipeline cache, no GPU scene. Unreal 5.8's guide still rebuilds the TLAS every frame, which is the shipped baseline RT2-35 adopts.

| Verdict | Topics |
|---|---|
| Behind | The TLAS build policy; where TLAS instances come from; static BLAS build, compaction and pooling; skinned BLAS policy; BLAS granularity (kept as the artists build it, D25); opacity flags and the emitter shells; far field and ray LOD; fragment against compute dispatch; hit shading; the render graph; barriers; queues and async compute; the memory budget and aliasing; pipelines and caches; the binding model; the GPU scene; large-world precision; a per-view frame index |
| Equal | Skipping the TLAS build when nothing changed: neither Unreal nor RageV skips today; RageV's measured-change key cannot serve as-is (computed after the build, changes with any skinned mesh, E-N2), so RT2-4 builds one; instance masks (they are used); ray queries against ray-tracing pipelines with SER |
| Not comparable | TLAS instance culling (RageV's choice is more correct at demo scale; D24 limits any culling to instance sets); ray budget allocation (a more general demand signal, but only ever adds rays); Mega Geometry, opacity micromaps, position fetch and work graphs (only OMM is a portable shipped baseline). **Corrected:** the acceleration-structure robustness tooling was claimed better; it runs only with validation on, and the other half cannot be shown (E-OB1 unsupported) |

| Candidate that could beat both | Verification | Where it is tested, and its gate |
|---|---|---|
| A change-driven TLAS: skipped when nothing changed, otherwise fully rebuilt | Partly: better only on still frames; at a million instances the partitioned TLAS is the shipped high end | RT2-4's arm, RT2-35 (no build on still frames; the rebuild no slower than today's refit), RT2-39 (the partitioned-TLAS arm) |
| Per-texel ray lists driven by the accumulators' own demand | Partly: RT-9's per-texel record showed no picture gain, and compaction recovers only the divergence share | RT2-20b: a picture gain first, then cost at equal rays |
| Far field in a second TLAS | Plausible (current Unreal may already separate it) | RT2-38: built only if Headland's reflection trace misses its section-6 line with near-field-only content and rays reaching far-field instances take more than half of that trace time; near-field cost unchanged with it present |

### F. Materials, G-buffer, composition

The GGX and split-sum core matches Unreal's and Filament's, and RageV already ships Tokuyoshi and Kaplanyan's geometric specular anti-aliasing, for analytic lights only. It is behind on the rest of the material pipeline: three Smith terms, no energy compensation, normal-map variance discarded at cook, no geometric normal, ids in half floats, meanings packed into float bits, and a second raster to compose the frame. Its ray hits are richer than Snowdrop's untextured ray world and Lumen's surface cache, but behind Battlefield V and Unreal's hit lighting (no normal maps, the sharpest mip).

| Verdict | Topics |
|---|---|
| Behind | The Smith term; geometric specular AA's coverage (analytic lights only); the G-buffer's lane contract; object identity lanes; composition in RT mode; the reflection's demodulation and remodulation; detail at ray hits; the tangent frame; ray-origin offsets; emissive in the composition. **Corrected:** the normal's precision was claimed better; the half-float lane is coarser than HDRP's 12-bit lanes over half its range (F-OB2 refuted). The material at ray hits was claimed better; hits have no normal maps and read the sharpest mip, which is above the cheap defaults (Snowdrop's untextured world, Lumen's surface cache) and behind Battlefield V and Unreal's hit lighting (F-OB3 refuted) |
| Missing | Energy compensation; normal-map variance in the roughness mips; a geometric normal lane; a shading-model id; a raster-to-hit parity test |
| Equal | The GGX distribution; Fresnel and F0; the diffuse model (Lambert is a shipped choice); split-sum lighting; the minimum roughness floor (Filament clamps too; both reasons, precision and aliasing, are documented); diffuse demodulation; the coat, sheen and anisotropy models; GGX importance sampling. **Corrected:** the environment mip from the reflection vector's derivatives was claimed better; it is an equivalent, cheap guard, since roughness widening of the same kind ships elsewhere (F-OB1 unsupported) |
| Not comparable | MSAA of G-buffer lanes (the deferred top engines offer MSAA only in forward; the owner keeps it in RT mode, D26); hit-shading coherence; the visibility buffer; Substrate's layered G-buffer and neural materials |

| Candidate that could beat both | Verification | Where it is tested, and its gate |
|---|---|---|
| One specular anti-aliasing budget for every consumer, pixels and ray hits alike | Plausible: HDRP has ray-cone mips at hits but no roughness filtering there | RT2-21c: with `--aa=none` at the owner's swing, edge jitter and speckle no worse than the 16-ray reference (after RT2-28a separates the depth test) |
| Parity by construction: one material function for the raster and every hit | Plausible (a target: today's hits are far from parity) | RT2-21d: the primary-ray parity test within one quantisation step per lane |
| Split geometry normals (smooth for curvature, face for offsets) | Plausible (byte-neutral only with ray-traced GI on, which the RT recipe now assumes) | RT2-21a: the floor's image distance within 5% of the flat-plane value (`--debug-view=reflection-image`); RT2-21e: zero acne at the origin and 10 km |
| EON diffuse | Plausible, but needs a diffuse-roughness input RageV's materials lack | Section 7, item 50 (revisit when content has it) |
| 16-bit UNORM normal lanes | Plausible (justified by the precision arithmetic) | RT2-21a: a curved chrome pole within 1 level of a float32-normal build |

### G. Glass, water, fog, particles

RageV's nearest glass pane already runs the shared direct and reflection passes with its own histories, which is Snowdrop's shape and steadier than Unreal's front layer (which runs its denoiser off and leaves the noise to TSR). Everything else is behind: glass and water are not in the ray structure, glass cannot tint, its coverage follows the reflection's brightness, the sea's rays are averaged only by TAA, the waves are a fixed grid with no levels of detail, fog is one pass after TAA, and particles are unlit. The claim that RageV leads on many lamps on water did not survive: it rested on absence of evidence, and its fixed memories break rule 2.

| Verdict | Topics |
|---|---|
| Behind | The panes behind the nearest; the glass composite (coverage and tint); the order-independent transparency method (weighted blending is the most expensive in Activision's table); refraction; the sea's mirror reflection; the water surface's generator and LOD; caustics; the fog model; fog along reflections and refractions; fog on see-through surfaces; the order of glows, particles and the temporal pass |
| Missing | See-through surfaces in the ray structure and tinted shadows; the sky and atmosphere; lamp light scattered in fog (WR-11, deferred by the owner); heterogeneous volumes; particle lighting |
| Equal | The nearest see-through surface on the shared passes; sub-pixel water detail folded into roughness; the underwater medium; foam; displaced water in the ray structure (no one ships it); the TAA reliance of see-through signals (both lean on it today). **Corrected:** many lamps on water was claimed better; refuted, because the claim rested on finding no shipped equal, held on the bridge only, and its fixed memories break rule 2, so it is treated as level until measured (G-OB1) |
| Not comparable | Light glows (an energy-correct stand-in for lamps smaller than a pixel, which the sources never address; its taste curves are the defect); primary surface replacement |

| Candidate that could beat both | Verification | Where it is tested, and its gate |
|---|---|---|
| Refraction in screen space first, traced only where it fails | Plausible (Snowdrop, not id Tech 8, is the precedent, and reports seams) | RT2-31e: the pier against the reference within grain, with no structure along the seam |
| The nearest see-through surface composited exactly | Plausible (its cost framing corrected) | RT2-31b: front-layer error 0 on the glass-over-glass fixture; Glitter's over-brightness gone |
| Tinted shadows in one ray query | Partly: glass and water must first enter the ray structure, and AO must exclude them | RT2-31a: the camp byte-identical; the tinted-pane shadow near zero against the reference |
| One fog integral per path segment | Plausible (exact only where the fog cap is 1) | RT2-33: the far bridge fogged in its sea reflection, within grain of the reference with the same once-per-path cap |
| One fixed mirror ray for the panes behind the nearest | Plausible (needs stored light for Realtime lamps) | RT2-31f (D28): no frame-to-frame change on a still camera under `--aa=none`; judged live |
| A per-particle light accumulator | Plausible (GPU-simulated emitters only) | RT2-48's arm, only if the cache shows cell structure on the camp's smoke |
