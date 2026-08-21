# The editor

The panels, what each is for, and every keyboard shortcut.

## Panels

| Panel | What it is for |
|---|---|
| **Viewport** | The editing view, through the editor camera. Gizmos, selection, the grid |
| **Game** | What the primary scene camera sees, with the full post chain. Shares a dock node with the Viewport, so they are usually tabs |
| **Scene Hierarchy** | Every entity, as a tree. Drag to re-parent |
| **Properties** | The Inspector: components of whatever is selected, and their fields |
| **Content** | The project's assets, as a browsable folder tree with a size slider |
| **Render Settings** | The project's render settings, and the scene's environment |
| **Statistics** | Frame time, draw calls, triangles, culled counts |
| **Build Log** | Output from building the game module or the C# assembly |

Panels dock, tab and float — the layout is ImGui's, and it is saved to
`panels.ini` on exit.

**Window → Theme** switches between dark and light. Both are checked against
WCAG 2.2 AA contrast by a script, so neither is a guess.

## Shortcuts

| Key | Does |
|---|---|
| **Ctrl+N** | New scene |
| **Ctrl+O** | Open scene |
| **Ctrl+S** | Save scene |
| **Ctrl+Shift+S** | Save scene as |
| **Ctrl+Shift+N** | Create an empty entity |
| **Ctrl+Z** | Undo |
| **Ctrl+Y** / **Ctrl+Shift+Z** | Redo |
| **Ctrl+P** | Play, or stop if playing |
| **W** | Translate gizmo |
| **E** | Rotate gizmo |
| **R** | Scale gizmo |
| **F** | Frame the selection |
| **F2** | Toggle the grid |
| **F3** | Toggle collider wireframes |
| **F4** | Toggle particle emitter volumes |
| **Delete** | Delete the selection |

> [!NOTE]
> The gizmo and view shortcuts are ignored while a text field has the caret, so
> typing a name does not switch tools.

## The viewport

| Input | Does |
|---|---|
| Left-click | Select. Clicking empty space deselects |
| Right-drag | Look around |
| WASD while right-dragging | Fly. QE go down and up, Shift is 3× |
| Middle-drag | Pan |
| Wheel | Zoom |
| Wheel **while right-dragging** | Fly speed, not zoom |

> [!IMPORTANT]
> **WASD only moves the camera while the right button is held.** On its own W
> is the translate gizmo, E is rotate and R is scale — the same keys every
> editor uses, and they are ignored while you are flying so the two never fire
> together.

The wheel is **positional** — it belongs to whatever the pointer is over, not
to whatever has focus. Scrolling the Inspector after clicking an object does
not zoom the scene behind it.

Zoom is a **ratio**: one notch is the same fraction of the current distance
whether you are a centimetre from a windscreen or a kilometre above the trees,
so it never crawls when far out and never overshoots when close in. Roughly two
dozen notches covers ten metres, and the same two dozen brings you back.

The fly speed is separate, persists until changed, and floors at 0.5 m/s. If
flying feels stuck, the wheel has probably been turned down while the right
button was held — scroll up with it held to wind it back.

Light and camera entities draw billboard icons so they can be found and clicked
without being visible geometry.

## Play mode

**Play** steps the scene: physics runs, scripts run, animations advance.
**Stop** restores the scene exactly as it was.

That restore is a **snapshot**, and it is complete: a cube knocked over during
play stands back up. Two consequences worth knowing:

- **A script attached while playing is discarded on Stop.** Correct snapshot
  semantics, surprising the first time.
- Anything you want to keep, change while stopped.

**Pause** holds the scene without restoring it, which is how you inspect a
state that took a while to reach.

## Saving

Two schedules, and the split is deliberate:

| What | When it saves |
|---|---|
| The scene — entities, components, environment | **Ctrl+S**, and you are asked before anything discards it |
| Render settings | The moment they change |
| Post profiles, materials, curves, LUTs | On edit |
| Panel layout, theme, window size, backend, vsync | On exit |

The scene is the only one that waits, because a scene is work in progress and
autosaving one takes away the freedom to try something. Everything else saves
itself.

The dot beside the scene name in the menu bar is lit whenever the scene has
changes that are not on disk.

## Scripts from the editor

**Build Scripts** unloads the game module, rebuilds it in the background with a
live console, and reloads whatever links. One rule for both languages.

**Building mid-play restarts the scene** — live instances run the loaded code,
so the editor stops, swaps both languages' scripts, and resumes Play on the new
code. After a failed build it stays stopped with the errors showing.

**New Script** writes a starter file into the project's `Source/` or
`Scripts/`, already registered.

## Choosing a backend

**Vulkan or OpenGL**, picked in the editor with a restart prompt, or with
`--rhi=vulkan|opengl`. Both are fully supported and produce the same image —
that equivalence is checked, and it is how several rendering bugs were caught.

## Command line

The flags worth knowing; the full list is in
[Getting started](getting-started.md#command-line-flags).

| Flag | For |
|---|---|
| `--rhi=vulkan\|opengl` | Which backend |
| `--validation=on` | GPU validation layers. **Off by default**, so a run without this reports no errors whether or not there were any |
| `--screenshot=<file>` | Render and write a frame |
| `--screenshot-frame=N` | Which frame to capture |
| `--frame-time=N` | Pin the timestep, so a capture is reproducible |
| `--camera=x,y,z,dist,yaw,pitch` | Place the editor camera |
| `--aa=<mode>` | Override anti-aliasing |
| `--scene=<path>` | Open a scene without changing the project's start scene |
| `--benchmark=N` | Measure N frames and print a breakdown |

## Where to go next

- [Getting started](getting-started.md) — building, running, and every flag
- [Core concepts](concepts.md) — what a project, scene and asset are
- [Cameras](cameras.md) — the editor camera against the scene cameras
