# Component reference

Every component, every field, with its default and what it does.

A component is data attached to an entity. Add one from **Add Component** in
the Inspector, or by name from a script. Nothing here is inherited and nothing
is optional-by-omission: a field holds a value, and the default is what it
holds until you change it.

> [!NOTE]
> This page is **checked against the header**. `rvdoc --check` fails if a field
> exists in `Components.h` with no entry here, or if an entry here names a
> field that no longer exists. A reference that silently drifts is worse than
> none, so it is not allowed to.

**Runtime state** is called out per component where it exists. Those fields are
real and reachable from a script, but they are *derived* — not saved to the
scene, not shown in the Inspector. If you set one and it does not survive a
reload, this is why.

## Every entity has two

You never add these and the Inspector does not show them, but they exist on
every entity and the scripting API reads them.

### IDComponent

| Field | Default | What it is |
|---|---|---|
| `ID` | minted on creation | The entity's UUID. Stable across saves, and what every cross-entity reference stores |

### RelationshipComponent

Parent and child links, by UUID rather than by handle, so they survive
serialization.

| Field | Default | What it is |
|---|---|---|
| `Parent` | invalid | The parent's UUID, or invalid for a root |
| `Children` | empty | **Derived.** Rebuilt on load from every entity's `Parent` |

> [!NOTE]
> Only `Parent` is written to disk. Storing both directions would let them
> disagree, and a hierarchy that disagrees with itself is a bug that surfaces
> three features later.

## Core

### TagComponent

| Field | Default | What it does |
|---|---|---|
| `Name` | — | The entity's name, in the Hierarchy and for lookup by name |

### TransformComponent

Position, rotation and scale are **local** — relative to the parent. World
space is derived from them.

| Field | Default | What it does |
|---|---|---|
| `Position` | `0, 0, 0` | Local position |
| `Rotation` | `0, 0, 0` | Local rotation, **radians**, XYZ order |
| `Scale` | `1, 1, 1` | Local scale |

**Runtime state:**

| Field | What it is |
|---|---|
| `World` | The composed world matrix, derived top-down from the roots |
| `PreviousWorld` | Where `World` was last frame — what a motion vector is made of |
| `CachedPosition` | What `Position` was when `World` was last computed |
| `CachedRotation` | What `Rotation` was when `World` was last computed |
| `CachedScale` | What `Scale` was when `World` was last computed |
| `CacheValid` | False until the first walk, so a new entity always computes |

> [!NOTE]
> **`World` is not cached behind a dirty flag, and deliberately so.** A flag has
> to be set at every write site — inspector, gizmo, scripts, serializer, and
> everything added later — and one missed site leaves an object silently
> rendering in the wrong place.
>
> Instead the walk *compares*. It keeps a copy of the three vectors it last
> composed from, and where they still agree — and the parent's world matrix did
> not move either — the existing `World` is already the answer. Nothing has to
> report that it moved something, because nothing is trusted to.
>
> The four cached fields exist for that comparison. They are derived state like
> `World` itself: not serialized, not shown in the inspector, and not part of
> what makes two transforms equal. Writing to them from outside the walk has no
> meaning beyond making it recompute once.

## Rendering

### MeshComponent

Geometry plus a material, both by handle. The overrides below are **free**:
base colour, emissive and the surface scalars already travel per instance, so
overriding them cannot split a batch — a thousand cubes differing only in
colour stay one draw.

| Field | Default | What it does |
|---|---|---|
| `Mesh` | the Cube primitive | Which geometry to draw |
| `Material` | invalid | Which material. Invalid means the renderer's shared default |
| `Static` | `false` | The object never moves. Static objects are what the bake sees, and they read fully baked lights from the field instead of lighting them live; a moving object is lit live by every light, casts a live shadow, and is left out of the bake. Skinned meshes are always moving, whatever this says |
| `OverrideBaseColor` | `false` | Use `BaseColor` instead of the material's |
| `BaseColor` | white | Albedo tint, RGBA |
| `OverrideEmissive` | `false` | Use `EmissiveColor` instead of the material's |
| `EmissiveColor` | black | Light emitted by the surface, RGBA |
| `OverrideMetallic` | `false` | Use `Metallic` instead of the material's |
| `Metallic` | `0.0` | 0 is dielectric, 1 is metal |
| `OverrideRoughness` | `false` | Use `Roughness` instead of the material's |
| `Roughness` | `0.5` | 0 is a mirror, 1 is fully diffuse |
| `OverrideOcclusion` | `false` | Use `Occlusion` instead of the material's |
| `Occlusion` | `1.0` | Baked ambient occlusion, 1 being unoccluded |
| `OverrideNormalScale` | `false` | Use `NormalScale` instead of the material's |
| `NormalScale` | `1.0` | How strongly the normal map is applied |

### TerrainComponent

A heightfield from an `.rvterrain` asset, drawn as chunk meshes with levels of
detail, surfaced from up to four materials in the proportions the asset's
paint says, and collided with through a static height field. Centred on the
entity. See [Terrain](terrain.md).

| Field | Default | What it does |
|---|---|---|
| `Terrain` | invalid | The `.rvterrain` holding the heights. Invalid draws nothing |
| `Size` | `256.0` | Metres a side, centred on the entity |
| `Height` | `40.0` | Metres the highest possible sample stands above the base |
| `Material` | invalid | Layer 0: the material wherever nothing else is painted, and the whole terrain when there is no paint. Invalid means the renderer's shared default. Shown as "Layer 0" in the inspector |
| `Layer1` | invalid | The material the paint's second channel blends in. Invalid means the layer is not there |
| `Layer2` | invalid | The third channel's material, likewise |
| `Layer3` | invalid | The fourth channel's material, likewise |
| `TextureScale` | `4.0` | Metres per repeat of the layers' textures; each layer's own tiling multiplies on top |
| `Collision` | `true` | A static height-field collider under the drawn surface. The terrain is its own collider: no RigidBody or Collider component is needed, and one on this entity is ignored |
| `Static` | `false` | The same switch a mesh has: on, the bake sees the terrain and it reads fully baked lights from the field; off, it is lit live and left out of the bake |

**Runtime state:**

| Field | What it is |
|---|---|
| `Runtime` | The chunk meshes, weight texture and layered material built from the asset at these dimensions; rebuilt when the asset or a dimension changes |

### CameraComponent

| Field | Default | What it does |
|---|---|---|
| `Camera` | perspective | Projection, field of view and clip planes |
| `ViewRank` | `0` | Which camera the game view renders through — **lowest wins**, 0 highest priority through 99 |
| `PostProfile` | invalid | The `.rvpostprofile` grading this view. Invalid means the neutral grade, **not** "no post processing" |

> [!NOTE]
> `ViewRank` is a rank rather than an `isPrimary` flag because a boolean can be
> true on two cameras at once, and then which one renders depends on iteration
> order — so adding a camera could silently change the view. Ties break on
> entity id, so the answer is the same on every run.

See [Post processing](post-processing.md) for what a profile holds.

### LightComponent

| Field | Default | What it does |
|---|---|---|
| `Light` | directional | Type, colour, intensity, range and cone angles |
| `Mobility` | `Half bake` | How much of the light the bake owns. `Realtime`: nothing, it can be switched by a script without touching any bake. `Half bake`: its bounce is baked, its direct light and shadows are live. `Full bake`: its direct light is in the field too, and static objects read it from there. `Hybrid Full Bake`: half baked within `HybridRadius` of the lamp, fully baked beyond |
| `HybridRadius` | `2.0` | Hybrid Full Bake only. Metres from the lamp within which it is lit live, keeping the bright spot under it and the glow that spot makes. Changing it means re-baking |

Directional, point and spot are all supported, and all cast shadows. Lighting
is clustered forward, so there is **no light count cap**.

### ReflectionProbeComponent

Captures the scene into a cube map from one point, for nearby surfaces to
reflect. The scene picks one probe per render: the nearest whose influence
contains the camera, and the sky otherwise.

| Field | Default | What it does |
|---|---|---|
| `Update` | `Baked` | `Baked` captures once and keeps it; `Realtime` re-captures continuously |
| `Resolution` | `128` | Per face. Reflections are seen through rough or curved surfaces, so this can be far smaller than it feels — 128 is hard to fault on anything but a mirror |
| `NearClip` | `0.05` | Near plane of the capture |
| `FarClip` | `100.0` | Far plane of the capture |
| `Influence` | `20.0` | How far from the probe its capture is still a reasonable answer. Past it, the sky is the better lie |
| `FacesPerFrame` | `1` | Realtime only. Six faces in one frame is a visible hitch; one per frame is a sixth of the cost and a sixth of a second of latency |
| `Rate` | `PerFrame` | Realtime only: `Hz15`, `Hz30`, `Hz45`, `Hz60` or `PerFrame`. **This is where a realtime probe's cost actually goes** — the demo at 15 Hz looks identical to per-frame and costs a quarter as much |

**Runtime state:**

| Field | What it is |
|---|---|
| `Probe` | The captured cube map |
| `NextFace` | Which face the next capture step writes |
| `RateAccumulator` | Seconds since the last step, against `Rate`'s interval |
| `Dirty` | Never captured, or something invalidated it. A baked probe watches this; a realtime one re-captures regardless |

## Physics

### RigidBodyComponent

| Field | Default | What it does |
|---|---|---|
| `Type` | `Dynamic` | `Static` never moves, `Kinematic` is moved by you, `Dynamic` is simulated |
| `Mass` | `1.0` | Kilograms. Dynamic bodies only |
| `Friction` | `0.4` | 0 slides, 1 grips |
| `Restitution` | `0.1` | Bounciness. 0 absorbs, 1 returns the energy |
| `LinearDamping` | `0.05` | Velocity bled off per second |
| `AngularDamping` | `0.05` | Spin bled off per second |
| `GravityFactor` | `1.0` | Multiplier on gravity. 0 floats; negative falls upward |
| `FreezeRotation` | `false` | Locks all rotation — what a character controller wants |

### ColliderComponent

| Field | Default | What it does |
|---|---|---|
| `Shape` | `Box` | `Box`, `Sphere` or `Capsule` |
| `HalfExtents` | `0.5, 0.5, 0.5` | Box only: half the size on each axis |
| `Radius` | `0.5` | Sphere and capsule |
| `Height` | `1.0` | Capsule only, the cylindrical part |
| `Offset` | `0, 0, 0` | Shifts the shape relative to the transform |
| `IsTrigger` | `false` | Reports overlaps without resisting them — `OnTrigger*` instead of `OnCollision*` |

## Audio

### AudioSourceComponent

| Field | Default | What it does |
|---|---|---|
| `Clip` | invalid | Which sound |
| `Bus` | `SFX` | `Master`, `Music`, `SFX` or `UI`, each with its own volume |
| `Volume` | `1.0` | Linear gain |
| `Pitch` | `1.0` | Playback rate; also shifts the pitch |
| `Loop` | `false` | Restart at the end |
| `PlayOnAwake` | `true` | Start when the scene starts playing |
| `Spatial` | `true` | Positioned in 3D. Off is a flat stereo sound, which is what music wants |
| `MinDistance` | `1.0` | Within this, full volume |
| `MaxDistance` | `50.0` | Beyond this, silent |
| `Stream` | `false` | Decode as it plays rather than loading whole — for long music |

**Runtime state:**

| Field | What it is |
|---|---|
| `Voice` | The playing voice's handle, or 0 |

### AudioListenerComponent

| Field | Default | What it does |
|---|---|---|
| `ListenerRank` | `0` | Which listener hears the scene — lowest wins, like `ViewRank` |

## Animation

### AnimatorComponent

| Field | Default | What it does |
|---|---|---|
| `Clip` | `0` | Which clip of the mesh's skeleton to play, chosen by name in the Inspector |
| `Playing` | `true` | Advancing or held |
| `Loop` | `true` | Restart at the end |
| `Speed` | `1.0` | Playback multiplier; negative plays backwards |
| `RunInEditor` | `false` | Animate without entering Play. Off by default so a saved pose is what you see |
| `BlendTime` | `0.15` | Seconds to cross-fade when the clip changes. 0 cuts |

**Runtime state:**

| Field | What it is |
|---|---|
| `Time` | Seconds into the current clip |
| `Active` | The clip actually playing |
| `Started` | Whether playback has begun |
| `FadingFrom` | The clip being faded out, or -1 |
| `FadingTime` | Where the outgoing clip was when the fade began |
| `FadeElapsed` | Seconds into the current fade |
| `Skinning` | The bone matrices handed to the renderer this frame |

## Particles

### ParticleEmitterComponent

The full guide, with the curve editor and the blend modes, is in
[Particles](particles.md). Every field:

| Field | Default | What it does |
|---|---|---|
| `Emit` | `true` | Spawning or stopped. Existing particles finish either way |
| `Rate` | `20.0` | Particles per second |
| `Burst` | `0` | Particles spawned at once when triggered from a script |
| `Lifetime` | `1.5` | Seconds a particle lives |
| `LifetimeJitter` | `0.25` | Fraction of random variation on the lifetime |
| `Shape` | `Point` | Where particles are born: `Point` is the emitter itself, `Box` is anywhere inside `BoxSize` |
| `BoxSize` | `2, 1, 2` | The spawn box's full extents in metres, in the emitter's frame. `Box` only |
| `Motion` | `Directional` | `Directional` uses the cone below, then gravity and drag. `Stationary` gives no velocity and applies neither |
| `Direction` | `0, 1, 0` | Which way the cone points |
| `Spread` | `25.0` | Half-angle of the cone, in degrees. 0 is a beam, 180 is a sphere |
| `Speed` | `3.0` | Initial speed along the cone |
| `SpeedJitter` | `0.3` | Fraction of random variation on the speed |
| `Gravity` | `0, 0, 0` | Constant acceleration |
| `Drag` | `0.0` | Velocity bled off per second |
| `SizeStart` | `0.25` | Size at birth |
| `SizeEnd` | `0.05` | Size at death |
| `ColorStart` | white | Colour at birth, RGBA |
| `ColorEnd` | transparent | Colour at death, RGBA |
| `SizeCurve` | invalid | A `.rcurve` overriding the two size endpoints |
| `ColorGradient` | invalid | A `.rcurve` overriding the two colour endpoints |
| `AlphaCurve` | invalid | A `.rcurve` overriding alpha alone |
| `Spin` | `0.0` | Rotation speed, radians per second |
| `Facing` | `Billboard` | `Billboard` faces the camera (3D); `Flat` lies in the plane (2D) |
| `Blend` | `Alpha` | `Alpha` sorts and blends, `Additive` glows, `WeightedBlended` is order-independent |
| `Space` | `World` | `World` leaves particles behind as the emitter moves; `Local` carries them |
| `Texture` | invalid | The sprite. Invalid is a soft disc |
| `MaxParticles` | `1000` | Pool size — the ceiling on live particles |
| `SimulateOnGpu` | `false` | Run the simulation in a compute shader. Measured 2.4× at ~16k particles |

**Runtime state:**

| Field | What it is |
|---|---|
| `Pool` | The live particles, on the CPU path |
| `EmitCarry` | Fractional particles owed from the last step |
| `Rng` | The emitter's random seed |

> [!NOTE]
> An unset curve means the start/end pair still decides that channel, so
> attaching one changes nothing until a key moves.

**Spawning in a volume.** A `Point` emitter puts every particle at the same
place, which is right for anything with a source -- a chimney, a muzzle -- and
wrong for anything spread over an area. Fireflies over a clearing leaving one
point read as a fountain however slowly they move, because the tell is the
empty space at the origin rather than the speed. `Shape: Box` spawns each
particle at a uniformly random point inside `BoxSize`.

`BoxSize` is the box's own size in metres, not a multiple of the entity's
scale -- the same rule `ColliderComponent` follows, so an emitter parented to
something scaled is not scaled twice. A **local-space** emitter is the one
exception and cannot be otherwise: its particles are stored in the emitter's
frame and the transform is applied when they are drawn, so its box rides the
entity's scale along with everything else it emits.

`Motion: Stationary` is not the same as `Speed` at zero -- gravity would still
take it, and turning gravity off as well spells one intention across two
fields that have to be read together. Stationary particles appear where they
were born, live out their lifetime and go, which with a box is a field of
things blinking on and off in place.

> [!TIP]
> **Window > Show Emitters** (F4) draws every box emitter's volume in the scene
> view, and nothing in the game view. It is off by default, like the collider
> overlay beside it. It is drawn from the same function the
> emitter spawns from, so it cannot show a box the particles do not use.

## User interface

The canvas system is not ImGui — it is the game's own UI, drawn by the engine
and saved in the scene.

### UICanvasComponent

| Field | Default | What it does |
|---|---|---|
| `ScaleMode` | `ScaleWithScreen` | `ConstantPixels` keeps one UI unit at one screen pixel; `ScaleWithScreen` scales against a reference resolution |
| `ReferenceResolution` | `1920, 1080` | The resolution the layout was authored at |
| `MatchWidthOrHeight` | `0.5` | 0 matches width, 1 matches height, 0.5 splits the difference |
| `SortOrder` | `0` | Which canvas draws on top |

### UIRectComponent

Anchors and offsets, the same model every UI toolkit uses: anchors are
fractions of the parent, offsets are pixels from the anchor.

| Field | Default | What it does |
|---|---|---|
| `AnchorMin` | `0, 0` | Lower-left anchor, as a fraction of the parent |
| `AnchorMax` | `1, 1` | Upper-right anchor |
| `OffsetMin` | `0, 0` | Pixels in from the lower-left anchor |
| `OffsetMax` | `0, 0` | Pixels in from the upper-right anchor |
| `SortOrder` | `0` | Draw order within the canvas |
| `Visible` | `true` | Drawn or not. Hides children too |
| `BlocksPointer` | `false` | Swallows clicks that land on it, so the scene behind does not get them |

### UIImageComponent

| Field | Default | What it does |
|---|---|---|
| `Texture` | invalid | The image. Invalid is a solid rectangle |
| `Color` | white | Tint, RGBA |

### UITextComponent

| Field | Default | What it does |
|---|---|---|
| `Text` | `"Text"` | What it says |
| `Font` | invalid | A `.rvfont`. Invalid uses the built-in |
| `Size` | `32.0` | Pixels |
| `Color` | white | RGBA |
| `Align` | `Left` | `Left`, `Center` or `Right` |
| `Wrap` | `true` | Break lines at the rect's width |
| `LineSpacing` | `1.0` | Multiplier on the line height |

### UIButtonComponent

| Field | Default | What it does |
|---|---|---|
| `Interactable` | `true` | Off ignores the pointer and shows no hover |
| `NormalColor` | white | Tint at rest |
| `HoverColor` | lighter | Tint under the pointer |
| `PressedColor` | darker | Tint while held |
| `OnClickTarget` | none | Which entity's script handles the click |
| `OnClickMethod` | empty | Which method on it, picked from a dropdown of that script's methods |

**Runtime state:**

| Field | What it is |
|---|---|
| `Hovered` | The pointer is over it |
| `Pressed` | Held down |
| `Clicked` | Released over it this frame — what a script polls |

### WorldTextComponent

Text that lives in the scene rather than on a canvas: labels over objects,
signs, damage numbers.

| Field | Default | What it does |
|---|---|---|
| `Text` | `"Text"` | What it says |
| `Font` | invalid | A `.rvfont` |
| `Size` | `0.5` | **World metres**, not pixels |
| `Color` | white | RGBA |
| `Align` | `Center` | `Left`, `Center` or `Right` |
| `WrapWidth` | `0.0` | Metres before wrapping. 0 never wraps |
| `LineSpacing` | `1.0` | Multiplier on the line height |
| `Billboard` | `Upright` | `None` keeps the transform's orientation; `Upright` turns to face the camera about Y; `Full` always faces it |

## Scripting

Both languages are equals — same hooks, same surface. See
[Scripting](scripting/index.md).

### NativeScriptComponent

| Field | Default | What it does |
|---|---|---|
| `ScriptName` | empty | Which registered C++ class to instantiate |
| `Label` | empty | What the Inspector calls it, if not the class name |
| `Fields` | empty | Per-entity values for the script's declared fields |

**Runtime state:**

| Field | What it is |
|---|---|
| `Instance` | The live object, while playing |
| `ActiveScript` | Which class the live instance is, so a rename is noticed |

### ManagedScriptComponent

| Field | Default | What it does |
|---|---|---|
| `ScriptName` | empty | Which C# class to instantiate |
| `Label` | empty | What the Inspector calls it |
| `Fields` | empty | Per-entity values for the script's declared fields |

**Runtime state:**

| Field | What it is |
|---|---|
| `Handle` | The managed object's handle |
| `ActiveScript` | Which class the live instance is |

## Prefabs

### PrefabComponent

| Field | Default | What it does |
|---|---|---|
| `Source` | invalid | The `.rvprefab` this entity was instantiated from |

Marks an entity as an instance, so the editor can tell it apart from one built
by hand.

## Where to go next

- [Core concepts](concepts.md) — entities, scenes, assets and play mode
- [Rendering](rendering.md) and [Post processing](post-processing.md) — the
  settings that are not on a component
- [Particles](particles.md) — the emitter in depth
- [Scripting](scripting/index.md) — reaching all of this from code
