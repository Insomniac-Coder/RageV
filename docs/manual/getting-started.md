# Getting started

## What you need

- **Windows 10 or 11**, 64-bit.
- **Visual Studio 2022**, or just the Build Tools. Either provides the compiler
  and CMake.
- **A GPU with Vulkan 1.3 or OpenGL 4.5.** Both backends ship; the engine picks
  one at startup and you can change it from the editor.

You do **not** need the Vulkan SDK to build. The headers, `volk` and the memory
allocator are vendored. Install the SDK only if you want validation layers,
which you do if you intend to touch the renderer.

## Build it

```bash
cmake --preset vs2022
cmake --build build --config Debug
```

> [!TRAP]
> CMake is not on `PATH` on a stock Visual Studio install. It ships inside the
> Build Tools at
> `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`.
> Add that to `PATH` or call it by full path — otherwise the first command above
> fails with "command not found" and looks like a missing dependency when it is
> not.

Three configurations exist and mean different things:

| Configuration | What it is for |
|---|---|
| `Debug` | Development. Validation layers and synchronization validation are on. |
| `Release` | Optimised, with the editor and assertions still present. |
| `Dist` | What you ship. No editor tooling, no validation. |

## Run the editor

```bash
build/bin/Debug/RageVEditor/RageVEditor.exe
```

> [!TRAP]
> Run each executable **from its own directory**. Assets are staged per target,
> so launching the editor from the repository root finds no shaders and shows
> you a black window that looks like a driver problem.

The editor opens the sample project's start scene. From here you can select
entities in the hierarchy, edit them in the inspector, drag assets in from the
content browser, and press **Play** to run the scene.

## Create a project

**File → New Project…** Pick a name and a location; the engine makes a folder
named after the project and fills it in:

```text
MyGame/
  MyGame.rvproject     name, asset root, start scene, simulation rate
  assets/
    scenes/            MyGame.rvproject points at scenes/Main.rage
    models/
    textures/
    audio/
    prefabs/
  bin/                 builds land here
  .gitignore           keeps bin/ out of version control
```

The starter scene is deliberately not empty — a ground plane, a cube, a sphere
and an angled directional light. An engine that opens on nothing makes the first
five minutes a hunt for which of the six things you need is missing, and "there
is no light so everything is black" reads as a broken install rather than as an
empty scene. Press **Play** and it already works.

The folder layout is the same in every project on purpose. It is what makes a
path like `models/rock.gltf` mean the same thing in someone else's project, and
`scenetest` checks that `Project::Create` still produces it.

> [!NOTE]
> The dialog asks you to name a *file*, because the editor only has file
> dialogs so far. What gets created is the **folder** — pick
> `C:\Games\MyGame` and you get `C:\Games\MyGame\MyGame.rvproject` with
> everything else beside it, not a loose project file in `C:\Games`.

## Run a project without the editor

`RageVRuntime` is the same engine with the editor removed — it is what your
players will effectively be running.

```bash
build/bin/Debug/RageVRuntime/RageVRuntime.exe --project=SampleProject
```

## Package a project

**File → Build Game** packages the open project into its own `bin/` — the
runtime, the assets, a config file, and no editor. Output goes to
`<project>/bin/<Name>/`, so the project folder can be zipped, moved or handed
over with its build intact and nobody has to remember where the last one went.
**Build Game As…** puts it somewhere else.

The same thing from the command line, for a scripted build:

```bash
build/bin/Debug/rvpack/rvpack.exe SampleProject SampleProject/bin/Sample --overwrite
```

`rvpack` is headless and needs no GPU. It refuses to write into a directory that
already has files in it unless you pass `--overwrite`, so a scripted build
cannot quietly flatten a folder somebody pointed it at by mistake.

## Command-line flags

Every flag below can also go in a `ragev.ini` file next to the executable, as
`key=value` lines. The command line wins over the file.

| Flag | Meaning |
|---|---|
| `--rhi=vulkan\|opengl` | Graphics backend. Restart-time by design. |
| `--vsync=on\|off` | Present synchronised to the display. |
| `--validation=on\|off` | Vulkan validation layers. **Off by default, everywhere** — see below. |
| `--fixed-hz=N` | Simulation rate, 20–240. Default 60. |
| `--width=N` `--height=N` | Window size. |
| `--audio=on\|off` | Whether to open an output device at all. |
| `--project=<path>` | The `.rvproject` to open, or a folder containing one. |
| `--scene=<path>` | Open this scene instead of the project's start scene. |
| `--screenshot=<file>` | Write a PNG of one frame and exit. |
| `--screenshot-frame=N` | Which frame to capture. Default 30, to let the scene settle. |
| `--benchmark=N` | Run N frames, print what they cost, and exit. |
| `--ui-scale=N\|auto` | Editor font and spacing. `auto` follows the monitor. |
| `--theme=dark\|light` | Editor theme. Default is whatever was last used. |
| `--select=<name>` | Open with this entity selected, so the inspector has something in it. |
| `--camera=x,y,z,d,yaw,pitch` | Where the editor's viewport camera starts. Angles in degrees. |

The last four exist to make a screenshot repeatable. A picture of the editor is
only worth taking if the next person can take the same one, and "open it and
drag until it looks like this" is not a reproduction — so the theme, the
selection and the viewpoint are all arguments.

> [!NOTE]
> `--vsync=off` set at startup and unchecked at runtime are not the same thing.
> Turning it off from the editor's checkbox saves the choice and applies what it
> can, but on Windows the frame rate does not fully unlock until the next start —
> the compositor decides how a window presents when that window first presents.
> The checkbox says so in place.

### Debugging GPU work: validation layers

When something on the GPU misbehaves — flicker that looks like a
synchronisation bug, a crash inside the driver, a resource that renders
garbage — set `validation = on` in `ragev.ini` (or pass `--validation=on`) and
run on Vulkan. The validation layers check every API call and report the
mistake in the log, by name, usually with a link to the specification.
They need the [LunarG Vulkan SDK](https://vulkan.lunarg.com/) installed;
without it the engine logs a warning and carries on unvalidated.

Turn them back off when you are done. The layers cost about **1.5 ms of CPU
per frame** — enough to more than double an editor frame — and only Vulkan
pays it, so leaving them on makes Vulkan look slower than OpenGL when it is
measurably faster. `--benchmark` prints the validation state in its banner so
a skewed measurement identifies itself.

## Where things live

```text
RageV/            the engine library
RageVEditor/      the editor
RageVRuntime/     the standalone runtime
SampleProject/    a project you can open and take apart
tools/            scenetest, rvpack, rvdoc, shaderinfo, rhismoke
docs/             this manual, plus the engine's own design notes
```
