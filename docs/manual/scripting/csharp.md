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

    public override void OnTick(float deltaTime)
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
public override void OnCreate() { }                // once, on the first step after Play
public override void OnTick(float deltaTime) { }   // every fixed step   -- see below
public override void OnFrame(float deltaTime) { }  // every rendered frame
public override void OnDestroy() { }               // on destruction, and when Play stops
```

Every entity in the scene exists by the time `OnCreate` runs, so looking others
up is safe there — which is not true of a constructor.

### Two rates, and choosing between them

The names say **when**, not what, because when is the only thing that decides
whether the code inside is correct.

`OnTick` runs once per **fixed simulation step**, and its `deltaTime` is the
fixed timestep — the same value every call, also available as
`Time.FixedDeltaTime`. A frame may run zero steps, one, or several. Multiply
rates by `deltaTime` anyway, so behaviour is identical when someone runs the
game at a different simulation rate.

`OnFrame` runs once per **rendered frame**, and its `deltaTime` is the real
elapsed time, which varies.

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

### Why a camera belongs in OnFrame

The engine blends the last two simulation states every frame, so a simulated
body moves smoothly at any display rate. `OnFrame` runs *after* that blend, so
what it reads is what is about to be drawn.

Chase a target from `OnTick` at 240 Hz and the camera moves on one frame in four
while the world moves on all four — which reads worse than no smoothing at all,
because the stutter is differential. The built-in `Follow` is the worked
example:

```csharp
public override void OnFrame(float deltaTime)
{
    if (!m_Target.Exists)
        return;

    Vector3 goal = m_Target.Position + new Vector3(0.0f, m_OffsetY, m_OffsetZ);
    Vector3 here = Position;

    // Framerate-independent smoothing: a plain lerp by a constant factor
    // converges at a rate that depends on the step size.
    float t = 1.0f - System.MathF.Exp(-m_Sharpness * deltaTime);
    Position = here + (goal - here) * t;
}
```

To smooth something the engine cannot see — a value your own script computed in
`OnTick` — `Time.InterpolationAlpha` is how far this frame falls between the last
step and the next, from 0 to 1:

```csharp
public override void OnTick(float deltaTime)
{
    m_Previous = m_Current;
    m_Current = Compute();
}

public override void OnFrame(float deltaTime)
{
    float shown = m_Previous + (m_Current - m_Previous) * Time.InterpolationAlpha;
}
```

It is meaningless inside `OnTick`, where it is whatever the last frame left.

### Ordering, exactly

Within one frame: physics transforms are interpolated, world transforms are
derived, `OnFrame` runs, anything it destroyed is deleted, world transforms are
derived again, and then audio, particles and rendering all read the result.

`OnFrame` **never runs before `OnCreate`**. Script instances are created by the
fixed pass and by nothing else, so a newly spawned entity gets its first `OnTick`
before its first `OnFrame`. C++ and C# scripts get both rates identically, and
the C++ half runs first at each — an order that is stated rather than left to
whatever the component pools happen to do this build.

> [!NOTE]
> `OnUpdate` was the fixed-step callback before the two rates were split. It is
> gone: an `override` of it is now a compile error, and a plain declaration is a
> hides-inherited-member warning plus a line in the log the first time the
> script is created. Neither is silence, which is the point.


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

### One extra line, once there is a UI

The action map does not know a canvas exists, so a click on a pause button is
also a click at the world behind it. The symptom is *"the gun fires when I open
the menu"* — a gameplay bug, nowhere near the menu that caused it:

```csharp
if (!Input.IsPointerOverUI && Input.WasActionPressed("Fire"))
    Fire();
```

Keyboard actions are unaffected. It is true while the pointer is over a button
— or over a rectangle whose `BlocksPointer` is set, which is how a modal's
backdrop stops the world behind it — and it stays true for the *whole* of a
press that began on a button, even if the hand has since moved off it.

## The game's UI

A button is a `UIImageComponent` and a `UIButtonComponent` on one entity, with a
label as a child. **There are two ways to hear about a click**, and they are the
same click seen twice — use whichever fits, not both on one button.

### The button calls your method

Write an ordinary method and pick it from the button's **On Click** in the
inspector:

```csharp
public class Menu : Script
{
    public void StartGame() { /* ... */ }
    public void Quit()      { /* ... */ }
}
```

**No registration.** This is the one place C# is plainly less work than C++,
where every bindable method has to be declared to the engine by hand: reflection
finds these, so writing the method is the whole job. It must be **public**, take
no arguments and return `void` — the dropdown offers nothing else.

The button's **Target** is which entity's script to call. Leave it empty for the
button's own entity.

> [!TRAP]
> **The name in the scene file has no compiler behind it.** Rename `StartGame`
> and the scene still says `"StartGame"` — the build succeeds and the button
> stops working. So a binding that resolves to nothing **logs a warning on every
> click**, naming the button, the target and the method. If a button does
> nothing, read the log before reading the code.

### Your script asks the button

```csharp
public override void OnTick(float dt)
{
    if (m_StartButton.WasButtonClicked())
        StartRound();
}
```

Reach for this when one manager reads several buttons, or when the click is one
of a few things a step already checks.

On the fixed step, not the frame: a click is an event the game acts on, and
acting on it twice because one frame ran two steps is what the edge contract
exists to prevent. Same terms as `WasActionPressed` — consumed by the first step
that runs, carried forward by a frame with none. A press dragged off the button
before the release is cancelled and never reported. **Both** mechanisms honour
that, so a bound method is also called exactly once per click.

Text is a property:

```csharp
m_ScoreLabel.Text = $"Score: {m_Score}";
```

That one is a direct call rather than `SetComponentField`, because a score is
written every frame and nobody should have to know a registry name for the
commonest thing in a HUD. **Colour deliberately is not**: a UI entity has two of
them, so it says which —

```csharp
m_Bar.SetComponentField("UIImageComponent", "Color", "0.9 0.2 0.2 1");
```

## Render settings

The scene's post-processing, live. Read fresh when each frame is built, so a
change made in `OnTick` or `OnFrame` is on screen that frame.

```csharp
using RageV;

public class Flashbang : Script
{
    private float _remaining;

    public void Detonate() => _remaining = 2.0f;

    protected override void OnFrame(float dt)
    {
        if (_remaining <= 0.0f)
            return;

        _remaining -= dt;

        // Blown out, recovering over two seconds.
        RenderSettings.Exposure = 1.0f + 4.0f * Math.Max(_remaining, 0.0f) / 2.0f;
    }
}
```

The typed properties are `AntiAliasing`, `Exposure`, `BloomEnabled`,
`BloomThreshold`, `BloomIntensity`, `AmbientIntensity`, `ShadowsEnabled` and
`ShadowDistance`.

```csharp
RenderSettings.AntiAliasing = AntiAliasing.Smaa;   // None, Fxaa, Smaa
RenderSettings.BloomEnabled = false;
```

Anything without a property is still reachable by name — the properties are a
typed front for the same bridge, and the names are the file formats' own keys:

```csharp
RenderSettings.Set("SkyIntensity", "0.2");
string sky = RenderSettings.Get("SkyIntensity");
```

**One name space, three owners.** A setting lives in one of three places —
anti-aliasing and shadows on the project, exposure and bloom on a
`.rvpostprofile` the camera names, ambient and the sky on the scene — and
this API deliberately does not make you care which: the name says where it
goes. See [Where a setting lives](../concepts.md#where-a-setting-lives).

The one consequence worth knowing: **a post setting on a camera with no
profile attached reports "no such setting"**, because there is genuinely
nothing there to write. `Set` answers false and `Get` answers null. Attach a
profile in the inspector — the camera's Post profile row has a **New post
profile…** entry — and both start working.

**These are not saved.** They are a runtime override; a game dimming its own
bloom should not quietly edit an asset. The editor drops its cached profiles
when Play stops, so what you go back to editing is what the file says.

**A settings menu is the obvious use, and quality is the other one.** Dropping
to `AntiAliasing.Fxaa`, or to `None`, is the cheapest frame time a game can buy
back without touching its content.

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
PlayOneShot("audio/thud.wav", 0.7f, 1.1f);   // ... a little higher
Audio.PlayOneShotAt("audio/ping.wav", hit.Position);      // where no entity stands
ulong voice = Audio.PlayOneShot2D("audio/stinger.wav");   // unpositioned: UI, narration
Audio.StopVoice(voice);
```

The same rules as C++: `PlaySource` restarts rather than overlaps (overlap is
what one-shots are for), an entity without an `AudioSourceComponent` is a
quiet no-op, and clips are named by their asset path — handles are the
engine's internal names, and a script has no honest way to hold one.

`Audio.PlayOneShotAt` is for a sound that belongs to an event rather than to a
thing: a ricochet where a ray struck, an impact between two objects that is at
neither of them.

Every one-shot takes a pitch, where 1 is the clip as recorded — a resampling
ratio, so 2 is an octave up and half as long. A small random spread is what
stops a repeated impact sounding like a sampler:

```csharp
private readonly System.Random m_Random = new System.Random();

public override void OnCollisionEnter(Collision collision)
{
    float pitch = 0.94f + (float)m_Random.NextDouble() * 0.12f;
    PlayOneShot("audio/impact.wav", 1.0f, pitch);
}
```

## Hierarchy

```csharp
Entity.Parent = Entity.FindByName("Rig");    // Entity.Invalid moves it to the root
foreach (Entity child in Entity.Children)
    child.Destroy();
```

`Entity.SpawnPrefab("prefabs/rock.rprefab")` instantiates a prefab by its
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

- **Putting presentation in `OnTick` or gameplay in `OnFrame`.** `OnTick` is
  per fixed step and `OnFrame` is per frame; a camera smoothed on the wrong
  one judders, and a score counted on the wrong one depends on the frame
  rate.
- **`FindByName` every step.** Cache it in `OnCreate`.
- **Forgetting that Stop rewinds the scene.** Play mode works on a copy;
  everything a script changed is discarded when play stops, `OnDestroy` runs,
  and the edit-mode scene returns untouched.
- **Expecting Play to survive a build.** Building mid-play restarts the
  scene: Stop, swap, Play again from the edit-mode state. Whatever the run
  had accumulated is gone, which is what Stop always means.
