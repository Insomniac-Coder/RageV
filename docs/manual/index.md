# The RageV developer manual

RageV is a Windows game engine with an editor, a standalone runtime, and a
packager that turns a project into a folder somebody else can run.

This manual is for people **making a game with it**. It describes the surfaces
you write against — scenes, components, assets, and scripts — and it is written
against the engine as it exists, not as it is planned. Where something is
missing, this manual says so rather than describing it in the future tense.

If you are working on the engine *itself*, the documents you want are
`docs/ARCHITECTURE.md`, `docs/ENGINE-NOTES.md` and `docs/HANDOFF.md` in the
repository. They are a different kind of document for a different reader.

## What the engine does today

| Area | State |
|---|---|
| Scenes and entities | Entity-component scenes with a transform hierarchy, stable UUIDs, and lossless save/load |
| Assets | Meshes, materials, textures and prefabs, imported from glTF, referenced by handle |
| Rendering | Physically based shading, image-based lighting, shadows for every light type, clustered forward lighting, instanced batching, skeletal animation, HDR post |
| Physics | Rigid bodies, colliders, triggers, raycasts, and contact events delivered to scripts |
| Audio | Positional and unpositioned playback, driven from scripts or from a component |
| Scripting | C++ today. C# is [in progress](scripting/index.md) |
| Shipping | A standalone runtime and a packager |

## What it does not do yet

Stated plainly, because finding out halfway through a project is worse than
finding out now.

- **No text rendering and no game UI.** The editor has an interface; your game
  cannot currently draw a score, a menu or a health bar.
- **No particle system.**
- **No animation state machine.** Skeletal meshes play clips; blending between
  them is a manual operation.
- **No navigation, no networking, no terrain tooling.**
- **Windows only.**

## How to read this

Read [Getting started](getting-started.md) first — it gets the editor open in
about five minutes. [Core concepts](concepts.md) is the vocabulary the rest of
the manual assumes. After that, [Scripting](scripting/index.md) is where the
actual work happens.

> [!NOTE]
> Press `/` anywhere in this manual to search it. Results are ranked by heading,
> so searching for a method name usually lands you on the exact section.
