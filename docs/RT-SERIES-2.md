# RT-series 2: roadmap

*Roadmap for the owner, 2026-09-24, with the owner's answers to its twelve decisions (section 5). Nothing in it is built yet. Sources: eight code-area reviews, each checked by a skeptic (164 findings survived); the inventory of 45 open issues; a survey of what current RT-first renderers do; a fresh benchmark on this laptop; and a critic's review of the first draft, whose points were each checked against the code before being applied (the last section lists what changed). The evidence behind every finding and issue id (file and line, the skeptic's verdict, the fix direction) is in `docs/RT-SERIES-2-FINDINGS.md`, with the benchmark and the reference survey. RT-24's work is uncommitted: `include/pbr_fragment.glsl`, `reflection_accumulate.rvshader`, `tools/scripts/garage/watch_arm.py`, `docs/HANDOFF.md` and `docs/RT-SERIES.md` are modified, and `SampleProject/Source/Orbiter.cpp`, `spin_measure.py` and `ghost_map.py` are untracked.*

## The short version

- **RT-24 comes first, and nothing else starts until it is committed.** RT2-0 finishes it inside today's renderer, one measured arm at a time. The first arm is the one the owner named: test the frame filter's three rules one by one on the clean swing test. A new finding bears on it directly: the reflections' "has anything moved" test compares last frame's camera with last frame's camera, so a camera swing in the runtime never gets the extra reflection rays RT-9 was built to give. That touches two open RT-24 symptoms, the late settle and the noisy tube reflections. RT2-0 also carries its own fallback arms, so no RT-24 symptom has to wait for a rewrite.
- **Next: trustworthy instruments, correct inputs and early wins** (RT2-1 to RT2-9).
  - Failures become loud, the engine reports its VRAM, and there is a validation-clean gate.
  - A reference renderer that does not share the filters it judges, and one harness that renders every test camera and diffs it against a stored picture.
  - Object ids become exact and stable. Today they stop being exact above 2,048 objects, and at 60,000 objects only 10% of glossy pixels keep their reflection history.
  - Random numbers move in every anti-aliasing mode, so "no reliance on TAA" can be tested at all.
  - Cheap fixes that do not need a rewrite land here instead of waiting: the behaviour-based flicker floor that repairs the bridge water regression (ISS-1), the shader and pipeline caches, and small CPU wins.
- **Then one light model** (RT2-10 to RT2-13). One shared light library. Area lights shaded as areas, at ray hits too. Every glowing surface becomes a light that can be sampled, and each fixture is counted once. Picking lights stops getting more expensive as lights are added.
- **Then a real frame** (RT2-14 to RT2-20). First, one measurement settles whether ray passes should be compute passes. Then a render graph that schedules, views that own their histories, signals described as data, a bindless RHI with uploads off the frame path, a memory budget, and ray passes in compute where they win. The screen shows no change, but everything after builds on it.
- **Then the signals rebuilt on that frame** (RT2-21 to RT2-30):
  - a designed G-buffer and one BSDF;
  - one composition pass (RT-2.2 first);
  - a new reflection denoiser;
  - shadow visibility reconstructed separately from shading;
  - bounce light in two layers: the bake for baked lights, and a live pass on top for Realtime lights (the owner's design, D4);
  - reflections tiered by roughness;
  - a final temporal pass that only anti-aliases;
  - an internal render resolution with temporal upscaling.
- **Then glass and water inside the ray-traced world** (RT2-31 to RT2-33).
- **Then scale** (RT2-34 to RT2-43): a GPU-resident scene, a ray-tracing world module, a job system, GPU-driven draws, levels of detail, scattered instances such as foliage, streaming, and large-world precision.
- **Size.** 44 items, roughly 228 to 293 working sessions (a session is about one solo working day, RT-SERIES's unit). Every item longer than a few sessions is broken into named steps, and each step ends with a report. Every milestone leaves the engine working. A new path lands behind a global setting or a measurement flag, is proven against the old path, and the old path is then deleted in a named step.
- **Target.** 60 FPS at the laptop's native 2560x1600 (the owner's choice, D8), with about 2 ms kept free because this GPU's timings drift. Today's garage at that resolution would cost about 52 ms (an estimate from the 1600x900 benchmark), so it has to get about 3.6 times faster. Render scale stays available as a fallback setting, but the plan does not count on it. The per-pass targets in section 6 are proposals only: each item's first arm measures, and the owner sets the bar from that measurement.
- **Owner decisions.** All twelve were answered on 2026-09-24; section 5 has each answer and what it changed. In short: this series includes the speed pass; OpenGL is frozen; the RT mode may replace its second scene draw with one compute pass; TAA stops being forced once every signal stands alone, and MSAA 4x becomes selectable; bounce light comes in two layers (baked lights from the bake, Realtime lights live on top); the engine's own upscaler is the default, with DLSS and FSR optional; shader caches are on; glowing surfaces become lights, judged first; the target is 60 FPS at native resolution; RT-24 may be committed with its one TAA reliance written down; the accepted sea work returns with the water rework; and the back glass panes stay a named TAA exception.

The working protocol stays as it is: one item, or one named step, per green signal; a report after each; one arm at a time for anything the owner judges live; solo coding (RT-FIRST §4, answer 6). Costs are measured and recorded as each item lands. No third-party type appears in a public engine header (the owner's API-wrapping rule): Jolt's job interface, VMA's budget types, the DLSS, FSR and opacity-micromap SDKs all stay inside private implementation files.

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

**Out-of-date facts** (each checked in the working tree):
- The GPU-driven lit path (`--gpu-lit`) is **on** by default, and its defect was fixed on 2026-08-24 (`EngineConfig.h:990`; HANDOFF.md:6549). Only its help text (`EngineConfig.h:51`) still says "unfinished, off".
- The transform walk is no longer the CPU wall. Since roadmap 8.15 it takes 2.6 ms at 60,000 objects. With rays on, the wall is the shadow phase: about 20 ms of CPU at 60,000 objects. Reading the code points at the ray-tracing instance work, but that is inferred, not profiled. The phase also holds the draw-list rebuild and cull-table upload (3.07 ms with rays off), a per-instance hash for measured change (`RayShadows.cpp:443-466`), emitter extraction, the TLAS pack and the build recording. RT2-2's CPU zones split it before RT2-35 is designed.
- The TLAS is refit in place when the instance count has not changed, and rebuilt every 64 frames. It is not rebuilt from scratch each frame. What *is* rebuilt every frame is the CPU instance list and the hit table.
- Texel emitters were built and merged on 2026-08-24. They do nothing on the garage's real content.
- The project has run TAA since 2026-08-27, by the owner's own settings. `MsaaSamples: 4` is still stored, and with rays on the engine forces TAA regardless.

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
- *Direction.* RT2-0, RT2-5, RT2-7, RT2-8, RT2-23, RT2-24, RT2-28.

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
- *Direction.* RT2-6, RT2-10, RT2-11, RT2-12, RT2-25.

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

### 2.1 The frame at the end of RT-series 2

The owner's target (D8): 60 FPS at native 2560x1600. A lower render scale with upscaling stays available as a setting, but nothing below counts on it.

**On the CPU** (threading is in 2.4): wait for the frame's GPU slot; read input; run the fixed simulation steps; update only the transforms that changed; copy what changed into the GPU scene; compile the frame graph (reused while the frame's shape stays the same); record the passes on several threads; submit.

**On the GPU**, in order:
1. **World update** (on the async compute queue, where that measures faster):
   - one skinning pass for every character;
   - BLAS builds and refits, batched under a per-frame budget;
   - the TLAS instance buffer written from the GPU scene, and the TLAS skipped, refit or rebuilt by policy;
   - the light and emitter buffers updated where they changed, and the light-candidate structure refit;
   - the realtime layer's cache updates, only while a Realtime light is near the camera.
2. **Cull and G-buffer.** GPU culling of instances (later, clusters), then one raster of all opaque geometry into the G-buffer. Its lanes: depth; motion; shading normal and roughness; geometric normal; albedo (sRGB, 8 bits); F0 and metalness; an integer lane with the stable object id, flags and shading model; occlusion; emissive. Then the first transmissive surface (glass or water) goes into its own lanes, along with the water's coverage.
3. **Classification.** For each tile, which signals have work: glossy or mirror surfaces, young history, glass, water, sky. The lists of points to process are compacted on the GPU.
4. **Signals.** Compute passes over the tile lists, each on its own grid with its own history:
   - direct light and shadows;
   - reflections (mirror tiles at full resolution, glossy tiles at half);
   - the realtime bounce layer (half resolution, only while a Realtime light is near the camera) and AO (half resolution);
   - refraction, plus the transmissive layer's direct light and reflections.
5. **Composition** (compute): emissive + direct + indirect (the baked layer plus the realtime layer) × albedo + specular (split-sum weight from the same F0 and roughness the trace used) + fog. The transmissive layer is composited on top.
6. **Transparent composite.** Particles, smoke and the deeper glass panes by WBOIT, lit from the cache. Lamp glows are drawn afterwards, depth-tested against the transmissive layers.
7. **Final temporal pass.** It anti-aliases geometry only, at native 2560x1600 (and upscales when a lower render scale is chosen). Its inputs are one generic per-pixel mask saying how much of the history is invalid, built from the signals' change maps and the layers' coverage, and one generic motion lane per see-through layer. No input is specific to a surface type. Whether reflections are added after this pass, or before it with their own confidence setting a short memory there, is measured in RT2-28.
8. **Post and UI**, at output resolution.

**Removed from the frame, compared with today:**
- the second raster of the scene;
- the per-fragment probe, field and split-sum composition inside the lit shader;
- the second glass reflection chain;
- the sea's private passes, and its rays traced inside draws with no accumulator;
- the surface-specific inputs to TAA;
- the instance and hit tables built on the CPU.

### 2.2 How each lighting signal rebuilds itself without leaning on TAA

| Signal | How it is traced | How it reconstructs itself | What the final temporal pass does for it |
|---|---|---|---|
| Direct light and shadows | K lights picked from the fixed-cost candidate structure; one ray each to a sampled point on the source; the light evaluated at that point | The light without shadows stays sharp. The ratio of shadowed to unshadowed light is averaged over time with measured change and a fast-history clamp, and filtered spatially with a radius set by the penumbra width | Nothing. Its change map feeds the generic mask |
| Reflections | Tiered by roughness; bounded visible-normal sampling; NEE from the shared candidate structure; hits lit by the one hit-shading contract, with stored light (the bake, plus the realtime layer's cache) beyond the first hit | A consistent resolve; two reprojection candidates (surface and virtual image) blended by confidence; the reflected content checked under any motion; one history-length counter; fast-history clamp; history fix; variance-guided blur; specular temporal stabilisation | Added after it, or before it with a short memory there (measured; the owner picks) |
| Bounce light (GI) | Two layers (D4). Baked lights: read from the bake, no rays. Realtime lights: one ray per pixel at half resolution, each hit lit by Realtime lights only, a small cache beyond it; the pass is skipped when no Realtime light is near | The realtime layer: the contract on the trace's own grid, a relative outlier stage, one joint bilateral upsample. The baked layer needs none | Nothing |
| AO | Short rays at half resolution | The contract on its own grid; applies to stored bounce light only (the bake and the realtime layer's cache) | Nothing |
| Transmissive layer (glass, water) | The same direct, reflection and refraction passes, run over the layer's points | The same accumulators, with histories kept only for the listed points | Its coverage feeds the generic mask |
| Water surface | A surface generator plus a medium, drawn as the transmissive layer; a displaced stand-in in the ray structure | As above; the foam keeps its own memory | As above |
| Particles, smoke | WBOIT, lit from the light library and the stored bounce light; no random term | None needed | Ordinary anti-aliasing |
| Glass panes behind the nearest | WBOIT, lit by the light library (RT2-13); their reflection rays stay in the draw, by the owner's RT-13 decision | None of their own. **Only TAA averages their reflection rays**, a named exception to "no reliance on TAA", accepted by the owner (D11) | Averages their rays (the exception) |
| Fog | Analytic height fog, applied in the composition and along reflection and refraction rays; froxels come with WR-11 | None needed | Nothing |

### 2.3 How geometry, acceleration structures and lights scale

- **Geometry.**
  - Every renderable object owns a stable slot in a GPU-resident instance table, which is updated only where something changed. Culling, raster, TLAS instances, hit shading and all ids use that slot.
  - Meshes arrive cooked, with separate position and attribute streams, 16-bit indices where they fit, clusters and LOD chains.
  - A model is one mesh with several material sections, not one entity per section. Placing it creates one entity, with sub-entities only where an author needs to move a part.
  - Scattered things (foliage, rocks, debris) are instance sets: one entity holding thousands of placements, so a million-instance forest does not hit the ECS's cap of 1,048,575 entities (RT2-39).
  - Positions are handled relative to the camera wherever precision matters (G-buffer reconstruction, ray origins, cache cells), so a world 10 km across renders as cleanly as one at the origin (RT2-41).
- **Acceleration structures.**
  - One multi-section BLAS per mesh LOD, built when the geometry arrives: batched, compacted, pooled, under a build budget.
  - The TLAS is written by a compute pass from the GPU scene, then skipped, refit or rebuilt by a measured policy.
  - The ray LOD is chosen once per frame for every view (the engine's rule that the ray world is world state, not view state).
  - Far-field stand-ins and small detail get their own mask bits. Cutouts get opacity micromaps if measurement supports them.
  - Skinned meshes are posed once per frame and rebuilt after large deformations.
  - Everything is streamed in, and evicted together with its geometry, under the VRAM budget.
- **Lights.**
  - One persistent GPU light buffer for the analytic types: directional (with an angular radius), sphere, tube, rectangle and disk, with spot cones as modifiers. One emitter table built from emissive triangles. Both are updated only where they change.
  - A GPU-built candidate structure (a light BVH or ReGIR, picked by measurement) gives every pixel and every ray hit a fixed-cost choice.
  - Each fixture carries its light through exactly one representation in each path.

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
| RHI binding (descriptor sets per pass, rewritten every frame) | Rewrite: a bindless heap plus a per-frame constant ring | At least five recorded bugs. It blocks GPU-driven and multi-threaded work |
| RHI synchronisation (implicit; each texture tracks its own layout) | Rewrite: barriers worked out by the render graph | Needed for compute passes, memory aliasing and extra queues |
| Uploads (a blocking submit for everything) | Rewrite: an upload ring on the transfer queue | Stalls; no streaming possible |
| Pipelines and shader compilation | Refactor | Caches, nothing created inside a frame, loud failures |
| Render graph | Rewrite | An ordered list that cannot express compute writes |
| FrameGraphBuilder (the 5.5k-line function) | Rewrite: tables of signals and layers, plus RT and raster recipes | Wiring by hand produces silent defects |
| Renderer3D (all static) | Rewrite the frame and pass half; keep the shader maths | There is no per-view context |
| The signal contract (accumulate, measured change, RT-9's allocator shape, guided downsample, half-float rounding) | Keep the ideas; refactor into one shader per kind of signal | Measured and principled |
| Reflection chain | Rewrite the denoiser. Keep NEE and MIS, the low-discrepancy sampling, the virtual-image idea and the demodulation | Its inputs and checks are wrong, and it costs 59% of the garage frame |
| Direct lighting | Rewrite the light model and the selection core. Keep the pass layout, the cull records, measured change and the every-light reference arm | Scaling and duplication |
| GI (bounce light) | Two layers (D4): the bake for baked lights, kept as it is, plus a live pass for Realtime lights on top. Keep the RT-3 arrangement, the shared hit shader, the NEE machinery, the bake stamps and the fill solver's parts | Four different answers today, and Realtime lights never bounce in baked scenes |
| Reflection probes | Refactor: capture through the ray-traced world, then use them only as a rough-reflection fallback | Captured without shadows today |
| Lit shader and materials | Refactor: modules, one BSDF, and a compute composition in RT mode. The forward path stays for the raster recipe | Double raster; five BSDFs |
| G-buffer | Refactor: redesigned lanes | The contract was never designed |
| TAA | Refactor into an anti-aliasing pass for geometry only (upscaling when a lower render scale is chosen) | Today it is where everything gets patched |
| Transparency (WBOIT, the glass layer) | Refactor: one transmissive layer and a ray traversal class. WBOIT stays for particles | Outside the ray-traced world today |
| Water | Refactor into a material, a surface generator and a medium; delete the private passes | A parallel renderer |
| Scene extraction and GPU cull tables | Rewrite: a GPU scene | Rebuilt every frame |
| The ray-traced world (RayShadows, BLAS/TLAS) | Rewrite: its own module | Lives inside the shadow code; builds block; nothing compacted |
| Mesh cooking | Refactor: separate streams, multi-section meshes, LOD chains, clusters | |
| Scattered instances (none today: every rock is an entity) | New: instance sets (RT2-39) | The ECS caps a world at about a million entities |
| World precision (float32 world positions everywhere) | Refactor: camera-relative reconstruction and scale-aware ray offsets (RT2-21, RT2-41) | Precision degrades kilometres from the origin |
| Frame loop and threading | Rewrite: a job system and frame pipelining | One thread |
| ECS | Keep the storage; refactor: per-component change versions, parallel ranges | Sound |
| Transform system | Refactor: flat arrays, writes that record the change | Walks every entity 3 to 7 times per frame |
| Asset loading | Rewrite into streaming. Keep the import-cache format and the virtual file system | Synchronous on first use |
| Physics glue | Refactor | Private thread pool; hard caps |
| C# bridge | Keep | Fine |
| Measurement harness | Keep and extend | It is the owner's method |
| OpenGL backend | Freeze (D1): the raster path keeps working as today, with a shader set that stops changing; new features are Vulkan-only | It cannot ray trace, and parity would hold the new design down |

## 3. Current issues and their fate

Every issue in the inventory, with one of three fates: fixed directly by an item; absorbed by a rewrite item (and why the rewrite removes it); or left parked (and why).

**RT-24, symptom by symptom.** RT2-0 finishes it inside today's architecture. Every symptom has a first arm and a fallback arm inside RT2-0, so nothing waits for a rewrite. (The step letters are RT2-0's.)

| Symptom | Removed by | Proven by |
|---|---|---|
| Ghosts behind reflected poles, the bumper, a car afterimage | Fix 2 (built), then arms A (the frame filter's rules), F (this frame's image distance), G (the curvature check), I (the struck-object check under camera motion); fallback H (a fast-history clamp) | `spin_measure.py --stage=many16`, the pole and bumper spot scores (added to the script in step 0) |
| The blur that spreads | Arm A (the frame filter's motion lane, colour box including the material widening, and moving feedback); fallback J (the filter's memory on a shiny pixel set by the reflection's own history length) | `spin_measure.py`, against the frame-filter-memory-off reference |
| Reflections settle late after a stop | Arms C (the motion test reads this frame's camera) and D (extra rays stay on for a bounded number of frames after motion) | spin_measure's after-stop rows |
| The car's shine settles late | Arms C and D, then E if the analytic highlight carries it | After-stop rows over the car; live |
| Tube reflections noisy while moving | Arms C (the extra rays really run during a swing), E (which copy of the tube carries the noise) and K (whether tube bars left out of the 16 aimed-emitter slots cause it). If K shows they do, the cure is RT2-11, and the owner decides whether RT-24 waits for it or is committed with that symptom recorded | spin_measure's grain score over the tube patch, without many16; live |
| Fix 1 hands a young picture's grain to TAA | Arm H (a young-pixel fill in today's accumulator) aims to remove the dependency inside RT-24. If it cannot, RT-24 is committed with the reliance written down (D9), and RT2-23 and RT2-28 remove it | The grain score with the frame filter's memory off |

**All issues:**

| Issue | What it is | Fate | Item | Why |
|---|---|---|---|---|
| RT-24 | Fast swing: ghosts, a spreading blur, late settle, noisy tubes | Fixed directly | RT2-0 (structure: RT2-23, RT2-28) | See the table above |
| RT-22 | A moving light's lighting trails behind it | Split | RT2-23 (the reflection third), RT2-24 (the direct history), RT2-31 (the glass pane's lamp light gets measured change) | Each remaining part lives in a different history; `emitter_lag.py` gates each one |
| RT-2.2 | No deferred resolve; opaque surfaces rastered and materials sampled twice | Fixed directly | RT2-22, step 1 | Built exactly as filed, with the albedo lane moved to sRGB8 first. Then, per D2, one compute composition replaces the second scene draw in RT mode |
| ISS-1 | The bridge water glitter blinks more since RT-22's rule (0.80% to 0.97%) | Fixed directly | RT2-8 | The behaviour-based floor replaces the rule keyed on "covered" pixels, so the sea gets its floor back with no water exception. It lands in M1, right after RT-24, not at the end of the series |
| ISS-2 | The general "how a pixel changes" rule for TAA's floor is not built | Fixed directly | RT2-8 | Built as the owner's parked proposal; it needs nothing from the later rewrites |
| ISS-3 | A reflection keeps moving after its object stops | Absorbed | RT2-23 (RT2-0 fixes the one-frame motion tail) | The new denoiser checks what the ray struck whenever anything moves, instead of closing its gates two frames after a stop |
| ISS-4 | Bridge cables, tower and thin deck members blink under TAA | Stays parked | (direction: RT2-38) | Not a ray-tracing defect: 0.00-0.01% with no AA or MSAA. The owner's look rules forbid fading the key members. RT2-29 must be checked on the cable band |
| ISS-5 | Lights at ray hits are points, even tubes | Fixed directly | RT2-10 | The one light library shades tubes and spheres as areas at every hit |
| ISS-6 | The GPU-driven lit path is "unfinished" in the docs but on in the code | Fixed directly | RT2-1 (text and a parity check with rays on), RT2-37 (replaced by compacted draws) | |
| ISS-7 | Two half-float histories still round toward zero | Fixed directly | RT2-6, arm 8 | Recorded as the owner's call; shown as an arm |
| ISS-8 | `kSettledBound` reads the wrong counter | Fixed directly | RT2-6 (arm 12) | One history counter. It changes pixels, so it is an owner-judged arm, not a silent patch |
| ISS-9 | The baked probe is a third brighter than the traced reflection | Fixed directly, then absorbed | RT2-6 (probes get shadows; the reference renderer settles which is right), RT2-27 (rough reflections from stored light, not the probe) | |
| ISS-10 | Switching a baked light throws the bake away | Absorbed | RT2-25 | Switchable lights are Realtime (the owner's rule), and with RT2-25's realtime layer they now bounce light too. Switching a baked light still means a re-bake, by design (D4: baked lights stay baked) |
| ISS-11 | The sea work the owner accepted on 2026-09-20 ("looks perfect to me, make it default") is on a parked branch; main's sea mirror has no accumulator | Absorbed | RT2-1 (its validation fixes), RT2-31 and RT2-32 | The owner chose to bring it back with the water rework in M5 (D10). Costs recorded then at 1440p: the pier +1.7 ms, Glitter +1.2 ms, Headland 1.3 ms faster. It was never checked with a moving camera, so RT2-32 checks that before it becomes the default |
| ISS-12 | Random draws move only with TAA's jitter; rays force TAA | Fixed directly | RT2-7; the force is lifted at the end of M5 (D3, RT2-33) | |
| ISS-13 | Texel emitters: inert on real content, unexplained cost | Absorbed | RT2-11 | The emitter table is rebuilt from emissive triangles, GPU-resident, with alias-table sampling |
| ISS-14 | Cutout limits (solid for AO, material alpha in shadows, the texture read during traversal) | Stays accepted; one part measured | RT2-38 | The limits are deliberate trades. Opacity micromaps are measured against the traversal-cost cliff |
| ISS-15 | Leftover ray-budget code and legacy sea paths; nine unverified review findings | Fixed directly | RT2-3, RT2-32 | Deleting the code makes the findings moot. The reuse verdict is not reopened |
| ISS-16 | The showroom slowdown, only partly recovered | Absorbed | RT2-1 (counters), RT2-13 and RT2-22 (register pressure), RT2-26 (field lookup per fragment) | The showroom became the garage, so its numbers are out of date. Section 6's budget replaces them |
| ISS-17 | The opaque pass is bound by per-pixel work, not the BRDF | Absorbed | RT2-22, RT2-29 | One composition with no second material evaluation, and fewer pixels. The BRDF finding stands |
| ISS-18 | No many-light direct lighting since RT-10 was dropped | Fixed directly | RT2-12 | A fixed-cost candidate structure. RT-10's two missing mechanisms become a later arm on top of it |
| ISS-19 | At most 16 glowing surfaces get aimed rays | Fixed directly | RT2-11 | |
| ISS-20 | Reflections on moving objects read memory from the wrong place | Absorbed | RT2-23 | Checking the struck object under any motion, plus a history-length counter |
| ISS-21 | Clean-up passes spread bright outliers; visible-normal sampling ships off | Absorbed | RT2-6 (clamp scaled by lobe width), RT2-23 (outliers bounded before any spatial pass; visible-normal sampling re-measured with honest densities) | |
| ISS-22 | Reflections pass through two frame filters in a row | Fixed directly | RT2-0 (bisection), RT2-28 | |
| ISS-23 | See-through surfaces are second class for ray tracing | Absorbed | RT2-31 (panes behind the nearest lit by the library in RT2-13) | The panes behind the nearest stay on the old path by the owner's RT-13 decision, and only TAA averages their reflection rays; the owner accepted that as a named exception (D11) |
| ISS-24 | A failed shader variant quietly swaps paths | Fixed directly | RT2-1 | |
| ISS-25 | Two validation errors on the bridge, fixed only on a parked branch | Fixed directly | RT2-1 | |
| ISS-26 | Per-frame scene walks grow with object count | Absorbed | RT2-34, RT2-43 | Tracked writes and change lists replace the walks |
| ISS-27 | No mesh levels of detail | Fixed directly | RT2-38 | |
| ISS-28 | Area lights: no LTC, no luminaire binding, the capsule highlight too bright | Fixed directly | RT2-10 (areas, rectangles, LTC as a measured option, tube energy), RT2-11 (a light owns its lens) | |
| ISS-29 | Night-realism items not built (WR-5 remainder, WR-6, WR-11, WR-12, WR-14) | Stays deferred | (prerequisites: RT2-33) | They stay in the WR series. RT2-33 does only fog in the composition and fog at ray hits |
| ISS-30 | Moving chrome cube: stripes, banding, speckle, softer look | Absorbed | RT2-23 (with RT2-21's curvature and the RT2-2 reference) | Measured against a truth render for the first time |
| ISS-31 | Tube rims report motion while parked | Fixed directly | RT2-6 (arm 14) | It changes pixels, so it is an owner-judged arm |
| ISS-32 | The sea's lighting gaps, and unconfirmed owner reports | Absorbed | RT2-32 | Lighting per crossing through the layer. The owner is asked for the camera before the blur is chased |
| ISS-33 | Per-pixel memory grows with resolution | Fixed directly | RT2-19, RT2-29 | |
| ISS-34 | The optimisation list lives only on the parked branch | Absorbed | This roadmap | Glass rays go to RT2-31; the probe fetch at every hit to RT2-25 and RT2-27; the sea mirror's accumulate to RT2-32 (D10); RT-10's +8 ms is dropped |
| ISS-35 | No pipeline or shader cache | Fixed directly | RT2-9 (D6: approved) | |
| ISS-36 | Raster fallback defects | Partly fixed | RT2-1 (re-check the device loss with rays on and reflections off), RT2-4 (name the demoted light) | The SSR smear and the voxel GI noise stay parked: the raster fallbacks are kept at parity, not improved, in this series |
| ISS-37 | Baked-lighting limits the traced picture now depends on | Absorbed | RT2-26 (including contact-scale bleeding and cells that move out of walls, both in its exit gate), RT2-40 (BC6H in the texture cooker) | Baked lights stay baked (D4), so reflections of Static surfaces keep taking baked lamps' light from the field, by design. Nested volumes blend; the light that bleeds at contact scale (about 1.9% of pixels) is measured against the reference renderer and must fall; cells inside walls move out, which fixes the leak case the author rule could not; a stale bake is flagged in the editor. The bake on OpenGL stays unsupported (OpenGL is frozen, D1). Lightmaps are not planned (meshes carry one UV set) |
| ISS-38 | Terrain: no shadow offset for LOD error, no triplanar mapping, macro breakup not exposed | Partly | RT2-21 (terrain layers at hits), RT2-38 (shadow offset) | Triplanar mapping and the macro inspector rows stay parked as materials work outside this series |
| ISS-39 | The visual-script loader drops unknown nodes and saves the loss | Stays outside | none | Not renderer work. It loses user data, so a small separate task is recommended now |
| ISS-40 | RT-9 earns nothing measurable | Fixed directly | RT2-0 (the motion test), RT2-23 (rays traded within a fixed budget), RT2-12 and RT2-24 (the direct half re-measured) | Part of the "nothing measurable" was probably a pass that never ran during camera motion. That is read from the code; RT2-0's pass-timing check confirms it before anything changes |
| ISS-41 | One 6,500-line lit shader and layouts mirrored by hand | Absorbed | RT2-1 (checks), RT2-13 and RT2-22 (dead arrays out), RT2-17 and RT2-21 (modules, one layout definition) | |
| ISS-42 | Measurement gaps that confound arms | Fixed directly | RT2-2 | |

## 4. The items, in order

Each item lands behind a flag or a reference arm, is measured, and is reported before the next one starts. Anything the owner judges live is shown one arm at a time. Costs are recorded as each item lands, even where the optimisation belongs to a later item.

---

### RT2-0 · Finish RT-24 (the owner's top priority, in progress)

- **What is wrong now.**
  - A fast camera swing in the garage still leaves a blur that spreads over the wet floor; fix 2 turned most of the ghosts into this.
  - Reflections, and the car's shine, settle a fraction of a second after the camera stops.
  - The ceiling tubes' reflections are very noisy while moving.
  - Fixes 1 and 2 are built and uncommitted.
  - Fix 1 hands a young picture's grain to the frame filter; its own comment says so (`pbr_fragment.glsl:6193-6195`). That is a reliance on TAA, which the owner's rule does not allow without the owner's say-so.
- **What changes.** One arm at a time, each measured on the clean swing test and then shown live, with a report before anything is changed. The usual order is 0, A, C, D, E; the fallback arms F to K run as the results point to them.
  - **0. Instruments first (no picture change).**
    - `spin_measure.py` gains the scores it lacks. Today it prints only the mean difference, the share of pixels off by more than 16 levels, and an 8x8-block smear. It gains spot scores for the pole (x 1400-1560, y 560-760), the bumper (660-940, 560-700) and a tube patch, and a **grain score**: over each spot, the per-pixel spread across the swing's centre-crossing frames (they all sit on the settled pose), averaged over the spot. Lower is better for all of them. Today's numbers are recorded with the reflection memory on and off for every spot (only the pole's memory-off 7.8 is written down now).
    - The live movers become smooth. The `Slider` script, which drives the car and the moving light in `watch_arm.py`, moves in `OnFrame` instead of the fixed 60 Hz `OnTick` (`Slider.cpp:22`), so the owner stops seeing a mover that halts every other frame at 240 Hz. `Orbiter.cpp` follows for consistency (it runs only under `spin_measure.py`'s pinned clock, where the two are identical).
    - `watch_arm.py` refuses to show an arm whose log reports a shader or pipeline failure.
  - **A** (the owner's named next step). Test `taa_resolve` on the clean 16-ray test, one rule at a time:
    1. The reflection motion lane it uses to line up old frames (RT-6.1).
    2. Its colour box. This includes the material widening, which opens the box 1.5x on the floor (roughness 0.21) and up to 5.2x at roughness 0.35. The widening assumes rough dielectrics look the same from every angle, which is false inside the traced gloss window (REFL-10).
    3. The moving feedback.

    The two feedback values (0.9 moving, 0.98 still) are the owner's own project settings (`SampleProject.rvproject`). Any change to them goes to the owner as a choice, and a rule that decides per pixel (arm J) is preferred over editing a global setting.
  - **C.** RT-9's "has anything moved" test reads this frame's camera. Today it compares last frame's camera with last frame's camera (`FrameGraphBuilder.cpp:2401-2403` against `Renderer3D.cpp:3900` and `7584`). So in the runtime a camera-only swing never counts as motion and the extra-ray allocation never runs during a spin, while in the two-panel editor it runs on every parked frame. Probe captures also pass through `BeginScene` and overwrite the stored eye for every face, so a scene with a realtime probe (the camp) can read "moved" while parked. First confirm all of this with `--pass-timings`: ReflectionBudget should be missing during a runtime spin. Then key the test on the view's own camera from FrameDesc, compared with the signal's own record. `AnyInstanceMoved`'s "last frame" flag, which is shifted once per view, gets the same fix.
  - **D.** Keep the extra rays on after a stop, for a bounded number of frames since the last motion (the settle length). Not "while tiles hold young pixels": thin edges always read young, and RT-9's still gate was built to stop exactly that half a millisecond of parked cost (2026-09-16). This is the lead HANDOFF names for the late settle.
  - **E.** Split the tube noise between the tube's two copies: one arm with DirectTrace's specular half zeroed. The tube's reflection exists twice. One copy is the analytic highlight, re-sampled every frame and averaged over four frames by surface reprojection. The other is the traced image of the glowing bar. No RT-24 arm has isolated the first yet.
  - **F.** This frame's image distance, in bounded form. The depth used to find last frame's picture comes from this frame's filtered hit distance, with sky misses flagged instead of averaged in as 10 km (`reflection_accumulate.rvshader:1502`, `reflection_trace.rvshader:826`). It helped before (pole 17.1 to 13.6); re-measure after arm A's winner. It replaces the image-distance smoothing that reads the wrong counter.
  - **G.** The curvature check: `--debug-view=reflection-image` on the floor under the tubes. If the image distance collapses toward zero, the normal map is being read as mirror curvature (REFL-04). In that case, take curvature from the depth buffer for this formula until RT2-21 adds a geometric-normal lane.
  - **H** (fallback for the ghosts, and the attempt to remove fix 1's TAA reliance). Two additions to today's accumulator. A **fast-history clamp**: a second, short history of a few frames bounds the long one, so an old picture cannot outlive the scene (the reference survey's most direct answer to a ghost that turns into a spreading blur). A **young-pixel fill**: a pixel whose history has just restarted gets a spatial fill sized by hit distance and roughness, fading out as its history builds. If the fill settles young grain with the frame filter's memory off, fix 1 no longer leans on TAA. If it cannot, RT-24 is committed with the reliance written down (the owner's answer to D9), and RT2-23 and RT2-28 remove it later.
  - **I.** The struck-object check under camera motion. Today the test that the reflection still shows the same object is off unless an object moves (REFL-05), so a camera-only swing checks almost nothing. Switch it on whenever the camera moves too. A generic rule with no surface key.
  - **J** (fallback for the spread). The frame filter's memory on a pixel set by the reflection accumulator's own history length: short where the reflection is young, normal where it has settled. A generic rule with no surface key, preferred over editing the global feedback settings.
  - **K** (diagnostic for the tube noise). Log which glowing surfaces get the 16 aimed-emitter slots (`kMaxAreaEmitters`, `Renderer3D.h:76`), then run the tube patch with the cap raised. This tells whether tube bars left out of the list cause the noise, before anything is rewritten. The real cure, if so, is RT2-11.
- **Owner sees.** The floor stays sharp through a swing. Reflections and the car's shine settle within a few frames of a stop. Tube reflections are calmer while moving. Live movers glide instead of stepping.
- **Touches.** `taa_resolve.rvshader`, `reflection_accumulate.rvshader`, `reflection_trace.rvshader`, `reflection_budget.rvshader`, `pbr_fragment.glsl`, `FrameGraphBuilder.cpp` (2396-2405), `Renderer3D.cpp` (`CameraStill`, `AnyInstanceMoved`), `Renderer3D.h` (the emitter cap, diagnostic arm only), `SampleProject/Source/Slider.cpp`, `SampleProject/Source/Orbiter.cpp`, `tools/scripts/garage/watch_arm.py`, `spin_measure.py`.
- **Scope.** Patch. **Depends on.** Nothing. Nothing else in this roadmap starts until RT2-0 is committed, so its measurements are never mixed with other changes. **Risk.** Medium: TAA rules touch every pixel of every scene, so every change is re-checked on the bridge.
- **Exit gate.**
  1. `spin_measure.py --swing=15 --rate=2 --stage=many16`: the pole and bumper spot scores, moving against settled, at or below their reflection-memory-off numbers (pole 7.8; the bumper's is recorded in arm 0) with the memory on. No other region gets worse.
  2. Without many16, the grain score over the tube patch lower than arm 0's number.
  3. spin_measure's after-stop rows: the frame five after the stop within the parked noise floor (`parked_stats.py`).
  4. Parked garage frames: diff 0 against today, or within the parked noise floor where a TAA rule changed.
  5. The four-way check every TAA change needs: the window drive (`watch_arm.py light 8`) and the car's stop (`watch_arm.py car 20`) by the owner's eye; the bridge's `check_glint_flicker.py` no worse than 0.97%; the cable band unchanged.
  6. `--pass-timings` shows ReflectionBudget running during a runtime spin and absent on parked runtime frames, in the garage and in the camp.
  7. Cost: A,B,B,A timings on the swing and on parked frames, in the garage and at Headland, before and after. Arms C and D spend rays on purpose; the cost goes to the owner with the picture.
  8. With the frame filter's memory off, young reflection pixels settle within the parked noise floor (fix 1 no longer leans on TAA), or the remaining reliance is written down (D9).
  9. The owner, live on `watch_arm.py spin 0` with no time limit: fixed. Then commit everything (`Orbiter.cpp`, `Slider.cpp`, `spin_measure.py`, `ghost_map.py`, `watch_arm.py`, the rebuilt module, the docs) and delete the captures.
- **Size.** 5-8 sessions.
- **Closes.** RT-24, framegraph-s1, ISS-40 (the motion test), CORE-08 (the test-mover half, through `Slider`). Also the measured arms of REFL-01, REFL-03, REFL-04, REFL-05 (the camera-motion check), REFL-07, REFL-10 and direct-s2, and the diagnostic half of REFL-06; their structural parts are later items.
- **If a symptom survives every arm.** The owner decides: commit RT-24 with that symptom recorded against the item that removes it (RT2-11 for tube bars left out of the list, RT2-23 for the structural ghost fix), or hold RT-24 open.

### RT2-1 · Fail loud, validate clean, and count VRAM

- **What is wrong now.**
  - A shader variant that fails to compile makes its pass return early, or makes a different variant run. A pipeline that fails to build leaves a null handle in Release.
  - Only screenshot and benchmark runs exit with code 3. The editor and the owner's live arms are not covered, and one failure once escaped the log entirely.
  - The bridge has two Vulkan validation errors: a 112-byte push into a 96-byte block (WaterAccumulateLamps), and integer images sampled through a linear-mip sampler (DirectWaterShade). Both fixes exist only on `wip/2026-09-21-rt10-and-sea`.
  - The screenshot path copies from a swapchain image created without the usage flag that allows it. Acceleration-structure scratch memory ignores the device's alignment rule.
  - Push-constant and buffer sizes are never checked against the sizes the shader compiler already reflects.
  - The ray counters, a measurement instrument, are compiled into every ray-traced frame.
  - The engine knows only the card's total memory (`VulkanDevice.cpp:707-723`), yet several later exit gates ask for VRAM before and after.
- **What changes.**
  - A missing shader or pipeline is loud in every run mode. `CreatePipeline` returns null on failure, the pass paints a fixed debug colour, the HUD names the variant, and pipeline failures count toward exit code 3. First, find out how the RT-11 failure escaped the log.
  - Re-land the two bridge validation fixes. Add TRANSFER_SRC to the swapchain usage where the surface supports it. Honour the scratch alignment.
  - In debug builds the RHI checks push-constant sizes, bound buffer ranges, sampler and format pairs, and unwritten bindings against the reflected shader.
  - The benchmark scripts first run a short `--validation=on` pass per scene, and refuse to report numbers while any Vulkan message remains.
  - The counters compile only under `--ray-counters=on`, and are taken in a separate run so that benchmarks time the shader that ships. Their cost is re-measured first: the recorded 0.26 ms and 0.85 ms predate RT-1 and are stale.
  - **VRAM is measured** (the measuring half of the memory budget; degrading under a budget stays in RT2-19). The engine asks the driver for its budget (`VK_EXT_memory_budget`, through VMA's budget query) and reports VRAM by category (targets, histories, textures, geometry, BLAS and TLAS, uploads) in the HUD and in every benchmark report.
  - Fix stale text: the `--gpu-lit` help, RTGI High's resolution, the traced AO resolutions, HANDOFF's shader-cache claim. Delete `FrameProfiler::LiveRayGpuMs`, which nothing calls, and its comment.
  - Run `check_gpu_lit.py` with rays on. Re-check the recorded device loss with rays on and reflections off.
- **Owner sees.** Nothing, until something breaks; then a coloured pass and a banner instead of a believable wrong picture. A VRAM figure in the HUD. With the counters off, some time back per frame (re-measured).
- **Touches.** `VulkanDevice.cpp` (and the budget query), `VulkanPipeline.cpp`, `VulkanCommandList.cpp` (`PushConstants`), `VulkanResources.cpp` (scratch), `Renderer3D.cpp` (pass guards; `PointSampler` at 1643-1649), `water_accumulate.rvshader` (`LampParams`), `ShaderCompiler.cpp`, `Entrypoint.h`, `pbr_fragment.glsl` and `PostProcess.cpp` (the counter define), `RayCounters.cpp`, `EngineConfig`, `FrameProfiler.cpp`, `RenderSettings.h`, `PostSettings.h`, `tools/scripts/bench_night.py`, `bench_scale.py`, `watch_arm.py`.
- **Scope.** Patch. **Depends on.** RT2-0 committed. It changes timings (the counters) and the bridge's picture (the validation fixes), so it must not mix into RT-24's uncommitted measurements. **Risk.** Low.
- **Exit gate.**
  - A deliberately broken variant (a staged compile error) shows the banner in the editor and makes the benchmark exit 3, naming the variant.
  - `--validation=on` runs of the garage, Headland, Glitter and the camp print no Vulkan message.
  - With the counters off, every scene is pixel-identical to counters-on apart from the HUD (diff 0), and an A,B,B,A run shows the time saved.
  - VRAM by category reported for the four shots, with the total agreeing with the driver's budget query.
  - `--screenshot` output is byte-identical to before.
  - `check_gpu_lit.py` is green with rays on.
- **Size.** 2-3 sessions.
- **Closes.** rhi-s1, GPU-13, GPU-15, rhi-s2 (the checking half), ISS-24, ISS-25, ISS-36 (the device-loss re-check); the measuring halves of GPU-09 and CORE-05; parts of ISS-6, GI-13, CORE-10 and frame-13.

### RT2-2 · Instruments that can settle questions

- **What is wrong now.**
  - Questions of bias (the probe against the trace, the floor's darkening, whether the clamps eat light) are judged against references built from the shipped chain. The 16-ray truth keeps every filter it is used to judge.
  - `bench_scale.py` runs with rays off, and its draw counter no longer parses the log.
  - The only scale scene is four primitive meshes, all Static and untextured, with no hierarchy, no emissive surfaces and nothing moving.
  - There is no many-light ray-traced scene: the bridge's lamps are baked.
  - The GI test scenes show less than one display level of GI (their skies are black). The measured worth of the traced bounce predates the fix that made its aimed rays work.
  - The profiler cannot see simulation, extraction or uploads.
  - Several switches have side effects. `--reflection-history=off` also switches TAA to surface motion and turns the noise blur off; `--rt-reflections=off` brings SSR back. There is no moving-camera harness for the bridge.
  - Around 30 later exit gates ask for "diff 0" over the garage, the three bridge cameras and the camp, over parked frames, a swing and every AA mode, and no harness does that in one command.
  - Back-to-back benchmark runs drift by up to a millisecond as the laptop warms, and there is no frame cap to hold the load steady.
- **What changes.** Seven named steps, each ending in its own report.
  - **2a. The baseline harness and a frame cap.** One command renders the standard camera set (garage, Headland, Deck, Glitter, the camp) parked, over a 60-frame swing, and in every AA mode, and diffs each frame against a stored baseline, printing a per-pixel diff image and a count per scene. Every "diff 0" gate after this uses it. An optional frame cap for measurement runs (`--frame-cap`) is added and its effect on the drift is measured.
  - **2b. A reference mode (`--reference-render`).** Progressive accumulation at a fixed pose, unclamped and unfiltered: no resolve, no colour box, no firefly clamp, no TAA. Thousands of samples per pixel, every light with many area samples, multi-bounce paths. It evaluates the **full** material at every path vertex: normal maps, the full BSDF (coat, sheen and anisotropy included), terrain layers, and textures at a mip chosen by the ray's footprint. It must not share the shipped hit shading's shortcuts (no normal maps, mip 0, Lambert-only bounces, terrain layer 0 only; MAT-02), or it would bless the simplifications it is meant to judge, which is REFL-14's flaw in the 16-ray truth. It gains transmission, refraction and a water medium before RT2-31 and RT2-32 are gated against it.
  - **2c. The scale benchmark.** `bench_scale.py` gains ray-traced arms at the project's settings, still and moving pairs, TLAS and BLAS timings, CPU phases and A,B,B,A order, and its parser is fixed. The 60,000-object figure (40.76 ms, one run) is re-measured as a palindrome first; that becomes the baseline. `make_scale_scenes.py` gains scenes that look like real content, at 20k, 60k and 120k: imported hierarchical models (a car of 50 to 150 parts), hundreds of distinct textured meshes, emissive fittings, a share of moving objects, and an alpha-tested foliage scene for RT2-38 and RT2-39.
  - **2d. A many-light scene**, starting from `lights256.rage`: 1k to 10k realtime lights of mixed sizes and ranges including tubes, moving lights, emissive meshes, rooms whose light ranges cross walls, a scripted camera path, and an every-light (K=0) arm.
  - **2e. GI scenes where bounce light carries energy**: a bright-sky enclosed room and a daylit version of the garage. Then GI on against GI off is re-measured by diff image, with realtime GI forced in the garage and the camp.
  - **2f. CPU timing zones** (nestable, aware of threads, near-free when off), with phases for simulation, scripts, physics, animation, transforms, extraction, TLAS instances and uploads. The shadow phase is split into its parts: the draw-list rebuild, the cull-table upload, the measured-change hash, emitter extraction, the TLAS pack and the build recording. Report counters: blocking submits and their milliseconds, bytes uploaded, asset-cache misses, allocations per frame.
  - **2g. Cleaner switches.** The confounded switches are split into single-purpose ones, a "no reflections" arm that does not bring SSR back is added, and spin_measure's method is applied at the bridge cameras.
- **Owner sees.** Nothing in the frame. Numbers that can finally answer "which one is right".
- **Touches.** New reference passes; `FrameGraphBuilder.cpp`; `EngineConfig`; `FrameProfiler.cpp`; `Application.cpp` (the frame cap); `tools/scripts/bench_scale.py`, `make_scale_scenes.py`, a new baseline harness, new scene scripts, `garage/spin_measure.py`, `watch_arm.py`.
- **Scope.** Refactor. **Depends on.** RT2-1. **Risk.** Medium: a reference renderer has to be shown to be right before it judges anything.
- **Exit gate.**
  - 2a: the harness reproduces its own baseline (diff 0) on an unchanged build, and flags a deliberately changed pixel.
  - 2b: a white-furnace test (a closed white room under uniform light converges to uniform within 1%). On the parked garage, the reference and the 16-ray truth agree region by region where both are unbiased (the matte walls and ceiling): a diff image with no structure beyond grain, and the per-region mean difference recorded. Where they disagree (the glossy floor, the chrome), that difference is what the reference exists to show, and it is reported, not tuned away.
  - 2c: the palindrome baseline at 60,000 objects recorded with its spread.
  - 2d: the many-light scene runs with its every-light arm.
  - 2e: GI on against off differs by at least two display levels over the lit region of each new GI scene (the number recorded); the garage and camp GI worth re-recorded.
  - 2f: the shadow phase's parts add up to its total within the spread.
- **Size.** 8-11 sessions.
- **Closes.** REFL-14, GEO-03, core-s4, direct-14, gi-s4, CORE-10, ISS-42; the frame-cap part of CORE-11.

### RT2-3 · Delete the arms already decided dead

- **What is wrong now.** Old paths stay compiled and wired to switches after their verdict:
  - the sea's retired choose-and-shade pair;
  - the water contract paths RT-8 called "dead weight";
  - the lamp reuse S4b, measured as losing, with its history still allocated every frame in water scenes;
  - the reflection follow-hit arm, measured worse;
  - the traced sky visibility, which leans on TAA;
  - a dead `GpuCull::HasObjects`, and stale comments.

  Every change must keep all of them compiling, and the water code alone carries nine unverified review findings.
- **What changes.** The owner confirms each deletion from a list that gives its verdict and record. Confirmed arms are deleted with their shaders, settings and histories. Reference arms still in use (`--direct-signal=off`, `--gi-signal=off`, `--ao-signal=off`, the opaque moving layer) stay until the items that replace them.
- **Owner sees.** Nothing at default settings. Fewer switches, and fewer variants compiled at launch.
- **Touches.** `Renderer3D.cpp` (the water variants at 1737-1757), `FrameGraphBuilder.cpp` (3142-3350, 3157), `EngineConfig`, the water_choose and water_shade shaders, `pbr_fragment.glsl` (the sky visibility at 1437-1463), `RenderSettings.h`, `GpuCull.h`, comments in `Scene.cpp`.
- **Scope.** Deletion. **Depends on.** RT2-1, and RT2-2a (the baseline harness). **Risk.** Low.
- **Exit gate.** Default-settings frames pixel-identical before and after on the garage, the bridge's three cameras and the camp (diff 0, through the harness); scenetest green on both backends; the launch compile count lower (in the log).
- **Size.** 1 session.
- **Closes.** GI-15, ISS-15; parts of MAT-10, direct-15, REFL-12, GEO-18 and frame-13.

### RT2-4 · Patches that should not change the picture, and small wins that need no rewrite

Only fixes whose picture should not change are here. Any fix that turns out to move pixels in the harness moves to RT2-6 and is shown to the owner as its own arm. The five the critic identified as picture-changing (the tile boxes, the particle velocity and weights, the history counter, the tube rims, and the world-grid lookup) are already in RT2-6.

- **What is wrong now.** Ten defects and four missed wins, each found in a review:
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
  - **Missed wins:** the TLAS is rebuilt or refit every frame even when its content key says nothing changed (the key already exists, `RayShadows.cpp:443-466`); the TLAS pack loop copies a shared pointer per instance (`VulkanResources.cpp:1237`); and asset change detection re-reads and re-hashes every source file at every launch and every editor refresh, on the main thread (CORE-09).
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
  - skip the TLAS build when the content key is unchanged; use a plain cast in the pack loop;
  - detect asset changes by file size and modification time kept in the `.meta` sidecar, and hash only when those move, on worker threads.
- **Owner sees.** Nothing on screen. 92 MB of VRAM back on the bridge. Prefabs work in packaged builds. Faster launches and editor saves. Some CPU back on parked frames (measured).
- **Touches.** `LightGlow.cpp`, `GpuCull.cpp`, `GpuParticles.cpp`, `Components.h`, `Scene.cpp`, `AssetManager.cpp`, `AssetRegistry.cpp`, `FrameGraphBuilder.cpp`, `IrradianceVolume.cpp`, `ShadowMap.cpp`, `Application.cpp`, `RayShadows.cpp`, `VulkanResources.cpp`.
- **Scope.** Patch. **Depends on.** RT2-1 and RT2-2a. **Risk.** Low.
- **Exit gate.**
  - Default frames diff 0 through the harness on the garage, the bridge's three cameras and the camp. Anything that moves a pixel leaves this item for RT2-6.
  - A scenetest claim that an entity spawned in `OnTick` has `PreviousWorld` equal to `World` on its first frame.
  - A packaged build spawns a prefab.
  - A teleport under MSAA with the AO signal on drops every history: the frame after the cut equals a cold start.
  - Bridge VRAM read before and after (RT2-1's report).
  - CPU zones (RT2-2f): the shadow phase on parked frames, before and after the TLAS skip; launch time and single-file save time before and after the change detection.
- **Size.** 3 sessions.
- **Closes.** direct-17, GEO-12, core-s1, core-s2, CORE-14, framegraph-s2, framegraph-s3, gi-s5, CORE-09 (the change-detection half); parts of MAT-16, CORE-07, GI-12, GEO-04 (the patches available today), CORE-11 (the minimised loop) and ISS-36.

### RT2-5 · Stable object identity in exact lanes

- **What is wrong now.**
  - The G-buffer's object id is exact, but the reflection, glass and direct histories keep it in half-float lanes, which hold whole numbers exactly only up to 2,048 (`FrameGraphBuilder.cpp:1012-1017`, `1802`, `2788-2793`). Above 2,048 a half float holds only every second whole number, above 4,096 every fourth, and so on, so most ids never match, and the accumulator refuses the history outright (`reflection_accumulate.rvshader:1126`). The arithmetic predicts about 11.7% of ids staying exact at 60,000 objects, which fits the measured 10.1% of glossy pixels keeping their history.
  - For everything drawn from the CPU list, the id is this frame's position in the submission list after culling (`Renderer3D.cpp:1413-1417`, `9639-9826`). That covers terrain chunks, skinned meshes, water, and every mesh in the editor's second view or under `--gpu-lit=off`. An object entering or leaving the view renumbers every later one, and TAA and the accumulators throw their histories away.
  - RT-17's struck identity folds the entity index into 1..1021 and drops the version (`Renderer3D.cpp:5657`).
  - The reflector's plane offset is stored in world units in a half float. Its rounding error reaches 0.25 m for surfaces 256 to 512 m from the world origin (`reflection_accumulate.rvshader:1955`).
- **What changes.**
  - One stable per-entity id, written at all five places where instance data is filled: the entity index plus its version, folded to 24 bits. It becomes the GPU-scene slot in RT2-34. Terrain chunks are keyed by their terrain and chunk index.
  - The G-buffer, the TAA guide and every history keep the id exactly, in a 32-bit integer lane.
  - The struck identity widens to 32 bits, and the id refusal gets its own counter lane.
  - The plane offset is stored relative to the camera.
- **Owner sees.** The garage and the camp unchanged when parked. On bridge pans, terrain stops losing its history for a frame when chunks cross the edge of the view. At scale, reflections accumulate again.
- **Touches.** `Renderer3D.cpp` (the fill sites, 5650-5658), `FrameGraphBuilder.cpp` (lane formats), `reflection_accumulate.rvshader`, `taa_guide.rvshader`, `taa_resolve.rvshader`, the direct and glass accumulate variants, `pbr_fragment.glsl` (4506).
- **Scope.** Patch. **Depends on.** RT2-2. **Risk.** Low; the history lanes grow a little, which RT2-19 accounts for.
- **Exit gate.**
  - Parked frames on the garage, the bridge's three cameras and the camp: diff 0.
  - A Headland pan: the object-id refusal share, now counted, falls, and a diff of the pan shows no new structure.
  - The 60,000-object scale scene with rays on keeps reflection history on more than 95% of glossy pixels (10.1% today); the realistic-content scenes likewise.
  - A,B,B,A cost.
- **Size.** 2 sessions.
- **Closes.** geometry-s1, geometry-s2, materials-s3, REFL-02, GEO-17, REFL-17; the integer-id part of MAT-12.

### RT2-6 · Correctness patches that do change the picture, one at a time

- **What is wrong now, and what changes.** Fourteen arms, one at a time. Each is shown to the owner as its own diff image, with the mean brightness and the bridge's red-to-blue ratio checked before anything is shown. Any fix from RT2-4 that turned out to move pixels joins this list the same way.
  1. **Probes captured after the ray structure is built.** Today every shadow ray in all six faces answers "lit". Probes are captured before `RenderShadows` (`RuntimeLayer.cpp:493`, `EditorLayer.cpp:562`), and `RayShadows::BeginFrame` has already cleared the structure's Active flag. The garage probe is baked, so this needs a re-bake, committed with the work. Afterwards the reference renderer settles ISS-9: which of the probe and the trace is right.
  2. **A field with no loaded or solved answer owns no light** (it publishes a "field valid" flag). Today a baked-lamp switch, or an edited baked light, zeroes the field while DirectTrace and ray hits still skip those lamps' far share. So on the bridge, every hybrid lamp's light beyond 2 to 3 m of its head disappears until the next bake (inferred from the code).
  3. **Lamp glows, sorted and additive particles, and world-space UI stop raising the traced reflection's weight**, which the lit shader keeps in the colour's alpha channel. The glow writes alpha 1 with additive blending, and the composite multiplies the reflection by that alpha (inferred: squares of reflected light round lamp glows on glossy surfaces).
  4. **Glows and non-weighted particles drawn after glass and water**, not before. Today the sea owns its pixels outright and erases any lamp glow in front of it (inferred).
  5. **The reflection trace's absolute cap of 64 per colour channel removed.** The tubes emit 34/63/100, so their mirror reflections lose 36% of their blue. Outliers stay bounded by the accumulator's relative clamps. One risk: until RT2-11 aims a ray at every emitter, a lucky hit on a bar that is not in the aimed list returns up to 100 at a single ray's weight, and the resolve already lifts its bright-sample cap where emission dominates (`reflection_resolve.rvshader:362-364`). That is RT-23's speckle mechanism. So this arm's gate includes `truth_test.py`'s bright-speck count, and if the count rises, the arm waits until after RT2-11.
  6. **A finite ceiling on specular before it is stored in half floats, plus an inf/NaN counter.** Since RT-21, a zero-radius light on a polished metal can overflow the half float.
  7. **The firefly clamp scaled by the lobe width**, and off where one ray covers the whole lobe. Today a sub-pixel light seen in a mirror is clamped down to its dark neighbours.
  8. **The two remaining half-float histories rounded through `half_float.glsl`** (water_foam and irradiance_fill; recorded as the owner's call).
  9. **One GI intensity for every GI source, or none.** Today the bridge profile's 2.6 scales the realtime bounce but not the baked one. Both arms are shown, and the owner decides.
  10. **The world-grid lookup compiled into the trace-only passes**, which today walk every light because it is compiled out of them (`pbr_fragment.glsl:2851-2941`). This is exact only on average, not pixel for pixel: the grid's cell lists are sorted by brightness (`LightGrid.cpp:194-215`), and RT-11's reservoir draws one random number per candidate in walk order (`pbr_fragment.glsl:3098-3125`), so the same seed picks a different light. The bake's walk over baked lamps keeps its own list.
  11. **The AO and GI ray budget's 2x2 boxes put back on the pixel grid.** Today they sit half a pixel off, so rows are skipped or counted twice (`tile_reduce.rvshader:52-61`). The rays move to different tiles.
  12. **One history counter.** `kSettledBound` and the image-distance blend read the trust count, which the anti-lag never resets; both move to the blend count. The stale "composite after TAA" headers are rewritten and TAA's dead alpha path removed. If RT2-0's arm F already replaced the image-distance smoothing, only `kSettledBound` is left.
  13. **Particles' velocity masked, and one transparency weight.** Sorted and additive particles blend their velocity output by an undefined alpha, and meshes and particles use different weights (20 against 90).
  14. **The tube rims' false motion.** A thin rim round each garage tube reports motion while the camera is parked (about 0.08% of pixels); its source is found and fixed.
- **Owner sees.** A darker, shadowed garage probe, which changes the fallback past the gloss window and the lighting at reflection hits. No reflection squares round lamp glows. Glows visible over the sea. Bluer tube reflections. Point highlights on chrome that survive. The bridge keeps its far lamp light after a baked lamp is switched. Particles stop smearing under TAA. Cheaper ray hits.
- **Touches.** `RuntimeLayer.cpp`, `EditorLayer.cpp`, `Scene.cpp` (3493, 4014-4099), `Renderer3D.cpp` (4134-4141, 4419), `direct_trace.rvshader` (463, 1406), `pbr_fragment.glsl` (2851-2941, 2955-2960, 5695, 5717, 5763, 5769, 6492), `LightGlow.cpp`, `light_glow.rvshader`, `ParticleRenderer.cpp` and the particle shaders, `UIRenderer.cpp`, `VulkanCommon.cpp` (blend presets), `reflection_trace.rvshader` (1004, 1013), `reflection_accumulate.rvshader` (1426-1449), `reflection_composite.rvshader`, `taa_resolve.rvshader`, `tile_reduce.rvshader`, the water_foam and irradiance_fill shaders.
- **Scope.** Patch. **Depends on.** RT2-1 and RT2-2 (the reference renderer, for arms 1, 5 and 7). **Risk.** Medium: these change the look on purpose, and the owner judges each.
- **Exit gate.** For each arm, the diff image against before, judged by the owner. In addition:
  - Arm 1: the frame-mean gap between probe and trace, and a diff of each against the reference renderer.
  - Arm 2: the bridge with one lamp switched, diffed against the unswitched frame; the far lamp light must be kept.
  - Arm 3: Headland with glows on against glows off, restricted to glossy opaque pixels: near zero after.
  - Arm 5: the tube reflection's hue against the reference, and `truth_test.py`'s bright-speck count no higher.
  - Arm 6: the inf/NaN counter at zero on every scene.
  - Arm 7: a sub-pixel light seen in a mirror against the unclamped reference.
  - Arm 10: a converged diff within grain against today; a parked camera leaves the reflection-change map black; the hit-record counters drop.
  - Arm 11: a diff within grain, judged as a diff image.
  - Arm 14: the taa-refusal view's count on the parked garage's tube rims drops to zero.
- **Size.** 4-6 sessions.
- **Closes.** GI-04, gi-s2, materials-s1, materials-s2, REFL-16, MAT-17, reflections-s4, GI-10, direct-18, REFL-13, ISS-7, ISS-8, ISS-9 (which estimate is right), ISS-31; parts of MAT-16, direct-02 and GI-06.

### RT2-7 · Random numbers that move in every anti-aliasing mode

- **What is wrong now.**
  - Every random choice in the traced shaders changes from frame to frame only while TAA's jitter is non-zero: lamp picks, soft-shadow points on discs and tubes, shadow-ray thinning, the thin-member fade, the sea's quad lane, `LampFrameSalt` (`pbr_fragment.glsl:4729-4731`, `1380`, `4075`, `4192`, `4819`; `ray_shadow_trace.glsl:24-25`; `water_lamps.glsl:453`; `Renderer3D.cpp:7987`, `8097`, `8267`).
  - Without TAA, each signal's accumulator averages the same frozen sample. That is why the owner forced TAA whenever rays are on (`FrameGraphBuilder.cpp:197-206`).
  - As a result, no signal can be shown to stand on its own, MSAA cannot run with rays, and a signal can come to lean on TAA unnoticed. RT-24's fix 1 does.
- **What changes.**
  - A frame-level sample service. Each signal's random sequence is indexed by that signal's own frame counter, and it advances whenever the signal's accumulator runs, whatever the AA mode. The jitter stays, for raster anti-aliasing only.
  - Rays traced inside draws with no accumulator of their own (the sea's mirror and refraction, the panes behind the nearest) hold still per pixel when no temporal filter runs, following the engine's WR-15 rule. RT2-31 gives them accumulators.
  - A measurement flag lets rays run under `--aa=none` and MSAA for testing.
  - The forced TAA is not lifted here. Young and moving pixels still lean on TAA through the sea's in-draw rays until RT2-31 and RT2-32, so the owner's answer to D3 is carried out at the end of M5 (RT2-33).
- **Owner sees.** Under TAA at the project's settings, nothing: pixel-identical, because the jitter is non-zero there (`TemporalJitterScale: 1`) and every draw already moves. That holds only while the jitter scale is above zero: at 0, TAA runs with zero jitter and every ray-traced draw freezes (`FrameGraphBuilder.cpp:142-160`). So the owner's RT-20 note that the picture looked "very stable" at jitter scale 0 was probably taken with the ray noise frozen. In the new no-AA and MSAA arms, noise that converges instead of frozen dots.
- **Touches.** `pbr_fragment.glsl`, `ray_shadow_trace.glsl`, `water_lamps.glsl`, the direct, reflection, GI and AO trace shaders, `Renderer3D.cpp`, `FrameGraphBuilder.cpp` (716-725, 197-206).
- **Scope.** Refactor. **Depends on.** RT2-1. **Risk.** Medium. Outside TAA, the direct light's four-frame specular memory will show its sampling noise. That is recorded and handed to RT2-24, not patched with TAA.
- **Exit gate.**
  - Under TAA: garage, bridge and camp parked frames and a spin_measure swing pixel-identical (diff 0).
  - With rays under `--aa=none`:
    - each signal's own output (`--capture-signals`) at frame 400 against frame 200 falls within the parked noise floor (it converges);
    - frames 200 and 201 differ in the random lanes (nothing is frozen);
    - with the frame filter's memory off, each signal's settled picture is within grain of its TAA-on settled picture (diff image);
    - moving and young states too, not only settled ones: a spin_measure swing and the frames after its stop converge per signal, and the grain score of young pixels is recorded;
    - the bridge's glint-flicker number is recorded.
  - With `TemporalJitterScale` at 0 under TAA: frames 200 and 201 still differ in the random lanes.
- **Size.** 2-3 sessions.
- **Closes.** reflections-s1, direct-07, ISS-12; the sample-service half of frame-03; the interim half of MAT-06.

### RT2-8 · A flicker floor that decides by behaviour, not by place

- **What is wrong now.**
  - TAA's flicker floor (the extra room its colour box gets from a pixel's own recent swing) is decided by where a pixel is: only at outlines, and never under a see-through surface (RT-22's two rules in `taa_resolve.rvshader`). The sea is drawn by the transparent pass, so it counts as covered and lost the floor that kept its lamp glitter steady. The bridge's blinking share rose from 0.80% to 0.97% of the frame (ISS-1).
  - Rules based on place cannot tell coverage flicker (an edge or sub-pixel detail swinging back and forth on the jitter's 8-frame cycle) from a real change (which moves one way and stays).
- **What changes.** The owner's parked proposal (ISS-2), built as filed. Per pixel, TAA keeps which side of its history the new input fell on and for how many frames running. The floor opens only while the input keeps swinging, and closes once the input has stayed on one side for a jitter cycle. It replaces both place-based rules, needs no water rule, and needs nothing from the later rewrites, so it lands here, right after RT-24, instead of at the end of the series.
- **Owner sees.** The bridge's water glitter back to its steadiness from before RT-22. The car window's streak stays fixed.
- **Touches.** `taa_resolve.rvshader`, `PostProcess.cpp` (one small history lane), `FrameGraphBuilder.cpp`.
- **Scope.** Patch. **Depends on.** RT2-0 committed, RT2-1, RT2-2a. **Risk.** Medium: it touches every pixel under TAA.
- **Exit gate.**
  - The four-way test: the window drive (`watch_arm.py light 8`) and the car's stop (`watch_arm.py car 20`) by the owner's eye; the bridge cables unchanged; the bridge water at 0.80% or better (`check_glint_flicker.py`).
  - RT-24's swing gate re-run; the parked edge shake at RT-20's numbers.
  - A,B,B,A cost.
- **Size.** 2-3 sessions.
- **Closes.** ISS-1, ISS-2.

### RT2-9 · Pipelines built at load; shader and pipeline caches (moved early)

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

### RT2-10 · One light library; area lights shaded as areas everywhere

- **What is wrong now.**
  - The range window, spot cone, sized-source highlight and water lobe are typed out eight times in three files, and the copies have drifted. The tube's length reaches the lit loop and DirectTrace but not ray-hit shading or the sea's fallback, and three comments contradict the code.
  - At ray hits every lamp is a point with a hard shadow (`pbr_fragment.glsl:2971-3096`), so a 3.07 m tube is a pinpoint in the chrome and the car.
  - Sized lights are treated as points for diffuse, cone and range.
  - A tube's single 0/1 shadow sample scales its whole highlight, so a partial occluder re-rolls the highlight every frame.
  - The tube highlight's energy grows with the tube's length: only the radius is renormalised. It has not been compared with a real tube since RT-21 removed the highlight cap.
  - There are no rectangle or disk lights, and the sun is hard.
- **What changes.**
  - **Step 1, pixel-identical.** One include, `include/lights.glsl`, holding per light type the light without shadows, a function that picks a point on the source with its probability, and the selection score. DirectTrace, ray-hit shading, the water, glass and the bake all call it.
  - **Step 2, one arm at a time:**
    - ray hits shade tubes and spheres as area lights with soft shadows;
    - DirectTrace's area-light estimate. Two forms: evaluate the light at the point the ray traced to, with that point's probability (unbiased, but noisy), or an analytic form (LTC: area-light shading from a fitted table, with the ray supplying only the shadow). The choice is not free: RT2-24's ratio estimator needs a light-without-shadows term with no noise in it, which only the analytic form gives. So the analytic form is the candidate, the sampled-point form is its reference arm, both are measured, and the owner picks knowing that constraint;
    - the tube length's energy is fixed after a comparison with a brute-force tube;
    - rectangle and disk types, and an angular radius for the sun.
- **Owner sees.** Tube reflections in the chrome and the paint become streaks. Highlights near partial occluders stop flickering. Soft sun shadows where a scene has a sun.
- **Touches.** New `include/lights.glsl`; `direct_trace.rvshader` (329-578), `pbr_fragment.glsl` (2971-3096, 4652-4895, 5102-5242), `water_lamps.glsl` (192-404), `ray_shadow_trace.glsl` (345-403), `Light.h`, `Renderer3D.cpp` (light packing, 4037-4060), `LightGlow.cpp`, the light inspector.
- **Scope.** Refactor. **Depends on.** RT2-2. **Risk.** Medium: step 2 changes the look on purpose.
- **Exit gate.**
  - Step 1: the every-light arm (K=0) and the default frames diff 0 on the garage, the bridge's three cameras and the camp. Drifts fixed on purpose are shown separately.
  - Step 2: against the reference renderer, region by region on the garage (floor, car, poles; `truth_test.py`'s distance and bright-speck count), and a single tube over a glossy plane at three roughnesses. On the bridge: per-pixel diffs at the three cameras with the mean brightness and the red-to-blue ratio, and `check_glint_flicker.py`, none of them worse. A,B,B,A cost.
- **Size.** 4-5 sessions.
- **Closes.** direct-06, direct-05, direct-s1, ISS-5; the area-light half of ISS-28.

### RT2-11 · Emissive geometry as sampleable lights; every fixture counted once

- **What is wrong now.**
  - Only 16 glowing surfaces get aimed rays in GI and reflections, taken first come, first served in scene order. The garage's 20 tube bars and the car's lamp parts compete for those 16 slots (`Renderer3D.cpp:3118-3121`).
  - Each surface is reduced to its bounding rectangle. A merged mesh, such as all of the bridge's lamp lenses as one entity, therefore radiates over empty space. In the realtime GI fallback that is orders of magnitude too much orange light, which may be the cause of the recorded red bridge.
  - A fixture made of a light plus a glowing mesh is counted twice. On glossy surfaces the analytic highlight adds to the traced image of the lens. On diffuse surfaces the live direct light adds to the lens emission stored in the bake (`irradiance_fill.rvshader:279`).
  - The texel-emitter tables engage only on flat, untransformed planes. Their sampler is a 12-step search through host-visible memory, re-uploaded every frame.
- **What changes.**
  - A persistent GPU emitter table, built from emissive triangles or clusters of them with their true area and power. It is updated when emissive geometry or an emissive texture changes. An animated emissive texture (a screen, a sign) refreshes its emitter's average power on the frames it animates, so the table never goes stale.
  - Picks without a search. An alias table (a constant-time way to pick in proportion to fixed weights) picks by power alone, because it encodes one fixed distribution. The terms that depend on the shading point (distance, facing) come from RT2-12's candidate structure, or, until RT2-12 lands, from resampling a few candidates drawn from the alias table. The 16-slot cap goes.
  - A luminaire link decides, per fixture, which representation carries the light in each path. The analytic light carries direct light and NEE at hits. The lens mesh stays visible to mirror rays (the owner's RT-7 ruling) but is kept out of the diffuse bounce, NEE and the bake wherever a light owns it. This is RT-7's planned single-counting rule, which was never built.
  - Per D7, every glowing surface without a light of its own becomes a light source. It is shown to the owner as its own arm first, and becomes the default only after the owner judges it. A surface that a light already owns (the tube bars) stays counted once.
- **Owner sees.** Calmer tube reflections, because every bar is aimed at. Glowing objects with no light of their own, like RT-22's cube, light what is around them (once the owner has judged that arm). After the re-bake, a slightly dimmer garage floor where the bake counted the bars twice. On the bridge, the realtime fallback without its saturated orange (if the bounding rectangle was the cause).
- **Touches.** `Scene.cpp` (2167-2245), `Renderer3D.cpp` (3108-3135, 5604-5625), `rtgi_trace.rvshader` (381-587), `reflection_trace.rvshader` (477-557, 683, 829-860), `irradiance_fill.rvshader`, `pbr_fragment.glsl` (3177, 3326), the texel-emitter machinery.
- **Scope.** Rewrite of the emitter model. **Depends on.** RT2-2 and RT2-5; not on RT2-10. It is too large to pull into RT-24 (5-6 sessions and a re-bake); RT2-0's arm K instead tells, before this item starts, whether the tube noise comes from bars left out of the aimed list. **Risk.** Medium.
- **Exit gate.**
  - First measure the double count: bake the garage with the bars' emission kept out of the bounce and the bake where a light owns them, and diff the field-lit picture.
  - Reflections: `truth_test.py` no worse in any region, and the tube patch's grain score on the swing test lower.
  - Every emitter in the scene reachable by aimed rays (a count in the log).
  - The bridge's realtime fallback (bake invalidated) diffed against the baked frame, with the mean and the red-to-blue ratio checked.
  - A,B,B,A.
- **Size.** 5-6 sessions.
- **Closes.** direct-03, gi-s1, gi-s3, GI-08, REFL-06, ISS-13, ISS-19; the luminaire half of ISS-28.

### RT2-12 · Light candidates at a fixed cost per pixel, shared by every pass

- **What is wrong now.**
  - DirectTrace scores every light whose range box touches the pixel's cluster cell, so its cost grows with the number of overlapping lights: 22 per pixel in the garage; 67 to 118 on average (190 at most) on the bridge, where the busiest cluster holds up to 150 of the 189 lights.
  - Ray hits walk every light in range. RT2-6 (arm 10) lets them use the world grid, but the cost still grows with the lights in range.
  - The world grid is a fixed 8x4x32 box shaped for the bridge, which breaks the rule against tuning for one scene. Both grids are rebuilt with a sort on the CPU, per view, every frame.
  - K independent reservoirs cost K random draws per candidate and pick with replacement.
  - Baked lamps with any moving object in range trace one extra ray each, outside the K budget.
  - The "moving object in range" flag costs lights × non-static meshes on the CPU, per view.
- **What changes.**
  - A GPU-built candidate structure over the persistent light and emitter buffers that adapts to the scene. Two designs are built and measured on the many-light scene, and the owner picks:
    - (a) a light BVH;
    - (b) ReGIR.
  - Every consumer draws from it: DirectTrace, hit shading in reflections, GI, water and glass, and the bake.
  - One sweep with K stratified picks replaces the K independent reservoirs.
  - The baked-lamp loss candidates enter the same sampler. Then "every lamp live, K-sampled" is measured against "full bake plus loss rays" on the bridge with moving cars.
  - The "moving object in range" flag is worked out only for fully and partly baked lamps, once per frame, through the grid.
  - Reuse that is aware of visibility (ReSTIR, with the two mechanisms RT-10 never built) is a later measured arm on top of this structure, not its base.
- **Owner sees.** A cheaper direct light on the bridge. The garage's converged picture identical to the every-light reference. The many-light scene becomes affordable.
- **Touches.** `LightGrid.cpp` and `LightGrid.h`, `Renderer3D.cpp` (4289-4355), new light-build compute shaders, `direct_trace.rvshader` (1124-1340), `pbr_fragment.glsl` (2851-2946), `water_lamps.glsl`, `Scene.cpp` (3068-3093, 2124-2127).
- **Scope.** Rewrite. **Depends on.** RT2-2, RT2-10, RT2-11. **Risk.** High: neither the current score nor the two candidates account for walls, so the many-light scene must include rooms whose light ranges cross walls.
- **Exit gate.**
  - The many-light scene at 1k, 5k and 10k lights: DirectTrace's time grows from 1k to 10k lights by no more than the palindrome spread (the curve is recorded), and the converged diff against the every-light reference shows no structure beyond grain.
  - Garage and bridge: the converged diff against the every-light reference no worse than today; the moving error on the dolly (`smear_metric.py`) and `emitter_lag.py` no worse.
  - A,B,B,A on the garage, Headland and Glitter.
  - CPU time of the grid build at 10k lights.
- **Size.** 6-8 sessions.
- **Closes.** direct-01, direct-08, direct-12, direct-16, direct-s3, GEO-15, ISS-18; the final halves of direct-02 and GI-06.

### RT2-13 · Transparent surfaces on the shared light code; the old in-shader light loop deleted

- **What is wrong now.**
  - Every see-through surface except the nearest glass pane is lit by the old loop inside the lit shader. Outside the baked field it uses the S4 sampler, whose random draws move only with TAA and nothing accumulates. Inside the field it traces one soft-shadow ray per casting lamp. Its WR-17 thinning relies on a list order that the brightness sort broke.
  - The opaque RT variant still compiles the loop, the S4 arrays (declared outside any define) and the mirror rays traced inside the draw, all switched off by uniforms.
- **What changes.**
  - See-through surfaces call the light library and the candidate structure, with a sampler that needs no history, measured against the current path on the close-up.
  - Then S4, WR-17, the light loop and the in-draw mirror are deleted from the RT-mode opaque variant. The raster recipe keeps the loop. The panes behind the nearest keep their in-draw reflection rays (the owner's RT-13 decision); they stay a named exception to "no reliance on TAA", which the owner accepted (D11).
  - Cell lists are ordered by a documented key.
- **Owner sees.** The back glass panes lit the same way as everything else. Possibly faster opaque shading, from fewer registers (unmeasured).
- **Touches.** `pbr_fragment.glsl` (1329-1384, 4597-4978, 5425-5454, 6093-6116), `LightGrid.cpp` (194-215), `Renderer3D.cpp` (variant defines).
- **Scope.** Refactor and deletion. **Depends on.** RT2-12. **Risk.** Low to medium.
- **Exit gate.**
  - The garage close-up's glass: a per-pixel diff against today, judged by the owner.
  - Opaque pixels: diff 0.
  - Register counts per pipeline (read through `VK_KHR_pipeline_executable_properties`) recorded before and after.
  - A,B,B,A.
- **Size.** 2-3 sessions.
- **Closes.** direct-s4, direct-15, MAT-09 (the loop half); part of ISS-41.

### RT2-14 · The compute question, answered first

- **What is wrong now.** Every ray pass is a full-screen fragment pass, and whether compute passes would be faster here has never been measured. The answer shapes RT2-15 and RT2-20, so it should come before them. The gain could be zero: RT-14 found the traced passes 90-99% bound by pixel count, and a fragment pass can also skip idle tiles by drawing one quad per tile.
- **What changes.** One A/B: DirectTrace as a compute pass in 8x8 tiles against today's fragment version, output pixel-identical, timed A,B,B,A. The compute version owns its output outside the graph, as voxel GI and the water foam already do, so nothing else has to change first. Texture reads during its shadow rays (cutouts) use an explicit level of detail, because compute has no screen derivatives.
- **Owner sees.** Nothing; a number that decides how RT2-15 and RT2-20 are built.
- **Touches.** `direct_trace.rvshader` (a compute variant), `ray_shadow_trace.glsl` (explicit level of detail), `Renderer3D.cpp`, `FrameGraphBuilder.cpp`.
- **Scope.** Measurement. **Depends on.** RT2-1 and RT2-2a. **Risk.** Low.
- **Exit gate.** Output diff 0 through the harness, and an A,B,B,A timing on the garage and at Headland. If compute wins beyond the spread, RT2-20 ports the other passes; if not, RT2-20 keeps only its other experiments. Either way RT2-15 is justified by its own gains, not by compute.
- **Size.** 1-2 sessions.
- **Closes.** The A/B half of GPU-02.

### RT2-15 · A render graph that schedules

- **What is wrong now.**
  - The render graph is an ordered list with pooled targets. It works out no barriers: the RHI issues one unbatched barrier per texture at every render-pass edge.
  - It shares no memory between short-lived targets.
  - Passes with no work still run: fifteen measured-change passes clear their targets and draw nothing whenever nothing changed. Dropping passes whose results nothing reads would not remove them, because their change maps are read.
  - It uses one queue, and it forbids compute and standalone passes from writing its images, while voxel GI and the water foam own storage images outside the graph.
  - Declaring the frame swaps histories, stamps camera records and sets renderer globals before the frame is even checked.
  - The graph is rebuilt every frame, with heavy allocation. The frame's builder has 84 `AddPass` call sites, and 61 to 72 passes ran in the benchmark.
- **What changes.** Five named steps, each behind `--render-graph=v2`, each ending in a report:
  - **16a.** Typed resources: attachments, storage images, buffers and the TLAS, each with a resolution class (output, internal, half, quarter), which is cheaper to add now than to retrofit in RT2-29 (render scale stays an optional setting under D8's native target). Every pass declares its reads and writes, with the pipeline stage and the kind of access. Declaration has no side effects: swaps, invalidations and camera records are committed after a successful execute.
  - **16b.** Compiling the graph works out batched barriers, with split barriers where producer and consumer are far apart. Proven with sync validation.
  - **16c.** Short-lived memory aliased by lifetime, and passes whose results nothing reads dropped.
  - **16d.** A pass can report that it has no work this frame, and its readers then get a constant cleared resource: the mechanism that actually removes the idle measured-change passes. The compiled graph is cached per frame shape; only resources are rebound each frame.
  - **16e.** The old executor is deleted.
- **Owner sees.** Nothing: the picture is identical. Less VRAM for short-lived targets (measured). Possibly some time back from batched barriers and skipped idle passes (measured, not promised).
- **Touches.** `RenderGraph.h` and `RenderGraph.cpp` (rewrite), `FrameGraphBuilder.cpp` (declarations), `VulkanCommandList.cpp` (render-pass transitions), `VulkanResources.cpp` (`TransitionTo`), `TemporalHistory.cpp`.
- **Scope.** Rewrite. **Depends on.** RT2-1 to RT2-4 and RT2-14 (D1 and D8 answered). **Risk.** High: a long pole with nothing visible to show for it until it is done.
- **Exit gate.**
  - Diff 0 through the harness, over 20 parked frames and a 60-frame swing, after every step.
  - Per-pass GPU timings in an A,B,B,A run, with the gaps between passes measured before and after.
  - VRAM of short-lived targets, before and after (RT2-1's report).
  - Validation clean with sync validation on.
  - scenetest green on Vulkan, and on OpenGL's frozen raster path.
- **Size.** 8-10 sessions.
- **Closes.** frame-01, frame-09, frame-17, GPU-07; the pass-culling part of frame-11 and the aliasing parts of GPU-09 and frame-05.

### RT2-16 · Views own their histories

- **What is wrong now.**
  - The applications own 16 temporal histories and 4 measured-change records per view (32 and 8 in the editor), and pass them in through 20 named FrameDesc fields. The runtime and each editor view fill those fields line by line, in three nearly identical blocks.
  - Histories are invalidated but never released: `Release` has no caller, and the target pool only grows.
  - The camera-cut list is maintained by hand and misses some histories.
  - The editor's game view repeats the shadow walk and the cull for its camera.
- **What changes.**
  - A View object owns one view's persistent resources: histories, exposure, ray budgets and change records, registered with the graph.
  - Generic rules: swap after a successful frame; drop on a camera cut, a resize or a feature being switched off; release when unused.
  - Applications create a view with one call. Work shared per frame is done once, and every view reads it.
- **Owner sees.** Nothing: the picture is identical. VRAM freed when a feature is switched off or a panel closes.
- **Touches.** `FrameGraphBuilder.h` (FrameDesc), `RuntimeLayer.h` and `RuntimeLayer.cpp`, `EditorLayer.h` and `EditorLayer.cpp`, `TemporalHistory.h` and `TemporalHistory.cpp`, a new ViewRenderer.
- **Scope.** Refactor. **Depends on.** RT2-15. **Risk.** Low.
- **Exit gate.**
  - Diff 0 in the runtime and in the editor with both views open.
  - A teleport arm under each AA mode drops every history.
  - VRAM falls by the view's history total when the game panel closes.
  - Editor CPU zones show no second shadow walk.
- **Size.** 3-4 sessions.
- **Closes.** frame-15; the release part of frame-05; the history half of frame-02; the generic half of framegraph-s2.

### RT2-17 · Signals and layers as data; Renderer3D split; RT and raster recipes

- **What is wrong now.**
  - Each signal is written out by hand inside `BuildFrame`. The glass reflection lane is a near copy of the opaque one, and the measured-change block exists four times.
  - Renderer3D is all static: about 37 shader and pipeline pairs, and 45 named descriptor sets plus 18 buffers for each view slot. Frame inputs reach draws through static setters, and switches are packed as bits into a float of the scene uniform.
  - Ray tracing is treated as a side feature of shadows. With shadows off, rays are off. The RT mode is resolved, and the lit shaders recompiled, inside `Scene::RenderShadows`, which also builds the TLAS outside the graph.
  - The G-buffer's shape is declared in three places, and they disagree.
  - About 40 of EngineConfig's roughly 150 fields are render switches, read deep inside passes.
- **What changes.** In stages, one signal at a time, never all at once. Each stage sits behind a flag, is diff 0 through the harness, and ends in a report.
  - **18a. The tables.** A descriptor per signal: its inputs and layer, grid divisor, history lanes and formats, kind of reconstruction, consumers and debug views. A descriptor per layer: the opaque G-buffer and the transmissive layer. One generic builder emits the passes, histories, camera-cut handling, budgets and change maps from those tables.
  - **18b. The signals migrate one by one**, in this order: AO, GI, direct light, reflections, then the glass lane. Each migration is its own diff-0 step with the hand-written version as its reference, which is then deleted.
  - **18c. Renderer3D split** into four parts: scene submission; a library of pipelines and variants; one module per signal; an explicit per-view render context passed to every pass. No statics remain.
  - **18d. Recipes and settings.** An RT recipe (the default wherever rays are available) and a raster recipe, as separate builders. The TLAS update becomes a graph pass, independent of shadows. One step resolves project settings and flags into a per-view settings struct, and the renderer reads only that. Measurement arms are registered in a list that shipping builds compile out. Structures shared between C++ and GLSL get one definition that both sides include.
- **Owner sees.** Nothing: the picture is identical. Adding a signal becomes one descriptor plus its shaders.
- **Touches.** `FrameGraphBuilder.cpp` and `FrameGraphBuilder.h`, `Renderer3D.cpp` and `Renderer3D.h`, `Renderer.cpp`, `Scene.cpp` (the rendering half), `RenderSettings.h`, `EngineConfig`, `scene_block.glsl`, `pbr_fragment.glsl` (the uniform mirrors).
- **Scope.** Rewrite. **Depends on.** RT2-15, RT2-16. **Risk.** High, which is why it is staged.
- **Exit gate.**
  - Diff 0 across all scenes and AA modes after every stage, with the flag on and off.
  - scenetest green.
  - A dummy signal added in a test build with one descriptor and nothing else.
- **Size.** 10-12 sessions.
- **Closes.** frame-02, frame-07, frame-08, frame-13, frame-16, CORE-06 (the render-world half), CORE-15, rhi-s2 (the single-definition half), the structural half of framegraph-s3, the ownership half of direct-13.

### RT2-18 · RHI core: bindless, a per-frame ring, the missing commands, uploads off the frame path

- **What is wrong now.**
  - Pass inputs go through descriptor sets that are rewritten every frame and must not be rewritten after they are bound. That rule has caused at least five recorded bugs (one produced 576 validation reports), and in Release a violation is silent undefined behaviour.
  - Only sampled textures are bindless, with 4,096 texture-and-sampler pairs.
  - Every upload, texture creation, texture mip and static BLAS build is a separate blocking submit on the graphics queue.
  - There is no copy from a buffer to a buffer or a texture, no indirect dispatch, and no draw count decided by the GPU.
  - The transfer queue is found at start-up and never created, and there are no timeline semaphores.
  - RHI objects hold layout state at record time, which blocks recording on several threads. Some GPU work never enters the graph and relies on that tracking: probe capture, ImGui, the loading screen and the bakes.
- **What changes.** Five named steps, so the two binding models coexist while passes migrate. Each ends in a report.
  - **19a. The missing commands and the upload ring** (no graph needed). New commands: copy buffer, copy buffer to texture, update buffer, clear texture, indirect dispatch, indexed indirect draw with a GPU count, and mesh-task indirect draws. A persistent mapped upload ring per frame in flight; blocking submits remain only for tools and bakes.
  - **19b. The transfer queue and timeline semaphores.** Each frame's uploads are batched on the transfer queue and handed over with a timeline semaphore.
  - **19c. The bindless heap and the constant ring**, migrated pass by pass. One global heap with tables for sampled images, storage images, samplers and TLAS slots; buffers reached by device address; resources as 32-bit handles, retired by TextureHeap's frame-slot scheme. Each draw or dispatch gets one push-constant block that points into a per-frame constant ring, so no descriptor is written per pass. Old-style sets and new-style handles work side by side until the last pass has moved.
  - **19d. The work outside the graph moves onto explicit barriers**: probe capture, ImGui, the loading screen and the bakes.
  - **19e.** Only then does the RHI stop tracking layouts at record time; the graph owns them.
- **Owner sees.** Nothing: the picture is identical. Shorter load times and no hitch on the first ray-traced frame (measured).
- **Touches.** `VulkanDevice.cpp`, `VulkanPipeline.cpp`, `VulkanCommandList.cpp`, `VulkanResources.cpp`, `TextureHeap.cpp`, `RHICommandList.h`, `RHIDevice.h`, `RHITypes.h`, every pass that commits a descriptor set, the probe capture, `VulkanImGui.cpp`, the loading screen, the bake passes.
- **Scope.** Rewrite. **Depends on.** RT2-15 and RT2-17 (which moves the TLAS build into the graph; until then it too relies on the record-time tracking). **Risk.** High.
- **Exit gate.**
  - Diff 0 through the harness after every step.
  - Cold and warm load times, and the worst frame while 200 new meshes spawn, before and after.
  - Blocking submits per frame (the counter): 0 in steady state and at spawn.
  - CPU time of descriptor work, before and after.
  - Validation clean with sync validation on, including a probe capture, a bake and the loading screen.
- **Size.** 11-13 sessions.
- **Closes.** GPU-01, GPU-04, GPU-10, CORE-03, frame-12, GPU-12 and MAT-14 (OpenGL frozen, D1), the queue and timeline part of GPU-05, the record-time state part of GPU-06.

### RT2-19 · A memory budget and lane budgets

- **What is wrong now.**
  - A failed allocation in Release carries on with a null handle, and nothing reacts to the budget RT2-1 now reports.
  - Histories have grown to about 440 bytes a pixel, 3.4 times RT-14's inventory. They include:
    - full-frame glass histories, although glass covers an unmeasured, probably small, share of the frame (the 0.06-0.82% often quoted is the share of pixels that changed when the glass layer was switched on, not its coverage);
    - a moving-layer lane written every frame although the feature is off;
    - a 32-byte copy of the G-buffer, made only because the G-buffer does not survive the frame (the TAA guide);
    - a history for a retired water pass.
  - The format list has no packed formats.
- **What changes.**
  - VMA's allocation priorities, and the budget RT2-1 measures, drive decisions: a failed allocation, or one that would pass the budget, degrades quality (fewer mips, lower internal resolution) instead of crashing.
  - Glass coverage is measured on the garage close-up and the bridge, so the glass histories' cost is known.
  - A lane budget per signal:
    - exact integer lanes instead of values packed into floats;
    - nothing allocated for switched-off arms;
    - last frame's G-buffer kept, instead of the TAA-guide copy (RT-14's own proposal);
    - packed formats added to the RHI and adopted only by diff image.
- **Owner sees.** Less memory used. A forced-low-memory test that degrades instead of crashing.
- **Touches.** `VulkanDevice.cpp`, `VulkanCommon.cpp`, `RHITypes.h`, `FrameGraphBuilder.cpp` (history formats), `TemporalHistory.cpp`, the accumulate shaders. VMA's types stay inside the Vulkan backend's private files.
- **Scope.** Refactor. **Depends on.** RT2-1 (the measurement), RT2-15, RT2-16. **Risk.** Low.
- **Exit gate.**
  - VRAM by category reported for the garage, the bridge and the camp, at 1600x900 and at 2560x1600, before and after.
  - A table of history bytes per pixel, by RT-14's method, before and after.
  - Diff 0 where a format change is exact; within grain, judged by the owner, where packing is adopted.
  - An arm with a forced low budget degrades without crashing.
- **Size.** 2-3 sessions.
- **Closes.** GPU-09, CORE-05 (the budget half), frame-05, reflections-s3, ISS-33.

### RT2-20 · Ray passes in compute; tile lists; queue and pipeline experiments

- **What is wrong now.**
  - Every ray pass is a full-screen fragment pass. That means no shared memory for neighbour gathers and blurs, no dispatch the GPU sizes to cover only the tiles with work, and no async compute.
  - The hit-shading include samples textures with implicit level of detail, which needs screen derivatives, so it cannot run in compute.
  - Every texture read at a ray hit is at mip 0, because nothing tracks the ray's footprint.
  - There is only one graphics queue.
  - Only ray queries are used: no ray-tracing pipelines, no SER.
- **What changes.**
  1. A hit-shading library usable from compute, with explicit level of detail from ray cones.
  2. If RT2-14's A/B favoured compute:
     - classify tiles and compact the tile lists on the GPU;
     - dispatch the budgeted passes indirectly, over only the tiles with work;
     - move the traces, the accumulators and the blurs to compute, with shared-memory neighbourhoods.
  3. An async compute experiment: the TLAS build plus one trace or accumulator, overlapping the G-buffer raster.
  4. A ray-tracing-pipeline and SER experiment on reflections (divergent hit shading), against ray queries in compute.
  5. Half-precision (fp16) arithmetic in the accumulators, watching the known half-float rounding trap (`include/half_float.glsl`).

  Each is adopted only on a measured gain.
- **Owner sees.** Frame time back where the ports win. Less sparkle on distant textured ray hits, from the ray-cone mips; this is a change in look, and the owner judges it.
- **Touches.** `pbr_fragment.glsl` (hit shading), `direct_trace.rvshader`, `reflection_trace.rvshader`, `rtgi_trace.rvshader`, `rtao_compute.rvshader`, `reflection_accumulate.rvshader`, `reflection_blur.rvshader`, `FrameGraphBuilder.cpp`, `VulkanDevice.cpp` (queues and extensions).
- **Scope.** Refactor. **Depends on.** RT2-14, RT2-15, RT2-18. **Risk.** Medium: the gain is unmeasured and could be zero; RT-14 found the traced passes 90-99% bound by pixel count.
- **Exit gate.**
  - Each port pixel-identical (diff 0), except the ray-cone mip arm, which is diffed against the reference renderer, and the fp16 arm, which is judged by diff image with the running averages checked for drift.
  - A,B,B,A per pass.
  - Adoption only on a gain beyond the spread.
- **Size.** 7-8 sessions.
- **Closes.** GPU-02 (the ports), GPU-11 (the experiments, fp16 included), MAT-05, frame-11, the async experiment of GPU-05.

### RT2-21 · The G-buffer as a designed contract, and one BSDF for every path

- **What is wrong now.**
  - The G-buffer's lanes were never designed as a contract:
    - albedo is linear 8-bit, which RT-2.2 names as its precondition;
    - there is no geometric normal, so shadow rays leave along the normal-mapped normal and mirror curvature comes from normal-map detail;
    - flags and two values share float bits;
    - there is no material or shading-model id;
    - under MSAA every lane is averaged at edges (Vulkan allows the sample-zero resolve only for integer formats).
  - Five different BSDFs are used, depending on the path:
    - DirectTrace uses GGX plus Lambert only, while clearcoat, sheen, anisotropy and wrap exist only in the raster loop;
    - ray hits get no normal map and read mip 0;
    - GI and bake hits are Lambert-only, so metals bounce nothing;
    - terrain hits see layer 0 only.
  - The vertex has no tangent.
- **What changes.**
  - A designed set of lanes, described once as data: depth; motion; shading normal and roughness; geometric normal; albedo (sRGB, 8 bits); F0 and metalness; a 32-bit integer lane for the id, flags and shading model; occlusion; emissive.
  - One BSDF module (evaluate, sample, probability and environment weight, per shading model: standard, coat, cloth, anisotropic). The composition, DirectTrace, every hit shader and the bake use it.
  - Hits are shaded at a "hit level of detail": the ray-cone mip, the normal map within the cone footprint (the tangent from the triangle's UVs, or a cooked frame), and layered terrain through its weight map.
  - GI and bake hits get the specular term, measured first.
  - Ray origins: offsets that scale with the size of the position (so they hold far from the world origin) along the new geometric normal, and positions rebuilt from depth relative to the camera, not in world units. RT2-41 carries the rest of large-world precision.
  - MSAA: integer lanes resolved by sample zero, float lanes by a small shader that takes sample zero, and a per-sample path at edges. MSAA with rays becomes selectable once the forced TAA is lifted at the end of M5 (D3).
- **Owner sees.** Normal-mapped walls keep their detail in reflections. Metals bounce light in GI, if measurement favours it. Materials with coat or sheen look the same in RT mode as in raster (today only latent, since no current asset uses them).
- **Touches.** `FrameGraphBuilder.cpp` (lanes), `pbr_fragment.glsl` (`SampleSurface`, `TraceSurface`, `ShadeTraced`), `direct_trace.rvshader`, `reflection_trace.rvshader`, `rtgi_trace.rvshader`, `irradiance_fill.rvshader`, `VulkanCommandList.cpp` (resolve modes), `MeshCook`, `Mesh.h`.
- **Scope.** Refactor. **Depends on.** RT2-17, RT2-20. **Risk.** Medium.
- **Exit gate.**
  - A pixel-identical step first: new lanes added, consumers unchanged.
  - Each consumer switched over separately, diffed against the reference renderer.
  - A parity scene for coat, sheen and anisotropy, RT against raster.
  - An MSAA edge scene: edge pixels under `--aa=msaa` diffed against `--aa=ssaa`.
  - A shadow-acne fixture (a strongly normal-mapped surface under grazing light) at the origin and 10 km from it: no acne at either.
  - G-buffer bandwidth per pass, by RT-14's method, before and after.
- **Size.** 6-8 sessions.
- **Closes.** MAT-02, MAT-04, MAT-12, GPU-14, direct-09, direct-10, direct-11, GEO-10, the geometric-lane part of REFL-04, the shading-model-id groundwork of MAT-15, the terrain-at-hits part of ISS-38; part of ISS-41; frame-16 (with RT2-17).

### RT2-22 · One composition pass: RT-2.2, then compute; the second raster deleted in RT mode

- **What is wrong now.**
  - In RT mode the opaque frame is still assembled by the forward lit shader. It rasterises every opaque draw a second time and samples the material again: about 0.6 ms of Headland's 2.2 ms lit pass (RT-SERIES.md:1692).
  - Per fragment, it combines probes, the field, the split-sum weight and emissive.
  - It writes an indirect lane that nothing reads.
  - It keeps the reflection's weight in the colour's alpha channel.
  - The weight's magnitude and hue come from two different shaders. An honest re-measure found the full-colour form about 7% darker; the in-code "4x" and "tint lost" comments are stale.
- **What changes.**
  - **Step 1** is RT-2.2 exactly as filed: the lit pass reads the G-buffer, with the albedo lane moved to sRGB8 first, and is diffed against the path that samples the material again.
  - **Step 2** (approved by the owner for RT mode only, D2, and kept only if it measures faster with a near-identical picture) is a full-screen compute composition in RT mode: emissive + direct + indirect × albedo + specular (the split-sum weight from the G-buffer's F0 and roughness, the same inputs the trace uses) + AO on stored bounce light only + fog. The reflection weight gets a lane of its own.
  - Then the opaque lit raster pass is deleted from the RT recipe (the raster recipe keeps it), together with the unused indirect lane.
- **Owner sees.** 1 to 2 ms back at Headland (0.6 ms of it measured). Reflection brightness from a single formula, which settles the 7%.
- **Touches.** `pbr_fragment.glsl` (5611-5925, 6184-6251), `FrameGraphBuilder.cpp` (1648-1665, 2609-2662, 3776-3826), `Renderer3D.cpp` (`DrawLit`), `reflection_composite.rvshader`, a new composition shader.
- **Scope.** Refactor and deletion. **Depends on.** RT2-21 (D2 approved). **Risk.** Medium.
- **Exit gate.**
  - Step 1: diff near zero against the sampled path on the garage, the bridge's three cameras and the camp; the owner judges the dark tones.
  - Step 2: diff near zero against step 1, except for the documented changes to the composition.
  - A,B,B,A.
  - MSAA, where enabled: edge pixels under `--aa=msaa` diffed against `--aa=ssaa`, as in RT2-21.
- **Size.** 4-5 sessions.
- **Closes.** RT-2.2, MAT-01 (the composition; the visibility-buffer end state is in section 7), MAT-08, frame-10, the final half of MAT-09, the structural half of materials-s1; part of ISS-17.

### RT2-23 · Reflection denoiser v2

- **What is wrong now.**
  - The depth used to find last frame's picture is smoothed at the surface's old position and averaged with 10 km sky misses.
  - Mirror curvature comes from normal-mapped normals and has no sign, so there is no concave case.
  - Under camera-only motion almost nothing checks the reflected content. The colour box is 6 to 12 spreads wide on glossy surfaces, the struck-identity test is off unless an object moves, and the distance test is loose. Keeping fewer frames is the only lever: ghosts or grain.
  - The density written for the resolve is always the old sampler's, even when visible-normal sampling drew the ray, which confounds that sampler's rejection.
  - There are two frame counters.
  - A variance lane is carried between the blur passes but never read.
  - One 1,973-line shader serves eight signal slots, through mode flags packed into floats and about twenty tuning constants.
  - Extra rays arrive only while something moves (RT2-0 patches part of this).
- **What changes.** The new denoiser runs behind `--reflection-denoiser=v2`, with the old path as the reference arm. Several pieces are tried first as arms on today's accumulator in RT2-0 (the bounded image distance, the struck-object check under camera motion, the fast-history clamp and young-pixel fill, extra rays after a stop). Whatever lands there carries into v2 unchanged. This item is the structural rewrite, in five named steps, each ending in a report:
  - **24a. Inputs:** this frame's spatially filtered hit distance in bounded form, with misses flagged; geometric curvature with a sign (RT2-21); the stable struck identity (RT2-5); the real sampling density.
  - **24b. Temporal:** two reprojection candidates (surface motion and virtual-image motion), blended by confidence instead of picking one. Checks under any motion: the predicted change in hit depth (given the camera's parallax) against the observed change, and the struck identity whenever anything moved, the camera included. One history-length counter drives the memory; a fast-history clamp bounds the long history.
  - **24c. Spatial:** a history fix for young and uncovered pixels, sized by hit distance and roughness, after outliers are bounded; a blur that uses the propagated variance; specular temporal stabilisation using the virtual motion.
  - **24d. Rays:** traded between tiles within a fixed average and driven by confidence, with extra rays kept on for the settle length. Visible-normal sampling re-measured with honest densities.
  - **24e. Structure:** one shader per kind of signal, built from shared functions; typed lanes; one roughness classification per tile, read by every pass; compute with shared-memory neighbourhoods. The moving layer's fate is decided by the owner here.
- **Owner sees.** Under a fast swing, lower ghosting and lower grain than today; RT-24's structural fix. Faster settling. Steadier chrome cube and poles.
- **Touches.** `reflection_trace.rvshader`, `reflection_resolve.rvshader`, `reflection_accumulate.rvshader` (split), `reflection_blur.rvshader`, `reflection_budget.rvshader`, `FrameGraphBuilder.cpp` (the signal descriptor), `Renderer3D.cpp` (the signal module).
- **Scope.** Rewrite. **Depends on.** RT2-2, RT2-5, RT2-7, RT2-20, RT2-21. **Risk.** High: the most-tuned part of the engine.
- **Exit gate.**
  - `spin_measure.py --stage=many16`: pole and bumper moving-against-settled at or below the memory-off numbers, with the memory on.
  - Without many16, a lower grain score than v1 at the same ray count.
  - The after-stop rows.
  - Against the reference renderer, region by region (floor, car, cube, poles): distance, bright-speck count and bias no worse.
  - `parked_stats.py` and `edge_shake.py` no worse.
  - `emitter_lag.py`'s reflection share, and RT-22's floor numbers.
  - The bridge's three cameras (diffs, glint flicker).
  - A,B,B,A, with the owner setting the cost bar from 24a's first measurement (section 6).
- **Size.** 9-12 sessions.
- **Closes.** REFL-03, REFL-04, REFL-05, REFL-07, REFL-11, REFL-12, REFL-15, ISS-3, ISS-20, ISS-21, ISS-30; RT-22's reflection third.

### RT2-24 · Direct light: visibility reconstructed apart from shading

- **What is wrong now.**
  - The direct light is denoised as shaded light, over time only. The young-pixel blur is off because a spatial filter that stops only at G-buffer edges smeared hard shadow edges on flat surfaces (T5, measured).
  - The specular half keeps four frames and leans on TAA to converge.
  - Most of RT-22's remaining trail sits in this history: 19.5% of the floor beside a moving light off by more than 16 levels, with the tubes as lines.
- **What changes.**
  - Visibility is separated from shading. The light without shadows stays sharp: exact where the candidate structure allows, otherwise its K-sample estimate. Only the ratio of shadowed to unshadowed light is denoised.
  - Over time, with measured change and a fast-history clamp.
  - In space, with a radius set by the penumbra width (from the shadow ray's distance to the occluder and the size of the source), not by G-buffer edges alone.
  - The analytic highlight is kept out of a lagging history.
- **Owner sees.** A shorter trail behind moving lights. Young pixels rebuilt without TAA. Hard shadow edges stay hard.
- **Touches.** `direct_trace.rvshader`, the direct accumulate variant, `Renderer3D.cpp` (the signal parameters), `FrameGraphBuilder.cpp`.
- **Scope.** Refactor. **Depends on.** RT2-7, RT2-12, RT2-20. **Risk.** Medium: an exact light without shadows pulls against a candidate structure of fixed cost, so the estimator is chosen by measurement.
- **Exit gate.**
  - `emitter_lag.py`: the floor beside the moving light below 19.5%; the car's side improved.
  - T5's shadow-edge test: the moving wall error no worse than a baseline re-measured on the current build first. (The old 2.71 levels was the shared memory-64 figure; the shipped memory of 4 measured 1.66 (`RT-FIRST.md:171-174`), and both predate the 3.07 m tubes, which now cast real soft edges.)
  - Converged diff against the every-light reference.
  - Convergence under `--aa=none` (from RT2-7).
  - The dolly's moving error.
  - A,B,B,A.
- **Size.** 4-6 sessions.
- **Closes.** direct-04, direct-s2 (the structural half); RT-22's direct-history part.

### RT2-25 · Bounce light from Realtime lights, added on top of the bake; one hit-shading contract

- **What is wrong now.**
  - In a baked scene the engine switches the live bounce pass off completely once the bake is ready. So Realtime lights give direct light but no bounce light at all: the garage has 7 of them and the bridge 14. The floor under RT-22's moving cube lights up, but none of that light reaches the car or the walls.
  - A surface is lit four ways depending on who looks at it: the lit pixel; a traced hit, using probe irradiance with no occlusion; a bake hit, using the sky with no occlusion; a probe capture.
  - The live bounce, where it runs, costs 2.8 to 3.0 ms of trace time (+3.6 ms of frame) at 1600x900 when forced on in the garage, and cannot serve the bridge (only the sky as second-bounce light, a 12 m reach).
  - The one world-space light store that exists (a 24 m box that follows the camera) is off in every shipped scene and read by no ray hit; its texture does not move with it, so misplaced light takes seconds to wash out.
  - Fixed clamps tuned on the showroom (4 and 32) cut real energy in brighter scenes, and the accumulator's diffuse kind has no stage for outliers.
- **What changes.** The owner's design (D4): bounce light in two layers that add up. Light adds up, so bounce from the baked lamps plus bounce from the live lamps equals bounce from all of them, and the two layers are exact together as long as the Static scene has not changed since the bake. Four named steps, behind `--realtime-bounce`, each ending in a report:
  - **26a. The layering rule.** Each light's bounce comes from exactly one layer. Half, Full and Hybrid lights: from the bake, read as today (baked lights stay baked). Realtime lights: from the realtime layer. In a scene with no bake (the camp), every light is Realtime, so the realtime layer carries all of it.
  - **26b. The realtime layer.** About one ray per pixel at half resolution. Each hit is lit by Realtime lights only, because the baked lights' bounce is already in the bake and would otherwise count twice. The whole pass is skipped when no Realtime light reaches the view, so a fully baked view pays nothing. A relative outlier stage replaces the fixed clamps, and the history-box width is re-measured on the trace's grid. A small camera-centred cache (cascades, wrap-around addressing, a fixed update budget as a global setting) stores the realtime layer's light so that reflection hits and second bounces can read it; it is built only if measurement shows it pays.
  - **26c. One hit-shading contract**, used by the realtime layer, reflections, the bake and probes: direct light from the candidate structure; bounce light from the bake for baked lights plus the realtime layer's cache for Realtime lights; sky light from occluded rays. The hit specular term (GI-09) is measured here.
  - **26d. Probes captured through the ray-traced world.** A probe is rendered by the same hit-shading contract, so it shows what the rays see. After RT2-27 it serves only as the rough-reflection fallback.
- **Owner sees.** Realtime lights fill the room around them with bounce light, the way baked lamps do. A fully baked view costs nothing extra. The camp's live bounce is cheaper. Probes agree with the traced picture.
- **Touches.** `rtgi_trace.rvshader`, `pbr_fragment.glsl` (`TraceSurface`, `ShadeTraced`), `irradiance_fill.rvshader`, `reflection_trace.rvshader`, `ReflectionProbe.cpp`, `RuntimeIrradianceField.*` (replaced by the small cache, if built), `Renderer3D.cpp`, `Scene.cpp` (the GI source switch; the field, cache and probe ownership).
- **Scope.** Rewrite. **Depends on.** RT2-2, RT2-10, RT2-11, RT2-12, RT2-20, RT2-21. **Risk.** Medium: every light must be counted in exactly one layer, including Hybrid lights, whose near direct light is live while their bounce and far direct light are baked.
- **Exit gate.**
  - `--pass-timings`: the realtime layer absent in a fully baked view, present when a Realtime light is near.
  - The garage with its Realtime lights on: the baked layer plus the realtime layer against the reference renderer with every light: near zero, or favouring baked (the owner's bar).
  - RT-22's moving cube: its bounce follows it (`emitter_lag.py`), and nothing is counted twice where it passes a baked lamp.
  - The camp (all Realtime): distance to the reference renderer within grain.
  - The leak scenes in `check_gi.py` stay at 0.000.
  - The garage probe captured through the traced world, diffed against the reference renderer at the probe's position.
  - A,B,B,A at native resolution. The proposal is about 1 ms for the realtime layer while Realtime lights are near; the owner sets the bar from 26b's first measurement.
  - The cache's VRAM, if it is built.
- **Size.** 9-12 sessions.
- **Closes.** GI-02, GI-05, GI-07, GI-09, GI-11, ISS-10; the Realtime-light half of GI-01; the traced-capture half of GI-04; the hit part of GI-06.

### RT2-26 · One rule for indirect light, and a bake that knows when it is stale

- **What is wrong now.**
  - The lit pixel adds a probe-or-sky term beside the traced bounce, and the two overlap: a local probe's irradiance already contains the room's light. The field's sky fraction is applied only where the bounce gave no answer. AO, which reaches 0.4 to 0.5 m, multiplies the traced bounce and the stored direct light.
  - The bake's stamp cannot see a moved Static object or an edited material, so a stale bake is silently wrong until someone re-bakes.
  - At most 8 volumes can be placed by hand, and the bridge uses all 8.
  - The field stores fully baked lamps' direct light at cell resolution, where AO darkens it (it does not darken the same lamp's live share).
  - Nested volumes do not blend, the rt and ss files are identical, and there are eleven indirect-light systems.
- **What changes.**
  - One composition rule: indirect light = the baked layer + the realtime layer (RT2-25), each light counted once. The separate probe-or-sky term is dropped wherever a layer gives an answer. AO applies to stored bounce light only, never to direct light, stored or live.
  - Baked lights stay baked (D4): their bounce, and the far direct light of Full and Hybrid lamps, keep coming from the bake at almost no cost.
  - The bake's stamp covers Static objects' transforms and materials, and the editor warns when a Static object has changed since the bake; the fix is a re-bake. Dynamic objects are unaffected: they are lit by both layers and never make the bake stale.
  - Nested volumes blend, and there is one file per bake. The stored field becomes sparse bricks that stream with the world (RT2-40), not one resident atlas capped at 8 volumes.
  - Cells that fall inside walls move out (probe relocation), which fixes the leak case the "cells smaller than the thinnest wall" rule could not. Light bleeding at contact scale (about 1.9% of pixels today) is measured against the reference renderer.
  - The raster recipe reads the same format. The reference arms move behind engine flags; RT2-30 deletes them. The settings documentation is rewritten.
- **Owner sees.** The baked and realtime layers agree where they meet. A stale bake is flagged in the editor instead of being silently wrong. AO no longer darkens baked lamps' direct light.
- **Touches.** `pbr_fragment.glsl` (5611-5925), `IrradianceVolume.*`, `BakedLighting.cpp`, `Scene.cpp` (the bake orchestration and its stamp, 3900-4100), `irradiance_fill.rvshader`, `check_gi.py`, the editor's bake panel, `RenderSettings.h`, `PostSettings.h`.
- **Scope.** Refactor. **Depends on.** RT2-25. **Risk.** Medium.
- **Exit gate.**
  - The baked layer plus the realtime layer against the reference renderer, diffed on the garage, the camp, the bridge cameras and the bright-sky scene: near zero, or favouring baked.
  - `check_gi.py` updated for the new rule (its gi_skylit band encodes today's residual) and green.
  - The leak scenes at 0.000, including the case that needed cells to move out of walls.
  - Contact-scale bleeding: the share of pixels off the reference by more than the grain falls from today's 1.9%.
  - Moving a Static object in the editor raises the stale-bake warning; moving a dynamic one does not.
  - A,B,B,A. Bake time and disk size recorded.
- **Size.** 7-9 sessions, in named steps: the composition rule; the stamp and the editor warning; nested blending, relocation and sparse bricks; the raster recipe and the docs.
- **Closes.** GI-01, GI-03, GI-12 (sparse bricks included), GI-13, GI-14, ISS-37; the field-lookup part of ISS-16.

### RT2-27 · Reflections tiered by roughness; rough reflections from stored light

- **What is wrong now.**
  - Every reflection pixel is traced at full resolution and gets the full resolve and three blurs, even when glossy and settled. In the garage that is 10.6 ms at 1600x900.
  - Above roughness 0.6 the reflection comes only from the baked probe, which is about a third brighter than the trace.
  - Between 0.25 and 0.6, two estimates that disagree are cross-faded.
- **What changes.**
  - First arm: measure how much of each garage and bridge frame falls in each roughness class, per tile. Nobody has measured it, and the saving depends on it.
  - One roughness classification per tile:
    - mirror-like tiles are traced at full resolution with full hit shading;
    - glossy tiles at half resolution, using RT-3.1's guided downsample and joint upsample;
    - rough tiles read the stored bounce light's direction (the bake, plus the realtime layer's cache) instead of the baked probe.
  - The gloss window becomes a cost setting inside one estimator (a global setting), not a switch between two estimates.
  - The blurs run only where the picture is noisy or young.
- **Owner sees.** Section 6's largest single saving. Rough surfaces stop taking their reflection from a probe that is a third too bright. No brightness seam across roughness.
- **Touches.** The reflection shaders, a classification pass, `FrameGraphBuilder.cpp`, `pbr_fragment.glsl` (the gloss window).
- **Scope.** Refactor. **Depends on.** RT2-23, RT2-25. **Risk.** Medium: RT-3.1 measured half resolution darkening the bridge's cables by 0.58 levels.
- **Exit gate.**
  - The reference renderer, region by region, plus a roughness-ramp scene with no seam anywhere from 0.2 to 0.8.
  - `spin_measure.py` no worse.
  - The bridge's cable band.
  - A,B,B,A. The proposal is garage reflections at most 4 ms at native 2560x1600; the owner sets the bar from the first arm's coverage measurement.
- **Size.** 5-6 sessions.
- **Closes.** REFL-08, reflections-s2; the fallback part of ISS-9.

### RT2-28 · The final temporal pass does anti-aliasing only

- **What is wrong now.**
  - With rays on, TAA is forced on, and it has become the place where signal and surface problems get patched. It takes 12 inputs, including the sea's motion, two change maps and the see-through revealage.
  - The reflection passes through two memories in a row, and TAA's single motion vector per pixel is chosen by comparing brightness.
  - A changed reflection restarts the whole pixel, however little of the pixel the reflection is.
  - The material widening assumes rough dielectrics look the same from every angle.
  - (The place-based flicker rules behind the water regression are already replaced by RT2-8's behaviour-based floor.)
- **What changes.**
  - Every signal is fully reconstructed before composition. The final pass anti-aliases geometry only.
  - It reads one generic per-pixel mask saying how much of its history is invalid, built from the signals' change maps and the layers' coverage.
  - The sea's special motion input (`PostProcess.h:147-152`) becomes a generic motion lane per see-through layer, so the sea keeps correct motion and no input is specific to a surface type. It is not simply removed: until RT2-31 gives the transmissive layer its own accumulators, the sea still needs its motion here.
  - Reflections are either composited after the pass (upsampled on their own guide), or composited before it with their own confidence setting a short memory there. Both are measured on the swing test and the bridge, and the owner picks. RT-4 and RT-6.1 measured compositing after TAA as worse, but that was before the reflection had its own stabilisation.
- **Owner sees.** RT-24's spread gone structurally. The bridge's water glitter stays at RT2-8's level. The window drive stays fixed, and the cables are unchanged.
- **Touches.** `taa_resolve.rvshader`, `PostProcess.cpp` and `PostProcess.h` (`TemporalResolve`), `reflection_composite.rvshader`, `FrameGraphBuilder.cpp` (3776-4047).
- **Scope.** Refactor. **Depends on.** RT2-7, RT2-8, RT2-23, RT2-24. **Risk.** Medium.
- **Exit gate.**
  - The four-way test: the window drive, the car's stop, the bridge cables, and the bridge water (`check_glint_flicker.py` at 0.80% or better).
  - RT-24's gate from RT2-0, re-run, including the young-pixel grain with the frame filter's memory off.
  - The parked edge shake at RT-20's numbers.
  - A,B,B,A.
- **Size.** 4-5 sessions.
- **Closes.** REFL-01, REFL-10, reflections-s5, ISS-22, the mask half of frame-03; the TAA reliance of fix 1 that D9 allowed RT-24 to be committed with, if arm H did not remove it.

### RT2-29 · Render scale and upscaling, as an option

- **What is wrong now.**
  - Every target is a fraction of the output size. SSAA can only render larger, and TAA's history sits at output size.
  - There is no render scale, no upscaler and no dynamic resolution.
  - The traced passes are 90-99% linear in pixel count (RT-14: 16 of 24 ms at 3.24 megapixels).
- **What changes.**
  - Internal and output resolutions become separate in the graph, and signal grids become relative to the internal one.
  - The final temporal pass can upscale. Per D5, the engine's own TAAU is the default, and DLSS Super Resolution and FSR are optional behind the same setting, with their SDKs inside private files. DLSS Ray Reconstruction is only a measured arm.
  - Render scale is one global setting, with `--render-scale`. Optional dynamic resolution is driven by GPU time, also a global setting.
  - Per D3, MSAA 4x stays selectable at full resolution; a render scale below 1 uses the TAA upscaler.
- **Owner sees.** A fallback lever, not part of the plan, because the owner's target is native resolution (D8). At a render scale of 0.75 per axis, every per-pixel pass handles about 44% fewer pixels, for a softer image the owner judges.
- **Touches.** `RenderGraph`, `FrameGraphBuilder.cpp`, `taa_resolve.rvshader` (becoming TAAU), `PostProcess.cpp`, `RenderSettings.h`.
- **Scope.** Refactor. **Depends on.** RT2-28 (D5 answered). **Risk.** Medium: the thin bridge geometry, and the owner's eye on the water glitter.
- **Exit gate.**
  - Arms at 0.67 and 0.75 against native, at the same output resolution.
  - The bridge's `check_glint_flicker.py` and cable band.
  - The garage's parked, edge and smear metrics.
  - A per-pixel diff against native at output resolution, judged by the owner's eye.
  - A,B,B,A.
- **Size.** 5-6 sessions.
- **Closes.** frame-06; the pixel-count part of ISS-17.

### RT2-30 · Delete the superseded reconstruction and GI paths

- **What is wrong now.** Once RT2-23 to RT2-29 are proven, the old paths are dead weight:
  - the old accumulator's special rules (the memory caps, the floor scaled by motion, `kSettledBound`, the untested silhouette history, the curved-mover choice);
  - the moving layer and follow-hit (per the owner's RT2-23 decision);
  - TAA's reflection-motion lane and change-map inputs;
  - the old resolve and blurs;
  - the camera-following runtime field;
  - `gi_denoise` and the one-frame-late traced GI chain (`--gi-signal=off`);
  - the post-chain AO arm (`--ao-signal=off`);
  - the probe-as-sky diffuse term in RT mode.
- **What changes.** All of it is deleted, with its settings and histories.
- **Owner sees.** Nothing at default settings. A smaller shader set.
- **Touches.** The reflection shaders, `taa_resolve.rvshader`, `RuntimeIrradianceField.*`, `gi_denoise`, `FrameGraphBuilder.cpp`, `EngineConfig`.
- **Scope.** Deletion. **Depends on.** RT2-23 to RT2-29. **Risk.** Low.
- **Exit gate.** Default frames unchanged from the last proven state (diff 0); scenetest green; compile count and binary size recorded.
- **Size.** 2 sessions.
- **Closes.** The final parts of REFL-12, GI-13 and frame-13.

### RT2-31 · Glass and water in the ray-traced world; one transmissive layer

- **What is wrong now.**
  - Blended glass and all water are left out of the acceleration structure. So glass casts no shadow, car windows are missing from floor reflections, and the bridge's metal cannot reflect the sea.
  - There is no coloured transmission, so glass cannot tint what is behind it, and only water refracts.
  - Glass and water use two separate single-layer targets, in different formats.
  - Only the nearest pane uses the shared passes, and the panes behind it cast their own rays.
  - The sea's mirror and refraction rays are traced inside its draw with no accumulator, so only TAA averages them.
  - The glass pane's own lamp light is never re-lit by measured change.
  - The whole glass reflection chain is duplicated at full frame size (96 bytes a pixel of history), although glass covers only a small share of the frame (measured in RT2-19; the old "about 1%" was a share of changed pixels, not coverage).
- **What changes.**
  - Glass and water enter the acceleration structure as a third traversal class. Shadow rays multiply their transmittance; other rays hit them and shade them with the one BSDF.
  - One "first transmissive surface" layer, in one format, feeds DirectTrace, the reflection chain and a new refraction pass. It is a compacted list of points tagged by layer, with histories kept only for the listed points, each signal with its own accumulator.
  - Measured change on the layer.
  - An RGB transmittance target, for tinted glass.
  - WBOIT stays for smoke, particles and the deeper panes. The panes behind the nearest stay on the old path, by the owner's RT-13 decision, and are lit by the library since RT2-13. Their in-draw reflection rays are the one named TAA exception, which the owner accepted (D11).
  - The sea's special motion lane in the final temporal pass (kept generic by RT2-28) is now fed by the layer's own motion.
- **Owner sees.** Car windows in floor reflections. Tinted glass shadows. The sea reflected in the bridge's metal and wet deck. Glass and sea noise averaged by their own accumulators.
- **Touches.** `Scene.cpp` (2881-2936), `Material.h`, `ray_shadow_trace.glsl` (`RayTraverse`), `pbr_fragment.glsl` (6087-6477), `FrameGraphBuilder.cpp` (642-700, 2731-3023), `oit_resolve.rvshader`, a new refraction pass.
- **Scope.** Refactor. **Depends on.** RT2-17, RT2-21, RT2-23. **Risk.** High.
- **Exit gate.**
  - The garage close-up and the pier, diffed against the reference renderer.
  - The bridge's `check_glint_flicker.py` at 0.80% or better.
  - Glass cost at the coverages RT2-19 measured (the garage close-up and the bridge), against today's +0.45 ms at the owner's close-up (where glass rays are 1.3 of the transparent pass's 2.0 ms).
  - The layer histories' VRAM.
- **Size.** 8-10 sessions, in named steps: the traversal class and tinted shadows; the one transmissive layer and its lists; the signals and accumulators on the layer; the refraction pass.
- **Closes.** MAT-03, MAT-06, REFL-09, ISS-23, the mirror half of ISS-11 (D10); RT-22's glass-pane part.

### RT2-32 · Water as a material, a surface generator and a medium

- **What is wrong now.**
  - Water is a parallel renderer. It has its own grid (a fixed 3 m spacing with no LOD), its own lobe inside the lit shader, its own surface layer, three implementations of lamp light, 19 flags, and refraction inside its draw.
  - At Headland the sea costs about 8.4 of 17.1 ms: Transparent 5.58, DirectWaterShade 1.09, DirectWaterChoose 0.58, WaterSurface 0.75, WaterFoam 0.22, WaterAccumulateLamps 0.17.
  - The seabed under the sea is G-buffered, ray traced and lit every frame, then covered entirely.
  - The lamp passes shade one water surface per pixel, while the water draw blends three to five crossings at 400 m (about 10% too bright at Glitter).
  - The sea work the owner accepted on 2026-09-20 is on the parked branch; per D10 it comes back here.
- **What changes.**
  - Water becomes:
    - a surface generator: displacement on a LOD grid, detail normals and foam memory;
    - the tuned lobe (anisotropic Beckmann, the Cox-Munk footprint roughness) as material parameters;
    - a medium for what lies below the surface.
  - The block-rate choosing (the sea's lamp choice made once per block of pixels rather than per pixel) stops being a water feature. It becomes a feature of the shared light-candidate structure, driven by one global setting, so any surface can use it. A cost strategy attached to one material would be the kind of surface-type rule the owner rejected.
  - It is drawn through the transmissive layer, with a displaced stand-in in the ray structure.
  - Coverage is known before the opaque signals, so any pixel that a transmissive layer covers completely skips DirectTrace, AO, GI, reflections and composition for the surface underneath. This is a generic rule for any fully covering layer, not a water rule. The raster-refraction path keeps its backdrop.
  - Per D10, the parked branch's sea work is re-landed here where it maps, as the starting point. It is checked with a moving camera (it never was) and judged live by the owner.
  - Deleted: the private accumulator, water_trace, the separate WaterSurface target, the in-shader lamp sampler, and the water-only flags.
- **Owner sees.** A faster bridge frame (the proposal is the sea at most 4 ms at native resolution; the owner sets the bar from the first measurement), no private sea behaviour, and the pier look the owner accepted, judged live.
- **Touches.** `Water.cpp`, `pbr_fragment.glsl` (4205-4422, 5246-5315, 6334-6477), `FrameGraphBuilder.cpp` (3096-3643), `Renderer3D.cpp` (1737-1757), `water_*.rvshader`, `water_lamps.glsl`, `EngineConfig`.
- **Scope.** Refactor and deletion. **Depends on.** RT2-31. **Risk.** High: RT-8 found the sea bit every session.
- **Exit gate.**
  - Per-pixel diffs at the bridge's three composed cameras (RT-8's lesson), with the mean brightness and the red-to-blue ratio unchanged unless shown to the owner.
  - `check_glint_flicker.py`.
  - The owner's live check on the pier.
  - A,B,B,A at Headland and Glitter.
  - The owner is asked for the camera behind the blur report (ISS-32) before chasing it.
- **Size.** 8-10 sessions.
- **Closes.** MAT-10, materials-s4, ISS-11, ISS-32; the final parts of ISS-15 and direct-15.

### RT2-33 · Fog, glows and particles in the composition; TAA no longer forced

- **What is wrong now.**
  - Fog is one post pass after TAA over the opaque depth. So traced reflections and refractions carry no fog along their own path: a far tower stays sharp in the near water. See-through pixels are fogged by what lies behind them.
  - Particles are unlit and have no motion.
  - With rays on, the engine still forces TAA.
- **What changes.**
  - Analytic height-fog transmittance and in-scatter at ray hits, along reflection and refraction segments.
  - Fog applied in the composition, before the final temporal pass, to opaque and transmissive surfaces alike.
  - Particles lit from the stored bounce light, plus a direct candidate at each particle's centre.
  - The froxel volume (WR-11) stays in the WR series.
  - **The last step of M5: TAA stops being forced (D3).** With the sea and the nearest glass on their own accumulators, every signal cleans itself up without TAA; the back panes are the owner's named exception (D11). TAA becomes a choice like any other AA mode. With rays it stays the default, and MSAA 4x becomes selectable at full resolution.
- **Owner sees.** The far bridge fogged in its reflection on the sea as well. Lit particles in the ray-traced scene. MSAA 4x available with rays on.
- **Touches.** `fog.rvshader`, the composition shader, the hit shading, `particle*.rvshader`, `ParticleRenderer.cpp`, `FrameGraphBuilder.cpp` (the TAA force), the render settings panel.
- **Scope.** Refactor. **Depends on.** RT2-22, RT2-25, RT2-31, RT2-32. **Risk.** Low.
- **Exit gate.**
  - With fog off, frames byte-identical. A Headland diff judged by the owner's eye. A,B,B,A.
  - The TAA step: with rays under `--aa=none` and under MSAA 4x, every signal converges, moving and young states included (RT2-7's checks); the owner judges MSAA 4x with rays live.
- **Size.** 3-4 sessions.
- **Closes.** MAT-13, the lighting half of MAT-16, the structural half of materials-s2; the prerequisites of ISS-29; ISS-12's forced TAA (D3).

### RT2-34 · A GPU scene and change-driven transforms

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
  - Only physics bodies are interpolated; objects moved in the fixed step move stop-go at any display rate other than 60 Hz.
- **What changes.** Four named steps, each ending in a report:
  - **35a. Tracked transform writes.** Writes to transforms go through setters that stamp a change version. C#, physics and the serializer use the same path. This breaks the native-module ABI, so the documented rebuild step runs and is checked.
  - **35b. A flat hierarchy.** Depth-ordered and linked by parent index. World matrices are worked out once per frame, for changed subtrees only. The old compare walk stays as `--validate-transforms`.
  - **35c. The GPU instance table.** Render proxies get stable slots in one device-local instance table: world 3x4, previous world, bounds, mesh and LOD, material offset, flags and ray mask. Only changed rows are uploaded, so the per-object loop above runs only for objects that changed. Culling, raster, TLAS instances, hit shading and every id read that one index. Extraction produces a snapshot.
  - **35d. Fixed-step interpolation, as its own owner-judged arm.** Previous and current simulation poses are kept for anything a fixed step moved, and blended at render time. This changes what moving objects look like (every script that moves things in `OnTick` glides instead of stepping), so the owner judges it live on its own.
- **Owner sees.** At scale, CPU frame time falls. The demo scenes are unchanged until 35d, whose change the owner judges.
- **Touches.** `Scene.cpp` (`UpdateWorldTransforms`, `RefreshDrawList`, `OnRender`), `Components.h`, `ECS.h`, `Renderer3D.cpp` (instance data), `GpuCull.cpp`, `Interop.cpp`, `PhysicsWorld.cpp`, the serializer.
- **Scope.** Rewrite. **Depends on.** RT2-5, RT2-17, RT2-18. **Risk.** High: changing the component layout breaks native modules and needs the documented rebuild step.
- **Exit gate.**
  - Diff 0 on the demo scenes after 35a to 35c.
  - `bench_scale.py`'s ray-traced arms on the realistic-content scenes at 20k, 60k and 120k: the transform-update, extraction and instance-fill phases grow with the number of changes, not the number of objects (the still arm near zero; moving arms at 1%, 10% and 100%).
  - A,B,B,A.
  - `--validate-transforms` clean on every scene and in scenetest.
  - 35d: the velocity lane of a fixed-step mover at `--frame-time=0.00833` (half a tick) is smooth instead of zero-then-a-step, and the owner judges the live movers.
- **Size.** 10-12 sessions.
- **Closes.** GEO-01, GEO-02, CORE-02, CORE-07, ISS-26, the extraction half of frame-04, the engine half of CORE-08.

### RT2-35 · The ray-tracing world as its own module

- **What is wrong now.**
  - The TLAS instance list is rebuilt from the ECS every frame (per view) inside the shadow code, and packed on the CPU. (RT2-4 already skips the build when nothing changed, and removed the shared-pointer copy per instance.)
  - A refit with the same count but different contents leaves a degraded tree for up to 64 frames.
  - The hit table is rebuilt from scratch every frame in `EndScene`. That is probably the roughly 4 ms that rays add to the G-buffer pass's CPU at 60,000 objects (inferred; RT2-2f confirms).
  - Each static BLAS is built the first time it is traced, with its own scratch buffer and a blocking submit. It is never compacted, and nothing tracks its memory.
  - Skinned BLASes are refit forever, one after another, and skinned twice (once in the vertex shader, once in compute), with no normals written.
  - Each material section of a model is its own BLAS and TLAS instance (a car of 150 parts).
  - The instance masks carry only static and moving.
- **What changes.**
  - A compute pass writes the TLAS instance buffer from the GPU scene. The build is skipped when nothing changed, refit when only transforms moved (judged against that slot's own last build), and rebuilt on structural change or at the refit limit. The policy per instance class is measured.
  - A BLAS manager:
    - builds when geometry loads, batched per frame under a build budget (a global setting), on async compute where that measures faster;
    - compacts and pools the results;
    - reports BLAS bytes by category.
  - One multi-section BLAS per mesh LOD and one TLAS instance per placed model, with the material found through the geometry index. On import, placing a model creates one entity, with sub-entities only where an author needs to move a part; that also takes a car of 150 parts from 150 entities to one, which matters against the ECS's cap of about a million entities.
  - The hit table becomes a view of the GPU scene, updated only where rows change.
  - One skinning pass per frame writes posed position, normal and previous position for raster and for batched refits, with a rebuild after large deformations.
  - A mask policy per ray type in RenderSettings, adopted by diff image.
- **Owner sees.** At scale, the share of the shadow phase that RT2-2f attributes to the ray-tracing world falls toward 1 ms at 60,000 objects (a proposal; the draw-list and cull-table share, about 3 ms with rays off, belongs to RT2-34 and RT2-43). No hitch on the first ray-traced frame. Less BLAS memory.
- **Touches.** `RayShadows.cpp` and `RayShadows.h` (replaced), `Scene.cpp` (2525-2943), `VulkanResources.cpp` (990-1330), `Mesh.cpp`, `Renderer3D.cpp` (5563-5694), `skin_positions.rvshader`, `AssetManager.cpp` (multi-section meshes).
- **Scope.** Rewrite. **Depends on.** RT2-18, RT2-34. **Risk.** High.
- **Exit gate.**
  - `bench_scale.py`: shadow-phase CPU and TLAS GPU time at 1k, 20k, 60k and 120k, before and after, still and moving.
  - BLAS megabytes before and after compaction.
  - Diff 0 on the demo scenes.
  - The car's reflection trace with 150 instances against 1.
  - A skinned test scene traced for 60 s, with and without the rebuild policy.
  - The worst frame while 200 new meshes spawn.
- **Size.** 8-10 sessions, in named steps: the TLAS written on the GPU with its policy; the BLAS manager; multi-section meshes and one entity per placed model; the hit table; skinning.
- **Closes.** GPU-03, GEO-04, GEO-05, GEO-06 (one entity per placed model included), GEO-13, GEO-16, rhi-s3, direct-13, the TLAS half of frame-04, the TLAS and bake half of CORE-06.

### RT2-36 · A job system, frame pipelining and parallel recording

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

- **What is wrong now.**
  - There is no LOD chain and no mesh simplifier, so every instance is drawn and traced at full detail at any distance.
  - Terrain rays always trace the finest level. That forces a tight raster LOD limit (0.15 ms per pass), and all four levels of every chunk are built up front: about 1.3 GB of vertex and index data at the maximum terrain size (inferred).
  - There are no far-field stand-ins.
  - Cutouts are resolved by a shader test on every candidate ray hit, reading mip 0.
- **What changes.**
  - LOD chains cooked at import.
  - Raster LOD chosen per instance on the GPU by screen size, with hysteresis.
  - Ray LOD kept stable for the whole world and chosen once per frame for every view; each TLAS entry points at its LOD's BLAS.
  - Far-field stand-ins get their own mask bit, for distant shadow rays.
  - Terrain rays go through each chunk's chosen level, or a coarse ray mesh, with a shadow offset for the LOD error.
  - Opacity micromaps baked for cutout meshes, in the 4-state form that keeps "unknown" exact. Any micromap SDK stays inside private implementation files.
  - The alpha test reads the ray-cone mip, with the floor a cutout edge needs.
- **Owner sees.** Denser assets become affordable. Possibly less sub-pixel flicker, where the owner's look rules allow fading (ISS-4).
- **Touches.** `MeshCook`, `Terrain.cpp`, `Terrain.h`, `AssetManager.cpp`, `cull_lit.rvshader`, `ray_shadow_trace.glsl`, the RT world module.
- **Scope.** Rewrite. **Depends on.** RT2-35, RT2-37. **Risk.** High.
- **Exit gate.**
  - Per-pixel diffs of the raster-against-ray mismatch, against full-detail rays, on the bridge and the garage.
  - The foliage scene from RT2-2c: trace time A,B,B,A with opacity micromaps, and a shadow diff of zero where the map is exact.
  - Memory before and after.
  - The bridge's cable band.
- **Size.** 8-10 sessions.
- **Closes.** GEO-07, GEO-11, ISS-27, the micromap part of ISS-14, the direction for ISS-4, the shadow-offset part of ISS-38.

### RT2-39 · Scattered instances and foliage

- **What is wrong now.**
  - The only way to place many copies of a mesh is one entity each, so a forest, a field of rocks or scattered debris is one entity per object. The ECS caps a world at 1,048,575 entities, so a million-instance scattering cannot exist.
  - Wind-animated vegetation has no path into the ray-tracing structure: its moving vertices would need BLAS refits, and nothing handles that for thousands of plants.
  - No TLAS test goes beyond 120,000 instances.
- **What changes.**
  - Instance sets: one entity holds a compact list of placements (position, rotation, scale, a variation seed) for one mesh. The GPU scene (RT2-34) culls and draws them, and the ray-tracing world module (RT2-35) enters them into the TLAS.
  - Wind as one vertex function, evaluated the same way for raster and for rays: a compute pass animates a few prototype BLASes per species and phase, refit on a budget, instead of one deforming BLAS per plant. Distant plants use a still stand-in (RT2-38's far field).
  - Alpha-tested leaves use RT2-38's opacity micromaps.
  - A TLAS scaling test to one million instances.
- **Owner sees.** Dense vegetation and scattered detail become possible, lit and shadowed by rays.
- **Touches.** A new instance-set component, `Scene.cpp` (extraction), the GPU scene, the ray-tracing world module, a wind compute pass, the foliage scene from RT2-2c.
- **Scope.** New capability. **Depends on.** RT2-34, RT2-35, RT2-38. **Risk.** Medium.
- **Exit gate.**
  - The foliage scene at 100,000 and 1,000,000 instances: CPU frame time flat in instance count (within the palindrome spread); TLAS build time recorded.
  - Wind: the traced shadow of a swaying plant matches a full-rebuild reference within grain, per pixel.
  - VRAM by category.
- **Size.** 4-6 sessions.
- **Closes.** The vegetation gap the critic found (no finding id).

### RT2-40 · Streaming and residency

- **What is wrong now.**
  - Assets load synchronously on first use, from inside render code in the middle of a frame.
  - On a warm cache, boot's preparation phase only reads files and throws the bytes away.
  - There are no load states, placeholders, priorities, eviction or partial (mip or LOD) loads, and the asset registry is not safe to use from several threads.
  - A game gets one start scene per process: scenes are single YAML files parsed whole, and there is no runtime level loading.
  - Textures are fully resident with every mip, compressed at BC1 quality at best, with no BC7 or BC6H, and formats are chosen from file names.
  - The file watcher polls, and a change refreshes caches unrelated to it. (RT2-4 already stopped the full re-hash at every launch.)
- **What changes.** Four named steps, each ending in a report:
  - **41a. Requests and load states.** An asynchronous request queue with priorities and cancellation: I/O threads read, jobs decode, and uploads go through RT2-18's ring. Load states with placeholders, reference-counted handles, and eviction under the VRAM and RAM budgets. The watcher moves to OS change notifications and refreshes only the paths it reports.
  - **41b. Partial residency.** Mip streaming driven by feedback from the GPU (the level each texture was asked for, written by the composition and by ray cones), and LOD streaming for meshes. BLASes are built on arrival and evicted with their geometry.
  - **41c. Levels.** Cooked binary scene cells streamed around the camera, and a runtime API to load and unload cells and levels. A binary snapshot for play mode.
  - **41d. The texture cooker** gains BC7 and BC6H, with explicit usage tags. This step depends on nothing else here and can land any time after RT2-4; it changes pixels, so the owner judges it by diff image.
- **Owner sees.** No hitch when something new appears. Worlds larger than VRAM. Sharper compressed textures (41d).
- **Touches.** `AssetManager.cpp`, `AssetRegistry.cpp`, `AssetWatcher.cpp`, `TextureLoader.cpp`, `TextureCook`, `PakFile.cpp`, `SceneSerializer.cpp`, `EditorLayer.cpp` (play mode), `Application.cpp` (boot).
- **Scope.** Rewrite. **Depends on.** RT2-18, RT2-19, RT2-35, RT2-36, RT2-38 (41d: only RT2-4). **Risk.** High.
- **Exit gate.**
  - A scene larger than VRAM: no out-of-memory, no visible pop over a scripted fly-through (diffed against a fully resident reference after it settles), and the 99th-percentile frame time recorded.
  - Boot time before and after.
  - 41d: per-pixel diffs of the demo scenes' textures, judged by the owner, and texture VRAM before and after.
- **Size.** 12-15 sessions.
- **Closes.** CORE-04, CORE-09 (the watcher half), CORE-16, GEO-08, MAT-07, the eviction half of CORE-05, the BC6H part of ISS-37.

### RT2-41 · Large-world precision

- **What is wrong now.** World positions are 32-bit floats everywhere: in the vertex transforms, the TLAS instances, the light-cache cells and the histories. RT2-5 fixes the reflector's plane offset, and RT2-21 the ray origins and the position rebuilt from depth; nothing else plans for worlds far from the origin. The current scenes are safe (the bridge spans about 1.35 km either side of the origin), but open worlds span 10 km and more.
- **What changes.**
  - The first arm measures: a fixture placed 10 km and 30 km from the origin, checked for vertex shimmer, shadow acne, reflection reprojection and cache-cell stability, so the work is sized by what actually breaks.
  - Camera-relative rendering: the view and every world matrix are formed relative to the camera on the CPU in double precision, so shaders only see small numbers.
  - The TLAS instances and the cache cells are expressed relative to a floating origin that moves in steps with the camera; histories and caches are shifted, not dropped, when it moves.
- **Owner sees.** Nothing in the current scenes. Large worlds render without shimmer.
- **Touches.** `Renderer3D.cpp` (view and instance matrices), the GPU scene, the ray-tracing world module, the light cache, a fixture scene.
- **Scope.** Refactor. **Depends on.** RT2-21, RT2-25, RT2-34, RT2-35. **Risk.** Medium.
- **Exit gate.** The fixture at 10 km and 30 km: per-pixel diffs against the same content at the origin show no structure beyond grain; the demo scenes diff 0.
- **Size.** 2-3 sessions.
- **Closes.** The large-world half of direct-11; the precision gap the critic found.

### RT2-42 · Core fixes: physics limits, skinning lookups, entity handles, script access

- **What is wrong now.**
  - Jolt's body limit is a compile-time 8,192, and a build that reaches it silently stops adding bodies. Every fixed step scans all bodies for new ones.
  - Skinned parts that name a derived handle walk the whole asset registry twice per frame to find their skeleton.
  - Entity handles cap a world at 1,048,575 entities, and past that a Release build silently aliases entity 0.
  - Generic C# component access goes through text.
- **What changes.**
  - Physics limits come from project settings; spawn and destroy events replace the scan; the lock-free body interface is used.
  - The owning handle is cached, and pose buffers are reused.
  - Passing the entity cap is a fatal error in Release now; widening the handle is decided before any scene above a million entities.
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

- **What is wrong now.** Once RT2-34 to RT2-37 are proven, the old paths are dead weight:
  - `RefreshDrawList`'s per-frame rebuilds and the CPU cull-table uploads;
  - RayShadows' per-frame instance list, CPU packing and per-frame hit-table rebuild;
  - the CPU mesh copies in the runtime, and the meshlet path if it was not folded in;
  - the old transform walk, which survives only as `--validate-transforms`.
- **What changes.** All of it is deleted.
- **Owner sees.** Nothing.
- **Touches.** `Scene.cpp`, `RayShadows.*`, `GpuCull.cpp`, `Mesh.*`, `Renderer3D.cpp`.
- **Scope.** Deletion. **Depends on.** RT2-34 to RT2-37. **Risk.** Low.
- **Exit gate.** Demo scenes pixel-identical; no regression on the scale scenes; scenetest green.
- **Size.** 2 sessions.
- **Closes.** The final parts of GEO-01, GEO-02, GEO-04, GEO-14, CORE-02, CORE-07 and ISS-26.

## 5. Milestones and decisions

No milestone is a big bang. A new path lands behind a global setting or a measurement flag, is proven against the old path by its exit gate, and the old path is then deleted in a named step. The engine works, and scenetest is green, at the end of every item.

| Milestone | Items | Sessions | The engine at the end | Flags it brings in | What it deletes |
|---|---|---|---|---|---|
| M0 Trustworthy ground | RT2-0 to RT2-3 | 16-23 | RT-24 fixed and committed; failures loud; VRAM counted; validation clean; one harness that diffs every test camera; a reference renderer; a ray-traced scale benchmark; many-light, GI and foliage test scenes | `--ray-counters`, `--reference-render`, `--frame-cap` | The arms already decided dead |
| M1 Correct inputs and early wins | RT2-4 to RT2-9 | 15-20 | Ids exact and stable; samples move in every AA mode; probes shadowed; no races between frames; the bridge water regression fixed; shader and pipeline caches; small CPU wins | AA arms with rays on (measurement), `--shader-cache` | TAA's dead alpha path; the second history counter; RT-22's place-based flicker rules |
| M2 One light model | RT2-10 to RT2-13 | 17-22 | One light library; tubes shaded as areas at hits; every emitter aimed and counted once; light selection at a fixed cost | `--light-candidates`, `--area-light-eval` | S4, WR-17 and the light loop in the RT lit variant; the 16-slot emitter list; the bridge-shaped grid |
| M3 A real frame | RT2-14 to RT2-20 | 42-52 | The compute question answered; a pixel-identical frame on a graph that schedules; views own histories; signals as data; a bindless RHI; uploads off the frame path; a memory budget that degrades instead of crashing; compute ray passes where they win | `--render-graph=v2`, `--trace-passes=compute` | The old graph executor; per-pass descriptor sets; blocking submits on the frame path; FrameDesc's named history fields; the renderer's process-wide statics; record-time layout tracking |
| M4 Signals rebuilt | RT2-21 to RT2-30 | 58-73 | A designed G-buffer; one BSDF; one composition pass; reflection denoiser v2; direct light by visibility ratio; bounce light in two layers (the bake, plus a live pass for Realtime lights); probes captured through the traced world; roughness tiers; a final temporal pass that only anti-aliases; render scale with upscaling | `--composition=compute`, `--reflection-denoiser=v2`, `--realtime-bounce`, `--render-scale` | The RT-mode forward lit raster; the old accumulator rules; the runtime field; the gi_denoise chain; TAA's surface-specific inputs |
| M5 Glass and water | RT2-31 to RT2-33 | 19-24 | Glass and water in the ray-traced world; one transmissive layer; water as a material with the accepted sea work; fog in the composition; TAA no longer forced | `--transmissive-layer` | The second glass reflection chain; the water's private passes, target and flags |
| M6 Scale | RT2-34 to RT2-43 | 64-81 | A GPU-resident scene; the ray-tracing world as a module; a job system; GPU-driven draws; LOD; instance sets for foliage; streaming; large-world precision | `--gpu-scene`, `--job-threads`, `--lod-bias` | The per-frame draw-list, instance and TLAS rebuilds; CPU mesh copies; the dormant meshlet path |

Total: about 228 to 293 working sessions. A session is about one solo working day (RT-SERIES's unit). Every item longer than a few sessions names its steps, and each step ends with a report and waits for the owner's green signal.

**The owner's decisions, answered on 2026-09-24.** Each answer is written into the items it touches.

| Id | Question | The owner's answer | What it changes |
|---|---|---|---|
| D0 | Scope of this series | RT-series 1 closes once RT-24 is committed; RT-22's remaining parts and RT-2.2 move here; this series also counts as the speed pass planned for after the RT series | Speed work happens inside this series, measured at every step; there is no separate pass afterwards |
| D1 | OpenGL's future | Freeze it | OpenGL keeps running the raster path as today, with a shader set that stops changing; the core RHI becomes Vulkan-shaped and new features are Vulkan-only (RT2-15, RT2-18, RT2-36) |
| D2 | Deferred composition in RT mode (reopened RT-FIRST §4, answer 1) | Allow step 2, RT mode only | RT-2.2 first, then one compute composition replaces the second scene draw in RT mode, kept only if it measures faster with a near-identical picture; materials are not rewritten and the raster mode stays forward+ (RT2-22) |
| D3 | TAA and MSAA once rays no longer need TAA | Lift the force; TAA stays the default | After the glass-and-water work, TAA becomes a choice like any other AA mode; with rays it stays the default, and MSAA 4x becomes selectable at full resolution (RT2-33) |
| D4 | How bounce light works | Two layers, the owner's design: a baked pass, with a realtime pass on top that also adds bounce light from Realtime lights | Baked lights keep the bake exactly as today ("baked means baked" stands for them); Realtime lights get live bounce light, in a pass that runs only while one is near the camera; the editor flags a stale bake when a Static object changes (RT2-25, RT2-26) |
| D5 | Upscaler and denoiser vendors | The engine's own upscaler, with DLSS and FSR optional | Own TAAU by default; DLSS SR and FSR behind the same setting, their SDKs inside private files; DLSS Ray Reconstruction only as a measured arm (RT2-29) |
| D6 | Shader and pipeline caches | Caches on | The SPIR-V cache keyed on the compiler version and build options, plus a persistent pipeline cache; every measurement runs with `--shader-cache=off` (RT2-9) |
| D7 | A glowing object with no light of its own | Yes, judged first | Every glowing surface without a light becomes a light source, shown as its own arm before it becomes the default; a surface a light owns stays counted once (RT2-11) |
| D8 | Frame-rate target | 60 FPS at native 2560x1600 | No upscaling is counted on; the budget in section 6 is re-proposed at native, where the garage must get about 3.6 times faster; render scale stays a fallback setting (RT2-29) |
| D9 | Committing RT-24 while fix 1 leans on TAA | Commit it, with the reliance written down | If arm H cannot remove the reliance, RT-24 is committed anyway and the reliance is removed by RT2-23 and RT2-28 (RT2-0) |
| D10 | The sea work accepted on 2026-09-20 | Wait for the water rework | The early re-land item is dropped; the sea work returns in RT2-32 as the starting point, checked with a moving camera and judged live |
| D11 | The glass panes behind the nearest | A named exception | Their in-draw reflection rays stay on the old path (the owner's RT-13 decision) and are the one accepted TAA reliance (RT2-13, RT2-31) |

## 6. Performance budget

**Baseline** (measured this week at 1600x900, 1.44 megapixels, rays on, the project's settings; section 1 has the table).

**Target** (the owner's choice, D8): 60 FPS at native 2560x1600 (4.10 megapixels), with no upscaling counted on. That leaves a GPU working budget of about 14.5 ms, with about 2 ms kept free: this laptop drifts about 1 ms between back-to-back runs, and the camp rose from 7.19 to 7.96 ms over five minutes on the same build.

**Every per-pass target below is a proposal, not a measured basis.** Nothing in RageV has yet measured, for example, how much of the garage is glossy per tile, which is what the reflection target depends on. So each item's first arm measures, and the owner sets that item's bar from the measurement. The targets show the shape of the budget, and where the time has to come from.

**Scaling assumption.** Native 2560x1600 has 2.84 times the benchmark's pixels, and RT-14 measured the traced passes as 90-99% linear in pixel count. So "today, at native" below is the measured number × 2.84. That column is inferred, not measured. At native the garage has to get about 3.6 times faster overall, and its reflections about 7 times. If measurement shows that cannot be reached at full quality, the fallbacks are the render-scale setting (RT2-29) and per-signal tiers, and the owner sees each trade.

**Garage** (GPU ms):

| Pass group | Measured, 1600x900 | Today at native (inferred) | Proposed target at native | Items that buy it |
|---|---|---|---|---|
| Reflections, opaque (trace 3.82, resolve 3.91, accumulate 0.94, three blurs 1.97) | 10.64 | 30.2 | 4.0 | RT2-23, RT2-27, RT2-20 |
| Direct light (DirectTrace 3.18, accumulate 0.43) | 3.61 | 10.3 | 3.2 | RT2-12, RT2-20, RT2-24 |
| Opaque lit pass, becoming the composition | 1.29 | 3.7 | 1.0 | RT2-22 |
| Glass layer (eight passes) | 0.83 | 2.4 | 0.5 | RT2-31 |
| TAA (native, no upscale) | 0.40 | 1.1 | 0.8 | RT2-28 |
| Transparent | 0.39 | 1.1 | 0.4 | RT2-31, RT2-33 |
| G-buffer (plus classification) | 0.26 | 0.7 | 1.1 | RT2-21 (more lanes), RT2-20 |
| AO | 0.21 | 0.6 | 0.4 | RT2-20 |
| Bounce light, baked layer | 0 (read inside the lit pass) | 0 | 0.3 | RT2-26 |
| Bounce light, realtime layer (7 Realtime lights; runs only while one is near) | 0 (off in baked scenes today) | 0 | 1.0 | RT2-25 |
| Ray-world upkeep and light-structure build (visible time) | not split out | — | 0.5 | RT2-12, RT2-35 |
| Post, UI, change maps, budget chain, the rest | ~0.6 | ~1.7 | 1.2 | RT2-15 (skipping idle passes), RT2-33 |
| **Total** | **18.25** | **~52** | **14.4 with the realtime layer running / 13.4 without** | |

**Bridge, Headland** (GPU ms):

| Pass group | Measured | Today at native (inferred) | Proposed target | Items |
|---|---|---|---|---|
| The sea (Transparent 5.58, DirectWaterShade 1.09, DirectWaterChoose 0.58, WaterSurface 0.75, WaterFoam 0.22, WaterAccumulateLamps 0.17) | 8.39 | 23.8 | 4.0 | RT2-31, RT2-32 (including skipping opaque work under the sea) |
| Opaque lit pass, becoming the composition | 2.28 | 6.5 | 1.2 | RT2-22, RT2-32 |
| Reflections, opaque | ~2.95 | 8.4 | 2.0 | RT2-23, RT2-27 |
| Direct light | 1.14 | 3.2 | 2.0 | RT2-12 (67-118 lights a pixel today) |
| G-buffer | 0.74 | 2.1 | 1.3 | RT2-21, RT2-38 |
| TAA (native) | 0.36 | 1.0 | 0.8 | RT2-28 |
| Bounce light, baked layer | 0 | 0 | 0.3 | RT2-26 |
| Bounce light, realtime layer (14 Realtime lights) | 0 | 0 | 1.0 | RT2-25 |
| AO, fog, glow, post, the rest | ~1.26 | ~3.6 | 1.4 | RT2-33 |
| Ray-world upkeep and light structure | — | — | 0.5 | RT2-12, RT2-35 |
| **Total** | **17.14** | **~49** | **14.5 with the realtime layer / 13.5 without** | |

**Camp.** 7.60 ms measured, about 21.6 ms at native (inferred). Its four lights are all Realtime and it has no bake, so its bounce light is entirely the realtime layer: that goes from about 5 ms at native (trace 1.22, record 0.22, re-light 0.20 and accumulate at 1600x900) to a proposed 1.5, and its reflections from about 7 ms at native to a proposed 1.5. The same items bring it under the budget.

**CPU** (the scale scene, 1280x720; the resolution does not change these):

| Case | Today (measured) | Target | Items |
|---|---|---|---|
| 60,000 objects, rays on | 40.76 ms frame (one run; RT2-2c re-measures it as a palindrome), limited by the CPU: shadow phase 19.98 ms (what inside it costs the time is split by RT2-2f; the ray-tracing work is inferred to dominate), G-buffer recording 11.53 ms, 4.69 ms unaccounted; GPU 7.54 ms | Main thread at most 8 ms; limited by the GPU | RT2-4 (the TLAS skipped when nothing changed), RT2-35 (the ray-tracing share of the shadow phase toward 1 ms; the per-frame hit table), RT2-34 (the per-object instance fill, the draw-list share and extraction by change lists), RT2-43 (the old rebuild paths deleted), RT2-36 (the rest spread across cores) |
| 60,000 objects, rays off | 13.85 ms (G-buffer recording 7.54, shadow phase 3.07); GPU 3.90 | At most 5 ms | RT2-34, RT2-36, RT2-43 |
| Realistic-content scenes at 60k and 120k (from RT2-2) | Not yet measured | Limited by the GPU | Same |

**VRAM.** Histories come to about 440 bytes per pixel by format arithmetic: about 1.8 GB at native 2560x1600. Target: at most 150 bytes per pixel (about 0.6 GB at native), bought by RT2-19's lane budgets and by histories kept only where glass is (RT2-31). Short-lived targets are aliased (RT2-15), BLASes compacted (RT2-35), and textures and geometry live in a fixed streaming pool (RT2-40). Every benchmark report carries VRAM by category from RT2-1 onward.

**Which items buy which milliseconds:**

| Item | Buys | Status of the number |
|---|---|---|
| RT2-1 | The counters' cost, with the counters off | Stale: the recorded 0.26 ms (the showroom, 2026-09-05) and 0.85 ms (Headland, 2026-09-04) predate RT-1; re-measured in RT2-1 |
| RT2-4 | CPU on frames where nothing moved (the TLAS build skipped); launch and editor save time | Unmeasured |
| RT2-12 | Direct light flat in light count; the bridge's direct light about 2 ms at native | Target; the many-light scene decides |
| RT2-15 | Batched barriers and skipped idle passes: small; less VRAM for short-lived targets | Unmeasured |
| RT2-20 | Compute ports: adopted only on a measured gain | Unmeasured |
| RT2-22 | At least 0.6 ms at Headland at 1600x900 (the material paid twice); the composition from 3.7-6.5 ms at native to about 1 ms | 0.6 measured (RT-SERIES.md:1692) |
| RT2-23 with RT2-27 | Garage reflections from about 30 ms at native to 4; Headland from 8.4 to 2 | Target |
| RT2-25 with RT2-26 | The realtime layer about 1 ms at native while Realtime lights are near, nothing otherwise; the baked layer about 0.3 ms; the camp's all-realtime bounce from about 5 ms at native to 1.5 | Target |
| RT2-29 | Not counted: render scale is a fallback under D8's native target. At 0.75 per axis, every per-pixel pass handles about 44% fewer pixels | RT-14's linearity is measured |
| RT2-31 with RT2-32 | The sea at Headland from about 24 ms at native to 4; garage glass from 2.4 to 0.5 | Target |
| RT2-34 | The per-object instance fill and extraction grow with changes, not objects: most of the G-buffer recording's 11.5 ms and the shadow phase's draw-list share (about 3 ms) at 60,000 objects | Target; the attribution is inferred until RT2-2f splits the phases |
| RT2-35 | The ray-tracing share of the shadow phase toward 1 ms at 60,000 objects, and the per-frame hit table (probably the ~4 ms rays add to the G-buffer recording) | Target; the share is measured by RT2-2f first |
| RT2-37 | Fewer commands and binds as distinct meshes grow; occlusion culling where it pays. Not credited with the G-buffer recording's 11.5 ms (the scene already goes out in about 20 indirect draws) | Unmeasured |

The budget is a target, not a promise. Every line is measured by its item's exit gate. Where a line misses, the matching global lever (the render scale, a signal's tier, the rays per pixel) trades quality for time, and the owner sees the trade.

## 7. Deliberately not doing, and why

1. **A separate RT renderer, or tracing primary visibility.** What the camera sees first stays rasterised (RT-FIRST §0): it gives the same answer for far less cost.
2. **A path-traced game mode on this laptop.** Only the reference mode (RT2-2) is built, as a truth instrument.
3. **DLSS Ray Reconstruction as the default denoiser.** It locks in one vendor. It is a single shared temporal system, which conflicts with every signal owning its accumulator. It needs uncorrelated samples, while RageV uses low-discrepancy sequences. It is allowed only as a measured A/B arm (D5).
4. **A neural radiance cache.** NVIDIA-only and experimental.
5. **ReSTIR as the base of direct light.** RT-10 measured its stages further from the truth. The fixed-cost candidate structure (RT2-12) is the base, and reuse aware of visibility, with RT-10's two missing mechanisms, is a later arm on top of it.
6. **ReSTIR GI in the game path.** Rejected twice as tangled up with the denoiser. No new reason was found to reopen it.
7. **A visibility buffer as the default.** RageV's scenes are measured as bound by pixels and rays, not geometry. It can be an arm on dense scenes after LOD (RT2-38), only if geometry becomes the wall. That is the end state MAT-01 proposes, which is not scheduled.
8. **Cluster acceleration structures (RTX Mega Geometry).** NVIDIA-only, and they need a cluster LOD chain first. An arm only after RT2-38, with a portable fallback kept pixel-identical.
9. **Displaced micro-meshes and work graphs.** The first is deprecated by NVIDIA; the second is not available on NVIDIA's Vulkan.
10. **Keeping every emitter shell out of shadow rays by instance mask** (GPU-11's proposed test). Already measured wrong: the back wall came out seven display levels too bright.
11. **Any rule keyed to a surface type.** The water exemption was rejected. RT2-28's behaviour rule replaces the rules keyed on location.
12. **Packing the G-buffer on theory.** RT-14 measured that the G-buffer is not where the memory goes. Lanes change only by diff image (RT2-19, RT2-21).
13. **Hi-Z occlusion culling by default.** It was dismissed for the open bridge without a measurement. It is measured on an occluded scene in RT2-37 and kept only if it pays.
14. **Async compute, and RT pipelines with SER, by default.** Adopted pass by pass, only on a measured gain on this laptop (RT2-20).
15. **Collapsing the raster GI fallbacks (voxel GI, SSGI).** The owner's rules keep a raster source for every signal, on a par with RT (RT-FIRST §4, answers 2 and 5). Only reference arms move behind engine flags.
16. **Froxel volumetrics, night display finishers, the bloom audit, foam** (the rest of WR-5, WR-6, WR-11, WR-12, WR-14). They stay in the WR series; RT2-33 does only their prerequisites. The owner decides when the WR series resumes.
17. **HDR display output** (GPU-16). Low priority beside the RT core; it comes after this series.
18. **Hair, eyes, subsurface scattering, decals** (most of MAT-15). RT2-21 adds the shading-model id so they can come later. None is built without content that needs it.
19. **Typed C# component accessors** (CORE-17). Only if a script-heavy test scene shows a cost.
20. **Frame generation.** It raises the displayed frame rate, not the rendered one, so it is not frame time.
21. **The visual-script loader's data loss** (ISS-39). Not renderer work. Because it loses user data, a small separate task is recommended now.
22. **Rejected finding frame-14** (the target pool "reallocates whenever the camera starts or stops"). The skeptic refuted it: the measured-change passes run whether the camera moves or not, so nothing is reallocated at a start or a stop. The real, small effect is a one-off resize when a setting changes, which RT2-15's aliasing removes.
23. **Claims corrected by the skeptics, which this roadmap does not act on:**
    - the TLAS is not rebuilt from scratch every frame (it is refit);
    - the editor's game view does not build a second TLAS;
    - MSAA 4x is dormant by the project's own choice, not because rays force TAA;
    - the 16-emitter cap is not what limits the bridge, whose lenses are all one entity; its real error is the bounding rectangle's power (gi-s3, in RT2-11);
    - the 4K reflection-cost figures are straight-line extrapolations;
    - a fragment pass can also skip idle tiles, by drawing one quad per tile, so compute is adopted on a measured benefit, not assumed.
24. **Particles and decals in the ray-traced world.** Particles stay out of the ray structure: they are lit from the cache (RT2-33), and rebuilding thousands of camera-facing sprites into a BLAS every frame would cost more than smoke's reflections are worth. This is revisited if a scene needs fire that shows in mirrors. Decals come with the shading-model groundwork (RT2-21) and are not built without content that needs them.
25. **Testing on a second GPU vendor.** Only this laptop's NVIDIA GPU is available. Validation, sync validation and RT2-1's spec fixes (the swapchain usage flag, the scratch alignment) stand in for now, and every vendor-specific feature (SER, cluster acceleration structures, DLSS) keeps a portable path that is pixel-identical. A run on an AMD or Intel card is recommended whenever one is available.

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
| GPU-14 | P | RT2-21 | Integer lanes resolved by sample zero; a sample-zero shader for float lanes |
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
| frame-03 | P | RT2-7, RT2-28 | Sample service; generic mask |
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
| direct-01 | P | RT2-12 | |
| direct-02 | P | RT2-6, RT2-12 | The world grid at hits is an owner-judged arm (exact only on average) |
| direct-03 | P | RT2-11 | Glowing objects with no light become lights, judged first (D7) |
| direct-04 | P | RT2-24 | Radius from the penumbra, not from G-buffer edges |
| direct-05 | P | RT2-10 | |
| direct-06 | C | RT2-10 | |
| direct-07 | C | RT2-7 | |
| direct-08 | C | RT2-12 | |
| direct-09 | P | RT2-21 | |
| direct-10 | C | RT2-21 | |
| direct-11 | P | RT2-21, RT2-41 | Geometric normal, scale-aware offsets, camera-relative positions; the rest of large-world precision |
| direct-12 | P | RT2-12 | |
| direct-13 | P | RT2-35, RT2-17 | |
| direct-14 | C | RT2-2 | |
| direct-15 | P | RT2-13, RT2-3 | |
| direct-16 | C | RT2-12 | |
| direct-17 | C | RT2-4 | |
| direct-18 | P | RT2-6 | It moves pixels, so it is an owner-judged arm |
| direct-s1 | S | RT2-10 | |
| direct-s2 | S | RT2-0, RT2-24 | The split arm; the structural fix |
| direct-s3 | S | RT2-12 | |
| direct-s4 | S | RT2-13 | |

**GI, AO, probes and baking**

| Finding | V | Item | Note |
|---|---|---|---|
| GI-01 | P | RT2-25, RT2-26 | Two layers (D4) |
| GI-02 | C | RT2-25 | |
| GI-03 | P | RT2-26 | `check_gi.py`'s bands updated |
| GI-04 | C | RT2-6, RT2-25 | Capture after the TLAS is built; then capture through the traced world |
| GI-05 | C | RT2-25 | |
| GI-06 | P | RT2-6, RT2-12 | |
| GI-07 | P | RT2-25 | |
| GI-08 | P | RT2-11 | |
| GI-09 | C | RT2-25 | Measured with RT2-21's BSDF |
| GI-10 | C | RT2-6 | Owner decides |
| GI-11 | C | RT2-25 | |
| GI-12 | P | RT2-4, RT2-26 | The wasted solve texture; sparse streamed bricks |
| GI-13 | P | RT2-1, RT2-26, RT2-30 | |
| GI-14 | P | RT2-26 | |
| GI-15 | C | RT2-3 | |
| gi-s1 | S | RT2-11 | |
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
| REFL-07 | C | RT2-0, RT2-23 | Patch; then trading within a budget |
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
| MAT-02 | C | RT2-21 | |
| MAT-03 | C | RT2-31 | |
| MAT-04 | P | RT2-21 | |
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
| MAT-15 | C | RT2-21 | Groundwork only; section 7, item 18 |
| MAT-16 | P | RT2-6, RT2-33 | |
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
| GEO-06 | C | RT2-35 | One entity per placed model included |
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

164 verified findings are mapped: 163 to items (four of them also partly to "not doing": GPU-11, MAT-01, MAT-15 and CORE-17), and GPU-16 to "not doing". The refuted frame-14 is listed in section 7.

## 9. What changed after the critic's review

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

**Added for AAA scale.** A baseline harness that diffs every test camera in one command (RT2-2a), instance sets and wind for foliage (RT2-39), large-world precision (RT2-41), one entity per placed model (RT2-35), probes captured through the traced world (RT2-25), sparse streamed bake bricks and cells that move out of walls (RT2-26), frame pacing (RT2-36), animated emissive textures kept current (RT2-11), and reasons for not putting particles and decals in rays or testing on a second vendor (section 7).

**After the owner's answers (2026-09-24).** The twelve answers were written into their items. Three reshaped the plan. The item that would have re-landed the sea work early was dropped, because the sea work now returns with the water rework (D10); the items after it were renumbered. Bounce light was redesigned around the owner's two-layer idea (D4): RT2-25 keeps baked lights on the bake and adds a live pass for Realtime lights on top, and RT2-26 makes a stale bake visible in the editor. The target moved to native 2560x1600 (D8), so section 6 was re-proposed at native and render scale became a fallback (RT2-29). Lifting the forced TAA became the last step of M5 (D3, RT2-33).
