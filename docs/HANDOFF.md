# RageV — handoff

**Read this first.** Updated 2026-08-08.

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

**Where it is:** phases 0, 1, 2 and 4 complete; Phase 3 through 3.3 and 3.5
(directional only), plus the specular half of 3.4. The engine loop closes — a project can be imported into,
placed in, scripted, played, and packaged into a folder someone else can run.

**Prove it still works** (from the repo root, ~2 minutes):

```bash
cmake --build build --config Debug
build/bin/Debug/scenetest/scenetest.exe --rhi=vulkan
build/bin/Debug/scenetest/scenetest.exe --rhi=opengl
```

430 checks, `exit 0`. Then look at a frame:

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
| `scenetest` | 371 checks: serialization, undo, assets, scripts, physics, audio, project, picking, packaging, render graph, post chain. |
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
(shadow cascades)-> 4 depth maps  4 scene renders, BEFORE the graph opens
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

The sky, the shadow cascades and the probe captures are the three things *not*
in `BuildFrame`. The
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
- **Nothing is culled in the shadow pass, on purpose.** Culling front faces
  hides acne by recording the back of each caster, and moves every shadow away
  from its caster by that thickness. On a sphere that is a diameter. Acne
  belongs to the normal-offset bias.
- **Cascades are fitted to a sphere and snapped to texels.** Both fix things
  that are invisible in a still frame: a box fit changes size as the camera
  turns and makes edges crawl; an unsnapped projection makes them shimmer on
  sub-texel movement. `scenetest` turns a camera through a circle and nudges it
  a millimetre at a time, because a screenshot cannot.
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

### Done

| Area | State |
|---|---|
| Build | CMake, 13 vendored submodules |
| RHI | Two complete backends, Vulkan + OpenGL, switchable at startup |
| Renderer | Cook-Torrance PBR, materials with 5 maps, primitives, 8 lights |
| Ambient | Scene colour + intensity, serialized (IBL's stand-in) |
| Identity | Real UUIDs, `GetEntityByUUID` |
| Hierarchy | Parenting, world transforms, drag-to-reparent |
| Reflection | `ComponentRegistry` drives inspector + serializer + add menu |
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
| Picking | Click to select in the viewport, triangle-exact; colliders too |
| Debug draw | Collider and trigger wireframes on F3, sleeping bodies dimmed |
| Projects | A folder is a project; the asset registry roots there |
| Runtime | `RageVRuntime` opens a project and runs its start scene |
| Capture | `--screenshot=<file>` writes a PNG of one frame and exits |
| Packaging | `rvpack` and File > Build Game: a runnable folder, ~9 MB |
| Render graph | Declared passes, pooled targets, compile-time validation (3.1) |
| Post chain | HDR scene, 5-level bloom, ACES tonemap, FXAA (3.2) |
| Tests | `scenetest`, **371 checks**, green on both backends |

**Phases 0, 1, 2 and 4 are complete.** The engine loop closes: a project can be
imported into, placed in, scripted, played, and packaged into a folder someone
else can run. Phase 3 (fidelity) and Phase 5 (C#) remain.

All three build configurations work: Debug, Release, and **Dist**, which is
what a shipped game uses -- asserts off, logging at warn so a player can still
report an error.

### Not done

- **An archive format.** Packaging emits a folder, not a `.pak`. Packing needs
  a virtual file system on the loading side to be worth anything, since every
  asset path goes through `std::filesystem` -- a larger feature than 4.3 was.
- **Asset cooking.** Assets ship in their source form: glTF is parsed at load,
  PNGs decoded at load. Fine at this scale, and the place to start when it
  is not.
- **Shadows, IBL, skybox** (Phase 3). Ambient is still a flat constant.
- **SMAA and TAA.** FXAA is the only anti-aliasing that exists. SMAA needs two
  precomputed lookup textures vendored in; TAA needs motion vectors, which
  means every mesh carrying its previous transform and the renderer writing a
  velocity target -- a renderer feature with prerequisites, not a post pass.
- **Clustered forward** — the 8-light cap stands.
- **Culling** — everything is drawn every frame.
- **Skeletal animation.**
- **Build/export** (Phase 4). No runtime target, no packaging. This is the
  remaining severed link in the engine loop.
- **C# scripting** (Phase 5). Deliberately last.
- Physics debug draw — colliders and trigger volumes are invisible.
- Mesh/convex colliders — box, sphere and capsule only.
- Texture map assignment UI; materials are not assets yet.
- Multi-select, copy/paste, focus-on-selection for multiple entities.
- Multi-viewport ImGui on Vulkan (OpenGL only).
- Audio: no reverb or effects, no Vorbis, no editor preview button, no mixer
  panel — bus volumes are reachable from code only.
- Lights and cameras are not clickable in the viewport: picking tests geometry
  and they have none. They need billboard icons first.
- Materials are still per-component `Ref`s rather than assets, so two entities
  cannot be pointed at one material from the inspector.

---

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

**Phases 0, 1, 2 and 4 are done. Phase 3 is at 3.3, and 3.4 is half done.**

1. **Finish 3.4 IBL** (`M` now, was `L`). The specular half is in: surfaces
   reflect the environment cube, roughness picks a mip, and the split-sum
   environment BRDF is Lazarov's analytic fit. What is left is the accurate
   version of both halves:
   - **A real GGX prefilter.** The mip chain is box filtered, so roughness
     selects a blur that is monotonic but not the lobe. Needs render-to-cube-face
     or a compute pass per roughness level.
   - **Irradiance.** The diffuse ambient is still one flat colour. A 32x32
     irradiance cube convolved from the environment replaces it, and is the
     step that makes a scene look lit by its surroundings.
   - **A BRDF lookup texture**, replacing the analytic fit. Smallest of the
     three and the least visible.
2. **Finish 3.5 shadows** (`M` now, was `XL`). Directional cascades are in.
   What is left is the other two light types, which are a smaller problem
   because each has a frustum and a range of its own -- no cascades to fit:
   - **Spot**: one perspective depth map per light, the same pass with a
     different matrix.
   - **Point**: a cube of six, which the probe machinery already knows how to
     fill.
   - Only one light casts at a time today, and the shader shadows only that
     one. More than one means an array of maps and a per-light index.
3. **3.6 culling**, **3.8 clustered forward** (removes the 8-light cap),
   **3.7 skeletal animation**.
4. **Phase 5 C#.** Last, because it must mirror a stable native surface.

The render graph is in place, so each of those is *a pass and a target added to
`BuildFrame`* rather than a new ownership question. That was the point of 3.1
and it held for 3.2.

**Anti-aliasing**, asked for during 3.2 and partly delivered:

- **FXAA** is done and is the default.
- **SMAA** (`M`) needs two precomputed lookup textures -- an area table and a
  search table -- vendored into the repository, and three passes.
- **TAA** (`L`) needs **motion vectors** first: every mesh carrying its previous
  world transform, the renderer writing a velocity target, a jittered
  projection and a history buffer. A renderer feature with prerequisites, not a
  post pass. The same motion vectors would then also buy motion blur and
  temporal upscaling, which is the argument for doing it properly rather than
  cheaply.

Smaller items, none blocking:

- **Billboard icons for lights and cameras**, which would also make them
  clickable — picking tests geometry and they have none.
- **Materials as assets**, so two entities can share one from the inspector.
- **A mixer panel.** Bus volumes exist and are reachable from code only.
- **Prefab instances do not update when the prefab changes.**
- **Packaging is per-machine.** `rvpack` finds the runtime beside itself or in
  a sibling build directory. A shipped editor would need that pinned down.
- **The collider overlay is tone mapped** along with the scene, because it
  draws into the HDR target to depth-test against the geometry it annotates.
  Its colours are therefore slightly off. Fixing it needs the depth buffer
  available in a second pass.

## 9. Known rough edges

- **Asset handles minted in the build output are lost on a clean build.** The
  assets root is the folder beside the executable, which CMake copies from the
  source tree. Assets added to the *source* tree keep their handle because the
  `.meta` is copied along; assets dropped into `build/` do not. The real fix is
  the project concept in roadmap 4.1. Recorded in `AssetRegistry.h`.
- **`.meta` files belong in version control.** They are the identity.
- Vendored EnTT is 3.10.0, checked in as a 5 MB **UTF-16LE** single header
  rather than a submodule — the encoding defeats grep.
- `quadshader.glsl`, `simpleshader.glsl`, `textureshader.glsl` in
  `RageVEditor/assets/shaders/` are pre-RHI leftovers. Safe to delete.
- `Chunk`/`Perlin` are parked in `experiments/terrain/` and not built.
- A script attached while playing is discarded on Stop — correct snapshot
  semantics, but worth knowing.
- The editor's font is loaded at a fixed 18px with no DPI scaling, so the UI is
  physically small on a high-DPI display.
- `Sandbox` predates the RHI and does not build.
