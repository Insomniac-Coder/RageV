# Physics

Rigid-body simulation on **Jolt**, stepping at a fixed rate, with collision and
trigger callbacks delivered to scripts in both languages.

Physics needs **two** components: a `RigidBodyComponent` says how the body
behaves, a `ColliderComponent` says what shape it is. One without the other
does nothing.

## Body types

| Type | Moved by | Costs | Use for |
|---|---|---|---|
| **Static** | nothing | least — static bodies live in their own broad-phase tree that is never rebuilt | Level geometry, walls, the ground |
| **Kinematic** | your code | middling | Platforms, doors, anything animated that should push things |
| **Dynamic** | the solver | most | Anything that falls, rolls or is thrown |

**Kinematic is the one people reach for too late.** A moving platform made
dynamic fights the solver; made kinematic it goes exactly where you put it and
pushes dynamic bodies out of the way rather than being pushed.

## Body settings

| Field | Default | What it does |
|---|---|---|
| Mass | `1.0` | Kilograms. Dynamic only |
| Friction | `0.4` | 0 slides like ice, 1 grips |
| Restitution | `0.1` | Bounciness. 0 absorbs the impact, 1 returns it |
| Linear damping | `0.05` | Velocity bled off per second — air resistance |
| Angular damping | `0.05` | Spin bled off per second |
| Gravity factor | `1.0` | Multiplier on gravity. 0 floats, negative falls upward |
| Freeze rotation | `false` | Locks all rotation. **What a character controller wants** — otherwise a capsule tips over |

## Collider shapes

| Shape | Fields | Notes |
|---|---|---|
| **Box** | Half extents | Half the size on each axis, so `0.5, 0.5, 0.5` is a one-metre cube |
| **Sphere** | Radius | The cheapest shape to test |
| **Capsule** | Radius, Height | `Height` is the cylindrical part, not the total |

`Offset` shifts the shape relative to the entity's transform — for a collider
that should sit at a character's feet rather than their centre.

> [!TRAP]
> **Half extents, not size.** A box collider with half extents of `1, 1, 1` is
> two metres on a side. This catches everyone once.

## Triggers

Set `IsTrigger` and the collider reports overlaps without resisting them.
Things pass through, and your script gets `OnTriggerEnter` / `OnTriggerStay` /
`OnTriggerExit` instead of the `OnCollision*` trio.

That is the whole difference. A trigger is a collider that answers the question
"is something inside me?" and does nothing else.

## Callbacks

Delivered after the simulation step that produced them. **The same event reaches
both entities**, each with `Other` set to the one it is not.

| Callback | When |
|---|---|
| `OnCollisionEnter` | A solid pair began touching. Once per pair |
| `OnCollisionStay` | Every step the pair remains in contact |
| `OnCollisionExit` | The pair separated. Once per pair |
| `OnTriggerEnter` | A body entered a trigger |
| `OnTriggerStay` | Every step it remains inside. Only while it is awake |
| `OnTriggerExit` | It left |

The `Collision` struct is documented in the
[C++ reference](scripting/cpp-reference.md#the-collision-struct); the field
worth knowing about is **`ImpactSpeed`**, the closing speed along the normal,
never negative — scale an impact sound or a damage number by it and it is right
for free.

> [!TRAP]
> **`OnCollisionExit` does not fire when a pair falls asleep still touching.**
> Jolt puts settled bodies to sleep to avoid simulating them, and a sleeping
> pair has not separated — it has stopped being interesting. Code that counts
> enters and exits to track "am I standing on something" has to handle this;
> code that asks the question directly does not.

## The fixed step

Physics runs on the **fixed timestep**, not the frame. `OnTick` is that step;
`OnFrame` is the rendered frame. Gameplay that touches physics belongs in
`OnTick`, where the step size is constant and the same on every machine.

Between steps, transforms are interpolated for rendering, so a body simulated
at 60 Hz still looks smooth at 144 fps. See
[Core concepts](concepts.md#the-fixed-step).

## Raycasts

Available from both languages, returning the entity hit, the position, the
normal and the distance. See [Scripting](scripting/index.md).

## Seeing what the solver sees

The editor draws collider wireframes with **F3**, or View → Colliders. This is
the fastest way to find the two most common problems: a collider that does not
match the mesh it is on, and a collider nobody remembered to add.

## Play mode and physics

Bodies are created when play starts and destroyed when it stops, and the scene
is restored to exactly what it was. A cube knocked over during play stands back
up on Stop — that is snapshot semantics, and it is deliberate.

## Where to go next

- [Component reference](components.md#physics) — every field on both components
- [Scripting](scripting/index.md) — the callbacks and raycasts
- [Core concepts](concepts.md#the-fixed-step) — why there are two rates
