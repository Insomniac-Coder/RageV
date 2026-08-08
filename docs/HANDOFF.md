# RageV — handoff

**Read this first.** Written 2026-08-08, on branch `vulkan-overhaul` (18 commits
ahead of `main`, not merged, not pushed).

Companion docs: [ROADMAP.md](ROADMAP.md) for where this is going and in what
order; [ARCHITECTURE.md](ARCHITECTURE.md) for renderer design detail.

---

## 1. What this is

A Windows game engine, originally a Hazel/Cherno-lineage 2D engine that had been
shelved mid-way through a Vulkan port. It now has a working RHI with **two
complete backends** (Vulkan and OpenGL), PBR mesh rendering, and an editor that
runs on either backend.

**Stated end goal:** ease of use of Unity, some of Unreal's graphical fidelity,
scope closer to Godot. PBR and shadows were the near-term targets; PBR is done,
shadows are not.

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

Run the editor:
```bash
build/bin/Debug/RageVEditor/RageVEditor.exe --rhi=vulkan
build/bin/Debug/RageVEditor/RageVEditor.exe --rhi=opengl
```

Backend also settable in `ragev.ini` next to the executable. It is a
**restart-time** choice by design: the window is created differently per backend
(Vulkan needs `GLFW_CLIENT_API=GLFW_NO_API` before creation), so switching live
would mean recreating the window, swapchain and every GPU resource.

| Target | Purpose |
|---|---|
| `RageVEditor` | The editor. Opens on a demo scene. |
| `shaderinfo` | Compiles a `.rvshader`, prints reflection + generated GLSL. |
| `rhismoke` | Drives either backend headlessly (`--rhi=`, frame count). |
| `scenetest` | Serialization round trip and undo/redo, 35 checks. |
| `Sandbox` | Stale, predates the RHI, **off by default**. |

### Verifying a change

Both of these should be run after any renderer change:

Run from each tool's own directory -- assets are staged per target.

```bash
build/bin/Debug/rhismoke/rhismoke.exe 120 --rhi=vulkan
build/bin/Debug/rhismoke/rhismoke.exe 120 --rhi=opengl
build/bin/Debug/scenetest/scenetest.exe --rhi=vulkan
build/bin/Debug/scenetest/scenetest.exe --rhi=opengl
```

Then run the editor on both and check the log contains **zero `[Vulkan]`
lines**. Validation and synchronization validation are both on in Debug; any
`[Vulkan]` line is a real defect.

---

## 3. Environment facts

- **Vulkan SDK 1.4.357.0 installed** (matches vendored header version exactly).
  Only the *core* component — every optional box duplicates something the repo
  vendors, and a second copy risks a version mismatch.
- Building does **not** require the SDK. `Vulkan-Headers` + `volk` are vendored.
  The SDK only supplies validation layers and tooling.
- Driver reports `apiVersion` 1.4.325. That is **not** an SDK version — it is the
  spec revision the GPU driver implements, and normally trails the header
  revision. Nothing to fix.
- GPU: NVIDIA RTX 5070 Ti Laptop, driver 591.91.
- RenderDoc installed. `vkSetDebugUtilsObjectNameEXT` and command-buffer debug
  labels are wired throughout, so captures show named objects and scopes.

---

## 4. Architecture

```
Application / Layers / Scene (EnTT)
        |
  Renderer2D (quads)   Renderer3D (meshes, PBR)
        |                    |
        +--------- RHI ------+
                   |
        +----------+----------+
   Platform/OpenGL      Platform/Vulkan
```

One renderer over two backends. The legacy `RenderAPI` / `RenderCommand` path
and its OpenGL-only resource wrappers are **deleted**.

`Application` owns the `RHIDevice` and drives `BeginFrame`/`EndFrame` around the
layer stack. Layers take no command-list parameter, so `Renderer` holds the
active one — the same role `RenderCommand`'s static played, scoped to a frame.

### Frame shape

```cpp
RHICommandList* cmd = device->BeginFrame();   // nullptr => skip this frame
if (!cmd) return;                             // do NOT call EndFrame

set->SetUniformBuffer(0, ubo);
set->Commit();                                // writes only the current frame's set

cmd->BeginRenderPass({ .Target = nullptr });  // nullptr targets the swapchain
cmd->BindPipeline(pipeline);
cmd->BindResourceSet(0, set);
cmd->DrawIndexed(indexCount);
cmd->EndRenderPass();

device->EndFrame();
```

### Shaders

One source of truth: Vulkan-flavoured GLSL with explicit `set`/`binding`, in
`.rvshader` files split by `#type vertex` / `#type fragment`.

```
.rvshader --glslang--> SPIR-V --+--> Vulkan (direct)
                                |
                                +--SPIRV-Cross--> GLSL 450 --> OpenGL
```

SPIRV-Cross also recovers descriptor set layouts, push-constant ranges and the
vertex input layout. Pipelines are built from that reflection, so a shader edit
cannot silently disagree with the C++ side.

Inspect any shader:
```bash
build/bin/Debug/shaderinfo/shaderinfo.exe assets/shaders/pbr.rvshader
```

---

## 5. Invariants that are load-bearing

Every one of these was a real bug that cost time. Breaking them tends to produce
intermittent corruption rather than an obvious failure.

### Vulkan

- **Render-finished semaphores are per swapchain image, not per frame in
  flight.** A present keeps its semaphore busy until the image is reacquired.
- **The in-flight fence is reset only once the frame is definitely proceeding.**
  Resetting before a possible early return leaves it unsignalled and deadlocks
  the next pass through that slot.
- **Resources never touch the device from their destructor.** They hold a
  `shared_ptr<DeletionQueue>` that outlives the device; a `Ref` surviving the
  device is then a no-op rather than a use-after-free.
- **`VulkanResourceSet` holds one descriptor set per frame in flight** and
  `Commit()` writes only the current frame's. This is what makes updates safe
  without extra synchronisation.
- **Every element of a sampler array must be written**, even ones a draw will
  not read — the shader indexes dynamically, so validation treats all as
  accessed.
- **Swapchain layout barriers must use `srcStageMask =
  COLOR_ATTACHMENT_OUTPUT`**, not `TOP_OF_PIPE`. The submit waits on the acquire
  semaphore at that stage; `TOP_OF_PIPE` forms no execution dependency and races
  the presentation engine.
- **Depth layout depends on format.** `D32_SFLOAT_S8_UINT` includes the stencil
  aspect, and the spec forbids `DEPTH_ATTACHMENT_OPTIMAL` there. Use
  `DepthAttachmentLayout()`.
- **Dynamic rendering does not do the initial layout transition** a render pass
  would; the swapchain depth image is transitioned once at creation.
- **`vkCmdPushConstants` stage flags must cover every stage of every overlapping
  range.** Derived from reflection in `VulkanCommandList::PushConstants` so
  callers cannot get it wrong.
- ImGui's Vulkan backend requires `VK_KHR_dynamic_rendering` enabled
  **explicitly** even though it is core in 1.3, and allocates `SAMPLER` and
  `SAMPLED_IMAGE` descriptors separately from combined ones.

### OpenGL

- **Flat binding points are assigned densely from 0 per resource type.**
  `set * 16 + binding` overflows `GL_MAX_TEXTURE_IMAGE_UNITS` (32 here) with a
  32-element sampler array. The map is computed once and **shared** between GLSL
  generation and resource-set binding; recomputing it separately is how they
  drift apart.
- **`glClear` respects the depth mask**, so the mask is forced on before
  clearing.
- Depth-only targets set draw/read buffer to `GL_NONE` or the framebuffer is
  incomplete. Shadow maps will need this.
- Push constants are emulated as a uniform buffer rewritten per draw.

### Engine

- **Anything rewritten every frame needs one instance per frame in flight.**
  Renderer2D's vertex stream, both scene UBOs, and `Material`'s param buffer all
  do this. A single instance is silently corrupted under load — the hardest
  possible time to diagnose it.
- Layers must be destroyed before the device (`LayerStack::Clear` in
  `~Application`), since layers own GPU resources.
- Each app needs its own output directory. A shared one let Sandbox's older
  shader overwrite the editor's, which masked a genuine compile failure.

---

## 6. Current state

### Done

| Area | State |
|---|---|
| Build | CMake, 12 vendored deps pinned to Vulkan SDK 1.4.357 |
| RHI | Device, command list, pipelines, resource sets, render targets |
| Vulkan backend | Complete. Dynamic rendering, VMA, frames in flight, deferred destruction |
| OpenGL backend | Complete. GL 4.5 DSA |
| Backend switching | `--rhi=` / `ragev.ini`, restart-time |
| Shaders | glslang → SPIR-V, SPIRV-Cross reflection + GLSL, disk cache |
| Renderer2D | Batched quads, texture slot dedup |
| Renderer3D | Meshes, push-constant transforms |
| PBR | Cook-Torrance, metallic-roughness, 5 material maps |
| Primitives | Cube, sphere, cylinder, plane, quad — cached per device |
| Lights | Directional / point / spot, intensity, range, cones |
| Editor UI | Menus, toolbar, panels, red-on-black theme, gizmos |
| Serialization | Registry-driven, version 3, lossless round trip |
| Identity | Real UUIDs, `Scene::GetEntityByUUID` |
| Hierarchy | Parent/child, world transforms, drag-to-reparent |
| Reflection | `ComponentRegistry` drives inspector + serializer + add menu |
| Undo/redo | Command stack; create, delete, reparent, fields, gizmo, ambient |
| Editor camera | Fly / orbit / pan / zoom, F to frame selection |
| Ambient | Scene colour + intensity, serialized |

### Not done

- **Shadows.** The RHI carries everything needed (depth-only targets,
  comparison samplers, slope-scaled bias, cube/array textures) but no shadow
  pass exists. The Render Settings toggle is present and disabled with a
  tooltip.
- **IBL.** The PBR ambient term is a flat constant standing in for it.
- **HDR pipeline.** Tone mapping (ACES) runs inside the PBR shader because the
  target is LDR. A float target + dedicated tonemap pass is the clean version.
- **Skybox / cubemaps.** `TextureType::TextureCube` exists in the RHI; nothing
  uses it.
- **Editor camera.** The viewport renders through the scene's *primary camera* —
  there is no independent fly/orbit camera. This is the single biggest usability
  gap for inspecting anything.
- **Readback.** Verification is "frames complete with no validation errors", not
  "the pixels are correct". A `RHIDevice::Readback()` would let the smoke tests
  assert on real output and double as screenshot support.
- **Texture maps in the editor.** `Material` supports five maps and
  `TextureLoader` can load them, but there is no UI to assign one.
- **Undo/redo.** Menu entries present and disabled.
- **Multi-viewport ImGui on Vulkan.** OpenGL only; each extra OS window needs
  its own swapchain.
- **Sandbox.** Predates the RHI, off by default, not ported.
- **`PushConstants` on OpenGL for shaders with no push-constant block** warns.

---

## 7. Decisions already made (do not relitigate)

- **CMake**, not premake. Old build files deleted.
- **Vendored Vulkan deps**, no SDK requirement to build.
- **Dynamic rendering** — requires Vulkan 1.3. No `VkRenderPass` anywhere.
- **Backend is restart-time**, not hot-swappable.
- **"Entity", not "GameObject"** — matches `RageV::Entity` / EnTT and avoids
  Unity's vocabulary. If ever renamed again, the C++ type must move too.
- **Theme rule: red means "you can act on this, or it is acting now."**
  Selection, hover, active, focus, checkmarks, drag grabs, active tab. Structure
  stays greyscale. An accent spread over decoration stops being a signal.
- **Menu entries for features that do not exist are shown disabled with a
  tooltip explaining why**, never omitted and never wired to nothing.

---

## 8. Agreed next steps

Chosen by the user, in order:

1. ~~**Research pass**~~ — **done**, see [ROADMAP.md](ROADMAP.md).
2. ~~**Phase 0**~~ — **done**. Editor camera, entity UUIDs, transform
   hierarchy, component registry, undo/redo, lossless round-trip test, native
   script lifetime fixes. Plus a scene ambient control and `Chunk` quarantined
   to `experiments/`. **Next is Phase 1: the asset pipeline.**
3. **Scripting — both paths.** User explicitly wants both:
   - Enrich the native C++ `ScriptableEntity` API (transform/component helpers,
     input, timing, spawn/find/destroy, scene queries).
   - Embed C# on top. Large: runtime host, interop layer, assembly build
     pipeline, managed class library.
   - Order matters: the native API defines the surface, C# mirrors it. Design
     once, bind twice.
4. Renderer continuation: skybox + cubemaps → IBL → HDR/tonemap pass → shadows.

**Flagged to the user, worth keeping in view:** the biggest gaps to an MVP+
engine are usually *not* rendering. They are asset import (glTF/FBX), an asset
database with references that survive renaming, play-in-editor with state
save/restore, physics, audio, and build/packaging. This project has a strong
renderer and **no asset pipeline** — the inverse of where most hobby engines
stall.

---

## 9. Known rough edges

- `quadshader.glsl`, `simpleshader.glsl`, `textureshader.glsl` in
  `RageVEditor/assets/shaders/` are **legacy leftovers** from the pre-RHI
  renderer. Only `quad.rvshader` and `pbr.rvshader` are used. Safe to delete.
- Vendored EnTT is 3.10.0, checked in as a 5 MB **UTF-16LE** single header
  rather than a submodule like the other eleven dependencies. The encoding
  defeats ordinary text tooling -- grep finds nothing in it.
- `Chunk`/`Perlin` are in `experiments/terrain/` and not built. See the README
  there.
