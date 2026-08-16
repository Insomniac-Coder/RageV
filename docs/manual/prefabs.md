# Prefabs

An entity, saved as an asset, so it can be placed many times.

A prefab is a `.rvprefab` file holding an entity **and its whole subtree** —
components, children, and the values you set. Dragging one into a scene creates
a copy.

## Making one

Drag an entity from the Scene Hierarchy into the Content browser. The resulting
`.rvprefab` captures that entity and everything under it.

## Placing one

Drag it from the Content browser into the scene, or into the Hierarchy to
parent it under something.

Each placement is an **independent copy** with its own entity UUIDs. The copy
carries a `PrefabComponent` naming the asset it came from, which is how the
editor tells an instance apart from an entity built by hand.

## What an instance is, and is not

**Editing an instance edits that instance only.** Change its colour and no
other copy moves.

**Editing the prefab asset does not push changes to placed instances.** They
were copied when they were placed, and they stay as they were.

> [!NOTE]
> This is the honest current state rather than a design claim. Prefab
> *variants* and pushing edits back to the asset are the two things a mature
> prefab system has and this one does not — an instance is a copy with a note
> saying where it came from.

## Spawning from a script

Both languages can instantiate a prefab at runtime, which is the usual way
bullets, enemies and pickups arrive. The call takes a prefab handle and returns
the new entity, so you can position it and set its fields immediately.

See [Scripting](scripting/index.md).

> [!TRAP]
> Entities spawned during play are **discarded on Stop**, like every other
> change made while playing. That is snapshot semantics working correctly.

## Where to go next

- [Component reference](components.md#prefabcomponent) — the one field
- [Assets](assets.md) — where prefabs live and how handles work
- [Scripting](scripting/index.md) — spawning and destroying at runtime
