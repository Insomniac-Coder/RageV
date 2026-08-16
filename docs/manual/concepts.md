# Core concepts

A handful of words carry most of the manual. This page defines them once.

## Project

A `.rvproject` file naming an asset root, a start scene, and a simulation rate.
Everything an asset refers to is relative to that root, which is what lets a
project be copied, packaged and opened on another machine.

The engine can run without a project. It shows you that state rather than
inventing one.

## Scene

A container of entities, saved as a text file you can read and diff. Scenes are
**losslessly round-trippable**: loading one and saving it again produces the same
bytes, every time. That is not a nicety — play mode is implemented as
snapshot-then-restore, so pressing Stop is only as correct as saving is.

## Entity

A thing in a scene. An entity is an identity and nothing else: it has no
behaviour and no data of its own until components are added to it.

Every entity carries a **UUID** that survives saving, loading, duplication and
play mode. Names do not have to be unique; UUIDs are. When you need to remember
*which* entity across time, remember the UUID.

## Component

Data attached to an entity. A transform, a mesh, a rigid body, a light, an audio
source, a script.

Components are registered with a reflection layer, which is why adding one shows
up in the inspector and in the scene file without either being taught about it
individually.

### The transform is a hierarchy

`TransformComponent` holds a **local** position, rotation (radians) and scale,
relative to its parent. World transforms are derived by walking up the chain.
Parenting one entity to another moves it with the parent.

## Asset

A mesh, material, texture, prefab, audio clip or scene stored under the project's
asset root and referenced by an `AssetHandle` — a stable identifier, not a file
path. A `.meta` sidecar next to each asset records the handle, so moving or
renaming a file does not break the scenes that use it.

A **prefab** is a saved entity, hierarchy and all, that can be spawned at
runtime.

## Where a setting lives

Three homes, and which one a setting is in follows from what kind of thing it
is. Nothing here is arbitrary, and knowing the rule saves hunting for a
slider.

| Kind | Setting | Home | Edited in |
|---|---|---|---|
| **Cost** | Anti-aliasing and its dials, shadow cascades, resolution and distance — all of them in **[Rendering](rendering.md)** | The `.rvproject` | Render Settings → Render settings |
| **Look** | Exposure and auto exposure, bloom, grading, depth of field, reflections, occlusion, motion blur, the lens and film effects — all of them in **[Post processing](post-processing.md)** | A `.rvpostprofile` asset | The camera that names it, or the asset itself |
| **Place** | Ambient light, the sky and its rotation | The `.rage` scene | Render Settings → Environment |

**Cost belongs to the project** because it is a judgement about the hardware,
and that judgement does not change because a different level loaded. It used
to be per scene, which meant a project with forty scenes stored its shadow
resolution forty times. Two narrower layers still override the anti-aliasing
mode, both about *this machine* rather than this project: the `AntiAliasing`
key in `ragev.ini`, and `--aa=` on the command line. The panel says so when
one of them is winning.

**Look belongs to an asset** because a grade is authored content that several
cameras should be able to share, and because sharing one is the difference
between changing a look once and changing it everywhere it was pasted.

### Everything persists

**There is no setting in this engine that only exists while it is running.**
Every control you can move has a file it goes back to, and the table above is
about *which* file, never *whether*:

| What | Written to | When |
|---|---|---|
| Render settings, including TAA's three dials | The `.rvproject` | The moment they change |
| Post profiles including auto exposure, LUT recipes, materials, curves | Their own asset file | On edit |
| Entities, components, the environment | The `.rage` scene | On **Ctrl+S** — and you are asked before anything discards it |
| Panel layout, theme, window size, backend, vsync | `ragev.ini` and `panels.ini` | On exit |

The scene is the only one that waits, because a scene is work in progress and
autosaving one would take away the freedom to try something. Everything else
saves itself, and the menu bar names any render setting it writes.

### The post profile

A `.rvpostprofile` is attached to a **camera**, on the Post profile row of its
Camera component, and it is **optional**: a camera with no profile renders
the neutral grade — exposure 1, bloom on — which is what every scene rendered
with before profiles existed.

The row is a dropdown of every profile in the project, plus **New post
profile…**, which writes one and attaches it in a click. Whichever is
selected, its own settings appear directly underneath, so a grade is edited
where it is used. The same settings appear in the Inspector when the
`.rvpostprofile` is clicked in the Content browser — one drawer, so the two
cannot disagree.

### What is on a profile

Nine effects and thirty-six settings, every one of them listed with its
default, its range and its cost in **[Post processing](post-processing.md)**.
The short version of what you get and what it costs:

| Effect | Default | Reach for it when |
|---|---|---|
| **Exposure** | on, 1.0 | The scene is uniformly too dark or too bright |
| **Bloom** | **on** | Bright things should bleed |
| **Colour grading** | off | You have a look, as a `.cube` or an editable `.rvlut` recipe |
| **Auto exposure** | off | The camera moves between very different light |
| **Depth of field** | off | You want a lens, not a blur — the controls are f-numbers and millimetres |
| **Screen-space reflections** | off | Wet floors and polished metal, for what is on screen |
| **Ambient occlusion** | off | Contact shadowing in creases |
| **Motion blur** | off | Fast motion should smear |
| **Vignette, aberration, grain** | off | You are modelling the camera rather than the scene |

**Everything except bloom is off by default, and off is exact** — no pass is
added, so a profile that has never touched an effect renders the same bytes as
a build without it.

**Editing one edits the asset.** Two cameras pointed at the same profile are
two cameras that will always agree, which is usually what is wanted and is
occasionally a surprise; the inspector names the file it is writing to for
exactly that reason.

> [!TRAP]
> **The profile saves itself. The camera pointing at it does not.** A grade
> you edit is written to its `.rvpostprofile` immediately; *which* profile a
> camera names is scene data and waits for **Ctrl+S**, like everything else
> in the scene. Attaching a profile and grading it therefore lands in two
> files on two schedules. The dot beside the scene name in the menu bar is
> lit whenever the scene has changes that are not on disk, and closing the
> editor, opening another scene or starting a new one all ask first.

The editor's viewport grades through the **primary camera's** profile, not a
setting of its own — the viewport is meant to show what the game shows, and a
grade you cannot see while authoring is a grade you author blind.

## Play mode

The editor has two states, and the difference matters more than it looks.

- **Editing.** Nothing steps. What you see is the scene as saved.
- **Playing.** The scene is snapshotted, then stepped: physics runs, scripts run,
  audio plays.

Pressing **Stop** restores the snapshot exactly. Nothing your game did during
play survives, which is what makes it safe to iterate.

> [!TRAP]
> Because Stop restores by *recreating* entities, any handle held across the
> boundary is dangling afterwards. Scripts are handed `Entity` values, and those
> are safe to use within a run; storing one in a static, or in anything that
> outlives play mode, is not. Store a `UUID` and look it up.

## The fixed step

Simulation runs at a fixed rate — 60 Hz by default, `--fixed-hz` to change it —
independently of how fast frames are drawn. A frame may run zero, one, or several
simulation steps depending on how long it took.

This is why script updates are **per step, not per frame**. A script that moves
something has to agree with the physics that will push it, and physics cannot be
correct at a variable rate. Everything in [Scripting](scripting/index.md) follows
from this one decision.

Rendering is still per frame, so the frame rate and the simulation rate are
genuinely different numbers. The editor's statistics panel shows both.
