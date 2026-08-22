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
| **Shift+S** | Screenshot the Game panel into the project's `captures` folder |
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
> typing a name does not switch tools. **Shift+S** is ignored there too, and
> also while the right mouse button is held — S is the fly-backward key, so
> sprinting backwards must not fill a folder with screenshots.

## Building

**Scene ▸ Build Game** packages the project into `<project>/bin/<Name>`;
**Build Game As...** picks somewhere else. Both open a dialog first, because a
package is slow and two of its answers are not in the project anywhere.

| | |
|---|---|
| **Graphics API** | Vulkan, OpenGL, or both. Both means the game starts on the first and falls through to the other when there is no driver for it; one means a machine without it gets a game that will not start |
| **Scenes** | A checkbox each, with **Select all**, **Select none** and **Select current** outside the list. Only the ticked ones ship |

The start scene always ships, whatever the list says, and it says so when it
has to add it back — a game whose first scene is missing cannot start. Narrowing
the list also narrows the binding check, so a build shipping three scenes does
not refuse over a broken button in a fourth.

Press **Build** and the dialog closes; everything after that is in the Build
Log. It runs on a worker, so the editor stays usable — live output and a Cancel
button, exactly as Build Scripts does. One build at a time: packaging copies
what a script build produces, so the two cannot overlap.

The packaged game carries the state the editor is in:

| | |
|---|---|
| **Render Settings** | Written into the packaged `.rvproject`, so ray tracing, anti-aliasing and shadows ship as authored |
| **Graphics backend** | `ragev.ini` names what the dialog picked; `backends = ...` lists every one the build supports, in the order the game tries them |
| **Post profiles** | Ship as assets; the inspector writes them through the moment they are edited |
| **Scenes** | Read from disk — a scene with unsaved changes warns, and ships as last saved |

> [!NOTE]
> Packaging validates every UI button binding in every scene before it writes
> anything, because a method name in a scene file has no compiler behind it.
> That means parsing every scene in the project, which is the slowest part of a
> build on a large one.

## Screenshots

**Shift+S**, or **Scene ▸ Screenshot**, writes what the Game panel is showing
to `<project>/captures/<scene>_<date>-<time>.png`.

It is the *game's* frame and not the window: no panels, no menus, no gizmos —
what the camera renders, HUD included, because a player sees the HUD. The
resolution is the Game panel's own size, so a bigger panel is a bigger picture.

The folder is made the first time you take one and is not under version
control: these are pictures *of* the project rather than content in it. They
also live outside the asset directory on purpose — inside it, the asset
registry would mint a handle and a `.meta` for every screenshot.

The status bar along the bottom says where it went, in the same place the asset
watcher reports a reload.

> [!NOTE]
> The menu item is greyed while the Game panel is closed, and the shortcut says
> so in the status bar. Nothing is rendering the camera's view then, so there is
> genuinely no frame to take — the panel is what drives that render.

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

## The status bar

The strip along the bottom of the window says what the project folder is doing.
It is always there — one line, and it does not appear and disappear, because a
bar that comes and goes moves every panel underneath it by its own height.

The left is what just happened; the right is what is standing. Hover the bar to
see the last few messages, which is the case where something was said and has
already been replaced.

A message fades toward the resting colour after a few seconds rather than
clearing. "What did it just say" is usually asked a little late.

## Assets that change on disk

The editor **watches the project's asset folder** and reloads anything that
changes while it is open. Rebuild a mesh from a script, re-export a texture from
another tool, and the scene picks it up within about a second — the status bar
says what it reloaded.

> [!NOTE]
> Before this existed, loaded assets were dropped only when a project was
> opened, so an editor left open across an export kept showing the old asset —
> and **reopening the scene did not help either**, because scene loading does
> not clear them. Restarting was the only way, and nothing said so.

Some details worth knowing:

- **A change is reported once it stops moving**, one scan after it is first
  seen. A tool writing a mesh puts it on disk long before it has finished, and
  importing half a file fails and caches the failure.
- **`.meta` files are not watched.** They are the importer's own output, so
  watching them is a loop: importing rewrites one, the rewrite is a change, the
  change asks for an import. Editing import settings by hand is therefore not
  noticed.
- **Reloading is dropping.** The changed asset is emptied from the caches and
  the next frame asks for it again, so it goes through the same loader
  everything else does.
- It is a **poll**, twice a second: `<filesystem>` has no change notification,
  and the three platform APIs that do are three different things. The cost is
  reported in the bar — 340 files in the sample project scan in about 0.8 ms.

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
