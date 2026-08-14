# Writing a script in C++

## Your first script

Every project has a `Source/` folder that builds into the project's **game
module** — a DLL the engine loads when the project opens. A C++ script is a
file in it. Add one there — or select an entity, add a **Script** component,
and choose **New Script...** in the dropdown, which writes one for you:

```cpp
#include <rvpch.h>
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

    void OnTick(Timestep dt) override
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

**File → Build Scripts** (Ctrl+B) compiles the module — in the background,
with the compiler's output live in the Build Log panel — and reloads it when
the build finishes. `Bobber` appears in the Script dropdown. Pick it, press
Play. No engine rebuild, no restart.

The **Name** row above the dropdown is a label for the component and nothing
else — call it "Bobbing crate" if that helps you read the inspector. Which
script runs is the dropdown's job.

The name in the dropdown is the string `RV_REGISTER_SCRIPT` derived from the type
name, and that string is what gets written into the scene file.

> [!NOTE]
> A script you have written but not yet built appears in the dropdown marked
> **(not built)**, and you can attach it now — it starts working the moment
> the build catches up.

> [!NOTE]
> Building while the scene is playing restarts it: live instances run code
> from the loaded DLL, so the editor stops the scene, swaps the module, and
> resumes Play on the new code. After a failed build it stays stopped with
> the errors in the Build Log rather than auto-playing the old code.

> [!TRAP]
> **Renaming a registered script breaks every scene that used it.** The name is
> the durable reference, exactly as an asset handle is for assets — the same way
> renaming a C# class does in Unity. Registered names are API. Rename with the
> same care you would rename a public function.

## Registering

`RV_REGISTER_SCRIPT` places a static registrar object at file scope. It runs
when the game module loads — the engine loads the DLL when the project opens,
the registrars fire, and the scripts exist. There is no manifest to maintain
and no list to forget a script from.

> [!TRAP]
> That mechanism has one historical failure mode worth knowing even though the
> module is immune to it: a static registrar inside a **static library** whose
> object file contains nothing else referenced gets dropped by the linker, the
> registration never runs, and there is no error anywhere. A DLL is linked
> from its object files, so a game module never hits this — but a script
> compiled into a static library of your own will, silently.

## The lifecycle

| Callback | When |
|---|---|
| `OnCreate()` | Once, on the first simulation step after Play. |
| `OnTick(Timestep dt)` | Every fixed simulation step. |
| `OnFrame(Timestep dt)` | Every rendered frame. |
| `OnDestroy()` | On destruction, and when play mode stops. |

`OnCreate` runs on the first *step*, not when the component is added. By then
every entity in the scene exists, so it is safe to look others up — which is not
true of a constructor.

### Two rates, and choosing between them

This is the single most important thing on this page.

The names say **when**, not what, because when is the only thing that decides
whether the code inside is correct.

`OnTick` runs once per **fixed simulation step**, at 60 Hz by default. A frame
may run zero steps, one, or several. The `dt` you are handed is the fixed
timestep, and it is the same value every call.

`OnFrame` runs once per **rendered frame**, and its `dt` is the real elapsed
time, which varies with the machine, the scene, and whatever else is happening.

The rule:

> **Gameplay goes in `OnTick`. Presentation goes in `OnFrame`.**
>
> If the physics, another player or a replay has to agree with it, it belongs on
> the fixed step. If it is only ever looked at, it belongs on the frame.

| Belongs in `OnTick` | Belongs in `OnFrame` |
|---|---|
| Moving a body, applying a force | Moving a camera |
| Firing, scoring, taking damage | Fading, flashing, pulsing |
| A timer that decides an outcome | A number counting up on screen |
| Anything a replay must reproduce | Anything only the eye consumes |

> [!TRAP]
> Do not use `OnTick` for anything that should happen once per *frame*, and do
> not assume it is called at the frame rate. On a fast machine with vsync off it
> is called far less often than frames are drawn; on a slow one, several times
> per frame. Both are correct.

> [!TRAP]
> And do not move gameplay into `OnFrame` because it feels smoother. It is
> frame-rate dependent by construction: the same input gives a different result
> on a different machine, which is exactly what the fixed step exists to prevent.

Multiply rates by `dt` in both. In `OnTick` it is what keeps behaviour identical
when someone runs the game at `--fixed-hz=120`:

```cpp
void OnTick(Timestep dt) override
{
    Translate(GetForward() * m_Speed * dt.GetSeconds());   // correct
    // Translate(GetForward() * 0.05f);                    // tied to the rate
}
```

### Why a camera belongs in OnFrame

The engine blends the last two simulation states every frame, so a simulated
body moves smoothly at any display rate. `OnFrame` runs *after* that blend, so
what it reads is what is about to be drawn.

Chase a target from `OnTick` at 240 Hz and the camera moves on one frame in four
while the world moves on all four. That reads *worse* than no smoothing at all,
because the stutter is differential — the world glides and the camera judders
against it. The built-in `Follow` is the worked example:

```cpp
void OnFrame(Timestep dt) override
{
    const Vec3 goal = m_Target.GetComponent<TransformComponent>().Position + m_Offset;

    // Framerate-independent smoothing. A plain lerp by a constant factor
    // converges at a rate that depends on the step size, which on a frame --
    // where dt varies -- means lagging further behind whenever the rate dips.
    const float t = 1.0f - std::exp(-m_Sharpness * dt.GetSeconds());
    GetPosition() += (goal - GetPosition()) * t;
}
```

To smooth something the engine cannot see — a value your own script computed in
`OnTick` — `GetInterpolationAlpha()` is how far this frame falls between the last
step and the next, from 0 to 1:

```cpp
void OnTick(Timestep) override  { m_Previous = m_Current; m_Current = Compute(); }
void OnFrame(Timestep) override { Show(Math::Lerp(m_Previous, m_Current, GetInterpolationAlpha())); }
```

It is meaningless inside `OnTick`, where it is whatever the last frame left.

### Ordering, exactly

Within one frame: physics transforms are interpolated, world transforms are
derived, `OnFrame` runs, anything it destroyed is deleted, world transforms are
derived again, and then audio, particles and rendering all read the result.

`OnFrame` **never runs before `OnCreate`**. Script instances are created by the
fixed pass and by nothing else, so a newly spawned entity gets its first `OnTick`
before its first `OnFrame`.

> [!NOTE]
> `OnUpdate` was the fixed-step callback before the two rates were split. It no
> longer exists, and a script that still declares it fails to compile — by
> design: the alternative is a method the engine silently never calls.


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

void OnTick(Timestep) override
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

PlayOneShot(m_ClipHandle, 0.8f);            // at this entity's position
PlayOneShot(m_ClipHandle, 0.8f, 1.1f);      // ... a little higher
PlayOneShotAt(m_ClipHandle, hit.Position);  // somewhere no entity stands
PlayOneShot2D(m_UiClip);                    // unpositioned: UI, narration
```

`PlaySource` restarts the source if it is already playing, and does nothing if
the entity has no `AudioSourceComponent`. One-shots are fire-and-forget; the
returned `AudioVoice` is only useful for a sound long enough to want to
interrupt.

`PlayOneShotAt` exists for sounds that belong to an event rather than to a
thing: a ricochet at the point a ray struck, a particle burst, an impact
between two objects that is at neither of them.

### Pitch

Every one-shot takes a pitch, where 1 is the clip as recorded. It is a
resampling ratio rather than a shift, so 2 is an octave up and half as long.

A small random spread is the difference between an impact that sounds like an
impact and one that sounds like a sampler:

```cpp
const float pitch = 0.94f + (float)std::rand() / RAND_MAX * 0.12f;
PlayOneShot(m_ThudClip, loudness, pitch);
```

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

### One extra line, once there is a UI

The action map does not know a canvas exists. So a click on a pause button is
also a click at the world behind it, and the bug shows up as *"the gun fires
when I open the menu"* — a gameplay symptom, nowhere near the menu that caused
it. The fix is a guard, and it belongs on every pointer action in the game:

```cpp
if (!IsPointerOverUI() && WasActionPressed("Fire"))
    Fire();
```

Keyboard actions are unaffected. `IsPointerOverUI` is true while the pointer is
over a button — or over a rectangle whose `BlocksPointer` is set, which is how a
modal's backdrop stops the world behind it — and it stays true for the *whole*
of a press that began on a button, even if the hand has since moved off it.

## The game's UI

A button is a `UIImageComponent` and a `UIButtonComponent` on one entity, with a
label as a child. **There are two ways to hear about a click**, and they are the
same click seen twice — use whichever fits, not both on one button.

### The button calls your method

Register the method, then pick it from the button's **On Click** in the
inspector:

```cpp
class Menu : public ScriptableEntity
{
public:
    void StartGame() { /* ... */ }
    void Quit()      { /* ... */ }
};

RV_REGISTER_SCRIPT(Menu)
    .Method<&Menu::StartGame>("StartGame")
    .Method<&Menu::Quit>("Quit");
```

C++ has no reflection, so the engine cannot find `StartGame` on its own — the
registration is how the name gets into the dropdown. (C# needs no equivalent;
see the C# guide.) The method must be **public**, take no arguments and return
`void`; anything else fails to compile at the registration line, naming the
method.

The button's **Target** is which entity's script to call. Leave it empty for the
button's own entity, which is what a script sitting on the button wants.

> [!TRAP]
> **The name in the scene file has no compiler behind it.** Rename `StartGame`
> in your code and the scene still says `"StartGame"` — the build succeeds and
> the button stops working. So a binding that resolves to nothing **logs a
> warning on every click**, naming the button, the target and the method. If a
> button does nothing, read the log before reading the code.

### Your script asks the button

```cpp
void OnTick(Timestep) override
{
    if (WasButtonClicked())
        m_Score++;
}
```

Reach for this when the script is on the button itself, or when one manager
reads several buttons — `if (WasButtonClicked(m_Start))` and
`if (WasButtonClicked(m_Quit))` in one place is often clearer than two bindings
pointing back at it.

On the fixed step, not the frame: a click is an event the game acts on, and
acting on it twice because one frame ran two steps is exactly what the edge
contract exists to prevent. It is the same contract `WasActionPressed` has —
consumed by the first step that runs, carried forward by a frame with none.
**Both** mechanisms honour it, so a bound method is also called exactly once per
click.

Text is written the same way it is read:

```cpp
SetText(FindEntityByName("Score"), "Score: " + std::to_string(m_Score));
```

A press that slides off the button before the release is **cancelled**, not
delivered — the same behaviour every desktop toolkit has, and the difference
between a button and a tripwire. Coming back to it before letting go completes
the click.

`ClickCounter` in `BuiltinScripts.cpp` is the whole of a working button in about
a dozen lines.

## Render settings

The scene's post-processing, live. `GetRenderSettings()` hands back the
struct the renderer reads when it builds the frame, so a change made in
`OnTick` or `OnFrame` is on screen that frame.

```cpp
class Flashbang : public RageV::ScriptableEntity
{
public:
    void Detonate() { m_Remaining = 2.0f; }

    void OnFrame(RageV::Timestep dt) override
    {
        if (m_Remaining <= 0.0f)
            return;

        m_Remaining -= dt;

        // Blown out, recovering over two seconds.
        GetRenderSettings().Exposure = 1.0f + 4.0f * std::max(m_Remaining, 0.0f) / 2.0f;
    }

private:
    float m_Remaining = 0.0f;
};
```

Everything in `SceneEnvironment` is there: `AA`, `Exposure`, the five bloom
values, the ambient term, the sky and the shadow settings.

```cpp
GetRenderSettings().AA = RageV::AntiAliasing::SMAA;
GetRenderSettings().BloomEnabled = false;
```

**These are not saved.** They are a runtime override; a game dimming its own
bloom should not quietly edit the scene asset.

C# reaches the same settings as `RenderSettings.Exposure` and friends, over a
name and a piece of text rather than the struct — a struct cannot cross that
boundary.

## Time

```cpp
const float step = GetFixedDeltaTime();   // the same value dt carries
const float now  = GetTime();             // seconds since the process started
```

## The engine's own scripts as worked examples

`RageV/src/RageV/Scripts/BuiltinScripts.cpp` contains seven scripts that ship
with the engine and are readable in about ten minutes:

| Script | What it demonstrates |
|---|---|
| `Spinner` | The minimum: one override, rate multiplied by `dt` |
| `Mover` | Input by action, movement relative to the entity's own axes |
| `Follow` | Reaching another entity, held by UUID |
| `ImpactFlash` | Collision events driving a material change |
| `ImpactSound` | Scaling a sound by `ImpactSpeed` |
| `TriggerZone` | Trigger enter/exit bookkeeping, including the sleep caveat |
| `ClickCounter` | A working UI button: poll the click, write the label |

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
