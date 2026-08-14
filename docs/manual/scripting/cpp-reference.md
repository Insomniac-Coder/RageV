# C++ API reference

Every member of `ScriptableEntity`, the base class of every C++ script.

> [!NOTE]
> This page is **checked against the header**. `rvdoc --check` fails if a member
> exists in `ScriptableEntity.h` with no entry here, or if an entry here names a
> member that no longer exists. A reference page that silently drifts is worse
> than no reference page, so it is not allowed to.

## Lifecycle

Override these. All are optional; the default does nothing.

| Member | Description |
|---|---|
| `OnCreate` | Once, on the first simulation step after Play. Every entity in the scene exists by then, so lookups are safe here — unlike in a constructor. |
| `OnTick` | Every fixed simulation step, **not** every frame. `dt` is the fixed timestep and is the same value on every call. Gameplay belongs here. |
| `OnFrame` | Every rendered frame, with the real elapsed time, which varies. Runs after the frame's physics interpolation, so it reads what is about to be drawn. Presentation belongs here. Never runs before `OnCreate`. |
| `OnDestroy` | On destruction, and when play mode stops. |
| `OnUpdate` | **Not a hook.** The fixed-step callback before the two rates were split, kept `final` so a script still written against it fails to build rather than failing to run. Change it to `OnTick` or `OnFrame`. |

## Collision callbacks

Delivered after the simulation step that produced them. The same event reaches
both entities, each with `Other` set to the one it is not.

| Member | Description |
|---|---|
| `OnCollisionEnter` | A solid pair began touching. Once per pair. |
| `OnCollisionStay` | Every step the pair remains in contact. |
| `OnCollisionExit` | The pair separated. Once per pair. **Not** fired when the pair falls asleep still touching. |
| `OnTriggerEnter` | A body entered a collider marked `IsTrigger`. |
| `OnTriggerStay` | Every step the body remains inside. Only while it is awake. |
| `OnTriggerExit` | The body left the trigger. |

### The Collision struct

| Field | Description |
|---|---|
| `Other` | The entity on the other side. May already have been destroyed this step — test it with `if (collision.Other)`. |
| `Trigger` | Either collider is a trigger. Always true in `OnTrigger*`, always false in `OnCollision*`; carried so one shared handler can tell. |
| `Point` | Contact point, world space. Zero on Exit — there is no contact left to describe. |
| `Normal` | Points from `Other` towards this entity, so moving along it moves away from what was hit. |
| `ImpactSpeed` | Closing speed along the normal when they met, units per second, never negative. Scale an impact sound or a damage value by it. |

## Identity

| Member | Description |
|---|---|
| `GetEntity` | This script's entity. |
| `GetScene` | The scene it belongs to. |
| `GetUUID` | Stable across save, load, duplication and play mode. What to store when you need to remember an entity. |
| `GetName` | The display name. Not unique. |
| `SetName` | Renames the entity. |

## Components

| Member | Description |
|---|---|
| `GetComponent` | Unchecked. Ask `HasComponent` first or be certain. |
| `HasComponent` | Whether the component is present. |
| `AddComponent` | Adds and returns it, forwarding constructor arguments. |
| `RemoveComponent` | Removes it. |

## Transform

Local, relative to the parent. Rotation is in **radians**. The three getters
return references into the component, so you may assign through them.

| Member | Description |
|---|---|
| `GetPosition` | Local position, by reference. |
| `GetRotation` | Local rotation in radians, by reference. |
| `GetScale` | Local scale, by reference. |
| `Translate` | Adds a delta to the local position. |
| `Rotate` | Adds an Euler delta, in radians. |
| `LookAt` | Orients towards a world-space target. `up` defaults to +Y. |
| `GetWorldTransform` | The composed world matrix. |
| `GetWorldPosition` | World-space position. |
| `GetForward` | The entity's own forward axis. |
| `GetRight` | The entity's own right axis. |
| `GetUp` | The entity's own up axis. |

## Other entities

| Member | Description |
|---|---|
| `FindEntityByName` | First match, or an invalid entity. Linear — do it in `OnCreate`, not every step. |
| `FindEntityByUUID` | By stable identity. The right way to hold a reference over time. |
| `FindEntitiesByName` | Every match, for names that are not unique. |
| `Spawn` | A new, empty entity with the given name. |
| `SpawnPrefab` | Instantiates a prefab asset, hierarchy included. |
| `Destroy` | Destroys this entity, or a given one. **Deferred to the end of the step**: it is still findable for the rest of that step. |

## Hierarchy

| Member | Description |
|---|---|
| `GetParent` | The parent, or an invalid entity at the root. |
| `SetParent` | Re-parents. The local transform is preserved as authored, so the world position changes. |
| `GetChildren` | Direct children only. |

## Physics

No-ops outside play mode and on an entity with no rigid body. World space.

| Member | Description |
|---|---|
| `AddForce` | Accumulates over a step and is cleared by it. A thruster. |
| `AddImpulse` | Changes velocity at once. A jump. |
| `SetLinearVelocity` | Sets velocity directly, ignoring mass. |
| `GetLinearVelocity` | Current velocity. |
| `Raycast` | Nearest body along the ray, which may be this one. Direction need not be normalised — the ray extends to its length. Test with `if (hit)`. |

## Rendering

| Member | Description |
|---|---|
| `GetRenderSettings` | What the frame costs, from the project: anti-aliasing and its parameters, shadows. Read fresh when the frame is built, so a change made in `OnTick` or `OnFrame` is on screen that frame. Note `ragev.ini` and `--aa=` still override the mode after this. |
| `GetEnvironment` | Where the frame is, from the scene: ambient light and the sky. |
| `GetPostSettings` | How the frame is graded, from the profile the scene's primary camera points at — exposure and bloom. **Null when that camera has no profile**, which is the honest answer: with none there is no grade to write to. |

None of the three is saved. They are runtime overrides — a game dimming its
own bloom should not quietly edit an asset — and the editor drops its cached
profiles when Play stops.

## Audio

| Member | Description |
|---|---|
| `PlaySource` | Plays this entity's `AudioSourceComponent`, restarting it if already playing. Does nothing without the component. |
| `StopSource` | Stops it. |
| `IsSourcePlaying` | Whether it is playing. |
| `PlayOneShot` | Fire and forget, at this entity's position. Optional volume and pitch; a small random pitch spread around 1 stops a repeated impact sounding like a sampler. |
| `PlayOneShotAt` | The same, from an arbitrary point — a particle burst, a ricochet, somewhere no entity stands. Static. |
| `PlayOneShot2D` | Unpositioned — full volume wherever the listener is. UI, narration, a stinger. Static. |

## Input

By action name, never by key code. See `RageV/Core/InputMap.h` for the bindings.

| Member | Description |
|---|---|
| `IsActionDown` | Held this step. |
| `WasActionPressed` | Went down since the last step. Consumed by the first step that runs, so it is never missed and never seen twice. |
| `WasActionReleased` | Came up since the last step. |
| `GetAxis` | Axis value. Unknown axes read zero rather than failing. |
| `IsPointerOverUI` | Whether the game's UI has the pointer this frame. **Ask before acting on a click** — the action map does not know a canvas exists. Static. |

## The game's UI

Conveniences over components `GetComponent` reaches perfectly well. They exist
because this is the surface C# mirrors, and because a label and a button are
what a game touches most.

| Member | Description |
|---|---|
| `SetText` | A `UITextComponent`'s string, on this entity or another. Does nothing without the component. |
| `GetText` | Reads it back; empty without the component. |
| `WasButtonClicked` | A completed press — down and up, both on the same button. True for one simulation step, the same contract `WasActionPressed` has. A press dragged off the button before release is cancelled and never reported. |

## Time

| Member | Description |
|---|---|
| `GetFixedDeltaTime` | The fixed timestep — the same value `OnTick` is handed. Static. |
| `GetInterpolationAlpha` | How far this frame falls between the last simulation step and the next, 0 to 1. For smoothing a value the engine cannot see; simulated bodies are already blended before `OnFrame` runs. Meaningless in `OnTick`. Static. |
| `GetTime` | Seconds since the process started. Static. |
