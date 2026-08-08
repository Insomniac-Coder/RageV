# RageV

A Windows game engine with a backend-agnostic RHI over **Vulkan and OpenGL**,
Cook-Torrance PBR mesh rendering, an EnTT scene system, Jolt physics with
collision callbacks, miniaudio for 3D sound, and an ImGui editor with docking
and gizmos.

Switch backend at startup:

```bash
RageVEditor.exe --rhi=vulkan
RageVEditor.exe --rhi=opengl
```

Run a project with no editor:

```bash
RageVRuntime.exe --project=path/to/YourGame
```

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
- `RageVRuntime/` — the standalone runtime: a project, no editor
- `SampleProject/` — the project the editor and tests open by default
- `Sandbox/` — sample application, stale and off by default
- `tools/shaderinfo` — compile a shader and dump its reflection
- `tools/rhismoke` — exercise either backend headlessly

**Start with [docs/HANDOFF.md](docs/HANDOFF.md)** — current state, what is done
and what is not, and the invariants that are load-bearing.
[docs/ROADMAP.md](docs/ROADMAP.md) is where it goes next and in what order,
benchmarked against Unity, Godot and Unreal.
[docs/ENGINE-NOTES.md](docs/ENGINE-NOTES.md) is engine architecture and
rendering research distilled into decisions for this engine.
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) has the renderer design in detail.
