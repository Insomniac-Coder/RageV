# Engine notes

Research distilled into decisions for **this** engine. Not a textbook — every
section says what RageV does, what it should do, and why.

Written 2026-08-08, after Phase 1. Companion to [ROADMAP.md](ROADMAP.md).

---

## 1. The simulation loop — and why it has to change before physics

RageV currently runs one variable timestep for everything:

```cpp
Timestep ts = time - m_LastTime;
for (Layer* layer : m_LayerStack)
    layer->OnUpdate(ts);
```

That is fine for a renderer and **wrong the moment physics exists**. Integrator
behaviour depends on `dt`; a variable one means the same scene behaves
differently on a fast machine, on a slow one, and on the frame someone drags the
window. Stacked boxes settle at 300 fps and explode at 40.

The settled answer is [Fix Your Timestep](https://gafferongames.com/post/fix_your_timestep/):

```
frameTime = now - last
if frameTime > 0.25:  frameTime = 0.25     // clamp
accumulator += frameTime

while accumulator >= dt:                    // fixed steps
    previous = current
    integrate(current, dt)
    accumulator -= dt

alpha = accumulator / dt                    // render between the two
render(lerp(previous, current, alpha))
```

Three pieces, three distinct reasons:

- **The clamp** stops a stall — a breakpoint, a window drag, a shader
  recompile — from queueing hundreds of steps and locking the process solid.
  Without it a 3-second hitch becomes a spiral of death.
- **The accumulator** decouples simulation rate from frame rate. Physics always
  sees the same `dt`.
- **The interpolation** is not polish. Render the raw state and anything
  moving fast visibly stutters, because the screen refreshes at moments that
  fall *between* two discrete physics positions. The alpha is simply how far
  through the current step we are.

**Consequence for the roadmap:** the loop change is a prerequisite for physics,
not a companion to it. It moves to the front of Phase 2 as item 2.0. Doing it
after Jolt means writing the transform sync twice.

**Design note for RageV:** interpolation needs two transforms per simulated
entity. Rather than doubling every `TransformComponent`, only physics-driven
entities need it — the previous and current transforms live on the rigid-body
component, and the sync writes the interpolated value into the transform before
rendering. Entities nothing simulates keep one transform and cost nothing.

---

## 2. Play mode is a serialization feature

Confirmed by how Unity does it: play mode is *snapshot, run, restore*, and the
snapshot is serialization. Community tools that preserve play-mode edits work by
serializing the hierarchy and matching objects by a **stable id that does not
change between play states** — exactly what Phase 0.2 built.

So RageV needs nothing new here beyond splitting the update:

- `OnUpdateEditor` — no scripts, no physics. What runs today.
- `OnUpdateRuntime` — scripts, then physics, on the fixed step.

Press Play → serialize the scene to a string, switch mode. Press Stop →
deserialize it back. The round-trip test is what makes this safe, and it is
already asserting byte-identical output.

One trap worth naming: scripts holding raw `entt::entity` handles across a
stop/start will dangle, because restore recreates entities. The native script
API must hand out `Entity` values resolved through UUIDs, never stored handles.
The undo system already learned this lesson the hard way.

---

## 3. Jolt, specifically

Concrete points from the [Jolt architecture docs](https://github.com/jrouwe/JoltPhysics/blob/master/Docs/Architecture.md)
that are easy to get wrong and expensive to discover later:

- **Add bodies in batches.** `AddBodiesPrepare`/`AddBodiesFinalize`. Adding many
  one at a time produces a degenerate broadphase tree and *missed collisions* —
  not just slowness. If one-at-a-time is unavoidable, call
  `PhysicsSystem::OptimizeBroadPhase` afterwards. Loading a scene adds every
  body at once, so this matters on the first thing RageV will do.
- **Start with two broad-phase layers**, moving and non-moving. Each broad-phase
  layer is a separate tree with its own query and maintenance cost. Many object
  layers mapping onto few broad-phase layers is the intended shape.
- **Sensors must not share a layer with static bodies** or with other sensors,
  or the broadphase does work that is thrown away.
- Bodies are addressed by `BodyID`, not by pointer, and the id survives
  deactivation. That maps cleanly onto storing a `BodyID` in the component and
  the `Entity` UUID in Jolt's user data.

**Scope call:** Jolt is a big dependency with its own CMake. It is worth it — a
solo project has no business writing a solver, and Godot reached the same
conclusion for its 4.6 default. But it lands *after* the loop and play mode,
because both are prerequisites and both are cheap.

---

## 4. ECS: stay on EnTT, and know what it costs

EnTT is a **sparse-set** ECS. Component add/remove is O(1) and cheap;
iteration is good but not archetype-good, because each component type has its
own array and a multi-component view chases the smallest pool.

Archetype ECS (Unity DOTS, flecs) packs whole component combinations
contiguously and iterates faster, at the cost of expensive structural change —
adding a component moves the entity between archetypes.

For an editor-scale scene with frequent add/remove from the inspector, sparse
sets are the right trade. **Do not switch.** The place this would bite is
hundreds of thousands of entities iterating every frame, which is a problem
RageV does not have and should not create — the terrain experiment that made one
entity per cube face is exactly the shape to avoid.

---

## 5. Rendering: what to build, in what order, and what to skip

RageV is a **forward** renderer with a hard cap of 8 lights and no shadows.

### Clustered forward is the right next step, not deferred

Clustered forward divides the view frustum into 3D cells, bins lights into them
with a compute pass, and each fragment reads only the lights in its cell. It
scales to thousands of lights, and unlike deferred it **keeps working for
transparency and MSAA** — deferred needs a separate forward path for both, which
is two lighting implementations to keep in agreement.

Deferred's advantage is decoupling lighting cost from geometry complexity, which
matters at a scale RageV will not reach. The 8-light cap goes away either way;
clustered costs one compute pass and no G-buffer.

### Shadows: the recipe that avoids the two classic failures

From the [stable CSM](http://longforgottenblog.blogspot.com/2014/12/rendering-post-stable-cascaded-shadow-maps.html)
and [MJP's survey](https://therealmjp.github.io/posts/shadow-maps/):

- **Fit each cascade to a sphere** around the frustum split, not to the frustum
  corners. A sphere is rotation-invariant, so the projection does not change
  size as the camera turns — this is what stops shadow edges from crawling.
- **Snap the light projection to texel increments.** Without it, sub-texel
  camera movement makes every shadow edge shimmer.
- **Normal-offset bias, not just depth bias.** Push the sample along the
  surface normal, scaled by the angle to the light. Pure depth bias trades acne
  for peter-panning (shadows detaching from their caster) and there is no value
  that avoids both.
- Slope-scaled bias on top, since the RHI already exposes it.

### IBL: split-sum, and the LUT is the cheap part

Karis's split-sum approximation: pre-integrate the BRDF into a 2D LUT indexed by
(NdotV, roughness), and prefilter the environment into a cubemap whose mips
correspond to roughness. Both are generated once at load. The irradiance map for
the diffuse term is a separate, much smaller convolution.

The current flat ambient term is already shaped as its fallback, so this slots
in without touching the direct lighting path.

### Bindless: where two backends starts to cost

Descriptor indexing is core in Vulkan 1.2 and is how a modern renderer avoids
rebinding descriptor sets per draw; combined with buffer device addresses it
removes most per-draw CPU work.

**But OpenGL 4.5 has no equivalent** short of vendor extensions. RageV committed
to two backends, and this is the first feature where that commitment has a real
price: bindless would mean two materially different resource-binding designs,
not one interface with two implementations.

That is a decision to make deliberately rather than drift into. It does not need
making yet — the renderer is nowhere near CPU-bound on descriptor updates — but
when it does, the honest options are "GL stays on the slow path" or "drop GL".

### Skip

GPU-driven rendering (compute-generated draw commands, meshlets) is a real
technique with real wins at hundreds of thousands of objects. RageV should do
**CPU frustum culling and draw sorting** and stop there. The gap between "no
culling" and "frustum culling" is enormous; the gap between that and GPU-driven
is invisible at this scale.

---

## 6. Render graph: the concrete shape

Confirmed the earlier roadmap call, and [Maister's deep dive](https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive/)
gives the design:

- Each pass **declares** its inputs and outputs — attachments with format and
  size class, buffers with usage. Nothing is bound imperatively.
- Barriers are **derived**: reads produce invalidation barriers, writes produce
  flush barriers, read-modify-write produces both. Consecutive reads need a
  synthetic zero-access entry to guard write-after-read.
- Resources are **aliased by lifetime** — track the first and last pass that
  touches each, and reuse physical memory when ranges do not overlap. Anything
  with history (this frame plus last) must never alias.
- Passes get reordered to maximise overlap, and compatible passes merge.

The warning that matters most: *do not scatter barriers through the code*. Doing
sync just-in-time requires non-local knowledge of the whole frame, which nobody
keeps in their head — it ends as pipeline barriers everywhere, which is a
half-reimplementation of the driver heuristics Vulkan exists to avoid.

RageV has three barrier bugs on record already, all found only by
synchronization validation. That is the argument for the graph, and it is why
the graph goes in *before* shadows and post rather than after.

---

## 7. Input: actions, not keys

Both Unity's Input System and Unreal's Enhanced Input converge on the same
shape, and it is worth copying rather than inventing:

- **Actions** are named intents (`Jump`, `Fire`), not keys. Game code asks about
  the action.
- **Bindings** map devices to actions, many-to-one — keyboard and gamepad can
  drive the same action without the game knowing.
- **Axes** are the continuous form, composed from keys (`W`/`S` → `MoveForward`)
  or read from a stick directly.
- **Contexts** (Unreal's mapping contexts, Unity's action maps) swap whole sets
  of bindings when the game changes mode — menu versus gameplay versus vehicle.
- Rebinding at runtime falls out for free, and is otherwise a rewrite.

RageV currently has `Input::IsKeyPressed(RV_KEY_W)` and nothing else, which is
the layer *underneath* this, not a substitute for it.

---

## 8. What this changes

| Item | Before | After |
|---|---|---|
| Loop | variable dt everywhere | fixed-step accumulator + interpolation, **before physics** |
| Phase 2 order | play mode first | **loop first**, then play mode |
| Physics | "add Jolt" | batch body adds, 2 broad-phase layers, BodyID not pointers |
| Lights | 8-light cap accepted | clustered forward removes it in Phase 3 |
| Render graph | Phase 3.1 | confirmed, and now has a concrete design |
| Bindless | unexamined | named as the point where two backends costs something real |
| ECS | EnTT | confirmed, with the failure mode written down |
| GPU-driven | unstated | explicitly out of scope |
