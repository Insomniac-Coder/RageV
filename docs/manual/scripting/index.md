# Scripting

A script is a class attached to an entity that receives lifecycle and physics
callbacks while the scene is playing. It is how anything in RageV does something
rather than merely sitting there.

## Two languages, one surface

| | C++ | C# |
|---|---|---|
| State | **Available now** | **Available now** |
| Lives in | the project's `Source/`, built into a game module DLL | the project's `Scripts/`, built into an assembly |
| Base class | `ScriptableEntity` | `Script` |
| Iteration | **File > Build Scripts** — reloads live (not during Play) | **File > Build Scripts** — reloads live (not during Play) |
| Cost of a mistake | Crashes the editor | Throws, and the editor survives |

The C# API is a mirror of the C++ one — the same names, the same lifecycle, the
same rules — deliberately, so that this manual describes one engine rather than
two. Where the two genuinely differ, the difference is called out.

> [!NOTE]
> The native surface was finished and frozen *before* C# work began. Every
> change to it afterwards costs a binding update, a marshalling update and a
> class-library update, so it was worth designing once. If you are writing C++
> scripts today, what you learn transfers.

## The shape of a script

```cpp
class Spinner : public ScriptableEntity
{
public:
    void OnUpdate(Timestep dt) override
    {
        Rotate({ 0.0f, m_Speed * dt.GetSeconds(), 0.0f });
    }

private:
    float m_Speed = 1.2f;
};

RV_REGISTER_SCRIPT(Spinner);
```

That is a complete, working script. Attach it to an entity in the inspector,
press Play, and the entity turns.

## What a script can reach

Scripts get a deliberately narrow surface, stated in engine terms rather than in
the terms of the ECS underneath:

- **Its own entity** — components, transform, name, UUID.
- **Other entities** — found by name or UUID, spawned, destroyed, re-parented.
- **Physics** — forces, impulses, velocity, raycasts, and contact events.
- **Audio** — its own source, or a fire-and-forget one-shot.
- **Input** — by action name, never by key code.
- **Time** — the fixed step, and seconds since start.

What it deliberately does *not* get is a raw handle into the entity registry.
See the [trap about play mode](../concepts.md#play-mode) for why.

## Where to go next

- [Writing a script in C++](cpp.md) — the guide, with worked examples.
- [C++ API reference](cpp-reference.md) — every member, what it does, and what
  it costs.
- [Writing a script in C#](csharp.md) — the same engine from the other
  language, and where the two differ.
