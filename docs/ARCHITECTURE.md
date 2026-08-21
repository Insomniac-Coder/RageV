# RageV rendering architecture

## Building

```bash
cmake --preset vs2022
cmake --build build --config Debug
```

CMake ships with VS 2022 Build Tools at
`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`
if it is not on `PATH`.

Every dependency is a vendored submodule built from source. **No installed Vulkan
SDK is required** — `Vulkan-Headers` provides the API and `volk` loads the entry
points at runtime. Installing the LunarG SDK is still worth doing during
development: it is the only way to get the validation layers, and the device
prints a warning and continues without them when they are absent.

Outputs land in `build/bin/<Config>/<Target>/`, each with its own copy of
`assets/`. They are deliberately not shared — a target stages the shaders it
was built against, so one that has not been rebuilt keeps running the old
ones.

| Target | What it is |
|---|---|
| `RageVEditor` | The editor. Runs on either backend. |
| `shaderinfo` | Compiles a `.rvshader` and prints reflection + generated GLSL. |
| `rhismoke` | Drives either backend end to end without the editor (`--rhi=vulkan\|opengl`). |

## Layers

```
Application / Layers / Scene (EnTT)
        |
   Renderer2D
        |
       RHI
        |
   +----+----+
   |         |
Platform/  Platform/
 OpenGL     Vulkan
```

One renderer, two backends. The legacy `RenderAPI`/`RenderCommand` path and its
OpenGL-only resource wrappers have been removed.

`Application` owns the device and drives `BeginFrame`/`EndFrame` around the layer
stack. Layers take no command-list parameter, so `Renderer` holds the active one
-- the same role `RenderCommand`'s static played, but scoped to a frame.

The backend is chosen at startup by `EngineConfig` (`--rhi=vulkan|opengl`, or
`ragev.ini`) and is fixed for the process lifetime. That is deliberate: the
window is created differently per backend, so switching live would mean
recreating the window, the swapchain and every GPU resource.

## The RHI

`RageV/src/RageV/Renderer/RHI/`. Backend-agnostic; no header here includes a
backend header.

The old `RenderAPI`/`RenderCommand` pair is OpenGL-shaped — immediate `Bind()`,
`SetUniform()` keyed by string, implicit global state, and a `DrawIndexed` with
no notion of a frame, a target, or a bound pipeline. There is nowhere in that
design to put a Vulkan command buffer, which is the actual reason the original
Vulkan port stalled at image views.

| Header | Role |
|---|---|
| `RHITypes.h` | Formats, usages, blend/depth/raster state, vertex layout, resource-binding descriptions, `DeviceCaps` |
| `RHIResources.h` | Buffers, textures, samplers, render targets |
| `RHIShader.h` | SPIR-V blobs + recovered reflection |
| `RHIPipeline.h` | Shader + vertex layout + fixed-function state as one immutable object |
| `RHIResourceSet.h` | Descriptor sets; writes batched behind `Commit()` |
| `RHICommandList.h` | Recorded commands, explicit render passes, push constants |
| `RHIDevice.h` | Frame lifecycle, swapchain, resource factories |

Frame shape:

```cpp
RHICommandList* cmd = device->BeginFrame();   // nullptr => skip this frame
if (!cmd) return;                             // do NOT call EndFrame

set->SetUniformBuffer(0, ubo);
set->Commit();                                // writes only the current frame's set

cmd->BeginRenderPass({ .Target = nullptr });  // nullptr targets the swapchain
cmd->BindPipeline(pipeline);
cmd->BindResourceSet(0, set);
cmd->BindVertexBuffer(0, vbo);
cmd->BindIndexBuffer(ibo, IndexType::UInt32);
cmd->DrawIndexed(indexCount);
cmd->EndRenderPass();

device->EndFrame();
```

`BeginFrame()` returning `nullptr` is normal — a resized or minimised window.
Calling `EndFrame()` after it is a bug.

## Shaders

One source of truth: Vulkan-flavoured GLSL with explicit `set`/`binding`
decorations, in `.rvshader` files split by `#type vertex` / `#type fragment`.

```
.rvshader --glslang--> SPIR-V --+--> Vulkan (direct)
                                |
                                +--SPIRV-Cross--> GLSL 450 --> OpenGL
```

SPIRV-Cross also recovers descriptor set layouts, push-constant ranges and the
vertex input layout, and `VulkanPipeline` builds its Vulkan objects from that
reflection. Editing a shader cannot silently disagree with the C++ side.

Compiled SPIR-V is cached on disk by source hash
(`ShaderCompiler::SetCacheDirectory`).

**Caveat:** SPIR-V carries vertex input locations and types but not offsets or
stride, because those are the application's choice. Reflection assumes one
interleaved binding packed in ascending location order. Anything with a
different packing must pass an explicit `VertexLayout` in the pipeline
description.

Inspect any shader with:

```bash
build/bin/Debug/shaderinfo/shaderinfo.exe assets/shaders/quad.rvshader
```

## Vulkan backend notes

`RageV/src/Platform/Vulkan/`. Requires Vulkan 1.3 with `dynamicRendering` and
`synchronization2`, so there are no `VkRenderPass` or `VkFramebuffer` objects
anywhere.

Things that are load-bearing and easy to break:

- **Render-finished semaphores are per swapchain image, not per frame in
  flight.** A present keeps its semaphore busy until the image is reacquired;
  a per-frame semaphore trips validation on 3+ image swapchains.
- **The in-flight fence is reset only once the frame is definitely going
  ahead.** Resetting before a possible early return (out-of-date swapchain)
  leaves it unsignalled and deadlocks the next pass through that slot.
- **Resource destructors defer through `VulkanDevice::DeferDestruction`.** The
  GPU may still be reading a resource from an in-flight frame. This is also why
  every resource must be released *before* the device.
- **`VulkanResourceSet` holds one descriptor set per frame in flight** and
  `Commit()` writes only the current frame's. That is what makes updating a set
  safe without extra synchronisation.
- **Every element of a sampler array must be written**, even ones a given draw
  will not read: the shader indexes dynamically, so validation treats all of
  them as potentially accessed.
- The viewport is **Y-flipped** so the RHI presents OpenGL's convention to
  callers. `glm` is built with `GLM_FORCE_DEPTH_ZERO_TO_ONE` to match Vulkan's
  `[0,1]` clip range.

## OpenGL backend notes

`RageV/src/Platform/OpenGL/OpenGLRHI.*`, built on 4.5+ direct-state-access.

- **Binding assignment is the subtle part.** Vulkan addresses resources by
  `(set, binding)`; GL has one flat namespace per resource type.
  `FlatBindingMap` assigns densely from 0 per type. A scheme that spaces sets
  apart (`set * 16 + binding`) looks fine until a real shader hits it: with
  `GL_MAX_TEXTURE_IMAGE_UNITS == 32`, the quad shader's 32-element sampler array
  at set 1 would land on units 16..47 and fail to link. The map is computed once
  per shader and shared by GLSL generation and the resource-set binding code --
  recomputing it separately in the two places is how they drift apart.
- Vertex format lives in the VAO; buffers attach at bind time. GL 4.5's
  separate-format model maps onto the RHI's pipeline/buffer split directly.
- `glClear` respects the depth mask, so the mask is forced on before clearing.
  Otherwise a pipeline that disabled depth writes silently skips the clear.
- The RHI expresses a viewport flip as Vulkan's negative height; the GL backend
  normalises it back to a positive rect.
- Depth-only targets set draw/read buffer to `GL_NONE` or the framebuffer is
  incomplete. Shadow maps need this.
- `PushConstants` is unimplemented and warns. GL has no equivalent; emulating it
  means a uniform buffer per pipeline layout, worth adding with the first shader
  that needs it.

## What is not done

- **`PushConstants` on OpenGL** warns and does nothing. Nothing uses it yet;
  emulating it means a uniform buffer per pipeline layout, worth adding with the
  first shader that needs it.
- **Multi-viewport ImGui is OpenGL-only.** Each extra OS window needs its own
  swapchain, which is a second presentation path the engine does not drive.
- **No readback path.** Verification is currently "the frame completed without
  validation errors", not "the pixels are right". A
  `RHIDevice::Readback(renderTarget)` would let the smoke test assert on actual
  output, and would double as screenshot support.

## Road to PBR and shadows

The type layer already carries what these need: cubemap and 2D-array textures,
depth-only render targets, comparison samplers with border colour, slope-scaled
depth bias, and HDR formats (`R16G16B16A16_SFLOAT`, `B10G11R11_UFLOAT`).

Suggested order:

1. **Finish the port** (items 1–4 above) so there is one renderer, not two.
2. **Forward PBR.** Replace the Blinn-Phong in `quad.rvshader` with
   Cook-Torrance (GGX + Smith + Schlick). Material textures go in their own
   descriptor set — albedo, normal, metallic-roughness, occlusion, emissive —
   which is why `MaxTextureSlots` and a per-material set matter more than the
   current single 32-sampler array.
3. **HDR + tonemap.** Render into an `R16G16B16A16_SFLOAT` target, then a
   fullscreen tonemap pass. Needed before lighting values stop clipping.
4. **Directional shadows.** Depth-only `RHIRenderTarget`, `Layers = cascade
   count`, `DepthSampled = true`, `RasterizerState::DepthBiasEnable` with a
   slope-scaled bias, and a comparison sampler for hardware PCF. `RenderPassBeginInfo`
   already supports depth-only passes.
5. **Point/spot shadows.** `TextureType::TextureCube` with six passes, or a
   layered pass once geometry-shader or multiview support is added.
6. **IBL.** Equirectangular → cubemap, irradiance convolution, prefiltered
   specular chain, BRDF LUT. All compute or fullscreen passes over cube targets;
   `TextureUsage::Storage` is already in the type layer for the compute route.

Steps 2 onwards are much easier once step 1 is done, because otherwise every
feature has to be written twice.
