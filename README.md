# RageV

A Windows game engine: EnTT scene system, ImGui editor with docking and gizmos,
YAML scene serialization, batched 2D renderer, and a Vulkan RHI.

## Build

```bash
cmake --preset vs2022
cmake --build build --config Debug
```

Requires VS 2022 Build Tools and a Vulkan-capable driver. **No Vulkan SDK
installation is needed** — headers are vendored and `volk` loads the driver at
runtime. Install the LunarG SDK if you want validation layers during
development.

Clone with submodules:

```bash
git clone --recurse-submodules <url>
```

## Layout

- `RageV/` — engine static library
- `RageVEditor/` — the editor application
- `Sandbox/` — sample application
- `tools/shaderinfo` — compile a shader and dump its reflection
- `tools/vksmoke` — exercise the Vulkan backend headlessly

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the renderer design, the
Vulkan invariants worth knowing before changing anything, and the roadmap to PBR
and shadows.
