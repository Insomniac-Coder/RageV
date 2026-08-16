# Cameras

A camera decides what the game view shows and how it is graded. The editor's
own viewport camera is a separate thing that is never saved.

## The scene camera

Add a **Camera** component. Its transform is the camera: position and
orientation, looking down its local −Z.

| Field | Default | What it does |
|---|---|---|
| Projection | Perspective | `Perspective` or `Orthographic`. The *type's* own default is orthographic; the editor sets perspective on every camera it creates, which is what you actually get |
| Perspective FOV | `60` | Degrees, **vertical** |
| Perspective near | `0.01` | Metres |
| Perspective far | `1000` | Metres |
| Orthographic size | `10` | Half the visible height, in metres |
| Orthographic near | `-1` | |
| Orthographic far | `1` | |
| Fixed aspect ratio | `false` | Off lets the camera take the viewport's aspect |
| View rank | `0` | Which camera renders. **Lowest wins**, 0 through 99 |
| Post profile | none | The `.rvpostprofile` grading this view |

### Near and far, and why the near plane matters more

Depth precision is not spread evenly: **almost all of it sits near the near
plane**. Halving the near plane costs roughly as much precision as doubling the
far one.

So a near plane of 0.001 "just in case" is how z-fighting arrives on distant
geometry. Push it out as far as the game tolerates — 0.05 or 0.1 for anything
that does not put the camera inside objects.

The default 0.01 is chosen for an editor that has to let you fly right up to
things, not for a shipping game.

### Which camera renders

`ViewRank` decides: **lowest wins**, ties broken by entity id so the answer is
the same on every run.

It is a rank rather than an `isPrimary` checkbox for a specific reason: a
boolean can be true on two cameras at once, and then which one renders depends
on registry iteration order — so *adding* a camera could silently change the
view. A rank always has a single winner.

## Post profiles are per camera

Grading belongs to a **view**, not a scene, because a scene can hold several
views — the editor already renders two of the same scene at once, and one grade
between them cannot tell them apart.

A camera with no profile renders the **neutral grade**: exposure 1, bloom on.
That is not "no post processing", which would be a much more surprising thing
to have to opt out of.

See [Post processing](post-processing.md) for what a profile holds.

> [!TRAP]
> **The profile saves itself; the camera pointing at it does not.** A grade you
> edit is written to its `.rvpostprofile` immediately. *Which* profile a camera
> names is scene data and waits for **Ctrl+S**. Attaching a profile and grading
> it lands in two files on two schedules.

## The editor camera

The viewport has its own camera, and it is **not** a scene camera: it has no
entity, no component, and it is never saved.

| Input | Does |
|---|---|
| Right-drag | Look around |
| WASD while right-dragging | Fly |
| Middle-drag | Pan |
| Wheel | Zoom |
| **F** | Frame the selection |

`--camera=x,y,z,distance,yaw,pitch` places it from the command line, which is
what makes a screenshot of a particular view reproducible.

> [!NOTE]
> The editor viewport still grades through the **primary camera's** profile.
> The viewport is meant to show what the game shows, and a grade you cannot see
> while authoring is a grade you author blind.

## The Game panel

The editor has two views of the same scene: **Viewport**, which is the editing
view through the editor camera, and **Game**, which is what the primary scene
camera sees, with the full post chain.

They share a dock node, so they are usually tabs of each other.

> [!NOTE]
> The scene view deliberately skips the cinematic post effects — depth of
> field, motion blur — unless you ask it to preview them. Authoring through a
> defocused viewport is not workable.

## Where to go next

- [Component reference](components.md#cameracomponent) — the component's fields
- [Post processing](post-processing.md) — what a profile holds
- [The editor](editor.md) — panels, and the rest of the viewport's controls
