# Writing a script in C#

## Your first script

Every project has a `Scripts/` folder with a real `.csproj` in it. Add a file
there — or select an entity, add a **Script** component, switch **Language** to
C#, and choose **New Script...** in the dropdown, which writes one for you:

```csharp
using RageV;

public class Bobber : Script
{
    private float m_Height = 0.5f;
    private Vector3 m_Origin;
    private float m_Elapsed;

    public override void OnCreate()
    {
        m_Origin = Position;
    }

    public override void OnUpdate(float deltaTime)
    {
        m_Elapsed += deltaTime;
        Position = m_Origin + new Vector3(0.0f, MathF.Sin(m_Elapsed * 2.0f) * m_Height, 0.0f);
    }
}
```

**File → Build Scripts** (Ctrl+B) compiles it. The build runs in the
background — the Build Log panel shows the compiler's output as it happens —
and when it finishes, the assembly loads and `Bobber` appears in the Script
dropdown. Pick it, press Play.

The class name is what scene files store, so renaming a class breaks every
scene that used it — the same rule as C++, the same rule as Unity.

> [!NOTE]
> The `.csproj` is not a prop. Open it in Visual Studio, Rider or VS Code and
> you get completion against the engine's API. The reference to
> `RageV.ScriptCore` is supplied by the editor at build time, so the project
> file works on any machine the project is copied to.

> [!NOTE]
> **Edits reload live.** Build Scripts retires the old assembly and loads the
> new one in its place — change a script, Ctrl+B, and the new code runs. No
> restart. If the scene is *playing* when you build, the editor stops it,
> swaps both languages' scripts, and resumes Play on the new code — the same
> loop for C++ and C#. After a failed build it stays stopped with the errors
> in the Build Log, rather than auto-playing old code under you.

## Fields and the inspector

Any field of a supported type shows up in the inspector — `bool`, `int`,
`float`, `string`, `Vector3`. Private is fine: reflection can reach it, and
requiring `public` would be telling people to write worse C# for the
inspector's benefit. This is the opposite of the C++ rule, where a field must
be public because the registration has to name it from outside the class.

The value shown before anyone edits it is whatever the field initialiser says
— `private float m_Speed = 1.2f;` shows 1.2. Only values somebody actually
changed are stored in the scene, so editing a default in code reaches every
entity that never overrode it.

A field of any other type is not an error. It simply does not appear, and
keeps whatever the constructor gave it.

## The lifecycle

```csharp
public override void OnCreate() { }              // once, on the first step after Play
public override void OnUpdate(float deltaTime) { } // every fixed step -- see below
public override void OnDestroy() { }             // on destruction, and when Play stops
```

Every entity in the scene exists by the time `OnCreate` runs, so looking
others up is safe there — which is not true of a constructor.

### OnUpdate is not per frame

`OnUpdate` runs once per **fixed simulation step**, and `deltaTime` is the
fixed timestep — the same value every call, also available as
`Time.FixedDeltaTime`. A frame may run zero steps, one, or several. Multiply
rates by `deltaTime` anyway, so behaviour is identical when someone runs the
game at a different simulation rate.

## Reading and writing the transform

The transform properties are on the script, local, and relative to the parent.
Rotation is Euler angles in **radians**, XYZ order.

```csharp
Position += new Vector3(0.0f, 0.1f, 0.0f);   // or Translate(...)
Rotate(new Vector3(0.0f, 2.0f * deltaTime, 0.0f));
Scale = new Vector3(2.0f, 2.0f, 2.0f);
```

`Entity.WorldPosition` answers in world space, with every parent applied.
`Forward`, `Right` and `Up` are the entity's own axes, so "forward" means
forward *for this object* — which is what makes a character controller work
when the character is turned:

```csharp
Vector3 direction = Forward * Input.GetAxis("MoveForward")
                  + Right   * Input.GetAxis("MoveRight");

if (direction.LengthSquared() > 0.0f)
    Translate(direction.Normalized() * m_Speed * deltaTime);
```

> [!TRAP]
> `Entity` is a struct returned by a property, so `Entity.Position = v` on its
> own assigns to a temporary — the compiler rejects it, which is the good
> outcome. Use the script's own `Position` property, or copy the entity to a
> local first. The shorthand properties exist precisely so scripts read like
> their C++ counterparts.

## Finding, spawning, destroying

```csharp
Entity player = Entity.FindByName("Player");
if (player.Exists) { ... }

Entity spawned = Entity.Spawn("Projectile");
spawned.Destroy();
```

An `Entity` is a UUID, not a pointer — it survives saving, loading and play
mode, and looking up one that no longer exists answers `false` rather than
faulting.

`FindByName` is linear over the scene. Do it once in `OnCreate` and keep the
result, rather than every step. `Destroy` is deferred to the end of the
simulation step, and has to be: a script destroying its own entity mid-update
would delete the object currently executing, so a destroyed entity is still
findable for the rest of that step.

## Physics

```csharp
AddForce(new Vector3(0.0f, 0.0f, -20.0f));    // accumulates over the step: a thruster
AddImpulse(new Vector3(0.0f, 5.0f, 0.0f));    // changes velocity at once: a jump

Vector3 v = Entity.LinearVelocity;
```

All of it is world space, a no-op outside play mode, and a no-op on an entity
with no rigid body.

## Collisions and triggers

Override what you need; the defaults do nothing.

```csharp
public override void OnCollisionEnter(Collision collision)
{
    if (collision.ImpactSpeed > 5.0f)
        Log.Info($"hit by {collision.Other.Name} at {collision.ImpactSpeed:F1} m/s");
}
```

The same event reaches both entities involved, each seeing the other as
`Other` and `Normal` pointing back at itself — so a script never needs to know
which side of the pair it is. `ImpactSpeed` is the closing speed when they
met, never negative; scale a sound or a damage number by it.

> [!TRAP]
> `collision.Other` may already have been destroyed in the same step. Test
> `collision.Other.Exists` before reaching into it.

Two sleep-related honesty notes, the same as C++: a pair that comes to rest
does **not** report `OnCollisionExit` when the simulation puts it to sleep —
the objects are still touching — and a trigger only sees bodies that are
awake.

## Input

```csharp
if (Input.WasActionPressed("Jump"))
    AddImpulse(new Vector3(0.0f, m_JumpImpulse, 0.0f));

float forward = Input.GetAxis("MoveForward");
```

By action name, never by key code — there is deliberately no way to ask about
a key. A script written against `"Jump"` keeps working when the player rebinds
it. `WasActionPressed` is consumed by the first step that runs and carried
forward by a frame that runs none, so a press is never missed and never seen
twice.

## Logging

```csharp
Log.Info($"{Entity.Name} is ready");
Log.Warn("low on fuel");
```

Not `Console.WriteLine`: a packaged game has no console, and the engine's log
is the only place anybody looks.

## Raycasts

```csharp
RayHit hit = Raycast(Position, Forward * 50.0f);
if (hit)
    Log.Info($"looking at {hit.Entity.Name}, {hit.Distance:F1} away");
```

Nearest body along the ray, which may be your own. The direction need not be
normalised — the ray extends to its length. `hit.Entity` may have been
destroyed since; test `Exists` before reaching into it.

## Audio

```csharp
PlaySource();                                // this entity's AudioSourceComponent
StopSource();
if (IsSourcePlaying) { ... }

PlayOneShot();                               // the source's clip, fire-and-forget, at this entity
PlayOneShot("audio/thud.wav", 0.7f);         // any clip, by asset path
ulong voice = Audio.PlayOneShot2D("audio/stinger.wav");   // unpositioned: UI, narration
Audio.StopVoice(voice);
```

The same rules as C++: `PlaySource` restarts rather than overlaps (overlap is
what one-shots are for), an entity without an `AudioSourceComponent` is a
quiet no-op, and clips are named by their asset path — handles are the
engine's internal names, and a script has no honest way to hold one.

## Hierarchy

```csharp
Entity.Parent = Entity.FindByName("Rig");    // Entity.Invalid moves it to the root
foreach (Entity child in Entity.Children)
    child.Destroy();
```

`Entity.SpawnPrefab("prefabs/rock.prefab")` instantiates a prefab by its
asset path. And `LookAt(target)` turns the entity so its forward (−Z) faces
the target — aiming at your own position is a no-op, not a NaN.

## Components

C++ scripts reach components as types — `GetComponent<LightComponent>()`. A
template cannot cross a language boundary, so C# reaches them **by registry
name, with values as text** — the same names and the same text forms the
inspector and the scene file use:

```csharp
if (Entity.HasComponent("LightComponent"))
    Entity.SetComponentField("LightComponent", "Intensity", "3.5");

Entity.AddComponent("AudioSourceComponent");
Entity.SetComponentField("AudioSourceComponent", "Clip", "audio/hum.wav");

string color = Entity.GetComponentField("LightComponent", "Color");  // "1 0.8 0.6 1"
```

Floats are invariant, vectors space-separated, booleans `true`/`false`,
assets by path. Because the component registry drives this, a component
gaining a field is visible from C# the moment it exists — nothing here goes
stale. Components the editor calls essential (the transform, the tag) refuse
removal from a script exactly as they refuse the inspector's X.

This manual describes the engine as it exists: everything a C++ script can
call now has a C# spelling, with the one structural exception above — typed
component access — traded for the registry's named access.

## When something throws

Every boundary the runtime crosses catches everything: an exception in a
script logs and stops that script's callback, and the editor survives. This is
the deliberate trade against C++, where a mistake is a crash. The cost is that
a silent `catch` can hide a broken script — read the log.

## Common mistakes

- **Doing per-frame work in `OnUpdate`.** It is per fixed step. Anything tied
  to rendering — smoothing a camera, fading UI — has no home in a script yet.
- **`FindByName` every step.** Cache it in `OnCreate`.
- **Forgetting that Stop rewinds the scene.** Play mode works on a copy;
  everything a script changed is discarded when play stops, `OnDestroy` runs,
  and the edit-mode scene returns untouched.
- **Expecting Play to survive a build.** Building mid-play restarts the
  scene: Stop, swap, Play again from the edit-mode state. Whatever the run
  had accumulated is gone, which is what Stop always means.
