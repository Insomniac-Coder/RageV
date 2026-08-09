# Writing a script in C++

## Your first script

Create a `.cpp` file anywhere that gets compiled into your game, and write:

```cpp
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"

using namespace RageV;

class Bobber : public ScriptableEntity
{
public:
    void OnCreate() override
    {
        m_Origin = GetPosition();
    }

    void OnUpdate(Timestep dt) override
    {
        m_Elapsed += dt.GetSeconds();
        Vec3 position = m_Origin;
        position.y += std::sin(m_Elapsed * 2.0f) * 0.5f;
        GetPosition() = position;
    }

private:
    Vec3 m_Origin{ 0.0f };
    float     m_Elapsed = 0.0f;
};

RV_REGISTER_SCRIPT(Bobber);
```

Build, open the editor, select an entity, **Add Component → Script**, leave
**Language** on C++, and choose `Bobber` in the **Script** dropdown. Press Play.

The **Name** row above it is a label for the component and nothing else — call it
"Bobbing crate" if that helps you read the inspector. Which script runs is the
dropdown's job.

The name in the dropdown is the string `RV_REGISTER_SCRIPT` derived from the type
name, and that string is what gets written into the scene file.

> [!NOTE]
> A C++ script is compiled into the engine, so a script you have just written is
> not in the dropdown yet. It appears there marked **(needs engine rebuild)**,
> and you can attach it now — it starts working the moment the build catches up.
> A C# script is compiled into the project instead, and needs only
> **File → Build Scripts**.

> [!TRAP]
> **Renaming a registered script breaks every scene that used it.** The name is
> the durable reference, exactly as an asset handle is for assets — the same way
> renaming a C# class does in Unity. Registered names are API. Rename with the
> same care you would rename a public function.

## Registering, and one linker trap

`RV_REGISTER_SCRIPT` places a static registrar object at file scope, which runs
before `main` and adds your factory to the registry.

> [!TRAP]
> This works for scripts compiled **directly into an executable**, and fails
> silently for scripts inside a **static library** whose object file contains
> nothing else that is referenced. The linker is allowed to drop such an object
> file entirely, the registration never runs, your script does not appear in the
> dropdown, and there is no error anywhere. The engine's own built-in scripts hit
> this: they are registered from an explicit function that something else calls,
> for exactly this reason. If your script vanishes from the dropdown, this is
> why.

## The lifecycle

| Callback | When |
|---|---|
| `OnCreate()` | Once, on the first simulation step after Play. |
| `OnUpdate(Timestep dt)` | Every fixed simulation step. |
| `OnDestroy()` | On destruction, and when play mode stops. |

`OnCreate` runs on the first *step*, not when the component is added. By then
every entity in the scene exists, so it is safe to look others up — which is not
true of a constructor.

### OnUpdate is not per frame

This is the single most important thing on this page.

`OnUpdate` runs once per **fixed simulation step**, at 60 Hz by default. A frame
may run zero steps, one, or several. The `dt` you are handed is the fixed
timestep, and it is the same value every call.

> [!TRAP]
> Do not use `OnUpdate` for anything that should happen once per *frame*, and do
> not assume it is called at the frame rate. On a fast machine with vsync off it
> is called far less often than frames are drawn; on a slow one, several times
> per frame. Both are correct.

Multiply rates by `dt` anyway. It is what keeps behaviour identical when someone
runs the game at `--fixed-hz=120`:

```cpp
void OnUpdate(Timestep dt) override
{
    Translate(GetForward() * m_Speed * dt.GetSeconds());   // correct
    // Translate(GetForward() * 0.05f);                    // tied to the rate
}
```

## Reading and writing the transform

`GetPosition`, `GetRotation` and `GetScale` return **references into the
component**, so you can read them or assign through them. Rotation is in
**radians**.

```cpp
GetPosition().y = 3.0f;          // assign a component
Translate({ 0.0f, 0.0f, -1.0f }); // add a delta
Rotate({ 0.0f, Math::Radians(90.0f) * dt.GetSeconds(), 0.0f });
LookAt(target.GetComponent<TransformComponent>().Position);
```

These are **local** — relative to the parent. When you need the answer in world
space, ask for it:

```cpp
Vec3 here = GetWorldPosition();
Mat4 me   = GetWorldTransform();
```

`GetForward`, `GetRight` and `GetUp` give the entity's own axes, so "forward"
means forward *for this object* rather than for the world. That is what makes a
character controller work when the character is turned:

```cpp
const Vec3 direction =
    GetForward() * GetAxis("MoveForward") +
    GetRight()   * GetAxis("MoveRight");

if (Math::Dot(direction, direction) > 0.0f)
    Translate(Math::Normalize(direction) * m_Speed * dt.GetSeconds());
```

> [!TRAP]
> Normalising a zero-length vector produces NaNs, and a NaN in a transform
> spreads to every transform derived from it — the object and all its children
> disappear, permanently, with no error. Guard the normalise, as above. This is
> the most common way to lose an object in this engine.

## Components

```cpp
if (HasComponent<MeshComponent>())
{
    auto& mesh = GetComponent<MeshComponent>();
    // ...
}

auto& body = AddComponent<RigidBodyComponent>();
RemoveComponent<AudioSourceComponent>();
```

`GetComponent` does not check. Ask `HasComponent` first, or be certain.

## Finding other entities

```cpp
Entity player = FindEntityByName("Player");
if (player)                                    // always test
    LookAt(player.GetComponent<TransformComponent>().Position);
```

An `Entity` converts to `bool`, and an invalid one is what you get when nothing
matched. `FindEntitiesByName` returns every match when names are not unique.

For anything you intend to remember, use the UUID:

```cpp
void OnCreate() override
{
    if (Entity player = FindEntityByName("Player"))
        m_Player = player.GetUUID();
}

void OnUpdate(Timestep) override
{
    Entity player = FindEntityByUUID(m_Player);
    if (!player)
        return;    // destroyed since
    // ...
}
```

> [!TRAP]
> Looking up by name every step is a linear scan of the scene. Once, in
> `OnCreate`, into a `UUID` — then look the UUID up. On a small scene the
> difference is invisible; on a large one it is the script's whole cost.

## Spawning and destroying

```cpp
Entity spark = Spawn("Spark");
spark.AddComponent<MeshComponent>();

Entity enemy = SpawnPrefab(m_EnemyPrefab);   // an AssetHandle

Destroy(enemy);    // that one
Destroy();         // this one
```

Destruction is **deferred to the end of the simulation step**. It has to be:
destroying an entity while the script pass is walking them would invalidate the
iteration, and a script destroying itself mid-update would delete the object
currently executing.

The practical consequence is that a destroyed entity is still there for the rest
of the step, and every script that looks it up in the same step will find it.
Test validity rather than assuming a `Destroy` has already taken effect.

## Physics

All of these are no-ops outside play mode and on an entity with no rigid body.
Velocities and impulses are **world space**.

```cpp
AddForce({ 0.0f, 20.0f, 0.0f });      // accumulates over a step, cleared by it
AddImpulse({ 0.0f, 5.0f, 0.0f });     // changes velocity at once
SetLinearVelocity({ 0.0f, 0.0f, 0.0f });
Vec3 v = GetLinearVelocity();
```

**A jump is an impulse; a thruster is a force.** Applying a force for one step
and expecting a jump gets you a twitch, because the force is cleared by the step
that consumed it.

### Raycasts

```cpp
if (RayHit hit = Raycast(GetWorldPosition(), GetForward() * 10.0f))
{
    // hit.Entity, hit.Point, hit.Normal, hit.Distance
}
```

The direction need not be normalised — the ray extends to its length, which is
how you set the range. The nearest body along it wins, and that may be the
entity casting the ray; check for that if it matters.

## Collisions and triggers

Six callbacks, delivered **after** the simulation step that produced them — so a
script reacting to a hit is reacting to a resolved one.

```cpp
void OnCollisionEnter(const Collision& collision) override
{
    if (collision.ImpactSpeed > 3.0f)
        PlayOneShot(m_ThudClip, Math::Min(collision.ImpactSpeed / 10.0f, 1.0f));
}
```

| Callback | Fires |
|---|---|
| `OnCollisionEnter` / `Exit` | Once per pair, for solid colliders |
| `OnCollisionStay` | Every step in between |
| `OnTriggerEnter` / `Stay` / `Exit` | The same, for colliders marked `IsTrigger` |

The same event reaches **both** entities, each with `Other` set to the one it is
not, and `Normal` pointing back at itself. So a script can be written without
knowing which side of the pair it happens to be on.

`Collision::Other` may already have been destroyed this step. Test it:

```cpp
if (!collision.Other)
    return;
```

> [!TRAP]
> **A pair that comes to rest does not report Exit when the simulation puts it to
> sleep.** The objects are still touching, and the engine only reports what
> changed physically. If you are counting occupants, a sleeping body stays
> counted — which is correct, and is not what most people expect.

> [!TRAP]
> **A trigger only sees bodies that are awake.** Something that falls asleep
> inside a trigger stops being reported until it moves again. If a trigger must
> notice a resting object, keep the object awake or poll it instead.

## Audio

```cpp
PlaySource();                        // this entity's AudioSourceComponent
StopSource();
if (IsSourcePlaying()) { }

PlayOneShot(m_ClipHandle, 0.8f);     // at this entity's position
PlayOneShot2D(m_UiClip);             // unpositioned: UI, narration, a stinger
```

`PlaySource` restarts the source if it is already playing, and does nothing if
the entity has no `AudioSourceComponent`. One-shots are fire-and-forget; the
returned `AudioVoice` is only useful for a sound long enough to want to
interrupt.

## Input

By **action name**, never by key code:

```cpp
if (WasActionPressed("Jump"))
    AddImpulse({ 0.0f, 5.0f, 0.0f });

if (IsActionDown("Sprint"))
    speed *= 3.0f;

const float turn = GetAxis("LookX");
```

This is not ceremony. A script written against `"Jump"` keeps working when the
player rebinds it, when a gamepad is added, and when the same action is bound to
two things at once. A script written against a key code does none of that.

Edges — `WasActionPressed` and `WasActionReleased` — are consumed by the first
step that runs, and a frame with no steps carries them forward rather than
losing them. So a press is never missed and never seen twice.

## Time

```cpp
const float step = GetFixedDeltaTime();   // the same value dt carries
const float now  = GetTime();             // seconds since the process started
```

## The engine's own scripts as worked examples

`RageV/src/RageV/Scripts/BuiltinScripts.cpp` contains six scripts that ship with
the engine and are readable in about ten minutes:

| Script | What it demonstrates |
|---|---|
| `Spinner` | The minimum: one override, rate multiplied by `dt` |
| `Mover` | Input by action, movement relative to the entity's own axes |
| `Follow` | Reaching another entity, held by UUID |
| `ImpactFlash` | Collision events driving a material change |
| `ImpactSound` | Scaling a sound by `ImpactSpeed` |
| `TriggerZone` | Trigger enter/exit bookkeeping, including the sleep caveat |

They exist partly so that pressing Play does something observable out of the box,
and partly to be exactly this: the worked examples of what reads well.

## Common mistakes

| Symptom | Cause |
|---|---|
| Script missing from the dropdown | Registered in a static library, object file dropped by the linker |
| Object vanishes and never returns | NaN in the transform, usually from normalising a zero vector |
| Behaviour changes with `--fixed-hz` | A rate not multiplied by `dt` |
| A jump barely twitches | `AddForce` where `AddImpulse` was meant |
| Trigger never reports Exit | The body fell asleep inside it |
| Stale entity after Stop | An `Entity` stored across play mode instead of a `UUID` |
| Scene loses its scripts on save | The script's registered name was changed |
