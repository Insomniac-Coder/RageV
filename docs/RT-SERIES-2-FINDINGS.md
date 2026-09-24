# RT-series 2: the evidence

*The audit behind `docs/RT-SERIES-2.md`, run on 2026-09-24: eight code areas, each inspected and then checked by a skeptic who re-opened every cited line; an inventory of every open issue; a fresh benchmark on this laptop; and a survey of current RT-first renderers. Every finding and issue id the roadmap uses is defined here. File and line references are to the working tree on 2026-09-24 and will drift as the code changes. Nothing here is a decision: the roadmap and the owner's answers in its section 5 are.*

## Contents

1. Findings by code area (164 verified)
2. The finding the skeptics rejected
3. Open issues (45) and the owner's standing rules
4. The benchmark (2026-09-24)
5. What current RT-first renderers do (the reference survey)

## 1. Findings by code area

Each finding gives its verdict, severity and scope, what is wrong, what it causes, whether it was measured, the evidence, the fix direction, the skeptic's note where there is one, and the roadmap item it goes to.

### GPU backend and RHI

**State of the area.** What exists today, answering the brief's questions. (An RHI is the engine's own layer over the graphics API. Vulkan is the live backend and OpenGL is kept alongside it.)

DESCRIPTORS. A descriptor set is a small GPU table saying which buffers and textures a shader reads. The RHI does not use fully bindless binding ("bindless" means one big table the shader indexes itself). It uses per-pipeline, per-set-index resource sets, and each one holds one VkDescriptorSet per frame in flight (2 by default). Every pass calls Commit() every frame, and Commit() writes the set again with vkUpdateDescriptorSets. A set that is already bound in this frame's command buffer must not be written again. That rule forces a separate set object for every place a pass runs twice in a frame. Sets come from a chain of 2,000-set pools. Bindless exists only for sampled textures: 4,096 combined image+sampler slots, one slot per (texture, sampler) pair, managed by TextureHeap with a well-built retire and recycle scheme. On the RT path, hit shading reads buffers through device addresses (buffer_reference).

PIPELINES. Shaders are compiled from GLSL with glslang at runtime and reflected with SPIRV-Cross, and layouts are built from the reflection. The SPIR-V disk cache is written but never switched on: SetCacheDirectory has no caller. There is no VkPipelineCache. The RT pass pipelines are created on first use, inside the frame. SPIR-V is not optimised and is pinned to version 1.5 for OpenGL's sake. Variants come from preprocessor defines: about 60 pipeline creation sites, and pbr_fragment.glsl alone has 25 RV_ defines and 122 #if/#elif branches.

MEMORY. VMA does the allocating and sub-allocating. There is no memory-budget extension, no residency management, no out-of-memory path, and no aliasing of transient targets. Every upload creates its own staging buffer. Per-frame data lives in separately allocated host-visible buffers that are rewritten in full every frame.

SYNCHRONISATION. A barrier is a command telling the GPU to finish or flush one piece of work before the next may touch the same memory. Barriers are implicit. Each RHI texture tracks its own layout at record time. BeginRenderPass and EndRenderPass move every attachment in and out of the attachment layout, one vkCmdPipelineBarrier2 call per image, never batched. Buffer barriers use the older sync1 call. The render graph derives no barriers, aliases nothing and reorders nothing, by design. There are 2 frames in flight, with binary semaphores and fences. The CPU waits at BeginFrame and at every ImmediateSubmit.

QUEUES. One graphics queue, one primary command buffer and one submit per frame. A dedicated transfer queue family is found and then never used. There is no async compute (a second GPU queue running compute work alongside the graphics queue).

RECORDING. Single-threaded: one VulkanCommandList per device. Mutable per-texture layout state, an unsynchronised descriptor pool chain and an unsynchronised deletion queue all block multi-threaded recording.

RAY TRACING. Only inline ray queries, all of them in full-screen fragment passes. There are no ray-tracing pipelines and no shader binding table. A BLAS (bottom-level acceleration structure) is the traversal tree for one mesh; the TLAS (top-level structure) places meshes in the world. Each mesh gets one BLAS, built lazily with FAST_TRACE through a blocking immediate submit, with no compaction. The TLAS is per frame in flight and rebuilt every frame from a CPU instance list. It is refitted (bounds updated without rebuilding the tree) while the instance count is unchanged, and fully rebuilt at least every 64 frames. Skinned BLASes are refitted per frame in the frame's command buffer.

MODERN FEATURES. Used: dynamic rendering, synchronization2 for images, descriptor indexing (textures only), buffer device address, mesh shaders (optional), NV checkpoints and device-fault reports. Not used: timeline semaphores, descriptor buffers, ray-tracing pipelines, shader execution reordering (SER), opacity micromaps, position fetch, cluster acceleration structures, cooperative vectors, fp16 arithmetic, HDR swapchain.

OPENGL. About 2.3k lines plus a SPIR-V-to-GLSL cross-compiler. It pins SPIR-V to 1.5, shapes BufferSync/TextureSync into "a short list of uses", keeps a bound-texture fork of the material path, and doubles scenetest. RT mode is already Vulkan-only.

ROBUSTNESS. Device loss is latched and the app closes, with good diagnostics (checkpoints, device fault, a GPU address registry). Release builds continue past failed Vulkan calls with null handles. The garage validates clean. The bridge still carries two known validation errors. The screenshot path copies from a swapchain image that was created without the transfer-source usage.

**What to keep.**
- Dynamic rendering (no VkRenderPass or framebuffer objects) and synchronization2 image barriers.
- Swapchain handling: render-finished semaphores per swapchain image, the in-flight fence reset only once the frame is certain, oldSwapchain handover, and IMMEDIATE preferred over MAILBOX for vsync-off with the measured reason (VulkanDevice.cpp:1251-1285).
- The deferred-deletion queue with frame-slot logic, the out-of-frame 'last submitted frame' rule and the DeviceAlive flag (VulkanCommon.h:84-132).
- Post-mortem diagnostics: NV checkpoints, VK_EXT_device_fault, the GPU address-range registry, including its forward use in VerifyInstanceReference, and the validation callback that names objects and command-buffer labels.
- The device-lost latch that reports once and stops (VulkanCommon.cpp:44-68).
- Validation setup through VK_EXT_layer_settings, with sync validation on by default and GPU-assisted validation as a switch.
- Pipeline and descriptor layouts built from SPIR-V reflection rather than written by hand.
- The one-frame-late buffer readback ring (ReadBuffer) and per-frame timestamp pools that never stall.
- TextureHeap's retire/recycle-by-frame-slot scheme with the error slot, so a wrong index is deterministically magenta. It is the right lifetime model for a full bindless heap.
- Per-frame-in-flight TLAS, refit with a periodic rebuild, instances packed on the stack and stored whole (measured 2.96 to 0.219 ms).
- Skinned-caster BLAS refit in the frame's command buffer, with the posed buffer per frame in flight.
- Capabilities queried rather than assumed (intersected MSAA limits, bindless floor, feature checks at device selection).
- VMA as the allocator.
- No third-party types in public RHI headers (NativeWindowHandle as void*).

**How it should look in an RT-first engine** (the inspector's view; the roadmap decides). Target: a Vulkan-shaped core RHI for an RT-first engine that can handle AAA-scale scenes.

1. BASELINE. Vulkan 1.3 plus required descriptor indexing (sampled images, storage images, samplers, update-after-bind), buffer device address, synchronization2, timeline semaphores, maintenance5/6, ray query and ray-tracing pipelines. Optional, behind caps and each adopted only on a measured gain in RageV: SER, opacity micromaps, position fetch, cluster acceleration structures, mesh shaders, fp16/int16 arithmetic, descriptor buffers, HDR swapchain. OpenGL is either frozen as a separate raster-only legacy backend with a frozen shader set, or dropped. The owner decides; the core stops carrying it.

2. BINDING. Fully bindless. One global heap with tables for sampled images, storage images and samplers, plus TLAS slots. Every buffer is reached by device address. A resource is a 32-bit handle freed through the frame-slot retire scheme TextureHeap already uses. Each draw or dispatch receives one push-constant block holding a pointer into a per-frame constant ring. No per-pass descriptor writes, so no rewrite-after-bind bug class.

3. MEMORY. VMA with memory budget and priority. Separate pools for static geometry, the AS pool, streamed textures (budgeted mip residency), render targets (transient ones aliased by lifetime), the per-frame upload ring and the readback ring. Per-category accounting in the HUD and in benchmarks. Allocation failure degrades quality instead of crashing.

4. UPLOADS AND STREAMING. A persistent mapped upload ring. Copy, update and clear commands. Uploads batched per frame on a transfer queue, with timeline-semaphore handoff to graphics. ImmediateSubmit only for tools and bakes.

5. QUEUES AND SYNC. Graphics, async compute and transfer queues with timeline semaphores. The render graph declares every read and write (texture and buffer, sampled and storage, stage and access) and compiles batched and split barriers, transient aliasing and queue assignment. RHI objects hold no recording-time layout state.

6. RECORDING. Passes are recorded in parallel into per-thread command pools and submitted in graph order. Hot paths use handles, not shared_ptr.

7. PIPELINES. The SPIR-V cache enabled, spirv-opt in the build, a persistent VkPipelineCache. Every permutation built at load on worker threads from a registry that states its variant count; nothing is created inside a frame. In debug, reflection checks push-constant sizes, binding types and format/sampler pairs.

8. RAY TRACING. An AS manager: BLAS builds batched in the frame command buffer under a per-frame budget, compacted, placed in the AS pool. A GPU-resident TLAS instance buffer and hit-record table with stable slots, updated by deltas (or written by compute from the GPU scene). A refit/rebuild policy per instance class. Trace passes run in compute (ray query) or in a ray-tracing pipeline with SER, whichever an A/B proves faster on this GPU. Indirect dispatch over GPU-compacted tile or ray lists, so ray budgets launch only the work they spend. Opacity micromaps for masked geometry and a cull-mask scheme for emitter shells, if measurement agrees.

9. ROBUSTNESS. A validation-clean gate on garage, bridge and camp inside the benchmark script. Keep checkpoints, device fault and the address registry. Swapchain usage and AS alignment follow the spec. Device loss reported, with an optional recovery path later.

SCOPE. Reaching this is a rewrite of the RHI's binding, synchronisation, upload and queue core, together with the render-graph contract it serves. Device creation, swapchain, diagnostics, deletion queue, VMA use and reflection can be kept and refactored. It should land in stages, each A/B-measured: (a) patch-level: the shader cache, pipeline cache and counters flag, the validation fixes, and the missing commands; (b) the upload ring and the AS manager; (c) the bindless heap and graph-derived barriers with storage-capable targets; (d) compute-first trace passes and the async queue experiments; (e) the RT-pipeline/SER experiment.

#### GPU-01 · Every upload and every static BLAS build stops the CPU until the GPU drains

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-18, RT2-35 (Upload ring and transfer queue; BLAS builds through the manager)
- **What is wrong.** There is no streaming path and no way to upload inside the frame. Every device-local buffer upload, every texture's first layout change, every mip of every layer and every static BLAS build is its own command buffer on the one graphics queue, and each time the CPU waits until everything queued ahead has finished. The command list cannot copy from a buffer into a buffer or a texture, and the transfer queue family found at start-up is never created. Loading hides this behind a loading screen that uploads in time-budgeted slices (recorded: cold open 4.9 s, warm 0.62 s). Still unmeasured: the cost of each submit, and the first frame after a load or spawn, which builds one BLAS per new mesh in the middle of recording (the ray list has no frustum test). A world streamed in the background at AAA scale cannot avoid hitches. Immediate work also runs before the frame's already-recorded commands, a bug class that has already happened once (RHICommandList.h:213-221).
- **What it causes.** Scene load time scales with thousands of CPU-GPU round trips: 1,000 cooked textures with about 12 mips each is about 13,000 blocking submits. A mesh entering the ray list for the first time stalls that frame. Streaming an AAA-sized world in the background is impossible without visible hitches. Separately, work submitted immediately runs before the frame's already-recorded work, which is an ordering-bug class that has already happened once.
- **Measured?** Not measured. No load-time or hitch number exists in the docs. The ordering bug is recorded (RHICommandList.h:213-221).
- **Already recorded?** RHIDevice.h:57-65 itself says ExecuteImmediate 'stalls, by construction' and is 'wrong on the frame path'. Mesh BLAS creation and texture creation still use it at runtime. No roadmap item covers this.
- **Fix direction.** Add a persistent, mapped upload ring (one large buffer sub-allocated per frame and recycled by the frame fence). Add CopyBuffer, CopyBufferToTexture and UpdateBuffer commands. Batch each frame's uploads, including initial layout changes, into one submission on a transfer queue that the graphics queue waits on through a timeline semaphore (a GPU counter that queues can wait on). Queue BLAS builds to a build manager that records a budgeted number per frame into the frame's command buffer (see GPU-03). Keep ImmediateSubmit for tools and bakes only. Measure load time and worst frame time when about 200 new meshes enter view, before against after.
- **Evidence:**
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:2086 - ImmediateSubmit records a one-off command buffer, submits it to the graphics queue and blocks in vkWaitForFences (2113-2115).
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:182 - Uploading to a device-local buffer creates a fresh staging buffer and calls ImmediateSubmit for the copy (195-221).
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:341 - Creating any sampled or storage texture costs an ImmediateSubmit just for its first layout change (341-356).
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:591 - StageInto gives every mip of every layer its own staging allocation and its own ImmediateSubmit.
  - RageV/src/RageV/Renderer/TextureLoader.cpp:494 - Cooked textures call UploadMip once per mip, so a 12-mip texture is 12 blocking submits plus 1 for creation.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1044 - The static BLAS constructor builds with ImmediateSubmit and waits.
  - RageV/src/RageV/Renderer/Mesh.cpp:214 - The BLAS is created lazily the first time a mesh is added to the ray list, which happens while the frame is being recorded (RayShadows.cpp:338).
  - RageV/src/RageV/Renderer/RHI/RHICommandList.h:213 - Records a real bug of this path: immediate work runs before anything recorded into the frame, so a probe's mips were built from an empty mip 0.
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:474 - Only graphics and present queues are created; no transfer queue exists to stream on.
- **Skeptic's note.** The code matches the finding. Each of these submits its own command buffer to the one graphics queue and waits (VulkanDevice.cpp:2086-2118): a device-local buffer upload (VulkanResources.cpp:182-221), the first layout change of every sampled or storage texture (341-356), every mip of every layer (StageInto 591-627, so a cooked 12-mip texture costs 13 blocking submits, TextureLoader.cpp:487-494), and every static BLAS (1033-1046). The transfer queue family is found and never created (VulkanDevice.cpp:120-148, 474-489). Three corrections. (1) 'No load-time number exists' is wrong. Loading already runs on a worker thread behind a loading screen and uploads in time-budgeted slices (AssetManager.h:332-346). Load times are recorded: cold open 17.2 s down to 4.9 s, warm 0.62 s (HANDOFF.md:11797-11808), warm boot 0.95 s (ENGINE-NOTES.md:1841-1845). What is unmeasured is the cost of each submit and the first-frame BLAS stall. (2) The command list does copy texture to texture (CopyToTextureLayer, CopyStripToTextureLayers) and builds mips inside the frame (RHICommandList.h:213-237). What it lacks is a copy from a buffer into a buffer or a texture, so CPU data cannot be uploaded inside the frame. (3) A BLAS is built when its mesh first enters the ray list, and that list has no frustum test (Scene.cpp:2846-2851). So the stall hits the first frame after a load or a spawn, not the frame a mesh 'enters view'. The proposed measurement should spawn or stream meshes rather than pan the camera. No steady-state frame issues an immediate submit today: the only renderer-side ExecuteImmediate builds the one empty TLAS at start-up (RayShadows.cpp:195). This is a load, spawn and streaming problem, so 'critical' is too strong; high for AAA-scale streaming.

#### GPU-02 · Ray tracing can only run as full-screen pixel-shader passes

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-14, RT2-20 (The A/B first (RT2-14), then the ports and tile lists)
- **What is wrong.** All ray-tracing passes (traces, accumulators, water) are full-screen fragment passes. The render graph pools only attachment targets (no storage usage), and the command list has no indirect dispatch. A compute version would have to own its output textures outside the graph, as voxel GI and the water foam already do. What compute would buy here is unmeasured. It is not helper-lane savings (a full-screen triangle has none), and it is not the 2.87 to 1.43 ms, which was register pressure already recovered. The plausible gains are on-chip shared memory for the accumulators' neighbour gathers and blurs, a GPU-sized dispatch over only the tiles the ray budget uses, and the option of async compute. One pixel-identical A/B (DirectTrace in compute against fragment, run A,B,B,A) should decide before the graph gains storage targets.
- **What it causes.** GPU time goes to the frame's heaviest passes: the traces and the accumulators that RT-24 is fighting in. Adaptive ray budgets (RT-9) still pay for idle pixels. It blocks overlapping ray work with raster work (GPU-05) and blocks sorting or binning rays for coherence.
- **Measured?** Measured nearby: moving a ray loop out of the lit fragment halved its cost (2.87 to 1.43 ms, ROADMAP.md:658/750). Fragment against compute for the trace passes has never been measured.
- **Already recorded?** Not recorded as a problem; RT-FIRST prescribes full-screen passes.
- **Fix direction.** Let the graph own storage-capable transient textures, add compute trace passes, and add DispatchIndirect. First A/B one pass: DirectTrace as a compute pass in 8x8 tiles against the fragment version, output pixel-identical, benchmarked A,B,B,A. If compute wins, classify tiles, compact a tile list on the GPU, and dispatch the budgeted passes indirectly over only the live tiles.
- **Evidence:**
  - RageV/src/RageV/Renderer/RenderGraph.h:56 - RGTargetDesc: a graph resource is a render target (colour attachments plus depth) and has no storage-image option.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:802 - Render-target textures get ColorAttachment|Sampled|TransferSrc usage and never Storage, so no compute shader can write one.
  - RageV/src/RageV/Renderer/RenderGraph.h:245 - Compute passes write no graph target: 'its output is a buffer, which the graph does not pool'.
  - RageVEditor/assets/shaders/direct_trace.rvshader:43 - `#type fragment`. So are reflection_trace:43, rtgi_trace:37, rtao_compute:31, reflection_accumulate:66 and water_trace:40.
  - RageV/src/RageV/Renderer/RHI/RHICommandList.h:127 - Only Dispatch exists; there is no indirect dispatch whose size the GPU decides.
  - docs/ROADMAP.md:658 - Measured: the bounce's ray loop cost 2.87 ms inside the lit fragment and 1.43 ms in its own pass, same rays and resolution, because a ray loop inherits its host shader's register pressure.
  - docs/RT-FIRST.md:118 - DirectTrace is prescribed as 'fullscreen, RV_TRACE_ONLY over the lit shader'.
- **Skeptic's note.** Confirmed facts: every RT pass is a full-screen-triangle fragment pass (direct_trace:43, reflection_trace:43, rtgi_trace:37, rtao_compute:31, reflection_accumulate:66, water_trace:40; triangle vertex stage at direct_trace.rvshader:37-41). Graph targets are attachment-only (RenderGraph.h:56-97; render-target textures get ColorAttachment|Sampled|TransferSrc, VulkanResources.cpp:801-802). There is no DispatchIndirect. Four overstatements. (a) 'The alternative does not exist': the RHI already has storage textures written by compute (Water.cpp:580-600 for the foam, VoxelGI.cpp:268-282). The graph just does not pool them, so a compute trace would own its outputs outside the graph, as those do. (b) Helper lanes (the extra 2x2-quad threads whose work is discarded) cost nothing here: a full-screen triangle covers every quad completely. (c) The 2.87 to 1.43 ms (ROADMAP.md:658) came from register pressure inherited from the lit shader. That gain was banked when the traces got their own passes, and it says nothing about fragment against compute. (d) A fragment pass can also skip idle tiles, for example by drawing one quad per live tile. What only compute offers is on-chip shared memory for the accumulators' neighbour gathers and blurs, a dispatch size chosen by the GPU, and async compute. The port is cheaper than it looks, because the traces already abstract the pixel coordinate over pbr_fragment.glsl (RV_TRACE_ONLY and RV_TRACE_PIXEL, direct_trace.rvshader:46-60). But the include has 16 derivative uses and 21 gl_FragCoord uses that must stay out of the traced path. Nothing is measured; the A/B the finding proposes is the right first step.

#### GPU-03 · Acceleration structures are rebuilt from a CPU list every frame and never compacted

- **Verdict:** confirmed. **Severity:** high. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-35
- **What is wrong.** The TLAS is the tree rays walk to find which placed mesh they hit. It is described again from scratch on the CPU every frame, even for a world where nothing moves. The instance list is rebuilt and repacked, and the GPU refits or rebuilds the structure. Each mesh's own tree (the BLAS) is built alone, blocking, and at full size. Compaction, a standard Vulkan step that copies a built BLAS into the smaller size the driver reports, is never done.
- **What it causes.** CPU cost grows with instance count every frame. At the measured 0.21 µs per instance, 100k instances is about 21 ms of CPU for packing alone (inferred), before the record list and the scene walk. The GPU spends a TLAS build or refit every frame on static scenes. BLAS memory is uncompacted on a 12 GB GPU. Refit-until-count-changes lets moving objects degrade traversal for up to 64 frames.
- **Measured?** Measured: 1,021 instances cost 0.219 ms of CPU packing (ROADMAP.md:673). Not measured: TLAS build GPU time, BLAS memory, and the saving compaction would give in RageV.
- **Already recorded?** Packing cost fixed at 8.12. RAY-BUDGET-DESIGN.md:1750-1770 lists compaction and rebuild frequency as 'investigate if profiling shows'. Not scheduled.
- **Fix direction.** Keep a GPU-resident instance buffer with one stable slot per ray instance, written only when an entity's transform, material or mask changes (or by a compute pass reading the GPU scene's transforms). Keep the hit-record table the same way. Build BLASes through a manager that batches builds in the frame command buffer under a per-frame budget, compacts them (query the size, copy, free the original), and draws from an AS memory pool. Choose a refit/rebuild policy per instance class. Measure TLAS ms, BLAS MB and CPU ms at 1k, 20k and 100k instances before and after.
- **Evidence:**
  - RageV/src/RageV/Renderer/RayShadows.cpp:240 - Every frame clears the instance list and the parallel hit-record list (BeginFrame into ClearInstances, 250-259).
  - RageV/src/RageV/Renderer/RayShadows.cpp:261 - Every entity is re-added every frame, copying Mesh/Material shared_ptrs and MaterialParams into a RayCaster record (296-305, 342-344).
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1233 - The CPU packs every instance into mapped memory each frame, with a static_pointer_cast per instance (1237) that costs atomic ref-count traffic.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1308 - Refit only while the instance count is unchanged, and a full rebuild at least every 64 frames, even when nothing moved.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:990 - Static BLAS flags are PREFER_FAST_TRACE only: no ALLOW_COMPACTION, and grep finds no compaction query or copy anywhere.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1034 - One BLAS per build, each with its own scratch buffer and an immediate submit; builds are never batched.
  - docs/ROADMAP.md:673 - Measured: packing 1,021 instances cost 2.96 ms of CPU before the write-combining fix and 0.219 ms after.
- **Skeptic's note.** Every claim checks out. The instance and record lists are cleared and refilled every frame (RayShadows.cpp:240-259, 261-345), and Scene.cpp:2861-2941 walks every mesh with asset lookups each frame. The CPU repacks every instance, with one static_pointer_cast each (VulkanResources.cpp:1233-1275). The TLAS is refit only while the instance count is unchanged and rebuilt every 64 frames, even when nothing moves (1276-1313). Static BLAS flags are PREFER_FAST_TRACE with no compaction anywhere (990-992). Each static BLAS builds alone, with its own scratch buffer and an immediate submit (1033-1046). The hit-record table is also rebuilt from scratch every frame (Renderer3D.cpp:5564-5694, 144 bytes a row, material records re-derived). RAY-BUDGET-DESIGN.md:1755-1770 only says 'investigate if profiling shows'. The 21 ms at 100k instances extrapolates 0.219 ms over 1,021 instances linearly. That is an upper-bound guess and is correctly labelled inferred. For measuring: in RT mode, the existing 'shadow maps' profiler phase brackets exactly this work (RuntimeLayer.cpp:496-499 calls Scene::RenderShadows, whose traced branch ends at Scene.cpp:2941-2942). That phase records a CPU time and a GPU timestamp span (FrameProfiler.cpp:345-356). So the TLAS build or refit GPU time and the per-frame list CPU time are already in every benchmark record; nobody has quoted them yet.

#### GPU-04 · Binding model: per-pass descriptor sets rewritten every frame, bindless only for 4,096 textures

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** architecture. **Scope:** rewrite.
- **Roadmap:** RT2-18
- **What is wrong.** Pass inputs are bound through descriptor sets (the tables telling a shader what to read) owned per pipeline and per frame in flight. They must be fully rewritten every frame, and a set may not be rewritten after it is bound in the same command buffer, so every pass that runs twice needs a second set. This has caused at least four recorded bugs. The check that names the bug runs only with validation on, so in Release it is silent undefined behaviour, a suspected cause of unexplained bridge pixel drift. Only sampled textures are bindless: 4,096 texture+sampler slots, an engine constant well under the device limit. Storage images and the TLAS are not. The CPU cost is unmeasured. Traced hits already reach every material texture and all mesh data (heap plus device addresses), so the remaining cost is correctness risk and friction for GPU-driven and multi-threaded work, not an RT blocker.
- **What it causes.** A recurring correctness bug class: at least five recorded incidents, one of them 576 validation reports. There is CPU cost per pass per frame. Past 4,096 texture/sampler pairs an AAA scene draws magenta. The model also blocks parallel recording (GPU-06) and GPU-driven passes that must reach arbitrary resources.
- **Measured?** The CPU cost of Commit is not measured. The bug incidents are recorded in HANDOFF and RT-SERIES.
- **Already recorded?** The traps are recorded several times in HANDOFF. ROADMAP 8.2 (2026-08-16) made bindless textures-only and forked at the shader and material level to keep OpenGL.
- **Fix direction.** One global bindless heap with separate tables for sampled images, storage images and samplers, plus TLASes. All buffers reached by device address. Per-pass parameters go in a push-constant block that points into a per-frame constant ring, so nothing is written per pass and no set is ever rewritten after binding. Resources become 32-bit handles freed with TextureHeap's retire-per-frame scheme, generalised. Use update-after-bind at the device's real limit. Descriptor buffers are optional later, only if they measure better.
- **Evidence:**
  - RageV/src/Platform/Vulkan/VulkanPipeline.cpp:428 - Each resource set allocates one VkDescriptorSet per frame in flight, per pipeline and set index.
  - RageV/src/Platform/Vulkan/VulkanPipeline.cpp:584 - Commit rewrites the set with vkUpdateDescriptorSets, allocating new vectors each call. The tripwire at 659-673 exists because a set bound in this command buffer must not be rewritten.
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:598 - Descriptor indexing is enabled only for sampled images.
  - RageV/src/Platform/Vulkan/VulkanDevice.h:253 - kBindlessCapacity = 4096: an engine constant, not the device limit.
  - RageV/src/Platform/Vulkan/VulkanPipeline.cpp:731 - The heap rejects buffers, storage images and acceleration structures (731-774).
  - RageV/src/RageV/Renderer/TextureHeap.cpp:54 - Slots are keyed by the (texture, sampler) pair, so one texture under two samplers takes two slots. A full heap reads magenta (68-79).
  - docs/HANDOFF.md:811 - 'one descriptor set per blur stride (the rewrite-after-bind tripwire: 576 reports -> 0)'.
  - docs/HANDOFF.md:4735 - The commit-once trap (other frame slots never written) cost a water attempt. The same trap is recorded again at 8836 and 12015, and materials at 8502.
  - docs/RT-SERIES.md:2259 - GlassDirectTrace needed 'an input set of its own so the opaque trace's recorded bind is never rewritten'.
- **Skeptic's note.** Confirmed facts: sets belong to one pipeline and one set index, with one VkDescriptorSet per frame in flight (VulkanPipeline.cpp:428-450). Commit writes only the current frame's slot, so callers must set and commit every frame, and it allocates two vectors per call (584-691). The rule against rewriting a bound set forces extra set objects (HANDOFF.md:811: 576 reports down to 0; RT-SERIES.md:2258-2260). Descriptor indexing is enabled for sampled images only (VulkanDevice.cpp:590-609). The heap rejects buffers, storage images and acceleration structures (VulkanPipeline.cpp:731-774). Slots are keyed per (texture, sampler) pair, and a full heap reads magenta (TextureHeap.cpp:50-93). The bug class is real (HANDOFF.md:811, 4735-4745, 8502-8505; 8836 and 12015 are the same particle trap written up twice). The finding misses one thing: the tripwire and the bind tracking run only under validation (VulkanDevice.cpp:1657-1669). In Release the bug is silent undefined behaviour, and HANDOFF.md:813-816 names it as a possible cause of unexplained bridge pixel drift. Three overstatements. Traced-hit shading already reaches every material texture through the heap and every mesh buffer by device address (pbr_fragment.glsl:730-735, 2763-2791; Renderer3D.cpp:5585-5600), so the RT path is not blocked from reaching arbitrary resources. The 4,096 cap is an engine constant clamped to the device limit (VulkanDevice.h:253, VulkanDevice.cpp:795), a one-line raise on this GPU. The CPU cost of the rewrites is unmeasured. The textures-only heap was the owner's 8.2 decision to keep OpenGL.

#### GPU-05 · One graphics queue, one command buffer, one submit per frame

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-18, RT2-20 (Queues and timeline semaphores; the async experiment)
- **What is wrong.** All GPU work goes through one graphics queue, as one command buffer and one submit per frame, synchronised with binary semaphores and fences. The transfer queue family found at start-up is never created, and no compute queue is looked for. So nothing can overlap across queues and nothing can stream in the background. In RT mode there is no shadow-map raster; the candidate overlaps are the TLAS build and skinning against the depth and G-buffer raster, and the accumulators and filters against transparent, water and post work. Any gain is unmeasured and may be negative on this laptop GPU.
- **What it causes.** Idle gaps inside and between passes cannot be filled. Traces that only need the G-buffer cannot overlap shadow or transparent raster. There is no background transfer for streaming (GPU-01).
- **Measured?** Not measured. Async compute gains depend on the GPU and can be negative, so this must be A/B-measured on the RTX 5070 Ti Laptop before adoption.
- **Already recorded?** Not recorded; no doc mentions async compute or a transfer queue.
- **Fix direction.** Add a queue abstraction (graphics, compute, transfer) synchronised with timeline semaphores. The render graph assigns passes to queues and inserts the cross-queue waits. First experiment: TLAS build plus one trace or accumulator on async compute, overlapping G-buffer or shadow raster, pixel-identical, benchmarked A,B,B,A.
- **Evidence:**
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:120 - FindQueueFamilies looks for graphics, present and transfer families only; a compute-only family is never considered.
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:474 - Queues are created for graphics and present only. The transfer family found at 135-146 is never used (dead code).
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:1618 - One vkQueueSubmit per frame with a binary semaphore and a fence; no timeline semaphores anywhere (grep).
  - RageV/src/Platform/Vulkan/VulkanDevice.h:28 - FrameContext has exactly one command pool and one command buffer.
- **Skeptic's note.** Facts confirmed: only graphics and present queues are created (VulkanDevice.cpp:474-489). The transfer family is found and never used (120-148; no other use of .Transfer). Each frame slot has one command buffer (VulkanDevice.h:28-34). Each frame is one vkQueueSubmit with a binary semaphore and a fence (VulkanDevice.cpp:1612-1632). Grep finds no timeline semaphores. No doc mentions async compute. One correction to the overlap example: with ray tracing on, Scene::RenderShadows hands every casting light to rays and returns after the TLAS build (Scene.cpp:2716-2730, 2941-2942), so there is no shadow-map raster to overlap with. The realistic overlaps are the TLAS build and skinning dispatches against the depth-prepass and G-buffer raster, and the accumulate and blur passes against transparent, water and post work. The finding itself says the gains are unmeasured and can be negative on this laptop GPU. 'High' overstates an unmeasured, possibly negative optimisation that also needs GPU-01 and GPU-07 done first.

#### GPU-06 · Command recording is single-threaded, and the RHI keeps mutable state that blocks threads

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-18, RT2-36 (Record-time state removed; recording timed before being parallelised)
- **What is wrong.** Command recording is single-threaded, and the RHI keeps recording-time state (each texture's current layout, the descriptor pool chain, the deletion queue) that would have to go before recording could be split across threads. Whether that matters is unmeasured. At 60,000 synthetic objects the whole frame is about 12.8 ms and CPU-bound, but the recording share was never separated from the renderer's own per-object walk, and the demo scenes are GPU-bound. Time recording alone at scale, with RT on, before designing threaded recording.
- **What it causes.** At large object counts the frame is CPU-bound on one core (measured). The RT path adds TLAS packing and hit-record building (GPU-03) to that same core.
- **Measured?** 12.8 ms of CPU lit walk and submission at 60k objects (HANDOFF.md:7117-7121). Recording itself has not been timed separately.
- **Already recorded?** The CPU-bound regime is recorded. Multi-threaded recording is not discussed anywhere.
- **Fix direction.** Per-thread command pools with one command buffer per pass or pass group, recorded in parallel and submitted in graph order. Resolve layouts and barriers when the graph compiles, not inside RHI objects at record time. Use per-thread descriptor and upload rings (trivial once GPU-04 removes per-pass descriptor writes). Refer to resources by handle rather than shared_ptr on hot paths.
- **Evidence:**
  - RageV/src/Platform/Vulkan/VulkanDevice.h:343 - One VulkanCommandList per device.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:416 - TransitionTo reads and writes the texture's tracked layout while recording, so recording order has to equal execution order.
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:901 - The descriptor pool chain grows with no synchronisation.
  - RageV/src/Platform/Vulkan/VulkanCommon.h:84 - The DeletionQueue is a plain vector per frame, not thread-safe.
  - RageV/src/Platform/Vulkan/VulkanCommon.cpp:38 - 'every Vulkan call in this engine is made from the render thread'.
  - RageV/src/Platform/Vulkan/VulkanCommandList.cpp:351 - Every bind does a std::static_pointer_cast (atomic ref-count increment and decrement), at lines 351, 362, 396, 414 and 421.
  - docs/HANDOFF.md:7117 - Measured: at 60,000 objects about 12.8 ms of CPU in the lit walk and submission, 2.9 ms in depth passes, 2.6 ms in the transform walk, 2.7 ms of GPU: 'Nothing on this curve is GPU-bound'.
- **Skeptic's note.** The RHI facts are right. There is one VulkanCommandList per device (VulkanDevice.h:342). Layouts are tracked at record time (VulkanResources.cpp:416-455). The descriptor pool chain (VulkanDevice.cpp:901-940) and the deletion queue (VulkanCommon.h:84-132) have no synchronisation. Vulkan calls are render-thread-only by design (VulkanCommon.cpp:38-41), and every bind does a shared_ptr cast (VulkanCommandList.cpp:349-421). The evidence is misquoted. HANDOFF.md:7117-7121 gives 'roughly 12.8 ms' as the whole frame at 60,000 objects (78 FPS, HANDOFF.md:7028). That frame splits into the lit pass's walk and submission (not quantified, roughly the 7 ms left over), depth passes 2.9 ms, the transform walk 2.6 ms and 2.7 ms of GPU. How much of the lit remainder is Vulkan command recording and how much is the renderer's own per-object work was never separated. The same note says the real scenes are the other regime: camp is 4.9 ms, all of it GPU (HANDOFF.md:7119-7122). The GPU-driven lit path already turns per-object draws into one indirect draw per mesh. It is on by default in code (EngineConfig.h:990); the brief and the comment at EngineConfig.h:51 both say off, and both are stale. Parallel recording stays unproven until recording is timed on its own.

#### GPU-07 · Barriers are implicit, one call per image transition and never batched; the graph derives none and aliases nothing

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-15
- **What is wrong.** Synchronisation is implicit. Opening and closing a render pass moves each attachment in and out of its layout, with one barrier call per image covering all its mips and layers, and buffer barriers use the older call with wide stage masks. The graph derives no barriers, aliases no memory and reorders nothing. It cannot yet, because compute and standalone passes write buffers and storage textures the graph never sees. Most of these barriers are real dependencies, and their idle cost is unmeasured. A fuller graph would mainly buy transient-target aliasing and the basis for multi-queue work.
- **What it causes.** The GPU synchronises at every pass boundary across 40+ active passes; an inferred 100-200 separate barrier calls a frame. Independent passes run strictly in sequence. Transient targets each hold their own VRAM (GPU-09).
- **Measured?** Not measured. Nsight Graphics GPU Trace (idle time between passes, number of wait points) should be read before deciding.
- **Already recorded?** ENGINE-NOTES.md:285-305 and RenderGraph.h:38-46 excluded this on purpose when the frame was small. Its own condition ('when the frame is big enough') is now met: 89 declared passes.
- **Fix direction.** The graph declares every read and write, textures and buffers, sampled and storage, with stage and access. Compile turns that into one batched barrier per pass boundary, split barriers where producer and consumer are far apart, transient aliasing by lifetime, and later the queue assignment for GPU-05. RHI textures stop tracking their own layout. Measure idle time per pass before and after.
- **Evidence:**
  - RageV/src/Platform/Vulkan/VulkanCommandList.cpp:139 - BeginRenderPass moves each attachment and each resolve twin into the attachment layout one by one (139-141, 156, 168).
  - RageV/src/Platform/Vulkan/VulkanCommandList.cpp:297 - EndRenderPass moves every attachment back to shader-read one by one (297-323), even when the next pass writes it again.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:436 - Each transition is its own vkCmdPipelineBarrier2 call and covers all mips and layers.
  - RageV/src/Platform/Vulkan/VulkanCommandList.cpp:557 - Buffer barriers use the older vkCmdPipelineBarrier with wide stage masks; TextureSync::ShaderRead covers vertex, fragment and compute (605-626).
  - RageV/src/RageV/Renderer/RenderGraph.h:14 - Derived barriers, aliasing and reordering are 'deliberately absent', described as 'optimisations for frames far larger than this one' (14-46).
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1650 - The frame now declares 89 passes (grep of AddPass, AddComputePass and AddStandalonePass).
- **Skeptic's note.** Confirmed facts: barriers come as side effects of render-pass begin and end (VulkanCommandList.cpp:139-173, 297-323). Each is one vkCmdPipelineBarrier2 per image covering all mips and layers (VulkanResources.cpp:416-455). Buffer barriers use the older sync1 call (557-580). The graph derives no barriers and does no aliasing or reordering, by design (RenderGraph.h:38-46). The builder has 89 AddPass, AddComputePass and AddStandalonePass sites, many of them conditional. One correction: the graph does not know everything a pass touches. Compute and standalone passes write buffers and storage textures it never sees and 'own their own synchronisation' (RenderGraph.h:245-276), and histories are imported targets. So deriving barriers first requires declaring every resource; the fix direction says this, the problem statement does not. Most pass-boundary barriers are genuine read-after-write dependencies that batching or splitting would not remove. No idle-time measurement exists: no Nsight or bubble figure appears in the docs. The concrete payoffs are transient aliasing (VRAM, GPU-09) and the groundwork for async compute (GPU-05).

#### GPU-08 · The shader cache was never switched on, there is no pipeline cache, and pipelines are built mid-frame

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** performance. **Scope:** patch.
- **Roadmap:** RT2-9 (Caches on (D6))
- **What is wrong.** Every launch and every RT-setting toggle recompiles about twenty lit variants from the 6.5k-line include with glslang on the main thread, because the SPIR-V disk cache was never switched on. This is known and recorded, and the owner chose to leave SetCacheDirectory unused. There is no VkPipelineCache, spirv-opt is not built, and trace pipelines are created on first use inside a frame. The costs are unmeasured and fall on startup, toggles and one first-use hitch, not on the steady frame. Enabling the cache safely needs a key that includes the compiler version and build options, plus a way to prove a cached shader is current, because the measurement workflow relies on shaders always being rebuilt from source.
- **What it causes.** Longer startup, a freeze when toggling RT settings (inferred), and a hitch on the first frame a trace pass runs. Unoptimised SPIR-V lengthens driver compiles.
- **Measured?** Not measured. There are no startup or toggle timings in the docs.
- **Already recorded?** Not recorded. HANDOFF.md:10547 believes the cache works.
- **Fix direction.** Call SetCacheDirectory, a one-line patch, and time launch and an RT toggle before and after. Persist a VkPipelineCache. Create every pipeline at load on worker threads and never inside a frame. Build spirv-opt and A/B its GPU effect pixel-identically. In debug, check push-constant and block sizes against the reflection (see GPU-13).
- **Evidence:**
  - RageV/src/RageV/Renderer/RHI/ShaderCompiler.cpp:214 - SetCacheDirectory exists but has no caller anywhere in the tree (grep, and git log -S shows only the commit that added it), so the SPIR-V cache is dead code.
  - RageV/src/RageV/Renderer/RHI/ShaderCompiler.cpp:407 - The cache is read and written only when a directory is set.
  - docs/HANDOFF.md:10547 - The docs claim toggling RT 'recompiles the lit shaders (SPIR-V cache makes it a file read)'. That is false.
  - RageV/src/RageV/Renderer/RHI/ShaderCompiler.cpp:489 - SPIR-V optimiser disabled; spirv-opt is not built (ENABLE_OPT=OFF).
  - RageV/src/Platform/Vulkan/VulkanPipeline.cpp:418 - vkCreateGraphicsPipelines with VK_NULL_HANDLE pipeline cache, and the same for compute at 129-130.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:8032 - The DirectTrace pipeline is created on first use inside the frame; DirectWater likewise at 7924-7944.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:2039 - CompileLitShaders compiles about 20 lit variants from the 6.5k-line pbr_fragment.glsl; that file has 25 RV_ defines and 122 #if/#elif branches.
- **Skeptic's note.** The code matches. SetCacheDirectory has no caller (ShaderCompiler.cpp:214-217), so the cache is never read or written (401-421). spirv-opt is off (489-495). Pipelines are created with a null VkPipelineCache (VulkanPipeline.cpp:129-130, 418). The DirectTrace and DirectWater pipelines are created on first use inside the frame (Renderer3D.cpp:7924-7944, 8032-8046). But 'not recorded' is wrong. The dead cache is recorded at ENGINE-NOTES.md:9225-9227, HANDOFF.md:9562-9564 and HANDOFF.md:1725 and 1756. ROADMAP.md:749 (item 10.2) lists SetCacheDirectory among unused functions left alone at the owner's direction. Only HANDOFF.md:10547 is stale. The no-cache state is also relied on: 'no shader cache to be stale' is used as a guarantee when proving a shader edit ran. So enabling it is not a one-line patch. The cache key (ShaderCompiler.cpp:125-146) hashes the spliced source, the defines and the stage, but not the glslang version or the Debug/Release debug-info options (482-495). A shared cache folder or a compiler upgrade would therefore serve stale SPIR-V. NVIDIA's driver keeps its own pipeline cache between runs, so the first-use hitch is mostly a first-launch cost. None of the startup, toggle or hitch times is measured, and none of this touches a steady-state frame.

#### GPU-09 · No VRAM budget, residency, aliasing or out-of-memory handling

- **Verdict:** confirmed. **Severity:** medium. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-1, RT2-19, RT2-15 (Measuring; budget; aliasing)
- **What is wrong.** Residency means deciding which data stays in VRAM when not everything fits. The engine does not know how much VRAM it uses or has left. Nothing keeps it under a budget, transient targets are never aliased, and a failed allocation in a Release build is not handled.
- **What it causes.** On a 12 GB laptop GPU, the scene target alone is about 181 MB at 2560x1440, or about 0.9 GB with MSAA 4x and resolve twins (inferred arithmetic). Add RGBA32F history pairs, uncompacted BLASes (GPU-03) and fully resident textures. Past the budget, Windows silently pages to system memory (a large slowdown), or allocations fail and null handles crash later. Nobody can see any of this happening.
- **Measured?** Nothing measured; no VRAM figure appears in the docs.
- **Already recorded?** Not recorded.
- **Fix direction.** Enable memory budget and priority. Account per category (targets, histories, textures, geometry, AS, upload) in the HUD and in benchmark output. Alias transient graph targets (GPU-07). Stream textures by budgeted mip residency. Add allocation-failure paths that degrade (drop mips, lower internal resolution) instead of crashing. Add packed formats and measure a slimmer G-buffer by pixel diff.
- **Evidence:**
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:707 - The VMA allocator is created without the memory-budget flag, and VK_EXT_memory_budget is never enabled.
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:820 - The only memory figure the engine knows is the sum of the device-local heap sizes.
  - RageV/src/Platform/Vulkan/VulkanCommon.cpp:65 - A failed Vulkan call logs and asserts; RV_CORE_ASSERT compiles out in Release (Core.h:19-29), so execution continues with a null handle.
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:579 - The scene target carries colour, accumulation, revealage, velocity, normal (RGBA16F), indirect (RGBA16F), albedo and surface id (RG32F, line 82): about 49 bytes a pixel.
  - RageV/src/RageV/Renderer/RHI/RHITypes.h:25 - The format list has no packed formats such as R10G10B10A2, R16G16_UNORM or 8/16-bit integer, so G-buffer lanes default to fat float formats.
- **Skeptic's note.** Confirmed. VMA is created without the memory-budget flag, and VK_EXT_memory_budget is never enabled (VulkanDevice.cpp:707-723; grep finds no budget or priority use). The only memory figure is the summed size of the device-local heaps (820-824). In Release a failed call logs and carries on, because RV_CORE_ASSERT compiles out (VulkanCommon.cpp:65-67, Core.h:19-29). The format list has no packed formats and no 16-bit unorm or small integer formats (RHITypes.h:25-80). Graph targets are never aliased. The 49 bytes per pixel checks out: colour 8, accumulation 8, revealage 1, velocity 4, normal 8, indirect 8, albedo 4, surface id 8 (FrameGraphBuilder.cpp:513, 578-603, 67-84). The 0.9 GB figure applies only in MSAA mode, not under the project's default TAA. No measured VRAM figure exists anywhere, only texture arithmetic (ENGINE-NOTES.md:1328, ROADMAP.md:485). If formats are slimmed, note that the normal lane's format was a recorded trade with a planned A/B (FrameGraphBuilder.cpp:60-67).

#### GPU-10 · The RHI command list lacks the copy and indirect commands a GPU-driven RT frame needs

- **Verdict:** confirmed. **Severity:** medium. **Kind:** missing-capability. **Scope:** patch.
- **Roadmap:** RT2-18
- **What is wrong.** A GPU-driven frame lets the GPU decide how much work to launch: which tiles get rays, how many meshlets survive culling. It also needs to move data inside the frame. The RHI can do neither.
- **What it causes.** Blocks ray-budget tile compaction (GPU-02), GPU-driven culling with GPU counts, and in-frame uploads (GPU-01).
- **Measured?** n/a (a capability gap).
- **Already recorded?** Not recorded.
- **Fix direction.** Add the commands as thin wrappers: DispatchIndirect, DrawIndexedIndirectCount, DrawMeshTasksIndirect(Count), CopyBuffer, CopyBufferToTexture, UpdateBuffer, ClearTexture. OpenGL 4.6 has most of these; see GPU-12 for whether that still matters.
- **Evidence:**
  - RageV/src/RageV/Renderer/RHI/RHICommandList.h:108 - DrawIndexedIndirect takes its draw count from the CPU, and nothing reads a count from a GPU buffer.
  - RageV/src/RageV/Renderer/RHI/RHICommandList.h:127 - Dispatch only; no DispatchIndirect.
  - RageV/src/RageV/Renderer/RHI/RHICommandList.h:179 - FillBuffer is the only buffer-write command. There is no CopyBuffer, CopyBufferToTexture, UpdateBuffer, ClearTexture or DrawMeshTasksIndirect.
- **Skeptic's note.** Confirmed. DrawIndexedIndirect takes its draw count from the CPU (RHICommandList.h:108-109). Only Dispatch exists (127). FillBuffer is the only command that writes a buffer (179-180). There is no CopyBuffer, CopyBufferToTexture, UpdateBuffer, ClearTexture or DrawMeshTasksIndirect. For precision: the command list does copy texture to texture (CopyToTextureLayer, CopyStripToTextureLayers, 224-237) and builds mips inside the frame (222). The GPU cull already writes each draw's instance count from the GPU (ROADMAP.md:660, item 8.3). So the gap is a draw count or dispatch size decided by the GPU, plus uploads from buffers into buffers and textures inside the frame.

#### GPU-11 · Tracing is ray-query-only; reordering, opacity micromaps and other current RT features have never been measured

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** missing-capability. **Scope:** refactor.
- **Roadmap:** RT2-20 (Experiments, fp16 in the accumulators included; the emitter-shell mask test is not done (section 7, item 10))
- **What is wrong.** Tracing uses ray queries only. Ray-tracing pipelines, shader execution reordering, opacity micromaps, position fetch, cluster acceleration structures and fp16 arithmetic are all unused, and the 2026-08 decision against pipelines was never measured under RT-first. The expected gain is unproven, because the traced frame was measured to be limited more by lights than by rays. Keep it as a measured experiment: reflections in an RT pipeline with shader execution reordering, against ray queries in compute. Drop the emitter-shell cull-mask idea, which was already measured wrong: letting rays pass through every emitter shell made the back wall seven display levels too bright.
- **What it causes.** Incoherent reflection and GI rays on curved, shiny surfaces (chrome poles, the car) diverge across threads. Future cutout foliage pays the software candidate loop on every hit. Every lamp fitting's shell is a candidate for every ray that passes through it.
- **Measured?** None measured.
- **Already recorded?** ENGINE-NOTES 7am decided against ray-tracing pipelines; not revisited under RT-first.
- **Fix direction.** Make it an experiment first. Build a ray-tracing-pipeline version of one signal (reflections) with the same shading code, A/B it against ray queries in compute (GPU-02), then with SER. Test emitter shells excluded from shadow rays by instance cull mask against force-non-opaque. Try opacity micromaps when masked foliage arrives. Try fp16 in the accumulators, watching the known half-float rounding trap. Adopt each only on a measured, pixel-checked gain.
- **Evidence:**
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:562 - Only the acceleration-structure, ray-query and deferred-host-operations extensions are enabled.
  - docs/ENGINE-NOTES.md:5419 - 2026-08-16, before RT-first: 'pipelines are not needed for anything on the roadmap and are not planned'.
  - RageV/src/RageV/Renderer/RayShadows.cpp:285 - Every emitter instance, and every cutout, is marked force-non-opaque, so each ray crossing it enters the shader's candidate loop (VulkanResources.cpp:1263-1265).
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:533 - No shaderFloat16 or 16-bit storage features are enabled, and no shader uses fp16 types (grep).
- **Skeptic's note.** Facts confirmed: only the acceleration-structure, ray-query and deferred-host-operation extensions are enabled (VulkanDevice.cpp:557-566). ENGINE-NOTES.md:5419-5425 says pipelines 'are not planned'. Emitters and cutouts are force-non-opaque (RayShadows.cpp:283-287, VulkanResources.cpp:1260-1265). Grep finds no fp16 feature or type anywhere. Two corrections. First, the proposed test 'emitter shells excluded from shadow rays by instance cull mask' repeats an arm already measured and rejected. Skipping every emitter let a wall see tubes that the fittings between should have hidden; at sixteen rays the back wall converged seven display levels too bright. That is why only the fitting the ray is aimed at is passed through (ray_shadow_trace.glsl:90-95, 106-110). Second, the traced frame was measured to be limited more by lights than by rays (ENGINE-NOTES.md:14279-14284): the lamps' shadow rays were 20-30% of the frame and the lit loop most of the rest. So the payoff of reordering rays is unproven. It is a reasonable later experiment, low priority.

#### GPU-12 · Keeping OpenGL shapes the RHI to the lowest common denominator

- **Verdict:** confirmed. **Severity:** medium. **Kind:** tech-debt. **Scope:** refactor.
- **Roadmap:** RT2-18 (OpenGL frozen (D1))
- **What is wrong.** The RT-first path already runs only on Vulkan. Yet every RHI design question (explicit barriers, bindless for everything, extra queues, indirect commands, descriptor buffers) must still have an OpenGL meaning or an OpenGL fork. About 2.3k lines of backend plus a SPIR-V-to-GLSL cross-compiler must be kept working, and every shader edit is tested twice.
- **What it causes.** Constrains the fixes for GPU-04, GPU-05, GPU-07 and GPU-10, doubles verification time, and forks shader code.
- **Measured?** n/a.
- **Already recorded?** The owner decided to keep OpenGL at 8.2 (ROADMAP.md:683-685); this finding lays out that decision's cost for RT-series 2.
- **Fix direction.** The owner's decision. Option A: freeze OpenGL as a raster-only compatibility backend behind a narrow legacy interface, with no new features and its own frozen shader set. Option B: drop it. Either way, the core RHI becomes Vulkan-shaped and the shader target can move to SPIR-V 1.6.
- **Evidence:**
  - RageV/src/RageV/Renderer/RHI/ShaderCompiler.cpp:451 - SPIR-V is pinned to 1.5 so OpenGL can cross-compile `discard`.
  - RageV/src/RageV/Renderer/RHI/RHITypes.h:563 - Synchronisation is deliberately 'a short list of uses', because both backends must implement it.
  - RageV/src/RageV/Renderer/RHI/RHIDevice.h:246 - The bindless heap is null on OpenGL, so the renderer keeps a bound-texture fork (the RV_BINDLESS branches in pbr_fragment.glsl).
  - docs/NEXT.md:222 - Sets are allocated per pipeline because OpenGL resolves bindings against the program.
  - docs/RENDERING-REVAMP.md:115 - Every change must compile and render on both backends, and scenetest must be green on both, before a merge.
  - docs/ROADMAP.md:683 - Owner decision (2026-08): 'the RHI stays one interface, and OpenGL is not dropped'.
  - docs/RT-FIRST.md:7 - RT mode is Vulkan-only: 'OpenGL and Vulkan part ways here'.
- **Skeptic's note.** All the citations check out: ShaderCompiler.cpp:451-467, RHITypes.h:563-570, RHIDevice.h:246-248, NEXT.md:223, RENDERING-REVAMP.md:115-116, ROADMAP.md:683-685 and RT-FIRST.md:7-9. The backend is 2,741 lines including its header, plus the SPIR-V-to-GLSL path (ShaderCompiler.cpp:733). The finding could add one more OpenGL-driven constraint: scenetest enforces OpenGL's 32-sampler limit on the no-define (raster) shape of the shared lit shaders (tools/scenetest/main.cpp:7204-7249). It is correctly framed as the cost of an owner decision, not as a defect.

#### GPU-13 · Validation is not clean on the bridge, and the RHI has the reflection data to catch these bugs but does not use it

- **Verdict:** confirmed. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-1
- **What is wrong.** The demo scene still does two things the Vulkan spec calls undefined. Both are bugs the RHI could reject in debug, because it already holds each shader's reflected push-constant sizes and binding types. The screenshot path that the pixel-diff harness depends on copies from an image created without the usage that permits it. NVIDIA tolerates this; other drivers need not.
- **What it causes.** Undefined behaviour on the bridge. The measurement harness relies on a copy the spec forbids. Latent failures on other drivers.
- **Measured?** Validation results are recorded (HANDOFF.md:601, 846-856).
- **Already recorded?** The two errors are recorded with planned fixes. The RHI-level checks, the swapchain usage and the scratch alignment are not recorded.
- **Fix direction.** Fix the two errors. In debug, have the RHI check push-constant sizes, binding types, sampler/format compatibility and unwritten bindings against reflection. Add TRANSFER_SRC to the swapchain usage when the surface supports it. Honour the scratch alignment. Make the benchmark script refuse to report numbers for garage, bridge or camp while validation messages remain.
- **Evidence:**
  - docs/HANDOFF.md:601 - 'validation clean with no flag (bridge: only the two known errors)'.
  - docs/HANDOFF.md:846 - The two errors: a linear-mip sampler on an integer texture, and 112 bytes of push constants into a 96-byte block (846-856).
  - RageV/src/Platform/Vulkan/VulkanCommandList.cpp:425 - PushConstants widens the stage mask from the reflection but never checks the size against it.
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:1317 - The swapchain is created with COLOR_ATTACHMENT|TRANSFER_DST, yet the --screenshot capture copies from it (2055), which requires TRANSFER_SRC.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1029 - AS scratch buffers are not explicitly aligned to minAccelerationStructureScratchOffsetAlignment (grep finds no use of it).
  - RageV/src/Platform/Vulkan/VulkanCommon.cpp:51 - Device loss is latched and the application closes, with no recovery.
- **Skeptic's note.** Both bridge validation errors are still unfixed in the working tree. The point sampler still has no Nearest mip mode (Renderer3D.cpp:1643-1649). water_accumulate's LampParams still declares 96 bytes with no Trace member (water_accumulate.rvshader:79-90), against the 112 bytes the C++ pushes (HANDOFF.md:846-856). PushConstants widens the stage mask from reflection but never checks the size (VulkanCommandList.cpp:425-446). The swapchain is created with COLOR_ATTACHMENT|TRANSFER_DST only (VulkanDevice.cpp:1317), yet --screenshot (Application.cpp:852) copies from it with vkCmdCopyImageToBuffer (VulkanDevice.cpp:2055), which requires TRANSFER_SRC. No doc records this one. Scratch buffers come from a plain vmaCreateBuffer with no alignment request (VulkanResources.cpp:903-935); the garage validates clean with RT on, so they happen to be aligned on this GPU. All of these are latent on NVIDIA. The value is a validation-clean gate and the reflection checks.

#### GPU-14 · MSAA resolves every G-buffer lane by averaging, including surface id, velocity and normal

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-21 (Integer lanes resolved by sample zero; a sample-zero shader for float lanes)
- **What is wrong.** Under MSAA, every G-buffer lane is resolved by averaging, including the surface id, the motion vector and the normal lane. Silhouette texels therefore carry ids no object has, a blend of two motions and a blend of two normals, and the RT accumulators and identity tests read those invented values. This happens only in MSAA mode, not under the default TAA, and its visible effect is unmeasured. Vulkan does not allow a sample-zero resolve on float colour attachments, so the fix is one of: store the id in an integer format (R32_UINT) and resolve it with sample zero; resolve velocity and normals in a small shader pass that reads sample 0; or draw the G-buffer single-sampled. Judge it with a pixel diff at silhouettes in MSAA mode.
- **What it causes.** Under MSAA, identity tests and reprojection at every silhouette run on invented values; the visible effect is not measured. The default project AA is TAA (SampleProject.rvproject:7), so this is live only when MSAA is chosen, which the owner treats as a deliberate setting.
- **Measured?** Not measured.
- **Already recorded?** Partially: ENGINE-NOTES.md:4956-4963, for SSR only.
- **Fix direction.** Let each attachment choose its resolve mode in the RHI: SAMPLE_ZERO for id, velocity and normal/roughness, AVERAGE for colour. Or draw the G-buffer single-sampled. Measure a pixel diff at silhouettes in MSAA mode.
- **Evidence:**
  - RageV/src/Platform/Vulkan/VulkanCommandList.cpp:154 - Every colour attachment with a resolve twin uses VK_RESOLVE_MODE_AVERAGE_BIT, with no per-attachment choice.
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1650 - The G-buffer pass writes velocity, normal, albedo and surface id into sceneHDR's attachments.
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:453 - sceneHDR is multisampled whenever the AA mode is MSAA (or TAA with --msaa).
  - docs/ENGINE-NOTES.md:4956 - Averaging was accepted in 2026-08 for SSR's surface lane only ('a slightly wrong ray on a one-pixel rim').
- **Skeptic's note.** The problem is real. With MSAA on, every colour attachment resolves by averaging (VulkanCommandList.cpp:154-159). That includes the surface id (R32G32_SFLOAT), velocity (R16G16_SFLOAT) and the normal lane (RGBA16F) that the GBuffer pass writes (FrameGraphBuilder.cpp:1650-1664). It applies only when anti-aliasing is MSAA, or TAA with --msaa (FrameGraphBuilder.cpp:458-466). The project default is TAA with MSAA off (SampleProject.rvproject:7-8). The acceptance in ENGINE-NOTES.md:4956-4963 covered the SSR surface lane only. The fix as written is not legal Vulkan. For a colour attachment with a non-integer format, VUID-VkRenderingAttachmentInfo-imageView-06129 allows only NONE, AVERAGE or the newer CUSTOM resolve (vendored RageV/vendor/Vulkan-Headers/registry/validusage.json). SAMPLE_ZERO (keep one sample instead of averaging) is allowed only for integer formats (06130). The visible effect is unmeasured.

#### GPU-15 · Ray counters are compiled into every RT lit shader with no off switch

- **Verdict:** confirmed. **Severity:** low. **Kind:** performance. **Scope:** patch.
- **Roadmap:** RT2-1
- **What is wrong.** A measurement instrument ships in every RT frame. The owner's rule is that measurement goes behind a flag.
- **What it causes.** 0.85 ms on the Headland frame, paid by every frame; the lane count keeps growing (64 now).
- **Measured?** 0.85 ms, 1.4% on Headland (ENGINE-NOTES.md:14266).
- **Already recorded?** Measured and kept in 7cy; the owner's measurement-flag rule was not applied to it.
- **Fix direction.** Compile the counters only under a measurement flag (--ray-counters=on, implied by --benchmark); keep the readback machinery as it is.
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:544 - RV_RAY_COUNTERS is defined whenever RV_RAY_SHADOWS is: subgroup reductions and atomics in the hottest shader.
  - RageV/src/RageV/Renderer/PostProcess.cpp:204 - The TAA resolve also gets the counters whenever the device can trace.
  - RageV/src/RageV/Renderer/RayCounters.cpp:95 - A buffer fill, a barrier and a readback every frame.
  - docs/ENGINE-NOTES.md:14266 - Measured after the early-depth fix: 0.85 ms, 1.4% of the Headland frame.
- **Skeptic's note.** Confirmed. RV_RAY_COUNTERS is defined whenever RV_RAY_SHADOWS is (pbr_fragment.glsl:543-548). The TAA resolve gets the counters on any device that can trace (PostProcess.cpp:200-204). Every frame pays a fill, a barrier and a readback (RayCounters.cpp:95-138). The only readers are the profiler (FrameProfiler.cpp:559), the editor and runtime HUDs (EditorLayer.cpp:2641, RuntimeLayer.cpp:654) and the debug views, so it is measurement-only. 7cy expected the budget controller to use the counts (ENGINE-NOTES.md:14286-14288), but nothing reads them for control today. The cost was measured at 0.85 ms, 1.4%, on Headland (ENGINE-NOTES.md:14262-14266). The owner's showroom bisect separately put it at +0.26 ms, 'always on, no off switch' (C:/Users/ism19/.claude/projects/C--Users-ism19-Code/memory/project_ragev_showroom_fps_regression.md). One caution on the fix: if --benchmark turns the counters on, every benchmark times a shader that does not ship. Take the counts in a separate run.

#### GPU-16 · No HDR display output

- **Verdict:** confirmed. **Severity:** low. **Kind:** missing-capability. **Scope:** patch.
- **Roadmap:** Not doing (Section 7, item 17)
- **What is wrong.** The final image is always an 8-bit SDR swapchain, whatever the display can show.
- **What it causes.** Specular highlights such as tube lights and chrome glints are compressed into SDR on HDR displays. Low priority beside the RT core.
- **Measured?** n/a.
- **Already recorded?** Not recorded.
- **Fix direction.** Later: VK_EXT_swapchain_colorspace with an HDR10 (PQ) or scRGB swapchain, plus a tonemapper output transform per display mode; judge by eye and by pixel measurement on an HDR display.
- **Evidence:**
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:1230 - The swapchain picks B8G8R8A8_UNORM with sRGB non-linear colour space; HDR10 or scRGB is never considered.
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:264 - The instance enables only surface and win32-surface (plus debug utils); no swapchain-colorspace extension.
- **Skeptic's note.** Confirmed. The swapchain picks only B8G8R8A8_UNORM with SRGB_NONLINEAR (VulkanDevice.cpp:1230-1244). The instance enables only the surface, win32-surface and debug-utils extensions (264-271). Low is the right rating beside the RT core.

#### rhi-s1 · A failed shader variant or pipeline quietly drops its pass, outside the only guard

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** stability. **Scope:** patch.
- **Roadmap:** RT2-1
- **What is wrong.** When a shader variant fails to compile, the pass that needed it returns early or a different variant runs, and the frame still looks plausible. When a pipeline fails to build, Release keeps a pipeline object holding a null handle. The only protection is that a screenshot or benchmark run exits with code 3. The editor and the owner's live arms are not covered. The recorded incident says nothing was logged at all, even though CompileStage does log compile errors (ShaderCompiler.cpp:474), so some code path or the harness hid it.
- **What it causes.** A measurement or an arm the owner judges live can be of a picture that is missing a pass. The recorded cost is most of an evening's measurements plus a fourteen-level change blamed on the wrong switches, and a crash far from its cause. RT-series 2 will add many passes and variants, which widens the exposure.
- **Measured?** Two incidents are recorded. How often this happens, and how the RT-11 failure escaped the log, are not established.
- **Already recorded?** Yes, as open: RT-SERIES.md:205 and HANDOFF.md:274-275. No roadmap item.
- **Fix direction.** Make a missing shader or pipeline loud in every run mode. Count pipeline-creation failures together with compile failures. Have a pass whose shader or pipeline is missing draw a fixed debug colour, and show an on-screen banner in the editor and runtime HUD naming the variant. Make watch_arm refuse to show an arm when the log reports a failure. Return null from CreatePipeline when the handle is null. First, find out how the RT-11 failure escaped the log.
- **Evidence:**
  - docs/RT-SERIES.md:199 - Recorded incident (199-206): a reflections-only variant failed to compile, 'nothing was logged', the pipeline fell back, and the picture moved fourteen display levels with every RT-11 switch off. 'The engine silently swallowing a shader compile failure is still open'.
  - docs/HANDOFF.md:1361 - A shader used the reserved word `half`, failed to compile, the pipeline came back null, and the crash surfaced in Scene::RenderShadowMaps, nowhere near the edit.
  - RageV/src/RageV/Core/Entrypoint.h:77 - The only guard (70-91): exit code 3 when a --screenshot or --benchmark run had any compile failure. Interactive runs are exempt by design.
  - docs/RT-SERIES.md:73 - Owner-judged arms are live watch_arm.py runs with 'nothing captured', so they fall outside that guard.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:8028 - DirectTrace returns silently when its shader or pipeline is missing (8028-8053), and the frame renders without the pass.
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:2189 - CreatePipeline always returns an object. A failed vkCreateGraphicsPipelines (VulkanPipeline.cpp:418) only logs in Release (VulkanCommon.cpp:65-67), and BindPipeline has no null check (VulkanCommandList.cpp:349-357).

#### rhi-s2 · CPU and GPU data layouts are copied by hand, and the reflection that could check them is unused

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-1, RT2-17 (Checks; one definition)
- **What is wrong.** Every structure the CPU hands the GPU is written two or more times, once in C++ and again in one or more GLSL files, and kept in step by discipline alone. That covers the scene uniforms, the push-constant blocks, the ray-instance and material rows, and the counter layouts. The shader compiler already reflects each block's size and member offsets, but nothing compares them with the C++ side. So a drift shows up as a validation error at best, and as silently wrong or run-to-run-different data at worst.
- **What it causes.** This is a standing correctness bug class: one live instance (the bridge's push-constant mismatch) and one recorded nondeterminism on OpenGL. RT-series 2 will add and reshape many of these blocks (G-buffer lanes, hit records, light records).
- **Measured?** The incidents are recorded (ROADMAP.md:549, HANDOFF.md:846-856). Nobody has counted how many blocks are mirrored.
- **Already recorded?** The 'update both' rule is written down (RENDERING-REVAMP.md:111-113). The push-constant size check overlaps GPU-13. No doc proposes generated or checked layouts.
- **Fix direction.** Give each block one definition that both sides include (a shared header with a thin C++/GLSL macro layer), or generate the C++ side from reflection. In debug builds, compare sizeof and member offsets with the reflected block at pipeline creation and at every buffer bind and push. Start with the scene UBO and the ray-instance row, and prove frames bit-identical on the garage and the bridge.
- **Evidence:**
  - docs/RENDERING-REVAMP.md:111 - 'the scene UBO block is mirrored by hand in scene_block.glsl AND pbr_fragment.glsl -- update both, append-only' (111-113).
  - docs/RAY-BUDGET-DESIGN.md:2951 - 'Scene UBO (three mirrors: scene_block.glsl, pbr_fragment.glsl, ...'.
  - docs/ROADMAP.md:549 - Item 9.12: SceneUniforms is mirrored by hand in two GLSL files, and a field added to one of them made OpenGL render differently from run to run.
  - RageVEditor/assets/shaders/water_accumulate.rvshader:79 - LampParams is still 96 bytes while the C++ pushes 112 (HANDOFF.md:852-855): a live validation error on the bridge, from this same class of bug.
  - docs/HANDOFF.md:1365 - 'The counter stride is in four places': RayCounters::Count, and RayCounterSlot()*32u in pbr_fragment.glsl, taa_resolve and rtao_compute.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:124 - GpuRayInstance mirrors RayInstance by hand, guarded by one size and one offset static_assert (124-152).
  - RageV/src/RageV/Renderer/RHI/ShaderCompiler.cpp:516 - Reflection already records every UBO, SSBO and push-constant block size (516-576), but SetUniformBuffer and SetStorageBuffer (VulkanPipeline.cpp:478-505) and PushConstants (VulkanCommandList.cpp:425-446) never compare against it.

#### rhi-s3 · Skinned acceleration structures are refit forever and never rebuilt

- **Verdict:** added by the skeptic. **Severity:** low. **Kind:** performance. **Scope:** patch.
- **Roadmap:** RT2-35
- **What is wrong.** A skinned mesh's ray-tracing tree is built from its first posed frame and only refit after that. Its internal split stays tuned to that first pose however far the animation moves the limbs. The engine added a periodic rebuild to the TLAS for exactly this reason, but not to skinned BLASes.
- **What it causes.** Inferred: rays that hit or pass near animated characters walk a looser tree, and the cost grows with the number of animated characters and how far they move. It is small with today's one animated fox; it matters for AAA-scale crowds.
- **Measured?** Nothing measured.
- **Already recorded?** Not recorded. ENGINE-NOTES 7an chose refitting and names no rebuild policy.
- **Fix direction.** Rebuild each skinned BLAS every N frames, or when its bounds grow past a set ratio of the bounds it was built with, staggered across characters. Measure trace time on a character running for 60 s, with and without, A,B,B,A and pixel-identical.
- **Evidence:**
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1071 - A dynamic BLAS is built once, then refit in UPDATE mode every frame with PREFER_FAST_BUILD. Nothing ever counts refits or forces a rebuild (1055-1085).
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1296 - The TLAS's own comment explains that refitting forever keeps a tree shaped for where things used to be, so traversal slows, and it caps refits at 64 frames (1296-1310). The BLAS path has no such cap.
  - docs/ENGINE-NOTES.md:5692 - The 'refit, not rebuild' decision for characters (5692-5700), taken before the TLAS got its refit limit.
  - RageV/src/RageV/Renderer/RayShadows.cpp:138 - A caster's structure is recreated only when its slot's mesh changes (EnsureCaster, 128-176).

### Frame orchestration (render graph, FrameGraphBuilder, Renderer3D, PostProcess)

**State of the area.** Paths are relative to C:/Users/ism19/Code/RageV. One function describes every frame: FrameGraphBuilder::BuildFrame, 5,521 lines (FrameGraphBuilder.cpp:385-5906). It describes the frame into a RenderGraph that is only a checked, ordered list of passes with pooled targets. The graph does no scheduling, derives no barriers, shares no memory between short-lived targets (no aliasing), drops no unused passes, and has no async compute. Compute passes may not write images the graph owns, so all 84 graphics-pass call sites are fullscreen raster draws, every ray trace included. At the SampleProject settings the garage frame runs about 54 passes while the camera moves and about 67 while it is parked (counted from the code path).

Every ray-traced signal is wired in by hand: direct light, AO, reflections, glass direct light, glass reflections, the sea's lamps and mirror, and GI. The apps own the signal histories: 16 TemporalHistory objects and 4 measured-change records per view, and 32 + 8 in the editor. They hand them in through 20 named FrameDesc fields, which the runtime and each editor view fill line by line.

Renderer3D is 9,901 lines, all static. It holds about 80 pipelines and one named descriptor set per pass per view, and takes per-frame inputs through static setters. Scene extraction runs on one thread inside the G-buffer pass callback, once per view. The ray-tracing scene structure (TLAS) is rebuilt from scratch every frame, inside RenderShadows, outside the graph.

There is no internal render resolution and no upscaler. In the garage, histories plus transient targets come to roughly 2.7 GB per view at 1440p, and none of it is ever released. TAA is forced on whenever rays run, because the sub-pixel jitter only exists under TAA. Its resolve now takes 12 textures, some of them specific to one signal or one surface kind, and that resolve is where RT-16, RT-22 and RT-24 show up.

**What to keep.**
- The graph's compile-time check: a pass that samples something no earlier pass wrote is refused with a message, and a pass that reads a texture it did not declare gets null and a warning (RenderGraph.cpp:113-134, 159-198, 342-383).
- One reconstruction contract for every signal (addSignal + AccumulateSignal/BlurSignal), filtered on the signal's own grid through a guidance downsample, with one bilateral upsample at the end (RT-3.1).
- Histories scoped per view, each carrying its own camera record (TemporalHistory.h:23-36; the ENGINE-NOTES 7u lesson).
- The Resolve* functions as the one place where settings, command-line flags and device capabilities decide what runs, plus the Features report of what actually ran (FrameGraphBuilder.cpp:177-383, 940-960).
- Halton jitter indexed by frame number and --frame-time, which make captures reproducible; A/B arms that are pixel-identical when switched off.
- Measured change (the re-light anti-lag) as a per-signal change map read by the accumulators, because it measures change instead of guessing it from noise.
- Per-16x16-tile ray allocation driven by each signal's own history confidence (RT-9), with the dead band and dwell that stop counts from flickering.
- Settings split by owner: project for cost, camera profile for look, machine (ini/CLI) for local preferences.
- Graph and pass names in GPU checkpoints, plus per-pass GPU and CPU timing with cached scope names (RenderGraph.cpp:417-468).
- Automatic camera-cut detection that drops histories (FrameGraphBuilder.cpp:542-575); it should become generic rather than a hand list.
- Deferred destruction on resize, which is safe with frames in flight (VulkanResources.cpp:860-872).

**How it should look in an RT-first engine** (the inspector's view; the roadmap decides). A performant RT-first frame for AAA-scale scenes would have four layers.

1) A render world. Once per frame, jobs copy what changed in the scene (transforms, materials, lights, instances) into persistent GPU buffers. The TLAS is refit for what moved and rebuilt only when instances are added or removed. Every view reads this one snapshot: game camera, editor camera, probe faces, shadow views. Nothing walks the entity registry inside a pass, and recording is spread over threads by pass group.

2) A real frame graph. Passes declare typed access to textures, storage images, buffers and the TLAS. The graph works out the barriers and batches them. It culls passes whose results nobody reads, aliases transient memory by lifetime, and runs independent compute on an async queue. It owns persistent per-view history resources: swapped after a successful frame, dropped on a camera cut, resize or feature switch-off, and released when unused. Declaring the frame is free of side effects, and the compiled graph is cached until the frame's shape changes.

3) Frame recipes built from data: an RT recipe (the default) and a raster fallback. The RT recipe runs in this order:
- GPU culling, then one raster of the opaque scene into a complete G-buffer at internal resolution, with the material evaluated once. Glass and water are extra surface layers described in the same lane table.
- One compute (or ray-pipeline) pass chain per signal: direct light, reflections, GI and AO, each with its own resolution divisor, reconstruction (today's contract, measured change, RT-9 allocation) and history.
- A compute lighting composite instead of a second raster, then the transparent composite.
- A temporal upscale to output resolution: the engine's own TAAU first, vendor upscalers behind the same interface, optional dynamic resolution. It is fed one generic history-invalid mask built from the signals' change maps and the layers' coverage, and no surface-specific inputs.
- The sample sequence (jitter and per-signal random index) is a frame service, independent of the AA mode.
- Bloom, tonemap and UI at output resolution.
Signals and layers are table entries: adding one means one descriptor and its shaders. It never again means a FrameDesc field, app-layer members, new Renderer3D descriptor-set members and new TAA inputs.

4) Settings: a few global levers (render scale, per-signal tier, RT optimisation level) in project settings. A/B arms go in a measurement registry compiled out of shipping builds, and arms are deleted once decided.

Getting there means rewriting the orchestration layer: RenderGraph, FrameGraphBuilder, the frame and pass half of Renderer3D, and history ownership in the apps. The RHI needs a transient upload and descriptor allocator. The shader math carries over, as do the reconstruction contract, measured change, the RT-9 allocator and the post-process shaders. Each step is proven by per-pass GPU timing in A,B,B,A order and by pixel diff against the current path.

#### frame-01 · The render graph is an ordered list, not a scheduler, and it cannot express compute work that writes images

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** architecture. **Scope:** rewrite.
- **Roadmap:** RT2-15
- **What is wrong.** The render graph is an ordered list with pooled targets. It works out no barriers itself: the RHI issues one unbatched barrier per texture at every render-pass edge, which makes each pass wait for the one before. It shares no memory between short-lived targets, drops no unused passes, uses one queue, and forbids compute or standalone passes from writing graph targets. The garage frame is about 67 graphics passes whether the camera moves or not, because the measured-change re-light and filter passes exist every frame. The traced passes are fragment passes for two reasons: the graph's write rule, and the hit-shading include's implicit-level-of-detail texture reads. Moving them to compute needs both fixed. What the serialisation costs is unmeasured; the per-pass GPU timings can show the gaps between passes.
- **What it causes.** Inferred from the code. Every pass boundary is a barrier that waits for all earlier colour and shader work, so independent chains run strictly one after another: direct light, AO, reflections, glass. There is a single queue, so traces cannot overlap the G-buffer raster. No transient memory is ever shared (see frame-05). RT passes cannot use compute features such as group-shared memory or thread-group tiling for ray coherence. At the SampleProject settings the garage frame runs about 54 passes moving and about 67 parked, each with its own render-pass begin/end.
- **Measured?** Not measured. Frame size then and now, counted: 11 passes and 7 targets (HANDOFF.md:8253), against 84 graph.AddPass, 56 CreateTarget and 40 Import call sites in FrameGraphBuilder.cpp today. RENDERING-REVAMP.md:2235 ranks async overlap as 'small', with no measurement behind it. The profiler already records per-pass GPU timestamps, which could measure the idle gaps between passes.
- **Already recorded?** Recorded as a deliberate design, not a defect (ENGINE-NOTES.md:258-313; RenderGraph.h:13-46). Aliasing and reordering were deferred because the frame was small. Derived barriers were rejected because the RHI already tracked layouts. The premise, a small frame, no longer holds, and the reasoning was never re-measured.
- **Fix direction.** Rewrite the graph. Resources get types: attachments, storage images, buffers, and the ray-tracing scene structure. Each pass declares its accesses. Barriers are derived and batched once per pass with exact pipeline stages. Transient memory is aliased by lifetime, unread passes are culled, independent compute runs on an async queue, and the compiled graph is cached until the frame's shape changes. Accept it only on per-pass GPU timings (A,B,B,A) with pixel-identical output.
- **Evidence:**
  - RageV/src/RageV/Renderer/RenderGraph.h:38 - 'Deliberately absent': aliasing and reordering declared out of scope (38-46); lines 15-20 say barrier derivation was not built
  - RageV/src/RageV/Renderer/RenderGraph.cpp:37 - A compute pass may not Write a target (37-43), nor may a standalone pass (45-52); only graphics passes produce graph images
  - RageV/src/RageV/Renderer/RenderGraph.cpp:417 - Execute is a linear loop in declaration order; each graphics pass begins and ends its own render pass (551-562)
  - RageV/src/Platform/Vulkan/VulkanCommandList.cpp:297 - EndRenderPass moves every colour attachment and depth of the target back to shader-read; BeginRenderPass moves them to attachment (140). This is the only synchronisation the graph relies on
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:454 - Each layout transition is its own vkCmdPipelineBarrier2 with stage masks derived from the layout; never batched
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:695 - Only a graphics queue and a present queue are created (695-696); no compute queue exists for async work
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:4560 - The irradiance fill must be a 'standalone' pass that records its own barriers; the graph declares nothing it reads or writes (4560-4571)
  - docs/ENGINE-NOTES.md:309 - Aliasing and reordering deferred as 'optimisations for frames much larger than this one' (309-311); the frame then was 11 passes and 7 targets (HANDOFF.md:8253)
- **Skeptic's note.** The code says what the finding says. RenderGraph.h:38-46 and ENGINE-NOTES.md:258-311 rule out aliasing and reordering and explain why barriers are not derived. RenderGraph.cpp:37-52 forbids compute and standalone passes from writing a target. Execute (RenderGraph.cpp:409-568) is a loop in declaration order, one render pass per graphics pass. VulkanResources.cpp:417-455 issues one vkCmdPipelineBarrier2 per texture per layout change. VulkanDevice.cpp:695-696 creates only a graphics queue and a present queue. The irradiance fill is a standalone pass (FrameGraphBuilder.cpp:4558-4571). The counts are right: 84 graphics passes, 2 compute, 3 standalone, 56 CreateTarget, 40 Import. Three things are wrong. (1) The pass counts. RecordIsLastFrame (FrameGraphBuilder.cpp:22-43) is true on every frame whose record was written the frame before, because the record is retaken and stamped with the camera on every moving frame (Renderer3D.cpp:8205-8209, 8290-8295). The file's own header says 'moving or not' (line 29). So the 3 re-light passes and 12 change-filter passes exist whether the camera moves or not. Counted from the code, the garage runs about 67 graphics passes either way; the two RT-9 budget passes are meant to be added only while something moves (but see framegraph-s1). (2) Not every graphics pass is a fullscreen draw: GBuffer, Scene, GlassLayer, Transparent, Overlay and UI draw geometry. (3) The traces are fragment passes for a second recorded reason as well. The shared hit-shading include samples shadows with implicit level of detail (the GPU choosing the mip from screen-space derivatives), which compute shaders cannot do (HANDOFF.md:6076-6081). A new graph alone therefore does not move the traces to compute. Nobody has measured what the serialisation costs. RT-14 (RT-SERIES.md:2817-2835) measured the traced passes as 90-99% pixel-bound, so reordering or async overlap may gain little. 'Critical' has no measurement behind it.

#### frame-02 · Every signal is wired by hand through FrameDesc, three app-layer blocks and one 5,500-line function

- **Verdict:** confirmed. **Severity:** high. **Kind:** architecture. **Scope:** rewrite.
- **Roadmap:** RT2-17, RT2-16
- **What is wrong.** Each traced signal is written out by hand inside BuildFrame: its history pairs imported, its budget, trace, resolve, measured-change and accumulate passes declared, and its invalidation and ping-pong swap called. The apps own the histories and hand them in through 20 named FrameDesc pointers, which the runtime and each editor view fill line by line. The glass reflection lane is a near copy of the opaque one. The measured-change block (the anti-lag re-light) exists four times. Rules that apply to every history, such as dropping them on a camera cut, are lists kept by hand.
- **What it causes.** Adding or changing a signal is a multi-file edit, per git show --stat. RT-13's glass stages touched 12 source files (+895 lines), RT-9 touched 14, RT-22 touched 12 (+510). Things that get left out become silent defects. Water passes once ran in the garage, which has no water. Today's camera-cut list skips the sea's lamp-light, mirror and choice histories. The private accumulator's own tests may catch part of that (inferred).
- **Measured?** Passes with nothing to run on cost 0.26 ms of the showroom's 8 ms frame (RuntimeLayer.cpp:591-595, fixed 2026-09-05). Commit stats from git show --stat 302e165 (RT-13), 71622c3 (RT-9) and b30d5cd (RT-22).
- **Already recorded?** The water-pass waste is recorded as fixed. The structural cost of hand-wiring is not recorded.
- **Fix direction.** Describe signals and surface layers as data. Each signal gets one descriptor: its inputs and layer, grid divisor, history lanes and formats, reconstruction kind, consumers and debug views. Each layer (opaque G-buffer, glass, water) gets one too. A single generic builder then emits passes, histories, cut handling, budgets and change maps from those tables. The graph owns the histories (see frame-05).
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:385 - BuildFrame begins here and ends at 5906: one function describes the whole frame
  - RageV/src/RageV/Renderer/FrameGraphBuilder.h:119 - FrameDesc carries 16 TemporalHistory and 4 MeasuredChangeHistory pointers as named fields (119-249)
  - RageVRuntime/src/RuntimeLayer.h:81 - The runtime declares each history by name (73-110) and wires them one by one (RuntimeLayer.cpp:532-549); the editor does it twice (EditorLayer.h:433-505; EditorLayer.cpp:672-689 and 830-847)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:2779 - The glass reflection lane (2779-3032) repeats the opaque lane (2364-2603): budget, trace, resolve, measured change, accumulate
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1901 - The measured-change block is written four times (1901-1979, 2222-2293, 2517-2593, 2924-2998)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:562 - The camera-cut list is maintained by hand (562-573) and does not drop WaterLampLight, WaterReflectionLight or WaterReservoirs
  - RageVRuntime/src/RuntimeLayer.cpp:593 - Code comment: hand-wiring built five water passes in a scene with no water, 0.26 ms of an 8 ms frame (591-595)
- **Skeptic's note.** Checked. BuildFrame runs from line 385 to 5906. FrameDesc carries 16 TemporalHistory and 4 MeasuredChangeHistory pointers as named fields (FrameGraphBuilder.h). The runtime fills them line by line (RuntimeLayer.h:73-110, RuntimeLayer.cpp:532-549), and so does each editor view (EditorLayer.cpp:672-689, 830-847). The measured-change block exists four times (RecordIsLastFrame at 1908, 2229, 2524, 2931). The glass lane repeats the opaque lane. The commit stats are right, though the line totals include docs and scripts. The camera-cut list (562-573) is less consistent than the finding says. It drops the glass budget and the glass change record, but not the opaque ReflectionBudget, DirectBudget, DirectChange, ReflectionChange or GiChange. Nor does it drop WaterLampLight, which is the only water history live by default: WaterReflectionLight needs --water-reflection, and the pass behind WaterReservoirs is retired even though its history is still allocated at 3157. Most of these omissions are harmless today because the invalidated histories stop their readers, but that is luck, not design. The 0.26 ms figure for passes with no water is in the code comment (RuntimeLayer.cpp:591-595). The structural cost of wiring by hand is not recorded anywhere in docs.

#### frame-03 · TAA is mandatory with rays and has become the place where signal and surface problems get patched

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-7, RT2-28 (Sample service; generic mask)
- **What is wrong.** With rays on the engine forces TAA. The reason is that the sub-pixel jitter exists only under TAA, and every random draw in the traced shaders advances from frame to frame only while that jitter is non-zero (pbr_fragment.glsl:4729-4731). So whether each signal converges depends on the anti-aliasing mode. Meanwhile the TAA resolve has grown inputs specific to one signal or one surface (the sea's motion, two change maps, the revealage), and that is where RT-16, RT-22 and RT-24 show up. The project's MSAA 4x is dormant because the project chose TAA on 2026-08-27; the forcing only removes MSAA as a choice while rays run. A frame-indexed random sequence independent of the AA mode has never been measured.
- **What it causes.** On screen: the spreading blur still left in RT-24 under a camera spin is in taa_resolve. RT-22's fix in the resolve made the bridge's water glitter blink more. RT-16 was two memories in series. By design: the project's MSAA 4x does nothing whenever rays run (SampleProject.rvproject keeps MsaaSamples: 4 under AntiAliasing: TAA). And each new signal defect invites another surface-specific TAA input, which the owner's rules forbid (no fix keyed to a surface type, no reliance on TAA).
- **Measured?** RT-22 regression: 0.80% -> 0.97% of the bridge frame blinking (HANDOFF.md:4-5, 89). RT-24 clean test: fix 2 took the pole score from 17.1 to 8.0 (HANDOFF.md:43). With the frame filter's memory off, the floor was as sharp as when settled (HANDOFF.md:57-58).
- **Already recorded?** Forced TAA is recorded as the owner's rule (HANDOFF.md:394-400). RT-16, RT-22 and RT-24 are recorded one by one. Not recorded: that the AA mode owning the jitter is their common root, and that the resolve's input list has become the point where everything couples.
- **Fix direction.** Make the sample sequence a frame-level service that does not depend on the AA mode. Measure separately whether the G-buffer itself must jitter, or whether a per-signal, frame-indexed random sequence is enough. Finish each signal's reconstruction before the composite. Give the final temporal pass, TAA or an upscaler, one generic per-pixel mask saying how far its history is invalid, built from the signals' change maps and the layers' coverage, with no surface-specific inputs. Prove it with MSAA-with-rays and no-AA-with-rays arms, compared by diff image.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:205 - With ray tracing on, ResolveAntiAliasing returns TAA whatever the project or --aa asks (197-206)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:716 - The sub-pixel jitter is only computed when TAA runs (716-725); HANDOFF.md:394-400 says the traced averages need that jitter
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:3910 - The TAA resolve samples the water layer, the direct and reflection change maps, the revealage, the TAA guide and the material lane (3910-4007)
  - RageV/src/RageV/Renderer/PostProcess.h:125 - TemporalResolve takes 12 textures, several added for one signal or one surface: water motion (147-152), change maps (161-170), see-through revealage (171-175)
  - docs/HANDOFF.md:57 - RT-24: 'the blur that spreads is taa_resolve under motion' (57-58)
  - docs/HANDOFF.md:4 - RT-22's fix in the frame filter regresses the bridge water glitter from 0.80% to 0.97% of the frame blinking (4-5)
  - docs/RT-SERIES.md:110 - RT-16: the traced reflection passes through two temporal filters in series
- **Skeptic's note.** Confirmed: ResolveAntiAliasing returns TAA whenever rays run (FrameGraphBuilder.cpp:204-206; HANDOFF.md:394-400). The jitter exists only under TAA (716-725). TemporalResolve takes 12 textures, including the sea's motion, both change maps and the revealage (PostProcess.h:125-175; FrameGraphBuilder.cpp:3910-4007). RT-22's regression and RT-24's spreading blur in taa_resolve are recorded (HANDOFF.md:4-5, 57-58). The finding misses stronger evidence for its root cause. Every random draw in the traced shaders advances from frame to frame only while the jitter is non-zero: pbr_fragment.glsl:4729-4731 calls it 'the rule every stochastic term here follows', and the same test appears at 1380, 4075 and 4819 and in Renderer3D.cpp:7987, 8097 and 8267. The 2026-09-21 'lamps freeze without TAA' lesson was therefore produced by that gate. A random sequence indexed by frame and independent of the AA mode has not been measured as an arm on main, so the finding's proposed test is not already rejected. One claim is wrong: MSAA 4x is dormant because the project itself switched to TAA on 2026-08-27 (commit 5dd603f), not because rays force TAA. The forcing only means MSAA cannot be picked while rays run (apart from the --msaa=N flag under TAA). A caution for the fix direction: RT-4 and RT-15 measured compositing the reflection after TAA as worse (edge flicker, then speckles from the moving layer). 'Finish each reconstruction before the composite' must not become 'composite after TAA' without a new measurement.

#### frame-04 · Scene extraction and recording run on one thread, again for each view, inside the G-buffer pass

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** scalability. **Scope:** rewrite.
- **Roadmap:** RT2-34, RT2-35, RT2-36
- **What is wrong.** Scene extraction and draw recording run on one thread, inside the G-buffer pass callback, once per view, with the lit half handed on through a file-scope global. Before the graph, every view re-walks the registry to rebuild the ray-tracing instance list and runs its own GPU cull. The TLAS itself is refit in place when the instance count is unchanged (with a full rebuild every 64 frames) and is built once per frame, not once per view. What grows with object count is the CPU rewriting every instance and the per-view walks. Measured at 60,000 objects: about 4.6 ms of walking and submitting inside the pass, on top of 2.9 ms of depth passes and 2.6 ms of transform walk.
- **What it causes.** CPU cost grows with object count times view count, and none of it can use the laptop's 24 hardware threads. Recorded at 60,000 objects: the lit pass's walk and submission, the depth passes (2.9 ms), the transform walk (2.6 ms) and 2.7 ms of GPU add up to about 12.8 ms. That leaves roughly 4.6 ms of walking and submitting inside the pass. A full TLAS build each frame also grows with instance count (inferred).
- **Measured?** HANDOFF.md:7116-7121 (60,000 objects). ENGINE-NOTES.md:11691-11697: at 60,000 objects the transform walk costs 27.4 ms over 7 calls, and RefreshDrawList 2.9 ms.
- **Already recorded?** The transform walk is recorded as the CPU wall. Not recorded as defects: extraction inside pass callbacks, the re-walk for each view, and the full TLAS rebuild every frame.
- **Fix direction.** Build one render-world snapshot per frame, using jobs. Keep a persistent GPU scene updated by deltas. Refit or update the TLAS for moved instances, and rebuild it only when instances are added or removed. Record command buffers in parallel by pass group. Keep scene code out of pass callbacks.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1634 - The GBuffer pass's execute lambda runs the whole scene callback behind static flags (1634-1647, 1219-1272)
  - RageVRuntime/src/RuntimeLayer.cpp:555 - That callback is Scene::OnRenderRuntime (555-558)
  - RageV/src/RageV/Scene/Scene.cpp:4965 - OnRender collects lights, resolves every item's material, fills every instance row and records every draw while a render pass is open (4965-5390)
  - RageV/src/RageV/Scene/Scene.cpp:4924 - The lit half's state is carried to the next pass in a file-scope global g_LitTail (4924-4930, 5396-5407)
  - RageV/src/RageV/Scene/Scene.cpp:2862 - The ray-tracing instance list is rebuilt from a registry walk each view (2862-2939), then built in RenderShadows before the graph
  - RageV/src/RageV/Renderer/RayShadows.cpp:434 - The top-level acceleration structure is fully built every frame; no refit or update path
  - RageVEditor/src/EditorLayer.cpp:802 - The editor's game view runs RenderShadows again, which means a second walk and cull (796-804)
  - RageV/src/RageV/Core/Application.cpp:518 - Threads exist only for loading (518, 559); the frame loop is single-threaded (791-864)
- **Skeptic's note.** Confirmed: the GBuffer pass's execute callback runs the whole scene callback (FrameGraphBuilder.cpp:1219-1272, 1634-1647, then RuntimeLayer.cpp:555-558, then Scene.cpp:4965-5390). It collects lights, looks up every item's material, fills instance rows and records draws while a render pass is open. The lit half's state travels in the global g_LitTail (Scene.cpp:4918-4932). Before the graph, RenderShadows re-walks the registry for each view to rebuild the ray-tracing instance list (Scene.cpp:2842-2941) and runs the GPU cull again. Rendering is recorded on one thread. The 60,000-object numbers are as recorded (HANDOFF.md:7116-7121; ENGINE-NOTES.md:11691-11697). Refuted: the TLAS (the scene-wide ray-tracing index) is not rebuilt from scratch every frame. VulkanAccelerationStructure::Build refits it in place, meaning it updates the existing index for moved objects (MODE_UPDATE), whenever the instance count is unchanged, and rebuilds it every 64 frames (VulkanResources.cpp:1305-1321). The editor's game view does not build it a second time (RayShadows.cpp:357-383, the BuiltThisFrame guard); it only rebuilds its own instance list, which is the 7bp device-loss rule. So the fix direction's 'refit, rebuild only when instances are added or removed' already exists, and so does the target design's TLAS refit. Also, 'threads exist only for loading' is not quite right: physics runs on Jolt's thread pool (PhysicsWorld.cpp:387-388). Rendering has no threads.

#### frame-05 · Render targets take about 2.7 GB per view at 1440p in the garage, with no reuse and no release

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-19, RT2-16, RT2-15
- **What is wrong.** Worked out from the formats at the SampleProject settings, the persistent histories now come to about 440 bytes per pixel (RT-14 recorded 128 on 2026-09-07). On top of that come roughly 290 bytes per pixel of transient targets that never share memory. That is about 2.7 GB per view at 1440p and 3 GB at the owner's 2560x1600 target, not yet measured on the device. The contributors are the full-resolution glass histories, the always-written moving-layer lane of a feature that is off by default, the TAA guide copy RT-14 already flagged, a pool that never frees, and histories that are invalidated but never released.
- **What it causes.** Inferred from the formats, at the SampleProject settings (TAA, rays, glass layer, measured change, baked GI). Histories come to about 440 bytes per pixel and transients about 290. For one view that is about 1.5 GB at 1080p, 2.7 GB at 1440p and 6.2 GB at 4K. The editor's two views roughly double it at panel size, and water scenes add more. On a 12 GB laptop GPU that crowds out AAA geometry, textures and acceleration structures. Memory bandwidth scales with the same numbers.
- **Measured?** The docs have no VRAM figure. Arithmetic in B/px: histories are TAA 32 + reflections 96 + direct 72 + glass direct 72 + glass reflections 96 + TAA guide 32 + AO 12 + change records 28 = 440. Transients are scene target 53 + glass layer 28 + traces, resolves, blurs and composites about 210.
- **Already recorded?** RT-14 (RT-SERIES.md:116) wants the G-buffer's bandwidth measured before any packing. Not recorded: the totals, the pool that never frees, and the moving-layer lane being written while the feature is off.
- **Fix direction.** Make persistent resources graph-owned, and release them when a feature or view stops. Alias transients by lifetime. Set a lane budget per signal from RT-14's measurement. Keep last frame's G-buffer lanes as graph history instead of copying them into the TAA guide. Drop the lanes of switched-off features. Render scale (frame-06) shrinks everything.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1004 - Traced reflections history: six RGBA16F lanes, a pair (96 B/px), including the moving-layer lane (1020-1023) of a feature that is off by default
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1798 - Direct light history: 4x RGBA16F + RG16F pair (72 B/px); the glass copies at 2731 (72 B/px) and 2788 (96 B/px)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1676 - The TAA guide is an RGBA32F full-resolution pair (32 B/px), a copy that exists only because the G-buffer does not survive the frame
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1905 - Measured-change record: 8 RGBA32F lanes on a 1/3 grid (about 14 B/px), with 4-lane copies for reflections (2521) and glass (2928)
  - RageV/src/RageV/Renderer/RenderGraph.cpp:338 - The pool only grows: every CreateTarget gets its own allocation, kept across frames, never evicted, never aliased
  - RageV/src/RageV/Renderer/TemporalHistory.h:123 - Release says 'The editor does this when a viewport closes', but nothing calls it in RageVEditor/src or RageVRuntime/src; Invalidate (121) keeps the memory
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:256 - The moving-layer output is declared and written every frame (1937), although ReflectionMovingLayer defaults off (EngineConfig.h:404)
- **Skeptic's note.** The formats and sizes check out. Reflections keep six RGBA16F lanes (FrameGraphBuilder.cpp:1004-1023). The direct light keeps four RGBA16F lanes and one RG16F (1798-1802). Both glass histories are full resolution (2731-2739, 2788-2793). The TAA guide is an RGBA32F pair (1676-1679). The change records are 8, 4 and 4 RGBA32F lanes on the 1/3 grid, and AO is half resolution. Together the histories come to about 440 bytes per pixel. The moving-layer lane is always allocated and written (reflection_accumulate.rvshader:256, 1937) while ReflectionMovingLayer defaults off (EngineConfig.h:404). The pool never evicts (RenderGraph.cpp:290-340), and neither app ever calls TemporalHistory::Release. Wrong: 'the docs have no VRAM figure' and 'not recorded: the totals'. RT-14, done 2026-09-07 (RT-SERIES.md:2786-2853), inventoried 53 B/px of scene target and 128 B/px of histories (741 MB at 2560x1600) and concluded that the histories are the big half. It also filed keeping the G-buffer for one frame instead of copying it into the TAA guide as 'the one real packing opportunity'. What is new is that the histories have grown about 3.4 times since that record. The 1.5, 2.7 and 6.2 GB totals are worked out from formats, not read from the device (the 4K arithmetic gives about 6.05 GB). Also missed: in water scenes WaterLampChoices is allocated even though its pass is retired, because the Prepare at line 3157 runs before the seaDirect branch.

#### frame-06 · No internal render resolution, no upscaler, no dynamic resolution

- **Verdict:** confirmed. **Severity:** high. **Kind:** missing-capability. **Scope:** refactor.
- **Roadmap:** RT2-29
- **What is wrong.** Every target's size is a fraction of the output size. SSAA can only render larger. The TAA history sits at output size and does not upsample, and per-signal divisors (AO, GI) are relative to output. There is no render-scale setting, no temporal upscaling, no DLSS, FSR or XeSS, and no resolution driven by GPU time.
- **What it causes.** Every per-pixel pass scales with output pixels: the G-buffer, every trace, every accumulator and most histories. 4K costs 2.25x the work and memory of 1440p, and there is no lever to pull. For AAA RT scenes on a laptop RTX 5070 Ti this is the largest missing cost lever.
- **Measured?** Estimate only: RENDERING-REVAMP.md:2210-2214, which ends 'the owner's eye decides'. Never built, never measured.
- **Already recorded?** Yes, as an unbuilt candidate (RENDERING-REVAMP.md:2210; NEXT.md:95).
- **Fix direction.** Add internal versus output resolution to the graph. Add a temporal upscale pass: the engine's own TAAU first, vendor upscalers behind the same interface. Make dynamic resolution one optional global setting. Make signal divisors relative to internal resolution. Judge the water glitter and the reflections by diff image and by the owner's eye.
- **Evidence:**
  - RageV/src/RageV/Renderer/RenderGraph.h:78 - A target's Scale is a fraction of the output size (78-82); there is no render-resolution concept
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:419 - SSAA can only render larger than output (419-437)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:3886 - The TAA history is prepared at output size; the resolve does not upsample
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:376 - Per-signal divisors (RayDetailDivisor 376-383; AO 2025-2028) exist, but they are relative to output size
  - docs/RENDERING-REVAMP.md:2210 - Render scale with temporal upsampling listed at an estimated 30-45% of per-pixel work, never built; a search of the source finds no DLSS, FSR or XeSS
- **Skeptic's note.** There is no render scale, upscaler or dynamic resolution anywhere: a search of RageV/src for DLSS, FSR, XeSS, upscal and render scale finds nothing. Targets are fractions of the output size (RenderGraph.h:78-82). SSAA can only scale up (FrameGraphBuilder.cpp:419-423). The TAA history is at output size (3886-3888). It is recorded only as an unbuilt candidate (RENDERING-REVAMP.md:2210-2214; NEXT.md:95). The case is stronger than 'estimate only'. RT-14 measured the traced passes as 90-99% linear in pixel count and 16 ms of a 24 ms frame at 3.24 megapixels (RT-SERIES.md:2817-2835), so internal resolution is a lever measured to be large. A caveat for the roadmap: under 'no reliance on TAA', each signal still has to be fully reconstructed at internal resolution before the upscale. The owner's eye on the water glitter decides the look.

#### frame-07 · Renderer3D is an all-static 9,900-line object that owns every pipeline and every pass's descriptor sets

- **Verdict:** confirmed. **Severity:** high. **Kind:** architecture. **Scope:** rewrite.
- **Roadmap:** RT2-17
- **What is wrong.** Every Renderer3D function is static, and all state lives in one Renderer3DData struct. That struct holds about 80 shader and pipeline pairs. For each view slot it holds about 70 named descriptor sets and buffers, one member per pass. Renderer3D compiles shader variants and uploads lights, clusters, instances, materials, bones and emitters. It draws shadows, glass and water, and it hosts more than 25 trace and filter entry points. Frame inputs reach draws through static setters called around the draw. Per-frame switches are packed as bits into a float in the scene uniform. Renderer.cpp adds process-wide statics that BuildFrame sets while declaring the frame.
- **What it causes.** There is no per-view context. Views must be declared and recorded one after another, and nothing can be recorded in parallel. Each new pass adds struct members and a slot index. Correctness depends on the order of Set*() calls around draws; ENGINE-NOTES 7u records a ghost caused by exactly this kind of process-wide state.
- **Measured?** None; this is structure. Line counts are from the current tree.
- **Already recorded?** Not recorded as a problem.
- **Fix direction.** Split Renderer3D into four parts: scene submission (the GPU scene upload), a library of pipelines and shader variants, one module per signal pass, and an explicit per-view render context passed to every pass. No statics. The shader math stays as it is.
- **Evidence:**
  - RageV/src/RageV/Renderer/Renderer3D.h:22 - Every member is static; pass entry points take long texture lists (AccumulateSignal 452-482 takes 16 textures)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:546 - Renderer3DData holds about 80 shader/pipeline pairs (550-745) and a per-view SceneSlot with about 70 named sets and buffers (926-1133)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:1094 - Fixed per-signal slot arrays: SignalAccumulateInputs[8] and SignalBlurInputs[8][3] (1094-1101)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:3853 - BeginScene is 1,315 lines (3853-5168); EndScene is about 1,000 (5168-6173)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:4112 - Per-frame switches are packed as bits 16-25 into a float in the scene uniform (RayRates.w, 4112-4130)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:2642 - Frame inputs reach draws through static setters called around the draw (2642-2657, 3684-3710)
  - RageV/src/RageV/Renderer/Renderer.cpp:25 - Process-wide statics for jitter, camera motion, screen reflections and GI dials (25-43, 165-174), set while the graph is being declared (FrameGraphBuilder.cpp:780-894)
- **Skeptic's note.** The class is all static (Renderer3D.h:22 onward). AccumulateSignal takes 16 textures (Renderer3D.h:452-482). The per-signal slot arrays are fixed: SignalAccumulateInputs[8] and SignalBlurInputs[8][3] (Renderer3D.cpp:1094-1101). BeginScene runs from 3853 to 5168. Per-frame switches are packed as bits into RayRates.w (4112-4130). Static setters are called around the lit draw (FrameGraphBuilder.cpp:2638-2660). Renderer.cpp holds process-wide statics (25-43, 243-258). One count is off: there are about 37 shader and pipeline pairs (about 78 references with the arrays), not 80 pairs; the per-view slot holds 45 sets and 18 buffers. The pattern has produced real defects: the 7u ghost, and two found in this review, framegraph-s1 (a stale camera used for a live decision) and framegraph-s3 (an indirect-light pointer that is never cleared).

#### frame-08 · Ray tracing is treated as a side feature of shadows, and raster is the default

- **Verdict:** confirmed. **Severity:** medium. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-17
- **What is wrong.** ResolveRayTracing returns false whenever shadows are off. The engine's defaults are raster: rays off, FXAA, traced reflections and GI off. The RT mode is resolved, and shaders recompiled, inside Scene::RenderShadows. The same function records GPU culling and builds the TLAS, before the graph and outside it. The raster fallbacks (SSR, SSGI, voxel GI, post-chain SSAO) are interleaved with the RT path inside the one BuildFrame.
- **What it causes.** RT-first exists only in SampleProject's settings file. The TLAS, which every trace reads, has no graph node and depends on a side effect of the shadow pass. Every fallback adds branches to the same function.
- **Measured?** None.
- **Already recorded?** Recorded as design (ENGINE-NOTES 7am/7an; comment at FrameGraphBuilder.cpp:212-216), not as a problem.
- **Fix direction.** Choose the pipeline at the top. Build an RT frame recipe (the default) and a raster fallback recipe as separate builders. Make the TLAS build or refit a graph pass. Make shadows one signal of the RT recipe.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:217 - Shadows off means rays off: 'The rays ride on the shadow pass' (212-218)
  - RageV/src/RageV/Renderer/RenderSettings.h:374 - RayTracing defaults to false; AA defaults to FXAA (250); traced reflections and GI default Off (396, 459)
  - RageV/src/RageV/Scene/Scene.cpp:1756 - The RT mode is resolved, and lit shaders recompiled, inside RenderShadows (1756-1902), which also records GPU culling (1914-1940) outside the graph
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:895 - The SSR, SSGI/voxel and post-SSAO fallbacks sit in the same function as the RT path (895-898, 4281-4442, 4581-4826, 4914-5024)
- **Skeptic's note.** ResolveRayTracing returns false with shadows off (FrameGraphBuilder.cpp:212-218). The RenderSettings defaults are raster (RenderSettings.h:250, 374, 396, 442, 459), and a new project keeps them, because ProjectTemplate.cpp writes no render settings. The RT mode is resolved for each view inside Scene::RenderShadows (Scene.cpp:1756-1760). There, SetRayTracedShadows recompiles every lit shader on the frame thread whenever the answer changes (Renderer3D.cpp:2378-2403). The GPU cull and the TLAS build are recorded there too, outside the graph (Scene.cpp:1914-1940, 2941). The raster fallbacks share BuildFrame with the RT path. It is recorded only as design (ENGINE-NOTES 7am/7an).

#### frame-09 · Declaring the frame changes state that outlives the frame

- **Verdict:** confirmed. **Severity:** low. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-15
- **What is wrong.** Three kinds of lasting state change happen while passes are still being declared, before Compile has checked the frame. The history ping-pong swaps (Advance) run then. RecordIsLastFrame rewrites each change record's camera. Renderer globals are set: the gloss window, GI dials, target formats and the active-feature report. If Compile then fails, the runtime skips execution, but the histories have already been swapped and marked valid.
- **What it causes.** The graph cannot be declared twice, cached, checked in advance, or declared for several views before any recording. A failed compile leaves histories claiming a frame that never ran; that is an edge case, but it shows the design.
- **Measured?** None.
- **Already recorded?** TemporalHistory.h:113-115 documents the swap-at-declaration as intended.
- **Fix direction.** Make declaration free of side effects. The graph commits state changes (swap, invalidate, record camera) only after a successful execute.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:4014 - history.Advance() swaps the pair while passes are being declared, before Compile; 20 such call sites (1702 ... 5023)
  - RageVRuntime/src/RuntimeLayer.cpp:621 - If Compile fails the frame returns without executing, but the histories were already swapped and marked valid (TemporalHistory.cpp:69-80)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:30 - RecordIsLastFrame rewrites each change record's last camera during declaration (30-43)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:780 - Renderer globals are set during declaration (780-894, 959, 492-499)
- **Skeptic's note.** There are 20 Advance() calls that swap history pairs during declaration (for example 1702, 2006, 4014). RecordIsLastFrame rewrites the record's last camera during declaration (22-43). Renderer globals are set while declaring (492-499, 780-894, 959). On a failed Compile the runtime returns (RuntimeLayer.cpp:621-629) and the editor logs, after the swaps have happened. This is documented as intended (TemporalHistory.h:113-115). Nothing visible goes wrong today, since a failed compile logs every frame anyway. The real cost is that the graph cannot be cached, declared twice, or declared for several views before recording. That makes it low, not medium. The mirror image, declaration reading state that the previous frame's execution wrote, does cause a live defect: see framegraph-s1.

#### frame-10 · The frame rasterises the scene twice and composes signals in a forward lit pass

- **Verdict:** confirmed. **Severity:** medium. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-22
- **What is wrong.** The G-buffer pass draws the scene. Then the lit ('Scene') pass draws every pending draw again with the lit pipelines, which evaluates the material a second time. That pass adds the direct light, AO and GI from textures bound per draw set, and a separate composite before TAA adds the reflection.
- **What it causes.** About 0.6 ms of the lit pass at Headland is the material paid a second time, and more with heavy materials near the camera. The forward lit shader also stays the place every new signal must be threaded through: more bindings, rebound on six sets.
- **Measured?** RT-SERIES.md:1692: at Headland the G-buffer pass costs 0.62 ms, and about 0.6 ms of the lit pass's 2.2 ms is the second material evaluation.
- **Already recorded?** Yes: RT-2.1 and RT-2.2 (RT-SERIES.md:89-90, 1692), parked by the owner to the end of RT-series 1.
- **Fix direction.** Do RT-2.2 first: feed the lit pass from the G-buffer, with the albedo lane widened first. Then move to a compute lighting composite from a complete G-buffer, removing the second raster. RT-14's measurement decides the extra lanes (emissive, coat, sheen).
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1650 - The GBuffer pass draws the scene (1650-1664); the 'Scene' pass draws it again (2609-2662)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:9155 - DrawLit re-draws every pending draw with the lit pipelines after rebinding signal textures on six sets (9114-9156)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:2624 - The lit pass samples each signal (2624-2629); the reflection is added by a separate composite before TAA (3776-3827)
  - docs/RT-SERIES.md:1692 - At Headland the material is paid twice, about 0.6 ms of the lit pass's 2.2 ms; RT-2.2 is parked to the end of the series
- **Skeptic's note.** Both the GBuffer pass and the Scene pass draw the scene (FrameGraphBuilder.cpp:1650-1664, 2609-2662). DrawLit rewrites the signal bindings on six sets and draws every pending draw again (Renderer3D.cpp:9114-9156). The reflection is added by the ReflectionComposite pass before TAA (3776-3827). Measured and recorded: about 0.6 ms of the lit pass's 2.2 ms at Headland is the material being evaluated a second time (RT-SERIES.md:1692). RT-2.2 is parked by the owner until the end of RT-series 1, and RT-14 found its precondition (a wider albedo lane) costs nothing if it uses sRGB8. The CPU cost of submitting every draw twice has not been measured.

#### frame-11 · Every pass is a fullscreen raster draw, including passes with no work and tiny ones

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-20, RT2-15
- **What is wrong.** 84 of the 89 pass sites are raster passes. Every frame runs six full-resolution reflection blur passes that are plain copies once the picture has settled clean. It also runs fifteen measured-change passes (three re-lights, twelve filters) that clear a target and draw nothing whenever no light or object changed, moving camera included. The ray budget's screen mean is a chain of seven raster passes over tiny targets. The traces cannot move to compute until the hit-shading include stops sampling with implicit level of detail. None of this is measured on its own; the per-pass timings can measure it.
- **What it causes.** Inferred: six full-resolution copy passes per settled garage frame, and about 15 open/clear/close passes while parked. Trace and filter passes get no group-shared memory and no control over thread-group shape for ray coherence. Unmeasured.
- **Measured?** None. The profiler's per-pass GPU timings can measure it.
- **Already recorded?** HANDOFF.md:6076 records the fragment-only constraint (for the irradiance fill). The blur copies are an accepted cost in the code comment (FrameGraphBuilder.cpp:1449-1454).
- **Fix direction.** Build a hit-shading library usable from compute, with explicit level of detail (for example from ray cones). Move traces, filters and reductions to compute passes. Cull or indirectly dispatch passes that have no work. Accept each move on per-pass GPU time with identical pixels.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1455 - The specular blur passes are always added; the comment says 'a settled and clean picture pays three copies' (1449-1458), for both the opaque and the glass reflections
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1762 - Change-filter passes return without drawing when nothing was re-lit, but the graph still opens and clears their targets (1752, RenderGraph.cpp:513)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:4157 - The budget's screen mean is a chain of 6-7 raster passes over tiny targets (4157-4186)
  - docs/HANDOFF.md:6076 - Traces 'had to be a fragment shader, not compute': the shared hit-shading include samples textures with implicit level of detail
- **Skeptic's note.** Confirmed: there are 84 graphics, 2 compute and 3 standalone pass sites. Both reflection signals always add three full-resolution blur passes: varianceFilter is on by default (FrameGraphBuilder.cpp:1449-1458, EngineConfig.h:429, MovingRadius 4 at Renderer3D.cpp:7307), and the code says a settled, clean picture 'pays three copies'. Change-filter passes clear their targets and draw nothing when nothing was re-lit (1762-1765 with RenderGraph.cpp:513-514). The budget's screen mean is seven raster passes over targets of 160x90 and smaller (4157-4186). Traces are fragment-only because of the implicit-level-of-detail include (HANDOFF.md:6076-6081). One claim is wrong: the 3 re-light and 12 filter passes open, clear and close without drawing on every frame in which no light or object changed, moving camera included, not only while parked. The re-light skips when its key is unchanged (Renderer3D.cpp:8329), and the key leaves out the camera (TemporalHistory.h:190-195). The blur copies are partly measured: the noise-driven blur shipped inside RT-15's four fixes, which together cost +0.33 ms parked (HANDOFF.md:564), not on its own.

#### frame-12 · No per-frame transient allocator: each pass owns named descriptor sets and upload buffers per view

- **Verdict:** confirmed. **Severity:** medium. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-18, RT2-15
- **What is wrong.** A descriptor set is the table that binds a pass's textures and buffers. Renderer3D keeps one named set per pass per view slot and rewrites it every frame. A set rewritten after it was bound invalidates the command buffer, so wherever two passes would share a set, a new member has to be added. Upload data is written into host-visible buffers during recording, so two passes reading the same data need separate buffers; the reflection trace keeps its own copy of the emitter table. PostProcess pools its sets by call order.
- **What it causes.** This is part of the cost of every new pass. The same data is uploaded more than once. The order-based pool rebuilds sets whenever the pass list changes (see frame-14).
- **Measured?** None.
- **Already recorded?** The individual traps are recorded (Renderer3D.cpp:980-988 and 1096-1101; PostProcess.cpp:140-156). The missing allocator is not.
- **Fix direction.** Add a per-frame linear upload ring and transient descriptor allocation to the RHI (or descriptor buffers, or push descriptors), handed out by the graph per pass.
- **Evidence:**
  - RageV/src/RageV/Renderer/Renderer3D.cpp:980 - The reflection trace keeps its own emitter buffers, because a host-visible buffer written between two recorded draws 'is written under the first one' (980-988)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:6983 - Each pass has a named set per view slot, rewritten each frame, with HasBinding guards against stale staged shaders (6983-7038)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:1096 - One set per blur pass was added after a set rewritten while bound invalidated the command buffer (1096-1101)
  - RageV/src/RageV/Renderer/PostProcess.cpp:410 - Post-process sets are pooled by call order and rebuilt when the pipeline at that position differs (135-158, 410-415)
- **Skeptic's note.** The reflection trace keeps its own emitter buffers, because a host-visible buffer written between two recorded draws is 'written under the first one' (Renderer3D.cpp:980-988). Each pass has a named set per view slot, rewritten every frame with HasBinding guards (6983-7038). One set per blur pass was added after a set rewritten while bound invalidated the command buffer (1094-1101). PostProcess pools its sets by call position and rebuilds one when the pipeline at that position differs (PostProcess.cpp:135-158, 398-415). Each trap is recorded in code comments; the missing per-frame upload and descriptor allocator is not recorded. Nothing is measured.

#### frame-13 · Settings and measurement flags have multiplied, and old reference arms stay wired into the frame

- **Verdict:** confirmed. **Severity:** medium. **Kind:** tech-debt. **Scope:** refactor.
- **Roadmap:** RT2-17, RT2-1, RT2-3, RT2-30
- **What is wrong.** RenderSettings has 24 fields, PostSettings about 69 and EngineConfig 151, of which about 40 are render on/off arms. About 75 lines of BuildFrame read EngineConfig, and pass functions read flags while recording. Whole old paths stay compiled and wired as reference arms: post-chain AO, one-frame-late traced GI, the sea's private choose/shade pair, the half-resolution sea mirror, and the reflection moving layer. For the sea, the default is the private accumulator, not the shared contract. Ray counters are compiled into the TAA resolve and every tracing shader whenever ray queries exist.
- **What it causes.** Every change has to stay correct across many combinations of arms. Measurement instruments cost frame time in normal frames: the ray counters cost 0.26 ms at 1440p on the showroom.
- **Measured?** Ray counters +0.26 ms (2560x1440, showroom, 2026-09-05 bisect). This number is in the owner's session memory (project_ragev_showroom_fps_regression.md), not in docs/.
- **Already recorded?** Each flag is documented in EngineConfig.h, and the owner's rule 'a global render setting plus a measurement flag' exists. The growth in arms and any policy for retiring them are not recorded.
- **Fix direction.** Keep a small set of global levers in settings: render scale, per-signal tier, RT optimisation level. Register A/B arms in a measurement registry compiled out of shipping builds. Delete arms once a decision is made, for example the sea's contract versus its private accumulator. Put the counters behind a compile define.
- **Evidence:**
  - RageV/src/RageV/Core/EngineConfig.h:367 - About 40 render on/off arms among 151 fields (367-676); EngineConfig.cpp parses 224 keys; RenderSettings has 24 fields and PostSettings about 69
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:4581 - The old post-chain AO is kept whole as the --ao-signal=off arm (4581-4826); the old one-frame-late traced GI as --gi-signal=off (4447-4538)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:3283 - The sea's private choose/shade pair is off by default (3283-3350); the half-resolution sea mirror runs on a flag only (3517-3643); the moving layer is off (3762, 4022-4047)
  - RageV/src/RageV/Core/EngineConfig.h:592 - WaterContract=false: the sea's private accumulator (FrameGraphBuilder.cpp:3449-3501), not the shared contract, is the default
  - RageV/src/RageV/Renderer/Renderer3D.cpp:7045 - Pass functions read measurement flags while recording (7045-7071)
  - RageV/src/RageV/Renderer/PostProcess.cpp:204 - Ray counters are compiled into the TAA resolve whenever ray queries exist, and the tracing shaders count too (RayCounters.h:1-28); there is no off switch
- **Skeptic's note.** The counts are roughly right: about 150 EngineConfig fields, 32 render bools in lines 367-676, about 25 RenderSettings fields and 70-78 PostSettings fields. Old paths stay wired as arms: the post-chain AO (4581-4826), --gi-signal=off (4447-4538), the retired private choose/shade pair, the half-resolution sea mirror that runs only on a flag, and the moving layer, off by default. Pass functions read flags while recording (Renderer3D.cpp:7045-7071). Ray counters: RV_RAY_COUNTERS is defined in every ray-traced lit shader (pbr_fragment.glsl:544-545) and in the TAA resolve (PostProcess.cpp:204-205), and there is no switch. The +0.26 ms comes from the 2026-09-05 bisect in the owner's memory note and has not been re-measured since. Note that WaterContract=false is a decided outcome, not an open choice: RT-8 job 3, which put the sea on the shared contract, 'measured no gain' and was dropped (RT-SERIES.md:59), so the contract arm is the dead one. The owner's verification protocol relies on reference arms (RT-SERIES.md:146), so arms should be deleted only after a decision, as the fix direction already says.

#### frame-15 · Editor and runtime duplicate the frame's wiring, and the editor pays for the scene twice

- **Verdict:** confirmed. **Severity:** low. **Kind:** tech-debt. **Scope:** refactor.
- **Roadmap:** RT2-16
- **What is wrong.** The runtime and the editor's two views each fill FrameDesc by hand, in three near-identical blocks. The editor owns two complete sets of histories. The game view re-runs RenderShadows, which re-walks the scene, re-culls, and re-fits shadows for its camera.
- **What it causes.** Each signal change has three places to update (see frame-02). With the game panel open, the editor pays for extraction, culling and shadows twice. It also holds a second full set of histories, which stays allocated after the panel is hidden.
- **Measured?** No measurement of the second view's cost beyond the code comment (EditorLayer.cpp:796-798).
- **Already recorded?** The doubled shadow cost is noted in a code comment. The duplication is not recorded.
- **Fix direction.** Add a ViewRenderer object that owns one view's graph and histories. Apps create views and supply a camera, an output and callbacks.
- **Evidence:**
  - RageVRuntime/src/RuntimeLayer.cpp:510 - FrameDesc is filled by hand (510-619)
  - RageVEditor/src/EditorLayer.cpp:611 - The scene view (611-773) and the game view (810-908) repeat the same wiring
  - RageVEditor/src/EditorLayer.h:433 - 32 TemporalHistory, 8 MeasuredChangeHistory and 2 ExposureState members (433-505), never released
  - RageVEditor/src/EditorLayer.cpp:796 - The game view re-runs RenderShadows: second shadow fit, cull and ray-tracing instance walk (796-804)
- **Skeptic's note.** There are three near-identical FrameDesc blocks (RuntimeLayer.cpp:510-619; EditorLayer.cpp:611-773, 810-908). The editor has 32 history, 8 change-record and 2 exposure members, never released. The game view re-runs RenderShadows (EditorLayer.cpp:796-804): a second shadow fit, GPU cull and ray-instance walk, but not a second TLAS build (RayShadows.cpp:357-383). This happens only while the Game panel is visible (EditorLayer.cpp:794).

#### frame-16 · The scene target's shape is declared in three places and they disagree

- **Verdict:** confirmed. **Severity:** low. **Kind:** tech-debt. **Scope:** patch.
- **Roadmap:** RT2-17, RT2-21
- **What is wrong.** BuildFrame declares the scene target with a 16-bit float normal lane plus albedo and object-id lanes. The runtime and the editor declare an 8-bit normal and no albedo or id at start-up, before the first probe capture. Renderer::SetTargetFormats passes the shape on to seven renderers, and Renderer3D rebuilds all its pipelines on any change. One scene target carries nine attachments and is shared by every forward renderer: sky, grid, particles, debug, glow and world UI.
- **What it causes.** The first frame's probe capture draws with pipelines of an out-of-date shape, and then every lit pipeline is rebuilt. Every new lane must be taught to all the renderers, which is the trap behind the 'measured +0.00' record.
- **Measured?** None.
- **Already recorded?** The kind of trap is recorded (FrameGraphBuilder.cpp:596-600; HANDOFF.md:7824-7827). The current disagreement is not.
- **Fix direction.** Describe the G-buffer and its layers once, as data read by the builder, the renderers and the probe path. Draw forward overlays into a small separate target composited afterwards.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:492 - SetTargetFormats with an RGBA16F normal plus RGBA8 albedo and RG32F object-id lanes
  - RageVRuntime/src/RuntimeLayer.cpp:92 - R8G8B8A8_UNORM normal and no albedo or id lane at start-up (92-94); EditorLayer.cpp:155-157 does the same
  - RageV/src/RageV/Renderer/Renderer.cpp:330 - The shape is fanned out to seven renderers (320-338); Renderer3D marks every pipeline dirty on any change (Renderer3D.cpp:2566)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:596 - The recorded trap: an attachment the other renderers were not told about 'measured +0.00' while every graph check passed (596-600)
- **Skeptic's note.** At start-up the apps declare an RGBA8 normal and no albedo or id lane (RuntimeLayer.cpp:92-100; EditorLayer.cpp:155-163). BuildFrame declares an RGBA16F normal plus albedo and an RG32F id (FrameGraphBuilder.cpp:492-499). SetTargetFormats passes the shape on to seven renderers (Renderer.cpp:320-338), and Renderer3D marks every pipeline dirty on any change (Renderer3D.cpp:2545-2567). The first probe capture is consistent with itself, because ReflectionProbe.cpp:68-85 builds the probe face from the same stale formats. So there is no format mismatch, only a wasted pipeline rebuild and a trap for the next lane anyone adds.

#### frame-17 · Rebuilding the graph every frame allocates heavily

- **Verdict:** confirmed. **Severity:** low. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-15
- **What is wrong.** Each frame the graph is cleared and every pass is declared again. Each pass is two std::function objects with large captures, a std::string name (some built by concatenation), and several vectors. BuildFrame also builds a std::map and copies PostSettings into several lambdas. Execute and the Vulkan layer build new vectors per pass, and PostProcess does a map lookup per draw.
- **What it causes.** Inferred: a few hundred heap allocations per view per frame. Probably well under a millisecond today, but it grows with passes times views and rules out caching a compiled graph.
- **Measured?** None.
- **Already recorded?** No. Only the pass-name cache (RenderGraph.h:355-371) addressed part of it.
- **Fix direction.** Compile the graph once per frame shape and only rebind resources each frame. Use arena allocation for per-frame data.
- **Evidence:**
  - RageV/src/RageV/Renderer/RenderGraph.cpp:215 - Begin clears all passes and resources each frame (210-230); each pass is two std::function objects, a std::string and vectors (271-288)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:5290 - Pass names built with std::to_string and concatenation every frame (5290, 5311, 1473); a std::map per frame (1522); PostSettings copied into three lambdas (5269, 5378, 5419)
  - RageV/src/Platform/Vulkan/VulkanCommandList.cpp:117 - New binding and attachment vectors per pass in BeginRenderPass (105-128)
  - RageV/src/RageV/Renderer/PostProcess.cpp:354 - A std::map pipeline lookup on every post-process draw (350-389)
- **Skeptic's note.** Begin clears all passes and resources every frame (RenderGraph.cpp:210-230). Each pass holds two std::function objects, a std::string and vectors (271-288, 318-336). Pass names are concatenated every frame (FrameGraphBuilder.cpp:1473, 5263, 5290, 5311). A std::map is built each frame (1522). PostSettings is copied into several lambdas (5269, 5378, 5419). BeginRenderPass builds new vectors per pass (VulkanCommandList.cpp:105-128). PostProcess looks a pipeline up in a std::map on every draw (PostProcess.cpp:354). The cost is inferred, not measured.

#### framegraph-s1 · RT-9's 'has anything moved' test compares last frame's camera with last frame's camera

- **Verdict:** added by the skeptic. **Severity:** high. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-0
- **What is wrong.** RT-9 spends extra reflection rays only where something moved. The test for a moving camera runs while the frame is being declared, before this frame's scene pass has run. Both numbers it compares were written during the previous frame's execution: the renderer's scene eye by that frame's BeginScene, and the signal's own record by that frame's accumulate pass. With one view the two are always equal. With the editor's two panels they come from different cameras.
- **What it causes.** In the runtime and in a one-panel editor, a camera-only move never counts as motion. The RT-9 tile allocation for reflections and glass therefore never runs during a camera move or spin, which is exactly the case it was built for ('a fast camera'). The RT-24 spin in the static garage is traced at one reflection ray per texel throughout. The lead that 'extra rays stop at the stop' cannot explain the late settle after a camera stop, and the tube reflections' noise while moving is partly this. RT-9's verdict that it earns nothing on the dolly may have measured a pass that never ran. In the editor with both panels open the test reports motion every frame, so both views buy rays while parked. That is the cost the owner asked to stop paying: +0.56 ms parked before the still gate (RT-SERIES.md:387; FrameGraphBuilder.cpp:2398). AnyInstanceMoved has the same per-view flaw, which halves its two-frame window in the editor.
- **Measured?** Not measured; inferred from the order of calls. A cheap check: run a camera-only spin with --pass-timings and look for the 'ReflectionBudget' pass. By this reading it never appears in the runtime, and it appears on every parked frame in the two-panel editor.
- **Already recorded?** No. RT-9's record (RT-SERIES.md:389-394) fixed the opposite failure: a facing that was never recorded and so read as 'turned' on every still frame. This direction, and the RT-24 lead's dependence on it, are not recorded.
- **Fix direction.** Decide motion at declaration from this frame's camera, which FrameDesc already carries (View), compared against the signal's own record. Verify with the pass timings on a spin and on a parked frame, in both apps. In the rewrite, pass each view's camera state explicitly, and never let the declaration read renderer state that execution writes (frame-07, frame-09).
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:2402 - rt9CameraStill = CameraStill(desc.Reflections->Motion()) is evaluated while the frame is being declared and decides whether the ReflectionBudget pass is added (2403-2405). The same test gates DirectBudget (1808-1809) and GlassReflectionBudget (2823-2825)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:7236 - CameraStill compares s_Data->SceneEye and SceneForward with the signal's recorded eye and facing (7240-7247)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:3900 - SceneEye and SceneForward are written only here, in BeginScene. BeginScene runs inside the GBuffer pass's execute (Scene.cpp:5070), which comes after BuildFrame
  - RageV/src/RageV/Renderer/Renderer3D.cpp:7584 - The accumulate pass stamps the signal's record with the same frame's camera (7584-7589). By the next declaration, both values are last frame's
  - RageVEditor/assets/shaders/reflection_trace.rvshader:894 - Without the tile map the trace falls back to RT-15b's rule: one ray per texel unless the surface moves on its own (894-911)
  - docs/RT-SERIES.md:401 - RT-9's camera-dolly speckle came out identical to before RT-9 (0.82 -> 0.71% in both). That is consistent with the pass never running during camera motion
  - docs/HANDOFF.md:64 - RT-24's open lead assumes RT-9's extra rays run while the camera moves and drop at the stop
  - RageV/src/RageV/Renderer/Renderer3D.cpp:5570 - AnyInstanceMoved's 'last frame' flag is shifted once per EndScene, which means once per view. With two views, it holds the other view's result from the same frame

#### framegraph-s2 · Camera-cut detection only works while TAA is running

- **Verdict:** added by the skeptic. **Severity:** low. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-4, RT2-16
- **What is wrong.** RT-6.9's camera-cut test (which drops every history on a teleport or camera switch) is keyed to the TAA history. In FXAA, MSAA or no-AA mode that history is invalidated every frame, so the test never runs and no history is ever dropped on a cut.
- **What it causes.** The RT path is unaffected today, because rays force TAA. In the raster fallback, including the engine's default of FXAA and the owner's MSAA, the AO signal's history, the one-frame-late GI buffer and SSR carry across a cut and smear for as long as each one remembers. It is one more frame-level service owned by the AA mode (frame-03).
- **Measured?** Not measured; inferred from the code.
- **Already recorded?** RT-6.9 is recorded as done. Its dependence on TAA is not recorded.
- **Fix direction.** Keep each view's previous camera in a record that does not depend on TAA, and run the cut test in every mode. Check with a teleport under MSAA with the AO signal on.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:545 - The cut test runs only if the TAA history holds a frame: was.Eye.w > 0.5f && desc.History->HasHistory()
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:730 - In any mode other than TAA, the TAA history is invalidated every frame (730-731), so the test above never fires
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1632 - The AO signal and its own history run in raster too; its condition does not require rays

#### framegraph-s3 · The scene pass's indirect-light hand-off is never cleared, so probe captures read the camera's buffer

- **Verdict:** added by the skeptic. **Severity:** low. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-4, RT2-17
- **What is wrong.** The hand-off that tells the lit shader about last frame's indirect-light buffer is a process-wide pointer. The scene pass sets it and, unlike the reflection hand-off next to it, never clears it. It keeps pointing at data inside the previous frame's graph pass until the next scene pass.
- **What it causes.** A reflection probe captured on a later frame (a realtime or dirty probe) is lit with the camera's last one-frame-late indirect buffer, sampled at the probe face's own pixels. That is exactly what the comment next to it says must not happen. It only matters where that buffer is in use: screen-space or voxel GI, or the traced bounce with --gi-signal=off. The garage bakes its GI, and the RT-3 signal path leaves the texture empty, so neither is affected. The pointer also targets memory that the graph frees at its next Begin; in the current call order nothing reads it in that window.
- **Measured?** Not measured; found by reading the code.
- **Already recorded?** No.
- **Fix direction.** Reset the pointer with the others after the draw (one line). Then compare, by diff image, a probe captured in a realtime-GI scene with one captured with the hand-off empty. In the rewrite, replace process-wide pointers with a per-view context (frame-07).
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1229 - Renderer::SetScreenIndirect(&indirectForScene) is set for the scene draw. The comment (1226-1228) says it is set and cleared 'on the same edges' as the reflections
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1271 - After the draw only camera motion, jitter and screen reflections are reset (1267-1271). The indirect pointer is not
  - RageV/src/RageV/Renderer/Renderer3D.cpp:3950 - BeginScene reads the pointer. When its intensity is set, BeginScene writes that intensity into the scene block and binds its texture at binding 16 (3950-3966, 4561-4566)
  - RageVRuntime/src/RuntimeLayer.cpp:493 - Probe faces are captured through BeginScene each frame, before the graph is built; the editor does the same (EditorLayer.cpp:562)

### Direct lighting, many lights and shadows

**State of the area.** Under ray tracing, which is the shipping mode (SampleProject: RT on, preset Quality, TAA), one full-screen pass called DirectTrace makes every opaque pixel's lamp light. It reads the G-buffer, the per-pixel depth, normal, material and id written before lighting. For each pixel the pass does the following. (1) It finds the pixel's cluster cell. The cluster grid is 16x9x24 screen tiles by depth slices, with light lists built on the CPU every frame from each light's range sphere. (2) It gives every lamp in that cell a cheap score for its unshadowed contribution. (3) It keeps K lamps (8 at Quality) by weighted reservoir sampling: each kept lamp's light is divided by its chance of being kept, so the average stays correct. (4) It shades those lamps with the old loop's BRDF and traces one shadow ray each, aimed at a point sampled on the source's disc or tube segment. Directional lights are always shaded, each with a hard ray. Static pixels under baked lamps take only the live share, plus extra rays that see moving objects only, to measure what a mover takes away.

The output is two pictures: diffuse with the albedo removed, and specular. Both go through the shared temporal accumulator with 'measured change'. Measured change re-lights one pixel in nine with last frame's random numbers to detect real change; it is an exact temporal-gradient check and costs nothing when nothing changed. The lit shader then adds the two pictures. The sea uses the same pass, split into choose and shade.

Traced hits (reflections, the bounce, the sea's mirror) light their hit point with a separate copy of the light code: one lamp per hit by reservoir, treated as a point with a hard ray. In the trace-only passes this copy walks every light in the scene. Emissive meshes act as lights only for next-event estimation at GI and reflection hits: at most 16 rectangles, first come first served, with texel tables built. The direct pass never samples them.

Shadow maps are not rendered in RT mode. They are the raster fallback, and ShadowMap only records each light's ray kind, so there is no duplication. The lit shader's own light loop, which has clearcoat, sheen, anisotropy, the WR-17 distance thinning and the S4 sampler, now runs only for glass panes behind the nearest one, the water's sun and the reference arm. LightGlow draws sized lamps as energy-conserving screen discs. The AO/GI tile budget (importance_tiles, tile_reduce, tile_budget) does not choose lights. The direct light's per-tile K (RT-9) is a separate allocator, and it is skipped at Quality.

Measured cost: garage at 1600x900, 27 lamps per pixel, DirectTrace 2.2 ms, and keeping 8 lamps costs as much as shading all 27 (RT-FIRST.md:160-165); 5.5 ms at 1440p (RT-10 branch notes). The bridge averages 78 lamps per pixel. The direct light's memory was measured inert on RT-24's ghost (HANDOFF.md:45-48). What is left of RT-22's moving-light trail sits mostly in the direct light's history (HANDOFF.md:237-243).

The design is sound at tens of lights per pixel and does not scale past that. Selection, hit shading and both light grids grow linearly with light count; every emitter exists twice; area lights are points except in their highlight; the light code is copied eight times; and the reconstruction leans on TAA. Stale input: TEXEL-EMITTERS.md says texel emitters were built and merged on 2026-08-24, and the code confirms it (EmitterCdf, UvToSurface), so the brief's 'planned, not built' is out of date.

**What to keep.**
- The pass layout itself: the G-buffer feeds DirectTrace, which feeds the shared accumulator, and the lit shader only adds two pictures, with diffuse stored without albedo so the denoiser blurs light, never texture.
- The every-light reference arm (K=0) that matches the old loop to 0.034 levels, and --direct-signal=off as the A/B: a built-in way to check any change.
- The term-shaped selection score (measured 3-10x better than scoring by irradiance), the estimator that stays correct on average, and one ray per distinct kept lamp.
- The 16-byte cull records with upward rounding, which reject only lamps that contribute exactly zero, and the per-cell live sublists: S2 took 20-32% off the bridge frames with pictures unchanged.
- Measured change: re-lighting sampled pixels with last frame's random numbers is an exact temporal-gradient check that costs nothing when nothing changed.
- The alpha test inside traversal, only for instances that ask for it; the static/moving instance masks for bake, runtime and the loss rays.
- Per-light R2 low-discrepancy points on the source, shifted by per-pixel interleaved gradient noise, which spreads penumbra samples evenly across many lamps.
- The texel-emitter luminance tables and affine UV maps, plus RT-11's power-heuristic MIS for aimed emitter samples: the building blocks for making emissive geometry the light.
- A world-space light grid that needs no camera: the basis for a world reservoir grid (ReGIR).
- LightGlow's energy-conserving sub-pixel disc (I/(d^2 * omega)), which stops distant lamps popping (fix its stride).
- The ray and lights-per-pixel counters (RV_COUNT_*) and the debug views: the instruments every future change needs.
- No shadow maps are drawn in RT mode; the raster shadow path stays a separate, clean fallback.

**How it should look in an RT-first engine** (the inspector's view; the roadmap decides). In an RT-first engine built for AAA scale, direct lighting rests on one emitter model, one light library and one sampling structure whose cost does not grow with the number of lights.

(1) One emitter model. Light types: directional with an angular radius, sphere, tube, rectangle and disk, with spot cones and IES profiles as modifiers, plus emissive meshes as real lights (today's texel-emitter tables extended from flat panels to any mesh). All of them live in one GPU light buffer that persists between frames and tracks which lights changed. A glowing fixture is one object: its mesh is the light, or it is an analytic light whose lens is kept out of light-carrying paths, never both. Range becomes an optional artistic window instead of the only culling device.

(2) One light library. A single shader include provides, per light type, the unshadowed term, a function that picks a point on the source with its probability, and the selection score. DirectTrace, every traced hit (reflections, GI, sea, glass) and the bake all call it. Material layers (clearcoat, sheen, anisotropy) are evaluated from a material lane or a bindless material fetch, so raster and RT shade alike.

(3) Candidate generation independent of light count. A GPU-built light hierarchy (a light BVH carrying power and orientation bounds) or a world reservoir grid (ReGIR), refit each frame for moving lights. Each pixel draws a fixed number of candidates (for example 8-32), resamples them with the term-shaped score, keeps K (1-2 at low settings, up to 8 at high), traces one ray each to a sampled point on the source, and evaluates the term at that point, which is real area lighting. The same structure gives every hit a fixed-cost next-event estimate. Visibility reuse (ReSTIR) is an optional, separately measured layer, not the base.

(4) Reconstruction that does not lean on TAA. Visibility is separated from shading (the ratio of shadowed to unshadowed estimates), a spatial filter guided by the G-buffer handles young and disoccluded pixels without blurring shading detail, the temporal accumulator uses measured change as its change detector, random sequences move in every AA mode, and G-buffer lanes stay consistent under MSAA (sample zero or per sample).

(5) Robust rays and ownership. A geometric-normal lane and offsets that scale with position; the ray-tracing world (instances, TLAS) owned by a scene-level module with persistence and refits, not by shadow rendering.

(6) Baked direct light only in the raster fallback. In RT mode, once sampling cost is flat, every lamp is live and the loss rays and baked/live share logic disappear. Decide this by measurement on a live many-light benchmark (1k-10k lights, moving lights, emissive meshes) with every-light and many-ray truth arms, diff images and A,B,B,A timing.

Keep the pass layout, cull records, measured change, masks, counters and the reference-arm discipline. Reaching this means rewriting the light model and the selection/evaluation core; the pass structure around it only needs a refactor.

#### direct-01 · Light choice per pixel sweeps the whole cluster cell, so cost grows with the number of lights

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** scalability. **Scope:** rewrite.
- **Roadmap:** RT2-12
- **What is wrong.** DirectTrace's per-pixel light choice visits every light whose range box touches the pixel's cluster cell: a 16-byte cull test, a 96-byte record read, a score and K hash draws for each. Its cost therefore grows linearly with overlapping lights. Measured at 1.4-2.2 ms at 1600x900 with 27 lights per pixel; never measured beyond that. How that cost splits between the sweep and the K shades is unmeasured (RT-1 attributes K=8's cost to the shades). The direction is a candidate structure with a fixed cost per pixel, proven on a many-light benchmark that also has occluded (multi-room) lights, because neither today's score nor a light BVH/ReGIR accounts for visibility.
- **What it causes.** Per-pixel cost = (lights in the cell) x (16 B + 96 B reads + score + K hashes) + K shades + K rays. Scoring and shading are the expensive part; the rays are cheap. The garage has 27 lamps per pixel and the bridge averages 78, up to 178. At AAA light counts (a lit city street, long-range floods, hundreds of overlapping lamps) the per-pixel sweep, and the frame with it, grows linearly. RT-10's reuse made it worse by adding re-scoring.
- **Measured?** Measured: T5 garage at 1600x900, 27 lights per pixel: 2.18 / 2.16 / 1.32 ms for every light / K=8 / K=4 (RT-FIRST.md:160-165). RT-1: at K=8 the rays halved (10.9 M to 5.0 M) and DirectTrace stayed at 2.16 ms (RT-SERIES.md:1659). S4 sizing: four rays cost 1.2-1.9 ms against 11.9-17.2 ms for the lamps' rays; the choice is the cost (RAY-BUDGET-DESIGN.md:3897-3899). RT-10 branch: DirectTrace 5.5 ms at 1440p; +8 ms from re-scoring borrowed picks. Never measured beyond about 200 lights in RT mode (see direct-14).
- **Already recorded?** Partly. The per-candidate cost is recorded (T5, RT-1, S4). Light BVH / lightcuts were rejected and ReGIR deferred (RENDERING-REVAMP.md:1103-1112, 2297-2299) on the premise of about 150 lamps, which was the bridge. The AAA target makes that premise obsolete. RT-10 (ReSTIR reuse) was dropped (RT-SERIES.md:61). The linear scaling is not recorded as the wall it is.
- **Fix direction.** Make per-pixel cost independent of the light count. Two candidate structures: (a) a GPU-built light hierarchy, i.e. a light BVH: lights grouped into a tree of bounds carrying their summed power, walked stochastically per pixel in O(log N); or (b) a world-space reservoir grid (ReGIR): each frame a compute pass pre-draws a small fixed set of light samples per world cell in proportion to their importance there, and a pixel runs RIS over 8-32 of them with a known probability. RageV's WorldLightGrid is the starting point for (b). Keep the term-shaped score and one ray per kept light. Treat visibility-aware reuse (ReSTIR) as a later, separately measured step; RT-10's agenda records it was never built. Prove on a live many-light benchmark (direct-14) against the every-light reference, with diff images and A,B,B,A timing.
- **Evidence:**
  - RageVEditor/assets/shaders/direct_trace.rvshader:1171 - The reservoir branch loops over every entry of the cell list: a 16-byte cull test, ScoreDirect (reads the 96-byte GpuLight) and K random draws per candidate (1183-1192).
  - RageVEditor/assets/shaders/direct_trace.rvshader:1124 - With K=0, or a cell no longer than K, every lamp is shaded and traced.
  - RageV/src/RageV/Renderer/LightGrid.cpp:224 - A light joins every cell its range sphere's box touches; the authored range is the only thing that limits list length.
  - RageV/src/RageV/Renderer/LightGrid.h:37 - Fixed 16x9x24 = 3,456 view cells, built on the CPU.
  - docs/RT-FIRST.md:160 - T5 cost table: every light 2.18 ms, K=8 2.16 ms, K=4 1.32 ms at 27 lights per pixel; line 165: K=8 costs what every light costs.
  - docs/RAY-BUDGET-DESIGN.md:3897 - Four rays a pixel cost 1.2-1.9 ms; 'the choice is the whole cost'.
  - docs/RENDERING-REVAMP.md:2297 - Light BVH / stochastic lightcuts rejected because 'at N~150 the grid+CDF wins'.
  - docs/ROADMAP.md:310 - Clustering is 'a loss where every light reaches the whole scene - the busiest cell then holds all of them'.
  - docs/RT-SERIES.md (branch wip/2026-09-21-rt10-and-sea):3382 - RT-10's reuse cost +8 ms in the garage at 1440p with no extra rays: the time went to re-scoring 24 borrowed picks a pixel; the fused DirectTrace was 5.5 ms.
- **Skeptic's note.** The code matches. DirectTrace's reservoir branch walks every entry of the pixel's cluster cell (direct_trace.rvshader:1171-1193). For each entry it does a 16-byte cull test, runs ScoreDirect (which reads the 96-byte GpuLight, 492-578) and makes K hash draws (1183-1192). The list is bounded only by each light's range box (LightGrid.cpp:224-226) over the fixed 16x9x24 cells (LightGrid.h:37-40). So per-pixel selection cost grows linearly with the lights in range; that follows from the construction. The measurements cited are right (RT-FIRST.md:160-165; RT-SERIES.md:1659). Corrections: (1) RAY-BUDGET-DESIGN.md:3897-3899 ('the choice is the whole cost') describes the water's lamp lever before T5: choosing fewer lamps is what saves the time. It does not measure what scoring costs. RT-1's own reading is that K=8's remaining cost is 'the eight full shades ... not a cheaper score' (RT-SERIES.md:1659). How the time splits between the sweep and the shades has never been measured. (2) RT-10's +8 ms of re-scoring exists only on the dropped, parked branch (RT-SERIES.md:61); it is not a live cost. (3) RT-10's three stages were built and measured further from the truth. What was never built is a correct weight for a pick whose visibility changed, and reprojection by the struck object (RT-SERIES.md:61), so 'never built' overstates. (4) RENDERING-REVAMP.md:1103-1112 names exactly this condition for revisiting ('thousands of independent emitters'), so the AAA target reopens a deferral the docs anticipated. (5) Neither the current score nor the proposed light BVH or ReGIR accounts for occlusion. Where light ranges cross walls, a fixed candidate count still spends the K rays on hidden lamps, so the benchmark must include that case. Frame-time growth past about 30 lights per pixel is inferred.

#### direct-02 · Hits in the reflection, bounce and sea-mirror passes walk every light in the scene

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** performance. **Scope:** patch.
- **Roadmap:** RT2-6, RT2-12 (The world grid at hits is an owner-judged arm (exact only on average))
- **What is wrong.** In the trace-only passes (reflections, bounce, sea mirror), every hit reads every light's 16-byte cull record, because the cell and world-grid lookups compile only into the lit shader. Restoring them saves the out-of-range reads: about 2 ms at Pier by S2's estimate, nothing in the garage. The in-range weighing that dominated the bridge's hit cost stays until a fixed-cost light structure exists (direct-01). The change must stay exact for the measured-change re-lights.
- **What it causes.** Per-hit cost grows with the total light count, in the passes that carry most hits. Invisible in the garage (30 lights); about 191 records per hit on the bridge; unusable at thousands of lights. RT-11's one-light sampling does not help, because weighing the lights still visits them all.
- **Measured?** Not measured since the passes moved; no 'lamps per hit' figure is recorded after RT-4. Earlier numbers: ~40 ms whole-scene walk at Headland; ~2 ms at Pier for out-of-grid walks (RAY-BUDGET-DESIGN.md:2566, 3735-3738).
- **Already recorded?** No. WR-10, S2 and S4 are recorded as done; this regression is not.
- **Fix direction.** Patch now: change the guard to exclude only RV_IRRADIANCE_FILL (the bake must keep walking every baked lamp) instead of RV_TRACE_ONLY, so every trace pass reads the world grid (no camera needed) or the view cell. It is exact, so the acceptance test is a diff of zero plus the RV_COUNT_HIT counters. Then serve hits from the same light structure as direct-01, so next-event estimation at a hit costs a fixed amount.
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2851 - `#ifndef RV_TRACE_ONLY` guards WR-10's cluster lookup and WR-16 S4's world-grid lookup for a hit.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2937 - The `#else` branch runs `for (int i = 0; i < hitLightCount; ++i)`; line 2808 sets hitLightCount = u_Scene.LightCount; line 2944 reads a cull record per light.
  - RageVEditor/assets/shaders/reflection_trace.rvshader:46 - Defines RV_TRACE_ONLY; so do rtgi_trace.rvshader:40 and water_trace.rvshader:43.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:4760 - The world grid's buffers are bound on LampSet, the trace passes' set 0, so the lookup could run there.
  - docs/RAY-BUDGET-DESIGN.md:2566 - The whole-scene hit walk was ~40 ms of Headland's 114 ms before WR-10.
  - docs/RAY-BUDGET-DESIGN.md:3730 - S2: a hit with no cell walks 191 records; about 2 ms at Pier from those walks alone.
- **Skeptic's note.** The code matches. WR-10's view-cell lookup and S4's world-grid lookup in TraceSurface sit inside #ifndef RV_TRACE_ONLY (pbr_fragment.glsl:2851-2936), and the #else branch walks all u_Scene.LightCount lights with a cull-record read each (2937-2946). reflection_trace (46), rtgi_trace (40) and water_trace (43) define RV_TRACE_ONLY, and the grid buffers are bound on LampSet (Renderer3D.cpp:4756-4762). The guard dates from 224b273 (2026-09-03), so the bounce always walked every light; mirror hits started doing so when RT-4/S5 moved them into trace-only passes. Corrections: (1) The ~40 ms Headland figure (RAY-BUDGET-DESIGN.md:2566) covered the whole hit lighting, including a shadow ray per lamp; it was not the out-of-range walk. WR-10's cell lookup itself measured null on Headland because the ~146 lamps actually in range were the cost (RENDERING-REVAMP.md:959-970). (2) Since S2, an out-of-range lamp costs one 16-byte read and a range test; S2's diagnosis put those walks at about 2 ms at Pier (RAY-BUDGET-DESIGN.md:3724-3742). RT-11's weighing of the in-range lamps remains either way. (3) RT-MEASURED-CHANGE.md:289 relies on the full walk for the reflection and bounce re-lights, so the patch must also be shown exact there (the reflection-change map must stay black on a parked camera). In the garage the cost is negligible (30 lights).

#### direct-03 · Every emitter exists twice: a light that lights and a glowing mesh that is seen. Paired fixtures count twice; unpaired ones light nothing directly

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** architecture. **Scope:** rewrite.
- **Roadmap:** RT2-11 (Glowing objects with no light become lights, judged first (D7))
- **What is wrong.** A fixture authored as an analytic light plus an emissive mesh is counted twice. On glossy surfaces the analytic highlight and the traced image of the lens add together. On diffuse surfaces the live direct light and the lens emission stored in the bake add together (the garage's Half-bake tubes). An emissive mesh on its own is never a direct light (accepted by the owner in RT-22), and only 16 emitters get aimed samples. The size of the double count is unmeasured. RT-7's single-counting rule and WR-9's contract were designed and never built.
- **What it causes.** On a smooth floor the tube's reflection is the analytic highlight plus the traced image of the bar: two separately tuned copies of one emitter. The bar's emission also lights surfaces through the bounce, on top of the light's direct light. Energy is not conserved, and brightness depends on two knobs. RT-22's moving glowing cube lights nothing unless a light rides with it. At AAA scale (signs, screens, windows) only 16 emissive meshes are sampled at all, chosen by scene order.
- **Measured?** Not measured as a double count. The only measurement against linking the light to its mesh (RT-7, 2026-09-14) was taken under the DistributionGGX ceiling that RT-21 removed a week later, so that negative result has expired. RT-21's record itself notes the brighter lamp reflections on the floor once the analytic highlight was uncapped.
- **Already recorded?** Partly. NEXT.md:353-357 ('the lens and the light are two objects pretending to be one'); WR-9's contract (RENDERING-REVAMP.md:906-918); RT-7's rule that a pixel taking the analytic term must never also get the lens emission from a ray (RT-SERIES.md:141). Neither the double count after RT-21 nor the expired rejection is recorded.
- **Fix direction.** One representation per emitter, general enough for any glowing object. Make emissive geometry the light that the direct pass samples: extend the existing texel-emitter luminance tables and affine maps from flat Planes/Quads to arbitrary meshes, and put them into the light structure of direct-01, with no separate analytic twin. Where an analytic light is kept for cost, exclude its fixture from reflection and NEE paths, or weigh the analytic term against the traced lens by MIS (WR-9's roughness split). Judge against a 16-ray truth render of an emissive-only scene.
- **Evidence:**
  - RageV/src/RageV/Renderer/Light.h:55 - Light types are Directional, Point and Spot only; there is no mesh or rectangle light.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:3120 - SetAreaEmitters keeps the first 16 emitters in scene order (break); no ranking by importance.
  - RageVEditor/assets/shaders/direct_trace.rvshader:771 - DirectTrace walks the directional lights and the cell's lights only; it never samples an emitter.
  - RageVEditor/assets/shaders/reflection_trace.rvshader:840 - Reflection rays see the emissive bar and weigh it against an aimed sample of it (RT-11).
  - RageVEditor/assets/shaders/rtgi_trace.rvshader:383 - GI's next-event estimation picks one of up to 16 emitters uniformly.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:6251 - color = ambient + Lo + emissive: Lo carries DirectTrace's analytic specular (5583-5588) and the traced reflection is composited as well (6232-6248).
  - docs/RT-SERIES.md:58 - RT-7: linking the light to its glowing mesh was rejected because 'the light's highlight cannot draw a mirror (DistributionGGX's 1e-4 ... cap)'.
  - docs/RT-SERIES.md:72 - RT-21 (2026-09-21) removed that cap and recorded brighter, tighter lamp reflections on the wet floor.
  - docs/RENDERING-REVAMP.md:906 - WR-9's single-counting contract (exactly one path per regime) was designed and never built.
  - docs/RT-SERIES.md:2087 - RT-22: 'a glowing object alone lights nothing'; the 16 emitter slots are filled by the tube bars.
  - docs/HANDOFF.md:193 - 2026-09-22: 'letting the lamp draw the tube's reflection' retired as 'not general: a glowing cube has no lamp'.
- **Skeptic's note.** The code matches. Light types are Directional, Point and Spot only (Light.h:55-60). DirectTrace samples only directional and cell lights (direct_trace.rvshader:771-853). Reflection rays see the bars and weigh them against an aimed sample (reflection_trace.rvshader:829-860). The lit pass adds DirectTrace's analytic specular through Lo (pbr_fragment.glsl:5583-5588) and the traced reflection as well (6224-6249). RT-7's rule (RT-SERIES.md:141) and WR-9's single-counting contract (RENDERING-REVAMP.md:906-918) were never built. The diffuse double count is concrete as well. The bake's hit shading adds the struck surface's emission: TraceSurface sets Emissive for every hit (pbr_fragment.glsl:3177), ShadeTraced returns it (3326), and irradiance_fill.rvshader:279 stores it. The garage's tube lights are Half bake (showroom.rage:9553), so their direct light is live in DirectTrace while their bars' emission sits in the field. Corrections: (1) 'Every emitter exists twice' overstates it; only fixtures authored as a light plus a glowing mesh do. (2) 'A glowing object alone lights nothing' was ruled by design by the owner during RT-22 (RT-SERIES.md:2087-2090). It is an accepted limitation to revisit, not an unknown defect. (3) Emitters past the 16-slot cap are still found by lobe and hemisphere rays, only without aimed samples (Renderer3D.h:63-75). (4) The rejection after RT-21 (HANDOFF.md:193) rests on generality ('a glowing cube has no lamp'), which the proposed mesh-as-light fix respects. The size of the double count is unmeasured.

#### direct-04 · The direct light is denoised as shaded light, over time only; the comment says TAA carries the rest

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-24 (Radius from the penumbra, not from G-buffer edges)
- **What is wrong.** Only the direct light's own temporal accumulator reconstructs it. There is no spatial pass (the young blur measured worse on hard shadow edges); the specular half has a 4-frame memory that leans on TAA to converge; and young texels show the raw K-sample estimate. RT-22's moving-light trail lives in this history. The direction is to separate visibility from shading (the planned ratio form, never built), with any spatial filter sized by penumbra width rather than stopped at G-buffer edges alone.
- **What it causes.** Noise under motion and at disocclusions depends on TAA, against the owner's no-reliance rule. The remaining moving-light trail (RT-22) lives in this history. Every cut in K made for cost shows up directly as noise.
- **Measured?** Measured: blur on / off: 5.76 / 2.71 levels on the wall while moving; twin memory 64 to 4: wall 2.71 to 1.66 (RT-FIRST.md:170-171). RT-22: floor beside the moving light 11.1% off by >16 levels with tubes as points, 19.5% with lengths (HANDOFF.md:237-243).
- **Already recorded?** Partly. The ratio estimator is planned (RT-FIRST step 2, R8) and not built. The reliance on TAA is stated in a code comment and not filed against the owner's rule.
- **Fix direction.** Reconstruct visibility apart from shading. Keep the unshadowed term, or its K-sample estimate, sharp per pixel, and denoise only the shadowed-to-unshadowed ratio: spatially with a G-buffer-guided filter (edges stopped by depth, normal and id; radius from history length and variance) and over time, with measured change as the change detector. Measure against the every-light converged truth under motion, in every AA mode.
- **Evidence:**
  - RageV/src/RageV/Renderer/Renderer3D.cpp:7341 - DirectSignal(): YoungRadius 0, with the comment 'the temporal average and TAA carry the noise instead'; PairMemory 4 for the specular half.
  - docs/RT-FIRST.md:170 - The young blur smeared hard shadow edges (5.76 vs 2.71 levels while moving), so it was turned off; the specular twin's memory dropped to 4 frames.
  - docs/RT-FIRST.md:57 - The plan was one visibility ratio per pixel (Heitz's ratio estimator).
  - docs/RENDERING-REVAMP.md:1711 - R8's design: denoise the lit/unshadowed ratio and multiply the analytic lighting by it. Never built.
  - docs/HANDOFF.md:237 - RT-22: after measured change, the direct light's history holds the bulk of a moving light's trail; 11.1% to 19.5% of the floor once tubes have length.
- **Skeptic's note.** The code matches. DirectSignal has YoungRadius 0, with the comment 'the temporal average and TAA carry the noise instead', and PairMemory 4 (Renderer3D.cpp:7336-7355). The moving radius and firefly clamp stay at their defaults, which are off (Renderer3D.h:347-359). The ratio estimator (RT-FIRST.md:57; RENDERING-REVAMP.md:1711-1720) was superseded by T5's shaded-light signal and never built. RT-22's trail sits mostly in this history (HANDOFF.md:237-243). Corrections: (1) The signal has its own temporal accumulator with a neighbourhood bound and measured change, so the owner's own-accumulator rule is met. What leans on TAA is the convergence of the 4-frame specular half and of young or disoccluded texels. (2) The measured moving error after the contract is small in the garage: 0.34/0.70/1.66/1.13 levels on floor/car/wall/poles at K=4 (RT-FIRST.md:174). (3) The fix as written repeats a measured negative. A spatial filter that stops at G-buffer edges does not stop at a shadow edge on a flat surface, which is exactly what T5 measured, 5.76 against 2.71 levels (RT-FIRST.md:170). A spatial pass on the visibility ratio needs a radius tied to penumbra width (the shadow ray's occluder distance and the source size). (4) The exact unshadowed sum the ratio form multiplies by needs the full light sweep, which pulls against direct-01.

#### direct-05 · Area lights are points except in the highlight's shape and the shadow sample; no rectangle lights; the sun is hard

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** image-quality. **Scope:** refactor.
- **Roadmap:** RT2-10
- **What is wrong.** Sized lights are points for diffuse, cone and range. The tube's shadow ray samples a uniform point along its length, and that 0/1 answer scales both the centre-based diffuse and the highlight, so highlights near a partial occluder come out biased and change every frame. There are no rectangle or disk types, and the sun is hard (recorded as deferred). The diffuse error is small at the garage floor's 4.3 m (about 6%) and large only within a metre or two of long sources.
- **What it causes.** For the garage's 3.07 m tubes, a surface 1 m below the middle is lit 1.83x too bright by the point model compared with a uniform line source of the same power (ratio sqrt(d^2+a^2)/d, with a = 1.535 m), 1.43x at 1.5 m and 1.12x at 3 m; points under the tube ends come out too dark. Sun shadows are knife-hard at any distance from the occluder. Reflections and GI hits see every lamp as a point. Rectangular luminaires, screens and windows cannot be lit correctly.
- **Measured?** Not measured in the engine. The ratios are the closed form for a uniform line source against a point of equal power.
- **Already recorded?** Partly: NEXT.md:330-357 ('every light in this engine is punctual', planned at about a week); ROADMAP.md:673 ('no penumbra'). Neither the hard sun nor the point-diffuse approximation is filed.
- **Fix direction.** The pass already traces one ray to a sampled point on the source. Evaluate the term at that same point, with the sample's probability, which gives an unbiased area-light estimate (solid-angle sampling for spheres, segment or rectangle sampling for tubes and panels). Or use an analytic integral for the unshadowed term (LTC, which fits area-light shading to a warped cosine, or the line integral) with the ray supplying visibility. Add rectangle and disk types and a sun angular radius. Measure against a many-ray truth render with diff images.
- **Evidence:**
  - RageVEditor/assets/shaders/direct_trace.rvshader:371 - 'The diffuse, the cone, the range and the shadow keep the centre'.
  - RageVEditor/assets/shaders/direct_trace.rvshader:346 - Windowed inverse-square falloff measured from the light's centre.
  - RageVEditor/assets/shaders/include/ray_shadow_trace.glsl:392 - The tube's shadow point is uniform along its length, and its 0/1 answer multiplies the centre-based term.
  - RageVEditor/assets/shaders/include/ray_shadow_trace.glsl:345 - A directional light always takes the hard ray: no angular size.
  - RageV/src/RageV/Renderer/Light.h:102 - 'Specular only ... the diffuse difference sits under the range window's own cut': an unmeasured claim.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:3093 - At a traced hit every lamp is a point with a hard ray to its centre.
- **Skeptic's note.** The code matches. Diffuse, cone and range are taken from the light's centre (direct_trace.rvshader:346-360); the comment at 371-374 still says the shadow keeps the centre too, which has been stale since RT-7's along-tube ray. The tube's shadow point is uniform along its length, and its 0/1 answer multiplies the centre-based term (ray_shadow_trace.glsl:392-403; direct_trace.rvshader:1275-1281). Directional lights take the hard ray (ray_shadow_trace.glsl:345-346). Traced hits use the centre and hard rays (pbr_fragment.glsl:3078-3095). The ratios 1.83/1.43/1.12 are the correct closed form for an isotropic line of equal intensity. Corrections: (1) Those ratios apply 1-3 m from a tube. The garage floor is about 4.3 m below the tubes (showroom.rage:9540, y 4.3), where the error is about 6% (about 13% at the car roof), and the 60/88-degree spot cones cut off the near field above and beside the tubes. (2) The hard sun is recorded as a deferred later stage (ENGINE-NOTES.md:5551-5554), and analytic area lights are planned (NEXT.md:330-357). (3) The finding misses one consequence: the same length-uniform 0/1 visibility also scales the highlight, whose visible part is the reflected segment. A partial occluder near one end, such as a pole, darkens a highlight that is fully visible and re-rolls it every frame.

#### direct-06 · The same light maths is copied by hand in eight places, and each new light feature reaches only some of them

- **Verdict:** confirmed. **Severity:** medium. **Kind:** tech-debt. **Scope:** refactor.
- **Roadmap:** RT2-10
- **What is wrong.** The range window, spot cone, sized-source highlight and water lobe are typed out again in eight places across three files.
- **What it causes.** RT-7's tube length reached the lit loop and DirectTerm but not hit shading or the sea's fallback, so a tube seen in a reflection or a GI hit is a point with a hard shadow. Clearcoat, sheen and anisotropy exist in only one copy (direct-10). Every future feature (rectangle lights, IES profiles, the sun disk) multiplies the work and the drift.
- **Measured?** Not measured; inferred from code.
- **Already recorded?** Partly. The tube length missing at hits was noticed in the 2026-09-22 session; the duplication as such is not filed.
- **Fix direction.** One light library: a single GLSL include with, per light type, the unshadowed term, a source-point sampler with its probability, and the score. DirectTrace, hit shading, the water, glass and the bake all use it. Acceptance: the every-light reference arm diffs to zero against today's picture before any feature is added.
- **Evidence:**
  - RageVEditor/assets/shaders/direct_trace.rvshader:329 - DirectTerm (329-458) and ScoreDirect (492-578), each with its own falloff and cone.
  - RageVEditor/assets/shaders/include/water_lamps.glsl:192 - ShadeLamp (192-287) and ScoreLamp (293-404): radius handling, no SourceLength.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:4655 - The WR-17 pre-loop is 'copied rather than shared so the loop's own bits stay exactly what they were'.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:4878 - The in-loop S4 sampler's copy of the falloff and cone.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5102 - The lit loop's copy.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2988 - The traced hit's copy: no radius, no length, a hard ray.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5193 - Comment says the length rides Params.z; the code reads Extent.x: drift between copies.
- **Skeptic's note.** Every cited copy exists: DirectTerm and ScoreDirect (direct_trace.rvshader:329-458, 492-578); ShadeLamp and ScoreLamp, with no length (water_lamps.glsl:192, 293); the WR-17 pre-loop, 'copied rather than shared' (pbr_fragment.glsl:4652-4690); the in-loop S4 score (4868-4895); the lit loop (5102-5242); and the hit copy, with no radius, no length and a ray to the centre (2971-3096). The drift is real. The lit loop's comment says the length rides Params.z and that 'a spot cannot be a tube', while the code reads Extent.x and the garage's tubes are spots (5190-5198). Light.h:98-99 and Renderer3D.cpp:4058 say the length runs along Direction, while the code uses the fixture's local X (Extent.yzw = Axis, 4060). direct_trace.rvshader:371-374 says the shadow keeps the centre. Four of the eight copies (the WR-17 pre-loop, the S4 score, ShadeLamp and ScoreLamp) run under the shipping settings only on the transparent path (see missed direct-s4). The live drift is therefore between DirectTerm/ScoreDirect, the lit loop and the hit copy. Not measured; inferred from the code.

#### direct-07 · Random draws move only while TAA jitters, so in MSAA, FXAA or no AA the direct accumulator averages a frozen sample

- **Verdict:** confirmed. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-7
- **What is wrong.** The WR-15 rule (hold stochastic choices still when no temporal filter exists) predates the per-signal accumulators. The direct signal now has its own accumulator in every AA mode, yet its lamp picks and soft-shadow points hold still unless TAA's jitter is on. The accumulator therefore averages the same sample every frame.
- **What it causes.** In the deliberate MSAA 4x mode, and in FXAA or no AA, the direct light keeps a fixed K-sample and gradient-noise pattern that never converges. That is reliance on TAA by construction. The fix was written on the parked branch for the sea only and never reached main, and main's docs do not mention it. The wide penumbrae of the 3.07 m tubes (since 2026-09-22) have not been checked in these modes.
- **Measured?** Branch note: 'It does not show as dots on land ... checked on the garage and the pier', taken before the tubes had length. Not re-measured on main.
- **Already recorded?** Only on the parked branch (RT-10 agenda item 1), not on main.
- **Fix direction.** Animate the draws wherever the signal's accumulator runs (the branch's water fix, applied to every DirectTrace variant and to the record and re-light keys). Check with the flicker protocol and a converged still under MSAA and no AA against the every-light reference.
- **Evidence:**
  - RageV/src/RageV/Renderer/Renderer3D.cpp:8096 - TraceDirectLight sets Animated only when the jitter is non-zero; the record (8266) and the sea's pass (7986) use the same test.
  - RageVEditor/assets/shaders/direct_trace.rvshader:1163 - The reservoir salt is 0 when not animated, so the lamp picks are fixed per pixel.
  - RageVEditor/assets/shaders/include/ray_shadow_trace.glsl:356 - Soft-shadow points (and the along-tube point, 396-398) move only under the jitter.
  - docs/RT-SERIES.md (branch wip/2026-09-21-rt10-and-sea):302 - 'On land the lamp picks still stay frozen when TAA is off. Only the water got that fix.'
  - docs/RT-SERIES.md:2331 - MSAA/SSAA speckle 'which only TAA's frame average takes away' (reflections, same rule).
- **Skeptic's note.** TraceDirectLight, TraceDirectWater and RecordDirectChange set Animated only when the jitter is non-zero (Renderer3D.cpp:8097, 7987, 8267), and the jitter is non-zero only under TAA (FrameGraphBuilder.cpp:716-725). The reservoir salt is 0 otherwise (direct_trace.rvshader:1163-1164). The soft-shadow and along-tube points use RV_TRACE_ANIMATED, which defaults to the same jitter test (ray_shadow_trace.glsl:24-26, 356, 397). The direct signal's accumulator runs in every AA mode, because directSignal has no TAA term (FrameGraphBuilder.cpp:1622-1624). Even the sea's pass on main uses the jitter test; the fix exists only on wip/2026-09-21-rt10-and-sea (branch RT-SERIES.md:302), and main's docs do not record the gate. A caveat on the fix: once the draws move without TAA, the specular half's 4-frame memory (Renderer3D.cpp:7351) will show frame-to-frame K-sample noise. That is the WR-15 blinking (ray_shadow_trace.glsl:288-297), so the change needs its own memory or reconstruction for non-TAA modes, measured under the flicker protocol.

#### direct-08 · The baked-lamp loss check traces one extra ray per baked lamp that has any moving object in range, outside the K budget

- **Verdict:** confirmed. **Severity:** medium. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-12
- **What is wrong.** The flag is set per lamp, not per pixel. One car inside a 600 m lamp's range makes every static pixel in that lamp's reach trace toward it, whether or not the car lies anywhere near the pixel-to-lamp line, and this walk is not part of the sampled K.
- **What it causes.** With traffic or crowds under baked lamps, rays per pixel grow with (lamps with a mover in range): tens per pixel on a bridge-like layout, unbounded at AAA scale. Most of those rays are wasted.
- **Measured?** Not measured; inferred from code.
- **Already recorded?** Not as a cost risk; ENGINE-NOTES asserts the cost is small.
- **Fix direction.** Either feed the loss candidates into the same sampler as the live lamps, or test the pixel-to-lamp segment against the movers' bounding boxes before tracing. Separately, once selection cost no longer grows with light count (direct-01), measure 'every lamp live, K-sampled' against 'FullBake + loss rays' on the bridge with moving cars: in RT mode, baking direct light for speed may no longer be needed.
- **Evidence:**
  - RageVEditor/assets/shaders/direct_trace.rvshader:1322 - For static pixels in the field, every baked lamp flagged MovingInRange gets a DirectTerm and a moving-only ray (1336).
  - RageV/src/RageV/Scene/Scene.cpp:3083 - MovingInRange means any moving object's box is inside the lamp's whole range sphere.
  - RageV/src/RageV/Renderer/LightGrid.cpp:235 - Such lamps rejoin the live sublist everywhere within their range.
  - docs/ENGINE-NOTES.md:13988 - The claim that 'only the handful with a car under them ever trace toward it'.
- **Skeptic's note.** For static pixels in the field, every baked lamp with bit 13 set (a non-static object inside its range) gets a DirectTerm and a moving-only ray, outside K (direct_trace.rvshader:1299-1340). The flag is set per lamp, from its range sphere against object boxes (Scene.cpp:3068-3092), and a flagged lamp rejoins every live list within its range (LightGrid.cpp:235-237). Keying on 'not Static' rather than 'moved this frame' is correct: non-static objects are not in the bake, so a parked car's shadow must still be traced. Two additions to the finding: (1) a Full-bake directional light is flagged by any non-static object anywhere (Scene.cpp:3069-3070), which adds one ray to every static field pixel; (2) even when no lamp is flagged, every static field pixel in a cell that holds any baked lamp walks the whole cell's cull records to find out (the holdsOwned gate at 1297-1303, the walk at 1322-1327). That is a per-pixel cost proportional to the list length on bridge-like layouts. Unmeasured; ENGINE-NOTES.md:13988-13989 asserts the cost is small on the bridge.

#### direct-09 · Under MSAA the G-buffer's normal, albedo and packed id lanes are averaged across the two surfaces at every edge

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-21
- **What is wrong.** Under MSAA, a supported mode but not the project default, the G-buffer lanes DirectTrace reads are box-averaged at edges. That was a deliberate choice recorded for SSR/SSAO, never re-checked once the direct light and the packed id lane began reading those lanes. Edge pixels are therefore lit with a blended normal and blended roughness/occlusion, and at static/moving boundaries possibly with the wrong baked/live split.
- **What it causes.** Wrong direct light on edge pixels, exactly where MSAA is meant to help: rims, sparkles, a wrong baked/live split at edges. The other G-buffer signals read the same inputs.
- **Measured?** Not measured. Validation layers pass because the resolve is legal.
- **Already recorded?** No.
- **Fix direction.** Resolve the G-buffer lanes with SAMPLE_ZERO like depth (an integer format for the id lane forces it), or keep a per-sample path at edges. Test on an MSAA edge fixture against --direct-signal=off.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:479 - Under MSAA the scene target, G-buffer lanes included, is multisampled.
  - RageV/src/Platform/Vulkan/VulkanCommandList.cpp:157 - Every colour attachment resolves with VK_RESOLVE_MODE_AVERAGE_BIT; depth resolves SAMPLE_ZERO (191).
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:4506 - The id lane packs a signed id (Static in the sign) with floor(roughness*65535) + occlusion; the normal lane is octahedral-encoded (4496).
  - RageVEditor/assets/shaders/direct_trace.rvshader:733 - The pass decodes roughness, occlusion and the Static flag from those lanes.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5586 - The lit pass reads the direct pair once per pixel for all of its samples.
- **Skeptic's note.** The code matches. All colour attachments resolve with AVERAGE (VulkanCommandList.cpp:149-159) and depth with SAMPLE_ZERO (180-192). The G-buffer lanes are extra colours of the multisampled scene target (FrameGraphBuilder.cpp:479, 588, 604-607), with the id lane in R32G32_SFLOAT (82). DirectTrace decodes the normal, the packed roughness and occlusion, and the Static sign from those resolves (direct_trace.rvshader:721-735). Corrections: (1) Averaging the surface lane is a recorded, deliberate decision (ENGINE-NOTES.md:4957-4964, 'What still averages, deliberately'), so 'already recorded: No' is wrong. It was made for SSR/SSAO, before the lane fed the direct light and before T5/RT-1 packed the id lane, and it was never revisited for those consumers. (2) The Static sign can flip only where a static and a non-static surface share a pixel. Averaging packed values can also carry the integer part's half into the occlusion fraction. (3) MSAA is not the project's AA mode (TAA, per SampleProject.rvproject); MSAA runs under TAA only with --msaa. Impact unmeasured.

#### direct-10 · With the direct signal on, clearcoat, sheen, anisotropy and diffuse wrap drop out of every analytic light

- **Verdict:** confirmed. **Severity:** medium. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-21
- **What is wrong.** The G-buffer has no lanes for these features and DirectTrace does not fetch the material, so the RT path shades them as a plain GGX material. Raster mode still has them.
- **What it causes.** Car paint (clearcoat), fabric (sheen) and brushed metal (anisotropy) look different in raster and RT, and lose their character in RT, against the owner's 'raster and RT on a par'. Latent today: no SampleProject material uses them.
- **Measured?** Not measured; inferred from code. No .rmat or .rage in SampleProject sets Clearcoat, SheenColor or Anisotropy.
- **Already recorded?** Partly: T5 records only the wrap (RT-FIRST.md:137); RT-2.1/2.2 list the lanes for a deferred resolve.
- **Fix direction.** Have DirectTrace fetch the material through the G-buffer id (bindless material table plus a UV lane), or add a compact material-feature lane; implement the layers once in the shared library (direct-06). Check parity against --direct-signal=off on a fixture with each feature.
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5321 - Anisotropic GGX, clearcoat (5362) and sheen (5377) exist only in the lit loop; so does wrap diffuse (5501).
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:4644 - Under the direct signal the lit loop walks no light.
  - RageVEditor/assets/shaders/direct_trace.rvshader:439 - DirectTerm: isotropic GGX and Schlick Fresnel; no coat, sheen or wrap.
  - RageV/src/RageV/Renderer/Material.h:265 - Comment claims 'a clearcoat is a clearcoat in a reflection too'; TraceSurface's hit specular (pbr_fragment.glsl:3131-3143) has no coat.
  - docs/RT-FIRST.md:137 - Only the coat's wrap is recorded as approximated.
- **Skeptic's note.** Anisotropy, clearcoat, sheen and diffuse wrap exist only in the lit loop (pbr_fragment.glsl:5316-5383, 5496-5502), and that loop walks no light under the direct signal (4643-4644). DirectTerm is isotropic GGX with Schlick Fresnel (direct_trace.rvshader:439-456). The hit specular has no coat (pbr_fragment.glsl:3132-3143), although Material.h:265-266 says a clearcoat is a clearcoat in a reflection too. Only the wrap is recorded (RT-FIRST.md:137), and RT-2.1 lists the lanes a deferred resolve would need. This breaks the ground rule 'Raster and RT kept on a par' (RT-FIRST.md:211). Latent: no SampleProject material sets these features; showroom_car_ext_carpaint_inst.rmat has none.

#### direct-11 · Shadow rays start along the normal-mapped normal, offset by a fixed 2-10 mm

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-21, RT2-41 (Geometric normal, scale-aware offsets, camera-relative positions; the rest of large-world precision)
- **What is wrong.** DirectTrace offsets shadow rays by a fixed 2-10 mm along the normal-mapped normal, because the G-buffer has no geometric normal; the old loop used the vertex normal. Measured harmless in the garage. The risk is inferred for strongly normal-mapped surfaces under grazing light and for worlds several kilometres from the origin.
- **What it causes.** On strongly normal-mapped surfaces under grazing light, rays leave along a tilted normal, giving self-shadowing or light leaks at the shadow terminator. Far from the origin (float spacing is about 1 mm at 8 km) 2 mm is a couple of float steps, and positions rebuilt from depth are noisier than that, so acne appears in open worlds. Fine at garage scale.
- **Measured?** Measured small in the garage: T5 parity against the loop 0.034 levels mean (RT-FIRST.md:153). Open-world scale not measured.
- **Already recorded?** No.
- **Fix direction.** Add a geometric (face) normal lane and scale-aware offsets (based on position magnitude, as in Waechter and Binder's method), with camera-relative reconstruction. Verify with an acne fixture placed far from the origin.
- **Evidence:**
  - RageVEditor/assets/shaders/direct_trace.rvshader:721 - p.N is the G-buffer's shading normal, and DirectVisibility (474-485) passes it as the ray's geometric normal.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:4490 - o_Surface stores the normal after the normal map.
  - RageVEditor/assets/shaders/include/ray_shadow_trace.glsl:240 - The origin is lifted 0.002*(1+min(slope,4)) m along that normal.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:1298 - The old loop used the interpolated vertex normal.
  - docs/HANDOFF.md:7337 - GI acne: position-reconstruction error beat the 2 mm lift at grazing angles.
- **Skeptic's note.** The code matches. The G-buffer stores the normal after the normal map (pbr_fragment.glsl:4490-4496), and DirectVisibility passes it as the normal the ray is offset along (direct_trace.rvshader:474-485, 721), with a fixed 2-10 mm lift (ray_shadow_trace.glsl:238-252). The old loop used the vertex normal (pbr_fragment.glsl:1296-1305). Corrections: (1) Measured harmless in the garage, whose surfaces are normal-mapped: T5 parity against the loop was 0.034 levels (RT-FIRST.md:153). (2) The acne cited at HANDOFF.md:7337 was a half-resolution texel-centre bug, fixed by snapping, and DirectTrace already snaps (direct_trace.rvshader:680-684). (3) ENGINE-NOTES still says the offset runs along the geometric normal (ENGINE-NOTES.md:5547-5550), which is no longer true for this pass. The precision risk far from the origin is inferred; across the bridge (about ±1.35 km) float spacing is about 0.2 mm.

#### direct-12 · The world light grid is a fixed 8x4x32 box shaped for the bridge, and both light grids are rebuilt on the CPU every frame for every view

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-12
- **What is wrong.** The world light grid's fixed 8x4x32 split is tuned to the bridge and stretched over every light's reach, and both light grids are rebuilt with a sort on the CPU for every view every frame. Negligible today and growing with light count. Its current consumers are the lit shader's hits and the direct re-light; the trace passes read no grid (direct-02).
- **What it causes.** A scene whose lamps run along x gets only 8 cells along its length; a 10 km world gets cells a kilometre wide, so hits read long lists (direct-02). CPU cost is O(lights x cells) per view per frame. Tuning to one scene breaks the owner's rule.
- **Measured?** Measured at design time on the bridge: 16x4x16 gave at most 93 and on average 46.8 lights per cell; 8x4x128 gave 85 and 39.5 (LightGrid.h:145-150).
- **Already recorded?** The bridge-specific shape is documented as intent, not as a defect.
- **Fix direction.** Replace it with direct-01's GPU-built, scene-adaptive structure (sparse or hashed world cells, or a light BVH), built from a persistent light buffer that tracks which lights changed.
- **Evidence:**
  - RageV/src/RageV/Renderer/LightGrid.h:155 - 'Shaped to the lights rather than square: this scene's lamps run along a bridge, 2,700 m in z'.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:349 - The same constants are hard-coded in the shader.
  - RageV/src/RageV/Renderer/LightGrid.cpp:281 - One grid stretched over the bounds of every light's range.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:4315 - The world grid (and the view grid, 4289) is rebuilt with a sort every frame, per view, on the CPU.
- **Skeptic's note.** The code matches. The world grid is 8x4x32, shaped for the bridge (LightGrid.h:155-161; pbr_fragment.glsl:349-351), and stretched over the lights' range bounds (LightGrid.cpp:279-303). Both grids are rebuilt with a sort in every BeginScene, that is, for every view (Renderer3D.cpp:4289, 4315). A shape tuned to one scene goes against the owner's no-scene-tuning rule. Corrections: 'hits read long lists (direct-02)' does not apply, because the trace-only passes read no grid at all. The world grid's live consumers are the lit shader's own hits and DirectTrace's re-light (direct_trace.rvshader:796-809). The CPU cost is measured negligible at current light counts (Renderer3D.cpp:4308-4314); its growth at thousands of lights is inferred.

#### direct-13 · RayShadows owns the whole engine's ray-tracing world and rebuilds it from scratch every frame inside the shadow code

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-35, RT2-17
- **What is wrong.** The ray-tracing instance list is rebuilt inside shadow rendering from a full CPU walk of every mesh, for every view every frame: asset lookups, material resolution, reference-counted copies, re-packing, a registry check and a hash per instance. There is no persistent instance table and no dirty tracking; the GPU TLAS itself is already refit. Emissive meshes are forced to report candidates on every ray, and folded identities can let an aimed ray pass through another listed emitter.
- **What it causes.** At AAA instance counts this adds to the known CPU wall, the per-frame transform walk. Every emissive mesh adds shader work to all traversals that cross it. Folded ids let light leak through other fixtures that share a fold in scenes with thousands of entities.
- **Measured?** Not measured for the TLAS path. Known: at large object counts the per-frame transform walk is the CPU wall (task brief; ENGINE-NOTES).
- **Already recorded?** Partly: the transform-walk wall is recorded. The TLAS ownership, the forced candidates and the id folding in the pass-through test are not.
- **Fix direction.** A scene-level ray-tracing world module: persistent instance table, dirty tracking, TLAS update or refit. Exclude the aimed fixture with a dedicated mask bit or the stepped-out sample point (AreaEmitter::HalfThickness) rather than by forcing candidates. Store full 32-bit identities in a uint lane.
- **Evidence:**
  - RageV/src/RageV/Scene/Scene.cpp:2862 - Scene::RenderShadows clears and re-adds every mesh and terrain chunk each frame, then builds (2941).
  - RageV/src/RageV/Renderer/RayShadows.cpp:434 - BuildTopLevelAS from scratch every frame.
  - RageV/src/RageV/Renderer/RayShadows.cpp:288 - Every mesh with emissive > 1 is forced non-opaque, so any ray crossing it runs the candidate shader.
  - RageV/src/RageV/Renderer/RayShadows.cpp:443 - With measured change on (the default), every instance's transform is hashed every frame.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:5657 - Instance identity folded modulo 1021.
  - RageVEditor/assets/shaders/include/ray_shadow_trace.glsl:108 - The aimed-emitter pass-through compares folded identities, so any emitter that shares the fold is also skipped.
  - RageV/src/RageV/Renderer/ShadowMap.h:153 - Each light's ray kind (to infinity, or to a point) is read from the shadow-map assignment table.
- **Skeptic's note.** Part of this is wrong. The TLAS (top-level acceleration structure, the index of every object that rays test against) is not rebuilt from scratch every frame. VulkanAccelerationStructure::Build refits it in place (MODE_UPDATE) whenever the instance count is unchanged, and rebuilds it every 64th refit (VulkanResources.cpp:1284-1330). What is real: Scene::RenderShadows clears and re-adds every mesh and terrain chunk every frame, with asset lookups, parameter resolution and reference-counted copies per instance. Per its own comment, every view rebuilds that list (Scene.cpp:2862-2941). Every instance is re-packed, checked against the registry (VulkanResources.cpp:1250-1275) and hashed for measured change (RayShadows.cpp:443-466). Emitters are forced to report candidates (RayShadows.cpp:288-289), which costs a record read and a fast exit per crossing. Identities fold modulo 1021 (Renderer3D.cpp:5655-5657), but only the 16 or fewer listed emitters carry RAY_INSTANCE_EMITTER (5604-5624), so a fold collision can only let an aimed ray through another listed fixture, not through arbitrary entities. The CPU cost is unmeasured; the ownership point (a scene-wide structure living inside shadow code) stands.

#### direct-14 · There is no live many-light ray-traced benchmark

- **Verdict:** confirmed. **Severity:** medium. **Kind:** missing-capability. **Scope:** patch.
- **Roadmap:** RT2-2
- **What is wrong.** The ray-traced direct path has only been measured in the garage (30 lights) and on the bridge, where the lamps are mostly baked.
- **What it causes.** No many-light redesign can pass the owner's measurement rule, and regressions like direct-02 stay invisible.
- **Measured?** n/a.
- **Already recorded?** Partly (RT-1's note).
- **Fix direction.** Build the benchmark first: 1k-10k realtime lights of mixed sizes and ranges, moving lights, emissive meshes and a scripted camera path, with an every-light (K=0) arm, a many-ray truth arm and A,B,B,A timing.
- **Evidence:**
  - docs/RT-SERIES.md:1661 - 'the win the design promised for 78-light pixels waits for a live-lit many-light scene' (the bridge's lamps are baked).
  - docs/ROADMAP.md:310 - The only 256-light numbers are from raster clustered forward.
  - docs/RT-FIRST.md:112 - The garage: 30 lights, 23 casting.
- **Skeptic's note.** The docs confirm it. RT-1's promised win 'waits for a live-lit many-light scene' (RT-SERIES.md:1661), the 256-light numbers are raster only (ROADMAP.md:310), and the garage has 30 lights (RT-FIRST.md:112). One note: SampleProject already holds local256.rage and lights256.rage (256 realtime, shadow-casting point lights with 9 m range) from the raster clustering benchmark. No doc records them being run under ray tracing, so they are a starting point, not the 1k-10k benchmark with truth arms the finding asks for.

#### direct-15 · Too many settings, and dead fallback paths, around the direct light

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** tech-debt. **Scope:** refactor.
- **Roadmap:** RT2-13, RT2-3
- **What is wrong.** Many lamp and shadow flags, switch bits packed into float lanes, and fallback copies (the sea's private choose/shade, RT-9's direct allocation skipped at Quality) add maintenance and compile surface. The brightness sort no longer serves any early-exit walk and breaks WR-17's station-order borrow. The S4 in-loop sampler and WR-17 thinning are still the transparent path's lamp loop, so they cannot be deleted before that path moves onto the shared code.
- **What it causes.** Every change must keep dead arms compiling, and the HANDOFF records that silently swallowed shader-compile failures are 'the most expensive defect here' (HANDOFF.md:274). The unused sort makes cell order undefined for lamps of equal brightness.
- **Measured?** RT-9: +0.15 ms for a coin flip at K=4 (RT-SERIES.md:60). The shared sea path costs 1.43 ms against the private pair's 1.21 (EngineConfig.h:626-631).
- **Already recorded?** Partly: RT-1 said the old instruments come out; RT-9 records its own inert result.
- **Fix direction.** Delete measured-inert arms after one last measurement; keep one fallback per signal and log it loudly; move switches out of uniform lanes into a settings table; order cell lists by a documented key.
- **Evidence:**
  - RageV/src/RageV/Renderer/Renderer3D.cpp:1739 - water_choose and water_shade are always compiled, although WaterDirect is on by default (EngineConfig.h:632).
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:4785 - The S4 in-loop sampler (to 4978) and WR-17's thinning with its borrowed visibility (4650-4712, 5425-5454) are still in the lit shader.
  - docs/RT-SERIES.md:25 - 'They come out in RT-1 rather than rot beside the new path'.
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1817 - RT-9's direct allocation is on by default, skipped at Quality, and measured as 'a coin flip at four' (EngineConfig.h:469-476).
  - RageV/src/RageV/Renderer/Renderer3D.cpp:4118 - RayRates.w and WorldGridScale.w (4351-4355) carry switches and ablation bits packed into float lanes.
  - RageV/src/RageV/Renderer/LightGrid.cpp:195 - Cell lists sorted by brightness (unstable std::sort) for an early-stopping walk that no longer exists; WR-17's borrow (pbr_fragment.glsl:4705-4711) assumes index order.
- **Skeptic's note.** Mostly accurate. water_choose and water_shade are always compiled although WaterDirect is on (Renderer3D.cpp:1737-1757; EngineConfig.h:632). RT-9's direct allocation is skipped at Quality (FrameGraphBuilder.cpp:1809-1818). Switches are packed into float lanes (Renderer3D.cpp:4111-4125, 4351-4355). There are about 25 distinct lamp and direct-light flags (49 names counting aliases). The brightness sort (LightGrid.cpp:194-215) serves no early-exit walk, since none exists in the shaders now, and it contradicts both LightGrid.h:57-63 and WR-17's station-order borrow (pbr_fragment.glsl:4705-4711). Correction: the S4 in-loop sampler and the WR-17 thinning are not dead. They are the lamp loop of every transparent surface except the nearest glass pane, where directSignal is false (pbr_fragment.glsl:4597-4613, 4804-4808, 5425-5454). Deleting them first requires moving that path onto the shared light code (missed direct-s4). Also, tied lamps sort in an unspecified but deterministic order, not an undefined one.

#### direct-16 · K independent reservoirs cost K random draws per candidate and sample with replacement

- **Verdict:** confirmed. **Severity:** low. **Kind:** performance. **Scope:** patch.
- **Roadmap:** RT2-12
- **What is wrong.** The per-candidate cost is multiplied by K, and a dominant lamp can win several reservoirs, so K=8 may keep only a few distinct lamps. There is no stratification.
- **What it causes.** The sweep costs more than it needs to, and the estimate is noisier than a stratified pick at the same K.
- **Measured?** K=8 costs about the same as shading every light at 27 candidates (RT-SERIES.md:1659). Not measured in isolation.
- **Already recorded?** No.
- **Fix direction.** Do one sweep that accumulates the running total, then make K stratified picks from it (systematic resampling), or use a K-sized reservoir; compare noise at equal cost. Mostly superseded by direct-01.
- **Evidence:**
  - RageVEditor/assets/shaders/direct_trace.rvshader:1183 - For each candidate, K PCG hashes decide K independent reservoirs.
  - RageVEditor/assets/shaders/direct_trace.rvshader:1264 - Duplicate picks share one ray but still count as separate samples.
  - docs/RT-SERIES.md:1659 - K=8 DirectTrace 2.16 ms, the same as every light.
- **Skeptic's note.** Each candidate draws K PCG hashes (direct_trace.rvshader:1165-1192). The reservoirs sample with replacement and duplicate picks share a ray (1264-1275), which is unbiased but not stratified. At K=8 and 27 candidates that is 216 hashes per pixel. T5 and RT-1's measurements (K=4 1.42 ms, K=8 2.16 ms, every light 2.18 ms) are consistent with the per-candidate draws being a sizable part of the sweep, but nothing isolates them. The picks are also white noise per pixel (a BudgetHash of the pixel, 1169), while the engine moved its soft-shadow points to interleaved gradient noise plus R2 precisely because white noise 'reads as salt and pepper' (ray_shadow_trace.glsl:299-308). One uniform draw per candidate, with K stratified offsets, keeps every reservoir unbiased at the cost of one hash.

#### direct-17 · LightGlow binds 80 bytes per light against a 96-byte light record

- **Verdict:** confirmed. **Severity:** low. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-4
- **What is wrong.** Since RT-7 grew GpuLight, the glow pass reads the last sixth of the lights outside its bound range.
- **What it causes.** Undefined behaviour. With robustness off it happens to read the right memory; with robustness on, or under GPU-assisted validation, the last lamps lose their glow or read garbage. On by default (PostSettings LightGlow = true).
- **Measured?** Not measured; inferred from code.
- **Already recorded?** No (HANDOFF.md:1305 records GpuLight's move to 96 bytes).
- **Fix direction.** Bind count x sizeof(GpuLight) and share the struct through one header; fix or delete the no-op.
- **Evidence:**
  - RageV/src/RageV/Renderer/LightGlow.cpp:240 - SetStorageBuffer(1, lights, 0, lightCount * 80u).
  - RageVEditor/assets/shaders/light_glow.rvshader:57 - GpuLight is declared with six vec4s (96 bytes), including Extent.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:50 - static_assert(sizeof(GpuLight) == 96).
  - RageV/src/RageV/Renderer/LightGlow.cpp:229 - The 'redo the division' line multiplies by Max(1u, 1u), a no-op.
- **Skeptic's note.** LightGlow binds lightCount*80 bytes (LightGlow.cpp:240) against a 96-byte GpuLight (light_glow.rvshader:57-67; Renderer3D.cpp:50), so the last sixth of the lights are read outside the bound range. No robustness feature is enabled (there is no robustBufferAccess in Platform/Vulkan), so today it happens to read the right memory. The 'redo the division' line multiplies by Max(1u, 1u), a no-op (LightGlow.cpp:229). It is called every frame (Renderer3D.cpp:5074).

#### direct-18 · The AO/GI ray budget's screen mean drops the first row and column on odd sizes and double-counts the last

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-6 (It moves pixels, so it is an owner-judged arm)
- **What is wrong.** The AO/GI budget's screen mean is built from 2x2 boxes that sit half an output texel off (the floor of the centre coordinate), at every size, so rows and columns are skipped or counted twice depending on rounding; the header's 'exact box' claim is false. Outside the direct-lighting area; impact unmeasured.
- **What it causes.** The mean that scales every tile's AO/GI rays is biased toward the bottom and right edges by a few percent, so the total rays drift from the setting. It does not affect lamp choice.
- **Measured?** Not measured; inferred from code.
- **Already recorded?** No.
- **Fix direction.** Use ceil sizes and weight partial footprints, or do the reduction in a single compute pass.
- **Evidence:**
  - RageVEditor/assets/shaders/tile_reduce.rvshader:13 - The header claims 'an exact box' so the mean is not 'weighted toward one edge'.
  - RageVEditor/assets/shaders/tile_reduce.rvshader:61 - Taps are clamped to the last texel.
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:4159 - Each level halves with floor, so odd sizes shift the 2x2 footprints.
- **Skeptic's note.** The defect is real, broader than stated, and outside this area. tile_reduce takes its 2x2 box at floor(uv*size), with uv at the output texel's centre (tile_reduce.rvshader:52-61), which lands at 2i+1 give or take float rounding. So on even sizes too, the first row or column can be skipped and the last read twice (clamped); only power-of-two sizes are deterministic, and those are deterministically shifted. Each level halves with floor (FrameGraphBuilder.cpp:4155-4160). This pass sizes AO/GI rays, not lamps; the direct allocation uses ReflectionBudget (FrameGraphBuilder.cpp:1836-1850). Impact unmeasured.

#### direct-s1 · Tube highlights gain energy with length: only the radius is normalized, and nothing has been compared with a real tube since RT-21

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-10
- **What is wrong.** The representative-point capsule (the highlight is aimed at the point of the tube nearest the mirror ray) gives every surface point whose mirror ray passes near the tube roughly the peak a point light of the same intensity would give. Energy is renormalized for the radius widening, but not for the length. So the highlight's total energy grows with the tube's length, roughly by the length's angular size over the lobe width, while the same light's diffuse, taken from the centre, does not.
- **What it causes.** The garage's 3.07 m tube highlights on the wet floor and the car could carry several times the energy their own light implies (inferred). This comes on top of direct-03's traced copy of the same fixture, and it enlarges any sampling noise in that highlight (direct-s2). Since the cap was removed and the lengths restored, no one has compared the analytic tube with a brute-force one; the 16-ray truth harness varies only the reflection rays, so it cannot show this.
- **Measured?** Not measured. The only comparison (RT-7, before RT-21 and before the lengths were restored) found the highlight too dim because of the cap.
- **Already recorded?** No. RT-7 and RT-21 are recorded separately; the missing length normalization is not.
- **Fix direction.** Repeat RT-7's brute-force comparison: integrate a single tube over a glossy plane with many rays, at several roughnesses, and diff the images. If it confirms, add the length term in the one light library (direct-06) or move the unshadowed term to an integral form. Do this together with direct-03's single counting, so the floor's tube image is counted once and at the right energy.
- **Evidence:**
  - RageVEditor/assets/shaders/direct_trace.rvshader:398 - specScale = (alpha/widenedAlpha)^2, built from the angular radius only (385-399). The tube branch aims H at the segment point nearest the reflection ray (375-384), and no term accounts for the length.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5241 - The lit loop's capsule has the same radius-only normalization (5198-5242).
  - RageV/src/RageV/Renderer/Renderer3D.cpp:4037 - Color.a is the light's Intensity, not scaled by length (Extent carries the length, 4060). The diffuse uses this intensity at the centre (direct_trace.rvshader:346-368).
  - docs/RT-SERIES.md:58 - RT-7's only comparison against a brute-force tube was taken under the DistributionGGX cap and found the highlight far too dim.
  - docs/RT-SERIES.md:72 - RT-21 removed that cap on 2026-09-21, raising sharp peaks by up to about 3000x.
  - docs/HANDOFF.md:178 - The garage's tubes got their 3.07 m length back on 2026-09-22 (showroom.rage:9548-9549).

#### direct-s2 · The analytic tube highlight is an untested second source for RT-24's moving tube-reflection noise

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** stability. **Scope:** patch.
- **Roadmap:** RT2-0, RT2-24 (The split arm; the structural fix)
- **What is wrong.** Each tube's reflection on the floor exists twice (direct-03). One copy is the traced image, reprojected by its virtual image and composited after TAA. The other is DirectTrace's analytic highlight: re-sampled every frame (lamp picks and the along-tube visibility point), averaged over four frames by surface reprojection even though a highlight does not stay at one surface point when the eye moves, then filtered by TAA. RT-24's bisection has so far switched reflection-side and frame-filter arms only.
- **What it causes.** Wherever the analytic streak is a large part of the tube reflection, its noise and lag under camera motion land on exactly the pixels the owner reports, and a fix on the reflection side would leave them. If direct-s1 holds, the streak is brighter than it should be, which makes this more likely (inferred).
- **Measured?** Partly: the direct light's memory was measured inert on the pole ghost (HANDOFF.md:45-48). Not measured for the tube noise.
- **Already recorded?** No. The tube noise is filed as open with no source attributed.
- **Fix direction.** Run one RT-24 arm on spin_measure's swing test with DirectTrace's specular half zeroed (or its tube-light terms removed), to split the tube noise between the two copies before changing anything else. Structurally: count the fixture once (direct-03), and keep the analytic highlight out of a lagging history. The ratio form in direct-04 evaluates the unshadowed highlight exactly every frame and accumulates only visibility.
- **Evidence:**
  - docs/HANDOFF.md:63 - Open and not started: 'tube-light reflections very noisy while moving' (63-66).
  - docs/HANDOFF.md:45 - The direct light's memory was measured inert only on the pole ghost (45-48); no arm has isolated it for the tube noise.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:7338 - DirectSignal is a Diffuse-kind contract (reprojected by the surface), with PairMemory 4 for the specular half (7348-7351).
  - RageVEditor/assets/shaders/include/ray_shadow_trace.glsl:396 - The along-tube shadow point is re-drawn every frame under TAA (396-399), and its 0/1 scales the whole highlight (direct_trace.rvshader:1275-1281); the lamp picks are re-salted every frame too (direct_trace.rvshader:1163).
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5586 - The analytic specular enters Lo before the frame filter, while the traced reflection of the same bars is composited after it (6224-6233).
  - SampleProject/Source/Orbiter.cpp:7 - RT-24's instrument orbits the car at 11 m, so the eye moves and view-dependent highlights slide across the floor.

#### direct-s3 · Marking which lights have a moving object in range costs lights x non-static meshes on the CPU, per view, every frame

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** scalability. **Scope:** patch.
- **Roadmap:** RT2-12
- **What is wrong.** The per-lamp 'moving object in range' flag is computed for every light, including realtime and Half-bake lights that never read it, by testing each light's range sphere against every non-static mesh box. The work is single-threaded and repeated for each view.
- **What it causes.** The cost is proportional to lights times dynamic objects. At AAA counts, for example 5,000 lights and 5,000 dynamic props, characters and vehicles, that is tens of millions of box tests per view per frame (inferred), adding to the known CPU wall. Negligible today: 30 lights x 151 non-static meshes in the garage, and the bridge scene has no non-static meshes.
- **Measured?** Not measured.
- **Already recorded?** The mechanism is described (ENGINE-NOTES.md:13978-13989); its CPU cost is not.
- **Fix direction.** Compute the flag only for Full-bake and Hybrid lamps. Bin the mover boxes into the world light grid's cells (or a small tree over movers) and flag only the lamps listed where a mover sits. Build it once per frame, not per view. Measure it on the many-light benchmark (direct-14).
- **Evidence:**
  - RageV/src/RageV/Scene/Scene.cpp:3080 - For every light, a loop over every non-static mesh box until one is in range, then over the non-static terrains (3080-3092).
  - RageV/src/RageV/Scene/Scene.cpp:2124 - Every mesh not flagged Static, and every skinned mesh, joins m_MovingBounds when the draw list is refreshed (2124-2127).
  - RageV/src/RageV/Scene/Scene.cpp:4969 - MarkMovingLights runs inside Scene::OnRender, once per view.
  - RageV/src/RageV/Renderer/LightGrid.cpp:235 - The flag is consumed only for Full-bake and Hybrid lamps, here and in the cull bit and Shadow.y (Renderer3D.cpp:121, 4206-4219; direct_trace.rvshader:1326).

#### direct-s4 · Transparent surfaces other than the nearest glass pane still light themselves with the old per-light loop

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-13
- **What is wrong.** Every blended surface except the nearest glass pane is lit by the old in-shader loop, a second, unshared many-light implementation (it also carries clearcoat, sheen and anisotropy). Inside the field it traces a ray per casting lamp. Elsewhere it K-samples lamps with draws that move only under TAA and has no accumulator. This is the code direct-15 proposes to delete.
- **What it causes.** Lamp cost on glass-heavy views grows with the cell's light count, per transparent layer (inferred). Panes behind the windscreen show lamp-choice noise that only TAA averages, and it freezes without TAA (inferred). The loop cannot be deleted until these surfaces move.
- **Measured?** RT-13: +0.45 ms at the owner's shot. The lamp loop's share of the transparent pass is not isolated.
- **Already recorded?** Partly. RT-13 records the owner's decision to keep panes behind the nearest on the old path, for layering reasons, not the many-light cost or the reliance on TAA.
- **Fix direction.** Move every surface kind onto the one light library and sampler (direct-06, direct-01) before deleting the loop. For layers with no history, measure a sampler whose noise does not need one (more candidates per pixel, or a world-space cache of pre-drawn lights) against the current path on the close-up, with diff images.
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:4597 - directSignal is true only in the opaque lit variant, or for the fragment that matches the nearest glass pane's layer depth and id (4597-4613).
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:4804 - The in-loop S4 sampler runs only where fieldWeight <= 0 and the cell holds more than 2K lamps; its salt is gated on the TAA jitter (4818-4819) and nothing accumulates it.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5425 - Otherwise the loop traces one soft-shadow ray per casting lamp, thinned by WR-17 (5425-5454). WR-17's borrow assumes station-ordered lists (4705-4711), which the brightness sort broke (LightGrid.cpp:194-215).
  - docs/RT-SERIES.md:64 - RT-13: the panes behind the nearest stay on the old path by the owner's decision; glass's own rays are 1.3 ms of the close-up's 2.0 ms transparent pass.

### Global illumination, AO, probes and baking

**State of the area.** Indirect light in RageV comes from about eleven systems that are added together in the lit shader. The realtime ray-traced bounce (rtgi_trace) is a pass between the G-buffer and the lit pass. It runs at half resolution (quarter at Low); High means 4 samples per pixel at half resolution. The old "High secretly traced at full resolution" defect was fixed on 2026-08-29, but RenderSettings.h still describes High as full resolution. Each sample casts one cosine-weighted ray and shades its hit in full: the material, a walk over every light in the scene, one kept light with a shadow ray, and the hit's emissive. Each sample also aims a shadow ray at one of up to 16 emissive rectangles. This is next-event estimation (NEE): aiming a ray at a light on purpose instead of hoping a random ray hits it. There is one traced bounce by default. The light arriving at that bounce's hit is the nearest reflection probe's irradiance, or the sky; GiBounces=2 traces one more ray instead. The output is irradiance without albedo. The shared reconstruction contract accumulates it at the trace's own resolution (64-frame memory, motion caps, the measured-change anti-lag, no young-history blur), and a joint bilateral upsample brings it to full resolution.

Beside the bounce there are:
- a baked irradiance field: hand-placed boxes holding the bounce as second-order spherical harmonics (a few numbers per cell describing how light varies with direction), plus sky visibility, plus the direct light of Full-Bake lamps; it is solved in a child bake process, with repeated sweeps adding one bounce each;
- a camera-following runtime field, 24 m across, that exists only when the GI source is Realtime;
- reflection probes, which are raster captures; their irradiance is used as the pixel's "sky" diffuse term and as the incoming light at every traced hit;
- sky IBL (image-based lighting from the sky) and a flat ambient;
- RTAO and SSAO, fed through the signal contract;
- SSGI and voxel GI, which are raster-only;
- gi_denoise, kept as a reference arm;
- an in-shader traced sky visibility, which is off.

The project sets RayTracedGiSource: Baked. So the garage and the bridge render baked GI, and the traced bounce runs only where no bake matches (the camp, or after any baked lamp changes). In baked mode, Realtime lights and moving objects give no bounce at all.

The lit shader combines the terms as (flat + probe) x sky visibility + bounce, all multiplied by AO. This double-counts whenever the bounce is traced:
- the field's sky visibility is skipped wherever the bounce has an answer;
- AO reaches 0.5 m while bounce rays reach 250 m;
- an indoor probe's irradiance already contains the room's light.

Probes are captured with no shadows under ray tracing. Every hit in a trace-only pass walks all lights, because the world light grid lookup is compiled out of those passes. The traced bounce, the bake's hits, the lit pixel and the probe capture each answer "what light arrives here" differently.

Measured cost: the bounce trace takes 2.8-3.0 ms in the garage at 1600x900, and realtime GI adds 3.6 ms to the frame against baked. The whole traced bounce is worth only +0.38 levels in the garage and +0.59 in the camp, so no test scene exercises GI strongly. At bridge scale the realtime path is both unaffordable and starved of quality: 16 emitters out of 176 lamp heads, sky-only secondary light, a cache that reaches 12 m. That is why the bridge is baked, with 8 hand-placed volumes, which is the cap.

**What to keep.**
- The RT-3/RT-3.1 arrangement for the bounce: traced from the G-buffer before the lit pass at the signal's own resolution, a guidance downsample by selection, the shared contract at that grid, and one joint bilateral upsample (RT-3.1 cut the GI chain from 0.89 ms to 0.27 ms, measured).
- The albedo-free irradiance convention (E/pi), with the lit shader multiplying by the surface's own albedo: one unit for the bounce, the field and probe irradiance.
- One hit shader (TraceSurface/ShadeTraced) shared by the bounce, the bake solve, reflections and water. Fix what it computes, but keep it shared: no second copy of hit shading.
- The emitter NEE machinery: an aiming table over emissive-map cells, the shadow ray stepped out of the fitting's shell, and emissive subtraction matched by entity identity (the RT-11 fixes were worth 11 and 17 display levels).
- Measured change (record plus an exact re-light of the same rays) as GI's anti-lag, including under camera motion: the share of a switched light's light the bounce still holds ten frames later went from 77% to 12.6%; 0.21 ms at one ray.
- The fill solver's structure: sweeps that each add one bounce, a swap pair so a sweep reads only the previous one, backface-based detection of cells buried in walls, neighbour-visibility bits, and sky visibility stored per cell so a bake follows the live sky colour. These are the right ingredients for a radiance cache's update pass.
- The bake stamp discipline (layout hash plus lighting hash, refuse on any mismatch) and the private BC6H probe container (1.5 MB instead of 12.5 MB per 512-pixel probe).
- RTAO's reconstruction: jitter-corrected positions, a slope-aware ray-origin lift, and the agreement rule between the written and geometric normals; it now runs through the contract.
- The per-tile ray budget map feeding the bounce trace.
- The GI debug views (gi-light, gi-refusal, gi-change) and the --gi-signal=off reference arm as measurement tools.

**How it should look in an RT-first engine** (the inspector's view; the roadmap decides). One model of indirect light, used by every consumer.

1. A world-space radiance cache. Camera-centred cascades of cells, fine near the camera and coarse far away. Wrap-around addressing means a camera move refreshes only the slab of cells that entered the grid. Each cell holds directional irradiance (the second-order spherical harmonics the field already uses), sky visibility, and an age or confidence value. A fixed ray budget per frame updates cells, prioritised by age, visibility to the camera and measured change. Update rays are shaded by the one hit shader and read the cache at their own hits, so bounces build up over frames the way the bake's sweeps do today. The bake becomes the same cache converged offline and saved: optional, for static far-field and low-end hardware, loaded as the cache's starting state, in the same format and read by the same code. A missing or stale bake then costs convergence time, never correctness, and Realtime lights and moving objects bounce by construction.

2. The per-pixel bounce stays a G-buffer pass at the signal's own resolution, on the shared contract (keep both). It spends about 1 sample per pixel. The ray is shaded at its first hit with direct light from the shared light sampler plus the cache for everything beyond. Rays that miss return the sky's radiance, so the pixel no longer needs a separate probe/sky diffuse term. Pixels without a traced estimate (transparent layers, far distance) read the cache's irradiance and sky visibility directly. AO applies only to cache light, never to the traced bounce.

3. Direct light at every hit (bounce, reflection, cache update, bake) goes through one world-space light grid with per-cell importance sampling. Emissive geometry is registered in the grid with its power. There is no 16-emitter cap and no walk over every light.

4. Reflection probes are captured through the traced path, with the ray structure built first, or retired to a rough-reflection fallback read from the cache. Only the distant sky counts as sky.

5. Bright outliers are handled by a relative stage in the accumulator, not by fixed clamps in scene units.

6. Raster mode keeps one fallback that reads the same cache format (the baked seed plus SSAO/SSGI). Whether voxel GI is retired is the owner's call under the raster/RT parity rule.

Every step follows the owner's protocol:
- diff images against a converged path-traced still, including a bright-sky interior and a daylit scene, because the current test scenes show GI changes of under one level;
- A,B,B,A palindromes on the garage, camp and bridge cameras.

Order of work:
1. The patches: probe-capture shadows, the world grid at trace-only hits, one GI intensity for every source, the fixed clamps replaced by a relative stage, and deleting the in-shader sky visibility.
2. The cache and the rewrite of how the lit shader combines indirect terms.
3. The bake's move to being the cache's seed.

#### GI-01 · Indirect light is baked in every shipped scene; the ray-traced bounce is only a fallback

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** architecture. **Scope:** rewrite.
- **Roadmap:** RT2-25, RT2-26 (Two layers (D4))
- **What is wrong.** The project's GI source is Baked, an owner ruling (ENGINE-NOTES 7cw). Wherever a bake matches, no traced bounce runs at all. As a result, Realtime and moving lights never bounce, and moving objects neither bounce light nor block it. The picture carries the field's cell-sized contact error that 7ct measured (5.73 against 1.48 for the traced bounce on the fixture), and a baked-lamp change swaps the GI pipeline mid-session. RT-FIRST's owner answer 4 and RT-3 call the bake a far-field fallback, but no near/far split exists. The only split ever built (rays ending in the field) was measured as the best picture and removed for cost: 8.7 ms against 7.2 ms for realtime and 3.9 ms baked on the showroom. Changing this reopens that ruling. It needs a design cheaper than the removed hybrid, priced with an A,B,B,A run.
- **What it causes.** On screen: moving and switchable lights light only what they hit directly (RT-22 records this as expected). After a lamp switch on the bridge, 10% of the lamps' light is still on the structure 79 frames later, while the GI pipeline changes underneath. Scale: at most 8 hand-placed boxes per scene, and the bridge uses all 8. Workflow: every lighting edit needs a re-bake before the picture is right.
- **Measured?** Showroom: baked 3.73 ms against realtime 7.4 ms (BAKING-ROADMAP.md:47, 2026-08-26). Garage frame: 11.39 ms at its baked setting against 14.97 ms with realtime forced (RT-SERIES.md:1896-1901). Bridge lamp switch: 10% of the lamps' light left at 79 frames, and the cause is the bake (RT-MEASURED-CHANGE.md:367-375). The rest is inferred from code.
- **Already recorded?** Partly. RT-FIRST.md:210 states the intent (baked as fallback). BAKING-ROADMAP.md:221-232 records that switching a baked light throws the bake away, accepted by the owner. RT-SERIES.md:76 records 'no bounce light from a moving light where the bounce is baked (expected)'. Nowhere is this recorded as a conflict with RT-first.
- **Fix direction.** Make the realtime path the source of truth and the bake an optional accelerator for it. The bake becomes the same world-space cache (GI-02) converged offline and loaded as the cache's starting state: same format, same reader, used for static far-field or low-end hardware. Realtime lights and moving objects then bounce by construction, and a stale or missing bake only slows convergence instead of breaking correctness. Before changing the project default, diff each scene's realtime result against a converged path-traced still.
- **Evidence:**
  - SampleProject/SampleProject.rvproject:21 - RayTracedGiSource: Baked (under RayTracedGlobalIllumination: High), so any scene with a matching bake never traces the bounce
  - RageV/src/RageV/Scene/Scene.cpp:1898 - bakedHonoured switches the traced bounce off whenever a field matches; the comment at 1884-1893 records that the traced chain was removed from Baked mode
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:913 - a Baked source that can be honoured drops the whole indirect chain: no gather, no voxel cones, no traced bounce
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2962 - the bake's solve skips lights whose mobility is Realtime (2962-2970), so a moving or switchable light has no bounce in baked mode
  - RageV/src/RageV/Scene/Scene.cpp:3738 - the bake holds static objects and nothing else
  - RageV/src/RageV/Scene/Scene.cpp:4247 - the bake's stamp does not see a moved wall or an edited material
  - RageV/src/RageV/Renderer/Renderer3D.h:87 - kMaxIrradianceVolumes = 8; GoldenGateDemo.rage (274-336, 6503-6542) already uses 8 hand-placed volumes at 5-32 m spacing
  - docs/RT-FIRST.md:210 - owner: baked probes and volumes stay as the far-field fallback for RT GI
  - docs/RT-MEASURED-CHANGE.md:367 - switching the bridge's baked lamps zeroes the field, and the scene falls back to the traced bounce mid-session
- **Skeptic's note.** The code checks out. SampleProject.rvproject:21 sets RayTracedGiSource: Baked. Scene.cpp:1898-1902 switches the traced bounce off whenever a field can be honoured, and FrameGraphBuilder.cpp:913-925 drops the whole indirect chain. pbr_fragment.glsl:2962-2970 keeps Realtime lights out of the bake's solve, and the solve traces static geometry only (ray_shadow_trace.glsl:230). kMaxIrradianceVolumes is 8 and the bridge uses all 8. 7 of the garage's 30 lights and 14 of the bridge's are Realtime. The costs match BAKING-ROADMAP.md:47 and RT-SERIES.md:1896-1901. Two things need correcting: the framing and the severity. 'Baked means baked' is an explicit owner ruling (ENGINE-NOTES 7cw, commit 4b024dc, 2026-08-26), and SampleProject's settings are deliberate owner choices. The one configuration that did split near from far (traced rays ending in the field) was built and measured as the best picture: error 0.73, against 1.48 for realtime and 5.73 for the field alone, on the fixture (ENGINE-NOTES 7ct). It was removed because it cost more than realtime: 8.7 ms against 7.2 realtime and 3.9 baked on the showroom. So this reopens a priced owner decision; it is not a hidden defect. The inspector also missed that a baked-lamp switch does more than swap the bounce. The zeroed field keeps owning every fully baked lamp's direct light (gi-s2).

#### GI-02 · No world-space light cache feeds the traced bounce; the one that exists is off, unread, and smeared when the camera moves

- **Verdict:** confirmed. **Severity:** high. **Kind:** missing-capability. **Scope:** refactor.
- **Roadmap:** RT2-25
- **What is wrong.** The engine has the pieces of a radiance cache (a store of light in world space that a ray can read at its hit instead of shading the hit from scratch): a traced solver that already produces multi-bounce light by feeding its previous answer back, and a camera-following box. They are not connected. The bounce ignores the box and uses one probe cube per region as the light arriving at its hits. The lit pass ignores the box wherever the bounce has an answer, which is every surface pixel. So when the cache is switched on, its 65K rays a frame light only forward-rendered glass. Its grid is not scrolled either: a one-metre camera move relabels every cell as one metre away without moving the data. At a 5% blend per revisit, and a revisit about every 7 frames, the wrong light takes tens of revisits (seconds) to wash out, while the code comment says a few frames. It reaches only 12 m from the camera.
- **What it causes.** Multi-bounce light in realtime mode is only as good as one probe cube: one per 42 m in the garage, and none on the bridge, where hits get the sky. The configuration the engine itself measured as its best picture is not available in realtime mode. About 0.7 ms of rays buys almost nothing when the cache is on. A moving camera would carry misplaced light. A 24 m box cannot serve an outdoor scene.
- **Measured?** Fill pass 0.75 ms (Scene.cpp:3579-3582). Commit 7954b34 (2026-08-29) calls the cache 'a 0.7 ms cost buying multi-bounce, not a saving' and 'making the pass sample the field is the step that turns that round'. Rays ending in the field gave the best picture at more cost than realtime (Scene.cpp:1884-1893). How long the misplaced light lingers is inferred from code, not measured.
- **Already recorded?** The missing wiring is stated in commit 7954b34 and was never done. The misplaced-light defect is not recorded; the code comment claims it clears in a few frames.
- **Fix direction.** Build one camera-centred radiance cache:
- wrap-around (toroidal) addressing, so a move rewrites only the slab that entered;
- several cascades for reach;
- a fixed update budget per frame, prioritised by age, visibility and measured change;
- a cheap single-fetch read for hits (today's visibility-aware field lookup costs up to 8 corners x 19 fetches).
Read it at bounce and reflection hits in place of probe irradiance, and at pixels the bounce did not cover. Use it to cut per-pixel rays. Prove the trade with an A,B,B,A palindrome and a diff against the 16-ray truth; the earlier measurement says the picture gets better.
- **Evidence:**
  - RageV/src/RageV/Renderer/RuntimeIrradianceField.h:43 - a fixed 24x12x24 cells at 1 m around the camera (43-46); 64 rays a cell, 65,536 rays a frame, hysteresis 0.05 (90-100)
  - RageV/src/RageV/Scene/Scene.cpp:3574 - the cache exists only when the source is Realtime and not Baked, so it is off in every shipped scene
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:3252 - the only read of the field's stored bounce at a traced hit is compiled for the bake (RV_IRRADIANCE_FILL); frame hits read only Full-Bake lamps' direct light (3275-3285) and discard the bounce
  - RageVEditor/assets/shaders/rtgi_trace.rvshader:615 - the light arriving at a bounce hit is the nearest probe's irradiance, never the cache (615-628)
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5704 - under the GI signal every surface pixel counts as answered, so the lit pass never reads the cache's bounce either (5761-5765)
  - RageV/src/RageV/Renderer/RuntimeIrradianceField.cpp:91 - 'the box travels, the texture stays': after a move every cell holds the light of a place one cell away (91-106)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:2988 - after the first sweep each revisit blends in only 5% (2988-2991), with the sweep counter parked at 1 (3056-3059)
  - RageV/src/RageV/Scene/Scene.cpp:1884 - record: traced rays that ended in the stored field gave 'the best picture anything here has rendered' and were rejected for costing more than realtime
  - RageV/src/RageV/Scene/Scene.cpp:3579 - the cache's fill pass measured at 0.75 ms a frame
- **Skeptic's note.** Verified. RuntimeIrradianceField.h:43-46 and 90-100 give 24x12x24 cells at 1 m, 64 rays a cell, 65,536 rays a frame and hysteresis 0.05. Scene.cpp:3574-3575 requires !WantsBakedGi(), so under the project's Baked wish the cache is off everywhere, the camp included. The only field read at frame hits keeps the fully baked lamps' direct light and discards the bounce and the sky (pbr_fragment.glsl:3275-3285). rtgi_trace.rvshader:615-628 lights bounce hits with the nearest probe. Under the GI signal every surface pixel counts as answered (5704), so 5761-5765 never reads the cache's bounce. RuntimeIrradianceField.cpp:91-106 moves the box without moving the texture, and Renderer3D.cpp:2988-2991 and 3056-3059 blend 5% per revisit, with the sweep counter parked at 1. Commit 7954b34 states the missing wiring. One caveat for the roadmap: the arrangement the fix wants, hits reading a stored field, is ENGINE-NOTES 7ct's hybrid, which was measured best and removed for cost (7ct: 'the per-hit field fetch is real money'). The cheaper lookup and the cut in per-pixel rays are the parts that must be proven.

#### GI-03 · The pixel's indirect light adds a probe/sky term and a bounce term that overlap

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-26 (`check_gi.py`'s bands updated)
- **What is wrong.** In realtime GI, the lit pixel adds the probe term at full weight beside the traced bounce. That term is a local probe's irradiance, which already contains the lit surroundings, or the sky. The field's sky fraction has only ever been applied where the bounce did not answer (pbr_fragment.glsl:5754-5765, contradicting 5736-5740). ENGINE-NOTES 7bb leaves the blocked-sky residual to AO, which reaches 0.4-0.5 m while the rays reach 250 m, and AO also darkens the traced bounce and the stored direct light. This affects the camp (its realtime mirror probe reaches 9 m over the clearing) and every realtime fallback. The garage and the bridge in baked mode are shielded by the field's sky fraction. check_gi.py's gi_skylit band (1.02-1.10) accepts the residual as correct, so that acceptance test must change with the model. Unmeasured.
- **What it causes.** Realtime GI and baked GI of the same room cannot agree, which breaks the owner's bar of near-zero difference between them. In realtime mode, interiors under a bright sky and anything inside a local probe's reach are over-lit, and sealed spaces leak sky light. Contact regions are darkened twice. No test fixture can show any of this, because all of them have black skies. The camp's GI intensity of 0.4 may be compensating for it (not measured).
- **Measured?** Not measured directly. RT-3 measured the whole traced bounce at +0.383 levels in the garage and +0.588 in the camp (RT-SERIES.md:1771-1772), so no current test scene exercises GI strongly. The rest is inferred from code.
- **Already recorded?** The model and its reliance on AO are recorded as a deliberate choice in ENGINE-NOTES 7bb (8686-8694). The comment/code disagreement at 5736-5765 and the probe-counted-as-sky double count are not recorded.
- **Fix direction.** One rule for where sky light comes from. Under a traced bounce, rays that miss return the sky's radiance (their own visibility test), and pixels with a traced estimate drop the separate probe/sky diffuse term. Where no traced estimate exists, use the cache's irradiance and sky visibility together (the field already stores both). Never add a local probe's irradiance as sky. Apply AO only to cache or field light, never to the traced bounce. Add a bright-sky enclosed fixture and a realtime vs baked vs truth diff to the acceptance tests, before and after.
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5621 - skyDiffuse is a blend of up to two reflection probes' irradiance plus the sky (5611-5625)
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5736 - comment: the field's sky fraction 'applies to every fragment over a volume however its bounce was found'
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5754 - code: the field is read and skyVisible set only where the bounce is not confident (5754-5765); under the GI signal that is nowhere
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5845 - ambient = kD*albedo*((flat + skyDiffuse)*skyVisible + irradiance)*occlusion*screenOcclusion, so AO also darkens the traced bounce and the stored direct light
  - RageVEditor/assets/shaders/rtgi_trace.rvshader:599 - a bounce ray that misses adds nothing and leaves the sky to the probe term; FrameGraphBuilder.cpp:893 sets the ray reach to 250 m
  - RageV/src/RageV/Renderer/PostSettings.h:694 - AO radius 0.5 m by default (the showroom profile uses 0.4)
  - docs/ENGINE-NOTES.md:8686 - the design's model: probe supplies sky, AO removes the sky the walls block, GI adds what the walls deliver
  - SampleProject/assets/scenes/showroom.rage:139 - the garage probe is an indoor capture with a 42 m influence; camp.rage:723 is a realtime indoor probe (9 m)
  - SampleProject/assets/scenes/irradiance_leak.rage:4 - the leak fixtures (and gi_corner, gi_away) use a black sky and zero ambient, so they cannot show a sky double count
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:3197 - ProbeIrradiance rotates every probe slot, local ones included, by the sky's rotation (1582-1587) and scales it by sky intensity
- **Skeptic's note.** The core is verified. skyDiffuse blends local probes' irradiance with the sky (pbr_fragment.glsl:5611-5625). The field's sky fraction is applied only where the bounce did not answer (5754-5765), which contradicts the comment at 5736-5740. AO multiplies the traced bounce and the stored direct light (5845-5846). Misses add nothing (rtgi_trace.rvshader:599), rays reach 250 m (FrameGraphBuilder.cpp:893), and AoRadius is 0.5 (0.4 in the showroom profile). ENGINE-NOTES 7bb records the reliance on AO as deliberate. Five corrections. (1) The code never applied the fraction where the bounce answered: the gate has existed since the comment was written (commit 2f87153), so 'no longer' is wrong. (2) Bright-sky fixtures exist: gi_skylit (grey sky) and sky_occlusion (bright sky under an eave, with a volume). check_gi.py claim 12 measures the traced lift under the grey sky, and its band (1.02-1.10) accepts the walls' bounce added over the full sky as correct. The test encodes the residual; it is not blind to it. (3) camp.rage:723 is a realtime probe on a mirror in the open camp, not an indoor probe. Its 9 m reach still covers much of the clearing, so the probe-as-sky double count applies there in realtime mode. (4) The rotation and intensity scaling of local probes is latent: every scene has SkyRotation 0 and SkyIntensity 1. (5) The +0.383 and +0.588 levels predate RT-11's NEE fix and the tube-length fix (gi-s4), so they no longer show that GI is weak. The effect is unmeasured, as the inspector says.

#### GI-04 · Reflection probes are captured without shadows under ray tracing

- **Verdict:** confirmed. **Severity:** high. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-6, RT2-25 (Capture after the TLAS is built; then capture through the traced world)
- **What is wrong.** Every probe capture under ray tracing happens before the frame's acceleration structure is built, so every shadow ray in all six faces answers 'lit'. In the probe's picture, lamps shine through the car, the pillars and their own housings. That probe then feeds: rough reflections beyond the traced gloss window, the pixel's diffuse 'sky' term, and the incoming light at every traced reflection hit (GI-05). The capture also has no AO and none of the traced signals, because it runs outside the frame graph.
- **What it causes.** Probes are systematically too bright. This is a likely contributor to the measured one-third mismatch that made cuts flash in RT-24 (34% of the frame off at a cut before fix 1). Every reflection hit and every realtime bounce hit is lit by that over-bright cube. The camp's realtime probe re-captures unshadowed 15 times a second.
- **Measured?** The brightness mismatch is measured (HANDOFF.md:35-36). That the missing shadows cause it is inferred; confirm by capturing after the structure is built and diffing the two probes.
- **Already recorded?** Code comments state that a probe capture gets zero, which means lit (Renderer3D.cpp:4136-4139, pbr_fragment.glsl:1283-1286), but it is not recorded as a defect. The mismatch is recorded in HANDOFF without a cause.
- **Fix direction.** Patch first: build the ray structure before probes are captured, or capture after RenderShadows, and diff the garage probe before and after. Later, capture probes through the traced hit-shading path so a probe shows what the rays see, or retire probes to a rough-reflection fallback read from the cache.
- **Evidence:**
  - RageVRuntime/src/RuntimeLayer.cpp:493 - CaptureReflectionProbes runs before RenderShadows (490-500); EditorLayer.cpp:562 and 578 use the same order
  - RageV/src/RageV/Renderer/Renderer3D.cpp:3786 - RayShadows::BeginFrame at the start of the frame clears the structure's Active flag (RayShadows.cpp:240-245); only RenderShadows sets it again (RayShadows.cpp:434-435)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:4134 - under rays ShadowParams.x = IsActive() ? 1 : 0, and the comment names 'a probe capture' as a zero case (4134-4141)
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:1283 - 'zero means lit'; ray_shadow_trace.glsl:39 and 234 return lit for every shadow ray when no structure is ready
  - RageV/src/RageV/Scene/Scene.cpp:3493 - a probe face is a full raster render of the scene through OnRender (3493-3499)
  - SampleProject/assets/scenes/showroom.rage:140 - the garage probe is Baked, so it was captured in the bake run in the same order; 23 of the garage's 30 lights cast shadows
  - docs/HANDOFF.md:35 - measured: the probe is about a third brighter than the traced picture (cut frame mean 65 against 49)
- **Skeptic's note.** Verified end to end. RayShadows::BeginFrame (RayShadows.cpp:240-245) is called from Renderer3D::BeginFrame at the top of every frame (Renderer.cpp:113, from Application.cpp) and clears Active. Only RayShadows::Build inside Scene::RenderShadows sets it again (Scene.cpp:2941; RayShadows.cpp:434-435). RuntimeLayer.cpp:491-501 and EditorLayer.cpp:560-582 capture probes before RenderShadows. The capture draws through OnRender (Scene.cpp:3493-3499), which builds no structure. ShadowParams.x is IsActive() ? 1 : 0 (Renderer3D.cpp:4134-4141), and TraceShadowFromMasked answers 'lit' when RV_TRACE_READY is false (ray_shadow_trace.glsl:39, 234). With RV_RAY_SHADOWS compiled, the shadow-map lookups are compiled out, so nothing else supplies a shadow. The garage probe is Baked, so its stored cube came from a bake run with the same order (the project has RayTracing: true), and 23 of the garage's 30 lights cast shadows. No doc records this as a defect. The link to the measured one-third brightness gap is correctly marked as inferred. One minor point: the camp's realtime probe captures one face per step at 15 Hz, so a full cube every six steps, not a full recapture 15 times a second.

#### GI-05 · A surface is lit four different ways depending on who is looking at it

- **Verdict:** confirmed. **Severity:** high. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-25
- **What is wrong.** The question 'what light arrives at this point' has four answers in the engine:
- the lit pixel: field, or probe + traced bounce;
- a traced hit: probe irradiance with no occlusion and no stored bounce;
- the bake's hit: the sky with no occlusion, plus the previous sweep;
- a probe capture: a raster render with no shadows (GI-04).
None of the hit paths uses the field's stored bounce, even where it exists. So the same wall has a different brightness on screen, in the floor's reflection, in a bounce and in the probe. Tube lights are lines on screen and points at hits.
- **What it causes.** Brightness mismatches between the picture and its reflections and bounces; the probe-against-traced third is one visible case. Sealed or covered spaces glow in reflections, because hits get probe or sky light with no occlusion. Every lighting fix has to be made up to four times, or it creates a new mismatch.
- **Measured?** The probe/traced mismatch is measured (HANDOFF.md:35-36). The rest is inferred from code.
- **Already recorded?** ENGINE-NOTES 7ax and 7ao accept 'one bounce, probe at the hit' as a simplification for reflections. The discarded field read and the four-way inconsistency are not recorded.
- **Fix direction.** One hit-shading contract for every consumer: direct light from the shared light sampler (GI-06, GI-08), indirect light from the radiance cache at the hit (GI-02), and sky light from occluded rays or the cache's sky visibility. The bake, the probes and the lit pixel all read or write through it. Verify with a scene where the same wall is seen directly, in a mirror and by bounce, diffed against a path-traced truth.
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:3214 - ShadeTraced, used for bounce and reflection hits, returns Direct + Diffuse*(flat + probe irradiance) + emissive (3327); it fetches the field's bounce and sky visibility and throws them away (3277-3285)
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:3197 - the probe irradiance used at a hit has no occlusion and comes from one probe per region
  - RageVEditor/assets/shaders/rtgi_trace.rvshader:211 - the bounce takes the nearest probe (211-230) while the lit pass blends two (pbr_fragment.glsl:2364-2414)
  - RageVEditor/assets/shaders/irradiance_fill.rvshader:279 - the bake's hits take the sky slot (0) with no occlusion, plus the previous sweep
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5754 - the lit pixel uses field bounce x sky visibility, or probe + traced bounce
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2988 - the hit light loop treats every light as a point (tube length ignored), while the lit path shades tubes as tubes (5198-5211)
  - SampleProject/assets/scenes/GoldenGateDemo.rage:274 - the bridge has irradiance volumes but no ReflectionProbeComponent, so every bridge hit's incoming light is the sky
- **Skeptic's note.** All four paths check out. Frame hits get the flat ambient, the fully baked lamps' stored direct light and one probe's irradiance; the field's bounce and sky are fetched and discarded (pbr_fragment.glsl:3197-3200, 3275-3285, 3327). The bounce picks the nearest probe that contains the hit (rtgi_trace.rvshader:211-230), the lit pass blends two (2364-2414), and the reflection passes take the strongest blend weight. The bake's hits take the sky slot with no occlusion plus the previous sweep (irradiance_fill.rvshader:279). The hit light loop never reads Extent, the tube length, while direct_trace.rvshader:311-320 and the raster loop shade tubes as tubes. The bridge has no ReflectionProbeComponent. The owner knows that tubes are points at hits (RT-23 notes), but the four-way inconsistency is not recorded as one problem. One wording slip: the bake's hits do read the stored bounce (the previous sweep), so 'none of the hit paths' should say 'no frame hit'.

#### GI-06 · Every ray hit in a trace pass walks every light in the scene; the world light grid is compiled out

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** performance. **Scope:** patch.
- **Roadmap:** RT2-6, RT2-12
- **What is wrong.** In every trace-only pass (the bounce, reflections, the optional water trace and the bake), the hit light loop walks every light's 16-byte cull record and, under RT-11, weighs every light in range. The cluster and world-grid lookups are compiled into the lit shader only (pbr_fragment.glsl:2851-2941), yet the C++ binds the grid and a comment claims it is read (Renderer3D.cpp:4685-4692). The cost grows with the scene's total light count. It is unmeasured in the trace passes and small in today's defaults: the bounce is off under Baked, the garage has 30 lights, and the sea's mirror rays run in the lit shader, which still uses the grid. The world grid's recorded payoff was 3-5 ms at the bridge's Pier camera.
- **What it causes.** GI hit cost grows linearly with the number of lights in the scene: 30 in the garage, about 190 positional lights on the bridge, thousands in an AAA city. It is also part of why the bounce costs about 3 ms (GI-07).
- **Measured?** About 45% of flat RTGI cost at 20 lights (RENDERING-REVAMP.md:1065-1067, measured before S2 and RT-11). The hit walk was about 5 ms on Headland and Pier before the world grid existed (HANDOFF.md:3057-3058). Its current cost inside trace passes is not measured.
- **Already recorded?** The behaviour is stated as a fact in RT-MEASURED-CHANGE.md:289. That it contradicts WR-10/S4's intent, and the comment at Renderer3D.cpp:4685, is not recorded.
- **Fix direction.** Enable the world-grid cell lookup in the trace-only TraceSurface; the buffers are already declared and bound. Measure the bounce and reflection passes with a palindrome on the garage and the bridge. Then move to per-cell importance (a cumulative light table per cell) so a hit samples lights in proportion to their contribution without walking them.
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2851 - the hit's screen-cluster and world-grid lookups sit under #ifndef RV_TRACE_ONLY; the #else branch walks all lights (2937-2941)
  - RageVEditor/assets/shaders/rtgi_trace.rvshader:40 - the bounce defines RV_TRACE_ONLY, as do irradiance_fill.rvshader:32, reflection_trace.rvshader:46 and water_trace.rvshader:43
  - RageV/src/RageV/Renderer/Renderer3D.cpp:4685 - the C++ binds the world grid to the bounce's set so that 'a hit off screen reads the cells instead of walking every light'
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:3098 - RT-11 keeps one light per hit but first weighs every light that survives the cull record (3098-3130)
  - docs/RT-MEASURED-CHANGE.md:289 - 'under RV_TRACE_ONLY a hit already walks every lamp'
  - docs/RT-SERIES.md:3409 - the plan believes WR-10's world grid at hits 'serves RT-11'
  - docs/RENDERING-REVAMP.md:1065 - measured: at 20 lights the hit walk was about 45% of the flat RTGI cost
- **Skeptic's note.** The code claim is right. The screen-cluster and world-grid lookups sit inside #ifndef RV_TRACE_ONLY (pbr_fragment.glsl:2851-2941), and rtgi_trace, reflection_trace, water_trace and irradiance_fill all define RV_TRACE_ONLY. Renderer3D.cpp:4685-4692 binds the world grid to the GI set, with a comment saying it is read. RT-SERIES.md:3409 and RT-MEASURED-CHANGE.md:289 contradict each other. Two corrections. First, the '45% of flat RTGI cost at 20 lights' (RENDERING-REVAMP.md:1065-1067) is a planning sentence written before WR-16, not a measurement of the hit walk. The 2026-08-27 measurement was full-resolution RTGI taking about 45% of the frame. The recorded hit-walk measurements are the bridge water's, taken in the lit shader: about 40 ms before S2 and 2.8-4.8 ms after (RAY-BUDGET-DESIGN.md:2566, 3875). Second, in today's defaults the cost is small. The bounce does not run under Baked. The sea's mirror rays run in the lit water shader, which still uses the grid; water_trace is opt-in. The garage's reflection hits walk 30 lights' 16-byte records with the range test. So this is a scaling and consistency problem with a cheap patch, not a measured cost today.

#### GI-07 · The realtime bounce cannot pay for the bridge

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-25
- **What is wrong.** Each bounce sample costs three things: a closest-hit ray with a full hit shade (a walk over every light's cull record and one kept light with a shadow ray), an aimed shadow ray at an emitter, and a probe fetch. Cost grows with pixels times samples, plus a walk over every light's record. Measured in the garage at 1600x900: 2.8-3.0 ms for the trace and +3.6 ms of frame against baked. Never measured on the bridge. What the bounce is worth on screen today is unknown, because the +0.38 levels predates RT-11's NEE fix. On the bridge, hits would get only sky light (it has no probes), and the NEE would aim at one merged rectangle spanning the bridge (gi-s3); the 16 cap is not what limits it.
- **What it causes.** At 1600x900 the realtime bounce adds about 3.6 ms to the garage frame and changes the picture by +0.38 levels. At 4K the trace scales roughly 4x with pixels. On the bridge it would also walk about 190 lights per hit and get only sky light at its hits. RT-first GI is unaffordable exactly where AAA scenes live.
- **Measured?** Garage and camp: RT-SERIES.md:1771-1785 and 1896-1901. Change record: RT-MEASURED-CHANGE.md:326-353. Realtime GI on the bridge has never been measured, because the bridge is baked.
- **Already recorded?** RT-3 records the cost and its open item (c): the traced bounce is worth under 0.6 levels, 'a question about the scenes'. The scaling argument is not recorded.
- **Fix direction.** Restructure the estimator:
- 1 sample per pixel at half resolution;
- rays end in the radiance cache after their first hit (GI-02);
- direct light at hits from the light grid's sampler (GI-06);
- a cache update budget fixed per frame, independent of screen size.
Measure the garage, camp and bridge cameras with palindromes and truth diffs. Add a scene where bounce light carries real energy (a bright-sky interior or a daylit garage), because the current scenes cannot show a GI change.
- **Evidence:**
  - docs/RT-SERIES.md:1779 - bounce trace 2.8-3.0 ms in the garage and 1.1 ms in the camp (1779-1785); garage frame 14.97 ms with realtime GI against 11.39 ms baked (1896-1901)
  - RageVEditor/assets/shaders/rtgi_trace.rvshader:517 - per sample: an aimed shadow ray (517), then a full TraceSurface (595) with its own light walk and shadow ray, then a probe fetch
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:376 - High is 4 samples per pixel at half resolution, Low is 2 at quarter (376-383)
  - RageV/src/RageV/Renderer/RuntimeIrradianceField.h:43 - the only world-space cache reaches 12 m from the camera
  - RageV/src/RageV/Renderer/Renderer3D.h:76 - 16 NEE emitters at most, against the bridge's 176 Hybrid lamp heads
  - docs/RT-MEASURED-CHANGE.md:326 - the bounce's change record adds 0.55 ms while the camera moves (0.21 ms at one ray, 352-353)
- **Skeptic's note.** The per-sample cost structure is right: an aimed shadow ray (rtgi_trace.rvshader:517), a TraceSurface with a light walk and one kept-light shadow ray (595), and a probe fetch (617). The costs match RT-SERIES.md:1779-1785 and 1896-1901 and RT-MEASURED-CHANGE.md:326-353, and realtime GI has never been measured on the bridge. Four corrections. (1) '+0.38 levels' was measured on 2026-09-07, before RT-11 found that every aimed NEE sample had been blocked by its own fitting (rtgi_trace.rvshader:506-518) and before the tubes got their length. It does not describe today's bounce (gi-s4). (2) 4K has 5.8 times the pixels of 1600x900, not 4 times. (3) The bridge's lamps are Light components walked at every hit: the 16-byte cull record rejects the out-of-range ones and RT-11 keeps one per hit. (4) The lamp lenses are one merged 'Lamps' entity, so the 16-emitter cap is never reached on the bridge. HANDOFF.md:4136-4141 recorded this correction after the same wrong claim was made once before. The scaling argument is inference.

#### GI-08 · Emitter sampling: 16 arbitrary emitters, picked uniformly, represented as boxes

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-11
- **What is wrong.** The bounce's emitter list holds one bounding-box rectangle per emissive entity, capped at the first 16 in registry order, and each sample picks among them uniformly; RT-11's importance pick serves reflections only. In the garage, 20 emissive tube bars plus the car's lamp parts exceed the cap. Some fittings get no aimed rays and are left to hemisphere hits, which the fixed clamp of 4 cuts. On the bridge the cap is not reached: all the lamp lenses are one entity and one rectangle (gi-s3). The cost of the uniform pick has been measured on reflections only.
- **What it causes.** More noise, and energy lost, in the bounce wherever more than a few emitters matter. On the bridge only 16 of 176+ lamp heads could be aimed at. At AAA scale (signs, windows, screens) NEE falls back to random hits, which the clamps then cut (GI-11).
- **Measured?** The uniform pick's cost is measured on reflections (RT-SERIES.md:158-162), not on the bounce.
- **Already recorded?** The 16 cap is recorded (NEXT.md:330-333). That the bounce still picks uniformly after RT-11 is not.
- **Fix direction.** Register emitters in the world light grid with their power. Pick per hit by importance (power, distance, facing) from the hit's cell. Share one sampler between the bounce, reflections and the cache update, and drop the global cap. Measure against the 16-ray truth.
- **Evidence:**
  - RageV/src/RageV/Scene/Scene.cpp:2214 - every emissive mesh brighter than 1 becomes an emitter: a rectangle taken from its bounding box, in registry order (2214-2232)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:3120 - only the first 16 are kept (3118-3121)
  - RageVEditor/assets/shaders/rtgi_trace.rvshader:383 - the bounce picks one of them uniformly (381-384)
  - docs/RT-SERIES.md:158 - RT-11 found a uniform pick 'a lottery of its own' for reflections (0.10 display levels delivered against 1.27 removed) and replaced it with importance there only
  - docs/NEXT.md:331 - AreaEmitter is NEE for the traced GI, capped at 16
- **Skeptic's note.** Verified: one rectangle per emissive entity, taken from its bounding box (Scene.cpp:2214-2245); the first 16 in registry order kept (Renderer3D.cpp:3118-3121); a uniform pick in the bounce (rtgi_trace.rvshader:381-384). RT-11 gave reflections an importance pick (RT-SERIES.md:158-162). In the garage the cap bites: 20 emissive tube bars (underground_garage_pbr_4_light.rmat, emissive up to 100) plus the car's lamp parts compete for 16 rows. Which fittings get aimed rays depends on registry order, and the rest fall to the hemisphere clamp of 4. The bridge claim is wrong. Its lamp lenses are one 'Lamps' entity (GoldenGateDemo.rage:236-244), which makes one emitter, so the cap is never reached. HANDOFF.md:4136-4141 recorded exactly this correction on 2026-09-01. The bridge's real problem is that this single rectangle radiates over its whole bounding face, which is an energy error, not a sampling one (gi-s3).

#### GI-09 · Bounce and bake hits are shaded matte only: metals and glossy floors bounce no specular light

- **Verdict:** confirmed. **Severity:** low. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-25 (Measured with RT2-21's BSDF)
- **What is wrong.** A bounce ray that lands on the chrome poles, the metallic car paint or the wet floor sees only the matte part of that surface, and for a metal that is nothing. The glossy floor's reflection of the tubes, which physically lights the ceiling and the car's underside, is missing from both the realtime bounce and the bake.
- **What it causes.** The ceiling and undersides are under-lit in exactly the garage's look (a shiny floor under tube lights). The specular term exists at reflection hits but has never been measured for the bounce.
- **Measured?** Not measured.
- **Already recorded?** The code comment names the option; no measurement exists.
- **Fix direction.** Measure the bounce and the bake with the hit's specular term, and with a directional cache lookup at the hit, against a path-traced truth of the garage ceiling. Adopt it if the diff favours it.
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2598 - RV_HIT_SPECULAR is not defined for rtgi_trace or irradiance_fill: 'giving them the term is one define each, and a re-bake to see it' (2598-2605)
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2802 - diffuse = albedo*(1 - metallic), so a metal hit returns black to the bounce
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:3327 - without RV_HIT_SPECULAR, ShadeTraced returns only Direct + Diffuse*(ambient + arriving) + emissive
- **Skeptic's note.** Verified. RV_HIT_SPECULAR is defined only outside RV_TRACE_ONLY, or by reflection_trace and water_trace themselves (pbr_fragment.glsl:2598-2605; reflection_trace.rvshader:54; water_trace.rvshader:51). Hits use diffuse = albedo * (1 - metallic) (2802), so a metal returns only its emission to the bounce and to the bake. Nothing has been measured. I lowered the severity for three reasons. The default garage and bridge run the bake, not the bounce. The claimed under-lit ceiling is inference. And turning the term on would bring in the unshadowed probe's specular (GI-04) and one kept light per hit (RT-11), so it has to be measured first, as the inspector says.

#### GI-10 · GI intensity scales the realtime bounce but not the baked one

- **Verdict:** confirmed. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-6 (Owner decides)
- **What is wrong.** A per-profile multiplier applies to one GI source and not the other. On the bridge the bake is read at 1.0x, while the realtime fallback, which takes over whenever a baked lamp switches (GI-01), runs at 2.6x. The camp would jump from 0.4x to 1.0x if it were baked. A GI multiplier other than 1 is also energy that the physics did not produce.
- **What it causes.** Visible brightness jumps whenever a scene moves between baked and realtime GI. Bakes cannot be validated against realtime. An artistic dial hides real energy errors (GI-03).
- **Measured?** Not measured.
- **Already recorded?** No.
- **Fix direction.** Apply one indirect scale to every source, or none, with 1.0 as the default. If a scene needs a multiplier, first find the missing or double-counted energy (GI-03, GI-09) with a truth diff.
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5695 - the traced or screen-space bounce is multiplied by u_Scene.Indirect.x (also at 5717)
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5763 - the field's stored bounce is added unscaled
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1132 - Indirect.x is the profile's GiIntensity
  - SampleProject/assets/scenes/bridge_cinematic.rvpostprofile:41 - the bridge profile sets GiIntensity 2.6; SampleProject/assets/post/camp_focus_8_4.rvpostprofile:24 sets 0.4
  - docs/NEXT.md:400 - acceptance bar: baked against realtime must show near-zero visible difference
- **Skeptic's note.** Verified. The traced or screen-space bounce is multiplied by u_Scene.Indirect.x (pbr_fragment.glsl:5695, 5717), which is the profile's GiIntensity (FrameGraphBuilder.cpp:837, 1131-1132). The field's stored bounce is added unscaled (5763), and the bake's solve never reads the value. The bridge uses bridge_cinematic.rvpostprofile (handle 8958722797779192383) at 2.6; the camp's profiles use 0.4. Correction to 'already recorded: No': the visible symptom is recorded. HANDOFF.md:4119-4123 says the bridge's realtime fallback 'looks like everything turning saturated red (GiIntensity 2.6 ...)', and 2.6 is a recorded suspect for compensating for under-lit lamps (HANDOFF.md:4009, 4135; RENDERING-REVAMP.md:94, 563). The asymmetry between the two sources is not recorded.

#### GI-11 · Bright outliers are cut by fixed clamps tuned on one scene; the accumulator has no outlier stage

- **Verdict:** confirmed. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-25
- **What is wrong.** Rare bright samples are handled by clamping radiance at fixed values in scene units. Those values depend on the scene's light levels and exposure, and they were tuned on the showroom. The shared accumulator's diffuse kind has neither the range-compressed averaging nor the fresh-sample firefly bound that the old GI denoiser had; the gap was filed for RT-5 and never done. The reason given for the widened bound no longer exists.
- **What it causes.** In a brighter scene (daylight, bright emissives) the clamps remove real bounce energy; in a dim one they do nothing and fireflies reach the accumulator. The bias changes from scene to scene, against the owner's rule never to tune for one scene. A bound that is too wide slows the reaction to lighting changes.
- **Measured?** The NEE clamp trade on the showroom ceiling: unclamped 63.15 mean with 90-level spikes; clamp 32 gives 62.62 and 12 (rtgi_trace.rvshader:577-582). Not measured anywhere else.
- **Already recorded?** RT-3 open item (a) (RT-SERIES.md:1829-1832; HANDOFF.md:1603). The clamps' dependence on the scene and the stale BoundWidth reason are not recorded.
- **Fix direction.** Replace the fixed clamps with a relative outlier stage in the accumulator: range-compressed accumulation, plus a bound against the fresh neighbourhood and the pixel's temporal moments. Re-measure BoundWidth on the trace grid. Validate with the unclamped 16-ray truth on the garage and on a bright scene.
- **Evidence:**
  - RageVEditor/assets/shaders/rtgi_trace.rvshader:369 - kIndirectClamp = 4.0 on every hemisphere sample (applied at 661-663)
  - RageVEditor/assets/shaders/rtgi_trace.rvshader:583 - the NEE clamp of 32 was chosen from showroom-ceiling measurements: 'nine tenths of the energy' (573-586)
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1426 - the accumulator's firefly clamp is compiled out for the diffuse kind (1426-1448)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:7870 - GI BoundWidth 6 is justified by a half-resolution upsample before the accumulate (7861-7870), which RT-3.1 removed (docs/RT-SERIES.md:1852-1861)
  - docs/RT-SERIES.md:1829 - open item (a): the contract has no range compression and no firefly bound, both of which gi_denoise had
- **Skeptic's note.** Verified. kIndirectClamp 4.0 is applied to every hemisphere sample (rtgi_trace.rvshader:369, 661-663). kNeeClamp 32 was chosen from showroom-ceiling numbers (573-586). The accumulator's firefly clamp on the fresh sample is compiled out for RV_SIGNAL_DIFFUSE (reflection_accumulate.rvshader:1426-1449). GiSignal's BoundWidth of 6 is justified by an upsample before the accumulate, and RT-3.1 replaced that with accumulating on the trace's own grid (Renderer3D.cpp:7861-7870; RT-SERIES.md:1852-1861). RT-3's open item (a) was filed for RT-5, and RT-5 closed without it (RT-SERIES.md:45, 1829-1832). One addition: because of the 16-row emitter cap, the fixed clamp of 4 also removes most of the light of any fitting left off the list; the garage's bars emit up to 100.

#### GI-12 · The baked field's storage does not scale and wastes memory

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-4, RT2-26 (The wasted solve texture; sparse streamed bricks)
- **What is wrong.** The baked field is one resident atlas holding at most 8 hand-placed volumes, with hard seams where volumes nest (recorded, NEXT.md:310-313). Every field also allocates and zero-fills a same-size solve texture whether or not it will ever solve (IrradianceVolume.cpp:214-231): about 92 MB of VRAM on the bridge (inferred). The rt and ss files are identical by an owner ruling (HANDOFF.md:5645-5654), so the disk duplication is known. What costs load and switch time is that every field creation reads both files in full (gi-s5).
- **What it causes.** About 92 MB of VRAM wasted on the bridge (inferred: a 91.8 MB field plus an equal twin) and 92 MB duplicated on disk. No way to cover a bigger world. Visible seams at the edges of nested volumes.
- **Measured?** File sizes: 91.8 MB x 2 on disk for the bridge. The VRAM doubling is inferred from code.
- **Already recorded?** The missing blend across nested volumes is recorded (NEXT.md:310-313). The twin texture and the duplicate files are not.
- **Fix direction.** If a stored field stays as the cache's baked seed (GI-01, GI-02): release the solve texture once a bake is adopted, write one file, store sparse bricks with streaming, and blend across nested boxes. Otherwise retire it into the cache's format.
- **Evidence:**
  - RageV/src/RageV/Renderer/IrradianceVolume.h:159 - 19 RGBA16F tiles per cell, 152 bytes
  - RageV/src/RageV/Renderer/IrradianceVolume.cpp:220 - the solve's swap texture is always created and zero-filled (214-231), even for a loaded bake that never solves
  - RageV/src/RageV/Scene/Scene.cpp:4161 - the same texture is written as both the rt and ss files (4156-4172); on disk each bridge file is 91.8 MB
  - RageV/src/RageV/Scene/Scene.cpp:3923 - switching the GI form recreates the field and reloads the twin file (3911-3923)
  - RageV/src/RageV/Scene/Scene.cpp:3813 - volumes past the cap of 8 are ignored; all volumes share one atlas texture and stay resident
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:1770 - nested volumes: the deepest containment wins with no blend (1770-1799); NEXT.md:310-313 says to finish blending 'before a scene leans on nested ones', and the bridge nests 5-12 m boxes inside a 32 m one
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2228 - the careful lookup reads up to 8 corners x 19 fetches
- **Skeptic's note.** Verified. Each cell holds 19 RGBA16F tiles (IrradianceVolume.h:159). A same-size solve texture is created and zero-filled for every field, including a loaded bake that never solves (IrradianceVolume.cpp:214-231). The rt and ss files are byte-identical; the bridge pair is 91,827,560 bytes each with the same MD5. A GI-form toggle recreates the field (Scene.cpp:3911-3927). Volumes past 8 are dropped (3813). Where volumes nest, the deepest wins with no blend (pbr_fragment.glsl:1767-1799). Corrections: the duplicate files are recorded and deliberate. The owner ruled that the screen flavour carries the multi-bounce field, and the pair machinery was kept 'for the day they diverge again' (HANDOFF.md:5645-5654; Scene.cpp:4047-4057; commit 4b024dc). The nested-volume seams are recorded too (NEXT.md:310-313). Two things are real and unrecorded: the always-allocated solve texture, about 92 MB of VRAM on the bridge (inferred), and the full read of the other flavour's file on every field creation (gi-s5).

#### GI-13 · Eleven indirect-light systems and their settings, most of them unused in RT mode

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** tech-debt. **Scope:** refactor.
- **Roadmap:** RT2-1, RT2-26, RT2-30
- **What is wrong.** Indirect light has many coexisting sources and settings. The owner's per-signal source rule (RT-FIRST.md section 4, answers 2 and 5) keeps a raster twin for every signal, so collapsing them is an owner decision. What is plainly wrong today is the documentation. RenderSettings.h says RTGI High is full resolution; it has been half since 7954b34. PostSettings.h says traced AO runs at the frame's resolution and SSAO Full at full resolution; the signal path runs both at half. The reference arms (the old chain plus gi_denoise, the runtime field, traced sky visibility) could move behind engine flags.
- **What it causes.** Every GI change has to be checked across a combinatorial matrix, defects hide in untested combinations, and users read settings that mean something other than what they do.
- **Measured?** Not measured.
- **Already recorded?** No.
- **Fix direction.** Collapse to two paths. RT mode: traced bounce + radiance cache + sky IBL, with AO on cache light only. Raster mode: one fallback reading the same cache format. Keep reference arms behind engine flags, not project settings. Rewrite the settings' documentation to match what the code does.
- **Evidence:**
  - RageV/src/RageV/Renderer/PostSettings.h:561 - GlobalIllumination, GiSource, GiRadius, GiIntensity, GiQuality, GiDenoise, GiBounces, VoxelGlobalIllumination and three voxel dials, AmbientOcclusion, AoRadius, AoIntensity (561-698)
  - RageV/src/RageV/Renderer/RenderSettings.h:435 - RayTracedSkyVisibility, RayTracedAmbientOcclusion, RayTracedGlobalIllumination, RayTracedGiSource (435-465)
  - RageV/src/RageV/Renderer/RenderSettings.h:455 - says RTGI High traces at full resolution; FrameGraphBuilder.cpp:376-383 makes High half resolution
  - RageV/src/RageV/Renderer/PostSettings.h:49 - says traced AO levels run at the frame's own resolution and SSAO Full is full resolution; FrameGraphBuilder.cpp:2023-2027 runs RTAO always at half and SSAO Full at half
  - RageVEditor/assets/shaders/voxel_inject.rvshader:35 - voxel GI is lit from shadow cascades; with no cascades the sun is unshadowed, and local lights are never shadowed
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:4447 - the old traced chain plus gi_denoise is kept whole as a reference arm
- **Skeptic's note.** The settings and their stale descriptions are as stated. RenderSettings.h:448-455 says RTGI High traces at full resolution, but FrameGraphBuilder.cpp:376-383 makes it half. PostSettings.h:46-50 says traced AO runs at the frame's resolution and SSAO Full at full resolution, but the signal path runs RTAO and SSAO Full at half (FrameGraphBuilder.cpp:2022-2030). The old chain plus gi_denoise is a reference arm (4443-4447), and voxel injection reads the shadow cascades (voxel_inject.rvshader:31-45). Three corrections. 'Most of them unused in RT mode' is about half: probes, sky IBL, the flat ambient, the baked field and RTAO are all live. 'Many combinations never ran' is unverified. And the fix 'collapse to two paths, one raster fallback' runs into the owner's per-signal source rule (RT-FIRST.md section 4, answers 2 and 5: RT GI off falls back to SSGI, voxel GI or probes, and raster is kept on a par with RT). The concrete, safe part is rewriting the stale descriptions and moving reference arms behind engine flags.

#### GI-14 · The field also stores Full-Bake lamps' direct light, at cell resolution

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** image-quality. **Scope:** refactor.
- **Roadmap:** RT2-26
- **What is wrong.** Fully and hybrid-baked lamps keep their far share as cell-resolution light in the field. The blur and lost bloom this causes were measured, and the owner accepted them with the 2 m Hybrid radius as the dial (HANDOFF.md:3179-3206). Unrecorded: AO multiplies the stored share (pbr_fragment.glsl:5845-5846) but not the same lamp's live share, so a lamp darkens in creases only where it is baked. And the stored share disappears whenever the bake stops matching (gi-s2).
- **What it causes.** Far lamp pools and shadows are blurry and leak. A lamp looks different when its mobility changes. The real cost problem this hides (direct light from many lights) is not solved.
- **Measured?** The 542 to 315 ms gain (NEXT.md:114-117). The image effects are not measured.
- **Already recorded?** The switch to Hybrid Full Bake is recorded. The resolution limit and the AO inconsistency are not.
- **Fix direction.** Keep direct light in the direct-light system (light sampling through the world grid) and use the cache for indirect light only. Until then, exclude stored direct light from AO.
- **Evidence:**
  - RageVEditor/assets/shaders/irradiance_fill.rvshader:296 - fully baked lamps' direct light is stored per cell, with one visibility ray from the cell centre (296-379)
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5770 - it is added at static pixels inside `irradiance` (5766-5772), so AO multiplies it (5845-5846), while live direct light is never darkened by AO (RT-2)
  - docs/NEXT.md:114 - the bridge's 176 lamps went Hybrid Full Bake at 2 m: 542 to 315 ms summed over eight cameras
  - SampleProject/assets/scenes/GoldenGateDemo.rage:277 - deck cells are 5 m apart; the bay volume's are 32 m (6511)
- **Skeptic's note.** Verified. The bake stores the direct light of fully and hybrid-baked lamps per cell, with one visibility ray from the cell centre (irradiance_fill.rvshader:296-379). The lit pass adds it inside `irradiance`, which AO multiplies (pbr_fragment.glsl:5766-5772, 5845-5846), while live direct light is never darkened by AO. The bridge's spacing is 5-32 m. Correction: the image cost was measured and accepted. The 2026-09-03 entry records Hybrid-against-live diffs per camera (mean levels from Profile 0.10 up to Deck 4.59 and Bluff 9.60) and the bloom lost under lamp heads to the 5 m cells. The owner chose Hybrid at 2 m as the dial (HANDOFF.md:3179-3206). Unrecorded: the AO asymmetry, and, more seriously, the fact that this stored light vanishes whenever the bake is invalidated (gi-s2).

#### GI-15 · Traced sky visibility traces inside the lit shader with no accumulator of its own

- **Verdict:** confirmed. **Severity:** low. **Kind:** tech-debt. **Scope:** patch.
- **Roadmap:** RT2-3
- **What is wrong.** The only ray-traced sky occlusion runs in the lit shader at full resolution and relies on TAA to converge. That breaks the owner's rule against relying on TAA and RT-FIRST's rule that every RT signal runs in its own pass. When it is on, it also stacks with AO.
- **What it causes.** It is dead code today. If enabled, it brings TAA-dependent noise and double darkening.
- **Measured?** Not measured.
- **Already recorded?** No.
- **Fix direction.** Remove it. Sky visibility should come from the bounce's own missed rays or from the cache (GI-03).
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:1437 - TraceSkyVisibility casts 2, 4 or 8 rays per pixel per frame inside the lit fragment (1437-1463); it is used at 5806-5820
  - RageV/src/RageV/Renderer/RenderSettings.h:434 - 'the temporal filter is what carries it'
  - SampleProject/SampleProject.rvproject:18 - RayTracedSkyVisibility: Off
- **Skeptic's note.** Verified. TraceSkyVisibility casts 2, 4 or 8 rays per lit fragment (pbr_fragment.glsl:1437-1463). It is used at 5806-5820 and compiled only when RaySkyVisibilityRays > 0 (Renderer3D.cpp:2016-2017). The project sets it Off. RenderSettings.h:434 says the temporal filter carries it, which breaks the no-TAA-reliance rule and RT-FIRST's answer 7 (every RT signal traced in its own pass). While it is off it costs nothing.

#### gi-s1 · Every glowing fitting is counted twice in indirect light: once as its analytic light, once as emissive geometry

- **Verdict:** added by the skeptic. **Severity:** high. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-11
- **What is wrong.** The garage's tubes exist twice. There are 20 analytic tube lights, which the direct light pass shades, and 20 glowing bar meshes, which the reflections see. The indirect-light paths cannot tell that these are the same fitting. When a bake ray hits a bar, the bar's emission is stored in the field as 'bounce'. The realtime bounce aims shadow rays at the bars and adds their light to the pixel as indirect light. Meanwhile the tube lights light the same surfaces directly. RT-7 planned the rule that prevents this: a pixel that takes the analytic light never also takes the lens emission from a ray. The owner rejected the light-to-mesh link for mirror reflections, and the rule was dropped for the diffuse paths along with it.
- **What it causes.** In the default baked garage, the field carries the bars' direct light, smeared over its 1 m cells, on top of the tubes' own direct light. The realtime bounce does the same through its aimed rays. A rough reading of the scene's own numbers says the duplicate is not small: a bar radiates up to 100 over roughly 3 m x 0.15 m, a peak intensity of about 44, against its tube light's 20. It also biases every tuning made against the picture (tube intensity, GiIntensity) and every baked-vs-realtime comparison.
- **Measured?** Not measured. The mechanism is read from code and scene data; the magnitude estimate is inferred.
- **Already recorded?** The rule was planned (RT-SERIES.md:103 and 141; RT-FIRST.md:67) and dropped when RT-7 closed (RT-SERIES.md:58). The double count in the bounce and in the bake is not recorded.
- **Fix direction.** Decide once per fitting which representation carries its light, for every consumer. The analytic light carries it in direct light and at every hit. The emissive mesh stays visible to mirror rays (the owner's RT-7 ruling) but is excluded from the diffuse bounce, the NEE and the bake wherever a light owns it, through a luminaire link that only the diffuse paths use. Measure first: bake the garage with the bars' emission excluded and diff the field-lit picture against today's.
- **Evidence:**
  - SampleProject/assets/scenes/showroom.rage:9538 - 'Tube 001', one of the 20 tube lights: Spot, Intensity 20, SourceLength 3.07, Half bake (9538-9554)
  - SampleProject/assets/scenes/showroom.rage:6147 - the tube's glowing bar mesh ('Bottom light bars'), Static, with material 15679706988273368715; 20 bars carry this material
  - SampleProject/assets/models/garage_pbr/underground_garage_pbr_4_light.rmat:3 - Emissive: [34.19, 63.08, 100, 1]
  - RageV/src/RageV/Scene/Scene.cpp:2214 - any entity whose emissive exceeds 1 becomes an emitter that the bounce aims at
  - RageVEditor/assets/shaders/rtgi_trace.rvshader:556 - the aimed sample adds the fitting's emitted light to the pixel's indirect estimate (556-587)
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:3327 - ShadeTraced adds the hit's emission, with no exclusion in the bake
  - RageVEditor/assets/shaders/irradiance_fill.rvshader:279 - the bake stores ShadeTraced's result, emission included, as the field's bounce
  - docs/RT-SERIES.md:58 - RT-7 closed after the light-to-mesh link was removed on the owner's rejection; the 'rays skip the lens emission' rule (RT-SERIES.md:141, RT-FIRST.md:67) was never built
  - SampleProject/assets/scenes/GoldenGateDemo.rage:236 - the bridge's lamp lenses (bridge_lamp.rmat, emissive [26, 15.6, 4.2]) sit beside 176 lamp lights

#### gi-s2 · A field that failed to load still owns every fully baked lamp, so a baked-lamp change removes their far light

- **Verdict:** added by the skeptic. **Severity:** high. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-6
- **What is wrong.** A field can fail to load: a baked lamp is switched, a baked light's value is edited, or a GI form has no file. It is then zero-filled but stays bound. The lighting code decides who lights a fully or hybrid-baked lamp by where the pixel sits in the volume boxes, not by whether the field holds anything. So the live direct pass and the traced hits keep skipping those lamps' far share, and the field adds zero in its place. The accepted record assumes the traced bounce rebuilds what was lost. It rebuilds bounce light only, and even its hits skip these lamps.
- **What it causes.** On the bridge, switching one of its 176 hybrid-baked lamps, or editing one baked light's colour without re-baking, removes the light of every hybrid lamp beyond about 2-3 m of its head. It is gone from every static surface inside the 8 volumes until the next bake; the sea keeps it because it is lit live. The owner's rule 'a light that switches is made Realtime' avoids the switch case but not the authoring case. The water test could not see it, because its reference arm was in the same broken state.
- **Measured?** Not measured; traced through the code. The water test's arms (RT-MEASURED-CHANGE.md:355-375) share the broken state, so its numbers neither show nor rule it out.
- **Already recorded?** No. The field reset on a baked-lamp switch is recorded (BAKING-ROADMAP.md:221-232; RT-MEASURED-CHANGE.md:367-375) with the wrong consequence.
- **Fix direction.** A field with no loaded or finished answer must own no light. Publish a 'field valid' flag, or bind no field, so fully and hybrid-baked lamps are lit live wherever the stored answer is missing. In the longer term GI-14's direction removes the dependency, because direct light never lives in the cache. Check with the bridge with one lamp switched, diffed against the unswitched frame, before and after.
- **Evidence:**
  - RageV/src/RageV/Scene/Scene.cpp:4014 - with no matching bake the field is zero-filled; only a bake run may solve it (4025-4033, 4058-4071)
  - RageV/src/RageV/Scene/Scene.cpp:4082 - fieldWanted = WantsBakedGi() || BakingLighting(): under the project's Baked wish the zeroed field stays bound (4082-4099)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:4419 - IrradianceExtents.w is the volume count whether or not the field was loaded or solved
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:1954 - IrradianceFieldWeight is the box's geometric fade, not 'the field holds an answer'
  - RageVEditor/assets/shaders/direct_trace.rvshader:463 - FieldShare = field weight x baked share; the live direct light keeps only 1 - FieldShare (497, 846 and the other call sites)
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2955 - traced hits drop the same share (2955-2960), so the bounce cannot restore it either
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5769 - the lit pass adds the stored direct light instead, which is now zero
  - SampleProject/assets/scenes/GoldenGateDemo.rage:350 - the bridge's 176 lamps are 'Hybrid Full Bake' with HybridRadius 2
  - docs/BAKING-ROADMAP.md:221 - the accepted record says that after a switch the traced bounce 'takes over and rebuilds what the field held' (221-232)

#### gi-s3 · Emitter power scales with the mesh's bounding box, so a merged mesh radiates over empty space

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-11
- **What is wrong.** In the traced bounce, an emitter's power is its radiance times the area of its bounding-box face, and an aimed point can land anywhere on that face. For a single fitting that is close to right. For a mesh that merges many small glowing parts, such as the bridge's lamp lenses or a building's windows, most of the face is empty space. That empty space is counted as glowing and unoccluded, because nothing blocks a shadow ray aimed at empty air.
- **What it causes.** On the bridge, the realtime bounce takes over after any baked-lamp change (GI-01). It then integrates the sodium colour over a rectangle roughly the size of the bridge instead of over 120-odd lenses. Every surface facing it gets orders of magnitude too much orange light, limited only by the per-sample clamp of 32, eight times the hemisphere's. This is a candidate cause of the recorded red realtime fallback, which is currently blamed on GiIntensity 2.6. Any AAA asset with merged emissive parts hits the same error.
- **Measured?** Not measured. HANDOFF records it as a suspicion worth testing; the energy argument is inferred from the estimator.
- **Already recorded?** Partly. The one-rectangle suspicion is in HANDOFF.md:4135-4141; that it inflates the power, not only the aim, is not recorded.
- **Fix direction.** Build emitters from the emissive triangles themselves, per triangle or in clusters, with their true area and power. At minimum, split an entity's emitter by connected emissive parts and reject rectangles far larger than their emissive area. Register the result in the light grid (GI-08). Check on the bridge: the realtime fallback diffed against the baked frame, before and after.
- **Evidence:**
  - RageV/src/RageV/Scene/Scene.cpp:2167 - the emitter is 'the flattest rectangle of the mesh's own bounds', described as 'exact for a plane, which is what every light fitting in a scene is' (2167-2171, 2214-2245)
  - RageV/src/RageV/Scene/Scene.cpp:2208 - radiance = emissive x the emissive map's mean; with no map the mean is 1 (Material.cpp:98)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:3125 - the area comes from the rectangle; only a degenerate one is dropped (3125-3132)
  - RageVEditor/assets/shaders/rtgi_trace.rvshader:463 - with no aiming table the point is uniform anywhere on the rectangle (463-469); the estimate multiplies by the rectangle's area (556-558), capped only by kNeeClamp 32 (583-586)
  - SampleProject/assets/scenes/GoldenGateDemo.rage:236 - one 'Lamps' entity at the origin carries every lamp lens with material bridge_lamp (emissive [26, 15.6, 4.2]); 'Beacons' at 247 likewise
  - docs/HANDOFF.md:4135 - recorded as an untested suspicion: all the lenses become one rectangle spanning the 2.7 km bridge (4135-4141); 4119-4123 records the realtime fallback turning the bridge saturated red

#### gi-s4 · The measured worth of the traced bounce predates the fix that made its aimed rays work

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-2
- **What is wrong.** The figure behind 'no current test scene exercises GI strongly' was taken when every aimed emitter ray in the bounce was blocked by its own fitting, and when the garage's tubes were points. Since RT-11 the aimed rays reach the fittings; in the garage these are bars emitting up to 100. Since 22fc27c the lights themselves have changed. Nobody has re-measured.
- **What it causes.** The inspector's summary, GI-03, GI-07 and the target design all lean on this figure. Today's realtime bounce in the garage may carry much more light, some of it the double count in gi-s1, and its cost against its benefit is unknown.
- **Measured?** The staleness is established from the dates in the docs and code comments. The bounce's current worth is not measured.
- **Already recorded?** No. RT-3's open item (c) stands as if it were current.
- **Fix direction.** Before sizing GI work, re-measure GI on against GI off, with diff images rather than means, with realtime forced in the garage and in the camp. Also measure the bounce's NEE alone. Repeat once gi-s1 is decided.
- **Evidence:**
  - docs/RT-SERIES.md:1771 - RT-3, 2026-09-07: the whole traced bounce is worth +0.383 levels in the garage and +0.588 in the camp
  - RageVEditor/assets/shaders/rtgi_trace.rvshader:506 - RT-11, found 2026-09-21: 'Every aimed sample this pass has ever taken was shadowed by the lamp it was aimed at' (506-518)
  - docs/HANDOFF.md:262 - the same defect, worth eleven display levels on the garage floor for reflections (262-267)
  - docs/RT-SERIES.md:73 - the tubes had no length in the scene from the 09-14 rollback until the fix at 22fc27c, which needed a re-bake
  - docs/RT-SERIES.md:1833 - RT-3 open item (c), 'the traced bounce is worth under 0.6 levels in both test scenes', still stands

#### gi-s5 · Every field creation reads both identical 92 MB bake files in full and allocates a solve twin

- **Verdict:** added by the skeptic. **Severity:** low. **Kind:** performance. **Scope:** patch.
- **Roadmap:** RT2-4
- **What is wrong.** Loading the bridge, switching back to a baked lighting, or toggling the GI form reads about 184 MB from disk, because both flavour files are read and they are identical by design. The engine then copies them, allocates two 92 MB textures and zero-uploads one of them, all in one frame. A baked-lamp switch, where no file exists for the new hash, still allocates both textures and uploads up to 184 MB of zeros.
- **What it causes.** Load time, plus a frame hitch on every lighting change, growing with the field's size. Inferred, not measured.
- **Measured?** Not measured.
- **Already recorded?** The identical pair is recorded as deliberate (HANDOFF.md:5645-5654). The full read of the twin and the always-allocated solve texture are not recorded.
- **Fix direction.** Read only the twin's header, or drop the twin until the flavours diverge. Allocate the solve texture only when a solve is requested, and zero-fill on the GPU. Keep the last field per lighting hash resident so that switching back does not re-read it.
- **Evidence:**
  - RageV/src/RageV/Scene/Scene.cpp:3911 - any change to the lighting hash, the layout or the GI form recreates the atlas (3911-3927)
  - RageV/src/RageV/Scene/Scene.cpp:3988 - the other flavour's file is read in full just to check its stamp, which the code calls bookkeeping 'nothing reads' outside a bake (3983-3995)
  - RageV/src/RageV/Renderer/BakedLighting.cpp:445 - Read loads the whole file and then copies the payload (445, 499)
  - RageV/src/RageV/Renderer/IrradianceVolume.cpp:214 - a same-size solve texture is allocated and zero-filled from a CPU buffer on every creation (214-231)
  - RageV/src/RageV/Scene/Scene.cpp:4014 - with no bake, the front texture is zero-filled through a CPU cell array (4014-4015)
  - SampleProject/assets/baked/GoldenGateDemo/field_3217cd40954c3f78_rt.rvfield:1 - 91,827,560 bytes, byte-identical (same MD5) to its _ss twin

### Reflections, denoising and temporal reconstruction (incl. TAA)

**State of the area.** What exists. The traced reflection is a "signal" with its own chain. A signal is a noisy per-pixel estimate that gets its own filtering. The chain runs in this order:
- ReflectionBudget (RT-9) picks one ray count per 16x16 tile, and runs only while something moves.
- ReflectionTrace casts one ray per pixel at full resolution from a fullscreen fragment shader, up to 4 in "young" tiles. It adds an aimed sample at up to 16 lamps (NEE, next-event estimation), combined with the lobe ray by MIS (multiple importance sampling, a weighting that stops the same light being counted twice).
- ReflectionResolve lets each rough pixel borrow 24-64 neighbours' hit points.
- The measured-change record, re-light and 4 filter passes re-trace last frame's rays to detect real light changes. This is an A-SVGF-style "anti-lag".
- ReflectionAccumulate is the shared 1,973-line "signal contract" shader (942 lines of code, 965 of comment), compiled 3 ways for 8 signals. It finds last frame's picture where the reflected image stood (the "virtual image", the point a reflection appears to sit behind the mirror). It tests that the reflector is the same, shortens its memory by motion and confidence, and holds the old picture to a box around this frame's neighbours.
- Three variance-guided blur passes follow.
- ReflectionComposite adds the picture into the lit frame BEFORE TAA (temporal anti-aliasing, which blends each pixel with its reprojected past) and hands TAA one motion vector per pixel.
- The nearest glass pane runs a second full-resolution copy of the whole chain. The sea and the panes behind it cast their own rays inside forward shaders, with no accumulator.

Does it hold up? Parked, largely yes. The measured change, the half-float rounding fix, RT-11's aimed samples and the object-ownership rule produce a clean still picture. Under camera motion it does not, and the reasons chain together:
1. The depth used to find the old picture is unreliable. It is smoothed at the surface's old place, averaged with 10 km sky misses, and collapsed by normal-map "curvature" on the wet floor.
2. Object ids are re-keyed by camera culling, so whole objects lose their reflection history and their TAA history on frames when anything crosses the frustum edge.
3. Nothing checks the reflected content well under camera-only motion. The box is 7-14 spreads wide on glossy surfaces, and the hit-identity test is off unless an object moves.

So the only lever left is to keep fewer frames. RT-24's fix 2 did exactly that. The resulting grain is handed to TAA; fix 1's own comment says so. TAA fetches one history per pixel with a motion chosen by comparing brightness, validates it against the surface, and blends at 0.9. That is the spreading blur the last session found.

Cost: about 8-9 ms of a 14-17 ms frame at 1600x900, summed from per-pass records taken on different dates. The work is linear in pixels, so 4K would be roughly 45-50 ms (inferred).

**What to keep.**
- Measured change: A-SVGF-style re-lighting of last frame's recorded rays with exactly last frame's inputs, filtered over a block grid (RT-MEASURED-CHANGE.md, change_filter.rvshader, the RECORD/RELIGHT variants). A measured, principled anti-lag that fires on nothing when nothing changes. It should become the one change signal every denoiser reads.
- include/half_float.glsl: stochastic rounding of every running average onto the half-float grid. It is the measured fix for this GPU truncating half-float writes toward zero, and it belongs in any new filter.
- RT-11 next-event estimation at reflection hits: lamps scored by how much of the lobe points at them, power-heuristic MIS, emitters matched by identity rather than geometry, sample points moved out to the fitting's face. Keep all of it; only the emitter structure behind it must scale (REFL-06).
- Low-discrepancy ray directions: Halton(2,3) over 64 frames, rotated per texel by the R2 sequence at the texel's index (the RT-15e fix that removed row and column banding).
- Accumulating the reflection on the unjittered pixel grid, so the reflection does its own anti-aliasing, and finding the old picture through the virtual image. P + sight x distance is exact for flat mirrors, and NRD's dominant-direction factor is used for rough lobes. The idea is right; the inputs need fixing (REFL-03/04).
- Demodulated composition: the traced picture is radiance with only a hue tint, and the split-sum weight is applied at composite time. This is the right shape for a denoised specular signal; finish it with a full-colour specular albedo read from the G-buffer.
- The single reconstruction contract as a goal (SignalParams), and RT-3.1's guidance downsample plus joint bilateral upsample. That pattern is exactly what half-resolution rough reflections need.
- TAA's geometric validation for surface content: RT-6's depth/normal/id tests with the 3x3 neighbour search, RT-6.6 (moments follow the chosen texel), RT-6.7 (sky both-or-neither), RT-6.8 (box from same-surface taps), RT-20 (jitter-crossing rule).
- Hard object ownership of histories (RT-15c: an object's history is used only for that object). Keep it once object ids are stable (REFL-02).
- Tile-coherent allocation (16x16 tiles, dead band and dwell), and reading the accumulator's own history as the allocator's input. The shape is right; the gating and trading are wrong (REFL-07).
- Instruments and discipline: refusal-reason and choice debug views (RT-12), per-reason counters (RT-19), spin_measure.py / ghost_map.py / the many16 stage for separating ghost from grain, per-pixel diff images, and A,B,B,A palindromes.

**How it should look in an RT-first engine** (the inspector's view; the roadmap decides). A performant RT-first design for this area, in the order the data flows. Each part builds on pieces RageV already has.

1) The frame hands every signal honest inputs.
- The G-buffer is kept for two frames, which removes the 32 B/pixel TAA-guide copy RT-14 flagged.
- Lanes: depth; shading normal; a geometric normal (or a signed curvature lane) for the reflector's shape; roughness and metallic; a pre-integrated specular albedo for demodulation; a STABLE per-entity object id as an exact integer; surface motion vectors covering camera and object motion.
- A shared roughness classification (mirror / glossy / outside the window), computed once per tile and read by every pass.

2) Sampling works on a compacted list, not the screen.
- One list of shading points from every layer (opaque glossy pixels, the nearest glass pane, water pixels), each tagged with a layer index. Tiles are classified by roughness.
- Mirror-like points are traced at full resolution. Glossy points are traced at half resolution (checkerboard) and joint-upsampled with RT-3.1's guidance.
- Tracing runs in compute or ray-generation passes; price invocation reordering on Blackwell.
- Each ray writes: demodulated radiance; its own hit distance, with misses flagged rather than averaged in as 10 km; a stable hit identity; the hit's motion; and the density it was actually drawn with (VNDF sampling, one pdf everywhere).
- NEE draws from a scalable emitter structure (a light BVH or world grid shared with the direct light and bounce, bar rows merged into line emitters). Keep RT-11's MIS and scoring.
- The ray budget works like the WR-16 controller: rays are TRADED within a fixed average, driven by the denoiser's confidence (history length, predicted disocclusion). Extra rays stay on until a restarted history reaches its settle length, which removes the slow settle after a stop.

3) One specular denoiser, measured in RageV.
- The stages correspond to what exists today. A hit-distance-aware spatial pre-pass (the resolve, made consistent: real source pdfs, the re-aim's change of solid angle, hit-distance weights, footprint-adaptive taps).
- Temporal accumulation from two candidates: surface motion, and virtual motion from THIS frame's spatially filtered, normalised hit distance plus geometric curvature. The candidates are blended by a confidence rather than one being picked.
- The reflected content is validated under ANY motion: predicted versus observed change in hit depth, and the stable struck identity. One history-length counter drives memory. The existing measured-change map drives the anti-lag.
- A history fix for young or disoccluded points: spatial reconstruction scaled by 1/historyLength, hit-distance aware.
- A variance-guided post-blur, with the propagated variance actually used.
- A specular-only temporal stabilisation step using the virtual motion. This gives the signal its own anti-aliasing and removes any need for the frame TAA.
- Everything runs as compute over tiles with group-shared neighbourhoods. State lives in typed lanes (integer flags, 32-bit ids, camera-relative planes), and histories are sized to the point list.
- This structure retires most of today's special rules: ShortenedMemory's three caps, the motion-scaled floor, kSettledBound, the untested silhouette history, the curved-mover choice, the moving layer and follow-hit.

4) Composition.
- The composite applies the specular albedo read from the G-buffer (so the lit shader stops carrying the weight in its alpha, which fits RT-2.2's deferred-resolve direction).
- It adds the reflection AFTER the frame TAA/upscaler, or into a lighting buffer that TAA never filters. TAA sees only surface content with surface motion. 'No reliance on TAA' then holds in every AA mode, including the deliberate MSAA 4x.

5) The measurement backbone.
- An independent reference mode (progressive, unclamped, unfiltered) for bias.
- many16 kept for separating ghost from grain.
- spin_measure, ghost_map, per-pixel diffs and A,B,B,A for everything else.

On the principled alternatives the brief asks about:
- ReBLUR/ReLAX. Their structure maps almost one-to-one onto the passes above. What RageV lacks is not stages but inputs (stable ids, geometric curvature, a proper hit distance) and three rules: blend virtual and surface candidates, weight by hit distance, and drive memory by history length.
- ReSTIR. RT-10's measurement stands for direct lighting. For reflections, the resolve is already a biased spatial reuse, and making it unbiased (Jacobian plus MIS with real source pdfs) is the ReSTIR-like step to measure first. Temporal reservoir reuse needs reprojection by what the ray struck, the mechanism RT-10's record names as missing.
- A neural ray-reconstruction approach (DLSS-RR class) would replace denoisers, TAA and upscaler together. It needs the same inputs (diffuse and specular albedo, normals, roughness, depth, motion, specular hit distance). But it is vendor-locked and is itself a black-box temporal filter, which conflicts with 'no reliance on TAA' and with owning the measurement. At most it could be an optional backend; the engine-owned path is needed regardless.

Order:
1. Patches first: stable ids (REFL-02), RT-9 gating, one counter, the pdf fix, the reflection image-distance debug check.
2. Then the input fixes (REFL-03/04).
3. Then the denoiser, composition and layer rewrite.

#### REFL-01 · Reflections are filtered twice in a row, and the second filter (TAA) can only move each pixel one way

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-0, RT2-28 (Bisection first; structural fix later)
- **What is wrong.** The traced reflection is averaged by its own accumulator. Because the composite runs before TAA, TAA then averages it again. TAA has one motion vector per pixel, the image's or the surface's, chosen by comparing brightness, and it runs its surface-identity test at the image's old position. RT-24's fix 1 and fix 2 deliberately keep the reflection young under motion and leave its grain for TAA to settle. TAA's 0.9 moving feedback (10% new each frame) then spreads a misregistered history, which was measured as the spread that remains (HANDOFF.md:56-61). Ray-traced frames always resolve with TAA (FrameGraphBuilder.cpp:197-206), so there is no non-TAA mode to fall back to or test in: the dependence is built in, not a gap in MSAA or FXAA. Taking the reflection out of TAA's path was measured worse on 2026-09-07 (RT-6.1: 9.00/5.98 before TAA against 6.77/5.87 or 10.70/8.86 after), and a layer added after TAA was measured grainy (RT-15, RT-23). Any such move therefore needs the reflection's own spatial and temporal stabilisation first, then a re-measurement on the 16-ray swing test.
- **What it causes.** On screen: the spreading blur on the wet floor that remains after fix 2, noise while moving, and the lag of two memories in series (RT-16). The uncommitted RT-24 state breaks the owner rule 'no reliance on TAA'. Every future signal composited before TAA inherits the one-motion-per-pixel limit.
- **Measured?** Measured (HANDOFF.md:56-61, clean swing test, one switch at a time): TAA's memory off gives a floor as sharp as settled; resolve gather off is worse; the noise blur off is grainier. RT-SERIES.md:2408-2422 (RT-6.1, slow dolly): composite before TAA with the motion lane scored 9.00 detail / 5.98 change, against 10.70 / 8.86 after TAA at blur 3. Inferred from code: the brightness-driven motion flip and the surface test at image-motion locations.
- **Already recorded?** Partly. RT-16's filing (RT-SERIES.md:110) names two filters in series. HANDOFF.md:56-61 traced the spread to taa_resolve. RT-4/RT-6.1 chose composite-before-TAA. Not recorded: fix 1 making TAA a dependency, the brightness-driven motion flip, the surface identity test at image-motion locations, and the stale headers.
- **Fix direction.** Make the reflection self-sufficient and take it out of TAA's path. Composite it after the temporal resolve, or into a lighting buffer TAA never filters. The reflection keeps its own anti-aliasing: it already accumulates on the unjittered grid. Add a history fix for young pixels and a specular temporal-stabilisation step that uses the virtual motion. TAA then handles only surface content with surface motion. RT-4 rejected the after-TAA arm on 2026-09-07, before the variance-driven blur existed (2026-09-21) and on a slow dolly, so re-measure it on the swing test once the chain's own reconstruction is in. For the current RT-24 bisection: the handoff's three TAA rules, plus REFL-02 and REFL-10.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:3776 - 3776-3826: ReflectionComposite runs before the SSAA/TAA resolve and hands the resolve its motion lane (reflectionMotion)
  - RageVEditor/assets/shaders/reflection_composite.rvshader:126 - 126-136: one motion per pixel, chosen by `reflected > rest`, which compares the (noisy) reflection's brightness with the rest of the pixel. Image motion or surface motion, never both
  - RageVEditor/assets/shaders/taa_resolve.rvshader:829 - 829, 845-847: TAA fetches its history with that single velocity and validates it with the SURFACE identity guide at the image's old place. Depth must agree within 2% (line 414)
  - RageVEditor/assets/shaders/taa_resolve.rvshader:1024 - 1024-1027: moving feedback 0.9 (SampleProject.rvproject:10), so 10% new per frame on top of the accumulator's own memory
  - RageVEditor/assets/shaders/taa_resolve.rvshader:1044 - 1044-1045: TAA reads the reflections' change map, so part of the reflection's anti-lag now lives in TAA
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:6193 - 6193-6195 (uncommitted fix 1): 'A young picture's grain is the frame filter's to settle'; share = 1 for any age
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1678 - 1678-1679 (uncommitted fix 2): the memory floor falls toward 1 frame as the picture moves
  - RageVEditor/assets/shaders/reflection_composite.rvshader:1 - 1-31 (and reflection_accumulate.rvshader:49-54): headers still say the picture is added after TAA and never passes through it
- **Skeptic's note.** The code matches the mechanism. ReflectionComposite runs before the TAA resolve and passes it one motion per pixel, chosen by `reflected > rest` (reflection_composite.rvshader:126-136; FrameGraphBuilder.cpp:3776-3826 and 3953-3955). TAA fetches one history with that motion and runs its surface-identity test at that spot, with depth allowed to differ by 2% (taa_resolve.rvshader:829-847, 407-418). Fix 1 (pbr_fragment.glsl:6184-6195) and fix 2 (reflection_accumulate.rvshader:1667-1679) are in the working tree, and HANDOFF.md:56-61 measured TAA's memory as the spread that remains. The headers are stale as claimed, and so are pbr_fragment.glsl:5956-5958 and 6244-6246. Four corrections. (1) With rays on, ResolveAntiAliasing always returns TAA (FrameGraphBuilder.cpp:197-206; owner decision at HANDOFF.md:394-400). The claim that nothing settles the grain under MSAA, FXAA or none therefore describes a mode that cannot run. (2) Relying on TAA to remove reflection noise was recorded before RT-24 (RT-SERIES.md:2331-2334, RT-13: 'which only TAA's frame average takes away'). (3) It was RT-6.1, not RT-4, that replaced the after-TAA composite. RT-4's rejected arm was the before-TAA composite without a motion lane (RT-SERIES.md:2411-2416). A layer added after TAA was also measured grainy (EngineConfig.h:397-404; RT-SERIES.md:75 arm a). (4) Forcing surface motion for TAA was measured inert on the ghost (HANDOFF.md:46), and the brightness-driven flip has not been measured. HANDOFF's next steps (1)-(3) already target exactly these TAA rules.

#### REFL-02 · Object identity is re-keyed by camera culling, so whole objects drop their reflection and TAA history while the camera moves

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-5
- **What is wrong.** On the default Vulkan path object ids are stable: each is its cull-table row + 1. Culling changes ids only for draws on the CPU list: skinned characters, indexless meshes, and every draw when the GPU lit path is off (OpenGL, --gpu-lit=off, lit meshlets). The problem that matters at scale is storage. The reflection history keeps the id in a 16-bit float lane, and the direct light in an RG16F lane, and a 16-bit float holds whole numbers exactly only up to 2048. In a scene with more than 2048 drawables, no odd id above 2048 can ever match, nor three in four above 4096. Those objects' reflections refuse their history every frame and show one-frame estimates, and moving objects lose the direct light's same-mover exemption from the plane test. The fix direction stands: exact integer ids (R32UI or an fp32 lane) in every history, entity-derived ids for the CPU list, a counter lane for refusal 6, and a wider struck identity.
- **What it causes.** Under camera motion, whole objects (the floor, the car) intermittently lose both histories for a frame. The result is noise while moving and a slower settle, since every restart rebuilds from one ray (REFL-07). At scale, odd ids above 2048 can never match in the reflection history, and different objects share struck identities.
- **Measured?** Not measured; inferred from code. Cheap test: run spin_measure.py with --debug-view=reflection-refusal and TAA's refusal view. Bursts of refusal 6 / 'object' covering whole objects mean it is live.
- **Already recorded?** Not recorded. RT-17 moved the ray table to entity-based ids because 'the row index is not' stable (pbr_fragment.glsl:749-752), but the G-buffer id kept the row index.
- **Fix direction.** Give every drawable a stable identity derived from its entity (the same Owner the ray table uses). Store it as an exact integer in the G-buffer, the TAA guide and the history id lane: 24 bits fit exactly in an fp32 lane, and R32UI is better. Widen RT-17's identity lane to 32 bits. Add refusal 6 to the counters. Compare refusal counts and the swing-test scores before and after.
- **Evidence:**
  - RageV/src/RageV/Scene/Scene.cpp:5189 - 5189-5193: a draw outside the camera frustum is skipped (continue) before Renderer3D::DrawMesh is called
  - RageV/src/RageV/Renderer/Renderer3D.cpp:1413 - 1413-1417: AllocateInstance appends, so the instance index is this frame's position in the submission list after culling
  - RageV/src/RageV/Renderer/Renderer3D.cpp:9639 - 9639: object id = instance index + 1. The sibling path's comment at 9108-9110 says 'stable for as long as the scene's draw order is'
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:4506 - 4506: the G-buffer id lane is that id (negated for static surfaces)
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1126 - 1126-1130: a history with a different id is refused outright (refusal 6) for every one of the 9 candidates, so the pixel falls to a one-frame estimate
  - RageVEditor/assets/shaders/taa_resolve.rvshader:407 - 407-411: TAA refuses a history with a different id (kObjectId), so the pixel takes the raw current frame
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1322 - 1319-1323: the RT-19 counters have no lane for refusal 6, so these refusals are never counted
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1018 - 1018 with reflection_accumulate.rvshader:1920: the stored id goes into an RGBA16F history lane, exact only up to 2048
  - RageV/src/RageV/Renderer/Renderer3D.cpp:5658 - 5658 with reflection_trace.rvshader:1024: RT-17's struck identity is (entity & 0xFFFFF) % 1021 + 1, a 10-bit hash in an R16G16F lane
- **Skeptic's note.** The CPU path does number ids by position after culling (Scene.cpp:5186-5195; Renderer3D.cpp:1413-1417, 9639). But the brief's 'the GPU lit path is off' is wrong: GpuLit defaults to true (EngineConfig.h:984-990) and runs on Vulkan with bindless (GpuCull.cpp:429). On that path every non-skinned, indexed, opaque or cutout mesh is drawn from the cull table, G-buffer included (Scene.cpp:2331-2367 and 5110-5162; Renderer3D.cpp:6099-6135). Its id is its fixed table row + 1 (Renderer3D.cpp:9108-9111), which does not change with culling. The garage has no skinned meshes (showroom.rage has no Animator), and spin_measure.py:133 runs Vulkan with the defaults. So in the garage and the RT-24 harness the floor, walls and car keep stable ids, and the claimed loss of history is not live. Still true: skinned meshes, indexless meshes and every draw under --gpu-lit=off, OpenGL or lit meshlets get unstable ids. The reflection history's id lane is RGBA16F (FrameGraphBuilder.cpp:1018; written at reflection_accumulate.rvshader:1920), and the direct light's is RG16F (FrameGraphBuilder.cpp:1802). Both are exact only up to 2048. The 1021-value fold of the struck identity and the missing counter lane for refusal 6 are real, but both are documented in code (Renderer3D.cpp:5650-5656; reflection_accumulate.rvshader:1319-1323). None of it is measured; the effect above 2048 is exact arithmetic.

#### REFL-03 · The depth a reflection is found by is smoothed in the wrong place and averaged with 10 km sky misses

- **Verdict:** confirmed. **Severity:** high. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-0, RT2-23
- **What is wrong.** For a flat mirror, the virtual image of a hit point is exactly P + sight x distance, so reprojection (finding where the picture was last frame) is only as good as that distance, and the distance is wrong in two ways. First, it is blended with the value stored at the surface's old place. Under camera motion the reflection slides across the surface, so that stored value belongs to a different reflected point (a pole at 5 m beside the wall at 15 m), and it lags by several frames. Second, it is an arithmetic mean over neighbours or over several rays, so one sky miss at 10,000 m drags the mean to hundreds of metres. A wrong depth puts the lookup in the wrong place, and the old picture of a reflected edge is blended in beside the edge.
- **What it causes.** Ghosts behind reflected poles and the car while the camera moves (RT-24). The design has then answered them by shortening memory (fix 2), which produces noise. Outdoors (AAA scenes: wet roads, windows, cars), every reflected skyline gets a wrong depth near the sky.
- **Measured?** Measured (HANDOFF.md:47-48): using this frame's image distance alone took the pole ghost score from 17.1 to 13.6 on the clean 16-ray swing test, a partial fix. The effect of averaging in misses is inferred; the garage rarely misses.
- **Already recorded?** Partly. HANDOFF.md:47-48 lists 'this frame's image distance' as a partial arm. 'Reproject by what the ray actually struck' is listed as unbuilt (RT-SERIES.md:75). Neither the surface-space smoothing nor the miss averaging is named as a cause.
- **Fix direction.** Estimate the virtual depth from this frame's rays, filtered in space in a bounded form. For example, use normalised hit distance d/(d+s), flag misses instead of setting them to 10 km, and take a closest or weighted estimate over the 3x3 the way the resolve already sizes its disc. Never smooth it at the surface's old place. Validate the history by whether its stored depth still agrees after reprojection. For texels with several rays, store a representative per-ray distance, not the mean. Judge with spin_measure's pole and bumper scores and ghost_map.
- **Evidence:**
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1502 - 1500-1502: image distance = mix(value stored at the SURFACE's old place, this frame's value, 1/min(trust+1, 8))
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1405 - 1405-1415: image = travelled x dominant / (1 + 2 x travelled x curvature), where travelled is the resolve's output
  - RageVEditor/assets/shaders/reflection_resolve.rvshader:371 - 371, 381: the resolve writes the weighted arithmetic mean of distances to its neighbours' re-aimed hit points
  - RageVEditor/assets/shaders/reflection_trace.rvshader:826 - 826, 949, 1013: a miss counts as 10,000 m, and a texel with several rays writes the arithmetic mean distance. Line 1020 writes their mean direction and the first ray's pdf
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1526 - 1499, 1526-1527: the history is looked up at P + sight x image
- **Skeptic's note.** reflection_accumulate.rvshader:1502 mixes this frame's image distance with the value stored at the surface's old place, weighted by the trust count, down to a 1/8 share for this frame. The trace averages the distances of several rays and counts a miss as 10,000 m (reflection_trace.rvshader:826, 949, 1013). The resolve writes a weighted arithmetic mean of distances (reflection_resolve.rvshader:371, 381). The accumulator then caps the result at 1000 m (reflection_accumulate.rvshader:1405), which the finding did not mention; it bounds the damage but does not fix it. The measured partial arm supports the lag half: using this frame's image distance moved the pole score from 17.1 to 13.6 (HANDOFF.md:47-48). The miss half is inferred and rare in the enclosed garage, as the finding says. The earlier 'forcing this frame's own: no change' (RT-SERIES.md:75) was measured on moving objects, not camera motion, so it does not refute this.

#### REFL-04 · Mirror curvature is measured from the normal-mapped normal, so the bumpy wet floor reads as a curved mirror

- **Verdict:** confirmed. **Severity:** medium. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-0, RT2-21, RT2-23
- **What is wrong.** The accumulator treats normal-map detail as the shape of the mirror. At 11 m a floor texel is about 1.4 cm wide, so a one-degree normal-map difference between texels two apart reads as about 0.6/m of curvature. The convex-mirror formula then puts the image a fraction of a metre behind the floor instead of metres. The history is looked up almost where the floor itself was, so the reflection is reprojected like floor texture. Concave surfaces are handled as convex. The direction test compares the normal-map normals of two different points after image reprojection, so on bumpy glossy materials it shortens memory because of noise.
- **What it causes.** If live, the wet floor's reflections get the wrong parallax under any camera translation (the swing orbit moves the eye up to about 36 m/s). That means ghosts and smear on exactly the surface RT-24 is about, memory dropped for no reason on normal-mapped glossy materials, and misplaced images on concave reflectors.
- **Measured?** Not measured; inferred from code. One debug view settles it: --debug-view=reflection-image on the floor under the tubes should read about the tubes' height. Near zero means this is live.
- **Already recorded?** Not recorded. The docs discuss curvature only for the chrome poles, with no mention of normal maps or concave surfaces.
- **Fix direction.** Keep a geometric normal in the G-buffer, or derive curvature from depth and positions, and make curvature signed so concave works. Use it for curvature and for the facing, plane and direction tests. Keep the shading normal for the lobe and the rays.
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:4490 - 4490-4496: the G-buffer's normal lane is the shading normal AFTER the normal map
  - SampleProject/assets/models/garage_pbr/underground_garage_pbr_73_pbr_Plane.003_0.rmat:14 - 7, 14: the garage floor (Plane.003) binds a normal map at NormalScale 1. It is a 4096x4096 bake per bake_manifest.json:1082-1093 (mean roughness 0.21)
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:533 - 533-553: Curvature() = |normal change| / distance to a neighbour 2 texels away, minus a 0.15/m allowance sized for half-float rounding. It is unsigned, so there is no concave case
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1415 - 1415: the convex-mirror formula. At k = 0.5/m, a reflection 3 m away is placed 0.75 m behind the floor
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1706 - 1704-1711: the reflection-direction test uses the chosen candidate's stored normal. After image reprojection that normal belongs to a different floor point
- **Skeptic's note.** The G-buffer's normal lane holds the shading normal after the normal map (pbr_fragment.glsl:4490-4496). Curvature() reads it two texels out, unsigned, minus a fixed 0.15/m allowance (reflection_accumulate.rvshader:533-553), and feeds the convex-mirror formula at 1415. The floor binds a normal map at scale 1 (Plane.003 .rmat:7,14; a 4096x4096 bake with mean roughness 0.2116, bake_manifest.json:1082-1093). The direction test at 1704-1711 compares the stored normals of two different floor texels after image reprojection. The consequence has not been measured, as the finding says. The docs discuss curvature only for poles and moving objects; RT-23's 'curvature classification ruled out' (HANDOFF.md:404-405) was about moving objects. Medium until the existing reflection-image debug view (RENDERING-REVAMP.md:1677) shows whether the floor's image distance collapses toward zero.

#### REFL-05 · Under camera motion almost nothing checks whether the old reflected picture is still right, so memory length is the only lever

- **Verdict:** confirmed. **Severity:** high. **Kind:** architecture. **Scope:** rewrite.
- **Roadmap:** RT2-0, RT2-23 (The camera-motion check as an arm; the content checks in the rewrite)
- **What is wrong.** The history tests check the reflector (same object, same plane, facing the same way, similar roughness), not the reflected content. For the content, the box is too wide on glossy surfaces to catch anything, the identity test is off when only the camera moves, and the distance test is loose. A misplaced history (REFL-02/03/04) is therefore accepted, and the only defence is to keep fewer frames as the picture moves. Any setting of that trades ghosting (more frames) against grain (fewer frames), which is the loop RT-24 is in. The accumulator's roughly 20 constants are dials on that trade.
- **What it causes.** Ghosts or noise under camera motion whatever the tuning. Each fix moves the defect elsewhere: fix 2 turned ghosts into grain, and TAA then spread the grain.
- **Measured?** Measured: fix 2 took the pole score from 17.1 to 8.0 (memory off: 7.8), so memory length is what moves the ghost (HANDOFF.md:42-43). HANDOFF.md:45-49 lists eight arms measured inert. The gating and the box widths are read from code.
- **Already recorded?** The ghost/noise trade is recorded in the RT-24 entry. ReBLUR's virtualHistoryAmount, which blends the surface and virtual candidates by confidence, was proposed 2026-09-06 (HANDOFF.md:1828) and never built.
- **Fix direction.** Validate the content under any motion. Compare the predicted change of the hit depth, given the camera's parallax, with the observed change. Compare a stable struck identity whenever the camera or anything else moved. Blend the surface and virtual candidates by confidence instead of picking one. History length then follows confidence, and the smear-cap / fewest / settled-bound family can go. REFL-02 to REFL-04 come first so the tests get correct inputs.
- **Evidence:**
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1602 - 1602-1605, 1621-1622, 1637-1638: the history box is 12 neighbourhood spreads on mirrors and about 7 at the floor's roughness 0.21, doubled once settled
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:689 - 687-693: RT-17's 'did the ray hit something else' test returns 1 unless Change.y is set
  - RageV/src/RageV/Renderer/Renderer3D.cpp:7572 - 7572-7577 with 5635-5639: Change.y requires a moving instance. A camera move alone leaves the test off
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:865 - 637-644, 865-875: the hit-distance test starts at a 34% change on the floor (8% on mirrors) and never cuts memory below a fifth
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1144 - 1144: while only the camera moves, a silhouette texel keeps its own history with no reflector tests at all
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1031 - 1031-1039, 1678-1679, 1704-1716: memory falls with texels moved, the motion-scaled floor, direction confidence and match confidence
- **Skeptic's note.** The code matches. The bound width is mix(12, BoundWidth 3, smoothstep(0.08, 0.3, roughness)), about 6.3 spreads at roughness 0.21 (not 7), doubled by kSettledBound (reflection_accumulate.rvshader:1602-1605, 1637-1638; Renderer3D.h:343). IdentityConfidence returns 1 unless Change.y is set (687-693), and Change.y needs a moving instance (Renderer3D.cpp:5635-5639, 7572-7577). HitConfidence starts at about a 35% change at roughness 0.21 (637-644, 865-875). A silhouette texel keeps its own history untested while only the camera moves (1144). RT-6.3's direction test and RT-6.10's hit test do run under camera motion, but they only scale memory, so 'memory length is the only lever' is fair. Measured support: fix 2 took the pole from 17.1 to 8.0, against 7.8 with the memory off (HANDOFF.md:43), and eight arms were inert (45-49). The proposed blending of the surface and image histories (ReBLUR's virtualHistoryAmount, HANDOFF.md:1828) was never built; no code matches it.

#### REFL-06 · Reflection rays aim at no more than 16 lamps, scored one by one per ray; the garage has about 125 emissive faces

- **Verdict:** confirmed. **Severity:** high. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-0, RT2-11 (The emitter-slot diagnostic; the emitter table)
- **What is wrong.** RT-11's aimed sampling covers only the first 16 emitters in the list. The other tubes are found only when a ray happens to hit them, and then they bring back their whole brightness. The resolve waves those hits through its firefly clamp because they count as emission, and spreads them across its disc. The scoring loop is linear in the number of emitters, so raising the cap costs every ray.
- **What it causes.** Noisy tube reflections, worst while moving when the history is short (an open RT-24 symptom). At AAA emitter counts (hundreds or thousands of emissive meshes), the aimed sampler covers a vanishing fraction of the light and its cost grows linearly.
- **Measured?** Measured: RT-11's gain (RT-SERIES.md:170-175: wet floor 13.31 to 9.69 against the 16-ray truth) was obtained with only 16 rows. Tying the tube noise to unlisted emitters is inferred. The emitter count is the 2026-09-05 record; recount before acting.
- **Already recorded?** The 16 slots against about 125 faces is recorded for the bounce (HANDOFF.md:2133-2137, 'options to discuss, not decided'). It is not recorded as a cause of reflection noise, nor is its interaction with the clamp exemption.
- **Fix direction.** One scalable emitter structure shared by reflections, bounce and direct light: a light BVH or a world-space grid of emitters, as RAY-BUDGET-DESIGN already proposes for lamps. Put every emissive mesh in it, merging rows of coplanar bars into line emitters, with O(log n) or O(1) selection per ray. Once no emissive hit is left unaccounted for, the blanket clamp exemption can go.
- **Evidence:**
  - RageV/src/RageV/Renderer/Renderer3D.h:76 - 76 with Renderer3D.cpp:3120: kMaxAreaEmitters = 16, filled first come first served
  - RageV/src/RageV/Renderer/Renderer3D.cpp:7067 - 7067-7069: the reflection trace receives min(emitters, 16)
  - RageVEditor/assets/shaders/reflection_trace.rvshader:549 - 477-500, 549-557: every aimed sample scores every row (3 probe points x a GGX pdf each) in a fixed float[16]
  - RageV/src/RageV/Renderer/Renderer3D.cpp:5614 - 5614-5623 with reflection_trace.rvshader:846-851: only listed emitters get the emitter flag. A hit on any other emissive surface keeps its full glow at one-ray weight
  - RageVEditor/assets/shaders/reflection_resolve.rvshader:362 - 362-364: taps whose light is mostly emission are exempt from the firefly cap (cap = 1e30)
  - docs/HANDOFF.md:2133 - 2133-2137: the garage has 61 bars with two emissive slots each plus a panel and a fixture, about 125 faces, against 16 slots
- **Skeptic's note.** kMaxAreaEmitters is 16, filled first come first served (Renderer3D.h:76; Renderer3D.cpp:3108-3122), and Nee.x = min(emitters, 16) (7067-7069). Each aimed sample scores every row, three GGX probes a row, in a fixed float[16] (reflection_trace.rvshader:477-500, 549-557). Only listed owners get the emitter flag (Renderer3D.cpp:5615-5625), so a hit on any other emitter keeps its full glow (reflection_trace.rvshader:846-851). The resolve lifts its firefly cap where emission is more than half of a tap (reflection_resolve.rvshader:362-364). RT-11's record says each texel sees one tube among sixteen rows, which implies the list is full. The ~125-face count is from 2026-09-05 (HANDOFF.md:2132-2137), so recount it, and tying the tube noise to unlisted emitters is inferred. RT-23 measured the resolve's emitter exemption switched off as leaving that night's speckle unchanged, and a finite credit as costing 2.3 levels of floor light (HANDOFF.md:309, 323-329). Removing the exemption must therefore wait for full emitter coverage, as the finding already says.

#### REFL-07 · Extra rays run only while something moves, so rebuilding pixels drop to one ray the moment the camera stops

- **Verdict:** confirmed. **Severity:** medium. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-0, RT2-23 (Patch; then trading within a budget)
- **What is wrong.** The allocator keys on 'something moved this frame' instead of 'this pixel's reconstruction is still young'. At the stop, every pixel the motion restarted is young, yet the pass is skipped and those pixels rebuild from one ray a frame. It also only adds rays (there is no fixed budget to trade), reads a frame late, and needs a quarter of a tile before a tile can ask.
- **What it causes.** The owner-reported slow settle after a stop (RT-24). Rays cannot be moved from settled regions to young ones, so AAA-scale scenes either pay more or get less where it matters.
- **Measured?** Measured: RT-SERIES.md:396-414 records -0.05/+0.09 ms and no measurable change in the garage. The settle mechanism is inferred: HANDOFF.md:63-66 names it as a lead, and spin_measure's after-stop rows can measure it.
- **Already recorded?** HANDOFF.md:63-66 records it as a lead, not yet run. RT-SERIES.md:464-469 records the trading shape as not built.
- **Fix direction.** Drive rays from reconstruction confidence every frame: history length after this frame's validation, plus predicted disocclusion. Keep the extra rays until the history reaches its settle length. Allocate within a fixed average, trading between tiles. Count texels made young by camera motion too. Verify with spin_measure's after-stop frames and a cost palindrome.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:2401 - 2396-2404: the reflection budget pass runs only when an instance moved or the camera moved this frame
  - RageVEditor/assets/shaders/reflection_trace.rvshader:894 - 894-911 with Renderer3D.cpp:7045: with no tile map, the trace falls back to extra rays only on surfaces that move on their own. After a camera stop that means one ray everywhere
  - RageVEditor/assets/shaders/reflection_budget.rvshader:127 - 80-89, 127-129, 148-152: tiles ask by last frame's 'young' frame count, need a quarter of the tile to ask, and are held by a dwell
  - docs/RT-SERIES.md:396 - 396-414: RT-9 'changes nothing measurable in this scene'. 464-469: it only adds rays and never trades within a fixed budget
  - docs/RT-SERIES.md:626 - 626-629: 'sample count is the lever, not smoothing': one ray sits 8.6 levels from the 16-ray truth, 16 rays sit 3.8
- **Skeptic's note.** The budget pass needs an instance moving, this frame or last, or the camera to have moved since last frame's recorded eye (FrameGraphBuilder.cpp:2396-2404; Renderer3D.cpp:7226-7248). Without its map the trace falls back to RT-15b's rule, which gives extra rays only to surfaces that move on their own (reflection_trace.rvshader:894-911; Renderer3D.cpp:7045). On the first still frame, restarted texels therefore get one ray. The quarter-of-a-tile bar, the six-frame 'young' test and the dwell match reflection_budget.rvshader:~118-150. HANDOFF.md:63-66 already names this as the unrun lead for the slow settle. RT-9 measured 'nothing measurable' on a slow dolly (RT-SERIES.md:396-414), and the add-only shape is recorded (464-469). One correction: the budget already counts texels made young by camera motion, since it reads any texel with six frames or fewer; only the motion gate stops it at the stop. The effect on the settle is unmeasured.

#### REFL-08 · The chain is full-resolution per-pixel fragment work, and the neighbour gather costs as much as the rays

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** performance. **Scope:** rewrite.
- **Roadmap:** RT2-27, RT2-20
- **What is wrong.** The reflection chain runs as fullscreen fragment passes at full scene resolution. Tiles are not classified, rough surfaces have no reduced-resolution path, tap counts are fixed, and each helper re-reads its own neighbourhood. The passes are 90-99% linear in pixel count (RT-14). Today the trace, about 4.2 ms at 1600x900, costs roughly twice the resolve, about 2.2 ms. With the record and re-light, the accumulate, the blurs, the glass copy and TAA, the chain is about 8-9 ms of a 14-17 ms frame, summed across records of different dates. That extrapolates to roughly 45-50 ms at 4K (inferred).
- **What it causes.** About 8-9 ms of a 14-17 ms frame at 1600x900 goes to reflections plus TAA; this is summed from per-pass records of different dates, not one run. Linear in pixels, that becomes roughly 45-50 ms at 3840x2160 (inferred), before any second glass layer or water.
- **Measured?** Measured per pass, as cited above. The frame share is a sum across records, and the 4K figure is a linear extrapolation.
- **Already recorded?** RT-14 recorded the pixel-bound table. 'The rough surfaces' trace at half resolution' (HANDOFF.md:1829) and 'reflections out of the fragment shader at half res' (NEXT.md:95) are listed candidates. The owner's rule places optimisation after the RT series.
- **Fix direction.** Compute passes over classified tiles. Trace rough surfaces at half resolution (checkerboard) with RT-3.1's guidance downsample and a joint upsample. Rebuild one position per pixel and share it through group memory. Make the tap count follow the footprint. Price a ray-generation dispatch with invocation reordering (Blackwell supports it) against the fragment trace, A,B,B,A.
- **Evidence:**
  - docs/RT-SERIES.md:2823 - 2821-2827: ReflectionTrace 2.28 ms and ReflectionResolve 2.00 ms at 1.44 MP (5.30 and 5.29 ms at 3.24 MP), both 90-99% linear in pixels
  - docs/HANDOFF.md:207 - 207: trace 4.2 ms. Also HANDOFF.md:175 re-light 0.40 ms; RT-SERIES.md:2135 accumulate 0.60 ms; RT-SERIES.md:565 blur passes 0.28 ms; HANDOFF.md:416 and 110 glass 0.54 + 0.14 ms; RT-SERIES.md:1995 TAA 0.27 ms
  - RageVEditor/assets/shaders/reflection_resolve.rvshader:208 - 208, 248-286, 294-315: two loops over 24-64 taps. Every tap rebuilds a world position (depth + normal fetch + a 4x4 matrix multiply) and fetches the hit lane
  - RageVEditor/assets/shaders/reflection_blur.rvshader:396 - 396-407 with FrameGraphBuilder.cpp:1455-1458: three 49-tap passes, each tap rebuilding a position. The passes run every frame because the noise blur is on
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:381 - 381-441, 533-600, 687-717: Neighbourhood, NeighboursAround, AtSilhouette, Curvature and IdentityConfidence each re-read the same 3x3 from memory
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:2444 - 2444-2478 with reflection_trace.rvshader:32-44: rays are cast from a fullscreen fragment shader over every pixel
- **Skeptic's note.** Confirmed: every pass is a fullscreen fragment pass at the scene's resolution, and no tiles are classified. The resolve runs two loops over its taps and rebuilds a position for each (reflection_resolve.rvshader:208-315). The three 49-tap blur passes run every frame because ReflectionNoiseBlur defaults to on (FrameGraphBuilder.cpp:1455-1458; EngineConfig.h:429). The accumulator's helpers each re-read the 3x3. Correction: 'the neighbour gather costs as much as the rays' was true in RT-14's table (2.00 against 2.28 ms at 1.44 MP, RT-SERIES.md:2821-2827). Since then RT-11 and RT-15's hit shading grew the trace to 4.2 ms (HANDOFF.md:207; RT-SERIES.md:565), so the resolve is now about half the trace. The 8-9 ms sum comes from records of different dates and the 4K figure is extrapolated; both are labelled. Half-resolution rough tracing and moving reflections out of the fragment shader are recorded candidates, deferred by the owner's optimisation-pass rule (HANDOFF.md:1829; NEXT.md:95).

#### REFL-09 · Four separate reflection implementations; the glass pane runs a second full-resolution copy of the whole chain

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** architecture. **Scope:** rewrite.
- **Roadmap:** RT2-31
- **What is wrong.** The nearest glass pane runs a complete second copy of the reflection chain at full frame size, about 96 bytes of history per pixel for the pair, or about 800 MB at 4K by format arithmetic, for glass that covers a small share of the frame. Its call has drifted from the opaque layer's configuration: the moving layer is forced on. The panes behind it and the sea's mirror trace inside forward shaders with no accumulator, so all their smoothing over time comes from TAA. Folding the sea into the shared contract was measured worse; if revisited, it must keep the full-resolution trace. Measured cost at the owner's shot: +0.54 ms for stage 3 and +0.14 ms for RT-22's ray plan and measured change.
- **What it causes.** Cost and memory grow with the number of layers, not with the pixels those layers cover. At 4K the glass history alone is about 800 MB. The glass copy costs +0.68 ms for at most 1% of pixels, and glass's own rays take 1.3 ms of the close-up's 2.0 ms transparent pass. Different estimators per layer give visibly different reflections for the same material. The sea breaks the no-reliance-on-TAA rule.
- **Measured?** Measured costs: RT-SERIES.md:64 (+0.45 ms; glass rays 1.3 of 2.0 ms) and HANDOFF.md:106-110 (+0.14 ms). Memory is arithmetic from the formats.
- **Already recorded?** RT-13 records the costs and the owner's decision to keep the panes behind on the old path. The duplication, the divergent flag and the full-resolution history for at most 1% coverage are not recorded as problems.
- **Fix direction.** One chain over a compacted list of shading points from every layer (opaque glossy pixels, the nearest glass, water), each tagged with a layer index. Store histories for the listed points only (or in sparse tiles), with one configuration for all layers. The sea's mirror joins the same reconstruction. Keep the owner's decision on the panes behind unless they reopen it.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:2770 - 2770-3023: the glass pane runs its own budget, trace, resolve, record, re-light, 4 filter passes, accumulate and 3 blurs, all at full frame size
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:2787 - 2787-2793: its history is the same six RGBA16F lanes at full resolution, 96 B/pixel, for glass covering 0.06-0.82% of the frame (RT-SERIES.md:64)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:3016 - 3016-3020 against 2594-2602: the glass copy passes a literal `true` for the moving layer, while the floor passes the setting, which is off by default (EngineConfig.h:404)
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:6087 - 6087-6099, 6110-6115, 6146-6148: panes behind the nearest and the sea cast their own rays in forward shaders, with a different estimator (lobe rays clamped to 8x the probe; the sea's mirror traced per quad) and no accumulator. The sea's ray contract is off by default (EngineConfig.h:602)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:7258 - 7258-7263, 7428-7437: the glass and sea signals are copies of ReflectionSignal on other slots
- **Skeptic's note.** Confirmed: the nearest glass pane runs a full second chain: budget, trace, resolve, record, re-light, six filter passes, accumulate and three blurs, with six full-size RGBA16F history lanes (FrameGraphBuilder.cpp:2770-3023, 2787-2793). Its call passes a literal true for the moving layer, while the floor passes the off-by-default setting (3016-3020 against 2594-2602). It therefore runs the two-layer arm that RT-23 measured worse on the floor (RT-SERIES.md:75, arm b). The panes behind it and the sea cast in-line rays with no accumulator (pbr_fragment.glsl:6087-6150). Three corrections. The 0.06% and 0.82% at RT-SERIES.md:64 are the share of pixels that changed when the layer was switched on, not glass coverage. The sea's lamp glitter does have an accumulator (water_accumulate); it is the sea's traced mirror that relies on TAA alone. Putting the sea's rays on the shared contract was built and dropped by measurement: job 2 turned the bridge red and job 3 bought nothing (RT-SERIES.md:1070-1080), so any revisit must keep the full-resolution per-quad trace. Keeping the panes behind the nearest on the old path is the owner's RT-13 decision.

#### REFL-10 · TAA opens its box 1.5-5x on exactly the surfaces that carry a strong traced reflection

- **Verdict:** confirmed. **Severity:** medium. **Kind:** image-quality. **Scope:** patch.
- **Roadmap:** RT2-0, RT2-28
- **What is wrong.** RT-6.2 widens TAA's clamp on rough dielectrics because their shading 'does not depend on where the eye is'. With traced reflections that is false up to roughness 0.6. The floor at 0.21 gets x1.5, a patch at 0.3 gets x3.8 and one at 0.35 gets x5.2, while the reflection on them depends strongly on the view. With a noisy input on top, the box admits almost any history.
- **What it causes.** A concrete mechanism for the RT-24 spread on the floor under motion: TAA keeps misregistered reflection history. It is cheap to test within the handoff's planned step (2), 'its box'.
- **Measured?** Not measured on the swing test. The factors are computed from the code's formula.
- **Already recorded?** The handoff's next step (2) is 'its box'. This specific clash with the gloss window is not recorded.
- **Fix direction.** As a diagnostic arm: no material widening where the reflection weight is non-zero (the composite knows the share), or use the trace's own roughness window. Long term this becomes moot once reflections leave TAA's path (REFL-01).
- **Evidence:**
  - RageVEditor/assets/shaders/taa_resolve.rvshader:1105 - 1105-1111: box extent multiplied by up to 8 at (1 - metallic) x smoothstep(0.15, 0.5, roughness)
  - RageVEditor/assets/shaders/taa_resolve.rvshader:286 - 286-301: kStableWiden = 8 was measured on a chrome cube crossing a matte wall with the camera still
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5976 - 5976-5980 with Renderer.cpp:171: the traced reflection's window is roughness 0.25-0.6, at full weight up to 0.25
  - SampleProject/assets/models/garage_pbr/bake_manifest.json:1082 - 1082-1093: the floor's mean roughness is 0.2116
  - RageVEditor/assets/shaders/taa_resolve.rvshader:1071 - 1071-1072: under motion the temporal floor is zero, so the box is the widened 3x3 min/max of a noisy composited reflection
- **Skeptic's note.** taa_resolve.rvshader:1105-1111 multiplies the box by up to 8 at (1 - metallic) x smoothstep(0.15, 0.5, roughness). That gives x1.55 at roughness 0.21, x3.8 at 0.30 and x5.2 at 0.35. At 0.30-0.35 the traced reflection still carries about 80-95% of its weight (pbr_fragment.glsl:5979; the High window is 0.25-0.6 per FrameGraphBuilder.cpp:266-273). Under motion the temporal floor is zero (1071-1072). kStableWiden = 8 was validated on a moving cube with the camera still (taa_resolve.rvshader:286-301; RT-SERIES.md:254-272). Its dolly gain was measured on metrics that the record itself says 'reward keeping history, which is what a ghost is' (RT-SERIES.md:3011-3016). It has not been measured on the swing test, and it fits HANDOFF's next step (2).

#### REFL-11 · The visible-normal sampler was rejected while the rest of the chain still weighed its rays by the old sampler's density

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-23 (The resolve's weights are left alone (R11 measured them))
- **What is wrong.** The visible-normal (VNDF) sampler, which is off by default, was rejected by eye during the RT-23 bisection. That was before the anti-lag's re-light defect, which was spraying speckle over the floor, was fixed, and while the trace still wrote the old sampler's density. The resolve's firefly cap uses that density to decide which bright taps are credible. Re-measure the rejection after the trace writes the density it actually drew with. Leave the resolve's tap weights alone: R11 already measured their source-density and Jacobian variants as no better. The default sampler's mirror substitution at grazing angles remains a recorded open bias.
- **What it causes.** A correct variance reduction for grazing glossy floors is switched off for a confounded reason. The default estimator stacks rejected draws on the mirror direction, which gives bands brighter than they should be at grazing view.
- **Measured?** The rejection was judged by eye (HANDOFF.md:345-350); with it off the picture is pixel-identical to the old path. The confound is read from code.
- **Already recorded?** VNDF being off is recorded. The density mismatch is not.
- **Fix direction.** Write the density the ray was actually drawn with. Use the real source densities in the resolve, including the change of solid angle from re-aiming. Re-measure VNDF against an independent reference (REFL-14), then retire the mirror-substitution sampler.
- **Evidence:**
  - RageVEditor/assets/shaders/reflection_trace.rvshader:302 - 302-339: VNDF (visible-normal) sampling when Nee.y is set. 360-385: LobePdf switches to the VNDF density for the aimed-sample weights
  - RageVEditor/assets/shaders/reflection_trace.rvshader:1015 - 1015-1017: the density written for the resolve is always the old D*NoH/(4*VoH), whichever sampler drew the ray
  - RageVEditor/assets/shaders/reflection_resolve.rvshader:336 - 336-340: the resolve's weights use the old form on both sides. 266-285 and 365-366: its firefly cap credits taps by the written density
  - RageVEditor/assets/shaders/reflection_trace.rvshader:341 - 341-346: the default sampler replaces every below-horizon draw with the exact mirror direction, stacking those draws on one direction at grazing view (the floor's case)
  - docs/HANDOFF.md:345 - 345-350 with EngineConfig.h:443-452: VNDF kept off because 'switched on it sprays speckles over the whole floor'
- **Skeptic's note.** Confirmed: when Nee.y is set, the trace draws VNDF directions (reflection_trace.rvshader:302-339) and uses the VNDF density for MIS (360-385), but it always writes the old D*NoH/(4*VoH) density (1015-1020). The resolve's credibility cap reads that written density (reflection_resolve.rvshader:266-285, 365-366). Four corrections. (1) The resolve's tap weights compare both lobes' old-form densities at the same re-aimed direction (336-340); for near-identical neighbours that ratio hardly depends on the sampler, so only the cap is materially confounded. (2) There is a stronger confound: VNDF was judged to spray speckles during the RT-23 bisection (HANDOFF.md:298-350, 'Made it worse: the VNDF sampler'). That was before the re-light defect that was actually spraying floor speckle was found and fixed (HANDOFF.md:133-168, a newer entry), so the rejection is stale either way. (3) The default sampler's mirror substitution is already recorded as an open bias (RT-SERIES.md:2366-2368 and 75). (4) 'Use the real source densities including the re-aim Jacobian' was measured in R11. The Jacobian changed nothing (+2.69 to +2.70), and the neighbour's own draw density moved the floor further from the reference (+3.14) (RT-SERIES.md:2364-2366; reflection_resolve.rvshader:331-333). VNDF is off by default, so the mismatch has no live effect today.

#### REFL-12 · One 2,000-line accumulator carries every signal's special cases through float-packed flags

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** tech-debt. **Scope:** rewrite.
- **Roadmap:** RT2-23, RT2-30, RT2-3
- **What is wrong.** One 1,973-line shader, 942 lines of code, serves eight signal slots through three compile variants, float-packed mode flags and about twenty tuning constants, and every pass in the chain defines mirror versus rough with its own thresholds. Lanes for arms that are off by default, such as the opaque moving layer, are still allocated and written every frame, and dead water-contract code remains. This coupling is what made the RT-23 and RT-24 bisections slow (HANDOFF.md:51-54; Renderer3D.h:361-374). The moving layer is a parked arm with a recorded partial success, not a failed one.
- **What it causes.** Every bisection like RT-24's loses hours to confounds. Memory and bandwidth are spent on features that are off. The structure is hard to extend to AAA-scale needs such as more layers and more signals.
- **Measured?** Line and constant counts are taken from the files. The confounds are recorded at HANDOFF.md:51-54 and Renderer3D.h:361-374.
- **Already recorded?** The individual confounds are recorded. The structure itself is not recorded as a problem.
- **Fix direction.** Build per-kind shaders from small shared functions. Keep state in typed lanes: integer flags, not float packing. Compute one roughness classification once and have every pass read it. Delete the arms that measured worse (moving layer, follow-hit), or allocate their lanes only when switched on. Make the trace and its re-light one function with explicit inputs.
- **Evidence:**
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1336 - 1,973 lines: 942 of code, 965 of comment, 32 preprocessor branches. Compiled 3 ways (Renderer3D.cpp:1776-1806) for 8 slots: reflections, glass reflections, sea mirror, direct pair, glass direct, AO, GI, sea lamps
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:480 - 318-319, 480-483, 883-889, 1963-1967: modes packed into float lanes. PreviousEye.w is 0/1/2, Probe.w is a bitfield, o_Extra.a = 0.5 x choice + refusal + 8 x mark, and roughness + 2 x metal share one value
  - RageV/src/RageV/Core/EngineConfig.h:388 - 388-468: about 15 reflection flags. Moving layer off (404), follow-hit off (415), VNDF off (452). Also SignalParams (Renderer3D.h:335-396) and 20 named constants in the accumulator, 10 in taa_resolve, 7 in the blur, 5 in the resolve
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1602 - 1602-1605, 1661, 691 against taa_resolve.rvshader:1109 and the 0.25-0.6 trace window: every pass defines 'mirror versus rough' with its own thresholds
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1020 - 1020-1023 with reflection_resolve.rvshader:158, 373-383: the sixth history lane is always allocated and the moving lane always written, although the moving layer is off by default
  - RageV/src/RageV/Core/EngineConfig.h:592 - 592, 602, 632: water_accumulate is live while its contract version is dormant, water_trace is opt-in, and water_choose is dead by default
  - docs/HANDOFF.md:153 - 153-167: RT-23. The re-light variant of the trace had silently drifted from the trace in three ways
- **Skeptic's note.** The counts check out: 1,973 lines, of which 965 are comments and 66 blank, leaving 942 of code, with 32 preprocessor branches. Three compiled variants serve eight slots (Renderer3D.cpp:1776-1806). Modes are packed into float lanes (318-319, 480-483, 883-889, 1963-1967). Each pass defines mirror versus rough with its own thresholds: the accumulator uses smoothstep(0.08,0.3), (0,0.3) and (0.05,0.3), TAA uses (0.15,0.5), and the trace window is 0.25-0.6. The sixth lane is always allocated and the moving lane always written (FrameGraphBuilder.cpp:1020-1023; reflection_resolve.rvshader:158, 373-383). RT-23 recorded the drift between the trace and its re-light. Two corrections. The moving layer did not simply measure worse: RT-23 recorded arm (a), the layer added after the resolve, as removing the ghost with grain as its one defect (RT-SERIES.md:75), and the glass copy runs with the layer on (FrameGraphBuilder.cpp:3016-3020), so deleting it is the owner's call. Follow-hit did measure worse (arm c). The dead water-contract code is already recorded as 'dead weight' (RT-SERIES.md:1078-1080).

#### REFL-13 · Two frame counters where one is now enough, a known bug from reading the wrong one, and headers that describe the old pass order

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** tech-debt. **Scope:** patch.
- **Roadmap:** RT2-6 (It moves pixels, so it is an owner-judged arm)
- **What is wrong.** Fix 1 removed the only reader that needed a frame count immune to anti-lag restarts, yet the accumulator still keeps two frame counts. Three places still use the trust count: kSettledBound, the image-distance smoothing, and the written alpha. The blur and the budget use the blend count. This is the recurring 'which counter says young' trap; kSettledBound's misuse was measured inert on the RT-24 ghost. TAA's alpha handling no longer receives anything, and five comments still describe the order before RT-6.1, with the composite after TAA.
- **What it causes.** A wrong box width after a restart (kSettledBound), image depth that lags after restarts, and misleading reasoning during the RT-24 investigation.
- **Measured?** Not measured. The kSettledBound defect is recorded as found in passing.
- **Already recorded?** kSettledBound is recorded (HANDOFF.md:127-128). The rest is not.
- **Fix direction.** Keep one history length, the blend count, in the alpha. Remove the id-lane copy. Rewrite the three headers. Remove TAA's dead alpha path.
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:6195 - 6195: after fix 1, the lit shader only asks whether the alpha is above zero
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1747 - 1747-1752, 1920: the blend count lives in the id lane's green channel, and the alpha keeps a separate 'trust' count
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1637 - 1637-1638: kSettledBound reads the trust count (HANDOFF.md:127-128: 'the fb9cbe9 trap a fourth time'). 1502 and 1728: the image-depth blend and `frames` also run on the trust count
  - RageVEditor/assets/shaders/reflection_blur.rvshader:246 - 246-247 with reflection_budget.rvshader:127-128: both passes had to learn to prefer the green channel (RT-23's last 1%)
  - RageVEditor/assets/shaders/taa_resolve.rvshader:882 - 882-887: says the composite runs after TAA. reflection_composite.rvshader:110 writes alpha 0 before TAA, so TAA's alpha handling (1128, 1142) is dead
- **Skeptic's note.** Confirmed: after fix 1 the lit shader only asks whether the alpha is above zero (pbr_fragment.glsl:6195). The blend count lives in the id lane's green and the alpha keeps the trust count (reflection_accumulate.rvshader:1728-1752, 1920). kSettledBound (1637-1638) and the image-distance blend (1502) read the trust count, while the blur and the budget prefer the green (reflection_blur.rvshader:246-247; reflection_budget.rvshader:~121). The composite writes alpha 0 before TAA (reflection_composite.rvshader:110), and with rays off the lit shader writes a weight of 0 (pbr_fragment.glsl:6243-6249). TAA's alpha box and blend (taa_resolve.rvshader:891-968, 1128, 1142) therefore receive nothing. The headers are stale, and so are pbr_fragment.glsl:5956-5958 and 6244-6246. Correction: 'not measured' is wrong for kSettledBound. It is on RT-24's list of arms measured inert on the ghost (HANDOFF.md:45-47).

#### REFL-14 · The '16-ray truth' runs through the same filters it is used to judge, and there is no independent reference

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** missing-capability. **Scope:** refactor.
- **Roadmap:** RT2-2
- **What is wrong.** Bias questions are still judged against references built from the shipped chain: probe against trace brightness, R11's floor darkening, the clamps, the 64 cap. The 16-ray truth keeps every filter, and the earlier one-off references still kept the trace's caps and its mirror substitution. No engine mode accumulates the reflection unfiltered and unclamped at a fixed pose, so the bias cannot be settled independently.
- **What it causes.** Long-running questions (probe versus trace brightness, the floor's darkening, whether the resolve blurs) cannot be settled, and a fix can agree with a biased truth.
- **Measured?** Not applicable; this is a method finding.
- **Already recorded?** RT-15 notes that 'a reference has to be shown to be right' and adopted the 16-ray render. That it shares the pipeline under test is not recorded.
- **Fix direction.** Add a reference mode: progressive, unclamped, unfiltered accumulation at a fixed pose, with no resolve, box, firefly clamps or TAA, and thousands of samples per pixel. Use it for bias. Keep many16 for telling ghost from grain.
- **Evidence:**
  - tools/scripts/garage/session_2026_09_21/truth_test.py:38 - 1-15, 38, 78-79: 'truth' is the shipped shaders with `rays = 16` inserted into the trace. It uses the same resolve, accumulator, blur and TAA, renders 2 frames at frame 150 of a parked shot, and runs with the noise blur off
  - RageVEditor/assets/shaders/reflection_trace.rvshader:1013 - 1013, 1020: a 16-ray texel still gets the 64 clamp, the mean distance and the mean direction
  - RageV/src/RageV/Core/EngineConfig.h:388 - No reference or path-traced mode exists among the renderer switches; searched EngineConfig.h and FrameGraphBuilder.cpp
- **Skeptic's note.** Confirmed: truth_test.py makes its 'truth' by inserting rays = 16 into the shipped trace. It keeps the resolve, accumulator, blurs and TAA, and renders 2 frames from frame 150 with the noise blur off (truth_test.py:1-15, 38, 57-59, 78-79). A 16-ray texel still gets the 64 cap and the mean distance and direction (reflection_trace.rvshader:1013, 1020), and EngineConfig.h has no reference or path-traced mode. Two corrections. That it shares the estimator is recorded: HANDOFF.md:368-370 calls it 'a same-estimator reference -- it judges reconstruction, not whether the estimator is right'. And lighter-filtered references were used for bias before: R11's truth with no resolve, an unbounded accumulator and memory 400 (RT-SERIES.md:2359-2368), and 2026-09-06's unclamped converged reference (HANDOFF.md:1854).

#### REFL-15 · The blur passes carry a variance lane from pass to pass that no weight ever reads

- **Verdict:** confirmed. **Severity:** low. **Kind:** tech-debt. **Scope:** patch.
- **Roadmap:** RT2-23
- **What is wrong.** The SVGF-style variance propagation is computed and passed along but never affects the picture. Passes 2 and 3 judge already-smoothed neighbours against the raw noise.
- **What it causes.** Wasted bandwidth every frame (the three passes always run), and the filter does not behave the way its comment says.
- **Measured?** Not measured.
- **Already recorded?** Not recorded.
- **Fix direction.** Use the propagated variance in the edge-stopping weight and measure it, or delete the lane.
- **Evidence:**
  - RageVEditor/assets/shaders/reflection_blur.rvshader:106 - 106-113: the comment says later passes judge taps by the variance the previous pass left
  - RageVEditor/assets/shaders/reflection_blur.rvshader:425 - 297-328, 425: the edge-stopping weight uses the centre's raw accumulator moments in every pass
  - RageVEditor/assets/shaders/reflection_blur.rvshader:434 - 216-222, 254, 434, 441: the propagated variance feeds only the next pass's o_Variance
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1466 - 1466-1467: one R16F full-resolution target per pass is allocated for it
- **Skeptic's note.** In every pass the edge-stopping weight and the radius use the accumulator's raw moments (reflection_blur.rvshader:297-328, 425). The propagated variance (VarianceAt, u_VarianceIn) feeds only the next pass's o_Variance (216-222, 254, 434, 441), which contradicts the header at 106-113. One R16F full-resolution target is allocated per pass (FrameGraphBuilder.cpp:1466-1467). The whole variance and edge-stopping filter was measured inert against the 16-ray truth (RT-SERIES.md:75), which fits.

#### REFL-16 · Every reflection ray is capped at 64 per colour channel; the tubes' blue is 100

- **Verdict:** confirmed. **Severity:** low. **Kind:** image-quality. **Scope:** patch.
- **Roadmap:** RT2-6
- **What is wrong.** The cap is an absolute value in scene units. Mirror reflections of the tubes lose 36% of their blue and shift toward green, and in a daylit AAA scene, sun and sky reflections (thousands of units) would be crushed.
- **What it causes.** A colour shift in the brightest reflections today, and wrong highlights at physical light levels later. It may contribute a little to the probe-versus-trace mismatch.
- **Measured?** Not measured.
- **Already recorded?** Not recorded.
- **Fix direction.** Remove the absolute cap. Bound outliers relative to the lobe and the neighbourhood in the denoiser, which already has firefly clamps, and by completing NEE (REFL-06).
- **Evidence:**
  - RageVEditor/assets/shaders/reflection_trace.rvshader:1013 - 1013: min(radiance, vec3(64.0)) * tint
  - SampleProject/assets/models/garage_pbr/underground_garage_pbr_4_light.rmat:3 - 3: Emissive [34.19, 63.08, 100]. Set by tools/scripts/garage/rebuild_pbr.py:70, TUBE_RADIANCE = 100
- **Skeptic's note.** o_Reflection = min(radiance, 64) x tint (reflection_trace.rvshader:1013; the re-light uses the same cap at 1004). The tube emissive is (34.19, 63.08, 100) (underground_garage_pbr_4_light.rmat:3). Under RT-11 a near-mirror ray keeps almost all of a struck tube's glow, and the aimed sample is added before the cap (846-858). Blue is cut about 36% while luminance drops only about 4%, so the effect is mainly a hue shift toward cyan. The cap came in with d34c905 and has no rationale anywhere in code or docs. Not measured.

#### REFL-17 · The reflector plane is stored as a world-space distance in a half float

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** scalability. **Scope:** patch.
- **Roadmap:** RT2-5
- **What is wrong.** The reflector's plane offset n.P is stored in world units in a 16-bit float, so its rounding error grows with the surface's distance from the world origin: up to 0.25 m for surfaces 256-512 m out, and 0.5 m at 512-1024 m. The plane test allows 0.05 m plus 1 cm per metre of eye distance. For vertical glossy surfaces such as facades and car sides a few hundred metres from the origin, seen from close by, the test fails on rounding alone. The accumulator then refuses the history, and the lit shader's reflector check drops the traced reflection entirely in favour of the probe, switching back and forth as the eye distance changes. Latent in the three test scenes. Store the offset relative to the camera, or in 32 bits.
- **What it causes.** In open-world AAA scenes, glossy facades and vehicles far from the origin would lose their reflection history and show one-frame noise.
- **Measured?** Not measured.
- **Already recorded?** The sea's version of this is recorded. The contract's is not.
- **Fix direction.** Store the plane offset relative to the camera, or in 32 bits.
- **Evidence:**
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1955 - 1955-1956: o_Surface.b = dot(N, P) in world space, written without the stochastic rounding, so the GPU truncates it
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1007 - 1007-1008: that lane is RGBA16F
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1153 - 1153-1173: the plane test's tolerance is 0.05 + 0.01 x eye distance
  - RageVEditor/assets/shaders/water_accumulate.rvshader:102 - 102-105: the sea keeps its plane in full float because at a kilometre 'a half's step is half a metre'
- **Skeptic's note.** Confirmed: o_Surface.b stores dot(N, P) in world units, unrounded, in an RGBA16F lane (reflection_accumulate.rvshader:1955-1956; FrameGraphBuilder.cpp:1007-1008). The plane test's tolerance is 0.05 m + 0.01 x eye distance (1153-1173), and the sea already avoids the problem (water_accumulate.rvshader:102-105). The finding understates it in two ways. A 16-bit float's step is already 0.125 m for values of 128-256 and 0.25 m at 256-512, not only 1 m at 1-2 km. And the lit shader's reflector check reads this frame's copy of the same lane (pbr_fragment.glsl:6208-6218, bound to the current history at FrameGraphBuilder.cpp:1059-1060) and sets share = 0 when it fails. So the traced reflection is dropped for the baked probe outright, not just refused its history.

#### reflections-s1 · Random sampling only changes from frame to frame while TAA jitters, so ray-traced frames cannot run without TAA

- **Verdict:** added by the skeptic. **Severity:** high. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-7
- **What is wrong.** Several samplers draw their random numbers from the pixel alone and advance them each frame only while the camera jitter is non-zero. They include the direct light's lamp picks, the soft-shadow points on discs and tubes, the shadow-ray thinning, the thin-member fade and the water's quad lane. (The jitter is TAA's sub-pixel camera offset.) Without TAA those signals' own accumulators average the same sample every frame, so the engine was changed to force TAA whenever rays are on. The reflection's lobe directions do advance by frame on their own (reflection_trace.rvshader:283), but the other signals inherit the coupling.
- **What it causes.** The owner rule 'no reliance on TAA: every signal needs its own accumulator' can neither hold nor be tested. No ray-traced configuration runs without TAA, so any signal can quietly lean on it, as RT-24's fix 1 now does, and there is no --aa=none or MSAA arm that would catch it. The project's MSAA 4x setting has no effect in RT mode. The inspector's target design, which says the rule 'holds in every AA mode, including MSAA 4x', rests on a mode that cannot run.
- **Measured?** The owner measured the frozen-sample symptoms that led to forcing TAA (HANDOFF.md:394-400). The coupling itself is read from code.
- **Already recorded?** The forced-TAA rule and its reason are recorded (HANDOFF.md:394-400; FrameGraphBuilder.cpp:197-206). Not recorded: that the cause is the samplers' jitter gate, or that it conflicts with the no-reliance rule.
- **Fix direction.** Advance every random sequence on the signal's own frame counter whenever that signal has an accumulator, independent of the camera jitter, and keep the jitter for raster anti-aliasing only. Then remove the forced-TAA rule, and show each signal is self-sufficient with TAA's memory off and with --aa=none, judged by per-pixel diffs against settled frames.
- **Evidence:**
  - RageVEditor/assets/shaders/include/ray_shadow_trace.glsl:24 - 22-25: RV_TRACE_ANIMATED is 'any camera jitter is non-zero'; the comment says every random choice in the engine holds still without a temporal filter
  - RageVEditor/assets/shaders/include/ray_shadow_trace.glsl:356 - 356-360 and 397-398: the soft-shadow point on a light's disc, and the point along a tube, advance by frame only when animated
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:4813 - 4813-4820: the direct light's reservoir picks get zero frame salt without jitter; the same gate appears at 1380 (shadow-ray thinning), 4192 (thin-member fade) and 4075 (the water's quad lane)
  - RageV/src/RageV/Renderer/Renderer3D.cpp:7987 - the direct-light pass's Animated flag is 'jitter non-zero' (also 8097 and 8267)
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:205 - 197-206: ResolveAntiAliasing returns TAA whenever ray tracing is on, whatever the project or --aa says
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:460 - 460-466: MSAA applies only as the MSAA mode, or under TAA when --msaa is given, so the project's MsaaSamples: 4 does nothing with rays on
  - docs/HANDOFF.md:394 - 394-400: owner decision of 2026-09-21 to force TAA, and its reason (the sea's orange dots, frozen lamp picks)

#### reflections-s2 · Reflections on surfaces rougher than the gloss window come only from the baked probe, measured about a third brighter than the traced reflection

- **Verdict:** added by the skeptic. **Severity:** high. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-27
- **What is wrong.** The reflection of the surroundings on every surface rougher than 0.6 comes entirely from the reflection probe, a cube map of the room captured in advance. Between 0.25 and 0.6 it is a blend of the traced reflection and the probe, weighted by roughness. The garage's probe is baked, and the two estimates disagree by about a third in brightness. So reflection brightness steps with roughness across the window, and reflections on rough surfaces never show moving objects or changed lights.
- **What it causes.** In an engine meant to be RT-first, most real materials (roughness 0.3-0.8: concrete, paint, plastics, worn metal) show static reflections that are not ray traced, with a brightness seam where the window blends two estimates that disagree. Since RT-24's fix 1 removed the fade, nothing hides the disagreement.
- **Measured?** The 65 against 49 gap is measured (HANDOFF.md:35-36). The seam across the window and the staleness under moving lights are inferred.
- **Already recorded?** The brightness gap is recorded (HANDOFF.md:35-36, and in the brief). The window's fallback to the probe on rough surfaces is recorded only as a quality-level design choice (7bt), not as an RT-first gap.
- **Fix direction.** First decide which estimate is right against an independent reference (REFL-14). Then give rough surfaces a ray-traced reflection that agrees with the trace: trace them at reduced resolution through the same chain, or read them from the RT GI's radiance cache. Make the window a cost lever inside one estimator, not a switch between two estimates.
- **Evidence:**
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:5979 - the traced share is 1 - smoothstep(0.25, 0.6, roughness): nothing above roughness 0.6
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:266 - 266-273: even the High level's window stops at 0.6 (Low 0.05-0.2, Medium 0.15-0.4)
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:6232 - prefiltered *= 1 - share: the rest of the reflection stays the probe, added at 6241-6242
  - docs/HANDOFF.md:35 - 35-36: the probe is about a third brighter than the traced picture (cut-frame mean 65 against 49)
  - docs/RT-FIRST.md:210 - baked probes stay only as the far-field fallback for GI; the plan says nothing about reflections on rough surfaces

#### reflections-s3 · Full-resolution temporal histories have grown to about 400 bytes a pixel, three times RT-14's inventory

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-19
- **What is wrong.** Every temporal history is kept at full resolution for two frames. Summing the formats gives 96 + 96 + 72 + 72 + 32 + 32 = 400 bytes a pixel, before the scene targets, the glass G-buffer, the half-resolution AO and GI, and the change maps. The total includes lanes for arms that are off by default (the opaque moving layer) and glass histories that cover the whole frame for a few percent of glass.
- **What it causes.** About 576 MB at 1600x900 and about 3.3 GB at 3840x2160 of a 12 GB GPU for histories alone, before textures, acceleration structures and bakes. Every history is read and written every frame, so this costs bandwidth as well as memory.
- **Measured?** Arithmetic from the formats, the method RT-14 used; not measured on the device.
- **Already recorded?** RT-14 recorded 128 bytes a pixel and that the histories are the bigger half (RT-SERIES.md:2800-2818). The growth since is not recorded: reflection lanes from four to six, the two glass copies, the direct light's fifth lane.
- **Fix direction.** Give each signal a lane budget: exact integer lanes for ids and flags instead of float packing; nothing allocated for arms that are switched off; glass histories only where glass is, in tiles or a compacted list; one G-buffer kept for two frames instead of the TAA guide copy (RT-14's own proposal); and reduced-resolution histories for rough signals once REFL-08's half-resolution path exists. Redo RT-14's inventory table afterwards.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1004 - 1004-1023: the opaque reflection history is six RGBA16F lanes, 48 bytes a frame and 96 for the pair
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:2788 - 2788-2793: the glass reflection history has the same six lanes at full frame size
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1798 - 1798-1802: the direct-light history is four RGBA16F lanes plus RG16F, 72 bytes for the pair; the glass direct light is the same at 2731-2738
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1676 - 1676-1679: the TAA guide pair is RGBA32F, 32 bytes; the TAA colour and moments pair at 3886-3888 is another 32
  - docs/RT-SERIES.md:2813 - 2800-2817: RT-14's inventory: 128 bytes a pixel of histories, 1.5 GB at 3840x2160 with the scene target

#### reflections-s4 · The accumulator's firefly clamp removes single-texel highlights on smooth reflectors

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** image-quality. **Scope:** patch.
- **Roadmap:** RT2-6
- **What is wrong.** A small bright source seen in a smooth reflector, such as a lamp, a headlight or a glint, lands on one texel whose eight neighbours reflect something dark. The clamp's ceiling is then close to those dark neighbours' level. The highlight is scaled down to the background every frame and never builds up in the running average. The clamp assumes a sample far above its neighbours is one lucky ray. That holds for a rough surface's spread of rays, but not for a mirror, whose single ray is the whole answer.
- **What it causes.** Point highlights go missing or dim on chrome, car paint and other smooth surfaces, worst at a distance where they are smaller than a pixel. That is the look AAA scenes rely on: streetlights on wet cars, glints on trim.
- **Measured?** Not measured. The clamp's accepted cost of about 3 levels was measured on a flat cube face, where highlights are larger than a texel.
- **Already recorded?** The accuracy cost is recorded as an accepted trade (RT-SERIES.md:629-630). The mirror case is not.
- **Fix direction.** Scale the clamp by the lobe width, switching it off where the texel's ray is the whole lobe, or judge each sample against the texel's own history instead of its neighbours. Test with a light smaller than a pixel seen in a mirror, against an unclamped reference.
- **Evidence:**
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1435 - 1435-1447: the new sample is scaled down to the mean plus 3 spreads of its eight same-object neighbours, the centre left out, at any roughness
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:416 - 416-440: NeighboursAround uses only the eight neighbours
  - RageVEditor/assets/shaders/reflection_resolve.rvshader:167 - 167-168 and 202-204: mirrors, and footprints under half a texel, skip the neighbour gather, so a smooth texel's new value is its own single ray
  - RageV/src/RageV/Renderer/Renderer3D.cpp:7318 - FireflySigmas = 3 on the reflection signal
  - docs/RT-SERIES.md:629 - 629-630: 'The clamps cost about 3 levels of accuracy at one ray', measured on the cube's flat face and accepted

#### reflections-s5 · TAA restarts whole pixels when a reflection changes, however little of the pixel the reflection is

- **Verdict:** added by the skeptic. **Severity:** low. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-28
- **What is wrong.** The reflection's change map says what fraction of the reflection changed, and TAA applies that fraction to the whole pixel. A reflection that makes up 3% of a rough wall and changes by half therefore restarts the wall pixel's TAA history as though the pixel had changed by half. The pixel loses its anti-aliasing and the averaging of its other signals.
- **What it causes.** Extra shimmer and aliasing on low-reflectance surfaces near anything that moves in their reflection (inferred). The measured gain was on the wet floor, which is mostly reflection.
- **Measured?** The gain on the floor is measured (HANDOFF.md:211-216). The cost on low-reflectance surfaces is not.
- **Already recorded?** Not recorded.
- **Fix direction.** Weight the reflection's changed share by how much of the pixel it is. The composite knows this: hand TAA the reflection's share of the pixel's brightness (its weight times the picture, over the pixel's luminance) instead of discarding the alpha. The need also disappears once the reflection leaves TAA's path (REFL-01). Measure the wall beside the moving-light drive.
- **Evidence:**
  - RageVEditor/assets/shaders/taa_resolve.rvshader:1043 - 1034-1050: share = max(direct-light change, reflection change) cuts the whole pixel's frame count to 1/share
  - RageVEditor/assets/shaders/change_filter.rvshader:104 - 102-107: the map holds the changed share of that signal's own light (change divided by that light), not of the pixel
  - RageVEditor/assets/shaders/reflection_composite.rvshader:110 - the composite zeroes the alpha before TAA, so TAA no longer knows how much of the pixel is reflection
  - docs/HANDOFF.md:211 - 211-216: RT-22 fed the reflection map to TAA; the floor trail went from 9.2 to 4.8 levels and flicker was unchanged

### Materials, the lit shader, G-buffer, transparency, water and particles

**State of the area.** Terms used below: a G-buffer is a per-pixel description of the visible surface (normal, colour, roughness and so on), written before any lighting. Forward shading lights a surface in the same pass that draws it. A deferred resolve is a full-screen pass that lights every pixel from the G-buffer instead. A visibility buffer stores only which triangle of which object is visible at each pixel, and works out the material later. A BSDF is the maths that says how a surface reflects or transmits light. A mip is a pre-shrunk copy of a texture. An accumulator is a pass that averages one noisy signal over time; the engine's shared version is called 'the contract'.

Current state. RageV has a real G-buffer pass. The 'GBuffer' pass (FrameGraphBuilder.cpp:1648-1665) draws every opaque kind (static, skinned, layered terrain, masked) with the RV_GBUFFER variant of the lit shader. It writes four lanes: velocity RG16F; an octahedral-encoded normal with roughness and metallic in RGBA16F; albedo plus the specular scalar in RGBA8 linear; and an RG32F lane packing the object id (its sign is the Static flag) with the shading roughness and occlusion. DirectTrace, RTAO, the GI trace and the reflection chain all read it. There is no visibility buffer and no deferred resolve. After the G-buffer pass, the 'Scene' pass draws the opaque geometry a second time through the 6,507-line forward include pbr_fragment.glsl. That pass samples the material again, then composes the direct-light signal, occlusion and GI with the probe blend, the irradiance field, the baked highlight, the environment reflection, the traced-reflection hook and emissive. The traced reflection itself is added later by reflection_composite, before TAA. 'RT mode as a real mode' is not built: RenderSettings.h:374 is still a RayTracing bool with per-effect levels.

The material model is metallic-roughness GGX plus Lambert. Clearcoat, sheen, anisotropy and a wrap-diffuse 'subsurface' exist only in the raster light loop. Terrain gets four layers with three maps each. There is no transmission, IOR, real subsurface scattering, hair, eyes or decals. Each path shades materials differently. DirectTrace, the RT path's direct light, is GGX only. Ray hits use vertex normals, read every texture at mip 0 and light every lamp as a point. The GI bounce and the bake shade hits Lambert-only, so metals return nothing. Terrain hits use layer 0 only.

Alpha-tested geometry is tested correctly inside ray-query traversal. Blended glass and all water are outside the acceleration structure. Transparency uses weighted blended order-independent transparency (WBOIT) with reflectance folded into coverage. Only the nearest pane of glass goes through the shared RT passes; panes behind it trace their own rays in the fragment shader. Water is a parallel renderer: its own grid, its own lobe, its own surface layer, three lamp-pass implementations, 19 flags, and mirror and refraction rays traced inside its draw with no accumulator. Under MSAA every G-buffer lane is resolved by averaging.

Textures: a 4096-slot bindless heap, every texture fully resident, cooked BC1/BC3/BC4/BC5, and no streaming. Particles are unlit and not in the ray structure. Fog is a post-process height fog applied after TAA.

Measured context:
- The lit pass costs 1.0-1.1 ms in the garage and 2.2 ms at Headland (RT-1, RT-2.1). About 0.6 ms of the Headland figure is the second material evaluation.
- The lit pass is 85% pixel-bound (RT-14).
- The lit shader sits at its occupancy edge: 8 extra registers once cost Headland 26% (pbr_fragment.glsl:630-634).

**What to keep.**
- One material sampling function (SampleSurface) shared by the G-buffer, lit, water and glass variants through preprocessor forks instead of copies; the layered and bindless forks change only what set 1 holds.
- The separate G-buffer pass before lighting, the object-id lane and the per-kind coverage (skinned and layered included since RT-2): keep the pass split and the consumers, and redesign the lanes (MAT-12).
- Alpha-tested geometry inside ray-query traversal (RayCandidateIsThere and RayTraverse in ray_shadow_trace.glsl): one traversal for every ray, and the thin-member fade applied identically in raster and in rays. This is a correct design with no any-hit stage, and the base for a transmissive class.
- The bindless heap's safety model: every slot pre-filled with the magenta error texture, slots retired per frame-in-flight and rewritten before reuse.
- Geometric specular anti-aliasing (Tokuyoshi-Kaplanyan) applied only to analytic lights, and the environment-map level chosen from the reflection vector's screen derivatives.
- The parallax march at the footprint mip (RT-2.1, measured 20.3 to 14.5 ms at Headland).
- Karis representative-point sphere and capsule lights, the same in the raster loop and DirectTrace (tubes shaded as tubes on screen).
- Macro variation as a pure function of world position, bit-identical in raster and at hits: consistency by construction.
- The cooked texture pipeline (.rvtex with full mips, BCn, import cache): extend it with BC7/BC6H and streaming rather than replace it.
- The water's measured tuning: the anisotropic Beckmann lobe with the view-aligned streak frame, the Cox-Munk footprint roughness, the foam memory buffer, block-rate lamp choosing with neighbour borrowing. Keep it as material and surface-generator parameters when water joins the shared passes (RT-8's lesson).
- WBOIT with a depth-ramp weight and reflectance folded into coverage: correct for thin non-refractive layers, particles and smoke.
- gbuffer_guide.rvshader's rule 'by selection, never by averaging': extend it to the MSAA resolve.
- Forcing early depth tests in the lit shader despite the counter atomics (the measured +11 ms reason is recorded in place).
- The practice of recording measured reasons next to the code they justify.

**How it should look in an RT-first engine** (the inspector's view; the roadmap decides). Goal: one visibility step, one material evaluation, one BSDF and one composition, with glass and water inside the ray-traced world. The G-buffer consumers that already work are kept: DirectTrace, the accumulators and the reflection chain.

1. Primary visibility, drawn once. GPU-driven raster (meshlets) writes a single-sample surface record. The end state is a visibility buffer: per pixel, which triangle of which instance is visible, plus depth. Under MSAA 4x, coverage is resolved by id, never by averaging data (MAT-04).

2. Material resolve. A compute pass, grouped into screen tiles by shading model, evaluates each pixel's material once. It uses analytic derivatives and writes a designed record:
- base colour (sRGB8 or RGB10A2)
- shading and geometric normals (oct16)
- roughness, metal/F0, occlusion
- integer object id, flags and shading-model id
- a few model parameters: coat, sheen, anisotropy with its tangent, transmission, subsurface
- emissive

This is the only place the camera's view samples material maps.

3. One BSDF module. Evaluate, sample, pdf and environment weight per shading model, energy-compensated. The resolve, DirectTrace, every hit shader (reflections, GI, refraction, the bake) and the composition all use it. Hits evaluate the same material at a 'hit level of detail': the mip from ray cones, the normal map within the cone footprint, and layered terrain through its weight map. Hits light tubes and spheres with the same light-shape code DirectTrace uses.

4. Signals and composition. Direct light, AO, GI, reflection, refraction and transmission are traced from the surface record, and each is rebuilt by its own accumulator (the owner's rule). One full-screen composition then adds, in colour: emissive, direct light, indirect light times albedo, specular (split-sum weight from the same F0 and roughness the trace used), the transparent layer and volumetrics. The frame filter only anti-aliases.

5. Transparency. Glass and water are materials with a transmission lobe. They sit in the acceleration structure as a third traversal class: shadow rays multiply their transmittance, and other rays hit and shade them. One 'first transmissive surface' layer (one format for glass and water) goes through the shared direct and reflection passes plus a refraction pass. An RGB transmittance target handles coloured glass. WBOIT stays for smoke, particles and deeper panes.

6. Water. A surface generator (displacement on a level-of-detail grid, detail normals, foam memory), plus the tuned dielectric lobe as material parameters, plus a participating medium. No private lighting passes. A displaced proxy goes in the acceleration structure.

7. Textures:
- a large bindless heap, with sampled images separate from samplers, 64k or more
- mip streaming driven by GPU feedback from the resolve and from ray cones
- asynchronous uploads under a VRAM budget
- cooking to BC7, BC6H, BC5 and BC4 with explicit usage tags
- virtual texturing only if terrain measurements demand it

8. Volumetrics and particles. A froxel volume, lit from the same light lists with ray-traced or shadow visibility, is applied in the composition to opaque and transmissive surfaces and along traced segments. Particles are lit from it and have real motion vectors.

9. Shader engineering:
- modules instead of one 6.5k-line include
- layouts generated from one definition
- a content-hash shader cache
- separate RT-mode and raster-mode programs
- variant compile failures fatal in development
- instruments only in debug variants
- per-pipeline register counts in the benchmark, via VK_KHR_pipeline_executable_properties

OpenGL is frozen as a raster fallback with its own small shader, or retired; that is the owner's decision.

Order, each step proven by per-pixel diffs and A,B,B,A timings:
- patches: mip level of detail at hits; RT-mode lit shader without the loop, sampler and in-line mirror; single-sample G-buffer or integer id lanes under MSAA; delete the dead water paths; the transparency weight; the specular storage cap
- the BSDF module and the compute resolve, a widened RT-2.2
- the transmissive traversal class and the unified layer
- texture streaming
- the visibility buffer

#### MAT-01 · The frame is still composed by the forward lit shader; the G-buffer is its side output, not the source of shading

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-22 (The visibility-buffer end state is not done by default (section 7, item 7))
- **What is wrong.** In RT mode the opaque frame is still assembled by the forward lit shader. It rasterises the geometry a second time and evaluates the material again, which RT-2.1 measured at about 0.6 ms of Headland's 2.2 ms lit pass. It then composes probes, the irradiance field, the split-sum term (the pre-computed environment-reflection factor) and emissive per fragment. The second raster is the accepted cost of the forward-plus-with-prepass design; the measured waste is the repeated material evaluation, which RT-2.2 files. A full-screen resolve needs new lanes (coat, sheen, anisotropy with its tangent, wrap, emissive) and a per-sample edge path so MSAA keeps working. Its main value is one composition path and scale, not frame time in today's scenes.
- **What it causes.** Frame time: about 0.6 ms at Headland today for the second material evaluation (measured); the lit pass is 1.0-2.2 ms in the demo scenes. The resolve is not a big frame-time win in today's scenes. Its value is one shading path and scale. At AAA triangle density (1-4 px triangles), forward raster wastes lanes on 2x2 quads in both geometry passes (inferred), and heavy materials pay twice. The forward design also forces every G-buffer lane to be multisampled under MSAA (MAT-04) and keeps the raster light loop compiled into the RT shader (MAT-09).
- **Measured?** Measured: RT-2.1 (RT-SERIES.md:1692) 0.6 ms of the 2.2 ms Headland lit pass is the repeated material; RT-14 (RT-SERIES.md:2799-2812) lit 1.017 ms at 1.44 MP (85% pixel-bound), G-buffer 0.231 ms; RT-1 (RT-SERIES.md:1659) garage lit 1.0-1.1 ms. Not measured, inferred from code: the wasted quad lanes at AAA density, and the cost of the probe and field lookups inside the lit pass.
- **Already recorded?** Partly. The forward decision is recorded (RT-FIRST.md:30). RT-2.2 (open, parked to the end of RT-series 1) covers only 'read the G-buffer instead of re-sampling the material', with the raster kept and the coat, sheen and tangent kept out of the G-buffer.
- **Fix direction.** In RT mode, replace the opaque lit raster pass with a full-screen compute resolve that reads the G-buffer (albedo lane widened first, MAT-12) and every signal, and composes once. The raster lit pass stays for raster mode only. End state (rewrite): a visibility buffer (instance id plus triangle id), rasterised once, with the material evaluated in the resolve from analytic derivatives. That removes the second raster and turns MSAA into an edge-resolve-by-id problem. Prove each step with per-pixel diffs (zero-level target) and A,B,B,A timings on the garage and Headland.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1648 - 1648-1665: the GBuffer pass draws every opaque kind and writes velocity, normal, albedo and id plus depth
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:2609 - 2609-2662: the Scene pass keeps that depth, draws the same geometry again through DrawLit, and writes colour, velocity, normal and indirect again (2612-2616)
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:4149 - SampleSurface (every map, parallax march) runs in both variants; the RV_GBUFFER variant returns at 4498-4509 and the lit variant evaluates the material a second time
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:5641 - 5641-5648: under RV_RAY_GI the lit pass writes a zero indirect lane that is 'read by nothing now'; 4141 and 4496 rewrite velocity and surface lanes the G-buffer already wrote
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:5611 - 5611-5925 and 6241-6251: after reading the signals, the lit shader still blends 2 probes plus the sky (6 cube fetches with ProbeParallax loops over 15 probes), reads the irradiance field (19 fetches on the fast path, up to 8x19 on the careful path, 2104-2329), the baked highlight and the reflection hook, then composes
  - C:/Users/ism19/Code/RageV/docs/RT-FIRST.md:30 - The deliberate decision: 'forward+ with a G-buffer'; 'Full deferred shading is not proposed: it would rewrite the material path for no RT gain'
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:1692 - RT-2.1: the lit pass pays the material once more, about 0.6 ms of its 2.2 ms at Headland; RT-2.2 (line 90) is filed for the end and keeps the second raster
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:2799 - RT-14: the Scene (lit) pass takes 0.561/1.017/2.160 ms at 0.36/1.44/3.24 MP and is 85% pixel-bound
- **Skeptic's note.** The code matches the finding. The GBuffer pass (FrameGraphBuilder.cpp:1648-1665) writes four lanes. The Scene pass (2609-2662) rasterises the same geometry again and rewrites velocity, normal and indirect. SampleSurface runs in both variants; the G-buffer variant returns at pbr_fragment.glsl:4498-4509. The lit pass still makes 3+3 cube fetches with ProbeParallax slot scans (2440-2464, 5611-5925), reads the field, and writes a zero indirect lane under RV_RAY_GI (5641-5648). The measured value is RT-2.1's ~0.6 ms of Headland's 2.2 ms lit pass (RT-SERIES.md:1692). Three corrections. (1) Rasterising twice is the forward-plus-with-prepass shape the owner chose: the G-buffer pass replaced the depth prepass (RT-FIRST.md:29-30). The measured waste is the second material evaluation, which RT-2.2 already files, parked by the owner to the end of RT-series 1 (RT-SERIES.md:90). (2) 'The G-buffer pass already runs the whole material path' is overstated. It samples every map but writes no coat, sheen, anisotropy/tangent, wrap or emissive lane (pbr_fragment.glsl:1087-1090), and RT-2.1 lists exactly those lanes as the resolve's cost (RT-SERIES.md:89, 128). (3) The fix direction skips MSAA. RT-2.1's own risk column says 'MSAA/SSAA modes ... must keep working beside it'. A compute resolve that lights one G-buffer sample per pixel loses MSAA's edge anti-aliasing unless edge pixels are shaded per sample, and MAT-04's single-sample G-buffer makes that harder.

#### MAT-02 · One material, five different BSDFs: the RT-first path shades less of the material than the raster fallback

- **Verdict:** confirmed. **Severity:** high. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-21
- **What is wrong.** How a surface is lit depends on which path lights it. RT mode is meant to be the primary path, yet its direct light drops four lobes the raster fallback evaluates. Every traced hit (reflection, refraction, GI, bake) drops normal maps, occlusion and every extended lobe, and lights tubes as points. The GI bounce and the bake treat metals as black. Terrain seen by rays is layer 0 on planar UVs, with no weight map and no cliff projection.
- **What it causes.** On screen: a clearcoat paint, velvet, brushed metal or skin authored today looks different in RT mode than in raster, and different again in its reflection. The sea's traced refraction shows the painted bay floor as one material. GI loses all bounce off metallic surfaces. None of the 458 sample .rmat files uses the extended lobes, so today's scenes do not show the lobe loss. The hit-side losses (normal maps, tubes as points, terrain layer 0, black metals in GI) are live in both demo scenes.
- **Measured?** Measured earlier: RT-15 found metal hits 'shaded Lambert-only came back black' in reflections before the RV_HIT_SPECULAR fix (RT-SERIES.md:67); GI and the bake still have that defect. Tubes shaded as points at hits is a known measured fact. The loss of lobes in RT mode and the size of the terrain layer-0 error are not measured; they are inferred from code.
- **Already recorded?** Partly. Terrain layer 0 in traced reflections is a stated limit (HANDOFF.md:10037-10038). Tubes as points at hits is known, and the owner requires the speckle fix to cover every light type. GI and bake Lambert-only is noted in code (pbr_fragment.glsl:2598-2602). DirectTrace dropping the lobes is not recorded.
- **Fix direction.** Write one BSDF module (evaluate, sample, pdf and environment weight per shading model) and include it in the resolve, DirectTrace, every hit shader and the bake. Give the G-buffer (or visibility buffer) a material index and a shading-model id, so the extended lobes are evaluated wherever the surface is lit. Hits evaluate the same material at a 'hit level of detail': the mip from ray cones (MAT-05), normal maps within the cone footprint, and layered terrain through its weight map. Hits light lamps with the same tube and sphere code DirectTrace already has (direct_trace.rvshader:375-401). Check each lobe against the 16-ray truth harness.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/direct_trace.rvshader:438 - 438-456: the RT direct light is GGX plus Lambert only: no clearcoat, sheen, anisotropy or wrap
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:5317 - 5317-5383 and 5497-5502: the raster loop adds anisotropic GGX, clearcoat (Kelemen), Charlie sheen and wrap diffuse
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:1087 - 1087-1090 and 4505-4507: the G-buffer has no coat, sheen, anisotropy, subsurface, tangent or emissive lane, so DirectTrace cannot know these lobes exist
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:6241 - 6241-6242: the environment and reflection weight is a single-lobe split-sum; the coat layer has no image-based term
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:2469 - 2469-2478 and 2717-2798: at a hit, the interpolated vertex normal only, with 'No normal map, no parallax ... no occlusion', and only base, roughness, metallic, specular and emissive read
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:2598 - 2598-2605: RV_HIT_SPECULAR is off for rtgi_trace and irradiance_fill (both RV_TRACE_ONLY), so bounce and bake hits are Lambert-only and a metal hit contributes zero (diffuse = albedo*(1-metallic), 2802)
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:2986 - 2986-3096: the hit light loop aims at the light's centre with no Extent or radius handling, so every tube or sphere is lit as a point
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:2926 - 2926-2937: terrain chunks enter the acceleration structure with component.Material, which Components.h:463-469 defines as 'Layer 0'
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/MaterialSerializer.cpp:68 - 68-82 and 204-216: clearcoat, anisotropy, subsurface and sheen are authorable and saved
- **Skeptic's note.** Verified. DirectTrace shades GGX plus Lambert only (direct_trace.rvshader:438-456). The raster loop adds anisotropic GGX, Kelemen clearcoat, Charlie sheen and wrap diffuse (pbr_fragment.glsl:5317-5383, 5497-5502). Under the direct signal that loop walks nothing (4640-4645), so these lobes vanish in RT mode, and no G-buffer lane carries them (1087-1090). Traced hits use the interpolated vertex normal and mip 0 (2700-2798) and light every lamp at its centre, with no Extent or radius (2986-3096). rtgi_trace and irradiance_fill compile without RV_HIT_SPECULAR (2598-2605), so a metal returns only its emissive to the GI bounce and the bake. Terrain enters the acceleration structure with layer 0 only (Scene.cpp:2926-2937; Components.h:463). As the finding says, none of the 458 sample .rmat files uses the extended lobes, so the lobe loss is latent while the hit-side losses are live. One more path difference the finding missed: the in-draw mirror rays (the sea, panes behind the nearest) light their hits with the reflector's probe (TraceReflection, pbr_fragment.glsl:3334-3340), while the reflection pass uses the probe at the hit (reflection_trace.rvshader:815-821).

#### MAT-03 · Transparency lives outside the ray-traced world: glass and water are not in the acceleration structure, and there is no coloured transmission or glass refraction

- **Verdict:** confirmed. **Severity:** high. **Kind:** missing-capability. **Scope:** refactor.
- **Roadmap:** RT2-31
- **What is wrong.** Glass casts no shadow and is invisible to reflection and GI rays. Water is invisible to every ray. Coloured glass cannot tint what is behind it, and glass has no refraction. Only the nearest pane is lit and reflected by the shared, accumulated passes. Every other pane re-runs the lit shader and casts its own rays. The exclusion was made because 'every ray treats the structure as opaque' (Scene.cpp:2883-2886). Traversal now has a candidate/confirm loop for masked geometry that could treat transmissive geometry correctly.
- **What it causes.** On screen:
- car windows are missing from floor reflections
- glass casts no tinted shadow
- the bridge's metal, glass and wet deck cannot reflect the sea, and the sea does not block GI rays
- no stained or tinted glass
- no bending through bottles or lenses

Cost: 1.3 ms of rays for the extra panes in the close-up (measured). Scaling: each extra pane per pixel adds a full lit shader plus its rays.
- **Measured?** Measured: the RT-13 costs (RT-SERIES.md:2289-2293 and 2313-2315). Not measured, from code: the visual cost of missing glass and water in rays, and the missing tint and refraction.
- **Already recorded?** Partly. Leaving glass out of the structure is a recorded, accepted trade-off (Scene.cpp comment), and the treatment of panes behind the nearest is an owner decision (RT-SERIES.md:64). Water missing from the structure, the lack of coloured transmission and the lack of glass refraction are not recorded.
- **Fix direction.** Treat transmission as a BSDF lobe. Insert glass and water into the acceleration structure as a third traversal class in RayTraverse. Shadow rays multiply the transmittance of transmissive candidates instead of stopping on them. Reflection and GI rays stop on them and shade them with the same BSDF. This also fixes the 'hollow cabin' that motivated the exclusion, since the cabin is then lit through the glass. Build one 'first transmissive surface' layer with one format for glass and water, and feed it to DirectTrace, the reflection chain and a new refraction pass, each with its own accumulator. Add an RGB transmittance target for coloured glass. Keep WBOIT for smoke, particles and deeper panes. Verify with per-pixel diffs at the garage close-up and the pier.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:2881 - 2881-2899: blended materials are skipped when building the acceleration structure; 'a car reflected in the floor loses its windows. That is a smaller wrong than a car with no interior'
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Material.h:80 - 80-83: TracedAsGeometry is only Opaque or Masked
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:2912 - 2912 and 2936 are the only two RayShadows::AddInstance sites (meshes and terrain chunks); no water body is ever inserted
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/oit_resolve.rvshader:48 - 48-65: one scalar revealage; the background can only be dimmed evenly, never tinted
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:6304 - 6304-6309 and 6468-6477: glass is 'transmitted * alpha + reflected' with a scalar coverage; refraction exists only under RV_WATER (6334-6399)
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:6087 - 6087-6116: panes behind the nearest one trace their own glossy rays inside the transparent fragment shader
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:642 - 642-676 WaterSurface and 685-700 GlassLayer: two separate single-layer transparency G-buffers with different formats and consumers
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:2289 - 2289-2293: glass's own reflection rays are 1.3 ms of the close-up's 2.0 ms transparent pass; line 64: the panes behind the nearest stay on the old path by owner decision
- **Skeptic's note.** Verified. Blended materials are skipped when the acceleration structure (the scene's ray-traced geometry) is built (Scene.cpp:2881-2899; Material.h:80-83). The only two AddInstance sites are meshes and terrain (Scene.cpp:2912, 2936), so no water body is ever inserted. The OIT resolve (the see-through composite) has one scalar revealage (oit_resolve.rvshader:48-65), so glass can dim the background but never tint it. Glass is 'transmitted * alpha + reflected'; refraction exists only under RV_WATER (pbr_fragment.glsl:6304-6309, 6334-6399). Panes behind the nearest one cast their own lobe rays inside the transparent shader (6087-6116). The two single-layer transparency targets have different formats (FrameGraphBuilder.cpp:642-700). RT-13's cost (glass rays 1.3 ms of the close-up's 2.0 ms transparent pass, RT-SERIES.md:2289-2293) and the owner's decision on the back panes (RT-SERIES.md:64) are quoted correctly. The candidate/confirm traversal the fix would extend exists (ray_shadow_trace.glsl:100-205). The owner's reasons for keeping back panes on the old path (a second layer only moves the wall; depth peeling costs a pass set per pane) still apply to any layer design.

#### MAT-04 · Under MSAA 4x every G-buffer lane is averaged at edges, so ray-traced passes read invented ids, roughness and normals

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-21
- **What is wrong.** In MSAA mode, which the project does not currently use (it runs TAA), every G-buffer lane is resolved by averaging. The ray-traced passes therefore read averaged ids (with Static in the sign), averaged packed roughness and occlusion, and averaged normals along every silhouette. Separately, each screen-space signal holds one value per pixel and is applied to every MSAA sample of that pixel. The effect on edges (rims, refused histories) is inferred and unmeasured. The fix needs selection rather than averaging for the ids and flags (integer lanes resolved from sample zero, or a single-sample G-buffer), plus a per-sample path for edge pixels.
- **What it causes.** MSAA 4x is a deliberate owner setting, and in that mode (inferred, unmeasured):
- DirectTrace misclassifies silhouette pixels as static or moving. The lit shader decides Static per fragment (pbr_fragment.glsl:4576 and 5769-5772), so fully baked lamps are counted twice or not at all along object edges: bright or dark rims.
- The accumulators' same-object tests refuse, or wrongly accept, history along every edge.
- Reflection and shadow rays leave from averaged normals.

Memory: RT-14's 53 bytes per pixel times 4 samples, plus the single-sample resolves, is about 1 GB at 1440p.
- **Measured?** Not measured. Related: RT-13 measured MSAA speckle on 1.96% of glass pixels and 1.23% elsewhere, against TAA's 0.63% and 0.61% (RT-SERIES.md:2331-2334), and attributed it to reflection noise; edge averaging was never tested. The memory figure is derived from RT-14's exact inventory (RT-SERIES.md:2799-2812).
- **Already recorded?** No. The depth resolve's sample-zero rule is recorded (ENGINE-NOTES.md:4927-4934), but the G-buffer lanes' averaging resolve is not.
- **Fix direction.** Draw the G-buffer single-sampled into its own target with its own depth, as the water and glass layers already are, and keep MSAA only for the final colour. Vulkan allows the sample-zero resolve only on integer formats (float lanes may only be averaged), so an alternative is to store the ids and flags in R32_UINT (resolved from sample zero) and resolve the float lanes with a small shader that takes sample zero. In the visibility-buffer end state, resolve edges by id. Verify with an edge-pixel diff under --aa=msaa against --aa=ssaa.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanCommandList.cpp:154 - 154-158: every multisampled colour attachment is resolved with VK_RESOLVE_MODE_AVERAGE_BIT
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanCommandList.cpp:177 - 177-192: depth is resolved from sample zero, because 'the average of two positions either side of a silhouette is a place where nothing is'
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:479 - 479 and 578-607: the G-buffer lanes are extra colour attachments of the multisampled scene target; the G-buffer pass writes them at 1650-1657
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanResources.cpp:874 - 874-884: GetColorTexture hands out the resolve; DirectTrace (FrameGraphBuilder.cpp:1882-1886) and ReflectionTrace (2462-2466) read the averaged lanes
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/direct_trace.rvshader:731 - 731-735: Static = id < 0; roughness = floor(g)/65535; occlusion = fract(g). An averaged float breaks all three
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/gbuffer_guide.rvshader:18 - 18-22: the engine's own rule: 'By selection, never by averaging ... the average of two normals across a silhouette points somewhere neither surface faces'
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:667 - 667-674 and 698: the water and glass layers are already single-sampled for exactly this reason
- **Skeptic's note.** The mechanism is confirmed. In MSAA mode (aa=MSAA, or TAA with --msaa=N; FrameGraphBuilder.cpp:453-478) the G-buffer lanes are attachments of the multisampled scene target. Every colour attachment is resolved with VK_RESOLVE_MODE_AVERAGE_BIT (VulkanCommandList.cpp:149-160), and GetColorTexture hands out that resolve (VulkanResources.cpp:874-884). DirectTrace reads it by texelFetch and decodes Static from the id's sign, and roughness and occlusion from its integer and fraction parts (direct_trace.rvshader:731-735), so at silhouettes all three are averages. Vulkan allows the sample-zero resolve only on integer formats (the vendored validusage.json:11189). Two corrections. (1) 'MSAA 4x is a deliberate owner setting' is out of date: the sample project runs TAA ('AntiAliasing: TAA in place of MSAA 4x', the owner's settings committed as found in 5dd603f on 2026-08-27; SampleProject.rvproject:7). MSAA is a supported mode (RT-13 validated under it) and the no-TAA rule makes it matter, but it is not the default picture. None of the consequences (rims, refused histories) is measured. (2) A single-sample G-buffer alone does not fix the edges. Every screen-space signal (direct, AO, GI, the reflection picture and its weight) is one value per pixel, and the lit pass applies it to both surfaces' samples at an edge pixel. The ~1 GB figure is the whole scene target at 4x (colour, OIT and depth included), not the G-buffer lanes.

#### MAT-05 · Every texture read at a ray hit is at mip 0: there are no ray cones

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** image-quality. **Scope:** refactor.
- **Roadmap:** RT2-20
- **What is wrong.** Every texture read at a ray hit is at mip 0, because nothing tracks the ray's footprint (no ray cones). Distant hits therefore sample one unfiltered texel, which can alias in reflections, refraction and GI, and it forces full residency. The on-screen and frame-time cost is not measured, and no current open symptom has been traced to it.
- **What it causes.** On screen: texture sparkle and grain in every traced signal on textured surfaces, which the accumulators, firefly clamps and probe bounds then fight. RT-23's long sparkle hunt ruled out many reconstruction causes but never tested the hit texture level (RT-SERIES.md:73-75). Frame time: uncached reads per hit (inferred from the RT-2.1 analogue). Scale: a mip-0 read needs mip 0 resident, which defeats texture streaming (MAT-07).
- **Measured?** Not measured for hits. The measured analogue is RT-2.1's 5.4 ms from mip-0 reads at distance (RT-SERIES.md:1688).
- **Already recorded?** As a note only (RENDERING-REVAMP.md:812 and the comment at pbr_fragment.glsl:6120-6123); never on the roadmap.
- **Fix direction.** Ray cones. Start each cone from the G-buffer pixel footprint and widen it by the sampled lobe's roughness. Grow its width with distance and with curvature at each hit. At the hit, choose the level from the cone width and a per-triangle UV density computed at mesh cook. Use the same level, with a floor, for the alpha test, and write it as streaming feedback. Measure the sparkle count on the moving-car harness and the traced passes' times, A,B,B,A.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:2763 - 2763, 2775, 2781, 2784 and 2791: base colour, roughness, emissive, metallic and specular are all read with textureLod(..., 0.0) at the hit
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/ray_shadow_trace.glsl:179 - 179-182: the alpha test inside traversal reads level zero
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_trace.rvshader:683 - The aimed emitter sample reads its texture at level zero
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:6118 - 6118-6148: 'A mirror ray reads ONE unfiltered texel at mip 0 -- there are no ray cones', patched with an 8x clamp against the probe
  - C:/Users/ism19/Code/RageV/docs/RENDERING-REVAMP.md:810 - 810-812: 'ray cones are the full fix', noted and never scheduled
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:1688 - The measured analogue: mip-0 fetches at a kilometre made every fetch a cache miss, 5.4 ms of a 20 ms frame (the parallax march)
- **Skeptic's note.** Mip-0 reads are confirmed at every hit texture fetch (pbr_fragment.glsl:2763-2791), in the alpha test inside traversal (ray_shadow_trace.glsl:179-182) and in the aimed emitter sample (reflection_trace.rvshader:683). Ray cones (a per-ray estimate of the ray's footprint, used to pick the mip) exist only as a note (RENDERING-REVAMP.md:812; pbr_fragment.glsl:6120-6123). Three parts of the impact are overstated. (1) The RT-2.1 'analogue' was a parallax march making 8-24 fetches per layer, per projection, for four layers, at every pixel (RT-SERIES.md:1688). A hit reads about five texels, so the 5.4 ms does not transfer. (2) RT-23's sparkle was fixed with a different cause, the measured-change re-light (RT-SERIES.md:73), so no open symptom is linked to hit mips. (3) Level 0 in the alpha test is a stated choice: 'a cutout's edge is the one thing a lower mip would move' (ray_shadow_trace.glsl:179-180). What stands: hit textures alias at distance and under motion, and reading at mip 0 would block any future streaming.

#### MAT-06 · In-shader rays with no accumulator make water and back glass panes depend on TAA

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-7, RT2-31, RT2-32 (The sea mirror with the water rework (D10); the back panes are a named exception (D11))
- **What is wrong.** The sea's mirror and refraction rays and the lobe rays of panes behind the nearest one are traced inside their draws with no accumulator of their own. Their hit lighting is re-drawn every frame: RT-11's one-light choice uses a frame-counter seed whatever the AA mode, and the back panes also re-draw the lobe direction. Only TAA averages them. That breaks the WR-15 hold-still rule and the owner's rule that every noisy signal has its own accumulator. Seeding by the pixel alone when no temporal filter runs is a cheap interim patch.
- **What it causes.** On screen:
- speckle on glass and water in MSAA mode (measured)
- blinking water glitter whenever the frame filter changes (measured in RT-22)

The only local fix available today, a water exemption, has already been rejected by the owner.
- **Measured?** Measured: RT-13 MSAA speckle (RT-SERIES.md:2331-2334); RT-22's 0.80% to 0.97% (RT-SERIES.md:76); RT-8 job 2, which blurred a half-resolution trace, turned the bridge red and was dropped (RT-SERIES.md:1073-1076).
- **Already recorded?** Partly: the symptoms are recorded one by one (the sea's noise, the MSAA speckle, the RT-22 regression). Their shared cause, and the conflict with the no-TAA rule, are not.
- **Fix direction.** Trace glass and water rays in passes over the transmissive layer (MAT-03), at full resolution (RT-8's lesson: keep the full-resolution trace), each with its own accumulator on the contract. Until then, follow the engine's own WR-15 rule for the in-shader draws: a fixed seed per pixel when no temporal filter is running.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:4058 - 4058-4078: the sea traces one ray per 2x2 quad; 'Under TAA the tracing lane walks the four positions frame by frame, so a still quad fills back in over four frames'
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:6369 - 6369-6389: the traced refraction is cast per quad in the water draw and composited that frame, with no accumulator
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:2831 - 2831-2842: each hit keeps one light, drawn with a seed that includes RV_TRACE_FRAME; ray_shadow_trace.glsl:29 defines it as the frame counter, which changes every frame whatever the anti-aliasing mode
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/EngineConfig.h:602 - WaterRayContract = false (602); HitLightSampling = true (459); FrameGraphBuilder.cpp:809-813 builds the sea's own trace pass only on explicit request
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:2331 - 2331-2334: MSAA speckle on 1.96% of glass pixels against TAA's 0.63%: 'the reflections' noise, which only TAA's frame average takes away'
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:76 - RT-22: a frame-filter rule change raised the bridge's blinking water glitter from 0.80% to 0.97%; the water exemption was rejected
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:1381 - 1381-1383: 'the sea's mirror has always been the noisiest thing on the bridge'; 1073-1079: the contract attempt (job 2) was dropped
- **Skeptic's note.** The core is confirmed. The sea's in-draw mirror and refraction rays (pbr_fragment.glsl:4058-4078, 6369-6389) and the lobe rays of panes behind the nearest one (6093-6116) have no accumulator. Two sources make them change every frame in every AA mode. First, RT-11's one-light-per-hit choice (on by default, EngineConfig.h:459): its seed uses RV_TRACE_FRAME, the frame counter (pbr_fragment.glsl:2839-2842; ray_shadow_trace.glsl:28-29). Second, for the back panes, the GGX lobe draw, which GlossyReflection also seeds from the frame counter (3376-3377). This breaks the engine's own WR-15 rule, which ShadowRayKept (1366-1379), the S4 sampler (4813-4817) and QuadTraceLane (4072-4078) all follow: every stochastic choice holds still when no temporal filter runs. Corrections. The sea's ray directions are deterministic (reflect and refract), so 'lobe sampling' applies to the glass panes only. The MSAA speckle figure (RT-SERIES.md:2331-2334) predates RT-13 stage 3 and RT-11 and was measured on glass, not water. Part of it (1.23% of non-glass pixels) came from the opaque reflection chain, which has its own accumulator. RT-8 job 3 (the sea's lamp light on the contract) was built and measured to buy nothing under TAA (RT-SERIES.md:1074-1076); only job 2's half-resolution approach is the one to avoid.

#### MAT-07 · The texture system cannot hold an AAA scene: 4096 heap slots, no streaming, BC1-class compression

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** scalability. **Scope:** rewrite.
- **Roadmap:** RT2-40, RT2-18
- **What is wrong.** The texture system has no streaming. Every texture is uploaded whole, all mips, synchronously, with no VRAM budget, no residency feedback and no mip bias fallback, and the cooker lacks BC7 (high-quality colour) and BC6H (HDR) and picks formats from file names. AAA-scale content would exceed 12 GB and stall loads. The 4096-slot heap cap is a constant that can be raised within the driver's limit.
- **What it causes.** Scale: AAA content (tens of thousands of textures, many GB) passes the slot cap (turns magenta) and the 12 GB of VRAM. Load stalls. Colour quality is capped by BC1. Mip-0 reads at hits (MAT-05) would defeat streaming even if it existed.
- **Measured?** Measured: ROADMAP 7.2's VRAM and load-time gains from cooking (ROADMAP.md:485). The slot cap and the absence of streaming are read from code.
- **Already recorded?** Cooking is recorded as done. Streaming, the slot cap and BC7/BC6H are in no roadmap (searched NEXT.md, ROADMAP.md and RT-FIRST.md).
- **Fix direction.** Split sampled images from samplers and grow the heap to 64k or more descriptors (or move to descriptor buffers). Build a residency manager that streams mips from GPU feedback: the requested level per texture, written by the resolve and by ray-cone hits. Upload asynchronously on a transfer queue under a VRAM budget, with mip bias as the fallback. Cook BC7/BC6H with a real encoder, using explicit usage tags instead of file names. Add virtual texturing only if measurements show terrain needs it.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanDevice.h:253 - kBindlessCapacity = 4096 (clamped at VulkanDevice.cpp:795)
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/TextureHeap.h:52 - 52-61: one slot per (texture, sampler) pair, because each slot is a combined image sampler
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/TextureHeap.cpp:68 - 68-80: when the heap is full, further textures read as the magenta error texture; 112-125: every entry is swept each frame
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/TextureLoader.cpp:425 - 425-521: synchronous load with every mip uploaded; 523-543: the uncooked path uploads uncompressed RGBA8 and generates mips at runtime; 752-765: terrain layer maps packed to RGBA8 at runtime
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/IO/TextureCook.h:14 - 14-21 and 38-45: BC1, BC3, BC4 and BC5 only (no BC7, no BC6H), with the encode chosen from the file name
  - C:/Users/ism19/Code/RageV/docs/ROADMAP.md:485 - 7.2: cooking via stb_dxt cut the 4K set's VRAM arithmetic from ~0.9 GB to ~0.15 GB
- **Skeptic's note.** Confirmed: textures are fully resident with every mip from load, loads are synchronous (TextureLoader.cpp:425-543), there is no residency feedback and no VRAM budget, the cooker has only BC1/BC3/BC4/BC5 with the format chosen from the file name (TextureCook.h:14-45), and a full heap turns further textures magenta (TextureHeap.cpp:68-80). Two corrections. The 4096-slot cap is a constant (kBindlessCapacity, VulkanDevice.h:253), clamped to a driver limit that ENGINE-NOTES §7al records as 'in the millions' (ENGINE-NOTES.md:5402). Raising it is a one-line change and does not require splitting images from samplers. Also, probe cubes already ship as BC6H as a file format, decoded to RGBA16F at load (ENGINE-NOTES.md:13877-13884). The architectural gap is streaming and residency, not the slot count.

#### MAT-08 · The reflection's weight is computed across three shaders that disagree on magnitude

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** correctness. **Scope:** refactor.
- **Roadmap:** RT2-22
- **What is wrong.** The reflection's weight is assembled in two shaders: the lit shader computes the magnitude from the fragment's own material, and the trace applies the hue from the G-buffer's 8-bit albedo and decoded normal. The honest re-measure shows about a 7% difference, left for later. Across the gloss window a traced picture is cross-faded with a probe that is about a third brighter. The composite passes TAA one motion vector per pixel for two layers that move differently. The in-code comments saying the tint is lost and the brightness is '4x' out are stale.
- **What it causes.** On screen:
- a coloured metal's reflection is only right where its colour channels are in proportion
- the traced reflection's brightness has no single formula behind it
- surfaces crossing the gloss window change brightness by about a third
- TAA gets one motion vector for two differently moving layers (the smear class RT-24 is working on)
- **Measured?** Measured: 4x floor brightness when the raw reflectance was applied (recorded in reflection_trace.rvshader:992-995); probe 65 against traced 49 (pbr_fragment.glsl:6190-6193).
- **Already recorded?** Yes for the pieces: the 4x is 'a separate question' in code; the probe brightness is a known fact; RT-24 (in progress, uncommitted) is working on the motion smear. The single cause, a term assembled in three shaders, is not recorded.
- **Fix direction.** Compose the specular term once, in colour, in the resolve (MAT-01), from the same G-buffer F0 and roughness the trace used. Settle the 4x and one-third gaps against the 16-ray truth before touching any look. Replace the gloss-window cross-fade with fewer, lower-resolution rays at high roughness, or with a probe renormalised to the traced picture, whichever measures better. Keep the reflection a separate signal with its own accumulator, composited after the frame filter per the owner's rule; RT-15 and RT-24 are moving that way.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:6241 - 6241-6249: the split-sum weight is collapsed to a luminance and sent in the colour alpha (6492): 'One channel, so a coloured metal's tint is not carried'
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_trace.rvshader:961 - 961-998: the trace multiplies its picture by reflectance divided by its luminance; applying the raw reflectance 'made the garage floor four times brighter ... do not agree, and finding out why is a separate question'
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_composite.rvshader:108 - 108-110: scene + weight * picture; 136: one motion vector per pixel, picked by comparing the two layers' brightness
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:5976 - 5976-5979 (also reflection_trace.rvshader:765-768): above the gloss window (roughness 0.25-0.6) no ray is cast and the probe answers alone
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:6190 - 6190-6193 (RT-24, working tree): after a cut 'the probe here is a third brighter than the traced picture (frame mean 65 against 49)'
- **Skeptic's note.** Two of the finding's pillars are already settled. The coloured-metal tint was fixed on 2026-09-07: the trace multiplies its picture by reflectance divided by its luminance, so the hue is carried (reflection_trace.rvshader:961-998; RT-SERIES.md:2734-2767). The '4x' was retracted in the same record: 'The four times was a broken baseline, not a broken fix' (RT-SERIES.md:2769). Measured honestly, the full-colour form is about 7% darker, which the record puts down to colour-versus-luminance and 'the two sites evaluating envBRDF from different normals and roughnesses', left for later. The comments the finding quotes are stale (pbr_fragment.glsl:6246; reflection_trace.rvshader:992-995; reflection_composite.rvshader:24-26). What stands: magnitude (lit shader) and hue (trace) still come from two shaders with different inputs; the probe is about a third brighter than the traced picture (pbr_fragment.glsl:6190-6193, a known fact); the gloss window (roughness 0.25-0.6 by default, 5976-5979) cross-fades the two; and the composite chooses one motion vector per pixel (reflection_composite.rvshader:136), which is RT-24's territory.

#### MAT-09 · The RT-mode lit shader still carries the raster light loop, its sampler and in-line tracing behind uniform switches

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** performance. **Scope:** patch.
- **Roadmap:** RT2-13, RT2-22
- **What is wrong.** In RT mode the opaque lit shader still compiles the raster light loop, the S4 sampler (with arrays declared outside any define), WR-17 thinning and the in-line mirror rays, all skipped by uniforms. Compiling them out under an RT-mode define is tidy. Whether it saves time is unknown: the precedents show dead arrays sometimes cost nothing once another limit dominates. Read the register counts per pipeline first, then time A,B,B,A.
- **What it causes.** Frame time: lower occupancy in the hottest raster shader (inferred). The measured precedent for the same code shape is 0.84 ms of 6.6 ms.
- **Measured?** Measured precedents only (NEXT.md:547-556; pbr_fragment.glsl:630-634). The current register count and cost are not measured.
- **Already recorded?** RT-1's intent is recorded (RT-SERIES.md:25 and 87); that the loop, the sampler and WR-17 are still in the RT variant is not.
- **Fix direction.** Compile the RT-mode lit shader (or the resolve) without the light loop, S4, WR-17 and the in-line mirror, under a per-mode define. RT-FIRST's RenderMode switch is the natural gate. Read register counts and spills per pipeline through VK_KHR_pipeline_executable_properties, which needs no profiler, add them to the benchmark output, and time A,B,B,A.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:4595 - 4595-4599: directSignal is a uniform bit 'so a preset change needs no recompile'; 4640-4645 set the loop count to zero at runtime
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:4792 - 4792-4794: sampleIndex[8] and sampleScale[8] declared outside any define; 4804-4978 is the S4 sampler, with sampleWeight[8] and sampleSeed[8]
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:1329 - 1329-1384 and 5425-5453: WR-17 distance thinning is still in the opaque loop
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:6093 - 6093-6116 with 2615-3187: the in-line mirror rays and the whole hit light loop are compiled into the opaque variant and skipped when the reflection pass ran
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:25 - The promise: WR-17, S1 and S4 'come out in RT-1'; the RT-1 record (line 1657) shows only S1 and --shade-lights were removed
  - C:/Users/ism19/Code/RageV/docs/NEXT.md:547 - 547-556: S1's identical unconditional 8-entry arrays took about 48 registers and 0.84 ms of a 6.6 ms frame while switched off
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:630 - 630-634: 8 registers of counters cost Headland 26% in 'a shader that was already at its occupancy edge'
- **Skeptic's note.** The code facts hold. directSignal is a uniform bit and zeroes the loop count at runtime (pbr_fragment.glsl:4595-4645). sampleIndex[8] and sampleScale[8] are declared outside any define (4792-4794), with the S4 sampler after them. WR-17 thinning is still present (1329-1384). The in-line mirror is skipped by a uniform (6093-6116). RT-1 promised to remove WR-17 and S4 from the opaque path (RT-SERIES.md:87), but its record shows only S1 and --shade-lights removed (1657). The frame-time precedents are mis-cited. The counters' '26%' (pbr_fragment.glsl:630-634) was mostly a lost early depth test: 'the flush cost 13 ms. Packing the eight counter words into two registers won 4 ms ... The cause was the depth test' (HANDOFF.md:2953-2958). NEXT.md:547-556 gives only an inferred '~48 registers' for S1; the measured showroom change (8.184 to 7.735 ms) bundles the removal of five water passes, and the bridge was unchanged (NEXT.md:567-574). The project's own bisect notes S1 cost +0.84 ms when it landed, but gating it out later recovered -0.05 ms (inside the noise) because other arrays had become the occupancy limit (project memory note of 2026-09-05; not in the repo). The transparent variant legitimately keeps the loop in RT mode, because the panes behind the nearest walk it.

#### MAT-10 · Water is a parallel renderer with dead alternatives

- **Verdict:** confirmed. **Severity:** medium. **Kind:** tech-debt. **Scope:** refactor.
- **Roadmap:** RT2-32, RT2-3
- **What is wrong.** Water has its own geometry, lobe, surface layer, lamp passes (three implementations), refraction inside its draw, foam simulation and 19 flags. It is not a material and not in the acceleration structure. The retired choose and shade passes and the dropped contract paths still compile and run behind flags.
- **What it causes.** Maintenance: every lighting or temporal fix needs a water twin or a water-keyed exception. RT-22's water exemption was rejected, and the next one will be too. Startup compiles and code volume grow. Scale: a fixed 3 m grid cannot cover an ocean out to the horizon.
- **Measured?** Measured: RT-8 shared DirectTrace 1.43 ms against the private pair's 1.21 ms (RT-SERIES.md:1062-1068). The dead code is recorded as dead.
- **Already recorded?** Yes for the dead code (the RT-8 record). The structural parallel path is not recorded as a problem.
- **Fix direction.** Now: delete WaterChooseLamps/WaterShadeLamps, water-ray-contract, water-contract and the in-shader lamp sampler. Then make water a surface generator (displacement, normals and foam on a level-of-detail grid) plus a dielectric BSDF plus a medium. The tuned anisotropic Beckmann lobe, the footprint roughness and the block-rate choosing survive as its parameters. Draw it through the transmissive layer (MAT-03) with a displaced proxy in the acceleration structure. Prove each fold with a per-pixel diff at the composed bridge cameras (RT-8's lesson).
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Water.cpp:217 - 217-229: a uniform grid mesh at a fixed spacing, no level of detail
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:4363 - 4363-4375: the water's colour comes from push constants and the material is ignored; 4205-4422: the surface model sits inside the uber-shader; 5246-5315: its own Beckmann lobe
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:3142 - 3142-3330: three lamp implementations (shared DirectWater, split or fused; the sea's own choose and shade passes; the in-shader sampler)
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Renderer3D.cpp:1737 - 1737-1757: water_choose, water_shade, water_accumulate and water_trace compiled at every launch
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/EngineConfig.h:592 - WaterContract and WaterRayContract (592, 602) are off; the file has 19 water-only flags (367-766)
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:1077 - 1077-1085: 'It is dead weight; deleting it loses nothing'; and the lesson that the private passes were tuning
- **Skeptic's note.** Verified. The water grid is built at a fixed spacing with no level of detail (Water.cpp:217-229). Its colour comes from push constants and the material is ignored by design (pbr_fragment.glsl:4363-4375). It has its own anisotropic Beckmann lobe. The retired WaterChooseLamps/WaterShadeLamps are still added when --water-direct=off (FrameGraphBuilder.cpp:3286-3330), and water_choose/shade/accumulate/trace are compiled at every launch (Renderer3D.cpp:1737-1757). There are about 20 water-only fields in EngineConfig.h, and WaterContract and WaterRayContract are off (592, 602). RT-8 calls jobs 2 and 3's code dead weight (RT-SERIES.md:1080-1081). Note that WaterAccumulateLamps is the sea's live private accumulator, not dead code. The fix direction correctly carries RT-8's lesson that the private passes were tuning to be bought back, proven with per-pixel diffs at the composed cameras.

#### MAT-11 · The uber-shader: 6.5k lines, about 36 compiles per launch, no cache, silent fallbacks, hand-mirrored layouts

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** tech-debt. **Scope:** refactor.
- **Roadmap:** RT2-1, RT2-9, RT2-17, RT2-21
- **What is wrong.** The lit shader is one 6.5k-line include compiled into about 36 variants per launch with no shader cache (the existing cache is never switched on). A variant that fails to compile silently falls back and changes the frame. Buffer layouts are mirrored by hand in two or three places and one comment has already drifted. The meshlet stage's missing object id is harmless today, because no variant it feeds reads that id.
- **What it causes.** Stability: silent picture changes when a variant breaks (it has happened), and drift between the hand copies (a stale comment already). Iteration: about 36 large recompiles at every launch and every traced-feature toggle; the time is not measured. Every new material feature has to be threaded through the monolith.
- **Measured?** Measured: the swallowed failure's 14-level change (recorded in pbr_fragment.glsl:2486-2489). Compile time is not measured.
- **Already recorded?** The missing shader cache is recorded (ENGINE-NOTES.md:9225). The rest is not recorded as one problem.
- **Fix direction.** Split the include into modules: surface sampling, BSDF, light shapes and sampling, hit shading, composition, water surface. Generate the UBO and SSBO structs from one definition. Turn on the existing ShaderCompiler cache, keyed by source hash plus defines. Make variant compile failures fatal in development builds. Move instruments into debug-only variants. Build RT-mode and raster-mode programs separately.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:1 - 6,507 lines, 296 preprocessor directives and about 40 RV_* defines, included by 17 shader files
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Renderer3D.cpp:1737 - 1737-1927 and 2039-2222: about 36 compiles of that include per launch in the default RT configuration (MeasuredChange and GlassLayer on)
  - C:/Users/ism19/Code/RageV/docs/ENGINE-NOTES.md:9225 - 9225-9227: SetCacheDirectory is never called, so every launch recompiles from source
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Renderer3D.cpp:2103 - 2103-2125: a failed variant is logged and the frame changes structure ('drawing with the depth prepass and no G-buffer pass'; 'skinned meshes stay outside the G-buffer')
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:2483 - 2483-2489: a swallowed compile failure once changed the picture by 14 levels with every switch off
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:209 - SceneData is declared again here (136-247) and in scene_block.glsl and Renderer3D; this copy says 'Five rows a volume' while scene_block.glsl:106 says six and the code uses 6
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/pbr_meshlet.rvshader:62 - 62-74: the mesh stage emits 'the identical ten varyings' but not v_ObjectId (scene_vertex.glsl:45-48), so under --meshlets the id lane is undefined
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Renderer3D.cpp:4112 - 4112-4130: control switches packed as integer bits into float UBO lanes; measurement bits are read on every map fetch (pbr_fragment.glsl:1133-1141)
- **Skeptic's note.** Confirmed. It is one 6,507-line include used by 17 shader files and compiled in roughly three dozen variants per launch. ShaderCompiler's cache is never enabled: SetCacheDirectory has no caller (ENGINE-NOTES.md:9225-9227, still true). A variant that fails to compile is logged and the frame silently changes structure (Renderer3D.cpp:2103-2125); a swallowed failure once moved the picture 14 levels (pbr_fragment.glsl:2483-2489). SceneData is mirrored by hand, with a stale 'Five rows a volume' comment (pbr_fragment.glsl:209 against scene_block.glsl:106). Not a defect: the meshlet v_ObjectId claim. pbr_meshlet only feeds the opaque lit variant, which never reads v_ObjectId; only the G-buffer, water-surface and glass-signal variants do (pbr_fragment.glsl:4506, 4557, 4610). The G-buffer pass never draws through meshlets (Renderer3D.cpp:2019-2036).

#### MAT-12 · The G-buffer contract is under-specified: 8-bit linear albedo, no geometric normal, meanings packed into float bits

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** image-quality. **Scope:** patch.
- **Roadmap:** RT2-21, RT2-5
- **What is wrong.** The G-buffer lanes were never designed as a contract. Albedo is 8-bit linear, which is a precondition problem for RT-2.2's resolve; today it only affects metal F0 in the traced direct specular and the reflection hue. There is no geometric normal, so rays on normal-mapped surfaces start from the shading normal (possible self-hits at grazing angles, unmeasured). Flags and two values share float bits, which any averaging corrupts. There is no material or shading-model id.
- **What it causes.** On screen: banding in dark albedos wherever the G-buffer albedo is used (the DirectTrace score, the reflection tint, the future resolve), and self-intersection or leaks at grazing angles on bumpy surfaces (inferred). It also blocks MAT-02's lobes.
- **Measured?** RT-14 measured that the G-buffer is not where the memory goes: 53 bytes per pixel against 128 bytes of histories (RT-SERIES.md:2799-2812), so widening it is cheap. The banding and leaks are not measured.
- **Already recorded?** Albedo is recorded as RT-2.2's precondition; the geometric normal appears once in HANDOFF.md:5494; the packing is not recorded.
- **Fix direction.** Design the lanes as a contract:
- albedo in sRGB8 or RGB10A2
- an oct-encoded geometric normal next to the shading normal
- integer lanes (R32_UINT) for object id, flags and material/shading-model id
- roughness and occlusion in their own UNORM lanes

Decide this together with RT-14's packing question and verify with diff images.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:78 - 78-82: albedo is R8G8B8A8_UNORM, linear; the id lane is R32G32_SFLOAT
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:4505 - 4505-4507: the object id carries Static in its sign; roughness times 65535 as the integer part plus occlusion as the fraction, all in floats
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_trace.rvshader:788 - 788 and 816: the shading normal decoded from the G-buffer is also used as the ray-offset normal (there is no geometric normal)
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:1692 - RT-2.2's precondition: 'a lit pass fed from 8-bit linear albedo bands in the dark tones'
  - C:/Users/ism19/Code/RageV/docs/HANDOFF.md:5494 - The missing geometric normal was part of an earlier unexplained reflection difference
- **Skeptic's note.** Confirmed. Albedo is R8G8B8A8_UNORM linear and the id lane is R32G32_SFLOAT (FrameGraphBuilder.cpp:74-81). Static, roughness and occlusion share float bits (pbr_fragment.glsl:4505-4507). There is no geometric-normal lane: the traces offset rays along the decoded shading normal (reflection_trace.rvshader:788, 816). There is no material or shading-model id. The impact today is smaller than stated. The lit pass multiplies direct diffuse, GI and ambient by its own full-precision albedo (DirectTrace's diffuse excludes albedo, direct_trace.rvshader:447-451). So the 8-bit lane reaches only metal F0 in DirectTrace's specular and score, and the hue of the reflection tint. The banding risk is RT-2.2's recorded precondition (RT-SERIES.md:90). Self-intersection from the missing geometric normal is inferred; the HANDOFF note (5494) concerns a reverted 2026-08 pass.

#### MAT-13 · Fog is a post-process over the opaque depth after TAA, invisible to rays and to transparency

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** image-quality. **Scope:** refactor.
- **Roadmap:** RT2-33
- **What is wrong.** Fog is one post pass after TAA over the opaque depth. Traced reflections and refractions therefore carry no fog along their own path: a far tower seen in near water stays sharp while the tower itself is fogged. Transparent pixels are fogged by what lies behind them, and the bridge depends on a fog floor at a flat sea. The froxel volume that fixes lamp in-scatter is already planned as WR-11; what WR-11 does not cover is fog at ray hits and moving the fog application into the composition, before the frame filter.
- **What it causes.** On screen (inferred, unmeasured): the far bridge is fogged directly but stays sharp in its reflection on the sea; lamp halos in fog come only from bloom; any non-flat water, or a fog layer below the floor setting, breaks.
- **Measured?** Not measured.
- **Already recorded?** No.
- **Fix direction.** Build a froxel volume: a 3D grid aligned to the camera frustum that stores fog density and lighting. Inject light from the same light lists with ray-traced or shadow visibility. Apply it in the composition before the frame filter, to opaque and transmissive surfaces alike, and along reflection and refraction segments. An analytic height-fog transmittance at hits is the cheap first step.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:4883 - The Fog pass comes after Transparent (3645), ReflectionComposite (3792) and TAA resolve (3910)
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/fog.rvshader:122 - 122-130: distance from the opaque depth (the far plane where there is none); 261 and 308: one inscatter colour from the sky cube; 326: a single mix over the finished colour
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:3214 - 3214-3328: ShadeTraced has no fog term; 1049-1055: water writes no scene depth
  - C:/Users/ism19/Code/RageV/SampleProject/assets/scenes/bridge_cinematic.rvpostprofile:9 - FogFloor 0 (fog clipped at sea level) with Fog on and StartDistance 420 (lines 2-11)
- **Skeptic's note.** Confirmed. The Fog pass runs after Transparent, ReflectionComposite and TAA resolve (FrameGraphBuilder.cpp:3645, 3792, 3910, 4883). It works from the opaque depth and mixes over the finished colour (fog.rvshader:122-130, 326). ShadeTraced has no fog term, and the bridge profile relies on FogFloor 0 (bridge_cinematic.rvpostprofile:9). 'Already recorded: No' is wrong for the core fix. A froxel volume (a 3D grid aligned to the camera frustum) lit from the cluster light lists, with ray-traced shafts for the floods and sampled by transparents and particles, is WR-11 in RENDERING-REVAMP.md:1118-1172 and on the NEXT.md:78 order: planned at 1-1.5 ms, not built. WR-11 applies 'at the existing fog application point', which is after TAA. New and unrecorded: traced reflections and refractions are never fogged along their path, and fog is applied after the frame filter instead of in the composition.

#### MAT-14 · OpenGL parity sets the limits of the Vulkan RT-first material system

- **Verdict:** confirmed. **Severity:** medium. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-18 (OpenGL frozen (D1))
- **What is wrong.** The shared shader source must fit OpenGL's per-stage sampler budget and binding model, although RT mode is Vulkan-only (RT-FIRST §0).
- **What it causes.** Scale and features: terrain is capped at four layers with three maps each; every new material input costs sampler juggling; per-scene data gets stuffed into the scene UBO.
- **Measured?** The sampler counts (33 against a limit of 32) are recorded in the comments.
- **Already recorded?** Yes, in code comments; not as a roadmap item or an owner decision.
- **Fix direction.** Owner decision: freeze OpenGL as a raster-only fallback with its own small lit shader (or retire it), and let the Vulkan RT path fetch all per-material data through bindless descriptors and buffer addresses, with no per-stage sampler budget.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:876 - 876-885: three maps per terrain layer 'because the shared set 0 already spends sixteen of the thirty-two texture units OpenGL gives a fragment stage'
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:386 - 386-390: a binding is declared only in the RT variants because the layered variant reached 33 samplers
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:1596 - 1596-1602: the irradiance field is packed into one 3D texture to stay under OpenGL's 32 samplers
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/scene_block.glsl:90 - 90-93: set 0 has no free binding and each pipeline family allocates its own layout, so new data goes into the scene UBO
- **Skeptic's note.** Verified in the code comments. The layered terrain keeps three maps per layer because set 0 spends 16 of OpenGL's 32 fragment samplers, and the bindless path keeps the same three so the two paths stay pixel-identical (pbr_fragment.glsl:876-885). u_ScreenReflectionSurface is declared only in RT variants because the layered variant hit 33 samplers (386-390). The field is packed into one 3D texture for the same budget (1596-1602). Set 0 has no free binding, so new per-scene data goes into the scene UBO (scene_block.glsl:90-93). RT-FIRST.md:7 records the owner's reading that 'OpenGL and Vulkan part ways here', but the shared include still obeys OpenGL's budget. Freezing or retiring OpenGL is an owner decision.

#### MAT-15 · Material model coverage is below AAA: no transmission or IOR, real subsurface, hair, eyes, decals or general layering

- **Verdict:** confirmed. **Severity:** medium. **Kind:** missing-capability. **Scope:** refactor.
- **Roadmap:** RT2-21 (Groundwork only; section 7, item 18)
- **What is wrong.** There is no shading-model id and no BSDF for transmission (thin or thick, with IOR or rough), subsurface scattering, hair, eyes or clear-coat flakes. There are no decals (the garage's decals are baked into its mesh), no detail maps, and no layering outside terrain. Parallax is not applied at hits.
- **What it causes.** Characters, foliage translucency, glass, liquids, car paint and decal-heavy environments cannot be authored at AAA quality.
- **Measured?** Not measured.
- **Already recorded?** Only the wrap limitation is stated in code.
- **Fix direction.** Add a shading-model id to the material record and the G-buffer, with a small closed set of BSDFs (standard, coat, cloth, anisotropic, transmissive, subsurface, hair) implemented once in the BSDF module (MAT-02). Add a decal buffer written before the resolve. Check each model against the truth harness before any asset uses it.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Material.h:86 - 86-228: a fixed parameter block: base, emissive, metallic, roughness, occlusion, specular, height, macro, coat, anisotropy, wrap subsurface, sheen
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Material.h:29 - 29-55: three blend modes (Opaque, Blend, Masked); no transmissive or refractive class
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:3528 - 3528-3538: 'A wrap, not a diffusion profile'
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:3705 - 3705-3729: the one Surface struct every path fills; layering exists only for terrain (3890-4050)
- **Skeptic's note.** Verified. MaterialParams is a fixed block (Material.h:86-228). There are three blend modes with no transmissive class (29-55). 'Subsurface' is a wrap and says so (pbr_fragment.glsl:3528-3538). Layering exists only for terrain. There is no decal code anywhere in RageV/src/RageV, and parallax is not applied at hits. This is a capability gap, not a defect. Under the owner's rules each new shading model has to be proven against the 16-ray truth before any asset uses it, which the fix direction already says.

#### MAT-16 · Particles are unlit and invisible to rays, report no motion, and outweigh glass 4.5 to 1 in the shared transparency sum

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** missing-capability. **Scope:** patch.
- **Roadmap:** RT2-6, RT2-33
- **What is wrong.** Particles are unlit by design, sit outside the ray structure, and report no motion; the sorted and additive pipelines blend the velocity lane by an undefined alpha. The weighted path gives smoke 4.5 times glass's weight at the same depth, against its own stated intent. That is latent, since no demo scene mixes weighted particles with glass.
- **What it causes.** On screen: flat, self-lit effects in a ray-traced scene; smearing of moving particles under TAA; wrong glass-over-smoke ordering. The camp scene has four emitters.
- **Measured?** The weight exponent was measured (particle_weighted.rvshader:60-80). The glass/particle constant mismatch and the velocity blend are not measured.
- **Already recorded?** No.
- **Fix direction.** Share one weight function between meshes and particles. Mask off, or give real motion to, the non-colour attachments in particle pipelines. Light particles from the froxel volume (MAT-13) or the irradiance field, plus the direct-light sampler at particle centres.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/particle.rvshader:46 - 46-50: velocity written as zero; colour = texture * vertex colour (no lighting)
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/ParticleRenderer.cpp:275 - 275-289: the alpha pipeline carries the scene's velocity, normal and indirect formats under one blend preset
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanCommon.cpp:432 - 432-438: AlphaBlend uses SRC_ALPHA on every attachment; the vec2 velocity output has no alpha, so its blend weight is undefined
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/particle_weighted.rvshader:36 - kUnitWeightDistance = 90.0, against 20.0 for meshes (pbr_fragment.glsl:6264), whose comment at 6255-6257 claims 'the same unit-weight distance'
- **Skeptic's note.** Confirmed. Sorted and additive particles are unlit by design (particle.rvshader:3-7) and write zero velocity (46-50). Their pipelines apply one blend preset to every attachment (ParticleRenderer.cpp:275; VulkanPipeline.cpp:344-360), so the vec2 velocity output blends by an undefined source alpha. The unit-weight constants are 90 for particles (particle_weighted.rvshader:36) against 20 for glass (pbr_fragment.glsl:6264), which contradicts the 'same unit-weight distance' comment (6254-6256). Correction: the 4.5:1 mismatch is latent. No demo scene uses WeightedBlended emitters; only the three particle test scenes do, and the camp's four emitters are Additive and Alpha. The larger particle problems are ones the inspector missed: these pipelines raise the alpha lane that carries the traced reflection's weight, and they are composited under glass and water (materials-s1, materials-s2).

#### MAT-17 · Specular can overflow half floats since RT-21 removed the highlight cap

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** stability. **Scope:** patch.
- **Roadmap:** RT2-6
- **What is wrong.** Since RT-21 lowered the GGX divisor floor, a zero-radius light delivering roughly 30-70 radiance units to a flat metal at the minimum roughness can exceed half float's 65504 in the direct specular target. Once a value becomes infinite it can turn into NaN downstream. The case is narrow and unmeasured; a finite storage cap beside include/half_float.glsl plus an inf/NaN counter would close it.
- **What it causes.** Possible stuck black or white pixels on polished metal lit by point lights (inferred, unmeasured). The garage's lights are sized, so the risk is latent there.
- **Measured?** Not measured.
- **Already recorded?** RT-21 chose the divisor floor over a roughness minimum (owner's call); the storage overflow is not discussed.
- **Fix direction.** Cap the specular before half-float writes at a finite storage ceiling, next to include/half_float.glsl's helpers, keeping RT-21's look. Add an inf/NaN counter to the benchmark.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:3413 - 3413-3430: the GGX divisor floor is 1e-9; roughness is clamped at 0.045 (4203)
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/direct_trace.rvshader:439 - 439-456 and 1406-1407: the specular is written to the RGBA16F pair with no clamp
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_accumulate.rvshader:1426 - 1426-1449: the firefly clamp is compiled out for RV_SIGNAL_PAIR and RV_SIGNAL_DIFFUSE; reflections are clamped to 64 at the trace (reflection_trace.rvshader:1013), the direct pair is not
- **Skeptic's note.** The peak is overstated about 19-fold. DistributionGGX floors its divisor at 1e-9 (pbr_fragment.glsl:3413-3430). At the 0.045 roughness clamp (4203), a = 0.002025 and a^2 = 4.1e-6, so the true divisor pi*a^4 = 5.3e-11 sits below the floor, and the peak is a^2/1e-9 = about 4.1e3, not 7.8e4. The floor still binds below roughness about 0.065. The rest holds: the direct pair is written to RGBA16F without a clamp (direct_trace.rvshader:1406-1407), and the accumulator's firefly clamp is compiled out for the pair (reflection_accumulate.rvshader:1426-1449). At the peak the specular is roughly 1000x the light's radiance head-on and about 2000x at grazing. Overflowing half float therefore needs a zero-radius light delivering roughly 30-70 units of radiance to a flat polished metal. Sized lights, such as the garage tubes, are safe because the lobe widens. Unmeasured.

#### materials-s1 · Lamp glows, sorted and additive particles and world UI inflate the traced reflection's weight, which lives in the colour's alpha

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-6, RT2-22
- **What is wrong.** The lit shader stores 'how much traced reflection to add at this pixel' in the colour target's alpha channel. The reflection composite later multiplies the reflection picture by that number. Anything blended into the colour target in between changes the number. The lamp glow sprites write alpha 1 with additive blending, so each glow quad adds 1 to the weight. Sorted-alpha particles set it to at least their own alpha, and additive particles and world-space UI raise it too. The engine already knows the rule: the see-through resolve was given a special 'under' blend so glass lowers the weight instead of raising it. The glow, particle and world-UI pipelines were never changed to match.
- **What it causes.** On screen (inferred): wherever a glow quad (about 26 px on the bridge profile) or an alpha/additive particle overlaps an opaque glossy surface inside the gloss window (roughness under 0.6), the composite adds the reflection at a weight of about 1 instead of the surface's own few percent. That makes the reflection roughly 5-25 times too strong there: a bright square or disc of reflected light around lamps and under particles. Live on the bridge (glows on, RT reflections on); latent for particles in the demo scenes; editor gizmo and overlay lines do the same.
- **Measured?** Not measured.
- **Already recorded?** No. HANDOFF.md:1849 records adding AlphaBlendUnder for the transparent resolve only; the glow (WR-5, built later), particle and world-UI pipelines were never updated.
- **Fix direction.** Stop sharing the weight with blended draws. The simplest option is to draw glows, particles and world UI after the reflection composite, since none of them is a reflecting surface. Alternatively give those pipelines alpha factors that leave the weight alone (source zero, destination one) or lower it (the resolve's 'under' preset). The cleanest option is to move the weight into a lane of its own. Verify with a per-pixel diff at the bridge headland camera, glows on against off, restricted to glossy opaque pixels.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:6492 - the lit shader stores the traced reflection's weight in the colour alpha; 6487-6491: 'the transparent resolve attenuates it by its coverage, nothing else reads it'
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_composite.rvshader:108 - weight = scene.a; line 110 adds weight * the reflection picture to the colour
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Renderer3D.cpp:5074 - LightGlow::Draw runs at the end of the opaque lit pass, into the scene target whose alpha is the weight
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/LightGlow.cpp:80 - BlendPreset::Additive on every attachment (comment 78)
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/light_glow.rvshader:247 - o_Color = vec4(colour, 1.0): alpha 1 across the whole glow quad
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanCommon.cpp:448 - 448-453 Additive alpha factors ONE/ONE (alpha += 1); 432-437 AlphaBlend alpha ONE/ONE_MINUS_SRC_ALPHA (alpha = As + Ad(1-As))
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/ParticleRenderer.cpp:332 - only the OIT resolve uses AlphaBlendUnder, 'Under, not over, for the alpha: the scene's alpha is the traced reflection's weight' (329-331); the particle alpha pipeline is plain AlphaBlend at line 275, and UIRenderer.cpp:173/217 likewise
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:5466 - particles are drawn last inside the Scene pass, into the same target
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/PostSettings.h:310 - LightGlow defaults to true; bridge_cinematic.rvpostprofile:12 turns it on, with a 24 px flare

#### materials-s2 · Glows and sorted/additive particles are composited behind every pane of glass and behind the sea, whatever their depth

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-6, RT2-33
- **What is wrong.** Glows and non-weighted particles are painted into the opaque image before glass and water are drawn. They leave no depth behind, so the later glass and water draws cannot tell that a glow or particle stands in front of them. The see-through composite then lays the glass or water over them: a pane dims anything in front of it by its coverage, and the sea, which owns its pixels outright when refraction is traced, replaces them entirely. The weighted particle path avoids this because it shares the see-through sum.
- **What it causes.** On screen (inferred): on the bridge, a lamp's glow is erased wherever the sea lies behind it. This is the headland view looking down on the deck lamps, the case the glow was built for. Sparks, smoke or fire in front of water disappear, and in front of glass they are dimmed and tinted as if behind it.
- **Measured?** Not measured.
- **Already recorded?** No. The comment at Renderer3D.cpp:5066-5070 treats the water covering the glow as intended.
- **Fix direction.** Draw glows and non-weighted particles after the transparent resolve. They can be depth-tested against the opaque depth plus the water-surface and glass-layer depths the frame already keeps (FrameGraphBuilder.cpp:642-700). Alternatively route them into the OIT sum as weighted fragments. Verify with a headland diff of lamp glows over water and a particle-in-front-of-glass fixture.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Renderer3D.cpp:5069 - the glow is drawn 'before the transparent pass so the water composites over it' (5066-5074)
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:5466 - sorted and additive particles are drawn at the end of the opaque Scene pass
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/LightGlow.cpp:84 - depth write off (ParticleRenderer.cpp:281 the same for particles): neither leaves any depth behind
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:3645 - 3645-3657: the Transparent pass keeps only the scene's opaque depth, so glass and water fragments behind a glow or particle still pass
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:6398 - traced-refraction water sets alpha = 1 ('The pixel is owned outright', 6457-6462); coverage 1 gives revealage 0 (6468-6477)
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/oit_resolve.rvshader:65 - the resolve blends the transparent average over the scene by coverage (with ParticleRenderer.cpp:332), replacing the scene where coverage is 1

#### materials-s3 · The G-buffer's object id is a per-frame list position for every CPU-drawn object, so terrain, skinned meshes and water get new ids when culling changes and lose their history

- **Verdict:** added by the skeptic. **Severity:** high. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-5
- **What is wrong.** Each G-buffer pixel carries an object id that means 'the same object as last frame'. TAA and the accumulators use it to decide whether a pixel's history can be reused. For GPU-culled static meshes the id is the object's slot in a cached table, so it stays put. For everything drawn through the CPU list (terrain chunks, skinned meshes, water, small meshes, and every mesh under --meshlets) it is just the order in which this frame's surviving draws were submitted. When one object enters or leaves the view, every object submitted after it gets a new number.
- **What it causes.** When the numbers shift, TAA and every accumulator except the sea's treat those pixels as a different object and throw their history away for that frame. On the bridge most draws are CPU-list terrain chunks. During a pan, each chunk crossing the frustum edge renumbers the chunks after it, so large parts of the terrain lose their TAA, direct-light, AO and GI history for a frame. Expected on screen: one-frame aliasing and noise pops (inferred). Skinned characters are hit the same way; the garage's static meshes are not.
- **Measured?** Not measured. RT-19's 'object id' refusal counter has only been read on the garage with a moving panel (0.2%, RT-SERIES.md:1632).
- **Already recorded?** No. RT-FIRST.md:81 (T3) made the id the record index at all five fill sites; RT-17 fixed the ray-instance version of this problem but the G-buffer id was not moved to a stable identity.
- **Fix direction.** Write a stable per-entity id (the RT-17 identity, or the entity handle folded into 24 bits) into Extra.x at all five fill sites, separate from the instance index. Measure first: RT-19's object-id refusal share during a bridge pan, then a per-pixel diff of a panning burst before and after.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Renderer3D.cpp:1415 - AllocateInstance: the instance index is the current size of this frame's array (1413-1417)
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Renderer3D.cpp:9826 - DrawLayeredMesh: Extra.x = array position + 1; the same in DrawMesh (9639), DrawWaterMesh (9690) and DrawSkinnedMesh (9773). Only the GPU table rows (9109-9111) are 'stable for as long as the scene's draw order is'
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:5303 - terrain chunks outside the frustum are skipped before DrawLayeredMesh (5309); CPU-list meshes are culled the same way at 5191
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:1927 - under --meshlets no GPU cull table is made, so every mesh goes through the CPU path
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/taa_resolve.rvshader:404 - 404-410: TAA refuses history outright when the id differs; taa_guide.rvshader:60-62 stores the G-buffer id
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_accumulate.rvshader:319 - SameObject hard refusal (also 1126); Renderer3D.cpp:7435 and 7563 exempt only the sea's signal, so reflection, direct, AO, GI and glass all test ids
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:1667 - on the bridge 184 of its 201 draws are on the pending (CPU) list, the terrain among them
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:2111 - RT-17 met the same issue for ray instances and used a stable entity-derived identity because 'the row index is rebuilt every frame in arrival order'

#### materials-s4 · Under the sea, the seabed is G-buffered, ray traced and lit every frame, then covered completely by the water

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-32
- **What is wrong.** The G-buffer does not know about the water, so every sea pixel holds the sea floor below it. The opaque direct-light, AO and GI passes, and the forward lit pass, do full work on that sea floor. Then the sea, drawn later, covers those pixels completely and shows its own refracted view of the floor. All the opaque work there is thrown away.
- **What it causes.** Frame time (inferred): a share, proportional to how much of the frame is sea, of the terrain's measured +1.5 ms of traced passes and of its G-buffer and lit-pass time at Headland and the Pier. Accumulator work and memory are also spent on pixels nobody sees.
- **Measured?** The whole terrain's cost is measured (RT-SERIES.md:1687); the underwater share is not.
- **Already recorded?** No.
- **Fix direction.** Give the frame the water's coverage before the opaque signals. Draw the sea's existing surface layer right after the G-buffer, and in RT mode (refraction traced) skip pixels the sea owns outright in DirectTrace, RTAO, GI, the reflection passes and the lit pass, with a stencil or early depth reject. The raster-refraction path still needs the lit backdrop and keeps it. In MAT-03's end state this is the transmissive layer marking full coverage. Prove with A,B,B,A timings at Headland and the Pier and a per-pixel diff, where the target is an identical picture.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/SampleProject/assets/scenes/GoldenGateDemo.rage:3611 - the bridge's single terrain is tagged 'Seabed' (2800 m, base at -125 m): land and sea floor in one
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1869 - DirectTrace (1869), OcclusionCompute (2041), GI trace (2189) and ReflectionTrace (2444) all run before the Scene pass (2609); the WaterSurface pass only comes at 3096, so none of them knows where the sea is
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/direct_trace.rvshader:91 - 'the sea writes no depth, and the buffer under it holds the seabed'
  - C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/include/pbr_fragment.glsl:6398 - the traced-refraction sea sets alpha 1 and owns the pixel; it re-shades the bottom with its own simplified hit shade (6369-6389)
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:1687 - removing the terrain (land and seabed together) removed +1.5 ms of DirectTrace, ReflectionTrace and the accumulates at Headland, called 'legitimate', plus most of the G-buffer and lit-pass cost

### Geometry, acceleration structures and scene scale

**State of the area.** RageV's geometry side is a clean, well-instrumented design that rebuilds everything from the ECS every frame. It is correct and fast up to the low tens of thousands of objects, and it has no mechanism for anything larger. Every frame, for every mesh entity, static or not, the engine: walks the transform hierarchy three to seven times; rebuilds its draw list and GPU-cull table twice; writes a 272-byte raster instance row (with a matrix inverse); re-adds the object to the ray tracer's top-level structure (TLAS); and writes a 144-byte hit-shading row. Bottom-level structures (BLAS) are built once per mesh at first trace, each with a blocking GPU round trip, and are never compacted. The TLAS is repacked on the CPU and refit or rebuilt every frame. Skinned characters are posed by compute and refit one at a time. There is no mesh LOD, no ray LOD or far-field proxy (terrain rays always trace full detail), no streaming and no GPU-memory accounting. A model's material sections each become their own entity, mesh, BLAS and TLAS instance. Culling is GPU frustum-only, with one indirect draw per distinct mesh per pass. Correction to the brief: the GPU-driven lit path (--gpu-lit) is ON by default (EngineConfig.h:990) and has been pixel-matched since 2026-08-24 (HANDOFF.md:6549-6560); it needs bindless. The meshlet path is off and dormant. Measured, with ray tracing OFF: 60k objects run at 78 FPS and 120k at 34 FPS, CPU-bound (HANDOFF.md:7022-7029). After roadmap 8.15 the transform walk is 2.6 of about 12.8 ms at 60k; the lit pass's per-object walk and submission is now the larger share (HANDOFF.md:7117-7121). The ray-traced path has never been on the scale curve. Today's demo scenes are GPU-bound on lighting (bridge: 2.85M triangles, 200 draws, 1 ms of CPU; RENDERING-REVAMP.md:2116), so none of this shows on screen yet. What would hold back a 100k-instance open world, in order: per-object re-extraction (GEO-01) and the transform walk (GEO-02) on the CPU; the TLAS walk, pack and hit table (GEO-04; the pack alone extrapolates to about 21 ms); first-use BLAS stalls and uncompacted BLAS memory (GEO-05); full detail at every distance with nothing streamed (GEO-07, GEO-08); for multi-part models, the ECS's one-million-entity limit (GEO-06). One latent synchronisation defect was also found: the GPU cull's object table is single-buffered across two frames in flight (GEO-12).

**What to keep.**
- The RHI acceleration-structure primitives. AccelerationGeometryDesc already takes vertex and index offsets and strides (RHITypes.h:189), so BLAS can read from a shared geometry arena without an API change. AccelerationInstance carries a mask and a per-instance ForceNoOpaque (RHITypes.h:213).
- The write-combined-memory lesson in the TLAS pack: assemble each instance in cached memory and store it whole (VulkanResources.cpp:1255-1267). It took the pack from 2.96 to 0.219 ms.
- One TLAS per frame in flight, plus an always-valid empty stand-in (RayShadows.cpp:186-199). Also the refit rule: refit only when the count matches, rebuild at least every 64 frames (VulkanResources.cpp:1296-1330).
- One BLAS per mesh, shared by every instance of it, which is real instancing in the ray world; and the primitive cache (Mesh.cpp:233-255).
- The GPU-address registry that names a freed BLAS before the GPU faults on it (VulkanResources.cpp:1155-1184).
- The rule 'an acceleration structure is world state, a LOD is view state' (Terrain.h:116-128, ENGINE-NOTES 7bp). Any future ray LOD must be chosen once per frame for all views.
- The static/moving instance-mask split, and what it gives the bake and the subtractive shadow (RayShadows.h:83-93, ENGINE-NOTES 7cx).
- GpuCull's layout. Each mesh slot owns a reserved range and an atomic hands out places, so nothing needs sorting. Past the device ceiling it refuses rather than truncates (GpuCull.cpp:121-164, 355-374).
- The --gpu-cull and --gpu-lit A/B switches and check_gpu_lit.py's parity guard: the model for proving every GPU-scene step bit-identical.
- The hit-shading table reached by buffer device address, sharing material records with raster (Renderer3D.cpp:5563-5664). The GPU scene's hit data should keep this shape.
- Culling bounds for skinned meshes that cover every animation clip, not just the bind pose (AssetManager.cpp:686-703).
- The compare-not-trust transform walk (Scene.cpp:302-348), kept as a debug validator once writes are tracked.
- Conservative, reverse-Z-aware frustum maths shared by the CPU and GPU (Frustum.cpp:6-89, cull_lit.rvshader:97-122).
- MeshCook's version stamp and IsCurrentVersion check (MeshCook.cpp:25, 196). This is where cooked streams, clusters and LOD chains should live.
- The measurement tools: bench_scale.py, --pass-timings, and RayShadows::GetGeometryKey, a hash of the structure's contents that can also drive 'skip the TLAS build when unchanged'.

**How it should look in an RT-first engine** (the inspector's view; the roadmap decides). Treat the scene as persistent GPU state that changes a little each frame, not something rebuilt from the ECS every frame.

(1) Transforms. The hierarchy lives in flat arrays, parents before children, linked by parent index. Writes are tracked by the ECS itself, so world matrices are recomputed once per frame for changed subtrees only. Static objects are never revisited. The old compare walk survives as a debug validator.

(2) GPU scene. Every renderable owns a stable slot in one device-local instance table: world, previous world, bounds, mesh and LOD, material offset, flags and ray mask. Only changed rows are uploaded each frame. Culling, raster, TLAS instance generation, hit shading, the G-buffer id and the reflection identity all use this one index. CPU cost follows changes, not objects.

(3) Geometry assets. MeshCook produces, offline:
- quantized position and attribute streams, including a tangent frame;
- 16-bit indices where the vertex count allows;
- clusters with bounds and normal cones;
- an LOD chain.
A model is one mesh with several material sections, all stored in large pooled geometry buffers.

(4) Raster. The GPU culls instances, then clusters: frustum first, and two-phase occlusion only if it pays on dense fixtures. The GPU chooses LOD, then draws compacted commands with one indirect-count call per pipeline, using bindless materials.

(5) Ray tracing. A BLAS manager builds one multi-section BLAS per mesh LOD when the mesh arrives: batched, on an async compute queue, compacted, pool-allocated, within a per-frame build budget and a tracked memory budget. A compute pass writes the TLAS instance buffer from the GPU scene. The TLAS build is skipped when nothing changed, refit when only transforms moved, and rebuilt on structural change. Ray LOD is chosen once per frame for every view, with far-field proxies and translucent and small-detail sets in their own mask bits, selected per ray type by global render settings. Alpha-tested meshes carry opacity micromaps. Skinned meshes are skinned once per frame into a buffer used by raster, motion vectors and batched BLAS refits, with rebuilds when deformation grows.

(6) Streaming. World cells, asynchronous IO and a transfer queue. Residency by distance and screen size under a VRAM budget from VK_EXT_memory_budget. BLAS built and compacted on arrival, evicted together with the geometry.

(7) Measurement first. The scale bench gains RT arms, TLAS/BLAS timings and memory lines, and still/moving pairs. Each step above is adopted only when palindrome timings and per-pixel diffs support it.

Getting there means rewriting scene extraction, the GPU scene and acceleration-structure management, and refactoring mesh cooking. The RHI's acceleration-structure primitives, GpuCull's slot layout and the parity checks carry over.

#### GEO-01 · No persistent scene on the GPU: every object is re-extracted into four separate per-instance tables every frame

- **Verdict:** confirmed. **Severity:** high. **Kind:** architecture. **Scope:** rewrite.
- **Roadmap:** RT2-34, RT2-43
- **What is wrong.** The renderer keeps no scene between frames. Every frame it walks the ECS and writes, for every mesh entity:
- a draw-list entry and a 96-byte GPU-cull record, twice, because the draw list is rebuilt twice a frame;
- a 272-byte raster instance row, with a matrix inverse;
- a 64-byte entry in the top-level acceleration structure (TLAS: the ray tracer's search tree over every placed object);
- a ~260-byte CPU record for that entry;
- a 144-byte hit-shading row.
Each of these carries its own copy of the world matrix, and each is keyed by a different index. The TLAS walk re-resolves mesh, material and parameters from the asset manager instead of reusing the draw list. The Static tick exempts an object from none of this.
- **What it causes.** Frame cost grows with the number of objects, not with what changed; this is the measured CPU wall. Mapped-memory writes come to about 0.7 KB per object per frame (about 70 MB a frame at 100k objects), plus about as much again in CPU-side records, repeated per view in the editor. Raster and ray passes cannot agree which object a pixel shows, because each uses a different id.
- **Measured?** Measured with ray tracing off (HANDOFF.md:7022-7029, 7117-7121): 60k objects = 78 FPS (~12.8 ms, CPU-bound), with 'the lit pass's own walk and submission' the largest share; 120k = 34 FPS. ENGINE-NOTES.md:11716-11724: re-uploading the cull table on every refresh was a measured net 1.6 ms loss at 60k. It was reinstated for correctness in e248ee2 (2026-08-21), and the 78 FPS figure includes it. TLAS pack: 0.219 ms for 1021 instances (ROADMAP.md:673). Per-object byte totals are inferred from the struct sizes.
- **Already recorded?** Partly. ROADMAP.md:853 says instance data, materials and probe selection are still per object on the CPU. ENGINE-NOTES 7bx calls GpuCull 'scaffolding with one of its two users built'. No document proposes a persistent GPU scene, and docs/RT-FIRST.md has no geometry or instance item.
- **Fix direction.** A GPU-resident scene.
- Each renderable gets a stable slot in one device-local instance table when it is created, freed on destroy.
- A row holds world 3x4, previous world, world bounds, mesh/LOD id, material-table offset, flags and ray-mask bits.
- Only the rows whose entity changed are copied up each frame, driven by the transform system's change list (GEO-02).
- Culling, raster, TLAS instance generation (a compute pass), hit shading, the G-buffer id and the reflection identity all use that one index.
Per-frame CPU cost then tracks changes, not objects. Prove it on the RT scale bench (GEO-03) with still and moving arms, A,B,B,A, and a zero pixel diff on the garage and bridge.
- **Evidence:**
  - RageV/src/RageV/Scene/Scene.cpp:1956 - RefreshDrawList rebuilds the whole draw list for every mesh entity: asset lookups for mesh and material, world bounds, emitter test and cull-table row. It runs twice per runtime frame: RenderShadowMaps at 2541 and OnRender at 5028.
  - RageV/src/RageV/Scene/Scene.cpp:1970 - Comment and code (1970-1986): the 96-byte-per-object GPU cull table is rebuilt and re-uploaded on every refresh, 'a correctness rule'; SetObjects is at 2435.
  - RageV/src/RageV/Scene/Scene.cpp:5133 - SetSceneInstance is called every frame for every object in the cull table, visible or not (loop 5118-5159).
  - RageV/src/RageV/Renderer/Renderer3D.cpp:9095 - Per object per frame: a 3x3 inverse for the normal matrix and a 272-byte InstanceData row (struct 377-421, size assert 420). The table is cleared at 4795, resized at 9079 and uploaded whole at 5735.
  - RageV/src/RageV/Scene/Scene.cpp:2862 - The TLAS walk (2862-2941) re-walks the ECS rather than the draw list. It re-resolves mesh, material and 128-byte params for every entity and calls RayShadows::AddInstance, which copies three shared pointers into a ~260-byte RayCaster record (RayShadows.cpp:261-345).
  - RageV/src/RageV/Renderer/Renderer3D.cpp:5572 - Every frame, and per view because EndScene runs per viewport: a 144-byte ray-instance row per TLAS instance (struct 129-151), with a hash lookup for its material (5575) and an emitter-owner scan (5617).
  - RageV/src/RageV/Scene/Scene.cpp:4738 - ProbeSlotFor scans every probe, per object per frame. Static (2125) only chooses lighting paths and exempts no object from any of these steps.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:5657 - Three identities for one object: TLAS build order is the hit's custom index (RayShadows.cpp:270); the instance row + 1 is the G-buffer id (Renderer3D.cpp:9111); the entity handle folded to 1..1021 is the reflection identity (here).
- **Skeptic's note.** Every cited line says what the finding claims. RefreshDrawList (Scene.cpp:1956) runs in RenderShadowMaps (2540-2541). OnRenderRuntime's own transform walk (1482) then raises the dirty flag again (358), so OnRender rebuilds the list (5028): two full rebuilds a frame, and each one re-uploads the 96-byte cull table (2435; GpuCull.h:79-87). SetSceneInstance runs for every table object in every view (5118-5159), with a 3x3 inverse each time (Renderer3D.cpp:9095). The TLAS walk re-resolves mesh, material and params from the ECS (2862-2915). EndScene builds a 144-byte hit row per TLAS instance per view (5563-5664). The Static flag only picks lighting paths (2125-2127). The measured numbers check out (HANDOFF.md:7022-7029 and 7117-7121; ENGINE-NOTES.md:11716-11724). The per-refresh table rebuild came back in e248ee2 on 2026-08-21, before the 78 FPS figure was taken. The byte totals are inferred, and they add up: about 670 bytes of mapped writes per object. Two qualifications. First, the hit table is only built while ray-traced reflections, GI or refraction are on (5563-5565). Second, 'raster and ray cannot agree on ids' has no consumer today; the id problems that actually bite are the missed items geometry-s1 and geometry-s2. Severity is high rather than critical because no shipped scene is CPU-bound (the bridge uses 1 ms of CPU, RENDERING-REVAMP.md:2116) and the ray-traced per-object costs have never been measured (GEO-03).

#### GEO-02 · The transform walk visits every entity three to seven times a frame, and each visit forces a draw-list rebuild

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-34, RT2-43
- **What is wrong.** UpdateWorldTransforms walks every root and every child, with a hash lookup per child, three to seven times a frame. The call sites are: after each fixed step, in the frame update (twice when scripts run), for probe captures, in the shadow pass, in the render call, for voxel GI when it is on, and for particles when emitters exist. AdvanceMotionHistory adds one more pass over every entity. Measured: 2.6 ms at 60,000 objects after roadmap 8.15, about 5 ns per entity per walk. Each walk raises the draw-list dirty flag whether or not anything moved. OnRenderRuntime walks again after the shadow pass has rebuilt the list, so the draw list and the GPU-cull table are rebuilt twice every frame.
- **What it causes.** Work proportional to the entity count, repeated several times per frame. The count grows with the number of fixed steps a slow frame runs, a feedback spiral recorded in ENGINE-NOTES 7bx. At the ECS limit of one million entities, the walk alone would take tens of milliseconds.
- **Measured?** ENGINE-NOTES.md:11694-11705: before 8.15, 7 walks a frame at 60k objects cost 27.4 ms of a 52 ms frame. After the compare fix: 2.6 ms at 60k (HANDOFF.md:7118-7119), about 5 ns per object per walk (Components.h:105-108). The one-million figure is a linear extrapolation (inferred).
- **Already recorded?** The walk and its remaining 2.6 ms are recorded (8.15). The dirty flag was rejected on robustness grounds, not by measurement. The direction below removes the forgotten-flag failure by construction, so it is not a retry of the rejected idea.
- **Fix direction.** Make change tracking something no writer can forget.
- Position, rotation and scale writes go through the ECS: a mutable accessor or setters that stamp the component with the frame number. C#, physics and the serializer use the same path. This is an API change for native script modules, which write the fields directly today, so it must land with a Sample.dll rebuild.
- Keep the hierarchy in flat arrays, parents before children, linked by parent index instead of UUID.
- Compute world matrices once per frame, for changed subtrees only, and never revisit Static entities.
- Hand the list of changed entities to the GPU scene.
- Keep today's compare walk as a --validate-transforms debug mode that fails loudly on any untracked write.
- **Evidence:**
  - RageV/src/RageV/Scene/Scene.cpp:358 - UpdateWorldTransforms (350-371) marks the draw list dirty unconditionally, then recurses from every root.
  - RageV/src/RageV/Scene/Scene.cpp:343 - Every child is found by UUID through a hash lookup (GetEntityByUUID) on every walk; each node compares position, rotation and scale with cached copies (318-321).
  - RageV/src/RageV/Scene/Scene.cpp:890 - Call sites: frame update (890, again at 913 when scripts run), after every fixed step (1239), shadow pass (2540), render call (1482/1505), voxel GI (3141) and probe capture (3313).
  - RageV/src/RageV/Scene/Components.h:50 - Children are a heap vector of UUIDs per entity. TransformComponent (64-130) is about 200 bytes and stores Euler rotation.
  - RageV/src/RageV/Scene/Components.h:73 - Dirty flags were rejected because a write site could forget to raise one (also Scene.cpp:307-317; ROADMAP.md:669).
- **Skeptic's note.** Most of it checks out: the walk, its call sites, the hash lookup per child (Scene.cpp:341-345 and 110-116), the dirty flag raised every time (358), and the measurements (ENGINE-NOTES.md:11694-11705; HANDOFF.md:7118; ROADMAP.md:669). The title overstates one thing. A walk only raises the flag; the list is rebuilt at the refresh points, which happens twice a frame (in RenderShadowMaps, then in OnRender after OnRenderRuntime's walk re-dirties it), not once per walk. Two other per-entity passes are missing from the finding: AdvanceMotionHistory (827-835) and the particle walks (ParticleSystem.cpp:207, GpuParticles.cpp:415). The owner rejected dirty flags for robustness, not by measurement (Components.h:73-83), and the proposed tracked-write design answers that reason. It does change TransformComponent's contract with native script modules (Components.h:99-103).

#### GEO-03 · The object-count benchmark has never measured the ray-traced path

- **Verdict:** confirmed. **Severity:** high. **Kind:** missing-capability. **Scope:** patch.
- **Roadmap:** RT2-2
- **What is wrong.** bench_scale.py is the only scale measurement, and all its runs have ray tracing off. None of the per-object ray-tracing costs has a number at any object count: the TLAS walk and pack, the hit-shading table, first-use BLAS builds, skinned refits, or TLAS build time on the GPU.
- **What it causes.** The engine is RT-first, but its scale limits are unknown on the path it ships with. Under the owner's measurement rule, nothing in GEO-01, 04, 05 or 07 can be proven until this exists.
- **Measured?** Not measured; that is the finding. The only per-instance RT number is the TLAS pack: 0.219 ms for 1021 instances (ROADMAP.md:673). Linear extrapolation gives about 21 ms at 100k instances for the pack alone (inferred).
- **Already recorded?** No.
- **Fix direction.** Add RT arms to bench_scale:
- --raytracing=on at the project's reflection and GI presets;
- report TLAS instance count, TLAS build/refit GPU time, BLAS bytes, and the CPU time of the TLAS walk, the pack and the hit table as separate phases;
- a still-scene / moving-scene pair, so an incremental design can be shown to cost nothing when nothing moves;
- run as A,B,B,A palindromes.
- **Evidence:**
  - tools/scripts/bench_scale.py:37 - Every run passes --render-defaults=on (37-40) and no ray-tracing flag.
  - RageV/src/RageV/Core/EngineConfig.h:54 - --render-defaults=on replaces the project's Render Settings with RenderSettings{}.
  - RageV/src/RageV/Renderer/RenderSettings.h:374 - RenderSettings::RayTracing defaults to false.
  - docs/HANDOFF.md:7022 - The recorded 1k-120k curve (7022-7029) is therefore the raster fallback.
- **Skeptic's note.** bench_scale.py passes --render-defaults=on and no ray-tracing flag (lines 37-40). --render-defaults replaces the project settings with RenderSettings{} (EngineConfig.h:53-56), and its RayTracing field defaults to false (RenderSettings.h:374). No document records an object-count curve with ray tracing on. Under the owner's measure-first rule, this gap blocks proof for GEO-01, 04, 05 and 07 and for the missed item geometry-s1.

#### GEO-04 · The TLAS is repacked on the CPU and rebuilt or refit every frame for every instance, even when nothing moved

- **Verdict:** confirmed. **Severity:** high. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-4, RT2-35, RT2-43 (The patches available today land early)
- **What is wrong.** Each frame RayShadows clears its list, the scene re-adds every instance, and the Vulkan backend packs every instance into mapped memory and records a build. The build is a refit (updating the tree's boxes in place, keeping its shape) when the instance count matches, and a full rebuild otherwise or every 64 frames. No path skips the build when nothing changed, even though a content hash of exactly this already exists. A refit is also taken when one object is destroyed and another spawned in the same frame: same count, different contents. That leaves a tree fitted to the old scene for up to 64 frames.
- **What it causes.** CPU time linear in instances every frame (about 0.21 µs per instance for the pack alone), on top of the GEO-01 walk and tables. The GPU pays a refit every frame even with a parked camera over a static world.
- **Measured?** ROADMAP.md:673: 2.96 -> 0.219 ms of CPU for camp's 1021 instances after the fix. TLAS build/refit GPU time against instance count has never been measured.
- **Already recorded?** Partly: the pack fix and the refit rule are recorded (ROADMAP 8.12, code comments). Rebuilding from the CPU every frame is not questioned anywhere.
- **Fix direction.** - Generate the TLAS instance buffer on the GPU from the GPU scene (GEO-01), with a compute pass that writes only moved instances.
- Skip the build when no transform, mask, flag or BLAS changed; the geometry key already computes this.
- Refit when only transforms moved; rebuild on structural change or at the refit limit.
- A patch available today: use a raw static_cast in the pack loop (VulkanResources.cpp:1237).
- Measure TLAS GPU cost at 1k, 10k and 100k instances before choosing thresholds.
- **Evidence:**
  - RageV/src/RageV/Renderer/RayShadows.cpp:434 - BuildTopLevelAS is recorded every frame with the full instance list.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1237 - The pack loop (1231-1276) runs every frame for every instance. std::static_pointer_cast creates and destroys a shared_ptr per instance: two atomic operations each.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1308 - Refit only when the count matches, with a full rebuild at least every 64 frames (1296-1330). There is no path that skips the build when nothing changed.
  - RageV/src/RageV/Renderer/RayShadows.cpp:443 - GetGeometryKey hashes exactly the structure's contents, but only under the measured-change flag (443-467).
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1250 - The recorded measurement: 2.96 ms of pack CPU for camp's 1021 instances before the write-combined fix.
- **Skeptic's note.** RayShadows clears and re-adds every instance (RayShadows.cpp:250-345), and Build records a full BuildTopLevelAS every frame (434). The Vulkan pack loop runs for every instance and copies a shared_ptr each time (VulkanResources.cpp:1237). It refits only when the instance count is unchanged, forces a full rebuild every 64 frames (1308-1310), and has no path that skips the build. GetGeometryKey is computed only under --measured-change, and only after the build (RayShadows.cpp:443-467). A refit with the same count but different contents is legal and rays stay correct; only the tree's efficiency drops, which is what the finding says. The 0.219 ms is measured (ROADMAP.md:673). The 21 ms at 100k instances is a straight-line extrapolation, and the GPU cost of the build or refit has never been measured. One point for the fix: there is one TLAS per frame in flight (RayShadows.cpp:96-99), so 'unchanged' has to be judged against that slot's own last build, two frames back.

#### GEO-05 · Each mesh's BLAS is built at its first trace with a blocking GPU round trip, never compacted, and not counted against any memory budget

- **Verdict:** confirmed. **Severity:** high. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-35, RT2-18
- **What is wrong.** A bottom-level acceleration structure (BLAS) is the ray tracer's search tree over one mesh's triangles. Here it is built the first time a frame's TLAS walk needs it, inside that frame. Each build allocates scratch, submits and waits on a fence, then frees the scratch; mesh uploads use the same blocking path. Static BLAS are built without the compaction flag, so they keep the driver's worst-case size. (Compaction copies a finished BLAS into the smaller size the driver reports afterwards.) Each BLAS owns a separate buffer, and nothing tracks GPU memory.
- **What it causes.** One stall per new mesh. Switching ray tracing on, loading a scene or spawning a new asset serialises dozens to hundreds of GPU round trips into one frame. BLAS memory stays uncompacted on a 12 GB laptop GPU. With no budget, running out of memory is the first warning, and nothing can be streamed on this path.
- **Measured?** Not measured: no hitch time, BLAS size or compaction ratio is recorded anywhere. Inferred from code.
- **Already recorded?** No. ENGINE-NOTES 7am records 'built once, immediately, the way a texture is uploaded' as the design.
- **Fix direction.** A BLAS manager:
- build when geometry is loaded, not at first trace;
- many BLAS per vkCmdBuildAccelerationStructuresKHR call, on an async compute queue, within a per-frame build budget;
- query compacted sizes and copy into compacted allocations;
- sub-allocate storage and scratch from pooled buffers;
- report BLAS bytes per category (static, skinned, terrain) in the benchmark;
- evict together with streamed geometry (GEO-08).
Measure the compaction saving and any trace-speed change on the garage and bridge before committing.
- **Evidence:**
  - RageV/src/RageV/Renderer/Mesh.cpp:214 - The BLAS is built the first time the frame's TLAS walk asks for it (209-231; called from RayShadows.cpp:338 inside RenderShadows).
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1046 - Each static BLAS allocates its own scratch buffer, builds through ImmediateSubmit and frees the scratch (1034-1052).
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:2113 - ImmediateSubmit submits to the graphics queue and waits on a fence (2086-2118): a full CPU-GPU sync per call.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:992 - Static BLAS flags are PREFER_FAST_TRACE only, with no ALLOW_COMPACTION. Each BLAS gets its own storage buffer (1002-1004).
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:213 - Mesh vertex and index uploads go through the same blocking ImmediateSubmit.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:922 - Allocation goes straight to VMA. A grep finds no vmaGetHeapBudgets or VK_EXT_memory_budget anywhere in RageV/src, so GPU memory use is never queried or reported.
- **Skeptic's note.** Mesh::GetAccelerationStructure builds the BLAS the first time it is asked (Mesh.cpp:209-231), and the TLAS walk is what asks (RayShadows.cpp:338). Each static BLAS gets its own scratch buffer and goes through ImmediateSubmit, which submits and then waits on a fence (VulkanDevice.cpp:2109-2115). The build flags are PREFER_FAST_TRACE only, with no ALLOW_COMPACTION (VulkanResources.cpp:990-992). Mesh uploads block the same way (213-219). No vmaGetHeapBudgets or VK_EXT_memory_budget appears anywhere in RageV/src; the editor shows only the total VRAM (EditorLayer.cpp:2923). The stall length, BLAS sizes and compaction ratio are all unmeasured. Two corrections to the fix direction. VMA already places each BLAS's buffer inside large shared memory blocks, so pooling buffers saves handles, not memory; the saving worth measuring is compaction. Terrain makes the stall much larger: each chunk's finest level is its own Mesh and BLAS, so a 4097-sample terrain would run about 4,096 blocking builds on its first ray-traced frame (inferred).

#### GEO-06 · Every material section of a model becomes its own entity, mesh, BLAS, TLAS instance and draw slot

- **Verdict:** confirmed. **Severity:** medium. **Kind:** architecture. **Scope:** refactor.
- **Roadmap:** RT2-35 (One entity per placed model included)
- **What is wrong.** The importer turns each primitive (each material section) of a model into a separate Mesh with its own buffers. Placing the model creates one entity per node plus one per extra primitive. Each Mesh gets a single-geometry BLAS, and each entity gets a TLAS instance, a hit-shading row, a cull record and a draw slot.
- **What it causes.** Every per-object cost in GEO-01, 02 and 04 is multiplied by the part count. A 10-part prop placed 100k times is more than a million entities, past the ECS limit. A 150-part car puts 150 overlapping boxes at the top of the ray tracer's tree for every ray that crosses it (traversal cost, not measured). Small BLAS are also less efficient than one per model.
- **Measured?** Counts recorded: about 150 primitives in the car (AssetManager.cpp:814-816); 155 meshes across 185 objects in the showroom. The trace-time and memory effect of the split has not been measured.
- **Already recorded?** No.
- **Fix direction.** - A mesh asset with several geometries, one per material section.
- One multi-geometry BLAS per mesh, per LOD.
- One TLAS instance per placed model; a hit finds its section's material through the geometry index (gl_GeometryIndexEXT) plus the instance's material-table offset.
- One entity per placed model, with sub-entities only when an author needs to move a part.
Measure the car's reflection trace cost with 150 instances against 1.
- **Evidence:**
  - RageV/src/RageV/Asset/AssetManager.cpp:817 - Every primitive of a model becomes a separate Mesh (814-824; 'A car is a hundred and fifty of them').
  - RageV/src/RageV/Asset/AssetManager.cpp:2063 - Import creates one entity per node plus one per extra primitive (2044-2091). Placing a model duplicates its whole node tree.
  - RageV/src/RageV/Renderer/Mesh.cpp:219 - One single-geometry BLAS per Mesh (218-227).
  - RageV/src/RageV/Renderer/RayShadows.cpp:270 - One TLAS instance, with its own custom index, per mesh entity; one hit-shading row and one cull record each as well.
  - RageV/src/RageV/Scene/Scene.cpp:2021 - The showroom has 155 distinct meshes across 185 objects.
  - RageV/src/RageV/Scene/ECS.h:58 - 20-bit entity index: about one million live entities at most (47-60).
- **Skeptic's note.** The code matches. AssetManager.cpp:814-824 builds one Mesh per primitive. Placing a model creates an entity per node plus one per extra primitive (2044-2091). Mesh.cpp:218-227 builds a single-geometry BLAS per Mesh. The TLAS walk adds one instance per mesh entity (Scene.cpp:2863-2915). ECS.h:59-61 caps the ECS at about a million entities. Placements of one model share its meshes and BLAS, so the multiplier falls on entities, TLAS instances, hit rows and cull records. I lowered the severity to medium for three reasons: the per-object costs it multiplies are already counted in GEO-01, 02 and 04; the traversal and small-BLAS costs are unmeasured; and the entity cap needs about 100k ten-part props before it is hit.

#### GEO-07 · No level of detail for meshes, none for rays, no far-field proxies; terrain rays always trace full detail

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** missing-capability. **Scope:** rewrite.
- **Roadmap:** RT2-38
- **What is wrong.** RageV has no mesh LOD chain and no simplifier (NEXT.md row 5, unbuilt). It also has no LOD for rays and no simplified stand-in geometry for distant rays (the proxy BLAS in RENDERING-REVAMP.md:2150-2175 is unbuilt). Every instance is therefore drawn and ray-traced at full detail at any distance. This caps asset density and memory, which is what blocks AAA-scale content; it does not cost frame time on today's scenes. Terrain is the one case with raster LOD. Its rays always trace the finest level (a deliberate rule, because the acceleration structure is world state), which forces a tight raster LOD limit measured at 0.15 ms a pass. Every chunk is built at all four levels up front. At the 4097-sample maximum that is 4,096 chunk BLAS and TLAS instances over 33.5M triangles, about 1.3 GB of GPU vertex and index data and about 0.8-0.9 GB of CPU copies (estimated from the vertex and index sizes).
- **What it causes.** Triangle count, BLAS memory and traversal cost are at full detail everywhere, which caps asset density; NEXT row 5 calls this what 'blocks the largest realism win there is'. At the maximum terrain size (33.5M triangles at level 0), the chunks need about 1.3 GB of GPU vertex and index data across four levels, about 0.9 GB of CPU copies, and a BLAS over all 33.5M triangles.
- **Measured?** RT-SERIES.md:1684: the terrain LOD veto kept for the ray mismatch costs 0.15 ms a pass. RT-SERIES.md:1681: Headland's view is 184 chunks and 751K triangles. The terrain memory figures are inferred arithmetic: 64-quad chunks, 4 levels, 32-byte vertices, 32-bit indices.
- **Already recorded?** Partly. Mesh LOD is NEXT.md row 5 and the proxy BLAS is in RENDERING-REVAMP, both unbuilt; the terrain ray level is 'noted, no item'. New here: ray LOD as an engine rule for every mesh, and terrain raster LOD's dependence on it.
- **Fix direction.** - Cook LOD chains at import (MeshCook).
- Raster: per-instance screen-size LOD with hysteresis, chosen on the GPU.
- Rays: a separate, world-stable LOD chosen once per frame and shared by every view (the 7bp rule: the structure is world state). Each TLAS entry points at its chosen LOD's BLAS.
- Far-field proxies in their own mask bit for distant shadow rays.
- Terrain rays through each chunk's chosen level or a coarse ray mesh.
Judge the raster/ray mismatch by pixel diffs against full-detail rays on the bridge and garage. Leave cluster-level ray tracing until those numbers say it is needed.
- **Evidence:**
  - docs/NEXT.md:316 - 'No LOD chain exists and no simplifier is vendored' (315-331). A grep of RageV/src finds LOD only in terrain.
  - RageV/src/RageV/Renderer/Terrain.h:66 - kRayLevel = 0 (line 50). kLevelErrorRatio is kept tight 'because of the ray tracer, not because of the silhouette' (58-66).
  - RageV/src/RageV/Renderer/Terrain.h:128 - ForRays always returns level 0, pinned so the acceleration structure is world state (116-128).
  - RageV/src/RageV/Renderer/Terrain.cpp:287 - All four levels of every chunk are built up front as separate meshes (276-291).
  - RageV/src/RageV/Asset/TerrainData.h:29 - Up to 4097x4097 samples: 33.5M triangles at level 0.
  - docs/RENDERING-REVAMP.md:2154 - A proxy BLAS for far shadow rays was proposed (2150-2175) and not built.
  - docs/RT-SERIES.md:1694 - Letting the TLAS carry each chunk's selected level: 'noted, no item'.
- **Skeptic's note.** The missing mesh LOD, simplifier, ray LOD and far-field proxies are real, and the terrain code is as quoted (Terrain.h:50, 58-66 and 116-128; Terrain.cpp:287-288). But much of this is already on record, and the terrain half is smaller than stated. Mesh LOD is row 5 of NEXT.md (305-321), and the owner has promoted it as the limit on asset density. RT-2.1 measured the terrain LOD limit that the rays force at 0.15 ms a pass, and filed 'the TLAS carries each chunk's selected level' as noted, no item (RT-SERIES.md:1684 and 1694). Building all four levels up front, and the memory that takes, is a stated and accepted design (ENGINE-NOTES.md 7ap, lines 6096-6106). A 4097-sample terrain is 4,096 per-chunk BLAS and TLAS instances totalling 33.5M triangles, not one BLAS. The memory arithmetic holds as an estimate. On today's bridge frame, raster LOD saves almost no time (RENDERING-REVAMP.md:2116 and 2141-2145); what it buys is asset density and cheaper far rays.

#### GEO-08 · No geometry streaming and no GPU memory budget: everything is loaded up front and stays resident

- **Verdict:** confirmed. **Severity:** high. **Kind:** missing-capability. **Scope:** rewrite.
- **Roadmap:** RT2-40
- **What is wrong.** All scene assets are uploaded at startup. A mesh not yet loaded is parsed and uploaded inside whatever frame first touches it. Meshes are never evicted during play. The device has one graphics queue, uploads block it, and nothing queries the GPU memory budget.
- **What it causes.** World size is capped by the 12 GB of the laptop GPU, and load time grows with the whole world. Anything loaded mid-game stalls a frame, and BLAS builds and uploads compete with rendering on one queue.
- **Measured?** Not measured.
- **Already recorded?** No. Roadmap 7.11 moved startup decoding to a worker thread; runtime streaming is not planned anywhere.
- **Fix direction.** - World cells with asynchronous file IO.
- A residency manager that loads and evicts by distance and screen size, under a VRAM budget read from VK_EXT_memory_budget.
- Uploads on a transfer queue, synchronised with timeline semaphores.
- BLAS build and compaction on async compute when geometry arrives (GEO-05); geometry and BLAS evicted together.
- **Evidence:**
  - RageV/src/RageV/Asset/AssetManager.cpp:791 - A cold mesh handle is parsed and uploaded synchronously by whichever call asks (759-828). GetMesh is called per entity by the draw-list refresh and the TLAS walk.
  - RageV/src/RageV/Asset/AssetManager.cpp:431 - Startup uploads run on the main thread under a time budget (420-487).
  - RageV/src/RageV/Asset/AssetManager.cpp:492 - Meshes leave the cache only on ClearCache or on a source-file Invalidate (490-595). There is no eviction by distance or budget.
  - RageV/src/Platform/Vulkan/VulkanDevice.h:216 - One graphics queue plus present; no transfer or async compute queue.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:213 - Uploads to device memory are blocking one-off submits.
- **Skeptic's note.** GetMesh parses and uploads synchronously when the mesh is not cached (AssetManager.cpp:759-828), and RefreshDrawList and the TLAS walk call it for every entity. Startup uploads run on the main thread under a time budget (418-487). Meshes leave the cache only on ClearCache or Invalidate (490-599). The device has one graphics queue plus a present queue (VulkanDevice.h:215-217). Uploads to device memory are blocking one-off submits (VulkanResources.cpp:195-221). No document plans runtime streaming (a grep of ROADMAP, NEXT, RT-FIRST and ARCHITECTURE finds none). Unmeasured, as the finding says.

#### GEO-09 · GPU-driven drawing is still one indirect draw per distinct mesh per pass, with frustum-only culling

- **Verdict:** confirmed. **Severity:** medium. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-37
- **What is wrong.** The GPU decides visibility, but the draw side still loops over every mesh slot on the CPU. For each slot it binds that mesh's buffers and issues one indirect draw (a draw whose instance count the GPU writes), even if nothing survived, and it does this in every geometry pass. The cull tests the view frustum only: nothing checks whether an object is hidden behind another or too small to matter. Slot and id lookups cost objects times distinct meshes.
- **What it causes.** Command and CPU cost scale with distinct meshes times passes, not with visible objects: tens of thousands of commands a frame once there are thousands of unique meshes. In dense scenes (cities, interiors) hidden objects are still rasterised in the prepass and G-buffer. By the code's own estimate the scans become milliseconds at 60k objects.
- **Measured?** Bridge: 200 draws and about 1 ms of CPU (RENDERING-REVAMP.md:2116). Showroom slot scan: 20-40 µs per refresh (Scene.cpp:2026-2033). Nothing measured at scale.
- **Already recorded?** Partly: 8.3's record (ENGINE-NOTES 7bx), the scan deferral in the code comment, and the Hi-Z judgement.
- **Fix direction.** - Put static geometry in one pooled geometry arena so one pipeline state can draw every mesh.
- The cull writes compacted draw commands, drawn with vkCmdDrawIndexedIndirectCount (one call per pipeline).
- Cull instances first, then clusters.
- Try two-phase Hi-Z occlusion (testing boxes against a small depth pyramid of what was already drawn) on an occluded fixture such as the garage or a city block, and keep it only if the palindrome says so.
- Replace the linear scans with mesh ids assigned by the GPU scene.
- **Evidence:**
  - RageV/src/RageV/Renderer/Renderer3D.cpp:4864 - Per mesh slot the lit pass binds that mesh's own vertex and index buffers and issues one DrawIndexedIndirect, whether or not anything survived (4840-4879). Shadows do the same at 9261-9302.
  - RageVEditor/assets/shaders/cull_lit.rvshader:97 - The only test is the frustum (97-122; main 124-140). No occlusion test and no small-object test.
  - RageV/src/RageV/Renderer/GpuCull.cpp:542 - Each depth view allocates an instance buffer sized to the whole scene (64 B per object), per frame in flight. This is the raster-shadow fallback only.
  - RageV/src/RageV/Scene/Scene.cpp:2026 - Mesh-slot lookups are linear scans over distinct meshes (2016-2098), deliberately left 'until a scene is CPU bound'. Renderer3D.cpp:5303-5350 does the same for the CPU path's ids.
  - docs/RENDERING-REVAMP.md:2238 - Hi-Z occlusion was judged not worth it for the bridge ('an open structure'); a judgement, not a measurement.
- **Skeptic's note.** The lit pass, the G-buffer pass and the depth prepass each loop over every mesh slot, bind that mesh's buffers and issue one DrawIndexedIndirect (Renderer3D.cpp:4840-4879 and 6099-6157). Shadows do the same (9261-9302). cull_lit tests the frustum only (97-140). Leaving the slot lookup as a linear scan is a deliberate, commented deferral (Scene.cpp:2016-2033), and so is the CPU path's id scan (Renderer3D.cpp:5303-5350). Hi-Z occlusion culling was dismissed for the open bridge by judgement, not measurement (RENDERING-REVAMP.md:2238). Nothing is measured at scale.

#### GEO-10 · Ray hits cannot use normal maps: the vertex has no tangent and a hit has no screen derivatives

- **Verdict:** confirmed. **Severity:** medium. **Kind:** image-quality. **Scope:** refactor.
- **Roadmap:** RT2-21
- **What is wrong.** The raster pass rebuilds each pixel's tangent frame from screen-space derivatives. A ray hit has no such derivatives and the vertex carries no tangent, so hit shading never applies the normal map. Skinned hits go further and use flat triangle normals, because only posed positions are written.
- **What it causes.** Reflections, refraction, the traced bounce and the bake all see normal-mapped surfaces as smooth, and characters look faceted in reflections. The RT-first path shows less surface detail than the raster picture it replaces.
- **Measured?** Not measured: no pixel comparison of a normal-mapped surface seen in reflection exists.
- **Already recorded?** Yes, as a stated limit (HANDOFF.md:10198-10200); never scheduled.
- **Fix direction.** - Cook a compact tangent frame into the vertex (for example a 32-bit quaternion frame), or derive the triangle's tangent at the hit from its three positions and UVs.
- Sample the normal map at a mip chosen from the ray's footprint.
- Write skinned normals from the skinning pass (GEO-13).
Judge by pixel diff of the garage floor's reflection of normal-mapped walls against a 16-ray reference.
- **Evidence:**
  - RageV/src/RageV/Renderer/Mesh.h:17 - MeshVertex is position, normal and texture coordinate only; there is no tangent.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:3576 - On screen the tangent frame is built from screen-space derivatives (3570-3580).
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2762 - TraceSurface reads the base colour, roughness, emissive, metallic and specular maps (2757-2792) and never the normal map.
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2725 - A posed (skinned) hit uses the flat triangle normal (2717-2726).
  - docs/HANDOFF.md:10199 - 'no normal map or parallax in a reflection', listed as a stated limit of 8.12 stage 3.
- **Skeptic's note.** MeshVertex has no tangent (Mesh.h:17-22). The raster pass builds its tangent frame from screen-space derivatives (pbr_fragment.glsl:3570-3580). TraceSurface samples the base colour, roughness, emissive, metallic and specular maps and never the normal map (2757-2792). Hits on posed (skinned) meshes use the triangle's own flat normal (2717-2726). This is already a stated limit (HANDOFF.md:10198-10200; ENGINE-NOTES.md:7853) and has never been scheduled. The garage has normal maps (the graffiti wall, RT-SERIES.md:1968), so its mirror floor reflects them flat. Not measured.

#### GEO-11 · Alpha-tested geometry is resolved by a shader test on every candidate hit, with full-resolution texture reads

- **Verdict:** confirmed. **Severity:** medium. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-38
- **What is wrong.** Every triangle a ray touches on a masked (cutout) instance stops traversal and runs a shader test, which ends in a mip-0 texture read. A candidate hit has no derivatives, so no lower mip is chosen. Light fittings are non-opaque for all rays, not only the rays aimed at them.
- **What it causes.** In an outdoor AAA scene, foliage, fences and grilles make up much of the geometry. Every ray would pay an uncached texture fetch per candidate triangle, the 'known cliff'. The bridge's thin steel already shows the shape of the cost.
- **Measured?** The candidate test itself has never been timed. The bridge note attributes far-shadow-ray cost to its thin members (RENDERING-REVAMP.md:2150-2151).
- **Already recorded?** Partly: NEXT.md:199-206 records the cliff and the AO trade.
- **Fix direction.** - Bake opacity micromaps (VK_EXT_opacity_micromap, available on this GPU) for masked meshes at cook time. The hardware then resolves each micro-triangle as opaque, transparent or unknown, and only unknown ones reach the shader.
- Pick the texture mip from the ray cone instead of mip 0.
- Give light fittings an opaque path for rays not aimed at them.
Measure on a foliage fixture with A,B,B,A before adopting.
- **Evidence:**
  - RageV/src/RageV/Renderer/RayShadows.cpp:288 - Masked instances and light fittings are marked FORCE_NO_OPAQUE for every ray (284-289).
  - RageVEditor/assets/shaders/include/ray_shadow_trace.glsl:181 - Per candidate triangle (100-186): instance record, material record, three indices and three UVs are read, then the base colour is sampled at mip 0.
  - RageVEditor/assets/shaders/rtao_compute.rvshader:371 - AO rays force everything opaque, so cutouts are solid for occlusion.
  - docs/NEXT.md:203 - A texture fetch inside traversal is described as 'the known cliff', kept affordable only by limiting it to masked instances (199-206).
  - docs/RENDERING-REVAMP.md:2150 - The bridge's far shadow rays are expensive 'because they traverse the bridge's thin steel'.
- **Skeptic's note.** Masked and light-fitting instances are FORCE_NO_OPAQUE for every ray (RayShadows.cpp:285-289). For each candidate hit, the shader reads the instance and material records, three indices and three UVs, then samples the base colour at mip 0 (ray_shadow_trace.glsl:100-186). AO rays force everything opaque (rtao_compute.rvshader:360-372). NEXT.md:199-206 records the texture fetch inside traversal as a known cost cliff. The bridge's thin members are masked (ray_shadow_trace.glsl:70-73), so the test runs there, but the note on far-shadow cost (RENDERING-REVAMP.md:2150) does not separate it from ordinary traversal; the finding correctly says the test has never been timed. For a light fitting that is not masked, the test is one record read and an early return, not a texture fetch.

#### GEO-12 · The GPU cull's object table is rewritten by the CPU while the previous frame may still be reading it

- **Verdict:** confirmed. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-4
- **What is wrong.** When the CPU starts frame N+1 it has waited only for frame N-1, so frame N may still be running on the GPU. Every other per-frame buffer on this path is kept once per frame in flight. The object table and draw template are one buffer each, overwritten with a plain memory copy on every refresh. If frame N's cull has not yet run when the copy lands, it culls with frame N+1's data.
- **What it causes.** Latent today, because a still scene writes identical bytes. With moving objects it can cause a one-frame wrong cull at the screen edge. In the raster-shadow fallback, shadow casters can be drawn with the next frame's matrices, because the depth cull copies matrices from the table. When the object set changes (streaming, spawning), the slot layout the CPU draws with and the table the GPU reads can disagree for a frame.
- **Measured?** Not measured; inferred from buffer ownership and frame pacing.
- **Already recorded?** No.
- **Fix direction.** Patch: ring the object and template tables per frame in flight, exactly as the view slots are. In the target design the table becomes the device-local GPU scene (GEO-01), updated by copies ordered with barriers.
- **Evidence:**
  - RageV/src/RageV/Renderer/GpuCull.cpp:98 - One object table and one draw template per pass (84-98), not one per frame in flight.
  - RageV/src/RageV/Renderer/GpuCull.cpp:100 - The per-view buffers are ringed per frame 'because the previous frame's views may still be reading theirs' (100-111). The table is not.
  - RageV/src/RageV/Renderer/GpuCull.cpp:390 - SetObjects memcpys into that mapped buffer on every refresh, which happens twice a frame.
  - RageV/src/RageV/Renderer/GpuCull.cpp:563 - The cull dispatch reads the shared table when it executes (also 470 for the camera).
  - RageV/src/Platform/Vulkan/VulkanDevice.cpp:1550 - BeginFrame waits only on the fence of the slot being reused. With two frames in flight (VulkanDevice.h:336), frame N may still be running while N+1 records.
- **Skeptic's note.** The object table and draw template are one host-visible buffer per pass (GpuCull.cpp:84-98), written with a plain memcpy (390-391; for mapped memory, VulkanBuffer::Upload is a memcpy, VulkanResources.cpp:189-193). By contrast, the per-view buffers are kept once per frame in flight (100-111), and so are Renderer3D's scene slots (Renderer3D.cpp:1375-1395). BeginFrame waits only on the fence of the slot it is reusing (VulkanDevice.cpp:1550), with two frames in flight (VulkanDevice.h:336). The finding misses a second case: the same buffer is also written twice inside one frame, first by RenderShadowMaps' refresh and then by OnRender's. The shadow and lit culls were recorded with the first refresh's counts (GpuCull.cpp:476-497) but read whatever the last write left. So the 'correctness rule' at Scene.cpp:1970-1985 does not actually keep the recorded counts and the uploaded table in agreement if the object set changes between the two refreshes. Latent: in steady state every write carries identical bytes. Not measured.

#### GEO-13 · Skinned characters are posed, refit and fenced one at a time for rays, and skinned twice

- **Verdict:** confirmed. **Severity:** medium. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-35
- **What is wrong.** For rays, each skinned mesh is posed, barriered and refit on its own, and the refits are serialised by barriers. A skinned BLAS is never rebuilt, however far the pose drifts from the shape it was built for. Skinning is implemented twice: in the vertex shader of every raster pass and in the compute pass for rays. The compute pass writes no normals.
- **What it causes.** Cost grows as a serial chain with the number of characters, so crowds are expensive. Traversal slows as poses drift (not measured). Two skinning implementations must be kept identical by hand. Posed hits have flat normals (GEO-10).
- **Measured?** Correctness only: the fox's traced shadow matches its posed mapped shadow at IoU 0.946 (HANDOFF.md:10236). Cost not measured.
- **Already recorded?** Partly: HANDOFF.md:10256-10258 notes the duplicated skinning arithmetic.
- **Fix direction.** - One skinning pass per frame writes posed position, normal and previous position (for motion vectors).
- Every raster pass reads that buffer as a plain vertex stream, and the BLAS refit reads it too.
- All refits in one build call with one barrier.
- Rebuild a skinned BLAS when a deformation measure (such as growth of its bounding box) crosses a threshold, set as a global render setting.
Measure on a 100-character fixture.
- **Evidence:**
  - RageV/src/RageV/Renderer/RayShadows.cpp:414 - Each posed caster gets its own descriptor rewrite, compute dispatch, buffer barrier and BLAS build call (393-426).
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1099 - Every refit is followed by a build-to-build barrier (1083-1099), so refits cannot overlap.
  - RageV/src/Platform/Vulkan/VulkanResources.cpp:1071 - After the first build, only refits, ever (1064-1074). The TLAS has a 64-frame rebuild limit; skinned BLAS have none.
  - RageVEditor/assets/shaders/skin_positions.rvshader:10 - Positions only, and the vertex shader's arithmetic 'copied rather than shared' (10-16). Every raster pass skins again in its vertex shader.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:9720 - Bones are copied separately for the raster draw (current and previous, 9715-9739) and again for the ray pass (RayShadows.cpp:325).
  - RageV/src/RageV/Renderer/RayShadows.cpp:23 - A posed buffer and a BLAS per caster per frame in flight (20-27, 130-175).
- **Skeptic's note.** Each posed caster gets its own resource-set rewrite, compute dispatch, barrier and BLAS build call (RayShadows.cpp:393-424). Each dynamic BLAS build ends with a barrier to the next build, so the refits run one after another (VulkanResources.cpp:1083-1099). After the first build there are only refits (1071-1073). The skinning arithmetic is duplicated and the compute copy writes positions only (skin_positions.rvshader:10-16). Bones are copied for the raster draw (Renderer3D.cpp:9715-9739) and again for rays (RayShadows.cpp:325). One more point the finding misses: every skinned caster in the scene is posed and refit every frame, whatever its distance or visibility (Scene.cpp:2870-2876). Cost is unmeasured; only correctness is recorded (HANDOFF.md:10236).

#### GEO-14 · Mesh data is uncompressed and duplicated: 32-byte float vertices, 32-bit indices always, and a CPU copy of every mesh

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-37, RT2-43
- **What is wrong.** Vertices are stored as full floats (32 bytes for static meshes, 64 for skinned). Every index is 32-bit, even for meshes under 65,536 vertices. Every mesh also keeps a CPU copy of its positions and indices that the shipped runtime never reads: picking is editor-only, and the meshlet builder is off by default. At scale this roughly doubles vertex memory compared with a compressed layout, doubles index memory for small meshes, and keeps a copy of all geometry in system memory (estimated, not measured). It does not measurably slow hit shading, because a hit reads one 32-byte sector per vertex either way.
- **What it causes.** Roughly twice the GPU memory and fetch bandwidth of a compact layout for vertex data, and double for most index data. The shipped runtime holds CPU copies of all its geometry for an editor-only feature. Hit shading is slowed by wide scattered fetches.
- **Measured?** Not measured; sizes inferred from the structs.
- **Already recorded?** No. Mesh.h:126-139 justifies the CPU copy for picking.
- **Fix direction.** - Cook separate streams: a position stream that depth passes, BLAS builds and hits all read (AccelerationGeometryDesc already takes an offset and stride), and a compact attribute stream (quantized normal and tangent, half-float UVs).
- Use 16-bit indices where the vertex count allows.
- Drop the CPU copies from the runtime; pick in the editor with a ray query against the TLAS or with an id buffer.
Measure memory and hit-shading cost before and after.
- **Evidence:**
  - RageV/src/RageV/Renderer/Mesh.h:35 - Static vertex is 32 bytes and skinned vertex 64 bytes, all float (17-45).
  - RageV/src/RageV/Renderer/Mesh.cpp:60 - Every index buffer is 32-bit (59-65, 111-117).
  - RageV/src/RageV/Renderer/Mesh.cpp:69 - Positions and indices are kept on the CPU for every mesh (67-83).
  - RageV/src/RageV/Scene/ScenePicking.cpp:216 - The CPU copies' only runtime reader besides the meshlet builder: editor picking, a brute-force test of every triangle of every mesh (185-217).
  - RageV/src/RageV/Renderer/Mesh.cpp:177 - The meshlet path stores a second copy of the positions (175-193). Terrain keeps four independent levels per chunk (Terrain.cpp:287-288).
  - RageVEditor/assets/shaders/include/pbr_fragment.glsl:2693 - Ray hits read these 32-byte float vertices by address, scattered reads the shader calls 'effectively uncached' (2688-2705).
- **Skeptic's note.** The layout facts are right. Vertices are 32 or 64 bytes of floats (Mesh.h:17-45). Indices are always 32-bit (Mesh.cpp:59-65 and 111-117), although the RHI supports 16-bit (VulkanResources.cpp:973). Every mesh keeps CPU copies of its positions and indices (Mesh.cpp:67-83), and their only readers at run time are editor picking and the meshlet builder, which is off by default (ScenePicking.cpp:190-231; Mesh.cpp:153-193). The hit-shading claim does not hold. The 'effectively uncached' comment at pbr_fragment.glsl:2688-2698 describes position loads that have already been removed for static hits. What remains is the normal and UV of three vertices, each inside one 32-byte memory sector, so a narrower vertex would touch the same number of sectors. Nothing is measured, and memory is not today's constraint.

#### GEO-15 · Which lamps have a moving object in range is found by testing every lamp against every unmarked object

- **Verdict:** confirmed. **Severity:** medium. **Kind:** scalability. **Scope:** patch.
- **Roadmap:** RT2-12
- **What is wrong.** For each view and each light, the scene walks a 'moving' list until it finds a box inside the light's range. The list holds every mesh without the Static tick, which is every mesh in an unmarked scene. A lamp with nothing moving nearby therefore tests the whole list.
- **What it causes.** Cost is lights times unmarked objects, per view per frame. 200 lamps against 100k unmarked props is up to 20 million box tests per view (inferred).
- **Measured?** Not measured.
- **Already recorded?** No.
- **Fix direction.** Answer it from a spatial index built once per frame: the world-space lamp grid planned for WR-10/S4. Each moving box marks the lamps of the cells it overlaps, so the cost is lights plus moving objects. Keep classifying by the Static tick: a non-static object is not in the bake, so it must still count.
- **Evidence:**
  - RageV/src/RageV/Scene/Scene.cpp:3083 - For each light, every moving box is tested until one is in range (3080-3093).
  - RageV/src/RageV/Scene/Scene.cpp:2127 - Every mesh without the Static tick is added to the moving list.
  - RageV/src/RageV/Scene/Components.h:365 - Static is off by default.
  - RageV/src/RageV/Scene/Scene.cpp:4969 - Runs in OnRender, so per view and per probe face.
- **Skeptic's note.** MarkMovingLights tests every light against m_MovingBounds until it finds one in range (Scene.cpp:3080-3093). Every non-static mesh goes into that list (2125-2127), and Static is off by default (Components.h:365). It runs in OnRender (4969), so once per view and once per probe-capture face. The scan stops at the first moving object in range, so the worst case is a lamp with no moving object nearby. Only fully baked and hybrid lights read the result (LightGrid.cpp:235 and 389), so skipping Realtime lights, a one-line change, would remove most of the cost even before a spatial index. Not measured.

#### GEO-16 · Instance masks carry only static/moving, and glass is left out of the ray world entirely

- **Verdict:** confirmed. **Severity:** low. **Kind:** architecture. **Scope:** patch.
- **Roadmap:** RT2-35
- **What is wrong.** An instance mask is 8 bits per placed object that a ray can use to skip it. Two of the eight are used, and the frame's rays pass all of them. Glass is not added to the TLAS at all. No object can be kept out of a given ray type.
- **What it causes.** Every ray type traverses every instance. There is no cheap way to exclude small detail from GI and AO rays, and no proxy set for far shadows (GEO-07). Transparent surfaces are missing from reflections.
- **Measured?** Not measured.
- **Already recorded?** Partly: the glass trade is explained at Scene.cpp:2882-2898, and a proxy mask bit is proposed in RENDERING-REVAMP.md:2150-2175.
- **Fix direction.** A global mask policy by ray type, set in RenderSettings:
- opaque;
- translucent: seen by reflection rays, with the candidate test deciding how much passes, and not by shadow or GI rays;
- small detail: classified automatically from world size, skipped by GI and AO rays;
- far-field proxies (GEO-07).
Adopt each rule only after a pixel diff and a palindrome timing.
- **Evidence:**
  - RageV/src/RageV/Renderer/RayShadows.h:92 - Two of the eight mask bits are defined, static and moving (83-93); each instance carries one (RayShadows.cpp:294).
  - RageVEditor/assets/shaders/include/ray_shadow_trace.glsl:229 - Frame rays use all bits (0xFF); only the bake and the subtractive shadow narrow the mask (224-230).
  - RageV/src/RageV/Scene/Scene.cpp:2899 - Blended materials never enter the TLAS; 'a car reflected in the floor loses its windows' (2882-2900; Material.h:80-83).
  - RageV/src/RageV/Scene/Components.h:321 - MeshComponent has no cast-shadow or ray-visibility property.
- **Skeptic's note.** Two mask bits are defined, and each instance carries one (RayShadows.h:83-93; RayShadows.cpp:294). The frame's rays pass all bits, 0xFF (ray_shadow_trace.glsl:224-230). Blended materials never enter the TLAS (Scene.cpp:2899; Material.h:80-83), a documented trade. MeshComponent has no per-object ray or shadow visibility setting (Components.h:321-410). Not measured.

#### GEO-17 · The reflection history's object identity folds every entity into 1021 values

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** scalability. **Scope:** patch.
- **Roadmap:** RT2-5
- **What is wrong.** RT-17's 'is it still the same object' test reads an identity built from the entity's index alone, folded into 1..1021. The ECS reuses the most recently freed index first, so an object destroyed and replaced in the same place keeps the same identity at any scene size, and the test cannot refuse a ghost across that swap. Past 1021 entities, the fold also makes about one pair in 1021 share a value. Low impact: the test only runs where something moved, and RT-17 was measured to change little.
- **What it causes.** At 100k instances about a hundred entities share each value. The history cannot see a change between two of them, such as one object replaced by another in a reflection, so a ghost can survive where it should be refused.
- **Measured?** RT-17 was measured on the garage and bridge cameras (RT-SERIES.md:2124-2135); never at scale.
- **Already recorded?** The fold is recorded as the design (RT-SERIES.md:2111-2112); its scale limit is not.
- **Fix direction.** Carry the GPU-scene instance index (GEO-01) in an integer lane, 32-bit or a 16-bit hash if bandwidth demands. Measure the wider lane's cost.
- **Evidence:**
  - RageV/src/RageV/Renderer/Renderer3D.cpp:5657 - Identity = (entity handle mod 1021) + 1, so it fits a half-float lane (5650-5658).
  - docs/RT-SERIES.md:2111 - The fold is the RT-17 design.
- **Skeptic's note.** The fold is as stated (Renderer3D.cpp:5650-5658; RT-SERIES.md:2111-2112). But the scale argument is weak, and the finding misses the bigger leak. The identity uses only the entity's index: the handle is cut to its low 20 bits, which drops the version number. The ECS hands out the most recently freed index first (ECS.h:527-532 and 556-571). So an object destroyed and replaced in the same place gets the same identity at any scene size. The fold adds a smaller leak past 1021 entities: a given pair of different objects shares a value with a chance of about 1 in 1021, and that chance does not grow with scene size. The test only acts where something moved (RT-SERIES.md:2123-2128), and RT-17 was measured to change little (2100-2106).

#### GEO-18 · A dormant meshlet path and stale guidance around the GPU-driven path

- **Verdict:** confirmed. **Severity:** low. **Kind:** tech-debt. **Scope:** patch.
- **Roadmap:** RT2-3, RT2-37
- **What is wrong.** The meshlet path (triangle clusters of up to 124 drawn by mesh shaders) is off by default and measured performance-neutral, and its next stage was never built. Turning it on disables the instance-level GPU cull. Three comments now contradict the code: the --gpu-lit status, the HasObjects policy, and where masked geometry goes.
- **What it causes.** Maintenance cost and wrong guidance; the audit brief itself carried 'gpu-lit is off'.
- **Measured?** Meshlets: showroom Scene pass 4.08 against 4.11 ms (HANDOFF.md:7209).
- **Already recorded?** The meshlet next stage is noted (HANDOFF.md:7218-7219); the stale comments are not.
- **Fix direction.** Fold meshlets into the cooked cluster pipeline of GEO-07 and GEO-09, or delete the path. Correct the three comments, and delete HasObjects.
- **Evidence:**
  - RageV/src/RageV/Core/EngineConfig.h:983 - --meshlets is off by default.
  - docs/HANDOFF.md:7209 - Meshlets measured performance-neutral; the task stage fed from the cull table was never built (7195-7219).
  - RageV/src/RageV/Scene/Scene.cpp:1927 - Meshlets and the GPU cull exclude each other (also 2571).
  - RageV/src/RageV/Renderer/Mesh.cpp:156 - Meshlets are built at run time on first draw with four blocking uploads. No normal cone, hierarchy or LOD (143-207).
  - RageV/src/RageV/Core/EngineConfig.h:984 - Says --gpu-lit is 'Unfinished' and flickers. HANDOFF.md:6549-6560 says it was fixed on 2026-08-24, and it is on by default.
  - RageV/src/RageV/Renderer/GpuCull.h:205 - HasObjects documents a 'first build of a frame counts' policy and has no caller; the table is rebuilt on every refresh.
  - RageV/src/RageV/Scene/Scene.cpp:2142 - The comment says masked geometry takes the CPU path; the code at 2323-2334 puts it in the GPU table.
- **Skeptic's note.** All verified. Meshlets are off by default (EngineConfig.h:983), were measured performance-neutral (HANDOFF.md:7195-7219), cannot run together with the GPU cull (Scene.cpp:1927 and 2571), and are built at first draw with four blocking uploads (Mesh.cpp:143-207). EngineConfig.h:51-53 and 984-990 still call --gpu-lit unfinished and flickering, although it is on by default and was fixed on 2026-08-24 (HANDOFF.md:6549-6560). GpuCull::HasObjects has no caller and documents a policy that was retired (GpuCull.h:196-205; GpuCull.cpp:303-310). Scene.cpp:2142-2156 says masked geometry takes the CPU path, while 2327-2334 puts it in the GPU table. One more stale comment of the same kind: Scene.cpp:2037-2040 still says the table is only built on the frame's first refresh.

#### geometry-s1 · The reflection history stores each surface's object id in a half float, so past 2048 objects most surfaces never keep a reflection history

- **Verdict:** added by the skeptic. **Severity:** high. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-5
- **What is wrong.** The G-buffer gives every drawn surface an object id: its instance row plus one. The id is stored exactly in a 32-bit lane. The reflection accumulator copies it into its history so the next frame can ask 'is this the same object?'. But that history attachment is a half float. A half float holds every whole number only up to 2048; above that it keeps only every 2nd number, then every 4th, and so on. The GPU also rounds these writes toward zero, the half-float fact already recorded in include/half_float.glsl. Next frame the exact id is compared with the rounded one, and since RT-15c any difference throws the history away.
- **What it causes.** In a scene with more than about 2048 instance rows, every surface whose id cannot be stored exactly loses its reflection history on every frame. That is half of ids 2049-4096, three quarters of ids up to 8192, and over 95% of surfaces at 60,000 objects. Their reflections fall back to one frame of rays plus the young-history blur, which is the noise the RT series has spent 24 items fighting. A big shiny floor is one object, so whether its reflection accumulates at all depends on its row number. Today's scenes are too small to show it (garage 185 objects, bridge about 200 draws). Glass reflections are affected the same way. The direct light's history uses a half-float id too, but there it only affects moving objects and candidates that are off the surface plane (reflection_accumulate.rvshader:1164-1168 and 1220-1224).
- **Measured?** Not measured: no ray-traced scene with more than a few hundred objects has been run (GEO-03). The failure itself follows directly from the formats: a whole number the half float cannot hold does not equal itself after being stored and read back.
- **Already recorded?** No. RT-6.5's record (RT-SERIES.md:2638-2672) tested the id check on the garage only and does not mention the lane's precision.
- **Fix direction.** Keep the id exact:
- a 32-bit lane for it in the reflection, glass and direct-light histories, as the TAA guide already does (RGBA32F, FrameGraphBuilder.cpp:1679);
- or an explicit fold, applied the same way when writing and when comparing.
Longer term, the id should be the stable GPU-scene index (GEO-01 and geometry-s2).
Prove it with a scale scene whose glossy floor sits at an odd row past 2048: the reflection refusals for reason 6 (object id) should shrink from the whole floor to its edges. The garage and the bridge should show a zero pixel diff, because all their ids are under 2048.
- **Evidence:**
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:1012 - The reflection history's fifth attachment, which holds 'the object id under each texel' (RT-6.5), is R16G16B16A16_SFLOAT, a half float (lines 1012-1017).
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:82 - The G-buffer's own id lane is R32G32_SFLOAT, which is exact, so the two sides of the comparison do not have the same precision.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:9111 - The id is the instance row plus one, so it runs up to the scene's object count.
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1390 - g_ObjectId is read from the G-buffer as a whole number, negated for a static surface.
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1920 - o_Ident.r = g_ObjectId is written unrounded into the half-float attachment.
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1126 - RT-15c rule: any history candidate whose stored id differs from the current one by more than 0.5 is refused outright. OwnHistoryPicture (1049-1057) applies the same test.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:5653 - RT-17 folds its own identity into 1..1021 'so it stays exact in the half float'. The limit was known for that lane and not applied to the G-buffer id stored beside it.
  - RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:2788 - The glass reflection history has the same half-float fifth attachment (2788-2793), and the direct light's history keeps its id in R16G16_SFLOAT (1798-1802).

#### geometry-s2 · The object id that TAA and the reflection history trust is this frame's row number, so it changes as the camera turns

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-5
- **What is wrong.** Rows in the GPU table (plain static meshes) keep their number while the scene does not change, so those ids are stable. Every other kind gets whatever row it takes this frame after culling: terrain chunks, skinned meshes, and every mesh in a view that has no GPU table. When an earlier chunk or character leaves or enters the view, every later one gets a new number. TAA's history test (RT-6) and the reflection accumulator (RT-15c) both treat a new number as a different object and discard the history.
- **What it causes.** Inferred, not measured. When TAA is the anti-aliasing mode and the camera moves over terrain, whole chunks drop to the raw frame for one frame whenever the visible set changes ahead of them; the bridge's Headland view draws 184 chunks. Glossy skinned characters lose their reflection history the same way. Spawning or destroying any table object resets every glass and CPU-path surface for a frame. With --gpu-lit=off, or in the editor's second viewport, it applies to everything. It does not explain the garage's RT-24 spin smear, because every garage mesh is in the GPU table.
- **Measured?** Not measured. RT-SERIES.md:3121-3139 recorded object-id refusals on 4.18% of pixels on the bridge Deck and 3.37% on the garage mid-dolly, attributed mainly to real silhouettes. Nothing has separated renumbering from real edges.
- **Already recorded?** No.
- **Fix direction.** Give every drawn thing an id that belongs to the object, not to the frame:
- the GPU-scene slot from GEO-01 for meshes, or the entity index until that exists;
- the terrain entity plus the chunk index for terrain chunks;
- written at every fill site, whichever path draws it, and kept exact as in geometry-s1.
Check it with --debug-view=taa-refusal and the reflection refusal counts on a Headland pan and on a pan past a skinned character, run as A,B,B,A.
- **Evidence:**
  - RageV/src/RageV/Renderer/Renderer3D.cpp:1415 - AllocateInstance: a CPU-path draw takes the next free row of this frame's instance table.
  - RageV/src/RageV/Renderer/Renderer3D.cpp:9639 - DrawMesh writes Extra.x = row + 1 as the object id. DrawWaterMesh (9690), DrawSkinnedMesh (9773) and DrawLayeredMesh, which draws terrain chunks (9826), do the same.
  - RageV/src/RageV/Scene/Scene.cpp:5191 - CPU-path meshes are frustum-culled on the CPU before they are submitted, and terrain chunks likewise (5303-5309). Each one's row therefore depends on how many earlier ones survived this frame's cull.
  - RageV/src/RageV/Scene/Scene.cpp:5116 - CPU-path rows start after the opaque plus blended table count, so any spawn or destroy of a table object shifts every CPU-path id and every blended row.
  - RageV/src/RageV/Scene/Scene.cpp:5102 - The GPU table is used only for the camera it was culled for. The editor's other viewport, --gpu-lit=off, or a table refused at the device limit sends every mesh down the CPU path.
  - RageVEditor/assets/shaders/taa_resolve.rvshader:407 - TAA refuses the history outright when the id differs from last frame's.
  - RageVEditor/assets/shaders/reflection_accumulate.rvshader:1126 - The specular reflection history is refused outright when the id does not match (RT-15c).
  - docs/RT-FIRST.md:81 - T3 built the id as 'the record's index + 1' when nothing read it yet. Staying the same from frame to frame was never a requirement.

### Engine core, threading, assets, ECS, scripting and physics

**State of the area.** Engine core today is a single-threaded, immediate-mode loop. The main thread does all of it, in order, every frame: window pump, input, 0 to 15 fixed simulation steps (C++ and C# scripts, then a Jolt physics step that waits on Jolt's own private 23-thread pool), the scene update (animation, physics sync, particles, audio), 4 to 7 full hierarchy walks, 2 to 4 full rebuilds of the draw list and GPU cull table, a rebuild of the ray-tracing instance list, frame-graph build and all command recording into one command buffer, then submit and present. Two frames in flight let the CPU record one frame while the GPU runs the previous one, but every GPU upload (buffer, texture creation, each texture mip, each static acceleration structure) is a blocking round trip that also waits for that previous frame. Assets load synchronously on first use. Boot is split into a worker CPU phase and a time-sliced main-thread upload phase, but on a warm cache the worker phase only reads files and throws the bytes away. There is no streaming, no eviction, no GPU memory budget, no runtime scene loading and no world partition. The ECS (a small sparse set that replaced EnTT on 2026-08-22) is sound, but nothing tracks changes, so the renderer re-derives the whole scene from it every frame. Measured: at 60,000 objects the frame is CPU-bound (about 12.8 ms of lit-pass walk and submission, 2.9 ms depth passes, 2.6 ms transform walks, against 2.7 ms of GPU; HANDOFF.md:7118). The shipped demo scenes are GPU-bound, so none of this shows in them yet. Correction to the brief: --gpu-lit is ON by default and its defect was fixed on 2026-08-24 (HANDOFF.md:6549). Only the in-code usage text (EngineConfig.h:51) still says 'unfinished, off'. Also: FrameProfiler::LiveRayGpuMs has no caller, so the 'ray budget driven by GPU time' comment in Application.cpp:802-806 describes nothing. RT-FIRST, RT-SERIES and NEXT do not cover engine-core threading, streaming, memory or the scene-to-renderer interface, so most findings below are new material for RT-series 2.

**What to keep.**
- FrameClock's stamped input edges (Core/FrameClock.h): no reader consumes an edge, so tick and frame readers each see a press exactly once. Keep these semantics through any threading change.
- FixedStep (Core/FixedStep.h): the accumulator with its 0.25 s clamp and a tested interpolation alpha.
- The boot split (Core/Boot.h, Application::RunBootPhase): CPU work on a worker and device work time-sliced on the main thread behind a live loading screen. The right shape to grow into the streaming system.
- The import cache format (Asset/ImportCache.cpp): source hash in the cooked file name, atomic .partial-then-rename writes, a version check on cooked bytes, and a sweep of stale entries.
- The ECS core (Scene/ECS.h): paged storage with stable references, versioned handles, cross-module type ids from a __FUNCSIG__ hash, and views that resolve their pools once and survive growth. Small enough to extend with change versions and parallel ranges.
- Vulkan deferred destruction per frame slot, and asynchronous per-frame readback slots (VulkanDevice.cpp:1015-1197): the GPU-to-CPU path already never blocks.
- GPU post-mortem tooling: NV checkpoints, VK_EXT_device_fault with a registry that names the faulting buffer, debug labels on GPU work, and stopping the run on device loss so the first error stays at the bottom of the log.
- The measurement harness: --benchmark with warm-up discard and percentiles, per-pass CPU and GPU timings, --slow-frames with a baseline, --frame-time pinning, --screenshot-count bursts, --capture-signals .npy output, and --camera poses.
- The Jolt integration choices: batched body adds plus broadphase optimisation, contacts recorded on Jolt's job threads and delivered after the step, sub-shape pair counting, and sleep-aware exit handling.
- The C#/native boundary: blittable function-pointer tables, an append-only ABI with a protocol version check, and UUIDs rather than pointers across the boundary.
- Safe iteration rules: deferred destroy by UUID, and collecting script handles before running a pass (Scene.cpp:141-168, 956-1011).
- The RT-15 motion-history snapshot point (Scene.cpp:1115-1129): previous transforms are captured before any tick moves anything.
- The VFS with an immutable pak reader that is safe to read from any thread (IO/PakFile.h).

**How it should look in an RT-first engine** (the inspector's view; the roadmap decides). An RT-first engine at AAA scale needs a CPU side whose per-frame work is proportional to what changed, spread across all cores, and that never waits on the GPU in the frame path. Every step below is adopted only when an A,B,B,A palindrome and a per-pixel diff prove it, as the owner requires.

(1) Threads. One engine-owned job system: work-stealing workers equal to hardware threads minus one, plus dedicated I/O threads. Every per-frame system is a task with declared inputs and outputs: input, the fixed-step simulation (scripts on the game thread; Jolt through JPH::JobSystem on the same workers), animation sampling, particle simulation, transform update, extraction, culling, writing TLAS instances, and command recording split by pass groups into parallel command buffers submitted in order. The frame becomes a two-stage pipeline: simulation of frame N+1 runs while the extracted snapshot of frame N is recorded and submitted. The in-flight fence wait moves to the top of the frame, before input.

(2) Scene data. The ECS stays a sparse-set store and gains per-component change versions and a parallel-for over dense ranges. Transforms become a flat, depth-sorted hierarchy (parent indices, not UUIDs), written only through an API that marks the change, with quaternion rotation and exactly one derive after simulation. A debug build keeps the full compare walk as an assertion that nothing wrote around the API. Every transform written in the fixed step keeps previous and current simulation poses and is interpolated at render, so motion vectors are right at any display rate.

(3) Render world. The renderer owns render proxies with stable GPU indices (transform, previous transform, bounds, material index, static/moving flags) in GPU buffers updated sparsely from change lists. The GPU cull, G-buffer draws, hit shading, the RT-17 reprojection data and the TLAS instance descriptors (written by compute; refit-versus-rebuild policy measured) all read those same buffers. The TLAS is a world-update step, independent of shadows. Per-view state (the temporal histories, exposure, ray budgets) lives in a View object an application creates with one call. Scene includes no renderer headers, and bake orchestration becomes a service.

(4) Assets and streaming. An asynchronous request queue with priorities and cancellation; file reads on I/O threads (overlapped now, DirectStorage later if measured to pay); decode on jobs. One upload service: a staging ring per frame in flight, the dedicated transfer queue with timeline semaphores, a per-frame upload budget as a global render setting, and batched, compacted acceleration-structure builds. Assets have load states with placeholders and reference-counted handles, with eviction under VRAM and RAM budgets read from VK_EXT_memory_budget. Mip and mesh-LOD streaming; cooked binary scene cells streamed around the camera; a runtime level and cell API. The registry detects changes by size plus modification time with lazy hashing, and hot reload uses OS change notifications on a background thread.

(5) Memory. A budget service with per-category accounting shown in the profiler; frame linear allocators for scratch data; no heap allocations in per-frame loops; no permanent CPU copies of GPU geometry.

(6) Diagnostics. Nestable, thread-aware CPU zones (optional Tracy backend) covering simulation, extraction and uploads; counters for blocking submits, bytes uploaded, cache misses, allocations and VRAM; keep the GPU pass timings, checkpoints and device-fault reports.

Scope: reaching this means rewriting the frame loop and threading model, the scene-to-renderer interface (the GPU scene and view objects) and the asset loading path, and refactoring the ECS, transform system, physics glue and scripting bridge. The measurement harness, ECS storage, fixed-step and input-edge logic, import-cache format, VFS and GPU post-mortem tooling carry over unchanged.

#### CORE-01 · The whole frame runs on one thread: no job system, and rendering waits behind simulation

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** high. **Kind:** architecture. **Scope:** rewrite.
- **Roadmap:** RT2-36 (OpenGL frozen (D1))
- **What is wrong.** All per-frame CPU work runs on the main thread: input, scripts, the physics step, animation, transform walks, building the draw list and the ray-instance list, building the frame graph, and recording every command into one command buffer. Simulation cannot overlap recording. Measured at 60k and 120k objects on 2026-08-22, before RT-first and on a four-mesh fixture: CPU-bound at 78 and 34 FPS against 2.7 ms of GPU. The shipped ray-traced scenes are GPU-bound, so this is the scaling wall for AAA content rather than a cost today. A job system with recording on several threads has to be designed around the recorded rule that only one thread touches the RHI, and around the OpenGL backend (keep it serial or retire it: the owner's call).
- **What it causes.** CPU cost grows with object count on one core out of 24. At scale the frame is CPU-bound: 78 FPS at 60k objects, 34 FPS at 120k, with the GPU mostly idle. Physics and script time add straight onto frame time. For AAA scale (100k+ entities, many lights, many moving objects) the CPU limit arrives long before the GPU one.
- **Measured?** The scale numbers are measured (HANDOFF.md:7028-7029 and 7117-7121, 2026-08-22). The thread usage is inferred from code: no thread timeline tool exists to observe it.
- **Already recorded?** Not recorded. No job system, render thread or parallel recording appears in ROADMAP, NEXT, RT-FIRST, RT-SERIES or ENGINE-NOTES (searched). The scale measurements are recorded; the threading conclusion is not.
- **Fix direction.** Build one engine-owned job system sized to the hardware, and run Jolt through its JPH::JobSystem interface on top of it. Express per-frame CPU work as tasks with dependencies: animation, particles, transform update, extraction, culling, writing ray-tracing instances, and command recording per group of passes into parallel command buffers submitted in order. Then pipeline the frame so the simulation of frame N+1 overlaps the recording and submission of frame N; this needs the extraction snapshot from CORE-02. Prove every step with A,B,B,A palindromes on the scale fixtures and the garage.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/Application.cpp:723 - The frame loop (723-960) runs one step after another on the main thread: window pump, input, 0-15 fixed steps, device BeginFrame, every layer's OnUpdate (scene update plus all render recording), ImGui, audio, then submit and present.
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanDevice.cpp:1573 - Each frame slot resets and begins one command pool and one command buffer. EndFrame (1618-1627) submits that single buffer once, so all recording is on one thread and the GPU starts only after the whole frame is recorded.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Physics/PhysicsWorld.cpp:387 - Jolt gets its own JobSystemThreadPool of hardware_concurrency()-1 threads (23 on this CPU). It is created per play session and used only inside PhysicsSystem::Update (890). Nothing else in the engine can use it.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetManager.cpp:360 - The only other parallel CPU work: boot-time cook workers, started as raw std::threads on each call and capped at 4 (360-415).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Renderer.cpp:98 - The renderer keeps the active command list, the jitter and the per-frame pools in statics, and resets a dozen static subsystems every frame (98-128). That global state can serve only one thread and one world.
  - C:/Users/ism19/Code/RageV/docs/HANDOFF.md:7118 - Measured at 60,000 objects: about 12.8 ms of CPU in the lit pass's walk and submission, 2.9 ms in the depth passes and 2.6 ms in the transform walk, against 2.7 ms of GPU. Lines 7028-7029 give 78 FPS at 60k objects and 34 FPS at 120k, CPU-bound.
- **Skeptic's note.** The code matches. Application.cpp:723-960 runs input, 0 to 15 fixed steps, BeginFrame, every layer's OnUpdate (the scene update and all recording), ImGui and submit on one thread. VulkanDevice.cpp:1573-1574 and 1618-1627 record and submit a single command buffer. PhysicsWorld.cpp:385-388 gives Jolt a private pool that only Step uses (890). Renderer.cpp:98-128 resets static subsystems every frame. Three corrections. (1) The scale numbers (HANDOFF.md:7015-7029 and 7116-7121) date from 2026-08-22. That is before RT-first's G-buffer and traced passes (RT-FIRST.md:38, 2026-09-06), and they were taken on a fixture of four primitive meshes (see core-s4). Nothing has measured the CPU side since. Every shipped scene is GPU-bound: the garage measured 4.03 ms of GPU in a 4.05 ms frame with the CPU idle in wait (Scene.cpp:2027-2029), and camp was 4.9 of 4.9 ms (HANDOFF.md:7118-7120). So this is the wall for AAA-scale content, not a cost in today's frame, which makes it high rather than critical. (2) The fix direction runs into a recorded rule: 'The RHI is not thread-safe and an OpenGL context belongs to one thread ... Touch the device only on the main thread' (ENGINE-NOTES.md:1742-1748). The OpenGL backend is still checked (scenetest runs on both backends; see the RT-13 row in RT-SERIES.md). Recording on several threads therefore needs either a Vulkan-only path with OpenGL kept serial, or retiring OpenGL. That is the owner's decision, and the roadmap has to ask for it first. (3) Jolt's calling thread runs jobs while it waits, so the main thread is not idle during the step. The step is also empty in the demo scenes: showroom, the bridge and camp have no rigid bodies.

#### CORE-02 · Every frame rebuilds the renderer's view of the scene from scratch, several times, on the main thread

- **Verdict:** confirmed. **Severity:** high. **Kind:** scalability. **Scope:** rewrite.
- **Roadmap:** RT2-34, RT2-4, RT2-43 (The race is fixed early)
- **What is wrong.** There is no persistent render-side scene (a 'GPU scene': one record per object living in GPU memory and updated only when that object changes). Instead, three separate per-frame walks re-derive the same facts from the ECS: the draw list (2 to 4 times a frame), the instance rows for every static object, and the ray-tracing instance list (per view). Each repeats the asset lookups. Because the renderer reads live component memory through borrowed pointers, the next frame's simulation cannot overlap this frame's recording. The cull table is also rewritten in CPU-visible memory that the previous frame may still be reading.
- **What it causes.** CPU time scales with the total object count, not the number of changes, multiplied by 2 to 4 refreshes and by the number of views. At 60k objects: 2.9 ms of refreshes plus about 12.8 ms of lit walk and submission. For RT-first this is the same data the top-level acceleration structure (TLAS: the ray-tracing search structure over every instance), hit shading and the previous-transform reprojection (RT-17) all need. It is rebuilt rather than updated, so it cannot scale to AAA instance counts. The table race is an inferred correctness hazard: for moving objects, or when the object set changes, a frame could cull with the next frame's matrices.
- **Measured?** ENGINE-NOTES.md:11697 (2.9 ms for 2 refreshes at 60k), ENGINE-NOTES.md:11716-11724 (table rebuilt twice a frame = net loss of 1.6 ms), HANDOFF.md:7118-7121 (12.8 ms lit walk and submission). That the revert brought the 1.6 ms loss back is inferred, not re-measured. The table race is inferred from code, not observed.
- **Already recorded?** The costs are recorded in ENGINE-NOTES 7bx and HANDOFF 2026-08-22. The revert appears only in the code comment at Scene.cpp:1970-1985. The GPU-scene design and the race are not recorded anywhere.
- **Fix direction.** Introduce render proxies (the renderer's own record of each drawable object) with stable GPU indices, created and destroyed when components are added or removed, and updated from per-frame change lists. Keep transforms, previous transforms, material indices and bounds in GPU buffers, uploading only the changed rows each frame through the upload ring in CORE-03. The GPU cull, the lit instance data and the TLAS instance descriptors all read those buffers; a compute pass writes the TLAS instances. Give every CPU-written buffer one copy per frame in flight. Extraction produces a snapshot so simulation can overlap rendering. Prove it with per-pixel diffs (picture unchanged) and palindrome timings at 20k/60k/120k objects and in the garage.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:358 - UpdateWorldTransforms marks the draw list dirty every time it runs, whether or not anything moved, so each walk forces the next RefreshDrawList to rebuild everything.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:2100 - RefreshDrawList visits every mesh entity. Per object: two asset-cache hash lookups that return shared_ptr copies (2108, 2135), a bounds transform, emitter extraction, and a linear search for the mesh's slot (O(objects x distinct meshes); the comment at 2016-2033 says it reaches milliseconds as counts grow). It ends with a full GPU cull-table upload (2435).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:1970 - The table is rebuilt on every refresh as a correctness rule. This reverts the build-once-per-frame optimisation that ENGINE-NOTES 7bx relied on.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:2024 - The code's own count is 'two to four refreshes a frame': the shadow path refreshes (2540-2541, 1917), then OnRenderRuntime walks the transforms again (1482) and OnRender refreshes again (5028).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:5118 - Under the GPU-driven lit path, every static object's instance row is rewritten every frame, visible or not (5116-5159). Each row costs a material lookup, a parameter resolve and a probe search.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:2863 - The ray-tracing instance list is rebuilt from the ECS every frame and once per view (2862-2941). This is another walk of every mesh with asset lookups, and RayShadows::AddInstance (RayShadows.cpp:261-345) copies two shared_ptrs per instance.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.h:173 - DrawItem holds TransformComponent* and MeshComponent* pointers into live ECS storage for the whole frame (169-212). The renderer reads the simulation's own memory.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/GpuCull.cpp:86 - There is one object-table buffer per pass (84-98), and SetObjects copies into it on every refresh (390-391). HostVisible buffers are a single persistent mapping (VulkanResources.cpp:129-142) and two frames are in flight (EngineConfig.h:171), so the CPU can overwrite the table while the previous frame's cull is still reading it.
  - C:/Users/ism19/Code/RageV/docs/ENGINE-NOTES.md:11697 - Measured at 60k objects: RefreshDrawList, 2 calls a frame, 2.9 ms. Lines 11716-11724: rebuilding the table twice a frame made GPU culling a net loss of 1.6 ms.
- **Skeptic's note.** Checked in the code. UpdateWorldTransforms marks the draw list dirty every time it runs (Scene.cpp:350-358). RefreshDrawList then looks up each object's mesh and material in the asset caches again (2108, 2135), finds the mesh's slot by linear scan (2016-2033, 2083-2100), extracts emitters, and uploads the cull table on every rebuild (2413-2436). Rebuilding the table on every refresh undoes the build-once rule of ENGINE-NOTES 7bx; building it twice a frame was measured there as a net loss of 1.6 ms (11716-11724). In play mode the frame rebuilds twice: RenderShadowMaps at 2540-2541, then OnRenderRuntime (1482) followed by OnRender (5028). The editor rebuilds more often. The ray-instance list is rebuilt from the ECS once per view, with asset lookups and three shared_ptr copies per instance (2862-2941; RayShadows.cpp:261-345). DrawItem holds pointers into live component storage (Scene.h:169-212). The table race is real. GpuCull keeps one CPU-writable ('host-visible') Objects and Template buffer pair per pass (GpuCull.cpp:84-98), and Upload is a plain memcpy into their permanent mapping (VulkanResources.cpp:189-193). With two frames in flight, frame N+1 overwrites the table while frame N's cull may still be reading it. Every other buffer the renderer writes each frame has one copy per frame in flight (Material.cpp:45-56, Renderer3D.cpp:1376-1392), so this one is an outlier. There is a second problem with the same buffer: it is rewritten again inside the frame after the cull dispatches have been recorded (the refresh inside the graph at 5028). Every dispatch therefore reads the last write, and the 'rebuild on every refresh' rule at 1970-1985 cannot guarantee the match it claims. With ray tracing on, the drawn matrices come from instance rows that have one copy per frame, so the race affects which objects count as visible and how the slots are laid out. On the raster fallback, the depth views draw with the table's own matrices. Small corrections: the lit rows rewritten every frame cover every object in both cull tables, static and moving, not only static ones. Writing every row instead of sorting was a measured choice: the sort was 4.1 ms of a 13.9 ms graph (Scene.cpp:5078-5086). All the costs come from the 2026-08-22 fixture measured before RT-first, and nothing here breaks a shipped scene today, so high rather than critical.

#### CORE-03 · Every GPU upload stops the CPU until the GPU has finished all queued work

- **Verdict:** confirmed. **Severity:** high. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-18
- **What is wrong.** A staging buffer is CPU-visible memory the data is copied into before the GPU copies it to its final place. A fence is the GPU's 'finished' signal that the CPU can wait on. Every upload, texture creation, texture mip and static acceleration-structure build here is its own submission followed by a CPU wait on its fence. All of them share the one graphics queue, so the first such wait in a frame also waits for the whole previous frame's GPU work, and the CPU/GPU overlap collapses for that frame. There is no upload ring (a reusable staging buffer written in a circle), no transfer queue, no batching, and no way to track completion without waiting.
- **What it causes.** Boot time scales with the number of buffers and mips times one GPU round trip each. At runtime, any first use of a mesh or material (a prefab spawned, anything streamed, a hot reload, the first ray hit on a newly visible mesh) stalls the frame: the whole in-flight GPU frame for the first wait, then a round trip per extra upload. The garage has 155 distinct meshes (Scene.cpp:2021), so the first traced frame performs about that many blocking acceleration-structure builds (inferred). Streaming is impossible on this path. The uncompacted acceleration structures also cost VRAM (see CORE-05).
- **Measured?** Nothing about the stall is measured. The boot total of 3.38 s (ENGINE-NOTES.md:1795) includes these uploads.
- **Already recorded?** Not recorded.
- **Fix direction.** Build one upload service: a persistent staging ring per frame in flight, copies recorded into the frame's command buffer or onto the dedicated transfer queue with a timeline semaphore (a GPU counter the CPU and other queues can check without stalling), and acceleration-structure builds batched per frame with compaction, under a per-frame upload budget that is a global render setting. Completion is tracked by timeline values, never a CPU wait on the frame path. Add a pipeline cache and background shader and pipeline compilation for setting changes. Measure first-use hitches (slow-frame log) and boot time before and after.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanDevice.cpp:2113 - ImmediateSubmit sends a one-off command buffer to the graphics queue and then blocks on vkWaitForFences (2114). That wait covers all earlier work on the queue, including the previous frame still in flight.
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanResources.cpp:208 - Every upload to a GPU-only buffer creates a new VMA staging buffer (208), does an ImmediateSubmit (213) and destroys the staging buffer (221).
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanResources.cpp:343 - Creating any sampled or storage texture does a blocking submit just to set its initial layout (343, 352).
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanResources.cpp:569 - Region uploads (569), per-mip and per-layer uploads (610) and GenerateMips (642) each block the same way.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/TextureLoader.cpp:493 - Cooked textures upload one mip at a time: a 13-mip texture costs 13 blocking submits, plus one when it is created.
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanResources.cpp:991 - Static per-mesh acceleration structures are built PREFER_FAST_TRACE without ALLOW_COMPACTION, with a blocking submit (1046).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Mesh.cpp:209 - The per-mesh acceleration structure is built lazily the first time it is asked for (209-230), and so are the meshlet buffers (162-193): that is, mid-frame, the first time a mesh is traced or drawn.
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanDevice.cpp:136 - A dedicated transfer queue family is detected (135-137), but only the graphics and present queues are ever created (695-696).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Renderer3D.cpp:2402 - Changing a ray-tracing setting recompiles the lit shader family and rebuilds its pipelines synchronously. The trigger comes mid-frame from Scene::RenderShadows (Scene.cpp:1757).
- **Skeptic's note.** Checked in the code. ImmediateSubmit submits to the graphics queue and waits on a fence, a signal the GPU raises when it has finished (VulkanDevice.cpp:2086-2118). A fence from vkQueueSubmit also covers all earlier work on that queue, so the first such wait in a frame waits for the frame still in flight. Every upload to a GPU-only buffer creates and destroys its own staging buffer (VulkanResources.cpp:196-221). Creating a sampled or storage texture blocks, and so do region, mip and layer uploads and GenerateMips (343, 352, 569, 610, 642). Cooked textures upload one mip at a time (TextureLoader.cpp:492-494). Static acceleration structures are built PREFER_FAST_TRACE with no compaction and a blocking build (VulkanResources.cpp:985-1049; the word COMPACT appears nowhere in RageV/src). Acceleration structures and meshlet buffers are built lazily (Mesh.cpp:143-230). A transfer queue family is detected (VulkanDevice.cpp:135-137), but only the graphics and present queues are created (695-696). Changing a ray-tracing setting recompiles the lit shader family mid-frame (Renderer3D.cpp:2378-2403, Scene.cpp:1756-1758). One correction: every mesh enters the TLAS every frame with no frustum test (Scene.cpp:2842-2931). All acceleration structures are therefore built in the first traced frame, or when a mesh first appears, not on the first hit of a newly visible mesh. I checked every per-frame upload site: a steady-state frame makes no blocking submits. The costs are first-use hitches, boot time, and the impossibility of streaming, all unmeasured, as the finding says. A recompile loads from the SPIR-V disk cache when the variant has been compiled before (ShaderCompiler.cpp:406-419). Pipeline creation is still synchronous, and no VkPipelineCache exists.

#### CORE-04 · Assets load synchronously on first use; there is no streaming, eviction or asynchronous I/O

- **Verdict:** confirmed. **Severity:** high. **Kind:** missing-capability. **Scope:** rewrite.
- **Roadmap:** RT2-40
- **What is wrong.** Loading is on demand and blocking: whatever code first asks for a mesh, material or texture does the whole read, decode and upload inline, including render code in the middle of a frame. The boot 'prepare' phase only warms the import cache, so on a warm boot all real work (read, deserialise, upload) is serial on the main thread. Assets have no load states, placeholders, priorities, reference-driven eviction or partial (mip/LOD) loading. The registry cannot be used from more than one thread, and a game cannot change level.
- **What it causes.** Visible hitches whenever something new appears (spawn, hot reload, a streamed-in object). Boot does not use the machine's cores on a warm cache. A world larger than memory cannot exist, and a game has one scene per process. Every AAA-scale scene (open world, many unique assets) is blocked on this.
- **Measured?** Boot times from 2026-08-13 are measured (ENGINE-NOTES.md:1794-1795). The project has since grown to about 1.6 GB on disk and boot has not been re-measured. Hitch sizes are inferred.
- **Already recorded?** The boot split and import cache are recorded (ENGINE-NOTES 7l, ROADMAP 7.11). Streaming, eviction, asynchronous I/O and runtime level loading are not planned anywhere.
- **Fix direction.** Build a streaming system. It has an asynchronous request queue with priorities and cancellation. File reads run on I/O threads (overlapped reads now, DirectStorage later if measured to pay), decoding runs on jobs, and uploads go through CORE-03. Assets move through load states with placeholders, and reference-counted handles drive eviction under the budget in CORE-05. Add mip streaming (cooked textures already store mips separately) and mesh LOD streaming, plus a runtime API to load and unload levels and cells. Make the registry a thread-safe, read-mostly snapshot. Keep the boot loading screen as the first client. Measure load-stall frames and boot time with palindromes.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetManager.cpp:791 - On a cache miss, GetMesh imports the whole model file and builds and uploads every primitive on the calling thread (759-828). RefreshDrawList calls it mid-frame (Scene.cpp:2108).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetManager.cpp:974 - On a miss, GetMaterial loads the .rmat and every map it names through GetTexture and TextureLoader::Load2D, synchronously (909-1033).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetManager.cpp:384 - The parallel boot phase reads each cooked file and discards the bytes ('The result is discarded'). GltfImporter.cpp:719 then reads the same file again on the main thread, and UploadPrepared (431-485) handles one asset at a time on the main thread.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetManager.cpp:490 - Caches are only ever emptied wholesale, on a project change. Nothing tracks use or evicts.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/IO/PakFile.cpp:250 - Every pak read opens a new ifstream and reads the whole entry. There are no partial reads (a single mip or LOD), no asynchronous requests and no priorities.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Audio/AudioEngine.cpp:339 - Playing a non-streamed clip decodes it in full on the calling thread (345), for example a script's one-shot inside the fixed step.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetRegistry.cpp:17 - The registry is two maps with no locking, and GetMetadata returns references into them (212-220). It is not safe to use from concurrent loaders.
  - C:/Users/ism19/Code/RageV/RageVRuntime/src/RuntimeLayer.cpp:144 - The runtime loads exactly one start scene, at boot. No API anywhere loads or changes a scene at runtime: searching RageV/src, RageVScriptCore/src and RageVRuntime/src for LoadScene or ChangeScene finds nothing.
  - C:/Users/ism19/Code/RageV/docs/ENGINE-NOTES.md:1795 - Measured 2026-08-13: runtime boot 7.50 s without the import cache, 3.38 s with it, on 198 MB of textures. The sample assets are about 1.6 GB on disk today (du, read-only).
- **Skeptic's note.** Checked in the code. On a cache miss, GetMesh imports and builds every primitive of the model inline (AssetManager.cpp:759-828), and RefreshDrawList calls it mid-frame. GetMaterial loads its maps synchronously (909-1033). The boot workers call ImportCache::Fetch and throw the bytes away; the code itself says warm boots 'do none of this work' (360-415). GltfImporter.cpp:719 then reads the file again on the main thread during UploadPrepared (431-485). Caches are cleared wholesale on a project change; the editor's Invalidate removes entries for changed files, but nothing tracks use or evicts. Pak reads open a new ifstream and read the whole entry (PakFile.cpp:250-258). The registry is two unlocked maps that hand out references (AssetRegistry.cpp:17-18, 212-220). The runtime loads one start scene (RuntimeLayer.cpp:137-150), and a search for LoadScene, ChangeScene or OpenScene in RageV/src, RageVScriptCore and RageVRuntime finds nothing. ROADMAP, NEXT and RT-FIRST contain no streaming item (grep). Note: ENGINE-NOTES 7l still describes a worker that loads assets and passes device work to the main thread (1742-1756). The code no longer works that way, so that section is stale. Audio: miniaudio decodes a clip in full the first time it plays (AudioEngine.cpp:339-345). The boot times date from 2026-08-13, on 198 MB. The project is now about 1.6 GB on disk, of which 641 MB is .rvfield bakes, which are not registry assets.

#### CORE-05 · No GPU memory budget, no memory tracking and no allocator strategy

- **Verdict:** confirmed. **Severity:** high. **Kind:** missing-capability. **Scope:** refactor.
- **Roadmap:** RT2-1, RT2-19, RT2-40 (Measuring; budget; eviction)
- **What is wrong.** VRAM budget means how much GPU memory the driver says this process may use right now. Residency management means deciding what stays in GPU memory. The engine does neither: it never asks for the budget, never counts its own usage by category, and never frees assets it no longer needs. Acceleration structures stay uncompacted, and every mesh's geometry is duplicated in system memory. CPU memory has no strategy either: per-frame scratch data comes from the general heap.
- **What it causes.** On the 12 GB laptop GPU, once textures, geometry, acceleration structures, per-view histories and render targets pass the budget, the driver silently pages to system memory (stutter) or allocation fails and is only logged. The engine can neither see this coming nor react. Ray tracing makes it worse (large uncompacted structures). There is no data for sizing an AAA scene to the hardware.
- **Measured?** Nothing measured.
- **Already recorded?** Not recorded.
- **Fix direction.** Build a memory service. Enable VK_EXT_memory_budget and poll it every frame. Account per category (textures, geometry, BLAS and TLAS, render targets, histories, staging) and show it in the profiler and the benchmark report. The streaming system evicts against the budget. Compact acceleration structures. Drop the CPU geometry copies: picking can use the ray-tracing structure that already exists, or an ID buffer. Add per-frame linear allocators for scratch data. Measure VRAM per scene before and after, and prove eviction does not change the picture (per-pixel diff at settle).
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanDevice.cpp:715 - VMA (the Vulkan memory allocator library) is created without its memory-budget flag (715-726). Nothing in RageV/src queries heap budgets: searches for vmaGetHeapBudgets, VK_EXT_memory_budget and MEMORY_BUDGET find nothing.
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanCommon.h:21 - VK_CHECK only reports a failed call and carries on, so running out of memory shows up as a log line followed by a null resource.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetManager.cpp:39 - About twenty caches (36-103) hold shared_ptrs to every mesh, texture and material ever touched until the project closes.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Mesh.h:180 - Every mesh keeps its positions and indices on the CPU for its whole life, for viewport picking (126-139).
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanResources.cpp:991 - Static acceleration structures are built without compaction, so they stay at their build size.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:4846 - Heap allocations in per-frame loops: a Pose vector per animator (4846, 4871, 4879), a bind-pose vector per un-animated skinned draw (5243), handle vectors per script pass (956, 1028, 1167) and a LightList per view (4965). No frame allocator exists: searches for arena, linear or pmr allocators find none.
  - C:/Users/ism19/Code/RageV/RageVRuntime/src/RuntimeLayer.h:73 - 16 full-screen temporal histories per view (73-110), 32 in the editor with its two views. None of this is counted against any budget.
- **Skeptic's note.** Checked in the code. VMA is created without the budget flag, and nothing asks for a budget (VulkanDevice.cpp:710-726). RageV/src has no vmaGetHeapBudgets, no memory_budget and no VRAM accounting; the editor's 'VRAM' row shows the card's total capacity (EditorLayer.cpp:2923). About 21 caches hold every asset until the project closes (AssetManager.cpp:36-103). Meshes keep CPU copies of positions and indices (Mesh.h:126-139, 180-181). Static acceleration structures are not compacted. Per-frame loops allocate on the heap: a Pose per animator, a bind pose per un-animated skinned draw, script handle vectors, and a LightList per view. No frame allocator exists (no pmr, arena or linear allocator in RageV/src). Precision notes: VK_CHECK logs a failure and carries on only in Release; in Debug, RV_CORE_ASSERT breaks into the debugger (VulkanCommon.cpp:65-67, Core.h:19-29). Of the 16 TemporalHistory members per view (RuntimeLayer.h:72-110), four are ray-budget maps with one texel per 16x16 tile and three exist only when the scene has water. There are also four MeasuredChangeHistory members, so 'full-screen' fits about twelve. The CPU geometry copy also feeds the lazy meshlet build (Mesh.cpp:155-158) and is kept for mesh colliders (Mesh.h:128-133), so dropping it means building meshlets when the asset is imported. Nothing about VRAM has been measured.

#### CORE-06 · The Scene class owns renderer state and drives renderer globals; the ray-tracing structure is built inside the shadow-map path

- **Verdict:** confirmed. **Severity:** high. **Kind:** architecture. **Scope:** rewrite.
- **Roadmap:** RT2-17, RT2-35
- **What is wrong.** Three layers are fused together. The simulation's Scene (5.5k lines) holds renderer data, bakes, GPU tables and draw submission. The renderer is a set of static singletons that the scene configures by calling setters. Per-view renderer memory (the temporal histories every ray-traced signal accumulates into) is owned by the application layers and duplicated for each of the three views. For an RT-first engine the key oddity is that the acceleration structure (the scene as rays see it) is built as a side effect of the shadow-map function, and switching shadows off switches off all ray tracing.
- **What it causes.** This blocks a render thread, multiple worlds, testing the renderer in isolation, and a renderer rewrite: every RT-series 2 change must go through Scene.cpp and three application layers, and each new temporal signal means edits in four places. RT features depend on an unrelated toggle.
- **Measured?** Not measured; structural.
- **Already recorded?** The shadow gating is documented as a choice (comment at FrameGraphBuilder.cpp:211-216). The coupling is not recorded as a problem.
- **Fix direction.** Split three ways. The Scene holds entities and simulation and publishes changes. A render world owned by the renderer holds the proxies, lights, TLAS, probes and bake orchestration, and updates the TLAS as a world-update step independent of shadows. View objects hold each view's histories, exposure and ray budgets, and an application creates one with a single call. Move bake orchestration into a lighting-bake service. Verify pixel-identical output (per-pixel diff, zero) before any behaviour change lands.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.h:578 - The irradiance field and its whole bake state machine are members of Scene (571-700).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.h:954 - The GPU cull tables, blended tables, area emitters and culled views are all Scene members (954-1018).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:11 - Scene.cpp includes 19 renderer headers (11-32) and calls about 40 distinct Renderer3D static functions (counted).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:1757 - Every frame the scene pushes ray-tracing modes into Renderer3D's static state (1756-1782, 1900-1902).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:2862 - The top-level ray-tracing structure is built inside RenderShadowMaps (2525-2541, 2842-2943).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:217 - 'The rays ride on the shadow pass': ray tracing, and with it reflections, GI and AO, is off whenever ShadowsEnabled is off (210-218).
  - C:/Users/ism19/Code/RageV/RageVRuntime/src/RuntimeLayer.cpp:532 - The application layer passes about 18 pointers to its own histories, budgets and exposure into FrameDesc (532-549). The editor does the same twice, once per view (EditorLayer.cpp:672-689, 830-847).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:4918 - A file-level global (g_LitTail) carries OnRender's leftover drawing into the lit pass, on the assumption that 'one scene draws at a time' (4902-4930).
- **Skeptic's note.** Checked in the code. The irradiance field and its bake state live in Scene (Scene.h:569-700), as do the GPU cull tables and the emitters (Scene.h:949-1018). Scene.cpp includes 19 renderer headers and calls 42 distinct Renderer3D static functions (counted). The scene pushes ray-tracing modes into renderer globals every frame (Scene.cpp:1756-1782, 1900-1902). The TLAS list and its build sit inside RenderShadowMaps (2525-2541, 2842-2943), and ResolveRayTracing returns false whenever ShadowsEnabled is off (FrameGraphBuilder.cpp:209-218). RuntimeLayer hands 18 history and budget pointers to FrameDesc (RuntimeLayer.cpp:532-549), and EditorLayer does it twice (672-689, 830-847). A new temporal signal therefore touches FrameDesc and three layer sites. g_LitTail is a file-level global (Scene.cpp:4902-4930). The shadows gate is documented as a design choice and was never measured as a problem, which the finding already says.

#### CORE-07 · The transform walk visits every entity 4 to 7 times a frame and finds each child through a hash map

- **Verdict:** confirmed. **Severity:** high. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-34, RT2-4, RT2-43 (The particle guard is fixed early)
- **What is wrong.** The compare-instead-of-recompute fix made each walk cheap per node, but the design still does O(all entities) work per call. It is called from about ten sites, including once per fixed step. The hierarchy is keyed by UUID, so every child costs a hash-map lookup and a pointer chase. Rotation is stored as Euler angles (three angles), so physics sync and reparenting convert quaternion to Euler and back every time, which loses precision near straight up or down. The GPU particle path adds an unconditional walk that the CPU path already guards against.
- **What it causes.** About 2.6 ms a frame at 60k flat objects, measured. Real imported content is hierarchical (a car is about 150 parts under one root), so it pays a hash lookup per child that the measurement never included. The cost rises with frame time: more fixed steps means more walks, a feedback loop the notes call 'the shape of a spiral'.
- **Measured?** ENGINE-NOTES.md:11696 (7 calls, 27.4 ms before the fix), HANDOFF.md:7043 and 7118-7121 (2.6 ms after the fix), Components.h:105-108 (about 5 ns per object). The per-child hierarchy cost is inferred, not measured.
- **Already recorded?** Recorded in ENGINE-NOTES 7bx; roadmap 8.15 fixed the recompute. Dirty tracking was declined for robustness (Components.h:73-83). The call count, the UUID-keyed children and the unguarded GPU-particle walk are not recorded.
- **Fix direction.** Keep the owner's guarantee that nothing can silently skip an update, but get it by construction. Transforms are written only through an API that stamps a per-entity change version (mutable access goes through a function that marks it), and a debug-only full compare walk asserts that no write went around it. Store the hierarchy as parent indices in depth order (flat arrays) and compute world matrices in one linear pass over changed subtrees, which can later run in parallel by depth level. Derive exactly once after simulation and once after frame scripts; render paths read and never re-derive. Store quaternions and show Euler only in the editor. Patch now: add the emitter guard to Gpu::Simulate. Measure on a hierarchical fixture as well as the flat one.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:341 - On every walk, each child is found from its UUID through the scene's unordered_map (341-345). Every node is visited even when nothing moved (338-340), and each compares nine floats against a cached copy (318-321).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Components.h:50 - Each entity stores its children as a heap std::vector of UUIDs (47-54). TransformComponent is about 200 bytes, with rotation stored as Euler angles (68), two matrices and cached copies (64-117).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:1239 - A full walk runs after every fixed step, so the count grows with the number of steps a slow frame runs. More full walks: 890 and 913 per frame, 1482 and 1505 per render entry, 2540 in the shadow path, 3141 (voxel GI) and 3313 (probes).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Particles/GpuParticles.cpp:415 - Gpu::Simulate forces a full walk every frame whenever the GPU particle pipeline exists, even in a scene with no emitters. The CPU particle path has the guard that skips this (ParticleSystem.cpp:197-207); the GPU path does not.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:829 - AdvanceMotionHistory makes another full pass every frame, copying World into PreviousWorld for every transform (827-835).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:264 - SetParent runs a full walk on every call, so reparenting many entities costs O(n squared).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Components.h:73 - Dirty flags were declined on purpose: 'a single missed one leaves an object silently rendering in the wrong place' (73-83).
  - C:/Users/ism19/Code/RageV/docs/ENGINE-NOTES.md:11696 - Measured at 60k objects: 7 walks a frame, 27.4 ms before the compare fix. After the fix: about 5 ns per object per walk (Components.h:105-108), 2.6 ms a frame (HANDOFF.md:7118-7121). The scale fixture has no hierarchy (make_scale_scenes.py writes no parents), so the per-child hash lookups were never measured.
- **Skeptic's note.** Checked in the code. Each walk visits every node, compares nine floats (Scene.cpp:318-321), and resolves every child through GetEntityByUUID (341-345). Children are heap vectors of UUIDs (Components.h:47-54). Rotation is stored as Euler angles; GetLocalTransform builds from FromEuler. Walks run after every fixed step (1239); at 890 and 913 every frame; at every render entry (1482, 1505); in the shadow path (2540); and conditionally for voxel GI (3141, only when ray-traced GI is off) and probes (3313, only while one is capturing). Gpu::Simulate walks unconditionally whenever the GPU particle pipeline exists, which is on every compute-capable device (GpuParticles.cpp:307, 404-415). The CPU particle path has a guard for this (ParticleSystem.cpp:197-207). AdvanceMotionHistory copies every transform every frame (827-835). SetParent walks the whole scene on each call (264), and InstantiateModel calls it once per part (AssetManager.cpp:2081). The scale fixture writes no parents (make_scale_scenes.py), so the per-child cost is unmeasured, as the finding says. For the roadmap: the brief's 'the CPU wall is the transform walk' predates the compare fix. After that fix the walk costs 2.6 ms at 60k objects, against 12.8 ms for the lit pass's walk and submission (HANDOFF.md:7118-7121). Changing TransformComponent's layout (quaternions, dropping the cached fields) breaks native script modules compiled against it (Components.h:99-103, ENGINE-NOTES 7by), and also the C# Euler Get/SetRotation API. The refactor needs a compatibility step for both.

#### CORE-08 · Objects moved in the fixed step are not interpolated, so above 60 FPS they move stop-go, including the test mover used for the RT temporal work

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** medium. **Kind:** image-quality. **Scope:** refactor.
- **Roadmap:** RT2-0, RT2-34 (The `Slider` test mover; the engine's interpolation (an owner-judged arm))
- **What is wrong.** Transforms moved in OnTick are not interpolated, a documented design choice. At display rates other than 60 Hz they move stop-go, and their motion vectors faithfully report that (zero, then a whole step). The RT test movers are tick-driven: Slider on the MovingPanel and on the car in watch_arm.py, and RT-24's Orbiter. watch_arm.py plays live with vsync on a 240 Hz display, so the RT-22 and RT-23 arms the owner judged live showed a mover that stops about every other frame, while pinned captures step once per frame. Fix the test tools first: move the movers to OnFrame, or pin the live runs. Whether the engine should interpolate every transform written in the fixed step is a separate design question, weighed against ENGINE-NOTES section 1.
- **What it causes.** Every object moved on the tick (gameplay movers, AI, vehicles, doors) gets wrong motion vectors at any display rate other than the tick rate. On screen: judder, plus ghosting or lag in every temporal RT signal. It also muddies the live-versus-captured judgements behind the moving-object arms of RT-15, RT-22 and RT-23.
- **Measured?** Not measured. The mechanism is established from code, and the stop-go frame pattern follows from the arithmetic (60 steps spread over 90 to 125 frames a second).
- **Already recorded?** Judder of presentation code in OnTick is recorded as a rule for script authors (HANDOFF.md:8458-8462, ENGINE-NOTES.md:78-87). That the RT test mover is itself tick-driven, and what that does to motion vectors and live judgements, is not recorded.
- **Fix direction.** Measure first, deterministically: run showroom_moving at --frame-time=0.0166 and at 0.00833 (half a tick, which reproduces the live stop-go pattern), then diff the velocity lane and the reflection history (--capture-signals). If confirmed, interpolate every transform written in the fixed step at the engine level: the transform system keeps previous and current simulation poses for anything a step touched and blends them by the alpha each frame (generalising SyncTransforms), so motion vectors come from interpolated poses. Until then, move Slider to OnFrame and re-judge the affected arms with smooth motion.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/docs/ENGINE-NOTES.md:57 - Design decision: only physics-driven entities keep two transforms and are interpolated (57-60).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Physics/PhysicsWorld.cpp:930 - SyncTransforms blends previous and current poses by the interpolation alpha for Jolt bodies only (917-953).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:1226 - OnTick scripts move transforms inside the fixed step. Nothing stores a previous/current pair for them.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:1125 - The motion history (last frame's transforms, from which motion vectors are made) is taken once per frame (1125-1129, 859-861). On a frame with no fixed step, World equals PreviousWorld, so a tick-driven mover reports zero motion; the next frame with a step reports the whole step.
  - C:/Users/ism19/Code/RageV/SampleProject/Source/Slider.cpp:22 - The dolly and mover script translates its entity in OnTick (22-30).
  - C:/Users/ism19/Code/RageV/SampleProject/assets/scenes/showroom_moving.rage:9793 - The chrome MovingPanel (roughness 0.12, 3 m/s) used for the moving-reflection work (RT-15, RT-22, RT-23) is driven by Slider (9776-9795).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/FixedStep.h:43 - Steps run at 60 Hz (EngineConfig.h:175). At the showroom's measured 8 to 11.6 ms frames (RuntimeLayer.cpp:595; RT-FIRST T4), roughly 30 to 50% of live frames run no step. Under --frame-time=0.0166 nearly every frame runs exactly one.
  - C:/Users/ism19/Code/RageV/docs/RT-SERIES.md:75 - RT-23 records that the mover was judged live because captured runs differ, attributing the difference to capture timing ('writing a PNG a frame gives the accumulator time the real thing does not have').
- **Skeptic's note.** The mechanism is right. Only Jolt bodies are interpolated (ENGINE-NOTES.md:57-60, PhysicsWorld.cpp:917-953). Slider moves in OnTick (Slider.cpp:22-30); it drives the MovingPanel (showroom_moving.rage:9776-9795) and the car that watch_arm.py injects (watch_arm.py:336). The motion snapshot is taken once per frame, before any tick (Scene.cpp:1115-1129, 859-861). The stated consequence is wrong, though: the motion vectors are correct. They report exactly the motion that was drawn: zero on a frame with no step, a whole step on a frame with one. Reprojection is therefore correct. What is wrong is the motion itself, which is stop-go, and it makes every accumulator alternate between its still and its moving behaviour. 'Ghosting or lag in every temporal RT signal' is not established. Judder from presentation code in OnTick is a documented engine rule (ENGINE-NOTES.md:73-87, HANDOFF.md:8458-8462), so the general case is by design. What is new is the measurement confound, and it is stronger than the finding says. watch_arm.py plays live with --vsync=on (line 415), and the code records the display as 240 Hz (VulkanDevice.cpp:1256-1263). At the garage's 8-12 ms frames, the movers the owner judged live for RT-22 and RT-23 therefore stepped on roughly every other frame, while pinned captures step exactly once per frame. RT-24's new Orbiter (SampleProject/Source/Orbiter.cpp:39-43, uncommitted) is also OnTick. spin_measure.py pins --frame-time to exactly 1/60 (spin_measure.py:35, 135), which is the same float as the fixed step, so its numbers are clean. The owner's hand spin uses ShowroomCamera.cs's OnFrame and is smooth.

#### CORE-09 · The asset registry re-reads and re-hashes every source file at every launch and on every editor refresh

- **Verdict:** confirmed. **Severity:** medium. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-4, RT2-40 (Change detection early; the watcher later)
- **What is wrong.** Change detection re-reads the whole project: every launch and every editor refresh hashes all loose source bytes on the main thread. Hot reload then compares every cached entry against the changed file with a filesystem call, and clears caches unrelated to it.
- **What it causes.** SampleProject holds about 805 MB of image, model and audio source across 1,629 files (counted read-only today). At byte-at-a-time FNV speeds that is about a second before anything appears at launch, and a similar freeze on every file save in the editor (inferred, not measured). With AAA source sizes (100+ GB), boot and every save would take minutes.
- **Measured?** It was 198 MB when recorded (ENGINE-NOTES 7l). Not re-measured since the project grew to about 1.6 GB on disk; the one-second figure is an estimate.
- **Already recorded?** Recorded as open, 'the next thing to hurt' (ENGINE-NOTES.md:1776-1782, HANDOFF.md:11836-11839), along with the CRLF trap (HANDOFF.md:1494). The claim that it runs on the boot worker is wrong for the current tree.
- **Fix direction.** Detect changes by file size plus modification time stored in the .meta sidecar, and hash only when those move or a cooked entry is missing. Hash with a fast chunked algorithm on worker threads. Refresh only the paths the watcher reports. Move the watcher to OS change notifications (ReadDirectoryChangesW) on a background thread. Invalidate through a handle-to-source index instead of calling filesystem::equivalent on every entry. Time boot and a single-file save before and after.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetRegistry.cpp:180 - ReadOrCreateMeta hashes every loose asset file on every scan.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetRegistry.cpp:269 - The hash is FNV-1a, one byte at a time, through an 8 KB read buffer (257-280).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/Application.cpp:416 - Registry::Init runs in AdoptProject, which the Application constructor calls (183-186) on the main thread with the window still hidden (102-103), before the loading screen or its worker exist.
  - C:/Users/ism19/Code/RageV/docs/ENGINE-NOTES.md:1776 - The notes say this hashing 'moves onto the worker with everything else and gets its own progress phase'. The current code does neither.
  - C:/Users/ism19/Code/RageV/RageVEditor/src/EditorLayer.cpp:1108 - Every batch of watched file changes triggers a full Registry::Refresh (re-scan and re-hash of the whole tree) on the main thread. Every Create* call does the same (AssetManager.cpp:1047, 1313, 1399, 1488, 1609, 1700, 1781, 1866).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetManager.cpp:562 - Invalidate calls std::filesystem::equivalent for every cached entry (on Windows it opens both files), and clears the texture loader, IBL and probe caches whenever anything is dropped (623-628).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetWatcher.cpp:92 - The editor walks the whole asset tree on the main thread every 0.5 s (AssetWatcher.h:55).
  - C:/Users/ism19/Code/RageV/docs/HANDOFF.md:1494 - Because the hash is over raw bytes, it changes after a CRLF checkout, so text assets re-import after git operations. The .gitattributes decision is the owner's call and still open.
- **Skeptic's note.** Checked in the code. Every Refresh re-hashes every loose asset (AssetRegistry.cpp:73-125, 178-181) with FNV-1a, one byte at a time (257-280). Registry::Init runs in AdoptProject, called from the constructor while the window is still hidden (Application.cpp:180-186, 101-103, 409-416), not on a worker. ENGINE-NOTES.md:1776-1782 and HANDOFF.md:11836-11839 are wrong about where it runs. The editor refreshes on every batch of changes (EditorLayer.cpp:1108) and in every Create* call (AssetManager.cpp:1047 to 1866). Invalidate calls filesystem::equivalent for each cached entry, and clears the loader, IBL and probe caches whenever anything is dropped (562-628). The watcher walks the whole tree every 0.5 s on the main thread (AssetWatcher.cpp:74-130). Precision: pak entries skip hashing (AssetRegistry.cpp:164-176), so a shipped game does not pay this; it is a development and editor cost. The 805 MB is 861 image, model and audio files (1,629 is every non-.meta file), and about 100 MB of .rage scenes are hashed too. The 641 MB of .rvfield bakes are not registry assets. The one-second figure is an estimate, as the finding says.

#### CORE-10 · The profiler cannot see where CPU time goes outside rendering

- **Verdict:** confirmed. **Severity:** medium. **Kind:** missing-capability. **Scope:** patch.
- **Roadmap:** RT2-2, RT2-1
- **What is wrong.** The CPU side has no nested timing zones, no per-thread timeline (it will need one once CORE-01 lands), no counters (bytes uploaded, blocking submits, asset-cache misses, heap allocations, VRAM), and no external profiler (no Tracy, PIX or Superluminal). The whole simulation half of the frame is reported as unaccounted time. One always-on GPU timer feeds nothing.
- **What it causes.** The costs this audit infers (registry scans, repeated walks, upload stalls, text parsing) can only be checked by adding temporary timers each time, which is exactly how the 27.4 ms walk was found. AAA-scale work needs these numbers from every run.
- **Measured?** Not applicable.
- **Already recorded?** The method is recorded (ENGINE-NOTES.md:11707). The missing phases and the orphaned ray timer are not.
- **Fix direction.** Add a scoped CPU zone macro (nestable, thread-aware, compiled in but near-free when off) with an optional Tracy backend. Add phases for Simulation, Scripts, Physics, Animation, TransformUpdate, Extraction, TLAS instances and Uploads. Put counters in the --benchmark report: blocking-submit count and milliseconds, bytes uploaded, asset-cache misses, allocations per frame, VRAM per category (from CORE-05). Either wire LiveRayGpuMs to a reader or delete it and fix the comment. Keep the GPU side as it is.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/FrameProfiler.h:28 - There are seven fixed phases (28-48): Wait, EnvironmentPrefilter, Shadows, Probes, Graph, ImGui, Present.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/Application.cpp:768 - The fixed steps (768-779) and the layers' OnUpdate (810-812) run outside every phase: scripts, physics, animation, transform walks, particles and audio all go unmeasured.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/FrameProfiler.cpp:296 - LiveRayGpuMs has no caller. The ray passes still write their own GPU timestamps every frame (FrameGraphBuilder.cpp:2055, 2201, 4495, 4681), and Application.cpp:802-806 describes a GPU-time ray-budget controller that does not exist.
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanDevice.cpp:1691 - What already works well: NV checkpoints and VK_EXT_device_fault crash reports (1691-1799), and debug labels on GPU work (VulkanCommandList.cpp:836-840).
  - C:/Users/ism19/Code/RageV/docs/ENGINE-NOTES.md:11707 - The project's own rule, 'measure the phase, not the feature', came from finding a 27.4 ms function that no phase covered.
- **Skeptic's note.** Checked in the code. There are seven fixed phases (FrameProfiler.h:28-48). The fixed steps and the scene update run outside every phase (Application.cpp:766-812; in RuntimeLayer.cpp, m_Scene->OnUpdateRuntime at 478 comes before the first phase at 486). RV_PROFILE_PHASE is used only at the ten phase sites (grep). LiveRayGpuMs has no caller (FrameProfiler.cpp:296; grep), while RayGpuScope still times every ray pass. The controller it fed was deleted on purpose as dead code (HANDOFF.md:2562-2566: 'Renderer's whole frame-time controller (243 lines)'). The right fix is to delete LiveRayGpuMs and the stale comment at Application.cpp:802-806, not to wire it up again.

#### CORE-11 · Input is read before the frame waits for the GPU, time is kept in float seconds, and there is no frame pacing

- **Verdict:** confirmed. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-2, RT2-4, RT2-36 (Frame cap; the minimised loop; input order, time and pacing)
- **What is wrong.** Input is sampled, the simulation runs, and only then does the CPU wait for the GPU slot and the next swapchain image (up to a full refresh with vsync), so what gets drawn was decided on older input. Wall time is squeezed into float seconds since startup, whose resolution coarsens the longer the program runs. There is no pacing mode (a way to line frames up with the display to keep latency low) and no frame cap.
- **What it causes.** Input-to-screen latency includes that whole wait on top of the frames in flight. Float time resolution is 0.24 ms after 1 hour, 0.98 ms after 2.3 hours and about 2 ms after 4.5 hours. That jitter feeds frame deltas, fixed-step counts, the interpolation alpha, particles and auto-exposure (FrameDesc.DeltaSeconds) in long sessions. Uncapped IMMEDIATE present keeps the laptop GPU at full load; it may contribute to the roughly 1 ms drift between back-to-back runs, which is untested.
- **Measured?** Nothing measured. The float resolution figures are arithmetic.
- **Already recorded?** Not recorded.
- **Fix direction.** Wait for the frame slot first, then poll input, simulate and record. Keep time as 64-bit ticks or a double, and convert only deltas to float. Add a pacing mode (VK_KHR_present_wait or NV low-latency) and an optional frame cap for measurement runs. Measure input latency, and back-to-back benchmark drift, before and after.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/Application.cpp:766 - InputMap::Update and the fixed steps (766-779) run before BeginFrame's fence wait and swapchain acquire (788-792; VulkanDevice.cpp:1550-1553).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/EngineConfig.h:171 - Two frames in flight; packaged games ship with vsync on (165-169).
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanDevice.cpp:1251 - FIFO present mode with vsync on; IMMEDIATE (else MAILBOX) with it off (1251-1284). Neither the present-wait nor the low-latency extension is used.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/Application.cpp:724 - The clock is read as float seconds since start (724, 737-741) and m_LastTime is a float (Application.h:116). The source, glfwGetTime, returns a double (WindowsPlatform.h:10).
- **Skeptic's note.** Checked in the code. InputMap::Update and the fixed steps run before the in-flight fence wait and the swapchain acquire (Application.cpp:766-792; VulkanDevice.cpp:1550-1553). The clock is converted to float seconds and m_LastTime is a float (Application.cpp:724, 737-741; Application.h:116), although the source returns a double. Present mode is FIFO with vsync on, IMMEDIATE or MAILBOX with it off (VulkanDevice.cpp:1251-1284). Neither the present-wait nor the low-latency extension is used (grep). The float resolution arithmetic is correct: 0.24 ms after 2,048 s, 0.98 ms after 8,192 s, 1.95 ms after 16,384 s (about 4.55 hours). One addition in the same area: when the window is minimised the loop spins without waiting (glfwPollEvents, then continue; Application.cpp:747-756, WindowsWindow.cpp:37-41), which keeps one laptop core busy. The link to benchmark drift is speculation, and is labelled as such. Vsync off in developer builds is a deliberate setting (EngineConfig.h:165-170), so any frame cap must stay optional.

#### CORE-12 · Physics has hard body limits, a private thread pool, and a per-body sync that walks the hierarchy every frame

- **Verdict:** confirmed. **Severity:** medium. **Kind:** scalability. **Scope:** refactor.
- **Roadmap:** RT2-42, RT2-36
- **What is wrong.** Jolt (the physics library) is well integrated, but limits are fixed in code, its threads belong to it alone, the step blocks the frame, and syncing results back costs hierarchy walks and quaternion-to-Euler conversions for every body, every frame.
- **What it causes.** An open-world level has far more than 8,192 static colliders, and the build silently stops at the cap apart from one log line. Frame time includes the whole physics step. Sync cost grows with body count times hierarchy depth.
- **Measured?** Nothing measured.
- **Already recorded?** Not recorded (the choice of Jolt is recorded in ROADMAP.md:775).
- **Fix direction.** Take the limits from project settings. Run Jolt on the engine job system through a JPH::JobSystem implementation, and start the step as tasks that overlap recording of the previous frame. Sync through the transform system (CORE-07) with quaternions and the no-lock body interface. Replace per-step discovery of new bodies with spawn and destroy events. Measure with a many-body fixture.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Physics/PhysicsWorld.cpp:406 - The limits are compile-time constants: 8192 bodies, 8192 body pairs, 4096 contact constraints (406-411).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Physics/PhysicsWorld.cpp:769 - When the cap is reached, Build logs 'out of bodies' and stops adding the rest of the scene (769-770).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Physics/PhysicsWorld.cpp:388 - Each play session gets a private JobSystemThreadPool of hardware_concurrency()-1 threads. The unsigned '- 1u' wraps around if hardware_concurrency() returns 0, which the standard allows.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Physics/PhysicsWorld.cpp:890 - Update runs synchronously inside the fixed step, on the main thread.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Physics/PhysicsWorld.cpp:901 - The locking version of the body interface (431-432) is called per body, twice per step (881-903), while iterating an unordered_map.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Physics/PhysicsWorld.cpp:944 - Per body, per frame: a UUID hash lookup (926), a recursive walk up the parent chain, a matrix inverse, a Decompose and a conversion to Euler angles (940-951).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:1155 - Every step scans all rigid bodies for new ones, using a hash lookup plus a linear search of the destroy queue (1147-1162).
- **Skeptic's note.** Checked in the code. The maximum bodies (8192), body pairs (8192) and contact constraints (4096) are compile-time constants (PhysicsWorld.cpp:402-407). When the cap is reached, Build logs 'out of bodies' and stops (765-770). Each play session creates a JobSystemThreadPool with hardware_concurrency() minus one threads (380-388), from OnRuntimeStart (Scene.cpp:780). Step runs synchronously inside the fixed step (873-903), through the locking body interface (431-432), with two reads per body. SyncTransforms does, per body per frame: a UUID lookup, a recursive walk up the parents, a matrix inverse, a decompose and a conversion to Euler angles (917-953; Scene.cpp:281-298). Each step scans every body for new ones, with a linear std::find over the destroy queue (Scene.cpp:1147-1162). Small notes: the 23 threads are created on every Play even though the demo scenes have no rigid bodies. The '- 1u' wrap needs hardware_concurrency() to return 0, which does not happen on Windows. Nothing is measured, as the finding says.

#### CORE-13 · Each animated skinned mesh scans the whole asset registry twice per frame

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** performance. **Scope:** patch.
- **Roadmap:** RT2-42
- **What is wrong.** Skinned parts that name a derived handle (every part InstantiateModel creates) find their skeleton and clips by walking the whole asset registry: twice per animated part per frame, and once per draw for un-animated parts, which also allocate a bind pose each time. No demo scene triggers it today (camp's fox uses the model's own handle). It would grow with imported rigs times project size. Cache the owning handle once.
- **What it causes.** Main-thread cost is O(animated parts x assets in the project) every frame. It is small today with about 1,600 assets, but grows with both character count and project size, and at AAA project sizes it would dominate the frame (inferred, not measured).
- **Measured?** Nothing measured.
- **Already recorded?** Not recorded.
- **Fix direction.** Resolve the owner once: cache derived-to-owner handles in the asset manager, or store the skeleton handle in the AnimatorComponent at import or load. Reuse pose buffers kept in the component. Move sampling onto jobs once CORE-01 exists. Time it with a fixture of many animated characters.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:4790 - GetSkeleton (4790) and GetClips (4797) are called for every animator, every frame.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetManager.cpp:857 - In FindForModel, a model part's derived handle misses the skeleton map and falls back to OwningModel (849-869).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetManager.cpp:729 - OwningModel iterates Registry::All(), a std::map of every asset in the project, on every call (725-756).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetManager.cpp:833 - Imported model parts carry derived handles (model + 1 + index), so every skinned part takes the fallback path (831-848).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:5241 - A skinned mesh with no animator repeats the same lookup on every draw and allocates a bind-pose vector each time (5240-5245).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:4846 - Pose (a std::vector of bone transforms) is allocated per animator per frame, with two more during a cross-fade (4871, 4879).
- **Skeptic's note.** The mechanism is right. UpdateAnimators calls GetSkeleton and GetClips for every animator every frame (Scene.cpp:4790-4797). FindForModel falls back to OwningModel, which walks every entry of Registry::All() (AssetManager.cpp:725-756, 849-869). A skinned draw with no animator repeats the lookup and allocates a bind pose on every draw (5239-5245). Pose vectors are allocated per animator (4846-4879). The claim is overstated, though. Only a skinned part whose MeshComponent names a derived handle (model + 1 + index, which InstantiateModel writes for every part; AssetManager.cpp:2085) takes the fallback. Camp's fox, the only animator in the demo scenes, names the model's own handle (camp.rage:986 is fox.glb.meta's Handle, 11679010045657754579). It hits the cache directly and pays nothing. No shipped scene pays this today. A freshly imported multi-part rig would, and the cost grows with the number of rigs times the number of assets in the project. The fix is patch-sized.

#### CORE-14 · Prefabs cannot be spawned in a default packaged game

- **Verdict:** confirmed. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-4
- **What is wrong.** The only asset loader that reads the disk directly instead of going through the VFS is the prefab loader.
- **What it causes.** In a shipped (pak) build, every SpawnPrefab fails with 'Could not read prefab'. scenetest runs against loose files, so it cannot catch this.
- **Measured?** Not run; inferred from code.
- **Already recorded?** Not recorded.
- **Fix direction.** Read the prefab through IO::VFS::ReadText, and add a packaged-build check that spawns a prefab.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetManager.cpp:1723 - InstantiatePrefab opens the prefab with std::ifstream(path), bypassing the VFS (the layer that makes pak contents look like files).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Project/ProjectPackager.cpp:621 - The default build packs all content, prefabs included, into content.pak, which is mounted over content/ (621-626, 641-690). No loose files are shipped.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetRegistry.cpp:138 - Other loaders read through IO::VFS (ReadText here) and therefore work from a pak.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Managed/Interop.cpp:360 - C# SpawnPrefab goes through InstantiatePrefab, and so does the C++ path (ScriptableEntity.cpp:105).
- **Skeptic's note.** Checked in the code. InstantiatePrefab opens the prefab with std::ifstream (AssetManager.cpp:1717-1727). It is the only asset loader that bypasses IO::VFS; the other ifstream uses are inside the pak and VFS code, the loose-file hash, the config reader and the shader cache. The default package is content.pak, with no loose content unless LooseContent is chosen (ProjectPackager.cpp:614-690). Only unselected .rage files are left out (501-508), so prefabs are packed. Both C# SpawnPrefab (Interop.cpp:348-361) and the C++ path (ScriptableEntity.cpp:105) go through InstantiatePrefab. Inferred from code, not run, as the finding says.

#### CORE-15 · Engine configuration has grown to 116 command-line switches that the renderer reads directly

- **Verdict:** confirmed. **Severity:** medium. **Kind:** tech-debt. **Scope:** refactor.
- **Roadmap:** RT2-17, RT2-1
- **What is wrong.** Every experiment leaves a switch behind in one global struct, and the renderer reads those switches directly and deep in its code. They are never resolved once into the settings for a view. Some of the documentation around them is stale.
- **What it causes.** Any renderer rewrite has to carry, or decide about, around a hundred switches. Because the overrides are process-wide, two views cannot differ, and there are hidden behaviour variants. Stale flag text misleads anyone reading it: this audit's brief listed gpu-lit as off.
- **Measured?** Not applicable.
- **Already recorded?** The owner rule (one global setting plus one measurement flag per lever) is recorded. The pile-up is not.
- **Fix direction.** Add one resolve step that applies the overrides onto the project's RenderSettings and produces a per-view resolved settings struct, and have the renderer read only that. Sort each switch into 'keep as the lever's one measurement flag' or 'park on a branch', the way RT-10 was handled, and fix the gpu-lit text. Prove the resolve step changes no pixel (per-pixel diff, zero).
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/EngineConfig.cpp:152 - The parser has 116 'if (key == ...)' branches. EngineConfig.h has about 149 fields and 20 Has*Override pairs (counted).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Renderer3D.cpp:7045 - The renderer reads raw switches from the process-wide config inside its passes: 54 reads in Renderer3D.cpp and 22 in FrameGraphBuilder.cpp, for example 7045-7071 and 7290-7410.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/EngineConfig.h:51 - The usage text says --gpu-lit is 'UNFINISHED, off by default'.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/EngineConfig.h:990 - The actual default is true, with a comment saying it flickers on mixed scenes. HANDOFF.md:6549-6556 records the defect fixed on 2026-08-24.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/EngineConfig.cpp:466 - Experiment arms stay as permanent switches (for example reflection-follow-hit, reflection-moving-layer and reflection-noise-blur at 466-480, and water-ablate at 520-537). RT-10's arms, by contrast, were parked on a branch (RT-SERIES.md:61).
- **Skeptic's note.** Checked in the code. EngineConfig.cpp has 116 'if (key ==' branches. Renderer3D.cpp reads EngineConfig::Get() 54 times and FrameGraphBuilder.cpp 22 times. The usage text calls --gpu-lit 'UNFINISHED, off by default' (EngineConfig.h:51-53), but the default is true (990), and HANDOFF.md:6549-6556 records the defect as fixed on 2026-08-24. The field comment at 985-989 also still says it flickers on mixed scenes. Experiment arms remain as switches (EngineConfig.cpp:464-486 and from 520). The brief's own statement that gpu-lit is unfinished and off is wrong for the same reason.

#### CORE-16 · Scenes are single YAML files loaded whole; there is no world partition and no runtime level loading

- **Verdict:** confirmed. **Severity:** medium. **Kind:** scalability. **Scope:** rewrite.
- **Roadmap:** RT2-40
- **What is wrong.** The only scene unit is the whole level as one text file. There are no streamable cells (world partition), no cooked binary scene format, and the editor's play/stop cycle round-trips everything through text.
- **What it causes.** Load time and memory scale with the whole level, so open worlds are impossible. Play and Stop cost grows with scene size (seconds at 100k entities, inferred).
- **Measured?** Nothing measured.
- **Already recorded?** Play-as-serialisation is recorded as a design (ENGINE-NOTES section 2). Open world is recorded only as future (NEXT.md:380).
- **Fix direction.** Cook scenes into binary chunks (cells) that the CORE-04 streaming system loads around the camera, keeping YAML as the editor's source format. Add a runtime API to load and unload cells and levels. Make the play-mode snapshot a binary registry copy. Give the ECS per-component change versions and a parallel-for over its dense arrays. Measure load time and play/stop time at 10k, 100k and 1M entities.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/SceneSerializer.cpp:342 - A scene is one YAML document, parsed whole.
  - C:/Users/ism19/Code/RageV/RageVRuntime/src/RuntimeLayer.cpp:144 - One start scene per process; nothing loads or unloads another.
  - C:/Users/ism19/Code/RageV/RageVEditor/src/EditorLayer.cpp:1375 - Play serialises the whole scene to YAML text, and Stop parses it back (1411-1413).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/ECS.h:497 - Multi-component views check membership in the other pools for every entity (497-506), and Destroy visits every pool (556-571). There are no change versions and no parallel-iteration API.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Components.h:99 - Component layouts are shared at the binary level with compiled game modules, so any layout change needs every module rebuilt (99-103).
  - C:/Users/ism19/Code/RageV/docs/NEXT.md:380 - Open-world support is recorded only as a future trigger: 'revisit bricks when an open world needs one'.
- **Skeptic's note.** Checked in the code. A scene is one YAML document, parsed whole (SceneSerializer.cpp:337-342). Each process loads one start scene (RuntimeLayer.cpp:137-150). Play serialises the scene to a string and Stop parses it back (EditorLayer.cpp:1375-1376, 1411-1413). A multi-component ECS view checks membership in the other pools, led by the smallest pool (ECS.h:345-377, 497-506), and Destroy visits every pool (556-571). Component layouts are shared with native modules (Components.h:99-103). Sizes for scale, from a read-only listing: scale_120000.rage is 53.7 MB of YAML and scale_60000.rage is 26.8 MB. Load and Play/Stop times are unmeasured. One misattribution: NEXT.md:378-380 is about sparse brick volumes for GI ('revisit bricks when an open world needs one'), not world partition or streaming. Those are recorded nowhere.

#### CORE-17 · C# component access goes through text on every call

- **Verdict:** partly confirmed (the problem text is the skeptic's corrected version). **Severity:** low. **Kind:** performance. **Scope:** refactor.
- **Roadmap:** RT2-42 (Only if a measurement shows a cost)
- **What is wrong.** Generic C# component field access (GetComponentField/SetComponentField) covers lights, cameras and every component without its own entry point. It goes through a linear name search and text formatting and parsing on each call. Transforms, forces, velocity, parenting and raycasts already cross the boundary as plain structs. This is negligible at the sample's script counts. Typed accessors for more components are worth building only if a script-heavy fixture shows the cost.
- **What it causes.** A few microseconds per component access. That is fine for the sample's handful of scripts and significant at AAA gameplay volumes.
- **Measured?** Nothing measured.
- **Already recorded?** The text choice is recorded for script fields (Interop.h:400-406). Its use for runtime component access is not discussed.
- **Fix direction.** Generate typed, blittable accessors (plain structs that cross the boundary without conversion) for the hot components (transform, rigid body, light, camera) from the ComponentRegistry. Keep the text bridge for tooling, and batch per-step calls. Measure with a script-heavy fixture.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Managed/Interop.cpp:604 - ResolveComponent costs a UUID hash lookup plus ComponentRegistry::Find on every call (604-618).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/ComponentRegistry.cpp:1618 - Find is a linear scan comparing against a std::string built from the caller's const char* (1615-1624).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Managed/Interop.cpp:826 - Every Get formats the value into text and every Set parses text back (664-802, 814-858).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:1010 - One native-to-managed call per script per step (InvokeTick) and per frame (1063), with handle vectors rebuilt on each pass (956-958, 1053-1055).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Managed/Interop.h:403 - Text was chosen for script fields because they change 'when somebody types, not per step'. The runtime component bridge reuses the same text form for per-frame access.
- **Skeptic's note.** The text bridge is real. Interop.cpp:604-618 resolves a component by name through ComponentRegistry::Find, a linear scan (ComponentRegistry.cpp:1615-1624). Get/SetComponentField format and parse text on every call (Interop.cpp:814-858). Scripts run one after another on the main thread. But the hot components already have typed entry points that pass plain structs without conversion ('blittable'): Get/SetPosition, Rotation and Scale, world position and axes, AddForce, AddImpulse, Set/GetLinearVelocity, LookAt, parenting and raycasts (Interop.cpp:101-364). C#'s Transform.Position uses them (RageVScriptCore/src/Engine.cs:241). Transform and rigid-body access therefore do not go through text; only generic field access does (lights, cameras, everything else). The demo scenes run 2 to 4 managed scripts. Nothing is measured.

#### core-s1 · Two per-frame GPU inputs are rewritten in one shared buffer while the previous frame may still read them, and the RHI has no per-frame guard

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-4
- **What is wrong.** With two frames in flight, any buffer the CPU rewrites every frame needs one copy per frame in flight, or frame N's GPU work can read frame N+1's values. The RHI leaves this to each subsystem, because host-visible Upload is a raw memcpy into a single mapping. Most subsystems build their own ring. Two do not: the GPU cull tables (CORE-02), and each GPU particle emitter's params and sort params (spawn count, spawn cursor, emitter matrix, time, and the camera used for sorting).
- **What it causes.** Inferred, not observed. A particle frame could be simulated with the next frame's spawn count, matrix and time, giving doubled or skipped spawns or an emitter one frame ahead. Culling could decide visibility on the next frame's bounds and slot layout. Neither the engine nor the usual validation run reports this kind of CPU-write/GPU-read race. It also becomes more likely as the CPU gets faster, because the write lands sooner after the fence wait, and a faster CPU is exactly what CORE-01, 02 and 07 aim for. GPU emitters appear only in particles_gpu.rage and particles_curves_gpu.rage today. The cull tables are used in every scene.
- **Measured?** Not measured; established from code.
- **Already recorded?** The cull-table instance is in this audit's CORE-02. The particle instance and the missing RHI-level guard are not recorded anywhere.
- **Fix direction.** Patch now: give GpuCull's tables and GpuParticles' Params and SortParams one buffer per frame in flight, the way Material and Renderer3D's scene slots already do. Then, together with CORE-03's upload ring, have the RHI hand out per-frame temporary allocations for CPU-written data. In debug builds, make an Upload into a permanent buffer that a still-running frame has bound an error, so this class of bug cannot return. Prove it with a per-pixel diff on the particle scenes, and on the garage with --gpu-cull on and off.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/Platform/Vulkan/VulkanResources.cpp:191 - Upload on a CPU-writable (host-visible) buffer is a plain memcpy into its one permanent mapping. Nothing keeps a copy per frame in flight, and nothing checks for a GPU read still pending.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Particles/GpuParticles.cpp:210 - Each GPU emitter owns a single host-visible params uniform buffer (205-210), and alpha emitters a single sort-params buffer (222-227).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Particles/GpuParticles.cpp:545 - Params are rewritten every frame in Simulate (545) and sort params at 689. The comment at 547-551 applies the one-per-frame-in-flight rule to the descriptor set only, not to the buffer behind it.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/GpuCull.cpp:98 - The cull tables: one per pass, not one per frame. This is the case CORE-02 found.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/Material.cpp:46 - Elsewhere the per-frame copies are built by hand in each subsystem: materials here, Renderer3D's scene slots (Renderer3D.cpp:1378), VoxelGI, AutoExposure, DebugRenderer, UI and particle batches.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/EngineConfig.h:171 - FramesInFlight = 2: the CPU records frame N+1 while the GPU may still be executing frame N.

#### core-s2 · An object spawned during a frame is drawn with an identity previous transform, so its first frame carries a false motion vector from the world origin

- **Verdict:** added by the skeptic. **Severity:** low. **Kind:** correctness. **Scope:** patch.
- **Roadmap:** RT2-4
- **What is wrong.** Motion vectors (how far each pixel moved since the last frame) come from the difference between World and PreviousWorld. Only the once-a-frame snapshot ever sets PreviousWorld, so a transform created after the snapshot keeps the default identity matrix. An entity spawned by a script (SpawnPrefab, Spawn, InstantiateModel) is therefore drawn on its first frame as if it had flown in from the world origin at unit scale.
- **What it causes.** Every spawned object feeds one frame of false motion into TAA's velocity, the reflection accumulator's object reprojection (RT-15), RT-17's moving test and the moving-light marking. On screen this is likely a one-frame smear or flash wherever the history lookup lands, limited by the rejection tests. No demo scene spawns at runtime, so nothing shows today; every gameplay spawn would.
- **Measured?** Not measured; established from code.
- **Already recorded?** Not recorded. The skinning path's version of the same rule is (Scene.cpp:4893-4898).
- **Fix direction.** In PropagateTransform, when CacheValid is false (the first time a World is derived), also set PreviousWorld = World. That is one line. Add a scenetest claim: spawn an entity in OnTick and assert that its first-frame PreviousWorld equals World.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Components.h:97 - PreviousWorld defaults to the identity matrix.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:833 - AdvanceMotionHistory is the only code that writes PreviousWorld (grep of RageV/src).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:1125 - The snapshot runs once, before the frame's first OnTick. Anything a tick or frame script spawns after it keeps the identity for the frame it is first drawn in.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Asset/AssetManager.cpp:1747 - InstantiatePrefab derives the new entity's World at once; PreviousWorld stays identity.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:5145 - The lit instance row takes PreviousWorld as it is (SetSceneInstance, which stores it as PreviousModel at Renderer3D.cpp:9094).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Renderer/RayShadows.cpp:303 - The ray-instance record takes it as it is and marks the object as having moved (304). RT-15's object reprojection and RT-17's moving tests read that mark.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:4893 - The skinned path handles the same case on purpose: a first pose stands in for the previous one, so it adds no motion.

#### core-s3 · ECS handles cap a world at about a million entities, and past the cap a Release build corrupts data silently

- **Verdict:** added by the skeptic. **Severity:** low. **Kind:** scalability. **Scope:** patch.
- **Roadmap:** RT2-42
- **What is wrong.** A scene can hold at most 1,048,575 live entities. A handle kept after its entity is destroyed becomes valid again once its slot has been reused 4,096 times. Neither limit is enforced in Release builds.
- **What it causes.** Nothing today. A world built at AAA scale from entities (props, foliage, projectiles) can reach the cap, and past it a Release build makes the next entity alias entity 0: silent data corruption instead of an error. The version wrap lets a gameplay reference to a destroyed object silently address a new one after a few minutes of heavy spawn churn (inferred).
- **Measured?** Not measured; arithmetic from the handle layout.
- **Already recorded?** The limits are stated in ECS.h:44-56 as far off. They are in no roadmap, and the Release behaviour past the cap is not recorded.
- **Fix direction.** Widen the handle (for example to 64 bits: 32 of index, 32 of version). This changes the Entity layout that native modules compile against (ENGINE-NOTES 7by), so it needs the same module-rebuild step as any component layout change. At minimum, make the cap a fatal error in Release with a clear message. Decide this before building any fixture above a million entities, which CORE-16's 1M-entity measurement would need.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/ECS.h:59 - An entity handle is 20 bits of index and 12 bits of version in one 32-bit word. The rationale at 44-56 says both limits are far beyond what the engine has run (largest fixture: 120k).
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/ECS.h:535 - The only guard on the cap is RV_CORE_ASSERT.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Core/Core.h:19 - Assertions exist only when RV_DEBUG is defined (19-29), so Release has no check.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/ECS.h:70 - MakeEntity masks the index, so entity number 1,048,576 gets index 0, which is a live entity's handle.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/ECS.h:528 - The free list is last-in first-out (m_Free.back()), so spawn/destroy churn reuses the same slot. The version wraps after 4,096 reuses (Destroy, about line 568).

#### core-s4 · The CPU evidence behind the engine-core rewrite predates RT-first and comes from a fixture that looks nothing like content

- **Verdict:** added by the skeptic. **Severity:** medium. **Kind:** missing-capability. **Scope:** patch.
- **Roadmap:** RT2-2
- **What is wrong.** CORE-01, 02 and 07, and the brief's 'known facts', rest on one CPU profile. It was taken before the RT-first frame existed, on a scene with no hierarchy, four meshes, no textures, no emissive fittings and nothing moving. The per-object CPU work the RT frame depends on is therefore unmeasured at scale: per-child lookups in the transform walk, the linear slot scan across many meshes, emitter extraction, the per-view ray-instance list with material resolution, the moving-object paths (the TLAS moving mask, moving-light marking, the motion history) and the G-buffer pass's submission.
- **What it causes.** Under the owner's rule that nothing is adopted without measurement, the CPU half of RT-series 2 cannot be sized or ordered. It could rebuild the wrong thing first, as happened before 7bx when the walk turned out to be the cost and culling was not. The brief already carries one stale fact ('the transform walk is the wall'; after the compare fix it is 2.6 ms against 12.8 ms of lit walk and submission).
- **Measured?** The fixture's content is read from the generator. That the RT-first frame's CPU cost at scale is unmeasured comes from the absence of any later scale entry in the docs.
- **Already recorded?** The fixture's purpose is recorded in its own docstring. Its gaps, and the absence of a post-RT-first re-measure, are not.
- **Fix direction.** Make this step zero of the engine-core track. Add scale fixtures that resemble content: imported hierarchical models (a car of 50 to 150 parts), hundreds of distinct meshes and textured materials, emissive fittings, a share of moving objects, and ray tracing on at the project's settings. Generate them at 20k, 60k and 120k with make_scale_scenes.py. Record CPU phases (using CORE-10's zones) and GPU time as A,B,B,A palindromes. Re-run the old four-mesh set as well, to see how far the RT-first frame has moved the old curve.
- **Evidence:**
  - C:/Users/ism19/Code/RageV/docs/HANDOFF.md:7015 - The 1k to 120k curve and the 60k breakdown (7019-7029, 7116-7121) were taken on 2026-08-22. grep finds no later scale measurement in HANDOFF, ENGINE-NOTES, RT-SERIES or RT-FIRST.
  - C:/Users/ism19/Code/RageV/docs/RT-FIRST.md:38 - The G-buffer pass landed on 2026-09-06, and every RT-series pass after it. None of them existed when the scale curve was measured.
  - C:/Users/ism19/Code/RageV/tools/scripts/make_scale_scenes.py:9 - The fixture uses 'the same four meshes ... the same sun with the same cascades' (9-11): four primitive handles (22-29), every object Static: true with an inline, untextured, non-emissive material (46-52), and no parents written anywhere.
  - C:/Users/ism19/Code/RageV/RageV/src/RageV/Scene/Scene.cpp:2016 - The code's own note: the scale scenes have four distinct meshes, while the showroom has 155 across 185 objects, so the slot scan's cost differs by orders of magnitude between fixture and content ('at 60k objects this is milliseconds').

## 2. The finding the skeptics rejected

#### frame-14 · The target pool matches by format, not size, so targets are reallocated whenever the camera starts or stops

**Verdict:** refuted. The premise is false. The measured-change re-light and filter targets do not exist only on still frames. RecordIsLastFrame (FrameGraphBuilder.cpp:22-43) compares the record's camera with last frame's camera, and the record is retaken and stamped with the camera on every frame the camera moves (Renderer3D.cpp:8205-8209, 8290-8295). So the test is true on moving frames too; the file's own header says 'every frame the passes ran the frame before, moving or not' (line 29). The only passes that depend on motion (ReflectionBudget, GlassReflectionBudget, DirectBudget) write imported history targets, not pooled ones. So the list of pooled targets is the same moving or parked, and nothing is reallocated when the camera starts or stops. What is true: the pool matches entries by format class, resizes them in place (RenderGraph.cpp:12-25, 290-311) and never evicts. So a change in the pass list, such as a feature toggle or a scene with water, resizes later targets of the same class and leaves unused entries allocated. Old images are destroyed later, once frames in flight are done with them (VulkanResources.cpp:860-872), so the cost is a one-off hitch when a setting changes. Low.

## 3. Open issues and the owner's standing rules

The inventory was taken from HANDOFF.md, RT-SERIES.md, NEXT.md, RT-FIRST.md, RENDERING-REVAMP.md, ROADMAP.md, BAKING-ROADMAP.md, TEXEL-EMITTERS.md, ENGINE-NOTES.md and the code's own markers, on 2026-09-24. The fate of each is the roadmap's section 3.

#### RT-24 · Fast camera spin: ghosts and a spreading blur behind reflections on the shiny floor

- **Status on 2026-09-24:** in-progress. **Area:** Reflections and temporal filtering (lit-shader composite, reflection accumulator, TAA).
- **Fate in the roadmap:** Fixed directly, RT2-0 (structure: RT2-23, RT2-28). See the table above
- **What the owner sees.** A fast swing of the camera in the garage leaves ghosts behind reflected objects on the wet floor (the poles' angled lines, the bumper outline, a car afterimage) and a blur that spreads. Reflections take a fraction of a second to settle after the camera stops, the car's shine settles late, and the ceiling tube lights' reflections are very noisy while moving.
- **Known cause.** Three layers, found by switching one thing at a time on a headless 15-degree swing test with 16 reflection rays per texel (so grain cannot hide ghosts). (1) The lit shader trusted the traced reflection according to its age and filled the rest from the baked reflection probe. A turning camera makes every shiny pixel young, so the floor flipped to the probe, which is about a third brighter. Fix 1 is built: trust is now all-or-nothing, set by whether a traced picture exists. (2) The reflection accumulator (the pass that averages each reflection texel over past frames) always kept at least four frames, however far the picture had moved, so every frame was three-quarters a misplaced old picture. Fix 2 is built: that minimum shrinks with how far the picture moved (pole score 17.1 -> 8.0; memory off gives 7.8). (3) The spread that remains is taa_resolve (TAA, the frame filter that blends each frame with earlier ones) under motion: switching its memory off makes the floor as sharp as the settled picture. Its three rules are not yet bisected: the reflection-motion lane it lines old frames up with (RT-6.1), its colour box, and its moving feedback of 0.9 (about ten frames). Lead for the slow settle: RT-9's extra rays switch off the instant nothing moves, which is exactly when pixels are rebuilding. The tube noise comes from reflections now keeping few frames, by design.
- **The owner's decisions about it.** The owner's highest priority, before anything else (2026-09-23). Commit only once RT-24 is fixed completely. Owner on fix 1: 'smear pretty much gone'. On fix 2: ghosting 'less, but it exists', now 'more a blur that spreads'. Next step as instructed: bisect taa_resolve's three rules one at a time on the clean test and report before changing anything. Live checks use watch_arm.py spin 0 with no time limit. Measured inert, do not retry: the alpha-scaled turn rule, refusing the fallback past the lobe, the moving-memory floor, surface motion for TAA, the young blur, the firefly clamp, kSettledBound, the direct light's memory. A partial effect only from this frame's image distance and the accumulator's kTemporalSigma set to 0 (owner: 'can't tell').
- **Source.** docs/HANDOFF.md 'RT-24 in progress' (lines 8-68); docs/RT-SERIES.md RT-24 row (line 77); memory project_ragev_rt24_spin_smear.md. Working tree, uncommitted: include/pbr_fragment.glsl lines 6082 and 6195 (fix 1), reflection_accumulate.rvshader ~1664-1680 (fix 2), SampleProject/Source/Orbiter.cpp, tools/scripts/garage/spin_measure.py, ghost_map.py, watch_arm.py

#### RT-22 · A moving light's lighting trails behind it

- **Status on 2026-09-24:** open. **Area:** Temporal filtering of direct light, reflections and glass.
- **Fate in the roadmap:** Split, RT2-23 (the reflection third), RT2-24 (the direct history), RT2-31 (the glass pane's lamp light gets measured change). Each remaining part lives in a different history; `emitter_lag.py` gates each one
- **What the owner sees.** A Realtime point light carried across the garage with a glowing cube: its pool on the floor and its glow on the car lag behind it for a few frames. As filed, 11.9% of the floor beside the car was off by more than 16 levels against the settled pose. With the tubes as real line lights it reads 19.5%, because a 3 m tube throws a wide soft shadow of the moving cube. The car-window streak part is fixed.
- **Known cause.** Measured change removes two thirds of the lag. (Measured change is the engine's anti-lag: it re-shades a sparse sample with last frame's random numbers, so any difference is real change, and shortens memories there.) Of the rest, the traced reflections hold about a third and the direct light's history holds the remainder. The window streak was TAA's temporal floor: its colour box was widened by the pixel's own recent swing, and a 0.98 still memory then kept the stale glow. The floor now applies only at outlines and never under a see-through surface (b30d5cd). The pane's reflection now has measured change and RT-9's ray plan, and its aimed ray uses the power heuristic. The glass pane's own lamp light is still never re-lit by measured change.
- **The owner's decisions about it.** The window streak is fixed by the owner's eye, and the window grain is 'good enough'. It was committed on the owner's word despite the water regression (ISS-1). Parked by the owner alongside it: the general flicker rule (ISS-2), the delayed reflection stop (ISS-3) and the cable flicker (ISS-4). A water exemption was rejected. A glowing object lighting nothing on its own is by design ('that's how our tube lights work').
- **Source.** docs/RT-SERIES.md RT-22 row (line 76); docs/HANDOFF.md '2026-09-23: RT-22's streak' (lines 70-131) and the RT-22 notes (lines 211-243); commits b30d5cd, 6017443

#### ISS-1 · Bridge water glitter blinks more since RT-22's frame-filter rule

- **Status on 2026-09-24:** regressed. **Area:** TAA (taa_resolve), transparent pass, water.
- **Fate in the roadmap:** Fixed directly, RT2-8. The behaviour-based floor replaces the rule keyed on "covered" pixels, so the sea gets its floor back with no water exception. It lands in M1, right after RT-24, not at the end of the series
- **What the owner sees.** On the bridge (parked camera, clock pinned, 16 frames from frame 240, check_glint_flicker.py) the share of blinking pixels rose from 0.80% to 0.97% of the frame. Every new blink is lamp glitter on the sea.
- **Known cause.** RT-22's rule skips TAA's temporal floor, and the x8 widening on rough surfaces, wherever the OIT revealage says a see-through surface covers the pixel. (OIT is order-independent transparency; revealage is how much of the background still shows.) The sea is drawn by the transparent pass, so it counts as covered and lost the floor that kept its glitter steady. The rule is decided by where a pixel is, not by how it behaves. Underneath that: the transparent pass writes no velocity, material or identity lanes.
- **The owner's decisions about it.** A water exemption was rejected as 'a BAD FIX -- cables can exist in an area without water too' and removed. Rule: never carve out a surface type; go back to the mechanism. The fix is the general rule in ISS-2, which the owner parked.
- **Source.** docs/HANDOFF.md header (lines 3-6) and 'THE REGRESSION' (lines 87-91); docs/RT-SERIES.md RT-22 row; taa_resolve.rvshader lines 1069-1070 (the covered and outline tests); commit b30d5cd

#### ISS-2 · The general 'how a pixel changes' rule for TAA's flicker floor is not built

- **Status on 2026-09-24:** parked. **Area:** TAA (taa_resolve).
- **Fate in the roadmap:** Fixed directly, RT2-8. Built as the owner's parked proposal; it needs nothing from the later rewrites
- **What the owner sees.** Two rules based on where a pixel is (floor only at outlines, no floor under glass) stand in for one based on behaviour. They cause ISS-1 and cannot tell coverage flicker from real change.
- **Known cause.** Coverage flicker (edges and sub-pixel detail) swings back and forth on the jitter's 8-frame cycle; a real change moves one way and stays. The filter keeps no per-pixel information that could tell the two apart.
- **The owner's decisions about it.** Proposed, then parked by the owner on 2026-09-23 'for later', after RT-24. Direction: keep per pixel which side of the history the input fell on and for how many frames running. Open the floor only while the input keeps swinging; close it once it has stayed on one side for a jitter cycle. This replaces both rules and needs no water rule. Test on all four: the window drive, the car's stop, the bridge cables and the bridge water.
- **Source.** docs/HANDOFF.md '2026-09-23': 'The general rule to build' (lines 93-99) and 'Parked by the owner' (lines 118-125); memory project_ragev_rt_series_state.md

#### ISS-3 · A reflection keeps moving a fraction of a second after the object stops

- **Status on 2026-09-24:** parked. **Area:** Reflection accumulator.
- **Fate in the roadmap:** Absorbed, RT2-23 (RT2-0 fixes the one-frame motion tail). The new denoiser checks what the ray struck whenever anything moves, instead of closing its gates two frames after a stop
- **What the owner sees.** When the car (or the moving cube) stops, its reflection on the floor stops a fraction of a second later.
- **Known cause.** Narrowed to the reflection accumulator's own memory after the stop: with --reflection-history=off it stops at once. Its 'something moved' gates (RT-17's identity test, RT-9's allocation, the moving blur) close within two frames of the stop. AnyInstanceMoved() also stays true one frame past the stop (RayAnyMoving || RayAnyMovingLast). Candidate fix: refuse reflection memory where the ray struck a mover, now that a refused texel gets up to 4 rays (--reflection-moving-rays). Moving the memory along with the object (RT-15's layer) measured worse and stays rejected.
- **The owner's decisions about it.** Parked by the owner on 2026-09-23, for after RT-24. Related to RT-24's slow settle.
- **Source.** docs/HANDOFF.md lines 118-122 and 228-235; docs/RT-SERIES.md RT-23 timing note (line 73); Renderer3D.cpp:7228

#### ISS-4 · Bridge cables, tower and thin deck members blink under TAA

- **Status on 2026-09-24:** parked. **Area:** TAA and sub-pixel geometry (bridge).
- **Fate in the roadmap:** Stays parked, (direction: RT2-38). Not a ray-tracing defect: 0.00-0.01% with no AA or MSAA. The owner's look rules forbid fading the key members. RT2-29 must be checked on the cable band
- **What the owner sees.** Parked on the bridge's Headland camera, the tower and cables blink under TAA. Final 2026-09-02 figures, as shares of each region's pixels blinking: tower 34.5% -> ~28%, deck 32.6% -> 31.8%, cables 1.9% -> 0.7% (the last with the still-feedback arm). The same view reads 0.00-0.01% under no AA or MSAA.
- **Known cause.** Jitter plus the resolve acting on geometry thinner than a pixel (ropes, posts, pickets, truss webs at about 0.1 px), plus bright lamp lenses. Ray tracing contributes nothing when parked. Neither version of RT-22's filter rule caused or fixed it. The sub-pixel members need a distance fade or LODs, not a change to the resolve.
- **The owner's decisions about it.** Parked on 2026-09-23. Standing look decisions: never fade the suspender ropes, rails, posts, lamp shafts or truss webs (only pickets, lamp arms, brackets and band flanges under 0.2 m fade, at 300-450 m); never boost still-pixel feedback on the bridge; keep lamp glow at 0.02 unless asked. The owner values the look over the residual TAA flicker.
- **Source.** docs/HANDOFF.md lines 123-124; memory project_ragev_flicker_bridge.md; docs/RENDERING-REVAMP.md WR-13 (line 1221)

#### RT-2.2 · No deferred resolve: opaque surfaces are rasterised and their materials sampled twice

- **Status on 2026-09-24:** deferred. **Area:** Frame architecture (G-buffer and lit pass).
- **Fate in the roadmap:** Fixed directly, RT2-22, step 1. Built exactly as filed, with the albedo lane moved to sRGB8 first. Then, per D2, one compute composition replaces the second scene draw in RT mode
- **What the owner sees.** The G-buffer pass and the forward lit pass both rasterise the scene and both sample every material. Reflections, GI, AO and direct light are traced in their own passes but combined inside the 6,500-line forward lit shader.
- **Known cause.** The engine is 'forward+ with a G-buffer prepass' (owner's answer, 2026-09-06). RT-2.2 would have the lit pass read albedo, normal, roughness, metallic, specular and occlusion from the G-buffer instead of sampling the material again. It is worth about 0.6 ms at Headland, more where heavy materials sit near the camera. Precondition: the albedo lane is R8G8B8A8_UNORM linear and must become sRGB8 or 16F first, or dark tones band. RT-14 measured the G-buffer and advised against packing it, while RT-2.2 wants the albedo lane wider, so the two are decided together.
- **The owner's decisions about it.** Owner-filed on 2026-09-07 for the very end of RT-series 1. On 2026-09-06 the owner ruled full deferred shading off the table ('no RT gain for a material-path rewrite'). RT-series 2's permission to rewrite major renderer parts reopens that question, and it should be put to the owner explicitly.
- **Source.** docs/RT-SERIES.md RT-2.2 rows (lines 41, 90, 129); docs/RT-FIRST.md §4 answer 1; FrameGraphBuilder.cpp:74-77 (kAlbedoFormat, 'eight bits linear for now')

#### ISS-5 · Lights seen at traced ray hits are lit as points, even tubes and sized lamps

- **Status on 2026-09-24:** open. **Area:** Hit shading for reflections and GI (include/pbr_fragment.glsl).
- **Fate in the roadmap:** Fixed directly, RT2-10. The one light library shades tubes and spheres as areas at every hit
- **What the owner sees.** Inside reflection and bounce rays, a 3 m tube or a lamp with a radius lights the hit as a pinpoint with a hard shadow. So polished chrome (poles at roughness 0.055, car paint at 0.18) seen in the floor shows pinpoint highlights where the tube on screen draws a streak.
- **Known cause.** The light walk that shades a hit (the one-light reservoir loop before ShadeTraced) reads neither the light's radius (Direction.w) nor its length (Extent.x), and it traces TraceShadowFrom, a hard point-shadow ray. Only the raster loop and direct_trace.rvshader (NearestOnLamp, TraceShadowTubeFromMasked) handle sized lights. A known companion defect: the analytic capsule highlight is 1.5-3x too bright at roughness 0.2-0.6.
- **The owner's decisions about it.** Owner rule (2026-09-22): the tubes ARE line lights (3.07 m, radius 0.075). Any speckle or light fix 'should cover everything', never one light type, so the fix goes at the hit-shading and estimator level. RT-7's option B (the lamp, not the rays, draws the tube's reflection) was rejected and rolled back on 2026-09-14. Its stated blocker, the GGX highlight cap, was fixed by RT-21, so the analytic-streak route is a candidate again, but it must be measured before it is proposed.
- **Source.** memory project_ragev_tube_lights_never_in_scene.md; pbr_fragment.glsl lines 2839-3160 (TraceShadowFrom at 2984, 3093, 3155); direct_trace.rvshader lines 314-521

#### ISS-6 · GPU-driven lit path: marked unfinished, and the docs and code disagree about its state

- **Status on 2026-09-24:** open. **Area:** GPU-driven rendering (GpuCull and the lit pass).
- **Fate in the roadmap:** Fixed directly, RT2-1 (text and a parity check with rays on), RT2-37 (replaced by compacted draws). 
- **What the owner sees.** The --gpu-lit help text says 'UNFINISHED, off by default ... wrong and flickering on mixed scenes'. The code default is GpuLit = true, with a comment saying it stays on so the defect is in view. ROADMAP 8.3 says done except meshlets. The 2026-08-24 hand-off says the mixed-scene disagreement was two CPU-side bugs, both fixed, with all four submission paths bit-identical.
- **Known cause.** Stale or contradictory records; which is true today is not established. Known limits: the GPU lit path engages only under bindless (never on OpenGL), skinned meshes take the CPU path, and the indirect path ignores --depth-sort. On static scenes it took 60,000 objects from 55 to 73 FPS.
- **The owner's decisions about it.** No owner decision recorded beyond fixing the 2026-08-24 defects. Needs checking under the G-buffer split and the RT passes before the roadmap leans on it.
- **Source.** EngineConfig.h:51 (UNFINISHED help text) and :984-990 (GpuLit = true); GpuCull.cpp:429-433; docs/ROADMAP.md 8.3; docs/HANDOFF.md 'the gpu-lit defect is dead' (line 6549); memory project_ragev_gpulit_defect.md

#### ISS-7 · Two half-float histories still round toward zero (water_foam, irradiance_fill)

- **Status on 2026-09-24:** open. **Area:** Temporal histories and baking.
- **Fate in the roadmap:** Fixed directly, RT2-6, arm 8. Recorded as the owner's call; shown as an arm
- **What the owner sees.** Running averages stored in half-float targets settle about memory/2 steps too dark (1.6-3.1% of the value) wherever the input moves. It is invisible on still references.
- **Known cause.** This GPU rounds float-to-RGBA16F writes toward zero (in 99.9% of 1.3 M texel-frames measured). include/half_float.glsl fixes it: StoreAsHalf with a dither, or StoreAsHalfNearest where a threshold reads the value. That fix went into five histories (0de7d38). Still unfixed: water_foam (rg16f) and irradiance_fill (rgba16f).
- **The owner's decisions about it.** Recorded as the owner's call. Standing rule: every new history or average kept in a half-float target rounds through half_float.glsl.
- **Source.** memory project_ragev_half_float_truncation.md; docs/HANDOFF.md lines 954-962 and 1055-1056 ('Owner's call'); docs/RT-SERIES.md RT-5 part 3 record (line 776)

#### ISS-8 · kSettledBound judges 'settled' from the picture's alpha, not the blend count

- **Status on 2026-09-24:** open. **Area:** Reflection accumulator.
- **Fate in the roadmap:** Fixed directly, RT2-6 (arm 12). One history counter. It changes pixels, so it is an owner-judged arm, not a silent patch
- **What the owner sees.** The accumulator widens its bound for settled texels, and it can treat a texel that measured change has just restarted as settled.
- **Known cause.** reflection_accumulate.rvshader lines 1637-1638 read c.past.a, which is the lit shader's trust value and is never reset by the anti-lag. The real blend count lives in the id lane's green channel. This is the fourth appearance of the 'which counter says young' trap (it was fixed for the blur on 09-21 and for the ray allocator in fb9cbe9).
- **The owner's decisions about it.** Found in passing on 2026-09-23 and measured inert on RT-24. No owner decision; a plain correctness fix.
- **Source.** docs/HANDOFF.md lines 127-128; memory project_ragev_rt_series_state.md; reflection_accumulate.rvshader:267, 1637-1638

#### ISS-9 · The baked reflection probe is ~1/3 brighter than the traced reflection, and the traced reflection sits below its own 16-ray reference

- **Status on 2026-09-24:** open. **Area:** Reflections, baked probes, light energy.
- **Fate in the roadmap:** Fixed directly, then absorbed, RT2-6 (probes get shadows; the reference renderer settles which is right), RT2-27 (rough reflections from stored light, not the probe). 
- **What the owner sees.** In the garage the probe picture is about a third brighter than the traced reflection (frame mean at a cut 65 vs 49). Anything that fades between the two flashes or washes out: RT-24's smear, and 34% of the frame changing at a camera cut. Separately, the shipped traced floor sits below a 16-ray render of the same estimator.
- **Known cause.** Which of the two is right is not established. Named suspects: (a) the firefly clamp, which once took about a fifth of the reflection's light (65.8 shipped, 75.0 with the clamp off, 81.7 at 16 rays) and later got an emitter exemption; (b) the resolve's ratio weighting (R11: the floor 2.5 levels under a per-texel reference), carried into RT-21 and not re-measured after RT-21 fixed the GGX cap; (c) reflection hits on Static surfaces take the baked lights' direct light from the irradiance field. mirror_control.py settled that reflections do carry the room's light (ceiling 71%, wall 106% of direct).
- **The owner's decisions about it.** Fix 1 removed the age-based fade, so the mismatch no longer drives RT-24, but the probe is still the fallback outside the reflection window. The owner's bar for baked against traced: close to zero visible difference, or a difference that favours baked, judged on per-pixel diff images; no gain factor to force a match.
- **Source.** docs/HANDOFF.md lines 32-37; memory project_ragev_rt24_spin_smear.md, project_ragev_reflection_sparkle.md, project_ragev_reflection_nondeterminism.md (R11), project_ragev_reflection_fixture.md; docs/RT-SERIES.md RT-4 and RT-21 rows

#### ISS-10 · Switching a baked light at runtime throws the bake away

- **Status on 2026-09-24:** accepted. **Area:** Baked lighting.
- **Fate in the roadmap:** Absorbed, RT2-25. Switchable lights are Realtime (the owner's rule), and with RT2-25's realtime layer they now bounce light too. Switching a baked light still means a re-bake, by design (D4: baked lights stay baked)
- **What the owner sees.** A Half, Full or Hybrid light switched off fades over a second or more (bridge: 10% of the light still on the structure after 79 frames).
- **Known cause.** The light leaves CollectLights, so Scene::LightingHash changes. The irradiance field then finds no bake on disk for the new hash, is zeroed, and the traced bounce rebuilds what the field held. Realtime lights are not in the hash and switch cleanly.
- **The owner's decisions about it.** Accepted on 2026-09-14: 'they are called baked for a reason'. Lights that switch must be Realtime. Do not use the garage's baked tubes for light-switch tests.
- **Source.** docs/BAKING-ROADMAP.md §2 (pitfalls); docs/HANDOFF.md lines 617-623; docs/RT-SERIES.md RT-16 row; memory project_ragev_baked_lights_switching.md

#### ISS-11 · The sea reflection work the owner accepted is not on main, and main's sea mirror has no accumulator

- **Status on 2026-09-24:** parked. **Area:** Water (sea reflection and lamp light).
- **Fate in the roadmap:** Absorbed, RT2-1 (its validation fixes), RT2-31 and RT2-32. The owner chose to bring it back with the water rework in M5 (D10). Costs recorded then at 1440p: the pier +1.7 ms, Glitter +1.2 ms, Headland 1.3 ms faster. It was never checked with a moving camera, so RT2-32 checks that before it becomes the default
- **What the owner sees.** On main, the sea's traced mirror reflection has no temporal accumulator of its own (WaterRayContract = false); it looks clean only because TAA averages it. The work the owner watched and accepted on the night of 2026-09-20 ('looks perfect to me, make it default') is absent from main.
- **Known cause.** That work was parked whole with RT-10 on branch wip/2026-09-21-rt10-and-sea (be4abf6) when main went back to 5b6d052. It includes: a sharp quarter-size mirror pass with its own accumulator; the brightness bound applied before averaging; a sub-pixel nudge; the lamp and soft-shadow draws varying whenever the sea's accumulate runs; WaterMirrorSparse; the validation fixes; and docs/SEA-ACCUMULATOR-HANDOFF.md. HANDOFF says 'the sea fix can be brought back on its own'. Costs measured then at 1440p: pier +1.7 ms, glitter +1.2 ms, headland 1.3 ms faster. It was never checked with a moving camera. Earlier, RT-8 job 2 (the sea mirror as a signal) was dropped for regressing the bridge (brighter and redder).
- **The owner's decisions about it.** The owner accepted the look live on the pier on 2026-09-20; committing it was 'to be discussed later'. Rules for any re-landing: no reliance on TAA; the frame must not get brighter or the bridge redder; judge with live runs, not stills or heat maps. The owner also suspects the sea's lamp choice behind the orange dots is wrong; that has not been examined.
- **Source.** EngineConfig.h:602 (WaterRayContract = false on main); branch wip/2026-09-21-rt10-and-sea (EngineConfig.h:622 and :632 are true there); docs/HANDOFF.md lines 382-405; memory project_ragev_sea_accumulator_baseline_bug.md; docs/RT-SERIES.md RT-8 row

#### ISS-12 · Random light picks only vary when TAA jitters, and the engine forces TAA whenever rays are on

- **Status on 2026-09-24:** open. **Area:** Anti-aliasing and every ray-traced signal.
- **Fate in the roadmap:** Fixed directly, RT2-7; the force is lifted at the end of M5 (D3, RT2-33). 
- **What the owner sees.** Under MSAA, SSAA or no AA with rays on, the random light picks and soft-shadow sample points would be frozen per pixel, so each signal's accumulator averages one answer with itself. That was seen as the sea's orange dots and frozen lamp picks on land. To prevent it, the engine now ignores the chosen AA mode whenever ray tracing is on.
- **Known cause.** Several draws decide whether to vary by asking 'is the raster jittered': params.Animated = jitter != 0 (Renderer3D.cpp 7987, 8097, 8267: TraceDirectLight and related passes), RV_TRACE_ANIMATED defaulting to the same jitter test (ray_shadow_trace.glsl:24-25), and LampFrameSalt (water_lamps.glsl:453). ResolveAntiAliasing returns TAA whenever ResolveRayTracing is true (FrameGraphBuilder.cpp:197-206), so --aa= applies only with rays off. The per-signal fix (draws vary whenever that signal's accumulator runs) exists only on the parked branch, and only for the sea.
- **The owner's decisions about it.** Forcing TAA with rays on was the owner's decision on 2026-09-21. In principle it conflicts with the standing rule 'no reliance on TAA: every signal has its own accumulator; judge noise with AA off'. Note: the sample project has used TAA since 2026-08-27 by the owner's own settings (commit 5dd603f), so the 'MSAA 4x is deliberate' rule describes an older state; MsaaSamples: 4 is still stored.
- **Source.** FrameGraphBuilder.cpp:177-208; docs/HANDOFF.md lines 394-400; memory project_ragev_sea_accumulator_baseline_bug.md; memory project_ragev_wr16_evaluation_findings.md (the S4c finding)

#### ISS-13 · Texel emitters: built and merged, but inert on real content and with an unexplained cost

- **Status on 2026-09-24:** deferred. **Area:** Emissive lights and aimed light sampling.
- **Fate in the roadmap:** Absorbed, RT2-11. The emitter table is rebuilt from emissive triangles, GPU-resident, with alias-table sampling
- **What the owner sees.** Per-texel aiming (stage 2) engages only on flat Plane and Quad primitives with untransformed texture coordinates; the shipped showroom reported 0 aiming tables. The feature was recorded as costing about 5% (7.13 -> 7.50 ms) with no pass to pin it on.
- **Known cause.** All three stages were built, checked and merged to main on 2026-08-24; the feature is not 'unbuilt' as sometimes stated. Tiled materials fold their tiling into UvTransform and fail the plain-UV test. The cost investigation found that the A/B scene is not in the repo and that the tools compared incompatible averages. The sampler is a 12-step dependent binary search over a cumulative table, not the O(1) alias table the design called for, and the table sits in host-visible memory and is re-uploaded every frame.
- **The owner's decisions about it.** The owner deferred the cost question. A PDF write-up was requested and is still pending. Optimising it belongs to the pass after the RT series.
- **Source.** docs/TEXEL-EMITTERS.md status (lines 1-10, 365-372); memory project_ragev_texel_emitters.md, project_ragev_texel_emitter_frame_cost.md; Renderer3D.cpp ~1505-1532 and 2299

#### ISS-14 · Cutout (alpha-tested) materials: done, with three known limits

- **Status on 2026-09-24:** accepted. **Area:** Materials and ray queries.
- **Fate in the roadmap:** Stays accepted; one part measured, RT2-38. The limits are deliberate trades. Opacity micromaps are measured against the traversal-cost cliff
- **What the owner sees.** Undersides of railings and fences read slightly too dark, because ambient occlusion treats cutouts as solid. An entity that overrides its base-colour alpha casts a shadow cut to the material's alpha instead. Closed boxes with cutouts cast solid shadow-map shadows.
- **Known cause.** Done on 2026-08-28: BlendMode::Masked with AlphaCutoff and its own pipelines. The ray side tests alpha inside the rayQueryProceedEXT loop, with masked instances marked FORCE_NO_OPAQUE. AO holds only the acceleration structure, on purpose; shadow alpha is read from the material; the shadow pass culls no faces. A texture fetch during traversal is the known performance cliff and has not been measured at AAA scale.
- **The owner's decisions about it.** The owner put this at the top of the queue on 2026-08-27. The three limits were recorded as deliberate trades.
- **Source.** docs/NEXT.md §0 (lines 177-235); memory project_ragev_cutout_materials.md (stale: still calls it the next task)

#### ISS-15 · WR-16 ray-budget leftovers: nine unverified review findings and legacy sea paths

- **Status on 2026-09-24:** open. **Area:** Ray budget and the water lamp passes.
- **Fate in the roadmap:** Fixed directly, RT2-3, RT2-32. Deleting the code makes the findings moot. The reuse verdict is not reopened
- **What the owner sees.** The old sea lamp paths and their switches remain in the tree beside the shared pass, and several defects claimed in them were never checked.
- **Known cause.** A 2026-09-05 review returned 12 findings; 3 were verified and fixed (50e11b7) and 9 remain unverified. Among them: the water choice reuse uses the biased ReSTIR combine, so the verdict that reuse loses was measured with a biased estimator; the choice history is reprojected without the previous frame's jitter; --light-sampling=8 silently runs K=4 under the passes (RV_LAMP_RESERVOIRS = 4); the 'no shadow ray to a lamp that casts none' rule sits at one of eight trace sites; the reuse history is still prepared every frame. Also: S3's bar of 0.01 changes per tile per second has no derivation, and check_glint_flicker.py was never run on S3. Since RT-8 job 1 moved the sea onto DirectTrace, much of this code is reachable only with --water-direct=off or --water-lamp-reuse=on.
- **The owner's decisions about it.** S4c's memory of 4,2 is provisional ('in future I might ask you to tweak it again'); when tuning it, show the owner detail, flicker and error together. What was left of S3-S5 folded into RT-8, RT-9 and RT-10, and RT-10 is dropped.
- **Source.** memory project_ragev_wr16_evaluation_findings.md, project_ragev_wr16_s3_allocator.md, project_ragev_wr16_s4b_verdict.md; docs/HANDOFF.md lines 2640-2648; EngineConfig.h:367 and 505; water_lamps.glsl:89; FrameGraphBuilder.cpp:3142-3160

#### ISS-16 · Showroom frame-time regression (181 -> 124 FPS) only partly recovered

- **Status on 2026-09-24:** deferred. **Area:** Performance.
- **Fate in the roadmap:** Absorbed, RT2-1 (counters), RT2-13 and RT2-22 (register pressure), RT2-26 (field lookup per fragment). The showroom became the garage, so its numbers are out of date. Section 6's budget replaces them
- **What the owner sees.** Between 2026-09-03 and 09-05 the showroom went from 5.53 to 8.05 ms at 1440p. Keeping water passes out of scenes that have no water brought it back to 7.74 ms (129 FPS).
- **Known cause.** Bisected into four blocks. Full bake plus the static/moving split: +0.80 ms (FieldLocate runs per fragment and per traced hit; BakedShare sits inside the light loop). Always-on ray counters: +0.26 ms, with no off switch. The S1 instrument's registers: +0.84 ms; gating it recovered nothing because the register limit had moved elsewhere. S2 plus the water work: +0.65 ms, of which 0.26 was water passes (now fixed). The showroom has since become the garage and RT-1 removed the light loop under RT, so these numbers are no longer current.
- **The owner's decisions about it.** Optimisation is a dedicated pass after the RT series (owner, 2026-09-15). Measure and note costs during tasks; do not trim features mid-task or switch them off for small gains.
- **Source.** memory project_ragev_showroom_fps_regression.md; docs/NEXT.md 'Applied 2026-09-05' (lines 541-585)

#### ISS-17 · The opaque pass is bound by per-pixel work, and the BRDF maths is not the cost

- **Status on 2026-09-24:** deferred. **Area:** Performance (lit pass).
- **Fate in the roadmap:** Absorbed, RT2-22, RT2-29. One composition with no second material evaluation, and fewer pixels. The BRDF finding stands
- **What the owner sees.** The Headland opaque pass is 10.8 ms at 1440p: about 1.7 ms of fixed geometry cost and about 9.1 ms that scales with pixel count.
- **Known cause.** By ablation, the whole specular core is 0.5% of the opaque pass; the arithmetic hides behind the 80-byte light-record reads. The levers are reading fewer records or shading fewer pixels (variable-rate shading, cheaper distant shading). A Nanite-style geometry system would aim at the 1.7 ms. Measured before RT-1 took light walking out of the lit shader under RT, and not re-measured since.
- **The owner's decisions about it.** Owner: 'note down the 9.1 stuff, we will look into it later'. Do not spend sessions on cheaper BRDF variants.
- **Source.** docs/NEXT.md 'The opaque pass is not BRDF-bound' (lines 488-537); memory project_ragev_brdf_not_the_cost.md, project_ragev_wr16_s3_allocator.md

#### ISS-18 · No many-light direct lighting since RT-10 was dropped; the direct pass tops out at 8 lamps a pixel

- **Status on 2026-09-24:** dropped. **Area:** Direct lighting (DirectTrace).
- **Fate in the roadmap:** Fixed directly, RT2-12. A fixed-cost candidate structure. RT-10's two missing mechanisms become a later arm on top of it
- **What the owner sees.** Scenes with many lights per pixel (the bridge: 78 lights per fragment on average, up to 147) rely on picking at most 8 lamps a pixel, with no reuse. RT-9's per-tile lamp allocation has no headroom at Quality: 8 is the ceiling, so its pass is skipped.
- **Known cause.** RT-10 (ReSTIR DI: choose then shade, borrow neighbours' picks, keep picks across frames) moved the garage further from the correct picture for about +3 ms and changed nothing on the bridge. It was taken out on 2026-09-21 and parked on wip/2026-09-21-rt10-and-sea. Never built: reprojection by what the ray actually struck, and a correct weight for a pick whose visibility has changed since it was chosen.
- **The owner's decisions about it.** Owner: 'we have tried everything in the book for RT-10 and things just get worse'. Recorded as 'worth coming back to, and the branch is the starting point'; what is missing is a mechanism, not a dial. Owner rule from the sea work: never leave a requested feature switched off because it gains little; deliver it on and report the numbers.
- **Source.** docs/RT-SERIES.md RT-10 row (line 61) and RT-9 row (line 60); docs/HANDOFF.md lines 382-393

#### ISS-19 · Rays can aim at no more than 16 glowing surfaces

- **Status on 2026-09-24:** open. **Area:** Emissive lights and aimed sampling for GI and reflections.
- **Fate in the roadmap:** Fixed directly, RT2-11. 
- **What the owner sees.** Beyond 16 emissive rectangles, reflection and bounce rays find the extra emitters only by luck, which shows as grain and fireflies. The garage alone has twenty glowing tube bars; which of them make the list is unverified.
- **Known cause.** kMaxAreaEmitters = 16 (Renderer3D.h:76), and SetAreaEmitters stops at the cap (Renderer3D.cpp:3120). Emitters are modelled as rectangles and picked from a flat list. For AAA scenes with hundreds of emissive surfaces this is a hard wall.
- **The owner's decisions about it.** No owner decision on the cap. Owner rule: light-sampling fixes must be general, never per light type.
- **Source.** Renderer3D.h:63-76; Renderer3D.cpp:3108-3135; docs/NEXT.md §6 ('capped at 16')

#### ISS-20 · Reflections on or of moving objects: the accumulator reads memory from the wrong place

- **Status on 2026-09-24:** open. **Area:** Reflection accumulator.
- **Fate in the roadmap:** Absorbed, RT2-23. Checking the struck object under any motion, plus a history-length counter
- **What the owner sees.** The driving car leaves a faint ghost on the floor behind it (now visible only on a plain white test floor). The car body's reflections sparkle and trail while it drives. The moving cube once carried blobs, partly because it had been rendering the default matte material all along.
- **Known cause.** One stored colour per pixel cannot follow both the still room and a moving reflected object. The accumulator reprojects by a virtual-image distance it estimates, not by what the ray actually hit. Refusing memory on movers removes the artefacts but loses the averaging. A second 'moving' layer composited after TAA removed the ghost, but its grain is left unaveraged. Not built: reprojection by the struck object (RT-17 already records the hit's instance and its previous transform).
- **The owner's decisions about it.** Owner (the RT-15 episode): when analysis finds the correct design, build and test it rather than offering lesser options. Ray allocation for near-mirror surfaces was assigned to RT-10, which is now dropped.
- **Source.** docs/RT-SERIES.md RT-23 earlier records (line 75) and RT-15 row (line 66); docs/HANDOFF.md lines 401-405 and 703-708

#### ISS-21 · The reflection clean-up passes spread bright outliers, and a more correct sampler ships switched off

- **Status on 2026-09-24:** open. **Area:** Reflection resolve, blur and ray sampling.
- **Fate in the roadmap:** Absorbed, RT2-6 (clamp scaled by lobe width), RT2-23 (outliers bounded before any spatial pass; visible-normal sampling re-measured with honest densities). 
- **What the owner sees.** Bright outlier rays (hits on the tubes) are spread across the floor by the neighbour gather and the three-pass blur. The visible-normals sampler (a more correct way to choose reflection ray directions) sprays speckles over the floor, so it ships off.
- **Known cause.** Established by reading, not yet by an instrument. The firefly clamp's threshold (mean plus k standard deviations) is computed over all taps including the outlier, so one huge tap raises the cap above itself. The blur's edge-stopping divides by the variance, so it smooths hardest exactly where a spike raised the variance. Robust statistics plus a finite emitter credit cost 2.3 levels of floor light and were reverted. Left in the tree: --reflection-vndf (off), the full variance and edge-stopping filter (measured inert), --reflection-moving-layer (off).
- **The owner's decisions about it.** RT-23's speckle itself is fixed (bf586e3). Before touching the clamp again, paint where the cap fires and watch it live. No fix may eat the lamp light RT-11 recovered; judge against the 16-ray truth.
- **Source.** docs/HANDOFF.md 'RT-23: bisected, not fixed' (lines 298-357); docs/RT-SERIES.md RT-23 row

#### ISS-22 · Reflections pass through two frame-averaging filters in a row (their accumulator, then TAA)

- **Status on 2026-09-24:** open. **Area:** Temporal architecture.
- **Fate in the roadmap:** Fixed directly, RT2-0 (bisection), RT2-28. 
- **What the owner sees.** The two memories compound, giving lag after changes and late settling. RT-24's spreading blur under motion lives in TAA's pass over a signal that already has a history of its own.
- **Known cause.** RT-6.1 and RT-4 combine reflections into the lit pass before TAA, so each traced signal is averaged by its own accumulator and then again by TAA (still feedback 0.98, about 50 frames; moving feedback 0.9). RT-16 raised whether one signal should pass through both filters; it was never settled.
- **The owner's decisions about it.** Owner decision on 2026-09-04 (WR-16 Part IV): two temporal filters in series are OK, and the direct light keeps its own history under every AA mode. RT-24's next step is exactly the bisection of TAA's rules on reflections.
- **Source.** docs/RT-SERIES.md RT-16 row (line 110) and 'Also reported' (lines 304-312); docs/HANDOFF.md RT-24's spread (lines 56-61)

#### ISS-23 · See-through surfaces are second-class for ray tracing and frame averaging

- **Status on 2026-09-24:** accepted. **Area:** Transparency and the glass layer (RT-13).
- **Fate in the roadmap:** Absorbed, RT2-31 (panes behind the nearest lit by the library in RT2-13). The panes behind the nearest stay on the old path by the owner's RT-13 decision, and only TAA averages their reflection rays; the owner accepted that as a named exception (D11)
- **What the owner sees.** Only the nearest pane of glass joins the shared ray-traced passes; panes behind it use the old path. A window's pixels carry the data lanes of whatever is behind the glass. The pane's own lamp light is never re-lit by measured change.
- **Known cause.** Weighted blended OIT writes no velocity, material or id. The glass layer is a single G-buffer layer, and depth peeling would cost a full set of passes per pane. Glass casts its own reflection rays: 1.3 ms of the close-up's 2.0 ms transparent pass.
- **The owner's decisions about it.** Panes behind the nearest stay on the old path by the owner's decision. The glass ray cost is for the optimisation pass.
- **Source.** docs/RT-SERIES.md RT-13 row (line 64) and record (lines 2195-2242); docs/HANDOFF.md lines 79-84 and 221-227

#### ISS-24 · A shader variant that fails to compile quietly swaps in a different rendering path

- **Status on 2026-09-24:** open. **Area:** Shader pipeline and robustness.
- **Fate in the roadmap:** Fixed directly, RT2-1. 
- **What the owner sees.** Dead code or a missing define changes the picture (once by 14 display levels) with nothing on screen to say why. RT-23's first fix attempt compiled to nothing and looked like a success.
- **Known cause.** When a pass's variant fails to compile, the pass falls back to an older path and writes one log line (Renderer3D.cpp ~1750-2210). The only guard is for measurement runs: screenshot and benchmark runs exit with code 3 if any shader failed (Entrypoint.h:83-90). Interactive editor sessions still just log.
- **The owner's decisions about it.** No owner decision recorded; the RT-11 hand-off lists it as still open.
- **Source.** docs/HANDOFF.md lines 271-276 ('the most expensive defect here'); docs/RT-SERIES.md line 205; memory project_ragev_reflection_fixture.md; Entrypoint.h:75-91

#### ISS-25 · Two Vulkan validation errors on the bridge are fixed only on the parked branch

- **Status on 2026-09-24:** open. **Area:** Vulkan correctness (water passes).
- **Fate in the roadmap:** Fixed directly, RT2-1. 
- **What the owner sees.** Running the bridge under --validation=on reports VUID-vkCmdDraw-mipmapMode-04770 (an integer texture read through a point sampler whose mip mode is linear) and VUID-vkCmdPushConstants-offset-01795 (112 bytes pushed into a 96-byte block).
- **Known cause.** PointSampler leaves its mip mode at Linear (Renderer3D.cpp:1643-1649), and water_accumulate.rvshader's LampParams block lacks the Trace vec4 (lines 79-90). Both fixes were made on the night of 2026-09-20, together with a TextureLoader::ZeroUint stand-in and a missing texture-heap bind in TraceWaterReflection. They exist only on wip/2026-09-21-rt10-and-sea.
- **The owner's decisions about it.** On 2026-09-13 the owner said to focus on these mismatches. They were fixed later but did not survive the RT-10 rollback.
- **Source.** docs/HANDOFF.md lines 840-860; memory project_ragev_sea_accumulator_baseline_bug.md (RT-10 section); wip branch Renderer3D.cpp:1606

#### ISS-26 · Per-frame scene walks still grow with object count

- **Status on 2026-09-24:** open. **Area:** CPU and scene update (Scene.cpp).
- **Fate in the roadmap:** Absorbed, RT2-34, RT2-43. Tracked writes and change lists replace the walks
- **What the owner sees.** At 60,000 objects the transform walk took 27.4 ms of a 52 ms frame. The fix raised that scene from 18 to 33 FPS, but the walk is still proportional to object count and runs several times a frame.
- **Known cause.** ROADMAP 8.15's fix caches each node's last composed transform and skips the arithmetic when nothing changed. But every entity is still visited on every call (the code's own comment: 'What the flag saves is the arithmetic at each node, not the visit'). UpdateWorldTransforms also raises m_DrawListDirty unconditionally (Scene.cpp:358), so the draw list is rebuilt every call. The fixed-step loop calls it after every tick, so a slow frame walks more times. Dirty flags were avoided on purpose, because a missed write site freezes an object. Other per-frame linear scans on record: slotFor, the TextureHeap::BeginFrame sweep, and CaptureReflectionProbes re-dirtying transforms.
- **The owner's decisions about it.** No owner ruling beyond 8.15's design (the walk looks; nothing is trusted to report a move). AAA scale needs a different shape that keeps that safety, such as change lists or transforms computed on the GPU.
- **Source.** docs/ROADMAP.md 8.15 (line 669); docs/ENGINE-NOTES.md 7bx (line 11650); Scene.cpp:350-372; memory project_ragev_transform_walk_cost.md, project_ragev_texel_emitter_frame_cost.md

#### ISS-27 · No mesh levels of detail

- **Status on 2026-09-24:** open. **Area:** Geometry and scale.
- **Fate in the roadmap:** Fixed directly, RT2-38. 
- **What the owner sees.** Every mesh draws at its full triangle count at any distance. The 2.7 km bridge is only 105k triangles because everything has to be cheap everywhere. Sub-pixel members flicker. Terrain has levels of detail; meshes and water do not.
- **Known cause.** No LOD chain and no mesh simplifier exist (NEXT.md item 5). This caps asset density and therefore realism, and it also drives the size of the ray-tracing acceleration structures.
- **The owner's decisions about it.** Ranked second only to assets as a realism win (2026-08-31). The owner's plan for authored or scanned rock depends on it.
- **Source.** docs/NEXT.md §5 (lines 315-328); memory project_ragev_bridge_scene.md

#### ISS-28 · Area lights: LTC and luminaire binding not built; the capsule highlight is too bright

- **Status on 2026-09-24:** open. **Area:** Direct lighting models.
- **Fate in the roadmap:** Fixed directly, RT2-10 (areas, rectangles, LTC as a measured option, tube energy), RT2-11 (a light owns its lens). 
- **What the owner sees.** Lights are points, or capsules and spheres shaded from one representative point. The sized-light highlight is 1.5-3x too bright at roughness 0.2-0.6. A lamp's glowing lens mesh and its light are separate objects that can drift apart. There are no rectangle lights.
- **Known cause.** WR-8 (LTC rectangle and line lights; LTC means linearly transformed cosines, the standard analytic area-light method) and WR-9 (the light owns its lens, so light and glowing mesh are counted once) are not built. RT-7 gave tubes a length and radius in the raster loop and the direct pass, with soft tube shadows.
- **The owner's decisions about it.** Linking a light to its mesh so the light draws the reflection was rejected on sight on 2026-09-14 (the floor reflections vanished). It must be measured before it is proposed again.
- **Source.** docs/RENDERING-REVAMP.md index (lines 207-209) and WR-8/WR-9 (lines 842-941); docs/NEXT.md §6 (lines 330-361); docs/HANDOFF.md lines 634-641

#### ISS-29 · Night-realism items still unbuilt (WR-5 remainder, WR-6, WR-11, WR-12, WR-14)

- **Status on 2026-09-24:** deferred. **Area:** Rendering features (the night scene).
- **Fate in the roadmap:** Stays deferred, (prerequisites: RT2-33). They stay in the WR series. RT2-33 does only fog in the composition and fog at ray hits
- **What the owner sees.** No volumetric light shafts (only exponential height fog), no bloom audit or glare weights, no local highlight bound for water sparkle, no night display finishers (toe, local exposure, stars, a Purkinje switch), and no night foam pass.
- **Known cause.** Not built. Done: WR-0 to WR-4, WR-13, WR-15, WR-17, WR-18 and WR-5's lamp sprites. WR-10 (the world-space light grid) was built inside WR-16 S4, and RT-7 superseded WR-7.
- **The owner's decisions about it.** The WR series comes after the RT series (owner, 2026-09-06). RENDERING-REVAMP's frame budget of 12.6 ms was measured with ray tracing off; re-baseline before using it to order work.
- **Source.** docs/RENDERING-REVAMP.md index (lines 198-221); docs/RT-SERIES.md 'the WR items to revisit' (line 3409); docs/NEXT.md §7

#### ISS-30 · Moving chrome cube: stripes, banding, speckles and a softer look

- **Status on 2026-09-24:** deferred. **Area:** Reflections on moving reflectors.
- **Fate in the roadmap:** Absorbed, RT2-23 (with RT2-21's curvature and the RT2-2 reference). Measured against a truth render for the first time
- **What the owner sees.** Speckles on the older part of the moving cube's face, vertical banding on its bottom bar, and vertical stripes after it passes the car (also in the shipped resolve). The cube reads softer than the chrome bars beside it.
- **Known cause.** Unexplained. On the softness: the resolve shares rays across a flat surface but cannot across a round one. Not measured against a truth render.
- **The owner's decisions about it.** Owner: 'a known issue for later'. The owner prefers more rays where history is young (RT-9) over the young blur.
- **Source.** docs/HANDOFF.md lines 686-688 and 889-893; docs/RT-SERIES.md line 768 and RT-15 row

#### ISS-31 · The garage tube rims report motion while the camera is parked

- **Status on 2026-09-24:** open. **Area:** TAA and motion vectors.
- **Fate in the roadmap:** Fixed directly, RT2-6 (arm 14). It changes pixels, so it is an owner-judged arm
- **What the owner sees.** A thin rim around each ceiling tube (about 0.08% of pixels) reports motion with the camera still. Measured change also flags about 400 tube-rim pixels, by up to 11-15 levels, on frame 2.
- **Known cause.** Not investigated. RT-20's edge rule skips these pixels.
- **The owner's decisions about it.** None recorded.
- **Source.** docs/HANDOFF.md lines 606-613, 892, 957

#### ISS-32 · The sea: structural lighting gaps and unconfirmed owner reports

- **Status on 2026-09-24:** open. **Area:** Water.
- **Fate in the roadmap:** Absorbed, RT2-32. Lighting per crossing through the layer. The owner is asked for the camera before the blur is chased
- **What the owner sees.** From the Glitter camera the sea's lamp light is about 10% too bright, one-sidedly. The owner reports horizontal lines instead of continuous streaks on Pier (a 2-pixel row pattern) and a blur on a low, close view of the tower base. The orange lamp dots are suspected to come from a wrong lamp choice.
- **Known cause.** The lamp passes shade one water surface per pixel, while the water draw blends 3-5 overlapping crossings at 400 m, so the nearest crossing's light is applied to the whole stack. The line pattern is not the reflection, the sun or the dither; refraction carries part of it (379 -> 330), and which artefact the owner means is unconfirmed. The blur is undiagnosed; the camera was never obtained. The sea keeps its own highlight shape (anisotropic Beckmann) inside the shared DirectTrace. There is no moving-camera harness for the bridge.
- **The owner's decisions about it.** Ask for the camera before chasing the blur. For anything that moves, run the scene live for the owner rather than showing stills or heat maps.
- **Source.** docs/HANDOFF.md lines 2536-2542 and 2762-2772; memory project_ragev_wr16_s4b_verdict.md, project_ragev_sea_accumulator_baseline_bug.md

#### ISS-33 · Per-pixel memory grows with resolution

- **Status on 2026-09-24:** open. **Area:** GPU memory and scale.
- **Fate in the roadmap:** Fixed directly, RT2-19, RT2-29. 
- **What the owner sees.** Measured change's direct-light record is about 30 MB at 1600x900 (roughly 170 MB at 4K by pixel count, not measured). The kept histories are about 128 bytes a pixel (RT-14). The old sea reuse history was about 118 MB at 1440p.
- **Known cause.** Full-resolution RGBA32F record lanes and many double-buffered histories. The named lever is packing values into half floats where that is exact; RT-14 advised against packing the G-buffer without measuring first.
- **The owner's decisions about it.** Belongs to the optimisation pass. The target GPU is a 12 GB laptop part.
- **Source.** docs/HANDOFF.md lines 776-781; docs/RT-MEASURED-CHANGE.md lines 174-192 and 261; docs/RT-SERIES.md RT-14 row and record (line 2786)

#### ISS-34 · The optimisation-pass list lives only on the parked branch

- **Status on 2026-09-24:** open. **Area:** Documentation and process.
- **Fate in the roadmap:** Absorbed, This roadmap. Glass rays go to RT2-31; the probe fetch at every hit to RT2-25 and RT2-27; the sea mirror's accumulate to RT2-32 (D10); RT-10's +8 ms is dropped
- **What the owner sees.** docs/RT-SERIES.md on main has no optimisation-pass section. The list the owner asked to keep exists only on wip/2026-09-21-rt10-and-sea: glass's own rays (1.3 ms); a probe fetch and BRDF at every reflection hit (+1.29 ms parked, +2.08 ms driving); the sea mirror's accumulate (about 1.6 ms); RT-10's +8 ms.
- **Known cause.** It was written on the night of 2026-09-20 in the tree that was parked on 2026-09-21.
- **The owner's decisions about it.** Owner (2026-09-20): 'just note it down as one of the things that is worth looking into for our post implementation optimisation phase'. The list is supposed to have one home.
- **Source.** git show wip/2026-09-21-rt10-and-sea:docs/RT-SERIES.md (line 3374); memory feedback_optimisation_pass_after_rt_series.md

#### ISS-35 · No pipeline cache and no shader cache: everything compiles on every launch

- **Status on 2026-09-24:** open. **Area:** Vulkan backend and load time.
- **Fate in the roadmap:** Fixed directly, RT2-9 (D6: approved). 
- **What the owner sees.** Every launch compiles every shader variant from the .rvshader sources beside the exe and builds every pipeline from scratch.
- **Known cause.** vkCreateGraphicsPipelines and vkCreateComputePipelines receive VK_NULL_HANDLE as their pipeline cache (VulkanPipeline.cpp:129 and 418), and the runtime compiles shader source at start without writing a cache. At AAA variant counts this becomes load time and hitches.
- **The owner's decisions about it.** None recorded.
- **Source.** VulkanPipeline.cpp:129, 418; memory project_ragev_texel_emitter_frame_cost.md; memory project_ragev_reflection_smear.md ('no shader cache is written')

#### ISS-36 · The raster fallback paths carry their own open defects

- **Status on 2026-09-24:** open. **Area:** Raster fallbacks (shadow maps, SSR, voxel GI) and the RT switches.
- **Fate in the roadmap:** Partly fixed, RT2-1 (re-check the device loss with rays on and reflections off), RT2-4 (name the demoted light). The SSR smear and the voxel GI noise stay parked: the raster fallbacks are kept at parity, not improved, in this series
- **What the owner sees.** A fifth shadow-casting point light is silently demoted (there are 4 point-shadow slots) and the warning does not name it. With RT reflections off, the screen-space fallback smears about 20%, far worse than traced reflections. Voxel GI noise has an unknown cause. RayTracing on with RayTracedReflections Off reliably lost the GPU device as of 2026-08-29.
- **Known cause.** ShadowMap::kMaxLocal = 4 (ShadowMap.h:140). SSR is a screen-space march. The voxel GI cause is open after three guesses were ruled out. The device loss was never recorded as fixed and has not been re-checked.
- **The owner's decisions about it.** Owner (RT-FIRST §4): raster and RT kept on a par wherever both can have a feature, and raster may lag where it cannot; every signal falls back to its raster version.
- **Source.** memory project_ragev_perf_session.md, project_ragev_flicker_open.md, project_ragev_rt24_spin_smear.md; docs/HANDOFF.md line 5487; docs/ENGINE-NOTES.md line 9150

#### ISS-37 · Baked-lighting limits that the traced picture now depends on

- **Status on 2026-09-24:** open. **Area:** Baked GI and probes.
- **Fate in the roadmap:** Absorbed, RT2-26 (including contact-scale bleeding and cells that move out of walls, both in its exit gate), RT2-40 (BC6H in the texture cooker). Baked lights stay baked (D4), so reflections of Static surfaces keep taking baked lamps' light from the field, by design. Nested volumes blend; the light that bleeds at contact scale (about 1.9% of pixels) is measured against the reference renderer and must fall; cells inside walls move out, which fixes the leak case the author rule could not; a stale bake is flagged in the editor. The bake on OpenGL stays unsupported (OpenGL is frozen, D1). Lightmaps are not planned (meshes carry one UV set)
- **What the owner sees.** Reflection and refraction rays that hit a Static surface take the baked lights' direct light from the irradiance field, so the traced picture changes with the bake. Other limits: light bleeds at contact scale on about 1.9% of pixels; one leak case needs probes that can move out of walls (the author rule is cells smaller than the thinnest wall); overlapping volumes do not blend; a bake run on OpenGL writes zeros; the texture cooker has no BC6H; lightmaps are impossible because meshes carry one UV set.
- **Known cause.** The design of the static/moving split and the irradiance volume; see the sources.
- **The owner's decisions about it.** Owner: baked probes and irradiance volumes stay as the far-field fallback for RT GI (RT-FIRST §4, answer 4); baked light stays at the physically correct level with no gain factor; switchable lights must be Realtime.
- **Source.** docs/HANDOFF.md lines 2120-2130; docs/BAKING-ROADMAP.md §1.4 and §3 (lines 252-263); docs/NEXT.md §4 (lines 310-313) and 'Not scheduled'; memory project_ragev_probes_and_baking.md

#### ISS-38 · Terrain: no shadow offset for LOD error, no triplanar mapping, macro breakup not exposed

- **Status on 2026-09-24:** open. **Area:** Terrain and materials.
- **Fate in the roadmap:** Partly, RT2-21 (terrain layers at hits), RT2-38 (shadow offset). Triplanar mapping and the macro inspector rows stay parked as materials work outside this series
- **What the owner sees.** Mapped layers smear into stripes on steep faces (worked around with an unmapped rock layer). The terrain's LOD budget is spent on avoiding shadow artefacts rather than on silhouettes. MacroScale and MacroStrength have no inspector rows and no suite checks.
- **Known cause.** Terrain texture coordinates are planar; the shadow ray is not offset by the drawn chunk's LevelError; ComponentRegistry::BuildMaterial never registered the Macro fields.
- **The owner's decisions about it.** The owner set macro breakup as important (2026-08-31); its shader half shipped.
- **Source.** docs/HANDOFF.md lines 4475-4495 and 4650-4660; memory project_ragev_bridge_scene.md

#### ISS-39 · The visual-script graph loader drops unknown node types and saves the result

- **Status on 2026-09-24:** open. **Area:** Editor and visual scripting.
- **Fate in the roadmap:** Stays outside, none. Not renderer work. It loses user data, so a small separate task is recommended now
- **What the owner sees.** A graph containing a node type the loader does not know loses that node silently, and a save writes the loss to disk.
- **Known cause.** The loader skips unknown node types instead of refusing the graph. The check catches this only on the test fixtures, never on a user's graph.
- **The owner's decisions about it.** None recorded. Outside the renderer, but it is a data-loss defect.
- **Source.** docs/ENGINE-NOTES.md lines 10361-10366

#### ISS-40 · RT-9's confidence-driven ray allocation earns nothing measurable here

- **Status on 2026-09-24:** accepted. **Area:** Ray budget.
- **Fate in the roadmap:** Fixed directly, RT2-0 (the motion test), RT2-23 (rays traded within a fixed budget), RT2-12 and RT2-24 (the direct half re-measured). Part of the "nothing measurable" was probably a pass that never ran during camera motion. That is read from the code; RT2-0's pass-timing check confirms it before anything changes
- **What the owner sees.** The reflections' tile allocation changes nothing in the garage. The direct-light half is a coin flip against the reference at 4 lamps and is skipped at Quality, where 8 is the ceiling. Extra rays stop the instant nothing moves.
- **Known cause.** Allocation only engages where young texels cluster; the direct pass is capped at 8 lamps; the 'still' gate was an agent's choice, not the owner's.
- **The owner's decisions about it.** Both halves are on by default. The owner asked 'why is RT-9 disabled?' about the still gate. It is also tied to RT-24's slow settle.
- **Source.** docs/RT-SERIES.md RT-9 row (line 60); docs/HANDOFF.md lines 434-452; memory project_ragev_sea_accumulator_baseline_bug.md

#### ISS-41 · One 6,500-line lit shader and hand-mirrored data layouts

- **Status on 2026-09-24:** open. **Area:** Code structure (shaders and GPU data layout).
- **Fate in the roadmap:** Absorbed, RT2-1 (checks), RT2-13 and RT2-22 (dead arrays out), RT2-17 and RT2-21 (modules, one layout definition). 
- **What the owner sees.** Changes to any one feature (water, glass, hit shading, debug views) land in the same forward lit shader. Unused arrays still cost registers: the S1 arrays cost +0.84 ms while switched off. Every new binding or field must be threaded through many places by hand.
- **Known cause.** pbr_fragment.glsl (6.5k lines) is included by the lit, trace-only, G-buffer, water and glass variants. The scene block is mirrored by hand in scene_block.glsl and pbr_fragment.glsl. All of set 0's bindings are used. InstanceData is a fixed 272-byte struct with every lane spoken for. OpenGL's 32-sampler limit holds the layered variant at 31. The ray-counter stride is written in four places. Variants are chosen by preprocessor defines.
- **The owner's decisions about it.** Owner: 'freedom to tear apart and rebuild the renderer' (2026-09-06); 'everything RT reads the G-buffer'; prefer the better implementation over the smaller diff.
- **Source.** docs/RENDERING-REVAMP.md §1 ground rules (lines 131-146); docs/RT-FIRST.md §1 and T3; memory project_ragev_showroom_fps_regression.md; memory project_ragev_probes_and_baking.md ('Four layout traps'); docs/HANDOFF.md 'The counter stride is in four places'

#### ISS-42 · Measurement gaps that confound test arms

- **Status on 2026-09-24:** open. **Area:** Tooling and measurement.
- **Fate in the roadmap:** Fixed directly, RT2-2. 
- **What the owner sees.** --reflection-history=off also removes the trust value, TAA's image motion and the noise blur. --rt-reflections=off brings SSR back (20% smear), so there is no clean 'no reflections' arm. Tests at 1-4 rays cannot tell ghosts from grain. There is no moving-camera harness for the bridge. Writing a PNG every frame flatters the accumulators. This laptop's GPU drifts about 1 ms between runs. The runtime reads shaders from its own staged copy, not the source folder.
- **Known cause.** Switches were built one feature at a time and share side effects. The headless swing harness (Orbiter, spin_measure, many16) exists only for the garage and is uncommitted.
- **The owner's decisions about it.** Owner: benchmarks run as A,B,B,A palindromes with the spread quoted; images compared pixel by pixel; for anything temporal, the owner watches the scene live, one arm at a time.
- **Source.** docs/HANDOFF.md lines 51-54; memory project_ragev_rt24_spin_smear.md, project_ragev_reflection_sparkle.md; docs/NEXT.md lines 531-537

### The owner's standing rules (as collected for the audit)

- RT-first, engine not scene: treat every rendering defect as an engine defect, fix the architecture for ray tracing in general, and propose architectural changes before building them. (memory feedback_rt_first_engine_not_scene.md (2026-09-06); docs/RT-SERIES.md preamble)
- No fix keyed on a surface type or object. If a fix regresses one surface, the fix is too narrow: go back to the mechanism. When the owner calls a fix bad, remove it first, then talk. (memory feedback_rt_first_engine_not_scene.md (2026-09-23 addendum); docs/HANDOFF.md lines 87-91)
- No reliance on TAA: every noisy signal needs its own accumulator and reconstruction. Judge a signal's noise with AA off, but never present the dotted AA-off picture as the reference look. (memory feedback_no_reliance_on_taa.md)
- A fix that makes the frame brighter or turns the bridge red is not a fix. Check mean brightness and the red-to-blue ratio at the bridge cameras before showing anything. (memory feedback_no_reliance_on_taa.md)
- Cost and quality levers are global RenderSettings values with a --flag override for measurement, never per-light dials. Per-light data describes only the fixture itself (range, radius, cone). (memory feedback_global_render_settings_not_per_light.md (2026-09-02))
- Build the better, more correct implementation even when it touches shared code. Time is not a cost that decides what gets proposed. No half-baked solutions. The owner is open to sophisticated architectural work that helps realism or performance, and does not want to be asked to pick between an easy shape and a correct one. (memory feedback_prefer_better_implementation.md (2026-09-07, 09-08, 09-15, 09-21))
- Measure before proposing: render or measure a replacement before recommending it; never cite big engines as evidence our result will look like theirs; say plainly what a change switches off. Nothing is adopted because other engines do it. (memory feedback_test_before_proposing.md (2026-09-14))
- Judge renders by per-pixel diff images, never by mean levels. A one-directional diff (all brighter or all redder) is a defect; a balanced diff is a differently seeded estimator. Render and open every camera a claim covers. (memory feedback_diff_images_not_means.md; docs/NEXT.md 'The standing rules' (lines 392-409))
- Benchmark as A,B,B,A palindromes and quote the spread; plain A,B interleaving is biased on this laptop, and a performance claim is a measurement or it is not a claim. (docs/NEXT.md lines 531-537 and 396-397)
- Optimisation is a dedicated pass after the RT series. Measure and record costs during tasks, but do not optimise mid-task. Never switch off or propose closing a requested feature because it gains little: deliver it on and report the numbers. (memory feedback_optimisation_pass_after_rt_series.md; memory project_ragev_sea_accumulator_baseline_bug.md (RT-10 section))
- The owner's settings in SampleProject (.rvproject and post profiles) are deliberate; never restore them wholesale. The project has used TAA since the owner's 2026-08-27 settings (the older 'MSAA 4x' note predates that), and with rays on the engine forces TAA (owner, 2026-09-21). (memory feedback_owner_settings_are_deliberate.md; commit 5dd603f; FrameGraphBuilder.cpp:197-206)
- Show one arm at a time and let the owner watch temporal effects live, with no time limit. Ask after each test and never pick a candidate yourself. Never present two failures as a choice. (memory feedback_show_one_arm_at_a_time.md; memory feedback_ask_which_result_to_apply.md)
- Explanation approval gate: explain each new step plainly (what changes, what it costs, what it switches off) and wait for an explicit OK. A question from the owner is never a go-ahead. (memory feedback_explanation_approval_gate.md (2026-09-15, 09-22, 09-23))
- One task per green signal: report after each task with the state of the tree, and start the next only when told. (memory feedback_report_each_task_green_signal.md; docs/RT-SERIES.md preamble)
- Discuss before building: bring what other engines do, name the defect a constraint forces, and put the options on the table; the owner decides. (memory feedback_discuss_dont_just_execute.md (2026-08-29))
- Answer plainly: say what each thing does before any comparison, define every term, one topic per answer, never an unexplained question, and always say which option looks better on screen. (memory feedback_answer_plainly.md)
- Everything RT reads the G-buffer: every ray-traced signal is traced in its own pass from the G-buffer's depth, normal, roughness, albedo and id, never inside the lit shader; temporal systems validate history by depth, normal and id. (docs/RT-FIRST.md §4 answer 7; docs/RT-SERIES.md preamble)
- Forward+ with a G-buffer prepass; full deferred shading was ruled off the table on 2026-09-06 (RT-series 2's rewrite permission should reopen this with the owner explicitly). (docs/RT-FIRST.md §4 answer 1)
- Every signal has a source, raster or ray-traced, and falls back to its raster version. Raster settings are greyed (never hidden) while ray-traced and vice versa. Raster and RT are kept on a par where both can have a feature; raster may lag where it cannot. (docs/RT-FIRST.md §4 answers 2, 3, 5)
- Baked probes and irradiance volumes stay as the far-field fallback for RT GI. Baked light stays at the physically correct level with no gain factor. The acceptance bar is close to zero visible difference, or a difference favouring baked. (docs/RT-FIRST.md §4 answer 4; memory project_ragev_probes_and_baking.md; memory feedback_diff_images_not_means.md)
- A baked light is baked for a reason: lights that switch must be Realtime. (docs/BAKING-ROADMAP.md §2; memory project_ragev_baked_lights_switching.md (2026-09-14))
- The frame may guess; the bake may not. Nothing transient (fallbacks, boosts) is blended into a stored history or bake. (docs/NEXT.md line 402; docs/RENDERING-REVAMP.md §1)
- No visual parameter may be a function of a value that moves frame to frame; any adaptive term needs hysteresis and a dead band. (docs/RENDERING-REVAMP.md §1 ground rules; memory project_ragev_perf_session.md)
- Flashing or fast-moving emitters bypass or hard-clamp every history buffer. Analytic overlays such as flare sprites are composited after the temporal resolve. (docs/RENDERING-REVAMP.md §1 ground rules (lines 152-162))
- Both backends must compile and render and scenetest must stay green on Vulkan and OpenGL. Respect OpenGL's 32-sampler limit. Any new half-float history rounds through include/half_float.glsl. (docs/RENDERING-REVAMP.md §1; memory project_ragev_half_float_truncation.md)
- Bridge look decisions: never fade the suspender ropes, rails, posts, lamp shafts or truss webs; never boost still-pixel feedback there; lamp glow stays at 0.02, flare share 0.2, unless the owner asks. (memory project_ragev_flicker_bridge.md (2026-09-02))
- The garage tubes are line lights (3.07 m, radius 0.075). A speckle or light-sampling fix must cover every light type, never one kind. (memory project_ragev_tube_lights_never_in_scene.md (2026-09-22))
- Order: RT-24 before everything else. Parked for later: the general flicker rule, the delayed reflection stop, the cable flicker. RT-2.2 stays last. RT-10 is dropped. Commit RT-24 only once it is fixed completely. (docs/HANDOFF.md header and RT-24 entry; memory project_ragev_rt_series_state.md)
- 'Commit and push' means the entire work, bakes and generated assets included. Delete debug captures once served. Scene textures stay untouched during cleanups; the owner runs any delete. (memory feedback_push_entire_work.md; memory feedback_delete_captures_when_done.md; memory feedback_ragev_cleanup_textures_untouched.md)
- No third-party type in a public RageV header (glm, Jolt, spdlog and others get real wrapper types), and public API sits in domain namespaces. (memory project_ragev_api_wrapping.md (2026-08-09))
- Ask before spawning agents or workflows, with the worst-case agent count in the question; use barriers so a run can be halted between phases; never kill a running stage on the owner's behalf. Stop on a permission or classifier denial and report. (memory feedback_ask_before_spawning_agents.md; memory feedback_workflow_stop_points.md; memory feedback_stop_on_classifier_block.md)
- Never touch RageV unasked while working on Ember; a plan written elsewhere is a description, not authorisation. (memory feedback_never_touch_ragev_unasked.md)

### Markers left in the code (TODO, FIXME and similar)

- `RageV/src/RageV/Core/EngineConfig.h:51` --gpu-lit help text: 'UNFINISHED, off by default: correct and much faster on static-only scenes, wrong and flickering on mixed ones' (the only explicit UNFINISHED/TODO-style marker in engine source and shaders)
- `RageV/src/RageV/Core/EngineConfig.h:990` GpuLit = true -- the code default contradicts the 'off by default' help text; the comment says it is left on so the defect stays visible
- `RageV/src/RageV/Core/EngineConfig.h:602` WaterRayContract = false on main: the sea mirror has no accumulator of its own (true on wip/2026-09-21-rt10-and-sea)
- `RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:205` ResolveAntiAliasing: 'if (ResolveRayTracing(render)) return AntiAliasing::TAA;' -- AA is forced to TAA whenever rays are on (owner, 2026-09-21)
- `RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:76` kAlbedoFormat R8G8B8A8_UNORM, 'Albedo in eight bits linear for now' -- the RT-2.2 precondition (sRGB8 or 16F)
- `RageV/src/RageV/Renderer/Renderer3D.cpp:7987` params.Animated = jitter != 0 -- stochastic draws vary only when TAA jitters (also at lines 8097 and 8267)
- `RageVEditor/assets/shaders/include/ray_shadow_trace.glsl:25` #define RV_TRACE_ANIMATED any(notEqual(u_Scene.Jitter, vec4(0.0))) -- the soft-shadow sample walk is gated on TAA's jitter by default
- `RageVEditor/assets/shaders/include/water_lamps.glsl:453` LampFrameSalt returns non-zero only when u_Scene.Jitter is non-zero (TAA-only lamp choice variation)
- `RageVEditor/assets/shaders/include/water_lamps.glsl:89` RV_LAMP_RESERVOIRS = 4u -- --light-sampling=8 silently runs K=4 under the water passes (unverified WR-16 finding)
- `RageVEditor/assets/shaders/reflection_accumulate.rvshader:1638` kSettledBound widening reads c.past.a (the trust the anti-lag never resets) instead of the blend count in the id lane's green
- `RageVEditor/assets/shaders/reflection_accumulate.rvshader:1664` RT-24 fix 2 (uncommitted): 'fewest' falls with how far the picture moved, specular instances only
- `RageVEditor/assets/shaders/include/pbr_fragment.glsl:6082` RT-24 fix 1 (uncommitted): settledShare = (settledReflection.a > 0 ? 1 : 0) * reflectionWindow; same form at line 6195 for the traced share
- `RageVEditor/assets/shaders/include/pbr_fragment.glsl:2984` The hit-shading light walk traces TraceShadowFrom (a hard point shadow) and reads neither Extent.x (length) nor Direction.w (radius); also at lines 3093 and 3155
- `RageVEditor/assets/shaders/taa_resolve.rvshader:1069` covered = Covered && revealage < 0.99; outline = sameSurface < 8 -- RT-22's location-based floor rule behind the water glitter regression
- `RageV/src/RageV/Renderer/Renderer3D.cpp:7228` AnyInstanceMoved() returns RayAnyMoving || RayAnyMovingLast -- keeps moving-only work scheduled one frame past a stop
- `RageV/src/RageV/Renderer/Renderer3D.h:76` kMaxAreaEmitters = 16 -- at most 16 emissive rectangles reach aimed sampling for GI and reflections
- `RageV/src/RageV/Renderer/Renderer3D.cpp:1649` PointSampler created without Mipmap = Nearest -> VUID-vkCmdDraw-mipmapMode-04770 on the bridge (fixed only on the parked branch)
- `RageVEditor/assets/shaders/water_accumulate.rvshader:79` LampParams push block is 96 bytes (no Trace vec4) while C++ pushes 112 -> VUID-vkCmdPushConstants-offset-01795 (fixed only on the parked branch)
- `RageV/src/RageV/Scene/Scene.cpp:358` UpdateWorldTransforms raises m_DrawListDirty = true unconditionally; the walk visits every entity on every call
- `RageV/src/Platform/Vulkan/VulkanPipeline.cpp:418` vkCreateGraphicsPipelines(..., VK_NULL_HANDLE, ...) -- no pipeline cache; the compute pipelines at line 129 are the same
- `RageV/src/RageV/Core/Entrypoint.h:90` return 3 when shaders failed -- only for screenshot or benchmark runs; interactive sessions fall back silently apart from a log line
- `RageV/src/RageV/Renderer/ShadowMap.h:140` kMaxLocal = 4 point/spot shadow slots in the raster fallback; a fifth caster is demoted
- `RageV/src/RageV/Renderer/FrameGraphBuilder.cpp:3156` The water lamp choice history is still Prepared here (sized by WaterLampReuse) even though --water-direct is the default path

## 4. The benchmark (2026-09-24)

The engine's own `--benchmark=300`, three runs per shot, 1600x900, the project's settings (rays on), Vulkan, validation off for timing and checked separately with `--validation=on`. Lower is faster.

- **Build.** No rebuild was needed, so none was run. Staged shaders (build/bin/Release/RageVRuntime/assets/shaders) compared against RageVEditor/assets/shaders with diff -r --strip-trailing-cr: every file in the source tree has an identical staged copy. The only difference is three files that exist only in the staged folder (quadshader.glsl, simpleshader.glsl, textureshader.glsl). They are leftovers with no source. A build copies files but never deletes, so it would not have removed them. The staged reflection_accumulate.rvshader is dated 2026-09-23 09:12, later than the source edit at 08:52, so the uncommitted RT-24 shader work (pbr_fragment.glsl, reflection_accumulate.rvshader) is what these runs measured. No .cpp/.h under RageV/src, RageVRuntime or RageVEditor/src is newer than RageVRuntime.exe (2026-09-22 23:10), so the binary matches the C++ source. Camera poses were checked against CAMERAS in tools/scripts/bench_night.py (Headland, Glitter) and the "Benchmark (garage)" line in docs/HANDOFF.md, and all match. Logs are in C:/Users/ism19/AppData/Local/Temp/claude/C--Users-ism19-Code/69b2a2c1-0da8-459a-8242-c1e5d00ae840/scratchpad/rt2/bench: <shot>_1..3.log for the timed runs, val_<shot>.log for the validation checks, scale_*.log, scale.txt, and parse.py (the summariser). No source, shader, scene or project file was touched.
- **GPU.** NVIDIA GeForce RTX 5070 Ti Laptop GPU (Vulkan 1.4.325, driver 591.91). The laptop was on mains power (Win32_Battery status 2, 81%). Timed runs had Vulkan validation off (the log says "validation off"). Validation was checked in separate 60-frame runs with --validation=on.
- **Other processes.** No RageVEditor.exe or RageVRuntime.exe was running before the first run or between runs (tasklist), and exactly one engine instance ran at a time. Browsers were open in the background the whole time: 20 chrome.exe, 8 msedge.exe and 24 msedgewebview2.exe processes. They were idle but may add a little noise.

### garage

- Scene `scenes/showroom.rage`, camera `-2.3,0.72,-2,11,0,4`, 1600x900, 301 frames x 3 runs.
- Frame: 18.25 ms mean (runs: 17.953 / 18.715 / 18.084 (range 0.76 ms); medians 17.88 / 18.49 / 17.72; p95 18.78 / 20.51 / 20.21). GPU: 18.22 ms.
- Rays per frame: shadow 5.85 M, water 0.00 M, reflection 1.39 M, GI 0.00 M (GI comes from the bake), AO 2.40 M -- 9.63 M in all, 6.7 per lit fragment (identical in all 3 runs)
- Lights per fragment: 22.1 avg, 23 max; at traced hits 24.0 avg over 1.36 M hits (24 lights in the scene, busiest cluster holds 23)
- Errors and validation: None in the timed runs: 0 'Shader compilation failed', no 'did not compile', no [Vulkan] lines. The validation-on check (val_garage.log) was also clean. Log noise: the C# script ShowroomMode reports 8 entities it cannot find ('Bay Downlight 0-3', 'Bay Downlight Light 0-3').

| Pass | CPU ms | GPU ms |
|---|---|---|
| ReflectionResolve | 0.007 | 3.912 |
| ReflectionTrace | 0.013 | 3.816 |
| DirectTrace | 0.011 | 3.183 |
| Scene (opaque lit) | 0.057 | 1.293 |
| ReflectionAccumulate | 0.013 | 0.940 |
| ReflectionBlur | 0.011 | 0.759 |
| ReflectionBlur2 | 0.005 | 0.676 |
| ReflectionBlur4 | 0.005 | 0.535 |
| DirectAccumulate | 0.014 | 0.428 |
| TAA resolve | 0.009 | 0.395 |
| Transparent | 0.012 | 0.387 |
| GBuffer | 0.739 | 0.263 |
| OcclusionCompute | 0.007 | 0.214 |
| GlassReflectionTrace | 0.009 | 0.210 |
| GlassDirectTrace | 0.006 | 0.163 |

### bridge Headland

- Scene `scenes/GoldenGateDemo.rage`, camera `500,89.47,-1100,0.01,-157.08,8.88`, 1600x900, 301 frames x 3 runs.
- Frame: 17.14 ms mean (runs: 16.819 / 17.325 / 17.289 (range 0.51 ms); p95 17.74 / 19.10 / 18.83). GPU: 17.12 ms.
- Rays per frame: shadow 2.53 M, water 0.38 M, reflection 0.13 M, GI 0.00 M (baked), AO 1.72 M -- 4.75 M in all, 2.8 per lit fragment
- Lights per fragment: 67.3 avg, 190 max; at traced hits 70.5 avg over 0.21 M hits (189-190 lights, busiest cluster holds 144-145)
- Errors and validation: The timed runs were clean (0 shader failures, no 'did not compile', no [Vulkan] lines, but validation was off). The validation-on check (val_headland.log) printed 20 [Vulkan] lines, all from two defects in the water path. (1) WaterAccumulateLamps pushes 112 bytes of fragment push constants into a pipeline layout that declares only 96 (VUID-vkCmdPushConstants-offset-01795). Push constants are small per-draw values handed straight to a shader; the last 16 bytes land outside the declared range, which is undefined behaviour. (2) Renderer3D.direct.water.shade (DirectWaterShade) binds the R32G32B32A32_UINT images u_ChoiceIn (set 3, binding 4) and u_WorthIn (binding 5) through a linear-filtering sampler, which that integer format does not support. Both hit the layer's duplicate limit.

| Pass | CPU ms | GPU ms |
|---|---|---|
| Transparent (sea surface + glass, forward shaded) | 0.014 | 5.576 |
| Scene (opaque lit) | 0.069 | 2.275 |
| ReflectionTrace | 0.012 | 1.473 |
| DirectWaterShade | 0.007 | 1.087 |
| DirectTrace | 0.011 | 0.811 |
| WaterSurface | 0.013 | 0.745 |
| GBuffer | 1.020 | 0.736 |
| DirectWaterChoose | 0.007 | 0.581 |
| ReflectionResolve | 0.008 | 0.536 |
| ReflectionAccumulate | 0.014 | 0.402 |
| TAA resolve | 0.012 | 0.359 |
| DirectAccumulate | 0.015 | 0.334 |
| WaterFoam | 0.009 | 0.221 |
| WaterAccumulateLamps | 0.010 | 0.171 |
| ReflectionBlur | 0.012 | 0.160 |

### bridge Glitter

- Scene `scenes/GoldenGateDemo.rage`, camera `500,2.5,180,0.01,-90,-1.146`, 1600x900, 301 frames x 3 runs.
- Frame: 13.21 ms mean (runs: 13.155 / 13.108 / 13.371 (range 0.26 ms); p95 14.50 / 14.15 / 15.47). GPU: 13.19 ms.
- Rays per frame: shadow 1.93 M, water 0.39 M, reflection 0.07 M, GI 0.00 M (baked), AO 1.30 M -- 3.69 M in all, 2.4 per lit fragment
- Lights per fragment: 118.1 avg, 190 max; at traced hits 57.6 avg over 0.10 M hits (189 lights, busiest cluster holds 150)
- Errors and validation: The timed runs were clean (validation off). The validation-on check (val_glitter.log) shows the same 20 [Vulkan] lines as Headland: the WaterAccumulateLamps 112-byte push into a 96-byte range, and DirectWaterShade sampling the UINT images u_ChoiceIn/u_WorthIn through a linear sampler.

| Pass | CPU ms | GPU ms |
|---|---|---|
| Transparent (sea surface + glass, forward shaded) | 0.012 | 3.315 |
| Scene (opaque lit) | 0.047 | 1.646 |
| DirectWaterShade | 0.007 | 1.056 |
| ReflectionTrace | 0.013 | 1.054 |
| DirectTrace | 0.010 | 0.947 |
| WaterSurface | 0.012 | 0.622 |
| GBuffer | 1.475 | 0.532 |
| DirectWaterChoose | 0.007 | 0.452 |
| TAA resolve | 0.010 | 0.354 |
| ReflectionAccumulate | 0.012 | 0.345 |
| ReflectionResolve | 0.007 | 0.321 |
| DirectAccumulate | 0.014 | 0.313 |
| WaterFoam | 0.008 | 0.217 |
| WaterAccumulateLamps | 0.009 | 0.192 |
| ReflectionBlur4 | 0.005 | 0.171 |

### camp

- Scene `scenes/camp.rage`, camera `scene's own start camera (no --camera)`, 1600x900, 301 frames x 3 runs.
- Frame: 7.60 ms mean (runs: 7.193 / 7.654 / 7.960 (range 0.77 ms, rising with every run: session drift); p95 7.79 / 8.30 / 8.52). GPU: 7.57 ms.
- Rays per frame: shadow 5.96-6.04 M, water 0.00 M, reflection 0.14-0.16 M, GI 1.62-1.67 M (traced here, no bake), AO 2.63-2.71 M -- 10.37-10.55 M in all, 7.2-7.3 per lit fragment
- Lights per fragment: 3.8-3.9 avg, 4 max; at traced hits 4.0 avg over 0.87-0.89 M hits (4 lights, busiest cluster holds 2)
- Errors and validation: None: 0 shader failures, no 'did not compile', no [Vulkan] lines in the timed runs. The validation-on check (val_camp.log) was also clean.

| Pass | CPU ms | GPU ms |
|---|---|---|
| GI trace | 0.006 | 1.215 |
| Scene (opaque lit) | 0.037 | 0.595 |
| ReflectionTrace | 0.009 | 0.589 |
| DirectTrace | 0.007 | 0.585 |
| ReflectionResolve | 0.005 | 0.559 |
| DirectAccumulate | 0.010 | 0.433 |
| ReflectionAccumulate | 0.011 | 0.396 |
| TAA resolve | 0.007 | 0.382 |
| OcclusionCompute | 0.005 | 0.291 |
| GiRecord | 0.021 | 0.218 |
| ReflectionBlur | 0.007 | 0.216 |
| ReflectionBlur4 | 0.004 | 0.208 |
| GiRelight | 0.021 | 0.199 |
| ReflectionBlur2 | 0.004 | 0.196 |
| GBuffer | 0.342 | 0.142 |

### The scale test

tools/scripts/bench_scale.py ran with its defaults (counts 1000,5000,20000,60000; 200 frames; 1280x720) in 26 s. Its table:
objects  frame    FPS   GPU     shadow  graph   other
1000     0.65ms   1529  0.62ms  0.08ms  0.22ms  0.04ms
5000     1.21ms    826  0.78ms  0.26ms  0.62ms  0.13ms
20000    4.20ms    238  1.67ms  0.99ms  2.31ms  0.57ms
60000   13.85ms     72  3.90ms  3.07ms  7.82ms  2.52ms
Three caveats:
(a) The script launches with --render-defaults=on, so it measures the engine defaults: FXAA, NO ray tracing (0 rays), 1 light. The curve is raster-only and says nothing about ray tracing at scale.
(b) Its draws/culled columns print 0 because its regex no longer matches the log. It expects 'N mesh draws, N culled'; the log now says '20 mesh draws (20 indirect), 116400010 triangles submitted, 0 culled on the CPU'.
(c) Against the curve recorded in ENGINE-NOTES (2053 FPS at 1k, 252 at 20k, 76 at 60k): 1k objects is 25% slower (0.49 -> 0.65 ms, a fixed per-frame cost that has grown); 20k and 60k are within about 5%.
At 60k objects (raster; scale_60000.log) the frame is CPU-limited: 13.65 ms frame against 3.86 ms of GPU work. The G-buffer pass costs 7.54 ms of CPU to record, the shadow phase 3.04 ms, and 2.49 ms is unaccounted. 116 M triangles go out in 20 indirect draws, with no level-of-detail reduction.
Extra measurement I added (one run each, the project's own settings so ray tracing is on, 1280x720, 200 frames; scale_rt_1000.log, scale_rt_60000.log):
- 1k objects: 3.33 ms, GPU bound.
- 60k objects: 40.76 ms (24.5 FPS), CPU bound, with only 7.54 ms of GPU work. The shadow-maps phase costs 19.98 ms of CPU (3.04 with RT off), the G-buffer pass 11.53 ms (7.54 with RT off), and 4.69 ms is unaccounted. Turning ray tracing on at 60k objects adds about 27 ms of CPU a frame.
Likely cause, from reading the code, not profiled: RayShadows rebuilds its instance list from scratch every frame. ClearInstances runs, then AddInstance for every object, and each call copies mesh and material references and material parameters into a caster record. BuildTopLevelAS then rebuilds the whole top-level acceleration structure every frame. That structure is the tree of object instances the GPU searches when it traces a ray; it is never updated in place or skipped for objects that did not move.
Also unexplained: at 60k objects only 10.1% of glossy pixels kept their reflection history (99.9% at 1k), and the refusal counters account for only 22.5% of the rest.

### Observations

- All four shots are GPU bound. The engine's own verdict line says so, and the CPU spends 10-16 ms a frame waiting for the GPU. Recording the render graph costs only 0.7-2.1 ms of CPU. The CPU becomes the limit only at large object counts (see scale_test).
- Garage: reflections take 10.7 of the 18.1 ms of GPU time (59%). The reconstruction (the resolve, the accumulator and a chain of three blur passes, together 6.8 ms) costs 1.8 times the tracing itself (ReflectionTrace 3.82 ms) for about one reflection ray per pixel (1.39 M). ReflectionResolve on its own (3.91 ms) costs more than the trace. The three blurs alone (1.97 ms) cost more than the whole opaque lit pass (1.29 ms).
- Garage direct light: DirectTrace costs 3.18 ms for 5.85 M shadow rays. Every traced reflection hit walks all 24 lights (24.0 per hit over 1.36 M hits); RT-11's single-reservoir pick still visits every light to choose one. On the bridge each hit walks 58-70 lights. The cost of shading a ray hit grows linearly with the number of lights, and nothing culls lights for ray hits. WR-10's 'light grid for ray hits' is still open.
- Light clusters barely cull on the bridge. (A cluster is a screen tile's list of the lights that can reach it.) The busiest cluster holds 144-150 of the 189 lights, and fragments average 67 (Headland) and 118 (Glitter) lights, up to 190. Any loop that walks the cluster list costs roughly all of the lights.
- Bridge: the water is the largest single cost. The Transparent pass, which is mostly the sea's forward shading, costs 5.58 ms at Headland (33% of the frame) and 3.32 at Glitter. The sea's own passes (DirectWaterShade, DirectWaterChoose, WaterSurface, WaterFoam, WaterAccumulateLamps) add another 2.6-2.8 ms. That is about 8.4 ms (49%) at Headland and 5.9 ms (45%) at Glitter. The sea still keeps its own copy of the direct-light choose and shade passes beside the shared DirectTrace (RT-8, 'the sea is the one surface with its own copy of every system', is still undone).
- Reflection passes have a large fixed cost that does not follow how many pixels actually reflect. The camp spends 2.46 ms on the reflection passes for 0.14 M reflection rays, and Headland 2.95 ms for 0.13 M. That is ten times fewer rays than the garage for only 3.6-4.4 times less time: the resolve, accumulator and three blurs run over the whole screen whatever the glossy coverage.
- Indirect light is traced only in the camp (GI trace 1.22 ms plus record, re-light and accumulate, about 1.8 ms in all). In the garage and on the bridge, 'RayTracedGiSource: Baked' is honoured: Scene.cpp turns traced GI off once the bake is ready, so both RT-first demo scenes get their bounce light from the bake. The log shows 'global illumination traced' followed a few seconds later by 'screen-space or none'.
- The glass layer runs its own copy of the reflection and direct pipeline. In the garage, 8 Glass* passes cost 0.83 ms plus 0.39 ms of Transparent, just for the car's windows.
- Opaque lit shading costs 2.28 ms at Headland against 1.29 in the garage. An earlier RT-SERIES measurement found the terrain is most of the lit and G-buffer passes on the bridge. On the CPU side, the bridge's G-buffer pass costs 1.0-1.5 ms to record because only 15 of its 385 draws go through the GPU-driven indirect path. In the garage all 280 draws are indirect and recording still costs 0.74 ms.
- The frame has 61-72 passes. Dozens of them cost about 0.002 ms each (ChangeMap, ChangeFilter x3, Record and Relight for every signal); their GPU cost is negligible. GPU work outside named passes is 0.06-0.09 ms. The ray-budget passes (Budget importance/reduce x6/allocate) still run in the scale scene with ray tracing off: tiny, but work for a job that is not there.
- The new vulkan validation defects appear only on the bridge, in the water path, and only with --validation=on: WaterAccumulateLamps pushes 112 bytes of push constants into a 96-byte range, and DirectWaterShade samples R32G32B32A32_UINT images through a linear sampler. The garage and the camp are validation-clean. The timed runs cannot show these errors because the benchmark runs with validation off.
- Ray tracing at scale is the big scaling wall. At 60k objects with ray tracing on, the frame is 40.8 ms and CPU bound, against 7.5 ms of GPU work: the shadow phase costs 20 ms of CPU, apparently rebuilding the ray-tracing instance list and top-level structure from scratch every frame. With ray tracing off the same scene runs at 13.7 ms, where the G-buffer pass's 7.5 ms of CPU recording is the wall. The per-frame transform walk (roadmap 8.15) now compares against the last frame instead of recomputing, and is no longer the largest item.
- Run-to-run drift: the camp rose 7.19 -> 7.65 -> 7.96 ms on the same binary and shot over about 5 minutes. The spread within a shot was 0.26-0.77 ms. Differences under about 1 ms between single runs cannot be trusted; an A,B,B,A palindrome order is needed.
- The garage frame (18.25 ms) is consistent with recent records: 17.12 ms after RT-11 per docs/HANDOFF.md, and 10.0 ms at RT-1 on 2026-09-06. The roughly 8 ms added since RT-1 is mostly in the reflection passes.
- The RT-15 moving-surface exemption in water_accumulate.rvshader changes nothing at Headland: 99.7% of sea pixels keep their history with or without it, per the engine's own counter.
- tools/scripts/bench_scale.py has two gaps. Its draws/culled parser no longer matches the log. And it measures with ray tracing off, so the repo has no standing benchmark of ray tracing at scale.

## 5. What current RT-first renderers do (the reference survey)

A survey of published techniques (papers, GDC, SIGGRAPH and HPG talks, vendor and engine documentation), taken as the target to compare RageV against. The owner's rule stands: nothing is adopted because another engine does it; each technique lists what it would have to prove in RageV.

### 1. Frame structure: visibility buffer or G-buffer versus forward, pass order, resolution strategy, upscaling

**The target.** THE 2026 TARGET. The first thing the camera sees (primary visibility) is still rasterised, not traced, because rasterising camera rays gives the same answer for far less cost. It is rasterised once, driven by the GPU, into a thin buffer. That is either a visibility buffer (per pixel only 'which triangle of which object', about 8 bytes) or a compact G-buffer (depth, normal, roughness, base colour, metalness, motion, surface id). Everything after that is screen-space compute work, in this order: (1) keep the ray-tracing acceleration structures up to date on the async compute queue while the geometry pass runs; (2) trace each lighting signal (direct light and shadows, diffuse GI, reflections, AO) in its own pass from the buffer, at its own resolution; (3) denoise each signal with its own temporal and spatial filter; (4) run one deferred lighting resolve (material times signals), with no second rasterisation of the scene; (5) draw one ray-traced transparent layer and the water on top; (6) run a temporal upscaler to output resolution; (7) post. The internal resolution is 50-75% of the output per axis. For most RT-first games that is where the needed 2-4x comes from. id Tech 8 (Doom: The Dark Ages), Lumen (UE5), Snowdrop (Avatar) and Anvil (AC Shadows) all ship some version of this shape.

RAGEV TODAY, AGAINST IT. (a) No render scale: the frame renders at native resolution, so every per-pixel ray and filter pays for 100% of output pixels. RENDERING-REVAMP's candidate list put 30-45% of the per-pixel cost on this lever. (b) Opaque geometry is drawn twice: after the G-buffer pass, the forward lit pass rasterises every opaque object again and samples its material again (RT-2.2, still open). That doubles geometry and material cost and keeps the 6.5k-line pbr_fragment.glsl at the centre of every frame. (c) The reflection is traced before the lit pass but composed inside it. (d) Every trace and denoise pass (direct_trace, reflection_trace, rtgi_trace, reflection_accumulate, the blurs) is a full-screen fragment pass on the single graphics queue. Fragment passes cannot use groupshared memory, cannot overlap on an async queue and cannot use SER. DIRECTION: under RT, make the G-buffer (or a visibility buffer) the only raster of opaque geometry. Finish the deferred resolve. Add a render scale with a temporal upscaler. Move trace and filter passes to compute so they can overlap.

#### Visibility buffer with a compute material resolve

- **What it does.** Rasterises only depth and a triangle/instance id per pixel, then runs each material's shading once per visible pixel in compute, grouped by screen tiles that share a material, so overdraw never pays for material work.
- **Vulkan on this laptop.** Core Vulkan 1.3 (compute, descriptor indexing, both already used by RageV). VK_EXT_mesh_shader (already enabled in RageV) for cluster culling. VK_EXT_shader_image_atomic_int64 only if a software rasteriser for tiny triangles is added. No vendor lock-in.
- **Rough cost.** The visibility raster is typically well under 1 ms at 1080p for millions of GPU-culled triangles. The resolve costs about what RageV's G-buffer material sampling costs today (G-buffer pass 0.25 ms in the garage, 0.70 ms at Headland, 1600x900) plus a tile classification pass of about 0.1-0.2 ms (estimate). It only pays back where geometry or overdraw dominates.
- **What it must prove in RageV.** RageV's real scenes are measured as fragment- and ray-bound, not geometry-bound (bridge: 2.85 M triangles, about 1 ms CPU for geometry). So this has to earn its place on an AAA-scale fixture: the 60k/120k-object scale scenes plus a dense-foliage scene. Acceptance: the resolve writes G-buffer lanes bit-identical to today's (20 parked frames, per-pixel diff 0), and an A,B,B,A palindrome shows it faster on the garage, Headland and the 120k fixture. If only the dense fixture gains, ship it as a mode, not the default.
- **Sources:** https://jcgt.org/published/0002/02/04/; https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf; https://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/; https://www.gdcvault.com/play/1023109/Optimizing-the-Graphics-Pipeline-With

#### Deferred lighting resolve as the only opaque lit path under RT (RT-2.2 taken to its end)

- **What it does.** One full-screen compute pass reads the G-buffer plus the denoised signals (direct, GI, reflection, AO) and writes the lit colour, so the scene is never rasterised a second time to be lit.
- **Vulkan on this laptop.** Core Vulkan, no extensions.
- **Rough cost.** RageV's own estimate: about 0.6 ms back at Headland (the second rasterisation), more where heavy materials sit near the camera. The resolve pass itself is about 0.4-0.8 ms at 1080p internal (estimate).
- **What it must prove in RageV.** Follow RT-2.2 as filed. First the albedo lane moves off linear R8G8B8A8 (to sRGB8 or 16F) or the darks band. Then: per-pixel diff against the sampled path near zero on the garage, the bridge's three cameras and the camp. Frame time in an A,B,B,A palindrome. Surfaces whose inputs are not in the G-buffer (emissive map, coat wrap, anisotropy tangent, sheen) either get lanes (priced against RT-14's bandwidth measurement) or stay forward; a diff proves which.
- **Sources:** https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf; https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf; C:/Users/ism19/Code/RageV/docs/RT-SERIES.md (RT-2.1, RT-2.2 records)

#### Internal render scale plus a temporal upscaler (TAAU, DLSS Super Resolution, FSR 3.1, XeSS)

- **What it does.** Renders and traces at a lower internal resolution, then rebuilds the output resolution from sub-pixel-jittered frames over time.
- **Vulkan on this laptop.** Own TAAU: no dependency (it extends RageV's taa_resolve). DLSS SR: Vulkan through NGX or Streamline, RTX GPUs only; the SDK licence requires NVIDIA attribution and use only on NVIDIA GPUs. FSR 3.1: MIT licence, any vendor, Vulkan. XeSS: Vulkan supported, runs on other vendors via DP4a. Vendor lock-in applies only to DLSS.
- **Rough cost.** From the DLSS guide (31 March 2026), 1440p output from 720p input on a desktop RTX 5070: 0.43 ms for the older CNN presets (E/F), 1.02 ms for transformer J/K, 1.33-1.87 ms for the newest L/M. The 5070 Ti Laptop is roughly 1.1-1.35x those (estimate). The saving is the pixel ratio on every per-pixel pass: 0.75 per axis = 56% of the pixels, 0.67 = 44%.
- **What it must prove in RageV.** Run a render-scale arm at 0.67 and 0.75 against native. Measure the bridge flicker protocol (check_glint_flicker.py blinking %, the cable band), the garage parked, edge and smear metrics, a per-pixel diff at output resolution against native, and frame time A,B,B,A. Own TAAU first (no lock-in), a vendor upscaler as a second arm. Owner rules: the upscaler resolves geometric aliasing only and is never the accumulator of a lighting signal. MSAA 4x's interaction with a scaled internal target must be measured.
- **Sources:** http://behindthepixels.io/assets/files/TemporalAA.pdf; https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf; https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/; https://developer.nvidia.com/downloads/dlss/license_agreement

#### Each signal at its own resolution, with guided downsample and joint bilateral upsample

- **What it does.** Traces and filters a signal on a coarser grid (half or quarter) using the G-buffer's depth and normal as guides, then upsamples once at the end with edge-aware weights.
- **Vulkan on this laptop.** Core Vulkan.
- **Rough cost.** id Tech 8's GI denoise costs 0.18-0.52 ms and its upscale 0.45-0.72 ms across Series S up to PC 4K. RageV's RT-3.1 cut the GI chain from 0.89 to 0.27 ms.
- **What it must prove in RageV.** Already built for AO and GI. Open: reflections tiered by roughness, and the thin-geometry loss RT-3.1 measured on the bridge cables (-0.577 levels over the cable band). DLSS Ray Reconstruction forbids checkerboard input, so this choice and an RR evaluation are decided together.
- **Sources:** https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf; https://github.com/NVIDIA-RTX/NRD; C:/Users/ism19/Code/RageV/docs/RT-SERIES.md (RT-3.1)

#### Render graph with transient memory aliasing and multi-queue scheduling

- **What it does.** Passes declare what they read and write, so the graph orders them, places barriers, reuses the memory of short-lived targets, and can move independent passes to the async compute queue.
- **Vulkan on this laptop.** Core Vulkan 1.3 (synchronization2 is already required by RageV). NVIDIA exposes separate compute-only and transfer queue families.
- **Rough cost.** CPU cost well under 0.1 ms. The gain is VRAM (transient targets share memory) plus whatever async overlap measures.
- **What it must prove in RageV.** RageV's RenderGraph executes passes in add order, and RT-FIRST notes there is no precedent for sampling a target and then writing it in one frame. Prove three things: render-target VRAM before and after aliasing, read from the allocator; the frame unchanged (diff 0); one pass moved to the async queue measured A,B,B,A.
- **Sources:** https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in

### 2. Direct lighting with many lights, area lights and emissive meshes

**The target.** THE 2026 TARGET. Direct light is sampled, not looped over. A light structure answers 'which lights can reach this point': screen clusters for camera pixels, plus a camera-centred world grid or a light tree for points hit by rays, on or off screen. Each pixel picks a small fixed number of lights by estimated contribution and traces one shadow ray per pick. Picks are steered either by what was visible last frame (MegaLights) or by reservoir reuse across frames and neighbours (ReSTIR DI). The result is demodulated (light without albedo) and denoised. The unshadowed light of a rectangle or tube is evaluated analytically (LTC), and rays only answer 'how much of it is shadowed'. Emissive meshes are real lights the sampler can pick, weighted against BRDF rays (multiple importance sampling, MIS: combining two sampling strategies so neither double counts). Cost is set by rays per pixel, not by light count.

RAGEV TODAY, AGAINST IT. Good: DirectTrace (T5/RT-1) already picks K lights per pixel by contribution, traces one ray each and runs the result through the reconstruction contract. That is structurally close to MegaLights, minus the visibility guide. Gaps: (1) RT-10 (ReSTIR DI) was dropped because stages 2 and 3 moved the picture further from the truth. The mechanisms published integrations name (previous-frame scene for visibility reuse, correct reuse weights) were never built. (2) At ray hits (reflections, GI), RT-11 keeps one light by weighted reservoir. It walks the on-screen cluster cell only when the hit projects on screen and otherwise all lights, which does not scale to thousands of lights. (3) Emissive meshes are capped at 16 rectangles built from bounding boxes (Renderer3D.h kMaxAreaEmitters = 16). Lights met at traced hits are shaded as points even when they are tubes, which is the known tube defect. DIRECTION: a world light structure for hits, emissive triangles as sampleable lights, a visibility guide for DirectTrace, then ReSTIR rebuilt with its missing mechanisms.

#### Fixed rays-per-pixel stochastic direct lighting with visibility guiding (the MegaLights shape)

- **What it does.** Each pixel picks a few important lights, steered away from lights that were shadowed there last frame, traces one ray toward each (a short screen-space trace first, then hardware RT), and a denoiser tuned to that sampling pattern reconstructs the result.
- **Vulkan on this laptop.** VK_KHR_ray_query (already used). No lock-in.
- **Rough cost.** Constant cost set by samples per pixel. Epic's docs say quality, not cost, varies with lighting complexity. Estimate 1.5-3 ms at 1080p internal on this GPU including the denoise. RageV's DirectTrace at K=4 measured 1.32-1.46 ms trace plus about 0.7 ms contract at 1600x900.
- **What it must prove in RageV.** Add only the guide (a per-tile record of which picked lights were unshadowed last frame, used to reshape this frame's pick probabilities with a floor so no light starves) to the existing DirectTrace. Prove on the bridge, where the garage cannot show it (78 lights per pixel, sized lamps; the garage hits the K=8 ceiling). Checks: converged diff against the every-light reference with no structure beyond grain, dolly moving error, emitter_lag.py's moving-light trail (11.9% baseline), cost A,B,B,A. An unbiased guide must converge to the same still picture.
- **Sources:** https://advances.realtimerendering.com/s2025/content/MegaLights_Stochastic_Direct_Lighting_2025.pdf; https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine; https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/lighting-at-scale-bringing-hundreds-of-dynamic-lights-to-mobile-with-unreal-megalights

#### ReSTIR DI: reservoir resampling reused across frames and neighbouring pixels

- **What it does.** Each pixel keeps a tiny 'reservoir' (one chosen light plus the weights that say how representative it is) and merges reservoirs from last frame and nearby pixels, so hundreds of candidate lights are effectively considered for the price of one shadow ray.
- **Vulkan on this laptop.** VK_KHR_ray_query. The RTXDI SDK runs on Vulkan (HLSL compiled to SPIR-V via DXC) on any RT-capable GPU, under the NVIDIA RTX SDKs licence (attribution).
- **Rough cost.** RageV measured about +3 ms for its three stages in the garage and 1.3-1.7 ms on the bridge (RT-10). The HPG 2021 productised version reports up to 7x lower cost than the original paper.
- **What it must prove in RageV.** RT-10's record is the starting point (branch wip/2026-09-21-rt10-and-sea). Cyberpunk's integration notes list exactly what RT-10 lacked: (a) reused visibility must be tested against the previous frame's scene ('consider previous frame TLAS for full control over the bias'); (b) light indices must be translated between frames (RageV already built this for measured change as u_ChangeRolls); (c) validation modes that bypass one stage at a time. Rebuild stage by stage against the 16-ray truth (truth_test.py). Each stage must not increase distance from the truth or bright-speck count. The bridge is the test case. Decide feeding spatial results back into the temporal loop by measurement: the slides show it takes the effective sample count from about 64 to about 65,000, at a correlation cost.
- **Sources:** https://research.nvidia.com/publication/2020-07_spatiotemporal-reservoir-resampling-real-time-ray-tracing-dynamic-direct; http://cwyman.org/presentations/2021_HPG_Productizing_ReSTIR.pdf; https://intro-to-restir.cwyman.org/; https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Cyberpunk_2077_Integration.pdf; https://github.com/NVIDIA-RTX/RTXDI

#### World-space light structure for ray hits: cascaded grid or light tree

- **What it does.** A camera-centred world grid (or a tree of lights) lists which lights can reach each region of space, so a reflection or GI hit anywhere, even off screen, finds its candidates without walking every light.
- **Vulkan on this laptop.** Core compute.
- **Rough cost.** id Tech 8's cascaded light grid: 16x16x16 cells x 8 exponential cascades, up to 64 ids per cell (lights, reflection probes, decals), 30 MB, shared by particles and glass. Build under about 0.3 ms for thousands of lights (estimate). A query is a few fetches per hit.
- **What it must prove in RageV.** WR-10 option A (on-screen cluster at the hit) was a measured null on the bridge because the cost is reading 146 light records per hit, not finding them. At AAA scale the walk grows with light count. Build a synthetic fixture with 2k-10k lights plus the bridge. Checks: hit-shading cost flat against light count, 16-ray truth distance unchanged, the sealed-room leak fixtures still 0.000 (the grid must respect range, never add reach).
- **Sources:** https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf; https://www.cemyuksel.com/research/stochasticlightcuts/realtime_stochastic_lightcuts.pdf; https://dl.acm.org/doi/abs/10.1145/3233305; https://github.com/NVIDIA-RTX/RTXDI

#### Emissive meshes as sampleable lights, with MIS against BRDF rays

- **What it does.** Every emissive triangle (or cluster of triangles) enters the light structure so the sampler can pick it, and a BRDF ray that happens to hit it is weighted against the light pick so the emission is counted once.
- **Vulkan on this laptop.** Core plus VK_KHR_ray_query. VK_KHR_ray_tracing_position_fetch is available on NVIDIA to read triangle positions at a hit without vertex buffers.
- **Rough cost.** Emissive triangle table rebuilt only when emissive geometry changes (about 0.1-0.3 ms when it does, estimate). Picks cost the same as analytic lights. Cyberpunk used BRDF sampling for emissives, with a roughness-based 'poor man's MIS' to keep it cheap.
- **What it must prove in RageV.** This replaces the 16-rectangle bounding-box emitter list (kMaxAreaEmitters) and fixes 'tubes shaded as points at ray hits'. Checks on the garage tubes and the bridge lamp lenses: truth_test.py distance and bright-speck count better than RT-11 on every region, no double count (the RT-11 identity match kept), cost A,B,B,A.
- **Sources:** https://github.com/NVIDIA-RTX/RTXDI; https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Cyberpunk_2077_Integration.pdf

#### Analytic area-light shading (LTC) with stochastic shadows by the ratio estimator

- **What it does.** Computes a rectangle's or tube's unshadowed light exactly from a fitted table, and uses rays only to measure the shadowed fraction, so noise lives only in the shadow.
- **Vulkan on this laptop.** Core.
- **Rough cost.** Two table fetches plus ALU per light-pixel pair. Shadows at the existing one ray per pick.
- **What it must prove in RageV.** This is WR-8 and RT-7's open half. RT-7 found a light's highlight could not draw a mirror reflection because of DistributionGGX's cap, which RT-21 has since fixed, so the premise changed. Prove a roughness window where LTC replaces traced tube reflections with no seam, against the brute-force tube and the 16-ray truth, garage floor and car paint.
- **Sources:** https://eheitzresearch.wordpress.com/415-2/; https://research.nvidia.com/publication/2018-05_combining-analytic-direct-illumination-and-stochastic-shadows

### 3. Global illumination: ReSTIR GI, world-space radiance caches (SHaRC, NRC), DDGI-style probes, Lumen's hardware path, id Tech / Snowdrop / Northlight

**The target.** THE 2026 TARGET, as shipped at 60 fps on consoles. GI rays leave the G-buffer at 1/2 or 1/4 resolution, one per pixel of that grid, and do no shading where they land. The hit looks its answer up in caches: first last frame's lit screen (when the hit is on screen and not hidden), then a world-space radiance cache (a hash grid of about 25 cm cells, graded by distance, filled by a small budget of rays each frame and fed back into itself for multiple bounces), and last a ray-traced probe volume as the far field. The gathered light is stored per pixel (id: 2-band spherical harmonics, which keeps normal-map detail), filtered at trace resolution, and upsampled. id Tech 8's whole GI costs about 1.7-2.1 ms, and its world cache is shared with transparencies and fog. Lumen does the same job with a surface cache (low-resolution material and lighting cards on meshes) and screen probes. Snowdrop traces per pixel and shades hits with an averaged material per mesh. The common idea: never run the full material and light loop at a GI hit.

RAGEV TODAY, AGAINST IT. RT GI traces at half resolution on the contract (RT-3/RT-3.1), which is good. But every GI and reflection hit runs the full lit shader (material, the RT-11 one-light reservoir, the baked field). On the bridge the light walk at hits was once about 40 ms of 114 (WR-10). The indirect light also has two different answers: the baked probe in the garage is about a third brighter than the traced reflection (RT-24 fix 1 found the probe/trace flip under motion). There is no live world cache, so multi-bounce, reflection hits and transparents each get light from somewhere different. DIRECTION: one live world radiance cache feeding GI hits, rough reflection hits, glass, water and fog. The baked field stays as the far-field fallback (owner's answer 4) but becomes a seed, not a competing answer.

#### Cache-lit final gather (the id Tech 8 shape)

- **What it does.** One GI ray per pixel at 1/2 or 1/4 resolution, whose hit is answered by the screen cache, then the world cache, then the probe volume, with zero shading at the hit, stored per pixel as spherical harmonics and filtered and upsampled.
- **Vulkan on this laptop.** VK_KHR_ray_query in compute (or VK_KHR_ray_tracing_pipeline). No lock-in.
- **Rough cost.** id's shipped numbers, mission 4 hotspot. Series X at 1440p: world sampling 0.27, radiance cache 0.21, probe update 0.08, final gather 0.54, denoise 0.18, upscale 0.59 = about 1.9 ms serial, 1.4 ms with async compute. PC RTX 4080 at 4K: about 1.7 ms. The 5070 Ti Laptop has roughly twice the shader rate of a PS5, so about 1.5-2.5 ms at 1080p internal (estimate).
- **What it must prove in RageV.** Against the current half-res RT GI with full hit shading: 16-ray / 400-frame truth distance within grain (bias only where the cache is coarse), sealed-room leak fixtures at 0.000, emitter_lag.py (a moving light's bounce must follow), the lights button settle (RT-16), and cost A,B,B,A. The camp is the realtime-GI scene; the garage bakes its GI.
- **Sources:** https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf

#### World-space radiance cache by spatial hashing (SHaRC; id Tech 8's world cache; surfels as the alternative)

- **What it does.** Stores outgoing light in a 1D hash table keyed by quantised world position (cell size growing with distance), updated from a few percent of pixels' paths each frame, so any ray hit can read multi-bounce light in one lookup.
- **Vulkan on this laptop.** Shader-only (SHaRC ships HLSL/GLSL, Vulkan/SPIR-V supported). Runs on any RT GPU; SHaRC is under the NVIDIA RTX SDKs licence, and id's version is simple enough to write in-house. No hardware lock-in.
- **Rough cost.** SHaRC: 40 bytes per cell (64 with directional data); 2^22 cells is about 160-256 MB. Updates trace about 4% of screen paths per frame. id: 14 MB cache, about 20k entries shaded per frame, reused over N frames, 0.1-0.2 ms.
- **What it must prove in RageV.** Reflection hits on rough surfaces and GI hits read the cache instead of running the lit shader. Checks: truth distance on the garage floor, walls and car within grain; no new leaks (the cache must be queried only when the ray is longer than the cell, SHaRC's own rule); the moving-light trail and light switch (the cache's lag must be bounded by the measured-change map, not left to its accumulation); VRAM read back; the probe-versus-trace brightness gap (about a third today) closing.
- **Sources:** https://github.com/NVIDIA-RTX/SHARC; https://raw.githubusercontent.com/NVIDIA-RTX/SHARC/main/docs/Integration.md; https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf; https://www.ea.com/seed/news/siggraph21-global-illumination-surfels

#### Ray-traced probe volumes (DDGI) as the far field and fallback

- **What it does.** A grid of probes around the camera, each tracing a few dozen rays per update to refresh irradiance and a depth-based visibility term that stops light leaking through walls, looked up by any surface that has no better answer.
- **Vulkan on this laptop.** Core plus ray query. RTXGI (DDGI) supports Vulkan.
- **Rough cost.** id: cascaded volumes 16^3 x 6 cascades plus up to 100 local volumes, interleaved (one cascade and one volume per frame), 64/32/16 rays per probe by platform, octahedral atlas RGB9E5 + RG16F visibility, 64 MB, update pass 0.08 ms. The RTXGI measurement: DDGI query about 0.58 ms against NRC's 1.59 ms.
- **What it must prove in RageV.** RageV's field is baked, not live. A live cascade must match the baked field on the static garage (diff within grain) and beat it where the bake cannot follow: a door, a moving light, a moving large object. Leak fixtures stay at 0.000.
- **Sources:** https://jcgt.org/published/0008/02/01/; https://arxiv.org/pdf/2009.10796; https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf; https://github.com/NVIDIA-RTX/RTXGI

#### Lumen hardware ray tracing (surface cache, screen probes, far field) as the reference budget

- **What it does.** Traces against the scene's hardware BVH, lights hits from a precomputed-per-frame surface cache instead of full materials (full 'hit lighting' is optional and expensive), gathers through downsampled screen probes, and extends range with a far-field representation.
- **Vulkan on this laptop.** Comparison target only (UE); the ideas map onto VK_KHR_ray_query or pipelines.
- **Rough cost.** Epic's budgets: GI + reflections + volumetric fog in 4 ms (60 fps 'High') or 8 ms (30 fps 'Epic') at 1080p internal on current consoles. The ray-traced scene should stay under about 100,000 instances after culling.
- **What it must prove in RageV.** Not adopted as a whole. It is the yardstick: RageV's GI plus reflections plus fog should land in the same 4-5 ms at 1080p internal on this laptop before the RT-first frame fits 60 fps.
- **Sources:** https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf; https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine

#### ReSTIR GI (reservoir reuse of indirect light paths)

- **What it does.** Treats each pixel's traced bounce as a candidate sample and reuses good ones across frames and neighbours, like ReSTIR DI but for indirect light.
- **Vulkan on this laptop.** Ray query or pipeline. RTXDI provides it (Vulkan via DXC).
- **Rough cost.** Heavier than a cache gather: a reservoir buffer (16-32 B/pixel), temporal and spatial passes, and rays for visibility checks. Typically paired with a denoiser that must be retuned for correlated input.
- **What it must prove in RageV.** Twice rejected in RENDERING-REVAMP ('heavy, denoiser-entangled'), and that stands for the game path. Revisit only inside a path-traced tier, after ReSTIR DI works, and with the DLSS-RR guidance applied if RR is used (randomise the temporal reuse, permutation sampling).
- **Sources:** https://research.nvidia.com/publication/2021-06_restir-gi-path-resampling-real-time-path-tracing; https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/RestirGI.md

#### Neural radiance cache (NRC)

- **What it does.** A small neural network trained every frame on a sparse set of long paths predicts outgoing light at any point, so paths can end after one or two bounces.
- **Vulkan on this laptop.** NRC library supports Vulkan (NrcVk.h; needs scalar block layout and DX-layout SPIR-V). Needs tensor cores and is NVIDIA-only; experimental per NVIDIA's guide; RTX SDK licence.
- **Rough cost.** About 192 MB at 1080p (training at 274x154). The RTXGI measurement: full-frame query about 1.59 ms on average. Needs two path passes per frame.
- **What it must prove in RageV.** High-end or path-traced tier only, never the default (vendor lock-in, and the owner's rule that nothing is adopted because big engines do it). Must beat the hash cache on truth distance at equal cost on the camp and garage.
- **Sources:** https://arxiv.org/abs/2106.12372; https://github.com/NVIDIA-RTX/NRC; https://github.com/NVIDIA-RTX/RTXGI/blob/main/Docs/NrcGuide.md

### 4. Reflections and glossy surfaces

**The target.** THE 2026 TARGET. Reflection rays are split by roughness. Mirror-like pixels trace at full internal resolution and shade the hit fully, with next-event estimation (NEE: at the hit, aim a ray at a chosen light instead of hoping the reflection ray finds it) and MIS for emitters. Glossy pixels trace at half resolution, reuse neighbours' rays with a ratio estimator (Stachowiak's stochastic SSR idea, now with hardware rays), and light their hits from the world radiance cache. Very rough pixels are served by the diffuse-like cache or probes. Directions come from bounded VNDF sampling (sampling only the facets the viewer can see, clipped so rough surfaces waste no rays below the horizon). Every ray writes its hit distance. The denoiser uses it to size its blur and to reproject history by the reflected image's own motion (the 'virtual image') on smooth surfaces, by the surface on rough ones, blended by roughness and corrected for curvature. In path-traced modes, primary surface replacement lets the denoiser see the reflected object as if it were the primary surface. id Tech 8 is the conservative end: screen-space reflections first, ray traced only on very smooth surfaces, probes otherwise, about 2 ms, off on consoles.

RAGEV TODAY, AGAINST IT. One ray per texel plus a hit-point resolve (24 taps by preset), traced before the lit pass (RT-4), composed inside the lit shader. Every hit runs the full lit shader. VNDF sampling exists but is off by default, although it cut sparkle 630 to 538 speckles a frame and is correct regardless (RT-23). The sparkle is the rays, not the accumulator (measured). RT-24 is open: a fast spin smears reflections, and after fixes 1 and 2 the remaining spread is taa_resolve under motion. Tube reflections are noisy while moving because memory is short by design. DIRECTION: bounded VNDF on by default; cache-lit glossy hits; hit-distance-driven blur and reprojection; and a written contract for how a denoised reflection passes the final temporal resolve (pillar 5).

#### Roughness-tiered reflection rays with neighbour ray reuse

- **What it does.** Mirror pixels get full-resolution rays and full hit shading, glossy pixels get half-resolution rays whose results neighbours share through a ratio estimator, and rough pixels read caches, so rays go where they change the picture.
- **Vulkan on this laptop.** VK_KHR_ray_query, or VK_KHR_ray_tracing_pipeline plus SER (pillar 8) for the divergent mirror-hit shading.
- **Rough cost.** id: about 2 ms for smooth-only RT reflections at 1/2-1/4 resolution. Estimate for the full tiered set at 1080p internal on this laptop: 2-3 ms including the denoise.
- **What it must prove in RageV.** Against today's one-ray-plus-resolve path: truth_test.py distance and speck count per region (wet floor, car body, cube, poles), spin_measure.py swing scores (pole spot, bumper spot) moving against settled, cost A,B,B,A. Tier thresholds are one global render setting, not per-material dials.
- **Sources:** https://www.ea.com/frostbite/news/stochastic-screen-space-reflections; https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf; https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf

#### Bounded VNDF sampling of reflection directions

- **What it does.** Samples reflection directions only among microfacets visible to the viewer, with the sampling range clipped so fewer directions fall below the surface and get thrown away, which lowers noise on rough surfaces for free.
- **Vulkan on this laptop.** Shader maths only.
- **Rough cost.** A few ALU per ray; saves rejected samples.
- **What it must prove in RageV.** RV_REFLECTION_VNDF is in the tree and off. RT-23 measured it cut sparkle 14%, though it 'made it worse' on one arm (the moving-car speckle). Add the bounded variant, then run the 16-ray truth (it must converge to the same picture: unbiased) plus the live car pass, and switch it on by default if both hold.
- **Sources:** https://jcgt.org/published/0007/04/01/; https://gpuopen.com/download/Bounded_VNDF_Sampling_for_Smith-GGX_Reflections.pdf

#### Hit-distance-aware reflection denoising with virtual-image reprojection and curvature correction

- **What it does.** The denoiser uses each ray's hit distance to decide how far to blur (a near hit gives a sharp contact reflection, a far hit a wide one) and to find where the reflected image was last frame, blending image motion and surface motion by roughness and correcting for curved reflectors.
- **Vulkan on this laptop.** Shader work. NRD ships HLSL compiled for Vulkan (NRI layer), under the NVIDIA RTX SDKs licence, on any vendor.
- **Rough cost.** NRD ReBLUR diffuse+specular: 2.55 ms at 1440p native on an RTX 4080. Scaled to 1080p internal on the 5070 Ti Laptop, about 2.3-2.9 ms for both signals (estimate). RageV's own contract is about 0.7 ms per signal at 1600x900.
- **What it must prove in RageV.** RageV already has a virtual-image lane (RT-6.1) and hit-identity tests (RT-6.10, RT-17). The gaps are (a) curvature: the NRD README warns reflections on curved surfaces reproject wrongly without it, and the car and chrome poles are exactly the RT-24 suspects; (b) the blur radius from hit distance. Checks: spin_measure.py with --stage=many16 (the one test that separates ghost from grain), pole spot and bumper spot against settled, parked metrics unchanged.
- **Sources:** https://github.com/NVIDIA-RTX/NRD; https://link.springer.com/chapter/10.1007/978-1-4842-7185-8_49; https://zheng95z.github.io/publications/trmv21; https://sites.cs.ucsb.edu/~lingqi/publications/paper_trmv.pdf

#### Primary surface replacement (PSR) and specular motion vectors

- **What it does.** For mirrors and clear glass, follows the reflection to the first non-mirror surface and writes that surface's depth, normal and motion into the denoiser's guides, so the filter works on the reflected object as if the camera saw it directly.
- **Vulkan on this laptop.** Shader work. DLSS-RR accepts a specular motion vector buffer, or a specular hit distance plus view and projection matrices.
- **Rough cost.** One extra buffer set plus the chain of mirror bounces (usually 1-2).
- **What it must prove in RageV.** Only for mirror-like pixels. Bevy's Solari notes PSR assumes flat mirrors and fails on curved ones, and its author saw no benefit from specular motion vectors with DLSS-RR. So this is an arm, not an assumption: the garage floor and car windows on spin_measure.py, with the curvature case measured on the poles.
- **Sources:** https://developer.nvidia.com/blog/rendering-perfect-reflections-and-refractions-in-path-traced-games/; https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md; https://jms55.github.io/posts/2026-04-12-solari-bevy-0-19/

#### Emitters in reflections by NEE with MIS (already in RageV, RT-11)

- **What it does.** Beside each reflection ray, aim one ray at an emitter chosen by its worth to the pixel, and combine the two so mirrors take the lamp from the mirror ray and rough surfaces from the aimed one.
- **Vulkan on this laptop.** Ray query.
- **Rough cost.** RT-11 measured -1.75 ms net (the aim +0.70, one light per hit -2.25).
- **What it must prove in RageV.** Keep it. It gains most once emissive meshes are real lights (pillar 2), which removes the bounding-box rectangles and the 'lamp shadows its own aim' class of bug RT-11 found.
- **Sources:** C:/Users/ism19/Code/RageV/docs/RT-SERIES.md (RT-11); https://intro-to-restir.cwyman.org/

### 5. Denoising and temporal reconstruction: NRD ReBLUR / ReLAX / SIGMA, DLSS Ray Reconstruction and other neural denoisers, motion for reflections, ghosting under fast camera motion (RT-24)

**The target.** THE 2026 TARGET. Each noisy signal has exactly one temporal owner: its denoiser. That owner does six things. (1) Reprojection by the right motion: the surface's for diffuse, the reflected image's for smooth specular, blended by roughness and curvature, with a confidence. (2) Geometric disocclusion tests on depth, normal and surface id. (3) Two histories, a long one for a clean still picture and a short 'fast' one of a few frames; the long history is clamped to the neighbourhood of the fast one, which removes ghosts under motion without making the still image noisy (NRD's fast history). (4) An anti-lag that measures real change instead of guessing (A-SVGF temporal gradients, or an external confidence input). (5) A 'history fix': a spatial fill for pixels with a young history, sized by hit distance. (6) A demodulated signal, so texture never blurs. The final temporal upscaler then only resolves geometric aliasing. For pixels whose colour is dominated by a view-dependent signal it either receives that signal's motion (DLSS-RR takes specular motion vectors) or is told to keep a short memory there (FSR's reactive and transparency-and-composition masks), so two long memories never run in series. The alternative is DLSS Ray Reconstruction: one learned model replaces every denoiser plus TAA plus the upscaler, fed raw noisy colour and guide buffers.

RAGEV TODAY, AGAINST IT. The contract (T4) is per signal, with G-buffer validation (RT-5, RT-6.x), measured change (A-SVGF-style, docs/RT-MEASURED-CHANGE.md), a motion-capped memory, identity tests (RT-17) and a virtual-image lane read by TAA (RT-6.1). What is missing: a fast-history clamp; a hit-distance-sized history fix (the young blur was retired as the wrong tool); curvature in the specular reprojection; and a rule for the TAA's memory on specular pixels. RT-24's own bisection puts the remaining spread in taa_resolve under motion (moving feedback 0.9, about ten frames, plus its box). RT-16 already found two memories in series compound (accumulator 64 frames, then TAA still-feedback 0.98). DIRECTION: add the fast-history clamp to the accumulator; give specular-dominated pixels a short TAA memory driven by the accumulator's own confidence; and run DLSS-RR as a measured A/B arm, not an assumption.

#### NRD: ReBLUR and ReLAX (radiance) and SIGMA (shadows)

- **What it does.** NVIDIA's open-source real-time denoisers: recurrent temporal accumulation plus hit-distance-guided spatial blurs, with anti-lag, fast history, history fix and confidence inputs; SIGMA is a dedicated per-light shadow denoiser.
- **Vulkan on this laptop.** D3D12, Vulkan, D3D11 through its NRI layer. Runs on any vendor's GPU. NVIDIA RTX SDKs licence (source available, attribution).
- **Rough cost.** RTX 4080 at 1440p native: ReBLUR diffuse+specular 2.55 ms (3.40 in SH mode), ReLAX 3.25 ms, SIGMA shadow 0.40 ms. On the 5070 Ti Laptop at 1080p internal, about 2.3-2.9 ms for ReBLUR and about 0.35-0.45 ms per SIGMA (estimates: pixel ratio 0.56, compute roughly half a 4080's, bandwidth about equal).
- **What it must prove in RageV.** As a reference arm, not a replacement by default: wire ReBLUR behind the same inputs the contract takes and compare on the RT-24 swing test (spin_measure.py --stage=many16), parked stats, the light switch settle and emitter_lag.py. Where NRD wins, lift its mechanism into the contract. The inputs NRD's README requires (2.5D or 3D motion, normalised hit distance, material ids) are also the checklist for RageV's own lanes.
- **Sources:** https://github.com/NVIDIA-RTX/NRD; https://raw.githubusercontent.com/NVIDIA-RTX/NRD/master/README.md; https://link.springer.com/chapter/10.1007/978-1-4842-7185-8_49; https://github.com/NVIDIA-RTX/NRD/blob/master/LICENSE.txt

#### DLSS Ray Reconstruction (neural denoise + upscale in one model)

- **What it does.** A tensor-core model takes raw noisy RT colour plus guides (diffuse and specular albedo, normals, roughness, depth, motion, optionally specular motion vectors or hit distance) and outputs a denoised, upscaled, anti-aliased frame.
- **Vulkan on this laptop.** Vulkan via NGX or Streamline (nvpro sample vk_denoise_dlssrr). RTX GPUs only, so full vendor lock-in; the DLSS licence requires attribution and NVIDIA-only use. No dynamic resolution. DLSS 4.5 (2026) moved RR to a second-generation transformer.
- **Rough cost.** DLSS-RR guide, Performance mode (quarter-pixel input), desktop RTX 5070: 1.50-1.59 ms at 1080p output, 2.58-2.72 ms at 1440p. The 5070 Ti Laptop is about 1.7-2.1 ms and 2.9-3.7 ms (estimate). VRAM 121-154 MB at 1080p, 212-268 MB at 1440p. It replaces the per-signal denoisers (about 3-4 ms) and the upscaler (about 1 ms), so it is roughly cost-neutral.
- **What it must prove in RageV.** Conflicts on record: it is a single shared temporal system, against 'every signal its own accumulator', and it demands uncorrelated samples. The guide says to avoid checkerboard, screen-space dithering, shared sampling patterns and correlated hashes, and to randomise ReSTIR reuse; RageV uses low-discrepancy sequences and an R2 rotation at the texel index. So it can only be an A/B arm on the owner's instruments: spin_measure.py, the lights button settle, emitter_lag.py, bridge glint flicker, 16-ray truth. Never the only path, and OpenGL and other vendors keep the contract.
- **Sources:** https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md; https://github.com/NVIDIA/DLSS; https://github.com/nvpro-samples/vk_denoise_dlssrr; https://www.nvidia.com/en-us/geforce/news/dlss-4-5-ray-reconstruction-1000-rtx-games-apps-out-now/; https://developer.nvidia.com/downloads/dlss/license_agreement

#### Fast-history clamp (short history bounding the long one)

- **What it does.** Keeps a second accumulation of only a few frames per signal and clamps the long history into the colour range the short one has seen, so an old image cannot survive long after the scene moved while a still pixel still converges over many frames.
- **Vulkan on this laptop.** Shader work (one extra history lane).
- **Rough cost.** One extra RGBA16F history plus a clamp: about 0.1-0.2 ms per signal at 1080p (estimate). VRAM about 16 MB per signal at 1080p.
- **What it must prove in RageV.** Not in the contract today (its bounds are the fresh neighbourhood and the temporal moments). It is the most direct published answer to RT-24's 'ghost turns into a spreading blur'. Measure on spin_measure.py --stage=many16 (pole spot 8.0 after fix 2, memory-off 7.8: the target is the memory-off number with the memory on), the after-stop rows (after0/2/5) for the late settle, and parked stats unchanged. The half-float truncation rule applies: write the fast history through include/half_float.glsl.
- **Sources:** https://raw.githubusercontent.com/NVIDIA-RTX/NRD/master/README.md; https://github.com/NVIDIA-RTX/NRD

#### Measured-change anti-lag (A-SVGF temporal gradients) on every signal

- **What it does.** Re-lights a sparse set of last frame's samples with this frame's scene and the same random numbers, so any difference is real change, never noise, and shortens memory only where and by how much it changed.
- **Vulkan on this laptop.** Shader work.
- **Rough cost.** RageV's record pass holds four RGBA32F lanes at 1/9 of pixels (about 20 MB at 1600x900), plus a re-light at the same rate.
- **What it must prove in RageV.** Built for direct light and reflections (RT-16 closed with it; RT-23 was a re-light mismatch). Extend to GI and AO, and feed its map to the final temporal resolve (RT-22 already does this for reflections). Checks: the lights button settle, emitter_lag.py (the trail's remaining third is the direct light's history), and a parked still that must not un-settle.
- **Sources:** https://cg.ivd.kit.edu/publications/2018/adaptive_temporal_filtering/adaptive_temporal_filtering.pdf; C:/Users/ism19/Code/RageV/docs/RT-MEASURED-CHANGE.md

#### History fix: a spatial fill for young histories, sized by hit distance

- **What it does.** For the first few frames after a pixel's history is refused, blurs it from same-surface neighbours with a radius set by the signal's own footprint (hit distance and roughness), then fades the blur out as history builds.
- **Vulkan on this laptop.** Compute with groupshared memory (fragment passes cannot use it).
- **Rough cost.** Runs only where history is young: typically 0.1-0.4 ms (estimate).
- **What it must prove in RageV.** RageV retired its young blur because a fixed radius smeared hard shadow edges (T5) and spread outliers (RT-23). The difference to prove is the footprint-driven radius, plus outliers bounded before the fill (RT-23's lesson). Checks: RT-24's tube noise while moving, spin_measure.py grain versus ghost, and T5's shadow-edge test on the direct light.
- **Sources:** https://raw.githubusercontent.com/NVIDIA-RTX/NRD/master/README.md; https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Cyberpunk_2077_Integration.pdf

#### Final temporal resolve contract: specular motion plus a reactive / confidence mask

- **What it does.** Tells the last temporal filter (TAA or upscaler), per pixel, how the visible shading moves and how much to trust its own history, so a denoised reflection or a changing light is not held for a second long memory.
- **Vulkan on this laptop.** Shader work. FSR 3.1 (MIT, Vulkan) documents the masks; DLSS-RR documents the specular motion input.
- **Rough cost.** One lane written by the accumulator; the TAA reads one more texture.
- **What it must prove in RageV.** This targets RT-24's remaining spread (taa_resolve's moving feedback 0.9 and box on shiny pixels) and RT-16's memories in series. Drive the TAA's feedback on a pixel from the reflection accumulator's own confidence and history length (already produced, RT-9's lane), not from a surface type: the owner rejected a water exemption as a bad fix. Checks, all four: spin_measure.py, the car's stop, the bridge cables and the bridge water (check_glint_flicker.py 0.80% baseline).
- **Sources:** https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/; http://behindthepixels.io/assets/files/TemporalAA.pdf; https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md

#### Other neural denoisers (AMD FSR Ray Regeneration)

- **What it does.** AMD's machine-learning ray-tracing denoiser inside FSR 'Redstone', paired with a neural radiance cache.
- **Vulkan on this laptop.** Requires AMD RDNA 4, so it is unavailable on the target NVIDIA laptop.
- **Rough cost.** Not applicable on this hardware.
- **What it must prove in RageV.** Note only. It shows neural denoising is now vendor-split (DLSS-RR on NVIDIA, Ray Regeneration on AMD), which is an argument to keep RageV's own contract as the portable path.
- **Sources:** https://gpuopen.com/amd-fsr-rayregeneration/; https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/

### 6. Transparency, glass, water and volumetrics under RT

**The target.** THE 2026 TARGET. The nearest transparent surface (glass or water) is one extra G-buffer layer. Its reflections, refraction and direct light run through the same trace and denoise passes as opaque surfaces, and its motion, material and id lanes are real, so no temporal filter reads the surface behind it. Panes further back stay forward with cheaper lighting. Refraction rays are fired only where the answer can show: Fresnel transmission times absorption above a threshold. Particles use weighted-blended order-independent transparency (a blend that needs no sorting) and are lit from a froxel irradiance volume. A froxel is a small box of a camera-aligned 3D grid; a froxel volume is that grid holding lighting or fog. Participating media (fog, mist) live in froxels: per-froxel lighting from the light lists, one ray toward the dominant light for shafts, temporal reprojection with clamps, flashing emitters resetting history. id Tech 8 fills a 50 MB froxel irradiance volume during its fog pass and reuses it for glass and particles. NVIDIA's RTX Volumetrics applies ReSTIR to scattering.

RAGEV TODAY, AGAINST IT. The glass layer (RT-13) is done and on by default. The water still has its own passes: RT-8's jobs 2 and 3 were dropped and the sea is not a G-buffer layer. The TAA marks the water as 'covered' by the transparent pass, which the owner rejected handling by surface type (RT-22: glitter blinking 0.80% to 0.97%). Fog is analytic only; WR-11 froxels are not built. Refraction rays are gated by Fresnel and reach (WR-18), which is good. DIRECTION: the water as a G-buffer layer, done properly this time; froxels (WR-11) with RT visibility, lit from the same world cache as GI; transparents lit from the froxel volume.

#### One ray-traced transparent layer in the G-buffer (glass and water alike)

- **What it does.** The nearest transparent surface writes depth, normal, roughness, motion and id into its own lanes, so its reflection, refraction and direct light use the shared signal passes and the temporal filters see its real motion.
- **Vulkan on this laptop.** Core.
- **Rough cost.** RageV's glass layer costs +0.45 ms at the owner's shot. A sea layer would be larger because the sea covers most of the bridge frame (estimate 1-2 ms at 1080p internal).
- **What it must prove in RageV.** RT-8 is where the sea 'bit every session'. The acceptance is the bridge's three cameras under the flicker protocol (check_glint_flicker.py), the cables unchanged, and the RT-22 'covered' rule becoming unnecessary because the sea writes its own lanes. The DLSS-RR guide recommends blending albedo, normal and roughness guides toward strongly reflecting water; the same applies to RageV's own guides.
- **Sources:** https://developer.nvidia.com/blog/rendering-perfect-reflections-and-refractions-in-path-traced-games/; https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md; C:/Users/ism19/Code/RageV/docs/RT-SERIES.md (RT-8, RT-13)

#### Refraction rays gated by transmission, with a screen-space fallback

- **What it does.** Fires a refraction ray only where (1 - Fresnel) times absorption through the water or glass leaves a visible answer, otherwise uses the screen or the lit backdrop.
- **Vulkan on this laptop.** Ray query.
- **Rough cost.** RageV measured -3 to -20 ms lossless on the sea cameras (WR-18).
- **What it must prove in RageV.** Done. Keep it inside the water-as-G-buffer rework, and re-verify that the gate's threshold is one global setting.
- **Sources:** C:/Users/ism19/Code/RageV/docs/RENDERING-REVAMP.md (WR-18); https://developer.nvidia.com/blog/rendering-perfect-reflections-and-refractions-in-path-traced-games/

#### Froxel volumetrics with RT visibility (and ReSTIR for scattering at the high end)

- **What it does.** Lights a camera-aligned 3D grid of fog cells from the light lists, traces one ray per cell toward the dominant light for real shafts, reprojects over time with clamps, and integrates front to back; transparents sample the same volume.
- **Vulkan on this laptop.** Core compute plus ray query. RTX Volumetrics is NVIDIA's ReSTIR-based system (RTX Remix).
- **Rough cost.** 1-1.5 ms class (WR-11's shipped-precedent figure). id's froxel irradiance volume is 50 MB (25 MB low spec).
- **What it must prove in RageV.** WR-11's own gate (the owner's eye on the bridge: lamp halos, tower tops swallowed, shafts from the floods). Plus: fog off must be byte-identical; the beacon flash must not ghost (history reset); the froxel lighting reads the same world cache as GI, measured against the analytic fog for energy.
- **Sources:** https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf; https://developer.nvidia.com/blog/nvidia-rtx-advances-with-neural-rendering-and-digital-human-technologies-at-gdc-2025/; C:/Users/ism19/Code/RageV/docs/RENDERING-REVAMP.md (WR-11: Wronski 2014, Hillaire 2015)

#### Weighted-blended OIT for particles, lit from the froxel irradiance volume

- **What it does.** Blends overlapping transparent particles without sorting, and lights them with one volume lookup instead of per-particle light loops.
- **Vulkan on this laptop.** Core.
- **Rough cost.** One volume fetch per particle fragment.
- **What it must prove in RageV.** RageV already has weighted OIT (particle_weighted, oit_resolve). What changes is the lighting source: prove against today's particle lighting on the camp fire and the bridge (energy within grain, no flicker under TAA).
- **Sources:** https://jcgt.org/published/0002/02/09/; https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf

### 7. Geometry at scale for rays: BLAS/TLAS strategy, refit versus rebuild, instancing, LOD for rays, cluster acceleration structures (RTX Mega Geometry), opacity micromaps, displaced micro-mesh status

**The target.** Terms first. A BVH (bounding volume hierarchy) is the tree of boxes the RT cores walk to find what a ray hits. A BLAS (bottom-level acceleration structure) is one mesh's tree. A TLAS (top-level acceleration structure) is the tree of all instances pointing at BLASes. A refit moves boxes without re-sorting the tree: cheap, but it degrades as things move. A rebuild re-sorts from scratch.

THE 2026 TARGET. Static BLASes are built for fast tracing and compacted (up to about 50% less memory) into pooled memory. Deforming BLASes are refitted and rebuilt periodically or after large deformation, on the async queue. The TLAS is rebuilt every frame (NVIDIA's guidance), or at 100k+ instances it is a partitioned TLAS where only changed partitions rebuild. Rays use LOD by ray type: secondary and far rays trace coarser BLASes or proxies selected by instance masks, texture detail comes from ray cones, and Lumen has a far field. On NVIDIA the high end is cluster-based BLASes that follow the raster's cluster LOD (RTX Mega Geometry). Alpha-tested foliage uses opacity micromaps, so the hardware settles most transparent or opaque micro-triangles without shader code. Hair uses linear swept spheres on Blackwell.

RAGEV TODAY, AGAINST IT. Static BLASes are built with PREFER_FAST_TRACE but never compacted. The TLAS is refitted for up to 64 frames (kRefitLimit) rather than rebuilt. Instance masks are written into the TLAS and then ignored (every query passes 0xFF), so the proxy-BLAS idea for far shadow rays (RENDERING-REVAMP) has nothing to select with yet. There is no LOD chain (NEXT row 5), cutout materials are not in the RT path yet (the next task per memory), and there is no BVH streaming. DIRECTION: compaction and pooled BLAS memory; measure TLAS rebuild against refit at scale; honour instance masks for proxies and ray-type LOD; build the LOD chain before any cluster BVH; OMM for cutouts.

#### BLAS policy: fast-trace static builds, compaction, pooled memory, dynamic refit with periodic rebuild on async compute

- **What it does.** Builds static meshes once for trace speed and shrinks them by compaction; refits skinned or deforming meshes each frame but rebuilds them on a schedule or after large deformation, off the graphics queue.
- **Vulkan on this laptop.** VK_KHR_acceleration_structure (compaction via VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR plus a compacted-size query and copy).
- **Rough cost.** Compaction: up to about 50% of static BVH memory (NVIDIA). A dynamic BLAS may be compacted once and then only refitted (Indiana Jones did this for dynamic BLASes).
- **What it must prove in RageV.** BVH VRAM read back before and after on the bridge and garage; pixel-identical frame (diff 0); trace time A,B,B,A (compaction can also speed tracing). Skinned: refit-only against periodic rebuild, trace time over a 60 s animation.
- **Sources:** https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/; https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass/

#### TLAS: rebuild every frame versus refit, and partitioned TLAS at 100k+ instances

- **What it does.** Rebuilds the scene-level tree each frame for best trace speed, or with the partitioned extension rebuilds only the parts of the scene that changed.
- **Vulkan on this laptop.** VK_KHR_acceleration_structure. VK_NV_partitioned_acceleration_structure (NVIDIA-only; RTX Mega Geometry family; NVIDIA sample with 100K+ physics objects).
- **Rough cost.** A full TLAS rebuild of tens of thousands of instances is a fraction of a millisecond on the GPU. Epic advises fewer than about 100,000 RT instances on consoles.
- **What it must prove in RageV.** RageV chose refit because it was 'markedly cheaper' at a couple of hundred instances; NVIDIA recommends the opposite for the TLAS. Measure both on the 60k/120k-object fixtures and the bridge: build time plus total trace time of all RT passes (refit degrades traversal) in an A,B,B,A palindrome. Partitioned TLAS only if a 120k fixture shows the rebuild dominating.
- **Sources:** https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/; https://docs.vulkan.org/features/latest/features/proposals/VK_NV_partitioned_acceleration_structure.html; https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine

#### LOD for rays: instance masks, proxy BLASes, ray-type LOD, ray cones

- **What it does.** Lets shadow, GI and far reflection rays trace simpler stand-in geometry chosen by instance mask, and picks texture detail at hits from the ray's spreading footprint (a ray cone).
- **Vulkan on this laptop.** Core: instance mask (8 bits) in VkAccelerationStructureInstanceKHR and the cullMask argument of rayQueryInitializeEXT.
- **Rough cost.** Proxy rays cost a fraction of full-detail rays through thin steel (RageV estimate for the bridge's far shadows). Ray cones: a few ALU.
- **What it must prove in RageV.** Honour the masks RageV already writes. For the bridge's far shadow rays against a slab/box proxy: shadow diff against full geometry (the big-blocker case must match), trace time, Balanced preset speckle gone. Owner rule: the choice of proxy by distance is one global setting.
- **Sources:** https://jcgt.org/published/0010/01/01/; https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine; C:/Users/ism19/Code/RageV/docs/RENDERING-REVAMP.md (proxy BLAS section)

#### Cluster acceleration structures (RTX Mega Geometry)

- **What it does.** Builds ray-tracing structures from small pre-built triangle clusters (about 128 triangles) that match a cluster-LOD raster (Nanite-style), so very dense or animated geometry can be traced at the detail the raster shows, with builds up to 100x faster.
- **Vulkan on this laptop.** VK_NV_cluster_acceleration_structure (driver 572.16+, all RTX GPUs, hardware-accelerated cluster intersection on Blackwell), plus VK_NV_partitioned_acceleration_structure. NVIDIA-only extensions: vendor lock-in; a KHR/EXT path is needed as fallback.
- **Rough cost.** Alan Wake 2 update 1.2.8: about 13% higher FPS and about 1 GB less VRAM on an RTX 4090 (Tom's Hardware test); Remedy's figures were 5-20% FPS and about 300 MB.
- **What it must prove in RageV.** Precondition: a cluster LOD chain for raster (NEXT row 5, not started) and meshlets (pbr_meshlet exists). Only then an arm: BVH VRAM, build time and trace time on a dense fixture against per-mesh BLASes, with a KHR fallback kept bit-identical on non-NVIDIA hardware.
- **Sources:** https://docs.vulkan.org/features/latest/features/proposals/VK_NV_cluster_acceleration_structure.html; https://github.com/nvpro-samples/vk_lod_clusters; https://github.com/nvpro-samples/vk_animated_clusters; https://developer.nvidia.com/blog/nvidia-rtx-mega-geometry-now-available-with-new-vulkan-samples; https://www.tomshardware.com/pc-components/gpus/testing-nvidias-rtx-mega-geometry-tech-vram-reducing-tech-a-leap-forward-for-path-traced-rendering

#### Opacity micromaps (OMM) for alpha-tested geometry

- **What it does.** Stores a tiny per-triangle opacity map in the acceleration structure so the hardware settles fully opaque and fully transparent micro-triangles itself, and only 'unknown' ones reach the shader's alpha test.
- **Vulkan on this laptop.** VK_EXT_opacity_micromap (NVIDIA Ada/Blackwell), and VK_KHR_opacity_micromap (Vulkan 1.4.351, 8 May 2026; in NVIDIA developer beta drivers). Works with ray queries: the candidate loop simply sees fewer non-opaque candidates. The OMM SDK bakes the maps.
- **Rough cost.** Indiana Jones on an RTX 5080: TraceMain pass 7.90 ms down to 3.58 ms. Memory: a few bits per micro-triangle.
- **What it must prove in RageV.** Build the cutout-materials task (the RT half inside the ray-query loop) first without OMM. Then OMM as the arm: shadow and reflection diff against the shader alpha test must be zero where the map is exact (4-state mode keeps 'unknown' exact), and foliage-heavy trace time A,B,B,A.
- **Sources:** https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_opacity_micromap.html; https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_opacity_micromap.html; https://github.com/NVIDIA-RTX/OMM; https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass/

#### Displaced micro-meshes (deprecated) and linear swept spheres for hair

- **What it does.** DMM compressed displaced geometry in the BVH on Ada; NVIDIA has dropped it for Mega Geometry. LSS is a Blackwell hardware primitive for strands (hair, fur).
- **Vulkan on this laptop.** VK_NV_displacement_micromap: deprecated, SDK archived, no longer available. Do not build on it. VK_NV_ray_tracing_linear_swept_spheres: Blackwell-only.
- **Rough cost.** Not applicable (DMM). LSS: cheaper and smaller than triangle hair (NVIDIA).
- **What it must prove in RageV.** DMM: skip. LSS: only if a scene needs traced hair, measured against triangle strands, with a triangle fallback for non-Blackwell hardware.
- **Sources:** https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_ray_tracing_linear_swept_spheres.html; https://github.com/NVIDIAGameWorks/Displacement-MicroMap-SDK; https://registry.khronos.org/vulkan/specs/latest/man/html/VK_NV_displacement_micromap.html

### 8. GPU execution: shader execution reordering, inline ray queries versus RT pipelines, bindless, async compute, work graphs

**The target.** THE 2026 TARGET. Uniform ray work (shadow and AO visibility, cache probes) runs as compute with inline ray queries. Divergent hit shading (mirror reflections, GI hits that do shade, path tracing) runs through ray-tracing pipelines with shader execution reordering (SER: the GPU regroups threads by what they hit so similar shading runs together), which NVIDIA, Intel and soon AMD support. Resources are fully bindless (any shader indexes any texture or buffer), now moving to Vulkan's new descriptor heap. BVH builds, denoisers and cache updates overlap raster on the async compute queue; id saved about 0.4-0.5 ms on consoles this way and says it could not have hit 60 Hz without it. Draws are GPU-driven: indirect, mesh shaders, device-generated commands. Shader or pipeline failures are loud, and permutations are compiled ahead or cached.

RAGEV TODAY, AGAINST IT. Ray queries only (VK_KHR_ray_query; no pipeline, so no SER). All trace and denoise passes are full-screen fragment passes, which lose groupshared memory for blurs, cannot reorder, and run 2x2 quads where compute would run exact threads. There is one graphics queue (no compute queue requested). Bindless exists (descriptor indexing), but per-pass set rules are fragile: Commit errors if a set was bound earlier in the frame (RT-FIRST T5 notes). A swallowed shader compile failure silently changed the picture by 14 levels and is still open (RT-11). GPU culling, indirect draws and a meshlet stage exist. DIRECTION: trace and filter passes to compute; RT pipelines with SER for divergent hits; a compute queue; fail-loud shader compilation.

#### Inline ray queries in compute for uniform work, RT pipelines for divergent shading

- **What it does.** Uses ray queries inside compute shaders when every hit is handled the same way (visibility, cache lookup), and ray-generation plus hit shaders when hits need different material code.
- **Vulkan on this laptop.** VK_KHR_ray_query (used today) and VK_KHR_ray_tracing_pipeline (available on the 5070 Ti; RageV would need a shader binding table, the table that maps hits to shaders). No lock-in.
- **Rough cost.** Compute instead of fragment: the gain is groupshared memory in filters (id's bilateral passes preload to shared FP16), no quad overdraw, and async eligibility. Typically a few tenths of a ms per filter chain (estimate).
- **What it must prove in RageV.** Port one chain (the direct light: trace, accumulate, blurs) from fragment to compute. It must be pixel-identical (diff 0; the half-float write path through include/half_float.glsl must be kept, since compute imageStore rounding may differ from render-target writes, so verify). Then time A,B,B,A. RT pipelines only for passes whose hits are divergent (mirror reflections, a path-traced tier), gated on SER's measured gain.
- **Sources:** https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/; https://developer.nvidia.com/blog/rtx-best-practices/

#### Shader execution reordering (SER)

- **What it does.** Before shading a hit, the ray-generation shader asks the GPU to regroup threads by a key (what was hit, which material), so threads that run the same code sit together.
- **Vulkan on this laptop.** VK_EXT_ray_tracing_invocation_reorder (Vulkan 1.4.333, Nov 2025; NVIDIA RTX 40/50 accelerated, RTX 20/30 accept it as a no-op; Intel Arc B; AMD coming) and the older VK_NV_ray_tracing_invocation_reorder. reorderThreadEXT is available only in ray-generation shaders, so RageV's fragment-shader ray queries cannot use it. hitObjectRecordFromQueryEXT can wrap a ray-query result into a hit object inside a raygen shader.
- **Rough cost.** Published gains: Black Myth: Wukong 15.10 to 4.08 ms (3.7x) on a path-traced pass; Alan Wake 2 RT cost 16.8 to 10.2 ms (-39%); Indiana Jones 4.07 to 3.08 ms (-24%); vk_gltf_renderer +47.8%.
- **What it must prove in RageV.** Only after a pass moves to an RT pipeline. SER pays where hit shading is divergent, which RageV reduces by other means (cache-lit hits). Arm: reflection hit shading with and without the reorder, same seeds, pixel-identical, A,B,B,A on the garage (car, poles, floor: three materials) and the bridge.
- **Sources:** https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_ray_tracing_invocation_reorder.html; https://www.khronos.org/blog/boosting-ray-tracing-performance-with-shader-execution-reordering-introducing-vk-ext-ray-tracing-invocation-reorder; https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/

#### Bindless resources and the new descriptor heap

- **What it does.** Puts every texture and buffer in one big table any shader indexes by number, instead of per-pass descriptor sets, which removes binding rules as a source of bugs and CPU cost.
- **Vulkan on this laptop.** Descriptor indexing (core 1.2, used today). VK_EXT_descriptor_buffer (available). VK_EXT_descriptor_heap (Vulkan 1.4.340, early 2026; multi-vendor including NVIDIA; SDK 1.4.341 supports it; check the installed driver).
- **Rough cost.** CPU savings in binding; GPU neutral.
- **What it must prove in RageV.** The measurable win is fewer failure modes: the set-commit rule, OpenGL's 32-sampler limit and 'a set allocated against one pipeline draws black on a sibling' are all recorded traps. Prove by migrating the signal passes' inputs to indices: pixel-identical, validation clean, CPU frame time A,B,B,A. The OpenGL path keeps its sets.
- **Sources:** https://www.khronos.org/blog/vulkan-introduces-roadmap-2026-and-new-descriptor-heap-extension; https://www.khronos.org/news/permalink/vulkan-sdk-1.4.341-released-now-supporting-vk-ext-descriptor-heap

#### Async compute overlap

- **What it does.** Runs compute work (BVH builds, denoisers, cache updates) on a second queue at the same time as raster passes that leave the shader cores partly idle.
- **Vulkan on this laptop.** A compute-only queue family (NVIDIA exposes one). Needs queue-ownership transfers or concurrent sharing, and timeline semaphores (core 1.2).
- **Rough cost.** id: GI 2.05 to 1.55 ms on PS5 and 1.9 to 1.4 ms on Series X with async; little gain on PS5 Pro. NVIDIA: effective when the SM occupancy view shows unused warp slots.
- **What it must prove in RageV.** Precondition: trace and filter passes in compute. Then move the TLAS/BLAS build and one denoiser to the compute queue and measure A,B,B,A on the garage and bridge, with GPU trace captures showing the overlap. Laptop caveat: the power limit can eat overlap gains, so measure on the fixed power profile.
- **Sources:** https://developer.nvidia.com/blog/advanced-api-performance-async-compute-and-overlap; https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf; https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/

#### GPU-driven submission (indirect draws, mesh shaders, device-generated commands; work graphs status)

- **What it does.** The GPU decides what to draw (culling, LOD, even pipeline switches) and writes its own draw commands, so CPU cost no longer grows with object count.
- **Vulkan on this laptop.** Indirect draws (core) and VK_EXT_mesh_shader (enabled in RageV). VK_EXT_device_generated_commands (multi-vendor successor of NVIDIA's NV extension; verify driver exposure). Work graphs: only VK_AMDX_shader_enqueue, experimental and AMD-only, so not available on NVIDIA Vulkan.
- **Rough cost.** RageV measured its GPU-driven lit pass at about 1.38x and GPU cull for depth at about 1.03x on the scale fixtures (HANDOFF).
- **What it must prove in RageV.** The GPU-driven lit path (--gpu-lit) is unfinished and off. It either finishes (the deferred resolve removes its hardest part, the lit raster) or is deleted. Prove on the 60k/120k fixtures: CPU frame time and FPS, pixel-identical to the CPU path.
- **Sources:** https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf; https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_device_generated_commands.html; https://gpuopen.com/news/gpu-work-graphs-in-vulkan/

#### Fail-loud shader and pipeline build

- **What it does.** A shader variant that fails to compile stops the run or paints an unmistakable error colour and logs it, never falling back silently; pipeline caches avoid hitches.
- **Vulkan on this laptop.** Core (VkPipelineCache; VK_EXT_graphics_pipeline_library for faster linking).
- **Rough cost.** None at runtime.
- **What it must prove in RageV.** Inject a deliberate compile error in a variant: the benchmark must exit non-zero and name the variant. This is a correctness gate for every measurement above: one swallowed failure cost an evening of false measurements.
- **Sources:** C:/Users/ism19/Code/RageV/docs/RT-SERIES.md (RT-11: 'the engine silently swallowing a shader compile failure is still open')

### 9. CPU and engine side: job systems, render threads, GPU-driven submission, streaming of geometry and textures, virtual texturing

**The target.** THE 2026 TARGET. The frame's CPU work is split across all cores. A job system (fibers or a task graph) runs gameplay, animation, physics, culling and command recording in parallel. Rendering is pipelined: the game simulates frame N+1 while a render thread records frame N and the GPU runs N-1. Scene data lives persistently on the GPU and is updated by deltas (only what changed is uploaded). The transform hierarchy is data-oriented (arrays, dirty flags, parallel per depth level). Geometry streams in pages with its LOD, textures stream by mip or as sparse virtual textures, and decompression runs on the GPU. Acceleration structures stream with the geometry. At AAA scale, the CPU should never be the limit at 60 fps with 100k+ objects.

RAGEV TODAY, AGAINST IT. The frame is single-threaded: only asset loading and physics use threads, and there is no job system and no render thread. The scale fixtures (Vulkan, 1280x720, after the big transform fix): 60,000 objects at 78 FPS and 120,000 at 34 FPS. At 60k, about 12.8 ms of CPU goes to the lit pass's walk and submission, 2.9 ms to the depth passes and 2.6 ms to the transform walk, against 2.7 ms of GPU. There is no streaming and no LOD chain, and every asset is resident. DIRECTION: a job system with parallel command recording, a persistent GPU scene, parallel transforms, then streaming with LOD. Order it by the owner's own method (a timer in before code), because three times confident guesses about where CPU time went were wrong.

#### Job system (fibers or task graph) with parallel command recording

- **What it does.** Breaks each frame into small jobs that any of the 24 hardware threads can run, including recording Vulkan command buffers in parallel from per-thread command pools.
- **Vulkan on this laptop.** Core: one VkCommandPool per thread, secondary command buffers or per-thread primaries, submitted together.
- **Rough cost.** Recording cost divided across threads, minus scheduling overhead (a few microseconds per job).
- **What it must prove in RageV.** On the 60k/120k fixtures: CPU frame time and FPS A,B,B,A, a pixel-identical frame, and no new validation errors. Target: the 12.8 ms lit walk plus submission and the 2.9 ms depth passes spread across threads.
- **Sources:** https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine; https://developer.nvidia.com/blog/advanced-api-performance-command-buffers/

#### Persistent GPU scene with delta updates, and a data-oriented parallel transform hierarchy

- **What it does.** Keeps every instance's transform, bounds and material on the GPU and uploads only what changed, while world matrices are recomputed only for dirty subtrees, in parallel by hierarchy depth.
- **Vulkan on this laptop.** Core (storage buffers, buffer device address, which RageV already enables).
- **Rough cost.** A static world costs near zero per frame. The TLAS instance buffer can then be written on the GPU from the same data.
- **What it must prove in RageV.** RageV's transform walk was 27.4 ms of a 52 ms frame before 'compare instead of recompute', and is 2.6 ms at 60k objects now. Prove: transform plus upload time on the scale fixtures, with 1%, 10% and 100% of objects moving, against today.
- **Sources:** https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf; https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf

#### Geometry and texture streaming (cluster pages, mip streaming or sparse virtual textures, GPU decompression)

- **What it does.** Keeps only the geometry pages and texture mips the camera needs resident, loading the rest from disk on demand and decompressing on the GPU, so scene size is bounded by disk, not VRAM.
- **Vulkan on this laptop.** Sparse binding and residency (core features, supported on NVIDIA). VK_NV_memory_decompression and VK_EXT_memory_decompression (GDeflate 1.0).
- **Rough cost.** A fixed texture pool (for example 4-6 GB on a 12 GB laptop) instead of full residency; page-in latency hidden by fallback mips or LODs.
- **What it must prove in RageV.** The first need is the LOD chain (NEXT row 5), then streaming. Prove on a fixture larger than VRAM: no out-of-memory, no visible pop over a scripted fly-through (per-pixel diff against a fully resident reference after settle), hitch-free frame times (99th percentile). The RT side must stream BLASes with the same LOD, or rays hit geometry the raster no longer shows.
- **Sources:** https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf; https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_memory_decompression.html; https://docs.vulkan.org/spec/latest/chapters/sparsememory.html

#### Render thread / frame pipelining

- **What it does.** Separates game simulation from render command generation by one frame, so both run concurrently.
- **Vulkan on this laptop.** Core (frames in flight are already per-frame resource sets in RageV).
- **Rough cost.** Hides up to one thread's worth of CPU time, at one frame of added latency.
- **What it must prove in RageV.** Only after the job system, and only if CPU time still exceeds the GPU's in real scenes. Today real scenes are GPU-bound (camp 4.9 ms, all GPU) while the fixtures are CPU-bound. Measure latency (input to photon) as well as FPS.
- **Sources:** https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine

### 10. Path-traced modes as the high end: Cyberpunk 2077 RT Overdrive, Alan Wake 2, Indiana Jones, Black Myth: Wukong, Doom: The Dark Ages

**The target.** THE HIGH END IN 2026. Primary visibility is still rasterised or held in a visibility buffer. Then a unified path tracer handles direct light, bounces, reflections and refraction, with: ReSTIR DI for many lights; ReSTIR GI or a radiance cache (SHaRC, NRC) to end paths early; SER for divergent materials; opacity micromaps for foliage; compacted and cluster BVHs; and DLSS Ray Reconstruction as the denoiser and upscaler, usually with frame generation. It runs at 30-60 fps only with heavy upscaling (typically 4K output from Performance mode on RTX 4080/5080-class desktops). Research is moving it closer: ReSTIR PT Enhanced (April 2026) reports a 2.74x average speedup over ReSTIR PT. For a laptop-class GPU this is not a 60 fps 1440p mode.

FOR RAGEV. The high end serves two purposes, in this order. (1) A reference path-tracing mode as the truth instrument. RT-FIRST already names this, and the owner's rule (prove by per-pixel comparison against truth) needs it more than the 16-ray and 400-frame truth harnesses provide. (2) Later, a high-end tier. The published lessons worth taking now are methodological: Cyberpunk validated every ReSTIR stage against an offline ground-truth path tracer in split-screen, which is exactly what RT-10 lacked.

#### Cyberpunk 2077 RT Overdrive

- **What it does.** Real-time path tracing with ReSTIR DI (up to about 250 lights per scene, all casting RT shadows), ReSTIR GI added in update 2.1, NRD then DLSS-RR, and a documented integration method.
- **Vulkan on this laptop.** Shipped on D3D12. Its ideas map onto VK_KHR_ray_tracing_pipeline plus VK_EXT_ray_tracing_invocation_reorder.
- **Rough cost.** Desktop RTX 40/50 with DLSS Performance plus frame generation for 60+ fps at 4K; a laptop 5070 Ti is a 1080p output, DLSS Performance tier (estimate).
- **What it must prove in RageV.** Take the method, not the renderer: an offline ground-truth mode with split-screen accumulation, validation modes that bypass one stage without code changes (radius 0, bypass flag), and the note that ReSTIR lets the denoiser be more conservative (shorter history, smaller blur). That last point is the lever RT-24's grain-versus-ghost trade needs.
- **Sources:** https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Cyberpunk_2077_Integration.pdf; https://www.nvidia.com/en-us/on-demand/session/gdc24-gdc1002/

#### Alan Wake 2 (Northlight)

- **What it does.** Path tracing with ReSTIR DI, handling large amounts of dynamic content in the acceleration structures, later updated with RTX Mega Geometry, DLSS-RR, and SER.
- **Vulkan on this laptop.** D3D12 title. The equivalent Vulkan features are listed in pillars 7 and 8 (NV-specific for Mega Geometry).
- **Rough cost.** SER: RT cost 16.8 to 10.2 ms. Mega Geometry: +5-20% FPS and 300 MB to 1 GB less VRAM.
- **What it must prove in RageV.** Evidence for the order of work: SER and BVH memory matter once hit shading is divergent and geometry dense. Neither is RageV's bottleneck today (light reads and per-pixel cost are).
- **Sources:** https://www.nvidia.com/en-us/on-demand/session/gdc24-gdc1003/; https://www.khronos.org/blog/boosting-ray-tracing-performance-with-shader-execution-reordering-introducing-vk-ext-ray-tracing-invocation-reorder; https://www.tomshardware.com/pc-components/gpus/testing-nvidias-rtx-mega-geometry-tech-vram-reducing-tech-a-leap-forward-for-path-traced-rendering

#### Indiana Jones and the Great Circle and Doom: The Dark Ages (id Tech)

- **What it does.** Games that require RT hardware, with a 60 Hz cache-based RT GI (pillar 3) as the base, plus a path-traced mode added later with SER, opacity micromaps, compaction of dynamic BLASes and DLSS-RR.
- **Vulkan on this laptop.** id Tech ships on Vulkan on PC, the closest existing precedent for RageV's API choice.
- **Rough cost.** Base RT GI about 1.7-2.1 ms (pillar 3). Path-traced pass on an RTX 5080: OMM 7.90 to 3.58 ms, SER -24%.
- **What it must prove in RageV.** id Tech 8 is the most relevant model: Vulkan, 60 Hz, RT required, small team, performance over everything. Its base design (cache-lit gather, light grid, froxel irradiance for transparents) is the benchmark for RageV's default tier; its path-traced mode is the optional one.
- **Sources:** https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf; https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass/; https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/; https://www.nvidia.com/en-au/geforce/news/doom-the-dark-ages-path-tracing-dlss-ray-reconstruction-update

#### Black Myth: Wukong (UE5 with NVIDIA's RTX branch)

- **What it does.** Full ray tracing in UE5 with ReSTIR-based GI, RT shadows, reflections and caustics, and up to two bounces, accelerated heavily by SER.
- **Vulkan on this laptop.** D3D12 title (NvRTX branch).
- **Rough cost.** SER 15.10 to 4.08 ms on one path-traced pass.
- **What it must prove in RageV.** Shows the size of SER's gain on truly divergent path tracing, which is not RageV's current workload.
- **Sources:** https://www.nvidia.com/en-us/geforce/news/black-myth-wukong-full-ray-tracing-dlss-3/; https://www.khronos.org/blog/boosting-ray-tracing-performance-with-shader-execution-reordering-introducing-vk-ext-ray-tracing-invocation-reorder; https://developer.nvidia.com/game-engines/unreal-engine/rtx-branch

#### ReSTIR PT and ReSTIR PT Enhanced (2026 research)

- **What it does.** Extends reservoir resampling to whole light paths (direct and indirect in one reservoir), with 2026 advances that halve spatial reuse cost, make path reconnection more robust and reduce correlation.
- **Vulkan on this laptop.** Research code; RTXDI 3.0 includes ReSTIR PT (Vulkan via DXC).
- **Rough cost.** 2.74x average (3.05x best) speedup over baseline ReSTIR PT on an RTX 5880 workstation card. Still research.
- **What it must prove in RageV.** Not for the game tier. A candidate estimator for the reference mode only after ReSTIR DI is proven unbiased in RageV.
- **Sources:** https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/; https://dl.acm.org/doi/10.1145/3804494; https://github.com/NVIDIA-RTX/RTXDI

#### A reference path tracer as the truth instrument (RTXPT as the published example)

- **What it does.** A slow, converging, unbiased renderer of the same scene and materials, used only to produce truth images every real-time change is compared against pixel by pixel.
- **Vulkan on this laptop.** VK_KHR_ray_tracing_pipeline or ray query. RTXPT supports Vulkan with extra setup (D3D12 by default).
- **Rough cost.** Seconds per converged frame; never in the game budget.
- **What it must prove in RageV.** It must reproduce the existing 16-ray and 400-frame truths where those are valid, and be a white-furnace-clean energy reference (Bevy's Solari still fails that test, a reminder that this is work). After that, every RT-series-2 item is judged against it.
- **Sources:** https://github.com/NVIDIA-RTX/RTXPT; C:/Users/ism19/Code/RageV/docs/RT-FIRST.md (section 0: 'a reference mode for truth renders')

### A frame budget for an RT-first frame on this GPU (the survey's estimate)

60 FPS RT-FIRST FRAME ON THE RTX 5070 Ti LAPTOP (planning numbers; every line is to be measured in RageV)

Target: 1440p output from 1920x1080 internal (0.75 per axis, 2.07 MP). Fallback: 1707x960 (0.67 per axis, 1.64 MP) when a scene is heavy. Frame 16.7 ms. Keep 1.5 ms of headroom for this laptop's clock drift (RageV measures about 1 ms run to run) and thermal limits. Working GPU budget: about 15.2 ms.

GPU, in frame order (ms):
- Geometry: GPU culling + G-buffer or visibility buffer + depth pyramid: 1.5-2.0
- BVH upkeep: TLAS rebuild + dynamic BLAS refit/skin, on the async queue, mostly hidden under geometry: 0.3-0.8 (0.1-0.3 visible)
- Direct light (sampled lights; 1 shadow ray per pick, 1-2 picks per internal pixel) + its reconstruction: 2.0-3.0 (trace 1.5-2.2 + denoise 0.5-0.8)
- Diffuse GI (cache update + half-resolution final gather + denoise + upsample): 1.5-2.5 (id Tech 8 ships about 1.4-2.1 ms on consoles)
- Reflections by roughness tier (mirror full-res, glossy half-res, cache-lit hits) + reconstruction: 2.0-3.0
- AO / short-range visibility, if kept separate: 0.3-0.5
- Deferred lighting resolve: 0.6-1.0
- Transparent layer (glass + water) + particles + froxel volumetrics: 1.5-2.5
- Temporal upscale to 1440p: 0.5 (own TAAU, or DLSS SR CNN about 0.5) to 1.4 (DLSS SR transformer)
- Post (bloom, exposure, tonemap, DoF, UI): 0.8-1.0
Sum at the low end: about 11.0 ms. At the high end: about 17.5 ms. So 60 fps holds only with the low end of most lines. That is why the internal resolution and the half-resolution GI and glossy tiers are not optional.

Alternative denoise path: DLSS Ray Reconstruction replaces the three reconstruction chains (about 3-4 ms) and the upscaler (0.5-1.4 ms) with one pass of about 2.9-3.7 ms at 1440p output. That is roughly cost-neutral (desktop RTX 5070 measures 2.58-2.72 ms; laptop estimated x1.1-1.35).

Rays: budget in rays per internal pixel, not rays per second. Target about 2-3.5 per pixel per frame in total (direct 1-2, reflection 0.25-1, GI 0.25-0.5), about 4-7 M rays at 1080p internal, with hit shading mostly from caches. For scale: before T5, RageV traced 24.5 M shadow rays per frame in the garage at 1600x900; at K = 4 it traces about 12.5 M, 7 M of them from reflection hits.

Reference points:
- Epic's Lumen budget: GI + reflections + fog in 4 ms at 1080p internal for 60 fps on consoles.
- id Tech 8 GI: about 1.7-2.1 ms.
- NRD on an RTX 4080 at 1440p native: ReBLUR diffuse+specular 2.55 ms, SIGMA 0.40 ms.
- RageV today at native resolution: garage about 15-17 ms at 1600x900 (1.44 MP, below the 2.07 MP target); bridge 34-60 ms at 1440p native on its cameras.

CPU (60 fps): with a job system, each of the game thread and the render/recording thread under about 8 ms, and culling, transforms and recording spread across 24 threads. RageV today, single-threaded: about 12.8 + 2.9 + 2.6 ms at 60k objects (lit walk and submission, depth passes, transform walk), 34 FPS at 120k objects.

VRAM plan (12 GB):
- Windows, driver and compositor: about 1 GB effectively unavailable.
- Render targets + per-signal histories: 0.4-0.7 GB. RT-14 measured RageV's persistent histories at 128 B/pixel, about 265 MB at 1080p internal.
- DLSS SR: 95-205 MB at 1440p output, or RR 212-268 MB.
- World radiance cache + probe atlas + light grid + froxels: 0.15-0.4 GB (SHaRC 160-256 MB at 2^22 cells; id's set about 160 MB total).
- BVH: 0.5-2 GB, compacted.
- Geometry + texture streaming pool: the remaining 6-8 GB.

### Hardware notes

THE GPU. RTX 5070 Ti Laptop: Blackwell GB205, 5,888 CUDA cores, 46 fourth-generation RT cores, 184 fifth-generation tensor cores, 12 GB GDDR7 on a 192-bit bus at 672 GB/s, 60-115 W TGP depending on the laptop (plus Dynamic Boost), clocks 847-1995 MHz. It is close to a desktop RTX 5070 (6,144 cores, 12 GB, 672 GB/s) running at lower clocks. Rule of thumb used above (my estimate): published desktop RTX 5070 timings x1.1-1.35. Published RTX 4080 timings x1.8-2.2 for compute-bound passes but only x1.1-1.3 for bandwidth-bound ones, because the 4080's 716 GB/s is barely more.

VULKAN FEATURE SET ON THIS GPU (current NVIDIA drivers; confirm each with vulkaninfo, since some are beta-driver only):
- Available: VK_KHR_acceleration_structure, VK_KHR_ray_query (both used by RageV), VK_KHR_ray_tracing_pipeline, VK_KHR_ray_tracing_position_fetch, VK_KHR_ray_tracing_maintenance1, VK_EXT_ray_tracing_invocation_reorder and VK_NV_ray_tracing_invocation_reorder (SER in raygen shaders only), VK_EXT_opacity_micromap, VK_KHR_opacity_micromap (Vulkan 1.4.351, May 2026, NVIDIA developer beta driver), VK_EXT_mesh_shader (enabled in RageV), VK_KHR_fragment_shading_rate, VK_EXT_descriptor_buffer, VK_EXT_descriptor_heap (1.4.340), VK_EXT_device_generated_commands, VK_NV/EXT_memory_decompression, a compute-only queue family.
- NVIDIA-only (a lock-in decision): VK_NV_cluster_acceleration_structure and VK_NV_partitioned_acceleration_structure (driver 572.16+; hardware cluster intersection on Blackwell), and VK_NV_ray_tracing_linear_swept_spheres (Blackwell only).
- Unavailable: VK_NV_displacement_micromap (deprecated and withdrawn), and work graphs (only AMD's experimental VK_AMDX_shader_enqueue).
- DLSS SR, RR and frame generation work on Vulkan through NGX or Streamline. They are RTX-only, and the licence requires NVIDIA attribution and use only on NVIDIA GPUs. NRD, RTXDI, SHaRC and NRC are under the NVIDIA RTX SDKs licence. NRD, RTXDI and SHaRC run on any vendor; NRC needs tensor cores.

12 GB VRAM LIMITS. The fixed costs at 1080p internal / 1440p output (histories, G-buffer, upscaler, caches) fit in about 1-1.5 GB. The BVH is the swing item: it is multi-GB for AAA scenes (Alan Wake 2's Mega Geometry update freed about 0.3-1 GB), so compaction and LOD for rays are required, not optional. Textures and geometry must stream into a fixed pool; RageV has no streaming today and keeps everything resident. Two formats to watch:
- RGB9E5 (id's choice for histories: no hue shift, 32 bits) cannot store negative values.
- RGBA16F writes on this GPU round toward zero (RageV's include/half_float.glsl workaround). Every new history, in compute or fragment passes, must go through that path or be re-measured.

LAPTOP REALITIES FOR MEASUREMENT.
- Power and thermal limits move clocks. RageV measures about 1 ms drift between back-to-back runs, so benchmarks must be A,B,B,A palindromes on a fixed power profile, plugged in.
- Measure with the discrete GPU driving the display (MUX or dGPU-only mode), not through the integrated GPU (Optimus). Otherwise a copy to the integrated GPU enters the frame time.
- The CPU has 24 threads that a job system can use; RageV currently uses about one for the frame.

THE OWNER'S MSAA 4x. The designs above (visibility buffer, deferred resolve, render scale with a temporal upscaler, DLSS-RR) all assume single-sample G-buffers. MSAA 4x multiplies G-buffer memory and bandwidth by about 4 and needs per-sample shading at edges in a deferred resolve. Its interaction with each change has to be measured and put to the owner, not assumed away.

### Known pitfalls

- Ghosting on reflections. It comes from reprojecting a reflection by the surface's motion when the reflected image moves differently, or by the virtual image on curved surfaces, where virtual-image motion is wrong. NRD warns explicitly about curved surfaces; the car and chrome poles are RT-24's suspects. Fix direction: roughness- and curvature-aware reprojection plus a fast-history clamp. Verify with spin_measure.py --stage=many16, the only test that separates ghost from grain.
- Two temporal filters in series. A per-signal denoiser followed by a long-memory TAA compounds lag (RageV RT-16: accumulator 64 frames, then TAA still-feedback 0.98) and spreads smear under motion (RT-24's remaining spread is taa_resolve's moving feedback and box). Fix direction: the final resolve reads each signal's confidence and motion (a reactive or confidence mask, specular motion), never a surface-type rule.
- Lag on lighting changes. A long history hides a switched or moving light for seconds. Anti-lag that guesses from one noisy frame fails (RageV R4 and RT-5 part 4). Measured change (A-SVGF-style) works but must re-light with exactly the trace's inputs; RT-23 was a re-light that drew differently from the trace.
- ReSTIR bias. Visibility or reuse weights that are 'plausible but wrong' give a clean picture of the wrong brightness. RageV's RT-10 stages 2-3 moved away from the truth. Published fixes: test reused samples against the previous frame's scene, translate light indices between frames, validate one stage at a time against a ground-truth path tracer.
- ReSTIR boiling and correlation. Feeding spatial results back into the temporal loop multiplies effective samples (about 64 to 65,000 in Cyberpunk's slides) and creates blotchy correlated noise. Denoisers and DLSS-RR assume independent samples; RR's guide asks for randomised temporal reuse and permutation sampling.
- Correlated sampling breaks learned denoisers. DLSS-RR's guide forbids checkerboard input, screen-space dithering, sampling patterns shared across the screen and weak hashes. RageV's low-discrepancy sequences and R2 rotation at the texel index would have to change for an RR arm.
- Denoisers eat real detail. Spatial blurs across shadow edges smeared hard shadows (RageV T5), and a fixed young-history radius spread bright outliers across the floor (RT-23). Blur radii must come from the signal's own footprint (hit distance, roughness), and outliers must be bounded before any spatial pass.
- Bright outlier samples (fireflies). One rare bright path dominates a pixel, then every reuse or blur spreads it (RT-23's source and two spreaders). Remove the variance at the source (NEE, emissive meshes as lights, VNDF sampling) before clamping.
- Light leaking in caches and probes. A hash cell or probe larger than the gap between walls passes light through. SHaRC's rule: query the cache only when the ray is longer than the cell. RageV's sealed-room leak fixtures must stay at 0.000 for every cache change.
- Cache and probe lag and inconsistency. A world cache or probe answers late and can disagree with the per-pixel trace. RageV's baked probe is about a third brighter than the traced reflection, which made the RT-24 fade flash. Any signal fading between two estimators must measure that they agree first.
- Half-resolution signals lose one-to-two-pixel geometry. The bridge cables darkened 0.577 levels over the cable band under RT-3.1's half-resolution filtering, and they flicker on their own. Every render-scale or half-res change must be judged on the cable band, not only on the garage.
- VRAM blow-ups. Histories per signal (RageV 128 B/pixel), upscaler state (DLSS-RR up to about 270 MB at 1440p), caches (SHaRC 160-256 MB), uncompacted BVHs and fully resident textures exceed 12 GB quickly. Budget each explicitly and read the allocator's numbers back in every benchmark.
- Acceleration-structure quality decay. Refitting a BLAS or TLAS indefinitely keeps a tree sorted for where things used to be, and traversal slows. RageV refits its TLAS up to 64 frames; NVIDIA recommends rebuilding the TLAS every frame. Measure total trace time, not only build time.
- Alpha-tested geometry in rays. Every non-opaque candidate interrupts hardware traversal to run shader code; the cost grows with foliage density. Opacity micromaps cut Indiana Jones' main trace pass from 7.90 to 3.58 ms. RageV has no cutouts in the RT path yet.
- Vendor lock-in. DLSS SR/RR/FG, NRC, RTX Mega Geometry and linear swept spheres are NVIDIA-only (some Blackwell-only). SER-EXT, OMM-KHR and descriptor heap are cross-vendor. RageV's OpenGL backend and non-NVIDIA users need the portable path kept pixel-identical where it exists.
- Silent fallbacks invalidate measurements. RageV's engine swallowed a shader compile failure and the picture moved 14 levels (RT-11). Stale Sample.dll files, stale staged shaders and stale bakes have each cost days. Every A/B needs a fail-loud build and a 'did the change take' diff.
- Laptop measurement noise. About 1 ms drift between back-to-back runs plus thermal throttling. Only A,B,B,A palindromes on a fixed power profile, with per-pixel diff images rather than mean levels, count as evidence (owner rule).
- Frame generation is not frame time. DLSS multi-frame generation raises displayed FPS but not the rendered rate, and it adds latency; a 60 fps target means 60 rendered frames.
- A fix keyed to a surface type or scene. It passes one scene and regresses another: RT-22's water exemption was rejected, and the frame filter's 'covered' rule regressed bridge glitter 0.80% to 0.97%. Every rule must be decided by what the pixel's signal does, measured on the garage and the bridge's three cameras.
