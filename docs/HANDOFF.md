# RageV — handoff

**Read this first.** Updated 2026-08-09.

Work on **`main`**. The `vulkan-overhaul` branch is merged into it and is
finished with, and `main` is pushed.

Companion docs:
- [ROADMAP.md](ROADMAP.md) — where this is going, in dependency order.
- [ENGINE-NOTES.md](ENGINE-NOTES.md) — research distilled into decisions. Read
  §1 before touching the simulation loop, §3 and §3a before touching physics or
  contacts, §6 before touching the render graph, §7a before touching audio, and
  **§7b before deciding a change is verified** — it is why exiting and pixels
  are both part of the bar.
- [ARCHITECTURE.md](ARCHITECTURE.md) — renderer design detail.

---

## 0. Cold start

The five-minute version, for picking this up with no memory of it.

**Where it is:** phases 0, 1, 2 and 4 complete; Phase 3 through **3.6** — the
render graph, HDR post, sky and cube maps, full image-based lighting, shadows
for every light type, frustum culling, instanced draw batching and clustered
forward lighting. What is left
of Phase 3 is skeletal animation (3.7). The engine loop closes — a project can be imported into,
placed in, scripted, played, and packaged into a folder someone else can run.

**Prove it still works** (from the repo root, ~2 minutes):

```bash
cmake --build build --config Debug
build/bin/Debug/scenetest/scenetest.exe --rhi=vulkan
build/bin/Debug/scenetest/scenetest.exe --rhi=opengl
```

544 checks, `exit 0`. Then look at a frame:

```bash
build/bin/Debug/RageVRuntime/RageVRuntime.exe --rhi=vulkan --validation=on --screenshot=f.png
```

**Four things that are easy to get wrong here, all learned the hard way:**

1. **Run each tool from its own directory.** Assets are staged per target.
2. **`--validation=on` for the runtime.** It ships with validation *off*, so a
   run without the flag reports zero validation lines whether or not there were
   any. That already hid a black screen and a segfault once.
3. **Verify by exiting, not by killing.** `exit 0` is part of the bar. A killed
   process runs no destructors, which hid a leak of every scene and every
   render target for months.
4. **Compare the two backends' frames, not just each on its own.** A
   Vulkan-only bloom flip survived a whole roadmap phase of clean runs, because
   nothing in the scene was bright enough for a mirrored contribution to show.
   `--screenshot` on both and look at them side by side.

**What to read before changing something:** §5 of this document. Every entry
was a real bug, and most fail silently rather than obviously.

**What is true right now, honestly:** §6 — what works, what works with a
caveat worth knowing, and what is not built. §9 for defects, §10 for what has
already gone wrong here and what caught it.

**What to do next:** §8. Everything in Phase 3 is done except **3.7, skeletal
animation**, and three of its five pieces are already in — the skeleton, the
shader includes and the glTF import, all tested. What is missing is the part
that puts a character on screen: the skinned vertex format, the skinned shader,
the renderer and shadow paths, and the components. `limb.gltf` in the sample
project is the end-to-end check waiting for them.

---

## 1. What this is

A Windows game engine. Originally a Hazel/Cherno-lineage 2D engine, shelved
mid-way through a Vulkan port, revived and taken through four of five roadmap
phases.

**Stated goal:** ease of use of Unity, some of Unreal's graphical fidelity,
scope closer to Godot.

The engine loop — *import → place → script → play → export* — **closes**.
What remains is fidelity (Phase 3) and C# scripting (Phase 5).

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
| `scenetest` | 544 checks: serialization, undo, assets, scripts, physics, audio, project, picking, packaging, render graph, post chain. |
| `rvpack` | Packages a project into a runnable folder. Headless; no GPU. |
| `rhismoke` | Drives either backend headlessly. |
| `shaderinfo` | Compiles a `.rvshader`, prints reflection + generated GLSL. |
| `Sandbox` | Stale, predates the RHI, **off by default**. |

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

Then run the editor on both backends. **Zero `[Vulkan]` lines in the log** is
the bar — validation and synchronization validation are both on in Debug, and
every `[Vulkan]` line so far has been a real defect.

---

## 3. Environment

- Vulkan SDK 1.4.357.0, core component only. Building does **not** need it;
  headers and volk are vendored. The SDK supplies validation layers.
- Driver reports apiVersion 1.4.325 — a driver capability, not an SDK version.
  Nothing to fix.
- GPU: NVIDIA RTX 5070 Ti Laptop, driver 591.91. RenderDoc installed; debug
  names and command-buffer labels are wired throughout.
- 14 vendored submodules: spdlog, imgui, glm, yaml-cpp, ImGuizmo, PerlinNoise,
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
   Scene (EnTT)  --  PhysicsWorld (Jolt)   ScriptRegistry
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
| Scripts | no | yes, on the fixed step |
| Physics | no | yes, after scripts |
| Contact callbacks | no | yes, after the step that produced them |
| Audio | no | starts on play, silenced on stop |
| Restore on Stop | — | full snapshot |

`Scene::OnUpdateEditor` / `OnUpdateRuntime` / `OnFixedUpdateRuntime`.
`OnRuntimeStart` builds the physics world and starts play-on-awake sources;
`OnRuntimeStop` silences everything and tears the physics world down.

One fixed step, in order:

```
scripts (OnUpdate)
flush destroy queue
update world transforms
physics step
dispatch contacts  ->  OnCollisionEnter/Stay/Exit, OnTrigger*
flush destroy queue          again: a handler may have destroyed something
```

Audio deliberately is not in that list. Listener and source positions are
pushed on the **frame**, from `OnUpdateRuntime`, after physics transforms have
been interpolated — audio is presentation, and belongs where rendering is for
the same reason.

---

## 5. Invariants that are load-bearing

Every one was a real bug. Breaking them tends to produce intermittent corruption
or silence rather than an obvious failure.

### Renderer

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
- **Entities are addressed by UUID, never by `entt::entity`.** Handles are
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
- EnTT iterates entities in **reverse creation order** — the serializer reverses
  it, or the file's order flips on every save/load cycle.

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
  fills `FlipY` for every post shader; nothing else samples a render target.

  An **even** number of passes hides it, which is why it shipped in 3.2 and
  survived until 3.3: with anti-aliasing on, the scene goes tonemap -> FXAA and
  comes out the right way up. The bloom chain has an odd number, so its
  contribution was added mirrored about the middle of the frame -- invisible
  until something was bright enough to bleed, then unmistakable as blobs
  floating on the opposite side of the image from every highlight.
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
- **Verify shutdown by exiting, not by killing.** Every one of these was
  invisible for as long as runs were terminated rather than closed. `exit 0`
  is part of the bar now; `--screenshot` gives any app a clean exit to check.
- An assert with no debugger attached **exits** rather than showing the modal
  dialog (`Entrypoint.h`). The dialog parks the process with every subsystem
  live, and an abandoned instance holding the audio device repeats a fragment
  of whatever it was playing — several at once sound like a broken machine.

### Build

- **A translation unit whose only contents are static registrars will be
  dropped from a static library.** Built-in scripts are registered by an
  explicit function referenced from `ScriptRegistry` for exactly this reason.
- Each app needs its own output directory.

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
| Subsystem health | Every renderer module has `IsReady()`, and `scenetest` asks all seven |
| Culling | Frustum culling per pass, against each pass's own frustum (3.6) |
| Clustered forward | 16x9x24 cells, lights binned on the CPU, no light cap (3.8) |
| Tests | `scenetest`, **544 checks**, green on both backends |

**Phases 0, 1, 2 and 4 are complete, and Phase 3 is complete except for
skeletal animation (3.7).**

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

### Not built

- **Draw sorting and instancing** (the rest of 3.6). Frustum culling is in;
  opaque draws are not sorted front-to-back and nothing is instanced.
- **Skeletal animation** (3.7). No skinning, no clips, no blending.
- **C# scripting** (Phase 5). Native only.
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
1.88 ms. **Vulkan was never submission-bound at this scale.** Its remaining cost
is unattributed and is the next thing to measure.

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
- **EnTT stays** — sparse sets are the right trade for editor-scale scenes.
- **A purpose-built component registry, not `entt::meta`** — EnTT identifies
  members by hashed id, so a serializer must store the name anyway.
- **`ViewRank`, not an `isPrimary` flag** — a boolean can be true twice.
- **Scene view stays on the editor camera during Play.**
- **GPU-driven rendering and bindless are out of scope.** Bindless is where the
  two-backend commitment would start to cost something real; see
  ENGINE-NOTES §5.

---

## 8. Next steps

**Phases 0, 1, 2 and 4 are done. Phase 3 is done except for 3.7.**

Start here, in this order.

### START HERE — 3.7, skeletal animation, continued (`L` remaining)

**Three of the five pieces are done and committed.** All tested, and none of
them visible yet, which is the honest state: **no skinned mesh renders**.

Done:

- **`RageV/src/RageV/Animation/Skeleton.{h,cpp}`** — bones, clips, sampling,
  hierarchy composition, blending. 21 checks, including the one everything
  rests on: at the bind pose every skinning matrix is the identity. Verified it
  can fail, by removing the inverse-bind multiply and watching it go red.
- **Shader includes**, and `pbr.rvshader` split so the skinned variant shares
  the lighting rather than copying six hundred lines of it.
- **glTF skin and animation import**, against `limb.gltf` — a two-bone post
  that bends, written by `tools/scripts/make_skinned_gltf.py` so the tests
  assert against numbers chosen here rather than against a downloaded model.
  Joints are reordered parents-first at import and everything referring to them
  is remapped, so the runtime never sees glTF's arbitrary order. 21 more checks.

Left, in dependency order. **The first three are one piece of work** — none of
them shows anything on its own:

1. **A skinned vertex format.** `SkinnedVertex` = the current 32 bytes plus
   four joint indices and four weights, as its own struct with its own vertex
   binding. Do *not* widen `MeshVertex`: static meshes are almost all of them
   and would pay 32 bytes a vertex for nothing. `Mesh` has to hold either, and
   `ImportedPrimitive::IsSkinned()` already says which it is.
2. **`pbr_skinned.rvshader`.** Thin, like `pbr.rvshader` is now: two extra
   attributes, a bone-matrix storage buffer, and
   `sum(weight[i] * bone[joint[i]])` before the model matrix. The rest is the
   shared include.
3. **The renderer path.** A second pipeline and the pose's skinning matrices in
   a storage buffer per skinned instance. **The shadow pass needs the same
   treatment or a skinned character casts its bind pose** — which is the kind of
   thing that looks like a shadow bug for an afternoon.
4. **The components** — a skinned mesh renderer, and something that advances
   time and chooses a clip, with registry entries so both serialize and appear
   in the inspector.

Once those land, `limb.gltf` in the sample project is the end-to-end check:
it should stand up straight and bend.

### 1. Phase 5 — C# scripting (`XL`)

Last, deliberately: it must mirror a native surface that has stopped moving.

### Smaller, none blocking

- **Materials as assets**, so two entities can share one from the inspector.
- **Billboard icons for lights and cameras**, which would also make them
  clickable — picking tests geometry and they have none.
- **Per-object reflection probe selection**, replacing the per-scene choice.
- **SMAA** (`M`) needs two lookup textures vendored. **TAA** (`L`) needs motion
  vectors first: every mesh carrying its previous transform, a velocity target,
  a jittered projection and a history buffer. The same motion vectors would
  then buy motion blur and temporal upscaling, which is the argument for doing
  it properly rather than cheaply.

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

### Performance, all unmeasured

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
- **Vulkan's remaining 1.88 ms is unattributed.** See section 8.

### Editor and tooling

- **Running any application without `--screenshot` opens a window and waits.**
  That is correct behaviour and not a hang, but it will look like one in a
  script. Every verification run should pass the flag.
- **`Sandbox` predates the RHI and does not build.** Off by default.
- **The editor UI does not scale itself.** `--ui-scale=N|auto` exists and
  defaults to 1.0; `auto` follows the monitor. Deliberate — on a 150% display
  auto gives a 27px font, which is what the OS asks for and larger than anyone
  wanted. There is no per-monitor handling and no live reload; it is read once
  at startup.

### Housekeeping

- **`.meta` files belong in version control.** They are the identity, and they
  are tracked — checked, not assumed.
- Vendored EnTT is 3.10.0, checked in as a single header rather than a
  submodule. UTF-8 now; it was UTF-16LE, which defeated grep.
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
- **Ask whether a defect predates the change.** The OpenGL flicker was found
  while verifying instancing and looked like its fault. Stashing the work and
  rebuilding took ten minutes and showed it was already there — which is the
  difference between a regression to revert and a bug to schedule.
- **Reporting a fix by what was logged rather than by what changed.** The
  readiness work was presented as six modules' log lines when the actual fix
  was three new flags and a test. A log line cannot be tested and is not a fix.

