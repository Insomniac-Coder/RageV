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

**Where it is: phases 0-5 are complete.** The render graph, HDR post, sky and
cube maps, image-based lighting, shadows for every light type, frustum
culling, instanced batching, clustered forward lighting and skeletal animation
are all done. So is scripting, in both languages, with live reload in both:

- **C++ scripts live in the project's `Source/`**, which builds into a game
  module DLL. Opening a project loads it -- in `Project::Load`, so the editor,
  the runtime and a packaged game all get it the same way. Build Scripts
  unloads the module, rebuilds it in the background (live console, cancel),
  and reloads whatever links. New Script writes into `Source/`. No engine
  rebuild anywhere in a game developer's loop.
- **C# scripts live in `Scripts/`**, built the same way, loaded from *bytes*
  into a **collectible AssemblyLoadContext** that is retired and replaced on
  every build -- that is 5.5, and it is done. The unload is verified with a
  WeakReference; a context that will not die is reported, not ignored.
- **Building mid-play restarts the scene.** Live instances run the loaded
  code, so the editor stops the scene, swaps both languages' scripts, and
  resumes Play on the new code; after a failed build it stays stopped with
  the errors showing. One rule, both languages. Play pressed during a build
  queues until the build lands.
- **As of interop protocol 4 the languages are equals**: audio, raycasts,
  hierarchy, and components by registry name with text values all reach C#.
  The one structural exception is typed GetComponent<T>, which cannot cross
  a boundary and is traded for the registry's named access.

**A project can have both languages on the same entity**, one inspector, one
scene format. The manual documents both guides and is generated with a drift
check (`rvdoc --check`).

**Also done, off-roadmap:** the developer manual and its generator, the
application icon, project creation with a per-project `bin/`, and 5.0 -- no
third-party type in a public header, and the public API segregated into
domain namespaces. The renderer was deliberately left out of that last one.

**Prove it still works** (from the repo root, ~2 minutes):

```bash
cmake --build build --config Debug
build/bin/Debug/scenetest/scenetest.exe --rhi=vulkan
build/bin/Debug/scenetest/scenetest.exe --rhi=opengl
```

888 checks, `exit 0`. Then look at a frame:

```bash
build/bin/Debug/RageVRuntime/RageVRuntime.exe --rhi=vulkan --validation=on --screenshot=f.png
```

And the one check that compares a render against a render instead of against a
stored image — it found both of 6.9's bugs and is where a transparency
regression will show up first:

```bash
python tools/scripts/check_oit.py --config Debug
```

4/4, `exit 0`.

**Five things that are easy to get wrong here, all learned the hard way:**

1. **Run each tool from its own directory.** Assets are staged per target.
2. **`--validation=on` for *every* verification run, editor included.**
   Validation is off by default everywhere now -- it costs ~1.5 ms/frame and
   was making Vulkan read as slower than OpenGL -- so a run without the flag
   reports zero validation lines whether or not there were any. That already
   hid a black screen and a segfault once, back when only the runtime shipped
   with it off.
3. **Verify by exiting, not by killing.** `exit 0` is part of the bar. A killed
   process runs no destructors, which hid a leak of every scene and every
   render target for months.
4. **Compare the two backends' frames, not just each on its own.** A
   Vulkan-only bloom flip survived a whole roadmap phase of clean runs, because
   nothing in the scene was bright enough for a mirrored contribution to show.
   `--screenshot` on both and look at them side by side.
5. **The harness prints `FAIL` in capitals.** A case-insensitive search for
   "fail" also matches the word *fails* inside the names of passing checks, and
   returns a count that looks like failures and is not. Match case, and read the
   `OK` line at the end.

**What to read before changing something:** §5 of this document. Every entry
was a real bug, and most fail silently rather than obviously.

**What is true right now, honestly:** §6 — what works, what works with a
caveat worth knowing, and what is not built. §9 for defects, §10 for what has
already gone wrong here and what caught it.

**What to do next:** section 8 -- the per-project game module, which is what
making the engine a DLL was for.

---

## 1. What this is

A Windows game engine. Originally a Hazel/Cherno-lineage 2D engine, shelved
mid-way through a Vulkan port, revived and taken through four of five roadmap
phases.

**Stated goal:** ease of use of Unity, some of Unreal's graphical fidelity,
scope closer to Godot.

The engine loop — *import → place → script → play → export* — **closes**, and
the renderer now does everything the roadmap asked of it. What remains is C#
scripting (Phase 5).

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
| `scenetest` | 700 checks: serialization, undo, assets, scripts, physics, audio, project scaffolding, picking, packaging, render graph, post chain, settings writer, .NET hosting, the interop boundary, the math layer against glm. |
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

> [!TRAP]
> **The failure marker is `FAIL`, in capitals.** Counting failures by grepping
> for lowercase `fail` matches nothing and reports every run as clean. The
> reliable signal is the last line -- `OK`, or `N check(s) failed` -- and the
> exit code. A whole session's worth of "0 failures" was measured with a broken
> grep before the `OK` line caught it.

Then run the editor on both backends. **Zero `[Vulkan]` lines in the log** is
the bar — validation and synchronization validation are both on in Debug, and
every `[Vulkan]` line so far has been a real defect.

If the change touched a public script API, also run:

```bash
build/bin/Debug/rvdoc/rvdoc.exe --check
```

It fails when `ScriptableEntity.h` gained a member the manual does not document,
or the manual documents one that no longer exists — in either direction, and
also when it manages to parse suspiciously few members, because a check that
passes by parsing nothing is worse than no check at all.

---

## 2. Projects

A project is a folder with a `.rvproject` in it. **File > New Project...**
creates one and fills it in:

```text
MyGame/
  MyGame.rvproject     name, asset root, start scene, fixed Hz
  assets/{scenes,models,textures,audio,prefabs}
  bin/                 builds land here
  .gitignore           keeps bin/ out of version control
```

Three decisions in that worth knowing:

- **The skeleton is fixed and checked.** `scenetest` asserts every folder,
  because a path like `models/rock.gltf` only means the same thing across
  projects if every project has the same layout, and conventions that nothing
  checks do not stay true.
- **`Project::BinaryRoot()` is `Root()/"bin"`, a convention rather than a
  setting.** Builds belong next to the thing they were built from, so a project
  folder can be zipped or handed over with its output intact. **Build Game**
  goes there; **Build Game As...** asks.
- **The starter scene is not empty** -- ground, cube, sphere, angled directional
  light. An engine that opens on nothing makes the first five minutes a hunt for
  which of six missing things matters, and "no light, so everything is black"
  reads as a broken install. `EditorLayer::PopulateStarterScene`.

`NewProject` creates a *folder* named after the project, not a loose
`.rvproject` wherever the dialog pointed -- the platform layer only has file
dialogs, and taking the picked name literally would scatter `assets/` and `bin/`
into someone's Downloads folder.

---

## 1a. Namespaces

The public API is segregated by domain. There is no `RV` alias -- `RageV` is the
only spelling.

| Namespace | Holds |
|---|---|
| `RageV` | The engine vocabulary: `Entity`, `Scene`, components, `UUID`, `Timestep`, `Application`, `Layer`, `Project` |
| `RageV::Math` | `Vec2/3/4`, `UVec4`, `Mat3/4`, `Quat` and all the arithmetic |
| `RageV::RHI` | Devices, buffers, pipelines, formats |
| `RageV::Audio` | `Engine`, and the mixer behind it |
| `RageV::Physics` | `World`, `ScaleCollider`, `DrawColliders` |
| `RageV::Assets` | `Manager`, `Registry`, `AssetMetadata`, the glTF importer |
| `RageV::Anim` | `Clip`, `Channel`, pose sampling and blending |

**The rule, and it is one rule:** the *machinery* lives in the namespace, and the
small value types that appear in other domains' signatures are re-exported into
`RageV` with a using-declaration. `Audio::Engine::Init` reads as information;
`Audio::AudioBus` on a component field next to a plain `Vec3` reads as noise. It
is the same arrangement `RageV::Math` has used for `Vec3` since it was written.

So: `AudioBus`, `AudioVoice`, `RayHit`, `BodyType`, `ColliderShape`,
`AssetHandle`, `Skeleton`, `AnimationClip` are all still spelled unqualified.
`Asset`, `AssetHandle` and `AssetType` never moved at all -- they live in
`Asset.h` at the root, because every subsystem in the engine names them.

**Types were only renamed where the short name was free.** `AudioEngine` became
`Audio::Engine`, `PhysicsWorld` became `Physics::World`, `AnimationClip` became
`Anim::Clip`. `AudioMode` and `AssetHandle` kept their prefixes, because this
engine names members after their types -- `AudioMode Mode`, `AssetHandle Handle`
-- and a member that shares its type's name hides it, so `Mode::Silent` stops
compiling inside the very struct that needs it.

> [!TRAP]
> **Forward declarations do not follow enclosing-namespace lookup.** `class
> Scene;` inside `namespace RageV::Physics` declares a new
> `RageV::Physics::Scene` that nothing ever defines, and the error appears at
> the use site rather than at the declaration. `PhysicsWorld.h`,
> `PhysicsDebugDraw.h`, `ColliderShapes.h` and `AssetManager.h` all hoist theirs
> into a `namespace RageV { }` block, with a comment saying why.

> [!TRAP]
> **Free functions do not come along for free.** A call like
> `ScaleCollider(collider, scale)` from outside the domain fails even though
> ADL usually saves you: the argument is `RageV::ColliderComponent`, so ADL
> searches `RageV`, not `RageV::Physics`. Anim's functions *are* found
> unqualified, because their arguments are Anim types. The difference is which
> namespace the arguments live in, not which namespace the function does.

---

## 2a. The application icon

`tools/scripts/make_icon.py` draws it — a two-tone V taken from the manual's
wordmark, red half and light half, on the site's near-black. Regenerate with:

```bash
python tools/scripts/make_icon.py
```

Pure stdlib: `zlib` writes the PNGs, `struct` writes the ICO container. No
imaging library, for the same reason there is no Node toolchain for the docs —
a clone builds with nothing but a compiler and one icon is not worth breaking
that. Small sizes are emitted as DIBs and 128/256 as PNG, which is what a
well-behaved `.ico` does.

> [!NOTE]
> **There are two icon mechanisms and both are needed.** `RageVEditor.rc` /
> `RageVRuntime.rc` compile `icon.ico` into the executable, which is what
> Explorer shows and what a pinned taskbar entry inherits.
> `WindowsWindow::SetWindowIcon` loads `assets/icon-32.png` and `icon-48.png`
> through GLFW, which is what the title bar and alt-tab use. Setting only the
> first gives a correct icon in Explorer and a blank default in alt-tab.

The PNGs live in `RageVEditor/assets/` only — that is the engine's asset root.
The runtime stages them explicitly alongside shaders and fonts, because its
asset list is deliberately a list rather than a copy of the whole directory.

---

## 2b. The developer manual

`docs/manual/` is Markdown; `docs/site/` is the generated HTML. Regenerate with
the `manual` target, or by hand:

```bash
build/bin/Debug/rvdoc/rvdoc.exe --in docs/manual --out docs/site
```

`tools/rvdoc` is a C++ target like the others — a Markdown subset, a syntax
highlighter, a client-side search index inlined as JS so the site works from a
folder without a server, and the reference check above. No new submodule, and no
Node toolchain to install before the docs will build.

`docs/manual/SUMMARY.md` is the table of contents. A page not listed there is not
part of the manual and will not be emitted.

This is a **different document from this one**. The manual is for someone making
a game with the engine; HANDOFF, ROADMAP, ARCHITECTURE and ENGINE-NOTES are for
someone working on the engine. Do not merge them — the first is a description of
supported surfaces, the second is a record of decisions and traps.

---

## 3. Environment

- Vulkan SDK 1.4.357.0, core component only. Building does **not** need it;
  headers and volk are vendored. The SDK supplies validation layers.
- Driver reports apiVersion 1.4.325 — a driver capability, not an SDK version.
  Nothing to fix.
- GPU: NVIDIA RTX 5070 Ti Laptop, driver 591.91. RenderDoc installed; debug
  names and command-buffer labels are wired throughout.
- 14 vendored submodules: spdlog, imgui, **glm (test-only)**, yaml-cpp, ImGuizmo, PerlinNoise,
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

- **Vsync off means `VK_PRESENT_MODE_IMMEDIATE_KHR`, not mailbox.** Mailbox is
  the nicer mode on paper and was the original preference. It is also silently
  wrong in the case that matters: a swapchain created in mailbox *as the
  replacement for a FIFO one* presents at exactly the refresh rate anyway.
  Measured on the RTX 5070 Ti, four runs each -- created mailbox at startup,
  ~450 FPS; recreated mailbox after the surface had been presenting FIFO,
  240.0 FPS every run; immediate from the identical toggle, 442 FPS. So
  unchecking VSync in the editor did nothing at all, while starting with
  `--vsync=off` worked, which is what made it look like a compositor decision
  rather than a mode decision.

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
- **A target may carry several colours; a pass may bind a subset of them.**
  `RGTargetDesc::ExtraColors` and `WriteAttachments`. The subset is not an
  optimisation: a pipeline's declared `ColorFormats` must match what its pass
  binds, so a three-attachment target that could only be bound whole would be
  undrawable by every pipeline that declares one colour -- which is every
  pipeline that draws the scene. Binding a subset is what lets one target with
  **one depth buffer** serve both the scene and a transparency pass; separate
  targets would each own a depth image and the transparent geometry would
  ignore the world. `SameShape` compares the extra formats, or the pool hands
  back a target with the right first attachment and the wrong number of them.
- **`PreserveDepth` clears colour and keeps depth.** `RGLoad` says one thing
  about both, which is right until a pass wants fresh accumulation buffers over
  the depth the scene already wrote.
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
  fills `FlipY` for every post shader.

  An **even** number of passes hides it, which is why it shipped in 3.2 and
  survived until 3.3: with anti-aliasing on, the scene goes tonemap -> FXAA and
  comes out the right way up. The bloom chain has an odd number, so its
  contribution was added mirrored about the middle of the frame -- invisible
  until something was bright enough to bleed, then unmistakable as blobs
  floating on the opposite side of the image from every highlight.

  **It bit a second time in 6.8, and the reason is worth more than the fix.**
  This entry used to end "nothing else samples a render target", which stopped
  being true the moment the weighted-blended resolve was written -- a fullscreen
  pass that reads two render targets and lives in `ParticleRenderer`, not in
  `PostProcess`, so it inherited none of this. It is a *single* pass, and one is
  an odd number: Vulkan composited every weighted particle upside down. The rule
  is therefore about what a pass *does*, never about where it lives: **anything
  that samples a render target and writes another owes `FlipY`.**

  It survived a full session of clean runs because the only test scenes were
  plumes near the middle of frame, and smoke that is flipped still looks like
  smoke. What found it was rendering the same particles as sorted `Alpha` and
  comparing the *silhouettes* -- an image is only checkable against something
  that is right by construction. `tools/scripts/check_oit.py` is now that check.
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
- **A registered enum field must be int-sized.** Reflection is type-erased, so
  every consumer of an `Enum` field — the serializer, the undo stack, the
  inspector's combo, the C# component bridge — reaches it through an `int*`.
  Declare one `: uint8_t` and it compiles, looks right in the inspector, and
  writes three bytes past its end into whatever member follows. `Field<>`
  static_asserts it now, and a suite check makes the same claim about the
  registry as it stands (which also covers a descriptor built by hand rather
  than through `Field<>`). The fix is always to drop the narrow underlying
  type: a component's enum is one field on one instance, never a packed array.
- **A deferred deletion is only deferred if its slot is not the next one
  flushed.** Entity destruction happens in the simulation phase, before
  BeginFrame — and a deleter pushed there used to land in the very slot
  BeginFrame flushes that same iteration, behind a fence covering a frame from
  two submissions ago, while the *previous* frame still executed. Out-of-frame
  pushes now slot by the most recently submitted frame; in-frame pushes stay in
  the recording frame's slot, since the resource may already be in its buffer.
  Found by the first game that destroyed material-bearing entities mid-play,
  and only under `--validation=on` — the GPU usually wins the race.
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
- **A vertex attribute's reflected format must carry its vector width.** The
  reflection switched on the base type and returned a one-component format for
  every integer, so a `uvec4` of joint indices came back as four bytes where the
  buffer holds sixteen. Every attribute after it then sat at the wrong offset
  and the mesh drew as a spray of triangles. Nothing about that is visible in a
  compile, a validation layer or a draw count -- `scenetest` reflects the
  skinned shader and checks the stride against `sizeof(SkinnedVertex)`.
- **A skinned mesh needs a full pose, never a short one.** Its vertices name
  bones by index, so a run shorter than the skeleton leaves them reading past
  their own instance's bones into the next character's. A mesh with no animator
  is given one identity *per bone*, which is what the bind pose is.
- **Whatever poses the lit pass must pose the depth pass.** Skinning the lit
  shader alone leaves a character walking while its shadow stands still in the
  bind pose, and that reads as a shadow bug for an afternoon.
- **A swapchain is retired, not destroyed and replaced.** `vkDeviceWaitIdle`
  waits on the *queues* and says nothing about the presentation engine, which
  may still hold images of the old swapchain and still be waiting on their
  semaphores. Destroying it first and creating a new one worked every time
  until the one time the compositor was a frame behind. The old handle is
  passed as `oldSwapchain` and destroyed after the new one exists; the
  per-image semaphores go with it, not before it.
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

### Compute, and blending across two attachments

- **A dispatch must be recorded outside a render pass.** Vulkan forbids it;
  OpenGL would allow it. Both command lists assert, so a pass that obeyed the
  rule only on the backend that complains cannot ship. This is why the GPU
  particle simulation runs from `Scene::OnUpdateRuntime` and not from
  `OnRender` -- and `OnUpdateRuntime` is also **once per frame**, where
  `OnRender` runs once per *view* and the editor has two.
- **A resource set holds one descriptor set per frame in flight, and `Commit`
  writes only the current frame's.** Committing once at creation populates one
  and leaves the rest never written. Validation catches it on frame two; a
  release build renders garbage. Rebind and commit every frame -- cheap, and
  what every renderer here already did.
- **A persistently mapped host-visible buffer written by a compute shader is a
  synchronisation point on OpenGL.** The particle state buffer was mapped so a
  rare seed could be a memcpy; it made the GPU path *slower than the CPU one*
  (6.9 ms against 3.3 ms) and stopped GPU timestamps resolving at all. Device
  local, seed through staging.
- **Barriers bracket a batch of dispatches, not each one.** A barrier orders
  everything around it, not only the buffer it names, so interleaving makes
  every dispatch wait for the previous one.
- **`independentBlend` is a Vulkan device feature.** Different blend state on
  different attachments of one pipeline is not free; without it every
  attachment must match the first. Enabled where supported, with a warning and
  a fallback where not.
- **`glDrawBuffers` is persistent framebuffer state.** Set it on every pass, or
  a pass that bound a subset leaves the next pass over that target writing into
  its selection.
- **`glClearBufferfv` indexes the draw-buffer list, not the framebuffer.**
  Binding attachments 1 and 2 means clearing indices 0 and 1.
- **A weight that saturates its clamp is not a weight, it is a constant --
  and in weighted-blended transparency a constant weight cancels exactly.**
  The resolve divides accumulated colour by accumulated alpha, so only the
  *ratio* between overlapping fragments survives. Scale them all alike and the
  ratio is one: the output is an unweighted average that still looks like
  plausible transparency. The shipped weight did this at every distance, and
  it was proven by replacing the whole expression with the literal `1.0` and
  getting a **pixel-identical** image. When a formula's output feeds a
  normalisation, the test is not "is the number sensible" but "does it still
  *vary*" -- a constant is the one value that hides inside any normalisation.
- **`gl_FragCoord.z` is not a distance and must not be used as one.** With the
  sample scenes' near plane of 0.01 it spans 0.995 to 1.0 across the entire
  world -- 4% of range doing the work of a depth term. Anything wanting linear
  view distance should take `1.0 / gl_FragCoord.w`, which is exact for this
  projection because clip.w *is* the view distance. Constants copied from a
  paper or a blog carry that source's near plane with them, silently.

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
- **Wait for the device to go idle before destroying anything, not partway
  through.** The loop can exit with frames still executing -- `--benchmark` and
  `--screenshot` both stop it in the iteration that submitted one, and a window
  close does the same -- so `~Application` idles first and clears the layers
  after. It used to be the other way round, and every benchmark run produced
  seven "currently in use by VkCommandBuffer" errors as the layers destroyed
  buffers, samplers, descriptor sets and pipelines out from under the GPU.
- **Verify shutdown by exiting, not by killing.** Every one of these was
  invisible for as long as runs were terminated rather than closed. `exit 0`
  is part of the bar now; `--screenshot` gives any app a clean exit to check.
- An assert with no debugger attached **exits** rather than showing the modal
  dialog (`Entrypoint.h`). The dialog parks the process with every subsystem
  live, and an abandoned instance holding the audio device repeats a fragment
  of whatever it was playing — several at once sound like a broken machine.

### Build

- **A translation unit whose only contents are static registrars will be
  dropped from a static library.** This no longer bites the engine, which is a
  DLL and therefore linked from its object files, but it is why built-in scripts
  are still registered by an explicit function referenced from
  `ScriptRegistry` -- and it would bite again the moment anything goes back into
  a `.lib`.
- **A tool must not link a vendored library the engine already links
  privately, and must not call one whose state lives in globals.** `RageV.dll`
  links `glfw` `PRIVATE`. `rhismoke` linked it *again* and called `glfwInit`
  and `glfwCreateWindow` itself, so the executable and the DLL each held their
  own GLFW with their own globals. The window was real -- the tool's null check
  never fired -- and the engine's copy had never heard of it, so
  `glfwGetWin32Window` inside `VulkanDevice::CreateSurface` answered null and
  Vulkan refused the surface with `hwnd is NULL`. OpenGL failed one step
  earlier and looked like a different bug entirely: glad was handed a
  proc-address loader belonging to a library nobody had initialised, and
  reported `gladLoadGLLoader failed`. **One cause, two symptoms that do not
  resemble each other.**

  Fixed 2026-08-11 by creating the window through `Window::Create` and dropping
  `glfw` from the tool's link line. `WindowProps::Visible` exists for exactly
  this case and already carried the diagnosis in its comment -- the prop had
  been added and `rhismoke` was never migrated onto it, which is its own
  lesson: a fix that leaves the old path compiling leaves it in use.
- **Static data members read by an inline accessor do not cross a module
  boundary.** A consumer links its own copy of the data rather than the
  engine's. `Application::Get`, `Log`'s two loggers and
  `Platform::GetPlatformType` are out of line for this reason; anything added
  with that shape has to be too.
- **A library with globals must be linked into one module only.** ImGui, GLFW
  and ImGuizmo each keep their state in globals. An executable that links its
  own copy gets its own state, and the engine cannot see it: that is exactly how
  `scenetest` came to fail with a null HWND on Vulkan and no current context on
  OpenGL. Either the module is linked once, or the state is handed across
  explicitly -- see `ImGuiBinding.h`.
- **Auto-export and LTCG are mutually exclusive.** `/GL` writes intermediate
  language into the object files, and the tool that scans them to build the
  export list cannot read it. The DLL would export nothing at all.
- Each app needs its own output directory, and `RageV.dll` is staged into each
  of them.

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
| Skeletal animation | Skinned vertex format, skinned PBR and depth shaders, per-instance bone matrices, `AnimatorComponent` (3.7) |
| Batching | Instanced draws keyed on mesh and material; 3238 draws down to 60 on the stress scene |
| Profiler | CPU wall time and GPU timestamps per phase, live in the editor and printed by `--benchmark` |
| Tests | `scenetest`, **700 checks**, green on both backends |

**Phases 0, 1, 2, 3 and 4 are complete.** Phase 5, C# scripting, is in
progress -- 5.1 (hosting) is done, and **5.0 now precedes the rest**: no
third-party type may appear in a public header, and the public API is being
segregated into `RV::Math::`, `RV::Audio::` and friends. That is an API break
the C# bindings must not be written before, or they get written twice.

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
| Particles | A GPU emitter cannot sort its own alpha — sorting would need a readback. Use `Additive` or `WeightedBlended` there. Emitters are still sorted against each other. |
| Particles | Weighted blending is an approximation and is loose in one known way: a stack of equally transparent layers spread over a very large depth range renders with the near one too strong. No depth-only weight avoids that; `Alpha` is what exactness costs a sort. Measured in §8 (6.9). |
| Particles | No sub-emitters, no arbitrary curves (size and colour interpolate start to end), and nothing collides. |

### Not built

- ~~C# scripting~~ -- **built, all of phase 5**, including hot reload, and as
  of interop protocol 4 the C# surface mirrors the C++ one entirely: audio,
  raycasts, hierarchy, LookAt, SpawnPrefab by path, and component access by
  registry name with text values -- the boundary's answer to GetComponent<T>,
  driven by the same ComponentRegistry as the inspector so it cannot go
  stale. Mid-play builds stop, swap and resume Play, in both languages.
- ~~Particles~~ -- **built**: an emitter component, CPU and GPU simulation with
  a switch that allocates nothing, billboard and flat facings (3D and 2D from
  one component), and three blend modes including order-independent. See the
  manual's particles page. The order-independent path is validated as of 6.9,
  and **curves are done as of 6.10** -- size, colour and alpha as `.rcurve`
  assets with a draggable editor, read identically by the CPU and GPU paths.
  Still missing within it: collision and sub-emitters (both deferred on
  purpose, scoped in §8), and a GPU emitter cannot sort its own alpha -- which
  is what `WeightedBlended` is for, and 6.11 would make exact.
- **Text and game UI.** The largest gap by some distance, and the one the mini
  game hit hardest -- see §8. Nothing can draw a score, a label or a menu.
- **A per-frame script hook.** `OnUpdate` is per fixed step by design, so
  anything that must move with the display -- camera smoothing, recoil, shake
  -- has nowhere to live. Named by the mini game as the second-largest gap.
- **Front-to-back depth sorting.** Opaque draws are grouped by mesh and
  material so they batch, which is the half of 3.6 that was worth measuring.
  Sorting them by depth as well would let early-z reject more, and is worth a
  measurement before it is worth code.
- **Animation blending between clips.** `BlendPoses` exists and is tested; no
  component drives it, so a character snaps between clips rather than easing.
- **Skinned bounds are the bind pose's.** A limb swinging wide leaves the box,
  so a skinned mesh can be culled while part of it is still on screen. Proper
  bounds mean per-clip boxes, or one grown to cover every pose.
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

**Phases 0-5 are done, the game-module arc is done, and as of interop
protocol 4 the two script languages are equals.** C# reaches everything C++
reaches: audio, raycasts, hierarchy, LookAt, SpawnPrefab by asset path, and
components by registry name with text values -- the boundary's answer to
GetComponent<T>, driven by the same ComponentRegistry as the inspector so it
cannot go stale. Building mid-play stops the scene, swaps both languages'
scripts, and resumes Play on the new code; validation layers are opt-in
everywhere (~1.5 ms/frame of CPU when on, Vulkan only -- the manual's
getting-started page has the section).

### Done - the mini game (Knockdown/)

The game exists, plays, and ships. **Knockdown**: you stand at a launcher
(A/D swing it, W/S tilt the muzzle, Space or left-click fires), lob heavy
balls at a pyramid of six crates on a platform, and knock them all onto the
ground. The beacon beside the platform walks from red to green as crates
land in the out-zone trigger; the last one plays a chime and doubles the
light. F resets the round by respawning the crate stack prefab. No text, no
UI -- the light, the sounds and the crates are the whole scoreboard.

It deliberately spans both languages: C++ (`Source/Launcher.cpp`,
`Ball.cpp`) does input, raycast aiming (the emissive AimDot is the sight),
prefab spawning and launch velocity; C# (`Scripts/GameManager.cs`,
`Crate.cs`, `Ambience.cs`) does trigger counting, the component-bridge
light changes, impact/win/ambience audio by asset path, and the reset. The
sounds are synthesised by `tools/scripts/make_game_audio.py` -- same
convention as the sky generator.

Verified end to end on both backends with validation on: the full autoplay
round (crates scattered, beacon green, win logged), a mid-play Ctrl+B that
stopped the scene, rebuilt both languages and resumed Play, and File >
Build Game producing a folder that runs standalone -- including booting
.NET from its own managed/ and running the C# manager.

**The exercise worked: it found five real engine holes in one session,**
each now fixed and covered by a scenetest check:

1. `File > New Project` was implemented but never put in the menu.
2. A rigid body gained during play never joined the simulation -- AddBody
   existed and had no callers. The first fired ball hung in the air.
   (Reconciled at the top of the fixed step, skipping condemned entities.)
3. C# collision/trigger callbacks were never delivered from a real
   simulation: DeliverContact resolved only the native component. The
   whole managed contact surface was written, bound, documented, tested at
   the table -- and unreachable. (`RageV.Builtin.ContactCounter` is the
   worked example and the regression probe.)
4. Nothing outside scenetest ever booted the .NET host or loaded a
   project's built assembly: in a fresh editor, C# was dead until a manual
   build, and a packaged game had no C# at all. (Project::Load now loads
   scripts for editor, runtime and packaged game by the same line, and the
   packager ships managed/.)
5. Editor C# builds always failed: the ScriptCore reference was passed as
   a CWD-relative path, which MSBuild resolves against the .csproj's
   directory. (Absolute now.)

Smaller finds: creating a project left the previous module loaded (fixed),
the C# manual's prefab example used `.prefab` for `.rprefab` (fixed), and
the content browser draws folder icons as `[/]` tofu (open, cosmetic).

**Sixth find, fixed the following session:** destroying a
material-bearing entity mid-play could free its descriptor sets while the
previous frame still executed. Entity destruction happens in the
simulation phase, *before* BeginFrame -- and a deleter pushed there landed
in the slot BeginFrame was about to flush that same iteration, behind a
fence that only covers a frame from two submissions ago. Deferred by zero
frames, in every process; the GPU usually won the race, which is why it
read as a rare runtime flake. DeletionQueue::Push now slots out-of-frame
pushes by the most recently *submitted* frame (see the comment in
VulkanCommon.h). Proven with a churn scene -- ~180 material destroys a
second under GPU load: 10 validation errors in 4000 frames before, zero
in 12000 after.

### In flight - phase 6 particles (user picked; UI/text deferred)

The user chose particles (2D + 3D, with a per-emitter GPU option like
Unreal's) plus the small audio wants. **Landed and verified this session,
Debug config, 802 checks, both backends, screenshots compared:**

- **Audio, protocol 5**: pitch on every one-shot and `PlayOneShotAt` (an
  arbitrary point), C++ and C# equal. Three appended table entries --
  `PlayOneShotAt`, `PlayOneShotPitched`, `PlayOneShot2DPitched`; the old
  two entries keep their exact shape, unused.
- **CPU particles (6.5/6.6)**: `ParticleEmitterComponent` (rate, burst,
  cone, gravity, drag, size/colour over life, spin, Facing
  Billboard|Flat, Blend Alpha|Additive, Space World|Local, optional
  sprite, `SimulateOnGpu` authored now so scenes never migrate),
  deterministic xorshift sim in `Particles/ParticleSystem.cpp` (per
  FRAME, not fixed step -- particles are presentation), instanced
  renderer in `Renderer/ParticleRenderer.cpp` + `particle.rvshader`
  (storage-buffer instances, base-instance push constant, alpha sorted
  twice / additive unsorted, depth test on write off), registry-driven
  inspector + serialization, `Assets::Manager::GetTexture` (new -- there
  was no plain 2D texture-by-handle loader at all).
  `SampleProject/assets/scenes/particles.rage` is the visual check: fire
  (additive), smoke (alpha, sorted), 2D fountain (flat), run via
  `RageVRuntime --project=SampleProject --scene=scenes/particles.rage`.

**Trap found while building the emitter, and since closed:** the
component registry's enum contract is int-sized, and nothing said so.
`Field<>` now static_asserts it (§5 has the invariant), so the mistake is
a build error naming the offending enum rather than a silent stomp. An
audit found no existing offender -- every registered enum was already
`uint32_t` or plain `int` -- so this was a live trap, not a live bug.

**6.7a RHI compute is done** (819 checks, both backends, 0 validation
messages). What landed:

- `ComputePipelineDesc` / `RHIComputePipeline` as a sibling of
  `RHIPipeline`, not a graphics desc with the graphics half defaulted.
  `GetWorkGroupSizeX()` and `GroupsFor(n)` come off reflection, so a
  resized `local_size_x` cannot leave a dispatch covering a fraction of
  its data.
- `ShaderReflection` gained `LocalSize[3]` and `Stages`. `Stages` is
  what answers "does this file have a compute stage" -- `LocalSize`
  cannot, because a legal `local_size_x = 1` is indistinguishable from
  the default.
- `BindComputePipeline`, `Dispatch`, `BufferBarrier(buffer, from, to)`
  with a `BufferSync` vocabulary of *uses* rather than Vulkan's
  access/stage pair (the only honest shape both backends implement).
  Both command lists follow whichever pipeline kind was bound last, so
  resource sets and push constants need no second statement of it.
- `RHIDevice::ExecuteImmediate(record)` -- a one-shot command list,
  submitted and waited on, outside the frame loop. Vulkan wraps its
  existing ImmediateSubmit (via a new `VulkanCommandList::Adopt`, since
  Begin/End belong to a frame); GL issues and `glFinish`es. It is what
  makes a dispatch testable headlessly, and it is generally useful.
- Resource sets no longer couple to a pipeline *kind*: both backends
  take the layout-bearing half plus an owning handle
  (`VulkanPipelineCommon`, `OpenGLPipelineBindings`).
- Caps: `SupportsCompute`, `MaxComputeWorkGroupSize/Count`.

The suite runs a real dispatch -- 100 elements over a 64-wide group, so
the tail group runs past the end and the shader's bounds check is
exercised -- and checks every element. A graphics-only shader is refused
a compute pipeline rather than given one that dispatches nothing.

**6.7b GPU particle simulation is done** (825 checks, both backends, 0
validation messages). `SimulateOnGpu` on an emitter now means it:
a fixed pool in a device-local SSBO, one compute dispatch per emitter
integrating and spawning, writing the *same instance layout* the CPU
renderer fills -- so both paths reach one vertex shader and "they look
the same" is structural rather than a promise.

Measured, Release, vsync off, ~16k particles across three emitters:

| | CPU sim | GPU sim |
|---|---|---|
| Vulkan | 3.30 ms | **1.37 ms** |
| OpenGL | 3.80 ms | **1.58 ms** |

Both 2.4x, and both GPU-bound afterwards (frame time ≈ GPU work), where
the CPU path had ~2 ms of CPU on top.

**The switch costs nothing**, as asked: the compute pipeline is built
once at Init, and an emitter's buffers are cached by UUID and kept while
the emitter exists -- not while it is *using* them. Toggling is a
branch. A suite check asserts the buffer pointer is identical across
off-and-back-on, so a future refactor that starts freeing on toggle
fails rather than just getting slower.

CPU→GPU seeds the pool from the live CPU particles, so the switch is
visually continuous. GPU→CPU is not: reading the pool back would stall,
which is the one thing the GPU path exists to avoid, so it drops what is
in flight and refills. Documented, not hidden.

**Two traps this cost, both worth knowing:**
- **A resource set holds one descriptor set per frame in flight, and
  Commit writes only the current frame's.** Committing once at creation
  populates one and leaves the rest never written -- validation catches
  it on frame two, a release build renders garbage. Rebind and commit
  every frame, which is what ParticleRenderer already did.
- **A persistently-mapped host-visible buffer written by a compute
  shader is a synchronisation point on OpenGL.** The state buffer was
  host-visible so the rare seed could be a memcpy; it made the GPU path
  *slower than the CPU one* (6.9 ms vs 3.3 ms) and stopped GPU
  timestamps resolving at all. Device-local, seed via staging.
  Barriers also bracket the whole batch rather than each dispatch --
  interleaved, every emitter waits for the one before it.

Local-space GPU emitters work: the pool simulates in the emitter's own
frame and the model matrix is applied when the instance is written, so
particles ride a moving emitter instead of being dragged by it -- the
same split the CPU renderer uses, where the transform belongs to
presentation rather than to integration.

Still open in the GPU path: alpha particles are not depth-sorted *within*
an emitter, because sorting would need a readback. Emitters are still
sorted against each other, so two overlapping effects are ordered
correctly. The artifact only shows when particles inside one emitter
differ strongly in colour or opacity; uniform smoke looks identical
sorted or not. `scenes/particles_gpu.rage` is the visual check.

### Done - 6.8, weighted-blended transparency

`ParticleBlend::WeightedBlended` is a third option beside Alpha and
Additive: alpha that needs no sort. Fragments accumulate into two
attachments -- a sum and a product, both order-independent -- and a
resolve pass works out the answer, so a thousand overlapping particles
land in any order and look the same. It is what a GPU emitter of smoke
wants, since sorting its pool would mean a readback.

`scenes/particles_oit.rage` is the check; both backends composite it with
no validation messages, 888 checks.

**It costs nothing when nothing uses it.** The two attachments and the
resolve pass only exist in frames whose scene contains a weighted
emitter, asked of the *scene* before the graph is described -- asking the
renderer would answer for the previous frame, which is a one-frame lag
and a dropped first frame.

**Four things this needed, in order, each committed separately:**
per-attachment blend state and the two weighted equations; multi-colour
graph targets with per-pass attachment subsets; `PreserveDepth`, so the
transparent pass clears its own attachments while keeping the depth the
scene wrote; and the emitter plumbing.

**Traps paid for along the way:**
- **`independentBlend` is a device feature.** Different blend state on
  different attachments is not free in Vulkan; without it every
  attachment must match the first. Validation said so plainly on the
  first run. Enabled where supported, and pipelines fall back to
  attachment zero's preset with a warning naming the pipeline when it is
  not -- wrong picture, not a crash.
- **OpenGL's `glDrawBuffers` is persistent framebuffer state.** It is set
  unconditionally now, because a pass that bound a subset would otherwise
  leave the next pass over that target writing into its selection.
- **`glClearBufferfv` indexes the draw-buffer list, not the
  framebuffer.** Selecting attachments 1 and 2 means clearing indices 0
  and 1.

The depth weight is the one thing left unproven; it is the START HERE
below.

### Superseded - the plan for 6.8

A third `ParticleBlend` beside Alpha and Additive, so an emitter can opt
into order-independent transparency instead of living with unsorted
alpha. It also fixes something the CPU path only approximates: emitters
are currently ordered by sorting their origins, which is wrong whenever
two of them interpenetrate.

**Piece 1 of 3 is done and committed** (`bcd34e7`): pipelines carry
per-attachment blend state, and `WeightedAccumulate` /
`WeightedRevealage` exist on both backends. `BlendPerAttachment` is
empty everywhere else, so nothing that predates it changed -- verified
by screenshot on both backends.

**What is left, in order:**

0. **The render graph has to grow two things first.** This was missed in
   the first estimate and is the bulk of the remaining work -- the
   particle side is small once it exists.

   `RGTargetDesc` carries exactly one colour format, and
   `RGPassBuilder::Write` binds a whole target; weighted blending needs
   two colour attachments written by one draw. And a graph target owns
   its own depth, so a separate transparent target would get a freshly
   cleared one and its particles would draw through walls.

   The answer follows from dynamic rendering: there is no VkFramebuffer,
   so every pass names its own attachments. One target can carry HDR
   colour, accumulation and revealage over a single depth image, and a
   *pass* binds a subset of it -- the scene pass attachment 0, the OIT
   pass attachments 1 and 2, both depth-testing against the same image.
   That needs:
   - `RGTargetDesc`: extra colour formats beyond `Color`, appended to
     the `RHI::RenderTargetDesc::ColorAttachments` vector that already
     exists and is already a vector. `SameShape` has to compare them or
     the pool will hand back a target of the wrong shape.
   - `RGPassBuilder::Write`: which attachment indices this pass binds,
     defaulting to all of them so no existing pass changes.
   - `Sample`: which attachment to read, for the resolve.

   The pipeline's declared `ColorFormats` must match the attachments the
   pass binds -- that is why the subset matters rather than just adding
   attachments to the scene target: every existing scene pipeline
   declares one colour format and would otherwise have to declare three.

1. **Then the transparent pass.** `FrameGraphBuilder::BuildFrame` is the
   extension point -- the scene render is already a `DrawScene` callback
   inside a graph pass, and `DrawOverlay` shows the pattern. Add a
   `DrawTransparent` callback writing attachments 1 and 2, accumulation
   `R16G16B16A16_SFLOAT` and revealage `R8`, clearing accumulation to
   zero and revealage to one. Depth test on, depth write off.
2. **A resolve pass**: fullscreen triangle compositing
   `accum.rgb / max(accum.a, epsilon)` over the scene colour, weighted by
   `1 - revealage`. Premultiplied-alpha blend against the HDR target.
3. **The plumbing**: the enum value, the registry dropdown, and
   `ParticleRenderer` splitting its pending draws so weighted emitters
   record into the transparent pass and the other two stay where they
   are. The compute path needs nothing -- the sim already writes a
   colour, and which pass consumes it is the renderer's business.

**Weights matter more than the plumbing.** The technique lives or dies
on the depth weight function; McGuire's paper gives several, and the
wrong one makes distant particles vanish or near ones dominate. Start
with the paper's equation 9 and *look at it* against the additive and
alpha versions of the same scene before believing it.

**Resume here:**
1. ~~6.7b GPU sim~~ -- done; the notes below are kept for context
1. **6.7b GPU sim**: per-emitter fixed pool of `MaxParticles` in a
   device-local SSBO, one compute pass integrates + emits (spawn count
   pushed per frame, hash(index,frame) RNG), dead particles collapse to
   size zero -- no compaction, no readback, no indirect draw in v1; the
   draw is the same `particle.rvshader` reading the GPU buffer (the
   instance layout matches on purpose). `SimulateOnGpu` currently parks
   the emitter entirely (CPU skips it, nothing draws) -- honest but
   dead; 6.7b is what makes the flag true. Then measure CPU vs GPU with
   `--benchmark` on a heavy emitter and record both numbers.

   **The switch must be ~instant** -- the user asked for this
   explicitly. The compute pipeline is created once at Init, and each
   emitter's GPU buffers are cached by entity UUID and *retained* when
   the flag goes off, so toggling is a branch rather than an
   allocation. CPU->GPU can seed the SSBO from the live pool for free
   and stay visually continuous; GPU->CPU drops the pool rather than
   stalling on a readback -- document that asymmetry rather than hiding
   it.

   The dispatch must be recorded **outside** the render pass, so the
   sim needs a hook before the scene's BeginRenderPass rather than
   inside Scene::OnRender. Both command lists assert on this.
2. ~~Docs~~ -- done. `particles.md` is a new manual page, the C++ and C#
   audio sections carry the pitch and `PlayOneShotAt` variants, and the
   site regenerates with 8 pages and a passing `--check`.
3. **Feedback for Knockdown** ("juice" -- the small sensory responses
   that make an action feel like it landed rather than merely
   registering: a puff on impact, a pitch-varied thud, a burst on the
   win). Concretely: an impact burst when a ball hits a crate and a
   confetti burst on the win, both fired from the C# scripts. It is not
   decoration -- it is the first end-to-end proof that a script can
   write `Burst` and see particles, which nothing currently tests
   outside the suite.
4. Consider a soft-dot sprite generator (tools/scripts convention) --
   untextured squares read fine but round sprites read better.

### Done - 6.9, the depth weight validated (and two bugs it found)

**The weight was wrong, and so was something bigger next to it.** Both were
found the same way: by building a scene whose correct answer is known, rather
than by looking at smoke and deciding it looked like smoke.

`tools/scripts/make_depth_scene.py` builds it, in two modes. `layers` is the
instrument: three emitters at 2, 20 and 200 units, one motionless particle
each, red near / green mid / blue far at alpha 0.5, every spatial quantity
scaled by distance so all three subtend the *same* screen angle. Each emitter
seeds the same RNG and draws from it identically, so all three take the same
random rotation and land perfectly concentric. It renders bit-identically run
to run, which makes pixel comparison meaningful. The right answer is
arithmetic -- 0.5 red over 0.5 green over 0.5 blue is 4:2:1, a red-dominant
pixel -- and sorted `Alpha` produces it, so the reference is not a stored
image but a render that is correct by construction. `smoke` is the same
experiment with real plumes, for the eyes.

**Bug one: the depth weight was a constant.** `gl_FragCoord.z` with the sample
scenes' near plane of 0.01 spans 0.995 to 1.0 across the whole world, so the
paper's `1 - z * 0.9` term varied by 4% from arm's reach to the horizon -- and
the `1e8` multiplier put the product 39x over the clamp ceiling anyway, at
every distance. Proven, not argued: replacing the entire expression with the
literal `1.0` gave a **pixel-identical** image. Weighted blending was silently
computing a flat average. The layers scene rendered mid-grey where the truth
is red-dominant.

**Bug two, found while fixing the first: the resolve was upside down on
Vulkan.** It is a fullscreen pass that samples two render targets, so it owed
the `FlipY` every post pass pays -- and being a single pass, nothing
downstream cancelled it. It shipped because the test scenes were plumes near
the middle of frame and flipped smoke still looks like smoke. §5 has the
rule, now stated as "anything that samples a render target and writes another
owes FlipY" rather than the old, and already-false, "nothing else samples a
render target".

**What was chosen, and why it is not the paper's.** Weight now falls as
`1/d` on linear view distance (`1.0 / gl_FragCoord.w`, exact for this
projection). The exponent is a straight monotone trade, measured both ways:

| weight | 3 layers, 2/20/200 | dense plumes |
|---|---|---|
| flat (what shipped) | 1.90 | 1.17 |
| d^-0.3 | **0.00** | 0.76 |
| **1/d (chosen)** | 3.05 | 0.29 |
| 1/d^2 | 4.11 | 0.22 |
| equation 9 | 4.23 | **0.20** |

Steeper is better on plumes and worse across depth, without exception. Read
the first column carefully: `flat` scores well there while being the one
option that is definitely broken, because grey sits near the mean of that
stack while ordering nothing. And `d^-0.3` is *exact* there -- equal-alpha
layers want their weight halving per layer however far apart they are -- and
useless in a plume. No depth-only weight is right for both; that is the
technique, not a defect.

So the exponent was settled on a property the table does not show. Under the
paper's 1e-2..3e3 clamp, equation 9 only **discriminates** between 1.5 and 100
units and clamps flat outside it -- which is why it draws the 20-unit and
200-unit layers identically. 1/d^2 spans 0.94 to 520. 1/d spans 0.03 to 9000,
wider than any scene this engine will hold, and the range it buys is at the
*near* end, where a camera standing inside a smoke cloud lives. It gives up
0.09 against equation 9 on plumes to never fail that way.

**`tools/scripts/check_oit.py` is the regression check** -- silhouette against
the sorted render, and near-beats-far at the centre. Both failures were
reintroduced deliberately to confirm it fails, and it names the flip as a flip.
It is not in scenetest because scenetest cannot read pixels back; it runs the
runtime and compares screenshots.

Verified: 888 checks both backends, 0 validation messages, Debug/Release/Dist,
`rvdoc --check`, and `particles_oit.rage` re-rendered on both backends with the
weighted content now landing in the same place on each.

### Deferred - the rest of the particle wants

Named here so they are not rediscovered as ideas. In the order they were
judged worth doing, which is not the order they were asked about:

- **Particle collision.** Two different features wearing one word. Analytic
  shapes (per-emitter planes and spheres) are cheap, identical on CPU and GPU,
  need no frame reordering, and cover sparks off a floor -- do this one first.
  Screen-space depth-buffer collision is what Unreal's GPU sprites do and is
  general, but it needs the sim to sample depth, which means moving the
  dispatch after the depth pass (it runs in `OnUpdateRuntime`, before the
  render pass, because both command lists assert dispatch-outside-render-pass)
  and accepting one-frame-old depth. Per-particle Jolt queries are CPU-only
  and O(particles), which defeats the GPU path entirely.
- **Sub-emitters.** The heaviest, and the one that wants a design note before
  code. Needs particle *death events*. The CPU side is tractable; the GPU side
  needs an append buffer the sim writes and indirect dispatch to consume it
  without a readback, plus an ownership model -- pooled child emitters, never
  an entity per particle.

### Done - 6.10, curves beyond start and end

**Complete, all seven steps.** Every ramp that was a two-point lerp on
normalised age can now be a shape. The user's three calls (2026-08-11): curves
are **assets**, the editor is a **real draggable one**, and size, colour and
alpha each get their own -- alpha separate from the colour gradient, so one
gradient can be shared between emitters that fade differently.

**The design decision that made it small.** `AssetHandle` was already
`FieldType::Asset`, so a handle field cost nothing new: the inspector drew it,
the serializer wrote it, and the C# bridge reached it as a path string, all
without changes. An inline curve would have needed a new `FieldType`, a new
text form and a new inspector control before the first curve rendered. **One
asset type, not two** -- a scalar curve and a colour gradient want the same
file, handle and sampling and differ only in channel count.

What exists now:

- `Asset/Curve.h` -- keys sorted on insert, so sampling (per particle, per
  frame) never pays for authoring (when somebody drags a point). `Curve::Baked`
  is the 64-sample table **both** paths read.
- `Asset/CurveSerializer` -- `.rcurve`, YAML so a curve stays diffable. Loading
  goes through `AddKey`, so a hand-edited file with keys out of order loads as
  the curve it describes.
- `Manager::GetCurve / CreateCurve / ReloadCurve / GetBakedCurve`. **A valid
  handle never answers null** -- a missing file gives an *empty* curve that
  evaluates to its fallback, so the emitter samples a value instead of every
  call site remembering a branch. Only an invalid handle answers null.
  `ReloadCurve` exists because the cache is otherwise the stale-mip-chain bug.
- `SizeCurve`, `ColorGradient`, `AlphaCurve` on the emitter. **The pairs still
  work and are not legacy**: an unset handle leaves that channel to
  `SizeStart/SizeEnd` or the colour pair, so every existing scene is
  unaffected and nobody must author a curve to get a particle.
- `Particles::Evaluate` -- the override rule, as a free function, because
  three things must agree on it: the CPU path, the compute shader and the
  suite.
- The GPU path is **handed the answer, not the ingredients**: the CPU resolves
  the ramps once per emitter per frame into `ColorRamp[64]`/`SizeRamp[64]` in
  the emitter UBO, so the shader has no copy of the rule to drift from. 2 KB
  per emitter to delete a class of bug.
- `UI/CurveEditor` -- draggable points, wired into the inspector from
  `SceneHierarchyPanel.cpp`.
- `tools/scripts/make_curve_presets.py` writes the stock curves and the
  side-by-side scene; `scenes/particles_curves.rage` and its `_gpu` twin.

**Measured, and it found something.** Under `Additive` the CPU and GPU paths
agree to **0.10 of 255** on both backends. Under `Alpha` a curve-driven
emitter differs by **2.7, seven times the run-to-run noise** -- and switching
to an order-independent blend collapses it, which pins the gap on the GPU's
inability to sort within an emitter rather than on the ramp. **Curves make
that gap worse**, because they widen how much particles differ in colour and
opacity, which is exactly when unsorted alpha goes wrong. That is now the
measured argument for 6.11.

**One property worth keeping in mind**: baking rounds off a curve peak that
falls between two of the 64 samples, by under one table step. A check pins it,
so changing `kSize` shows as a number moving rather than silently.

### The sample project's flame

The demo scene's pedestal is lit: `Brazier Flame`, a child of the pedestal in
`scenes/demo.rage`, sitting on the ornament. It is the first particle effect
in the repository with a real sprite and the first thing that uses all of
6.10 at once -- worth keeping as the reference for what a finished emitter
looks like.

Additive because fire is light rather than matter; local space, so it rides
the pedestal and its uniform scale sizes the particles (0.75 is what makes a
bonfire read as a brazier); upward *emitter* gravity for buoyancy, which is
one field because an emitter's gravity is not the world's; and all three
ramps curved, because a flame is exactly the case where none of them is a
straight line -- a tongue is widest a third of the way up, which two
endpoints cannot say in either direction.

It lives in the demo scene rather than a scene of its own on purpose: an
effect belongs in the scene it decorates, and a showcase nobody opens is not
a showcase. `make_curve_presets.py` writes the curves it uses (`flame_size`,
`flame_alpha`, `ember`) and the scene references them by handle.

The sprite came in at 3840x2160 and 16:9. A particle quad is square and
stretches its sprite to fill, so it was cropped to its own alpha bounding
box, squared about that box's centre and resized to 512 -- 2.5 MB to 236 KB.
Squared about the *content* rather than the image, because a billboard
rotates about its middle and an off-centre sprite wobbles as it spins.

### Done - the architecture book (docs/design/)

**90 pages, 12 chapters, ~31,000 words, 64 code listings, 6 vector diagrams,
11 figures.** `architecture.html` is the source, `RageV-Architecture.pdf` the
deliverable, `README.md` the regeneration command and its traps.

The brief, which grew over the session: a knowledge-transfer document written
so a software engineer **learning game development** could follow it and arrive
at this engine -- explicitly modelled on learnopengl.com, a path from A to B
rather than a description of a finished artefact.

Chapter 2 is the mathematics (spaces, the projection matrix derived, why depth
is not distance, colour, the rendering equation, alpha compositing ending in
the algebra showing a constant OIT weight cancels). Chapter 3 is the hardware
and both APIs, ending with weighted-blended transparency traced all the way
down through each. Chapter 4 is the build order. Chapters 5-8 are the engine
as teaching rather than reference. Chapter 9 is every invariant grouped by
*kind of mistake*. Chapter 10 is the verification methodology. **Chapter 12 is
nine milestones with real code**, each ending with a proof you can run.

**Three traps in producing it**, all in `docs/design/README.md`:

- **Chrome does not paint the root background when printing.** Pages end up
  transparent, which most readers draw white and a dark-mode reader draws
  **black, with black text on it**. A fixed `body::before` in `@media print`
  paints a real white rectangle. Verify by decompressing the PDF's content
  streams and counting pages with a white fill -- the regeneration reports it.
- **Chrome refuses to write the PDF into the repository** ("Access is denied")
  and needs `--user-data-dir`. Print to a temp path and copy.
- **The Claude Code preview pane forces dark mode** regardless of
  `color-scheme`. Render with headless Chrome to see true colours.

Also: code blocks are light, not dark -- a dark block on a light page inverts
to light-on-light in a reader's dark mode and prints as a solid black
rectangle. And `table { width: 100% }` will flatten an inline matrix unless
`.mat` sets `width: auto`.

**What would still improve it** (judgement, not a list): a continuity
read-through. The chapters were written across several sessions and the seams
may show -- repeated explanations, cross-references assuming a different
reading order, and a tone that shifts between the reference chapters and the
walkthroughs.

### Done - Knockdown's juice, and two staleness bugs it exposed

**The last open item from the particle work, and the first end-to-end proof a
C# script can drive the particle system.** Nothing outside the suite exercised
that path.

Two emitters in `Main.rage`, neither emitting on its own. **Impact** is dust,
moved to `Collision.Point` and burst by whichever crate was hit -- one shared
emitter for all six crates, in world space so the dust hangs where it was born
rather than following the emitter to the next hit. **Confetti** sits above the
platform and fires once on the win from `GameManager`. The existing
impact-speed ramp drives both the sound and the dust, so a light tap looks as
light as it sounds.

**Two pre-existing breakages this uncovered, both of which made the game
unrunnable and neither caused by the change:**

- The managed assembly in `Knockdown/Scripts/bin/` predated protocol 5, so
  every collision threw `MissingMethodException` on `PlayOneShot` -- the
  signature gained a pitch parameter. **The engine looks for
  `Scripts/bin/<Name>.dll`**, not the SDK's nested `bin/Debug/net8.0/`; a hand
  run of `dotnet build` must copy it up.
- The native game module failed to load with **error 127**, so `Launcher` and
  `Ball` were unknown scripts and the game could not be aimed or fired.
  Rebuild it with:
  `cmake -S Knockdown/Source -B Knockdown/bin/module -DRAGEV_ENGINE=<repo>/build`
  then `cmake --build Knockdown/bin/module --config Debug`.

Both fixed; the runtime now reports 2 native and 3 C# scripts with no
unknown-script warnings.

**A verification lesson worth keeping.** The dust works -- the *user* confirmed
it by playing. My own screenshots kept missing it and I was drifting toward
the wrong conclusion: a 0.45 s effect sampled at four arbitrary frames is a
coin flip, and the brightness threshold I measured with would not have caught
grey dust against a pale sky anyway. What I had actually established was that
an authored burst renders and that `SetComponentField` returned true with the
emitter positioned; I had not closed the gap between those two facts. **When a
visual check keeps coming back negative, question the instrument before the
feature.**

### START HERE - text and game UI

**The largest gap in the engine, agreed as next (2026-08-11).** It is the item
the mini game hit hardest: Knockdown cannot say "press F to reset", show a
score, or put up a title, and communicates entirely through a light and four
sounds. Workable for one mechanic, not for two.

Nothing here is designed yet -- that is the first task, not the code. The
shape phase 6 originally sketched:

1. **MSDF text.** A multi-channel signed-distance font atlas, so glyphs stay
   sharp at any size from one texture. Generating the atlas is an offline
   step; `tools/scripts` is where the generator belongs, by convention.
2. **A screen-space canvas that is deliberately *not* ImGui.** ImGui is the
   editor's, and a game's UI has different requirements: it ships, it is
   skinned, it is authored in scenes rather than in code, and it must work in
   a packaged build with no editor present.
3. **Widgets**: text, image, button, and a layout rule simple enough to
   author.

**Two decisions to make before writing code**, and they interact:

- **Where does UI live in the scene?** Components on entities (fits the ECS,
  the inspector and serialization for free -- the §5.3 argument) or a separate
  UI tree? Components almost certainly, given how much the registry already
  gives away.
- **Where does it render?** A pass after tonemapping, since UI is authored in
  sRGB and must not be tonemapped. That is a new graph pass writing the LDR
  target -- straightforward given §6.3, but decide it deliberately.

**It pairs naturally with the per-frame script hook below**, because UI
animation wants per-frame timing, and doing the hook first means UI can use it
rather than being retrofitted onto it.

### Pending - the per-frame script hook

**Agreed as the next engine gap after the current work** (2026-08-11), and
recorded here rather than done because a design document took priority.

`OnUpdate` is per *fixed step*, by design. Every other presentation system --
animators, physics interpolation, world transforms, audio, both particle
paths -- already runs per frame in `Scene::OnUpdateRuntime`, each with a
comment saying why. Scripts are the one thing still pinned to the simulation
rate, and that is now the second-largest gap in phase 6 after text/UI.

The sharpest argument is not convenience: `SyncTransforms` blends the last
two simulation states by `Application::GetInterpolationAlpha()`, so the world
already moves smoothly at any display rate. A camera moved from `OnUpdate`
updates on one frame in four at 240 Hz against a world that updates on all
four -- which reads *worse* than if neither were smoothed, because the
stutter is differential. No script can even see the alpha today.

Two decisions it forces:

1. **The name.** `OnUpdate` already means fixed step in both manuals and
   every existing script, so it cannot be reused. Something that says frame
   -- `OnFrame(dt)` -- makes the varying `dt` obvious at the call site.
2. **Where in the frame.** A camera script must read *interpolated*
   transforms, so it belongs after `UpdateWorldTransforms` and before
   `UpdateAudio` -- around Scene.cpp's audio call. Earlier and it reads last
   step's positions, which defeats the purpose.

Both languages ship it together (they are equals at protocol 5), and the
real risk is misuse: gameplay in a per-frame hook reintroduces frame-rate
dependence, which is what the fixed step exists to prevent. That is a naming
and documentation problem, not a code one.

### After that - 6.11, GPU alpha self-sorting

Now cheaper than it looks, and no longer urgent -- weighted blending is the
answer for GPU smoke and it works properly as of 6.9. This is for emitters
that want *exact* alpha.

Bitonic sort in compute, key = view depth, no readback; the pipelines,
dispatch and buffer barriers all exist. The pleasant part: `MaxParticles`
defaults to 1000, and anything up to 2048 sorts inside a **single workgroup**
in shared memory -- one dispatch, not the 55-stage global ladder. The draw
then reads an index buffer rather than the pool directly.

### Superseded - the original 6.9 brief

**The one thing in the particle work that is written but not proven.**

Weighted-blended transparency lives or dies on its depth weight function.
`particle_weighted.rvshader` uses McGuire and Bavoil's equation 9:

```glsl
float weight = clamp(pow(min(1.0, color.a * 10.0) + 0.01, 3.0)
                   * 1e8
                   * pow(1.0 - gl_FragCoord.z * 0.9, 3.0),
                   1e-2, 3e3);
```

**Why this needs eyes rather than a test.** The failure mode is not a
crash or a wrong number -- it is a picture that looks fine. Too steep a
curve and distant particles quietly disappear; too shallow and the
nearest fragment dominates everything behind it. Both look like
plausible smoke until compared against the sorted version, and the paper
itself says the constants want tuning per scene depth range.

It has been compared once, on `scenes/particles_oit.rage`, whose whole
depth range is about ten units. That proves the plumbing, not the curve.

**What to actually do:**
1. Build a scene with real depth range -- particles at 2, 20 and 200
   units, ideally the same emitter repeated so only distance differs.
2. Render it three ways: `Alpha` (sorted, the reference), `Additive`,
   and `WeightedBlended`. The alpha version is the truth to compare
   against, since a single CPU emitter *is* correctly sorted.
3. Look for the two named failures specifically. Distant emitters
   thinner than the sorted version means the depth term is too strong;
   near ones washing out what is behind them means it is too weak.
4. If it needs tuning, the constants to move are the `0.9` and the two
   exponents. The paper offers several alternative weights (equations
   7 through 10) trading depth sensitivity against alpha sensitivity --
   try 10 if 9 is too aggressive at range.
5. Record what was compared and what was chosen, here. A weight tuned
   once and undocumented is one somebody re-derives.

Everything under it is verified: per-attachment blend, multi-attachment
graph targets, the transparent pass and its shared depth, and the
resolve. 888 checks, both backends, no validation messages.

### After that - the rest of phase 6

### Picking what comes next

The papercut list is the phase 6 ballot, and what actually hurt while
building a real game, in order:

1. **No game UI or text.** The game cannot say "press F to reset", show a
   score, or put up a title. Everything had to be communicated with a
   light and four sounds -- workable for one mechanic, not for two.
   **Now the START HERE**, agreed 2026-08-11.
2. **No per-frame script hook.** OnUpdate is per fixed step by design, so
   camera smoothing and feedback that has to move with the display
   (recoil, shake) have nowhere to live; the camera rides the launcher
   rigidly. **Still open**, second, and analysed in full below -- it pairs
   with UI, which wants per-frame timing for its own animation.
3. ~~No particles.~~ Done -- CPU and GPU, three blend modes, curves, and a
   burst fired from a C# script in Knockdown.
4. ~~Small API wants~~ -- done: `PlayOneShotAt` and pitch on every
   one-shot, both languages, protocol 5.

Both remaining items are phase 6. After them the ranked list is: **6.11 GPU
alpha self-sorting** (bitonic, single workgroup for pools <= 2048, now with a
measured argument from 6.10), then **particle collision** (analytic shapes
first, screen-space second) and **sub-emitters**, then phase 7's deferred
debts.

### Worth knowing before extending scripting further

- **The NativeApi table is append-only and at protocol 5.** Inserting a
  field in the middle rebinds every field after it on one side only; the
  crash lands somewhere unrelated. Append, bump kProtocolVersion on both
  sides (Interop.h and Interop.cs), and the handshake tests follow the
  constant automatically.
- The C# reload refuses while instances are alive, on both sides of the
  boundary: the editor parks the swap until Stop, and ScriptHost refuses with
  a log line if anything reaches it anyway.
- The unload-verification warning ("the previous script context did not
  unload") firing means a type from the project assembly is being held
  somewhere -- a static, an event, a cache. That is a leak of every version
  ever built, and it is loud on purpose.
- RetireProjectContext is [MethodImpl(NoInlining)] and must stay that way:
  inlined, the JIT may pin its locals to the caller's frame and the collect
  loop spins against the very reference it is waiting on.

### Housekeeping that used to be folklore, now automatic

- **Stale exports.def**: deleting an engine source used to strand its symbols
  in the DLL's export list and fail the link. A pre-build pass
  (cmake/PruneStaleObjects.cmake) prunes objects whose sources are gone and
  the .def with them. The one quirk left is the VS generator's: the first
  build after deleting a globbed source may fail compiling the ghost file;
  the second succeeds. That is CONFIGURE_DEPENDS reloading, not a bug here.
- **Dock layout proportions**: ImGui stores split sizes in pixels; the editor
  rescales the dock tree on window resize and persists the reference size in
  panels.ini, which also remembers which panels are open. Layout, size and
  visibility all survive restarts now.

### Done — Phase 5, C# scripting (`XL`)

**Phase 3 is complete.** Skeletal animation landed with the rest of it: a
skinned vertex format, a skinned PBR shader and a skinned depth shader sharing
the static ones' lighting through includes, a second pipeline in `Renderer3D`,
per-instance bone matrices, and an `AnimatorComponent` that advances a clip and
composes a pose.

`scenes/skinning.rage` in the sample project is the end-to-end check: two posts
side by side, one animated and one in its bind pose. The animated one bends and
**its shadow bends with it**; the other does not move at all, and is
pixel-identical between the two backends.

Phase 5 is the last MVP+ item, and the dependency argument for doing it now is
the strongest one left: C# has to mirror a native surface that has stopped
moving. Seven items in ROADMAP §5.

**5.1 is done.** `RageV/src/RageV/Managed/DotNetHost.h` boots CoreCLR through
hostfxr and hands back function pointers to static managed methods;
`RageVScriptCore/` is the managed assembly; `scenetest` proves the round trip
both ways, including the protocol-mismatch path. Three things about it worth
knowing before touching it:

- **The engine does not link nethost and does not need the .NET SDK to build.**
  `hostfxr.dll` is found at runtime (`DOTNET_ROOT`, then the registry, then
  `%ProgramFiles%\dotnet`) and the four API types are hand-declared. A clone
  still builds with nothing but a compiler; CMake reports C# as unavailable and
  moves on.
- **`EnableDynamicLoading` in the csproj** is what emits
  `RageV.ScriptCore.runtimeconfig.json`. Without that file
  `hostfxr_initialize_for_runtime_config` fails with a number rather than a
  sentence, and it is the usual reason a first attempt at hosting .NET fails.
- **`Interop.ProtocolVersion` is checked on the first call.** A partial rebuild
  that leaves one side stale is otherwise a stack corruption somewhere
  unrelated, minutes later.

**5.2 is done.** `RageV/src/RageV/Managed/Interop.h` is the boundary, and it
uses a different mechanism in each direction on purpose:

- **Managed calling native: a table of function pointers**, handed over once at
  bootstrap. Not `[DllImport]`: at the time there was no `RageV.dll` to import
  from, the engine being a static library. There is one now -- it became shared
  so a project could load a C++ game module -- so the original reason has
  expired, but the table stays. It skips the per-call marshalling stub, and
  binding by name across a boundary that already versions itself with
  `kProtocolVersion` would be a second answer to a question that has one.
- **Native calling managed: `[UnmanagedCallersOnly]` entry points**, reached
  through hostfxr. Static, blittable arguments only, and **no exception may
  leave one** -- an exception crossing an unmanaged frame terminates the process
  rather than unwinding, so each entry point catches at its own edge.

`NativeApi`'s field order *is* the ABI, mirrored field for field in `Native.cs`.
Adding to the end is the only safe edit; inserting in the middle rebinds every
field after it on one side only, and the result is a call through a pointer to
the wrong function. `Interop::kProtocolVersion` and `Interop.ProtocolVersion`
are compared before anything else is called, which turns a partial rebuild into
a sentence rather than a crash somewhere unrelated.

An entity crosses as a **UUID, never a pointer** -- play mode restores a scene
by recreating entities, so a pointer handed to a script dangles the moment Stop
is pressed. The scene binding is a raw `Scene*` for the same reason: managed
code must not be able to keep a scene alive past Stop.

The managed `SelfTest` walks every shape that crosses -- struct in, struct out,
string out, string back including the truncation contract, a float return, an
unknown entity, the log -- and reports each as its own bit. `scenetest` asserts
them individually, because a single pass/fail would say "interop is broken" and
leave the next person to work out which of nine things it was.

**5.3 is done.** `RageVScriptCore/src/Engine.cs` is the class library --
`Entity`, `Script`, `Input`, `Time`, `Log`, `Collision`, `Vector3` -- and
`ScriptHost.cs` instantiates a `Script` subclass by name, drives its lifecycle,
and delivers contacts. `BuiltinScripts.cs` has C# `Spinner` and `Mover` that are
line-for-line comparable with the native ones in `Scripts/BuiltinScripts.cpp`,
which is the clearest available statement that the two APIs are one API.

**5.4 is done.** A generated project scaffolds `Scripts/<Name>.csproj` and an
`Example.cs` that already compiles. **File > Build Scripts** shells out to
`dotnet build`, parses the diagnostics into file/line/code/message, shows them in
a Script Build panel with errors above warnings, and loads the result. Verified
end to end in `scenetest`: a project is created, compiled, and a script type
*from that project* is instantiated by name and stepped.

Three things in there that were not obvious:

- **`cmd` eats the outer quotes.** `_popen` runs through `cmd /c`, which strips
  the first and last quote of a command line beginning with one -- so a quoted
  `C:\Program Files\dotnet\dotnet.exe` arrives unquoted and cmd reports that
  `'C:\Program'` is not a command. The whole line is wrapped in one more pair of
  quotes to give cmd a pair to eat.
- **The project assembly must resolve `RageV.ScriptCore` to the copy already
  loaded**, via an `AssemblyLoadContext.Default.Resolving` handler. The obvious
  alternative -- copying the DLL next to the project's output -- is a trap: two
  files with the same assembly name load as two different assemblies, so the
  project's `Script` is not the engine's `Script`, `IsAssignableFrom` fails, and
  the error says the type is not a Script when it plainly is.
- **The assembly is the evidence, not the exit code.** A build can print nothing
  alarming and still not have produced anything, so success requires the file to
  exist *and* zero errors.

**5.6 is done, and with it a project can have both languages.**
`ManagedScriptComponent` puts a C# script on an entity: a type dropdown of what
the loaded assemblies actually contain, the script's fields reflected out of the
type, and both saved into the scene. `Scene::StepManagedScripts` runs them on
the fixed step, after the native pass so the ordering between the two languages
is stated rather than being whatever the component pools do this build.

Four decisions in there:

- **Fields come from reflecting the type, not from anything stored.** A script
  that gains a field shows it immediately; one that loses a field stops showing
  it with no scene migration. Only values somebody *changed* are stored, so
  editing a default in code reaches every entity that never overrode it.
- **Private fields are editable.** `private float m_Speed = 1.2f;` is how C# is
  written, and it would otherwise be the one thing nobody can tune. Unity needs
  a `[SerializeField]` attribute for this; not requiring one is fewer concepts
  for the same result. `readonly` and `static` are skipped.
- **Values cross as text, in the invariant culture.** The scene file is text
  anyway, and a tagged union would be two languages' worth of upkeep on
  something that happens when a person types. Invariant specifically: a machine
  with a comma decimal separator would otherwise write `1,5` into a scene that
  then loads nowhere else.
- **The handle is not copied and not serialized.** EnTT copies components when a
  scene is duplicated, which is what play mode does on every press of Play, and
  two components owning one managed instance would both destroy it.

**C++ scripts have editable fields too**, declared at registration because C++
has no reflection:

```cpp
RV_REGISTER_SCRIPT(Spinner).Field<&Spinner::Speed>("Speed");
```

The member must be **public** -- a registration names it from outside the class,
and reaching a private one needs a friend declaration in every script. C# has the
opposite rule, and for a good reason: reflection can read private fields, and
demanding `public` there would mean telling people to write worse C# for the
inspector's benefit. Both languages converge on one set of inspector rows, one
set of stored values, and one scene format.

The script component draws its own rows rather than using the generic field
list, and **`desc.Fields` is deliberately empty for both script components**.
Listing `ScriptName` there produced two "Script" rows -- one of them a plain text
box that could name a script which does not exist. The name is still written
under the same YAML key by `SerializeExtra`, so scenes from before that change
load unaltered.

Rows use `BeginField`/`EndField` like every other component: label in a
fixed-width left column, widget filling the rest. **New Script...** is the last
entry of the script dropdown rather than a button beside it -- choosing a script
and making one are the same decision. The popup cannot be opened from inside the
combo (the combo closes and takes it with it), so the entry sets a flag and the
popup opens after `EndCombo`.

A **Language** row (C++ / C#) converts the
entity between the two components. The name and field overrides are deliberately
*not* carried across: two scripts that share a name are still different scripts,
and moving one's tuning values onto the other applies numbers to fields that only
coincidentally match.

**New Script...** writes a working template, never a blank file. For C# it goes
into the project's `Scripts/` and is selected immediately. For C++ it goes into
the engine's own `Scripts/` folder -- and only when the editor was built from
source, which it knows via `RV_ENGINE_SCRIPTS_DIR`; a packaged editor explains
what to do by hand instead.

> [!NOTE]
> **This used to need `/WHOLEARCHIVE`, and no longer does.** A static library
> drops any object file nothing references, and a translation unit whose only
> contents are a static registrar -- exactly what a script file is -- has no
> referenced symbol. The script compiled, linked, and never appeared in the
> dropdown. There was no fix on the library side either: a
> `#pragma comment(linker, "/include:...")` lives in the object file being
> discarded, so the directive never reaches the linker. That was tried first.
>
> The engine is a DLL now, linked from its object files rather than from an
> archive, so every translation unit is in it whether or not anything calls into
> it. The same will be true of a project's game module. Worth knowing anyway:
> the failure returns the moment script code lands in a `.lib` again.
>
> `Scripts/TemplateProbe.cpp` is the generated template kept in the tree so every
> build compiles and registers it. It is what caught this, and it is what would
> catch it again.

**C++ scripts still have to be compiled into the engine or game binary**, so a
project cannot add one without rebuilding the engine. That is the remaining
asymmetry between the two languages. The machinery works end to end and
there is no way to attach it: no managed script component, no inspector entry,
the editor could not put one on an entity. 5.6 fixed that; see below.

C++ scripts have their own limit worth stating in the same breath: they must be
compiled into the engine or game binary, so a *project* cannot add one without
rebuilding the engine.

**5.0 was completed before 5.2, as planned.** No third-party type in a public
header, and the public API segregated into `RageV::Math::`, `RageV::Audio::`,
`RageV::Physics::` and so on. Binding C# against `glm::vec3` and then renaming it
means writing the interop, the marshalling and the class library twice — which
is the exact failure the "C# last" argument was meant to avoid, arriving through
a different door.

**5.0 landed, and went further than planned.** `RageV/src/RageV/Math/` is the
engine's own maths: `Vec2/3/4`, `UVec4`, `Mat3/4`, `Quat`, the operators inline
in `Functions.h`, and inversion, decomposition, projections and the whole
quaternion set in `Math.cpp`. **glm is not linked into the engine at all.** All
31 public headers and 35 `.cpp` files were migrated off it.

**The thing that makes that defensible is `tools/scenetest/GlmBridge.h`.** glm
is still vendored and still linked — into `scenetest` only — purely as an
oracle. 34 checks compare every operation against it on deliberately awkward
values, because axis-aligned test numbers pass through a transposed matrix
unharmed. Delete that block and the engine is running unverified arithmetic in
the hottest path of a renderer. It is the single most important test in the
suite and the least obvious one.

Four decisions in there worth not re-litigating:

- **Conversions in the bridge are member-wise, never a `reinterpret_cast`.** The
  layouts match and casting would work, but glm has shipped quaternions both
  `w`-first and `w`-last depending on version and build flags, so a cast that is
  right in one configuration rotates everything wrongly in another.
- **`Normalize` guards against zero length**, which glm does not. glm returns
  NaNs, a NaN in a transform takes the object and every child with it, and
  nothing reports anything. There is a check for it.
- **Right-handed, clip-space depth in `[0, 1]`.** Written at the top of
  `Math.cpp` because nothing else defines it any more. The textbook `[-1, 1]`
  projections differ in one column and still produce a picture.
- **`Math::Max` is float-only.** Integer maxima are `std::max`'s job.

**What is left of 5.0:** the domain namespaces — `RageV::Audio::`,
`RageV::Physics::`, `RageV::Assets::`. `RageV::Math::` and `RageV::RHI::` are
already in that shape. Watch for `Renderer`, `Scene` and `Input`, which exist as
*types* at the scope those namespaces would occupy.

### After MVP+ — phases 6, 7 and 8

Added 2026-08-09 at the owner's direction. **None of them blocks anything**, and
they are not in dependency order, because past MVP+ there is no spine left to
respect. ROADMAP §5 has the detail; the shape is:

- **Phase 6 — text, UI and particles.** The only remaining gap a *player* would
  notice rather than a developer. MSDF text, a screen-space canvas that is not
  ImGui, widgets, and CPU-simulated 2D/3D particles with a measurement before
  any compute pass.
- **Phase 7 — the deferred debts, collected.** Archive format and the virtual
  file system it needs, asset cooking, materials as assets, billboard icons,
  the animation blend nothing currently drives, skinned bounds that cover the
  animation, per-object probes, depth sorting, SMAA and TAA.
- **Phase 8 — formerly out of scope, reopened.** GI, bindless, GPU-driven
  rendering, terrain, navigation, networking, other platforms, XR, FBX, visual
  scripting, a plugin ecosystem. Each carries the reason it was excluded as its
  cost. **Two have prerequisites that are decisions rather than work**: bindless
  means dropping OpenGL or maintaining two binding models, and terrain means
  deleting `experiments/terrain/Chunk` first, which makes one entity per voxel
  face.

**The recommendation in ROADMAP §9 is to build a game before starting Phase 6.**
Forty-odd items with no ordering is a menu, and needing one is the only reliable
way to know which.

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
- **RageV implements its own math, and it costs nothing. Measured properly, on
  the third attempt.** The method matters more than the number here.

  The first comparison said **+3.0%** and was wrong. The two sets were taken
  forty minutes and several rebuilds apart; re-running the *unchanged* baseline
  later gave 2.275 ms against its own earlier 2.246 ms, so the machine had
  drifted +1.3% with no code involved. The owner called that as thermal drift
  before the data did.

  Settled by building each version, copying it aside, and alternating runs so
  drift hits both equally. Eight pairs, 600 frames each, Release, Vulkan, vsync
  off, no compilation between runs.

  Delegating every operation to glm, against glm at the call sites:

      paired difference  +0.021 ms (+0.9%),  sd 0.035,  t = 1.69 on 7 df
      the delegating build was faster in 3 of 8 pairs

  RageV's own implementation, against glm at the call sites:

      paired difference  +0.013 ms (+0.6%),  sd 0.087,  t = 0.44 on 7 df
      the native build was faster in 4 of 8 pairs

  Neither is significant at p < 0.05. **Anything under about 1.5% on this scene
  is below what this method can resolve**, and a difference measured across
  separate sessions is not a measurement at all — it is the machine's mood.

  Three arrangements were tried and all three measure the same: glm at the call
  sites, glm behind an out-of-line wrapper, and no glm at all. The choice
  between them was therefore never a performance question, which is worth
  knowing before anyone re-opens it.

### Performance, all unmeasured### Performance, all unmeasured

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

### The test that disarmed itself

Worth its own heading, because it is the most dangerous shape a defect can take
in this repository and it very nearly shipped.

The glm migration was done with a substitution script. It rewrote `glm::vec3` to
`Vec3` everywhere — including inside the `scenetest` block whose entire job is
comparing `RageV::Math` against glm. Twenty-eight checks were left comparing the
answer to itself. **They passed.** The suite reported OK, and would have reported
OK forever, while covering nothing.

The same suite is what caught the argument-order difference between
`glm::decompose` and `Math::Decompose`, which would otherwise have silently
swapped position with scale in the glTF importer and the physics body sync. So
the disarmed test was guarding a real bug at the time it was disarmed.

Found by reading the diff, not by anything automated. Nothing in the build could
have caught it: the code compiled, the checks ran, the count did not change.

**The rule that came out of it:** a test whose value comes from comparing against
something external must say so in a comment, and anything that rewrites the tree
must exclude it. `Functions.h`, `Math.cpp`, `GlmBridge.h` and the block itself
all now carry that warning.

**The general form** — and this is the part worth remembering — *a check that
passes because it checked nothing is worse than no check at all*, because it also
consumes the attention that would have gone to writing a real one. `rvdoc` fails
when it parses suspiciously few members for the same reason.

### A 3% regression that was not there

The math migration appeared to cost 3% of a CPU-bound frame. It did not; the two
measurements were taken forty minutes and several full rebuilds apart, and the
machine had drifted. The owner said so before the data did.

Re-running the *unchanged* baseline later gave a different number from itself.
The fix was to build both versions, copy them aside, and alternate runs so drift
hits both equally — at which point the difference fell to +0.6% with the new
version faster in half the pairs, which is noise.

**The rule:** a benchmark comparison is only valid if both sides are measured in
the same session, alternating, with no compilation in between. §9 has the method
and the numbers. Sequential A-then-B on this machine cannot resolve better than
about 1.5%.

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
| The swapchain was destroyed before its replacement existed | Since the port | `vkDeviceWaitIdle` covers the queues, not the presentation engine. Only crashed when the compositor happened to be a frame behind, so it survived every resize until a present-mode change made it frequent enough to notice |
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
8. **A setting that works one way round is not a working setting.** Vsync off
   at startup was fast and vsync off at runtime was not, and the shared code
   path made "the OS decides" sound obvious enough to write down as a
   conclusion. It was wrong, and it was wrong for two sessions. Varying the one
   thing that had never been varied -- the present mode itself -- took ten
   minutes and answered it.
9. **Read the whole log, not the tail.** `--benchmark` prints its summary last,
   so `| tail` showed the numbers and hid seven validation errors underneath
   them. They had been printed on every benchmark run for as long as the flag
   had existed.

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
- **When a timing bug will not reproduce, go looking for the known hazard of
  that shape.** The vsync crash survived 25 forced toggles under validation in
  both build configurations. Failing to reproduce it was itself the evidence:
  a deterministic bug would have fired on the first one, so the search moved
  from "what did I break" to "what in this path is order-dependent" -- and
  swapchain teardown had the textbook mistake sitting in it. Confirmed fixed by
  the person who could reproduce it.
- **Ask whether a defect predates the change.** The OpenGL flicker was found
  while verifying instancing and looked like its fault. Stashing the work and
  rebuilding took ten minutes and showed it was already there — which is the
  difference between a regression to revert and a bug to schedule.
- **Reporting a fix by what was logged rather than by what changed.** The
  readiness work was presented as six modules' log lines when the actual fix
  was three new flags and a test. A log line cannot be tested and is not a fix.

