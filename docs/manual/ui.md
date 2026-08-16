# User interface

The game's own UI — drawn by the engine, saved in the scene, and reachable from
scripts. **This is not the editor's ImGui**, which is developer chrome and does
not ship.

Text is rendered from a **multi-channel signed distance field**, so it stays
crisp at any size from one atlas.

## The shape of a UI

Four things, in a hierarchy:

1. A **canvas** — an entity with a `UICanvasComponent`. It decides how the
   whole UI scales with the window.
2. **Rects** under it — entities with a `UIRectComponent`, which is the
   position-and-size of a UI element.
3. **Content** on a rect — a `UIImageComponent` or a `UITextComponent`.
4. **Behaviour** — a `UIButtonComponent` for something clickable.

A rect with no content draws nothing and is a useful invisible container.

## Canvas scaling

The one decision that matters, and getting it wrong is the classic UI bug.

| Mode | What it means |
|---|---|
| `ConstantPixels` | One UI unit is one screen pixel |
| `ScaleWithScreen` | One UI unit is a fraction of the reference resolution |

**`ScaleWithScreen` is the default and is almost always right.** A HUD laid out
in constant pixels is *half the size* on a display with twice the pixels — it
was authored on one monitor and shipped for all of them.

With `ScaleWithScreen`, set `ReferenceResolution` to the resolution you are
authoring at (1920×1080 by default) and everything scales from there.
`MatchWidthOrHeight` decides what happens when the aspect ratio differs: 0
matches width, 1 matches height, 0.5 splits the difference.

## Anchors and offsets

The same model every UI toolkit uses, and worth stating plainly because the
combination is what makes it powerful.

- **Anchors** are fractions of the parent: `0,0` is its lower-left corner,
  `1,1` its upper-right.
- **Offsets** are pixels in from those anchors.

The two together give every layout behaviour people usually want a special mode
for:

| Goal | Anchors | Offsets |
|---|---|---|
| Fixed-size, top-left | min `0,1` max `0,1` | the size, as offsets |
| Fixed-size, centred | min `0.5,0.5` max `0.5,0.5` | half the size either way |
| Stretch to fill, with a margin | min `0,0` max `1,1` | the margin on each side |
| Full-width bar at the top | min `0,1` max `1,1` | height in the offsets |

**When the anchors are equal, the offsets are a size. When they differ, the
offsets are a margin.** That single sentence is the whole mental model.

## Pointer handling

`BlocksPointer` on a rect swallows clicks that land on it, so the scene behind
does not also get them. A full-screen panel with it off lets the player shoot
through the menu.

`Visible` hides an element **and its children**.

Scripts can ask `IsPointerOverUI()` before acting on a click, which is the
other half of the same problem.

## Buttons

A `UIButtonComponent` tints its rect through three states — normal, hover,
pressed — and calls a method when clicked.

**The binding is authored, not coded**: the component names a target entity and
a method on that entity's script, both picked from dropdowns in the Inspector.
The method list comes from the script itself, so it cannot name something that
does not exist.

> [!NOTE]
> Bindings are **validated at load and at package time**. A button pointing at
> a method that was renamed is an error you see when the scene opens, not a
> click that silently does nothing.

Scripts can also poll `Clicked` on the component, which is true for the frame a
release happened over the button.

## Text

| | `UITextComponent` | `WorldTextComponent` |
|---|---|---|
| Lives on | a canvas | the scene |
| `Size` is in | pixels | **world metres** |
| Wraps at | the rect's width | `WrapWidth` metres, 0 for never |
| Faces | the screen | per `Billboard` |

`WorldTextComponent` is for labels over objects, signs and damage numbers. Its
`Billboard` field decides how it turns:

- **`None`** — lies in the entity's own plane. A sign, or a number painted on
  the floor.
- **`Upright`** — turns to face the camera about Y only, so it stays vertical.
  The usual choice for a name tag.
- **`Full`** — always squarely faces the camera.

## Fonts

A font is a `.rvfont` asset: a metrics table beside a distance-field atlas,
baked from a `.ttf` by the `rvfont` tool. The `.ttf` itself is **not** an asset
— nothing at runtime can read one.

Leaving the font unset uses the built-in.

> [!TRAP]
> A font atlas is a distance field, not a picture. It must never be
> block-compressed or mip-chained — that is what makes the text soft. The
> engine guards this at both ends, but it is worth knowing if you ever bake one
> by hand.

## Where to go next

- [Component reference](components.md#user-interface) — every field on all six
  components
- [Scripting](scripting/index.md) — reading text, handling clicks, pointer
  queries
