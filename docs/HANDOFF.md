# RageV — handoff

**Read this first.** Updated 2026-08-08, on branch `vulkan-overhaul`
(36 commits ahead of `main`, **not merged, not pushed**).

Companion docs:
- [ROADMAP.md](ROADMAP.md) — where this is going, in dependency order.
- [ENGINE-NOTES.md](ENGINE-NOTES.md) — research distilled into decisions. Read
  §1 before touching the simulation loop and §3 before touching physics.
- [ARCHITECTURE.md](ARCHITECTURE.md) — renderer design detail.

---

## 1. What this is

A Windows game engine. Originally a Hazel/Cherno-lineage 2D engine, shelved
mid-way through a Vulkan port, revived and taken through two and a half phases
of a planned roadmap.

**Stated goal:** ease of use of Unity, some of Unreal's graphical fidelity,
scope closer to Godot.

The engine loop — *import → place → script → play → export* — has four of five
links working. Export is the missing one.

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
| `RageVEditor` | The editor. Opens on a demo scene. |
| `scenetest` | 155 checks: serialization, undo, assets, scripts, physics. |
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
```

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
- 13 vendored submodules: spdlog, imgui, glm, yaml-cpp, ImGuizmo, PerlinNoise,
  GLFW, Vulkan-Headers, volk, VulkanMemoryAllocator, glslang, **cgltf**,
  **JoltPhysics** (v5.6.0).

---

## 4. Architecture

```
Application  (fixed-step loop, InputMap, AssetRegistry/Manager)
     |
   Layers -> EditorLayer
     |
   Scene (EnTT)  --  PhysicsWorld (Jolt)   ScriptRegistry
     |
  Renderer2D / Renderer3D
     |
    RHI  ->  Platform/Vulkan | Platform/OpenGL
```

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
| Restore on Stop | — | full snapshot |

`Scene::OnUpdateEditor` / `OnUpdateRuntime` / `OnFixedUpdateRuntime`.
`OnRuntimeStart` builds the physics world; `OnRuntimeStop` tears it down.

---

## 5. Invariants that are load-bearing

Every one was a real bug. Breaking them tends to produce intermittent corruption
or silence rather than an obvious failure.

### Renderer

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

### Input

- Sampled once per frame; edges are held until a fixed step consumes them. A
  frame with no steps must not lose a press, and two steps must not see one
  press twice.

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
| Scripting | Registry by name, rich native API, three built-in scripts |
| Physics | Jolt — rigid bodies, 3 collider shapes, triggers, raycasts |
| Editor | Two viewports, proportional dock layout, red-on-black theme |
| Tests | `scenetest`, **155 checks**, green on both backends |

### Not done

- **Collision callbacks into scripts** (roadmap 2.6). Physics can push things;
  a script cannot yet react to a hit. Next most valuable item.
- **Audio** (2.5). Nothing at all. miniaudio is the chosen library.
- **Shadows, IBL, skybox, HDR/post** (Phase 3). Ambient is a flat constant.
- **Clustered forward** — the 8-light cap stands.
- **Culling** — everything is drawn every frame.
- **Skeletal animation.**
- **Build/export** (Phase 4). No runtime target, no packaging. This is the
  remaining severed link in the engine loop.
- **C# scripting** (Phase 5). Deliberately last.
- Physics debug draw — colliders are invisible.
- Mesh/convex colliders — box, sphere and capsule only.
- Texture map assignment UI; materials are not assets yet.
- Multi-select, copy/paste, focus-on-selection for multiple entities.
- Multi-viewport ImGui on Vulkan (OpenGL only).

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

1. **Collision callbacks into scripts** (2.6, `M`). `OnCollisionEnter` /
   `OnTriggerEnter` on `ScriptableEntity`, fed by a Jolt contact listener.
   Finishes making physics useful.
2. **Audio** (2.5, `L`). miniaudio: 2D and 3D sources, listener, volume groups.
3. **Phase 3 fidelity** — render graph → skybox/cubemaps → IBL → HDR/post →
   shadows. A parallel track against a stable RHI; pulling it forward costs no
   structural debt.
4. **Phase 4 ship** — project concept, standalone runtime, packaging. This is
   the definition of done for MVP.
5. **Phase 5 C#.** Last, because it must mirror a stable native surface.

---

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
