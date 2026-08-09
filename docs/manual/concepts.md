# Core concepts

Six words carry most of the manual. This page defines them once.

## Project

A `.rvproject` file naming an asset root, a start scene, and a simulation rate.
Everything an asset refers to is relative to that root, which is what lets a
project be copied, packaged and opened on another machine.

The engine can run without a project. It shows you that state rather than
inventing one.

## Scene

A container of entities, saved as a text file you can read and diff. Scenes are
**losslessly round-trippable**: loading one and saving it again produces the same
bytes, every time. That is not a nicety — play mode is implemented as
snapshot-then-restore, so pressing Stop is only as correct as saving is.

## Entity

A thing in a scene. An entity is an identity and nothing else: it has no
behaviour and no data of its own until components are added to it.

Every entity carries a **UUID** that survives saving, loading, duplication and
play mode. Names do not have to be unique; UUIDs are. When you need to remember
*which* entity across time, remember the UUID.

## Component

Data attached to an entity. A transform, a mesh, a rigid body, a light, an audio
source, a script.

Components are registered with a reflection layer, which is why adding one shows
up in the inspector and in the scene file without either being taught about it
individually.

### The transform is a hierarchy

`TransformComponent` holds a **local** position, rotation (radians) and scale,
relative to its parent. World transforms are derived by walking up the chain.
Parenting one entity to another moves it with the parent.

## Asset

A mesh, material, texture, prefab, audio clip or scene stored under the project's
asset root and referenced by an `AssetHandle` — a stable identifier, not a file
path. A `.meta` sidecar next to each asset records the handle, so moving or
renaming a file does not break the scenes that use it.

A **prefab** is a saved entity, hierarchy and all, that can be spawned at
runtime.

## Play mode

The editor has two states, and the difference matters more than it looks.

- **Editing.** Nothing steps. What you see is the scene as saved.
- **Playing.** The scene is snapshotted, then stepped: physics runs, scripts run,
  audio plays.

Pressing **Stop** restores the snapshot exactly. Nothing your game did during
play survives, which is what makes it safe to iterate.

> [!TRAP]
> Because Stop restores by *recreating* entities, any handle held across the
> boundary is dangling afterwards. Scripts are handed `Entity` values, and those
> are safe to use within a run; storing one in a static, or in anything that
> outlives play mode, is not. Store a `UUID` and look it up.

## The fixed step

Simulation runs at a fixed rate — 60 Hz by default, `--fixed-hz` to change it —
independently of how fast frames are drawn. A frame may run zero, one, or several
simulation steps depending on how long it took.

This is why script updates are **per step, not per frame**. A script that moves
something has to agree with the physics that will push it, and physics cannot be
correct at a variable rate. Everything in [Scripting](scripting/index.md) follows
from this one decision.

Rendering is still per frame, so the frame rate and the simulation rate are
genuinely different numbers. The editor's statistics panel shows both.
