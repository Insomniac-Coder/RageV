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
- `OnFixedUpdateRuntime` — scripts, then physics, on the fixed step.
- `OnUpdateRuntime` — the frame: interpolation, presentation, rendering.

> **Added 2026-08-11.** Scripts ended up on *both*, which the original note did
> not anticipate. `OnTick` is the fixed-step half and lives in
> `OnFixedUpdateRuntime`; `OnFrame` is the frame half and lives in
> `OnUpdateRuntime`, after the interpolation in §1 has been applied and the
> world transforms derived from it. The argument is §1's own: the engine
> already blends the last two simulation states so the world moves smoothly at
> any display rate, and a camera driven from the fixed step moves on one frame
> in four at 240 Hz *against* a world that moves on all four. Differential
> stutter reads worse than none. A script can also read the alpha itself, as
> `GetInterpolationAlpha`, for smoothing a value the engine cannot see.

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

### 3a. Contacts: the callback is not the hard part

Written after implementing 2.6. The plumbing — a `ContactListener`, a queue,
six virtuals on `ScriptableEntity` — is a morning. What decides whether a
script can *trust* what it is told is three properties of Jolt that are stated
in its docs and are easy to read past.

**The callbacks run on job threads with every body locked.** Jolt is explicit
that using a locking body interface there deadlocks. So the listener records
the raw fact and returns; every map lookup, every decision, every delivery
happens on the main thread after `Update` returns. This is not a performance
choice, it is the only correct shape.

**`OnContactRemoved` cannot read either body.** It gets a `SubShapeIDPair` and
nothing else, because one of the bodies may already have been destroyed.
Anything the removal needs — which entities, whether it was a trigger — has to
have been cached when the contact was *added*.

**Jolt withdraws a body's contacts the instant it falls asleep.** This is the
one that produces a wrong game rather than a crash. A box lands, settles, and
about a second later Jolt reports every one of its contacts removed — so a
naive implementation tells the script the box left the floor at exactly the
moment it looks most firmly on it. Any "am I grounded" check built on that is
wrong, intermittently, in a way that looks like a physics bug.

Neither body being awake is what distinguishes it from real separation: bodies
that genuinely separate are moving, and a body cannot fall asleep while it is.
Suppressing the exit and remembering the pair as dormant gives the semantics
Unity and Godot both have, where enter and exit describe touching rather than
simulation state.

**Reporting granularity is per sub-shape pair, not per body pair.** With convex
shapes those are the same thing, so it is invisible today — and stops being
invisible the moment compound or mesh colliders arrive. Counting sub-shape
pairs per body pair, and firing enter/exit on the transitions off and back to
zero, costs nothing now and means one touch stays one touch later.

**On ordering:** contacts arrive from several threads, so the order they land
in is the scheduler's rather than the scene's. Sorting before delivery is
cheap and is the difference between a simulation that replays and one that
nearly does.

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

### What was actually built, and why it is smaller

Written after 3.1. The barrier argument above **did not survive contact with
the code**, and it is worth recording why rather than quietly building to it.

This RHI already tracks image layout per texture: `EndRenderPass` puts a
target's textures into a shader-readable layout and `BeginRenderPass` puts them
back. Sampling a previous pass's output is already correct with the graph
knowing nothing about it. Deriving barriers in the graph would have meant a
second layout tracker over a working one -- two sources of truth about the same
state, which is worse than either alone.

The three barrier bugs were also not the class the graph prevents. Two were in
the swapchain path (a transition with no execution dependency on the acquire,
and one that assumed a single pass per frame) and one was a descriptor set
rewritten while bound. A pass-level dependency graph would have caught none of
them.

So the graph that exists does three things the frame genuinely needed:

- **Owns the intermediate targets**, pooled and resized with the output. Phase
  3 adds an HDR buffer, a bloom chain, a BRDF LUT and shadow maps; without an
  owner each of those hangs off whichever layer wanted it first.
- **Makes the frame a data structure** rather than a sequence of calls spread
  across files.
- **Refuses an invalid frame** at compile time, with a message. A pass sampling
  a target nothing wrote is otherwise a black screen, which is the single most
  expensive thing to diagnose in a renderer.

Aliasing and reordering are still absent, and now for a second reason: they are
optimisations for frames much larger than this one, and both need lifetime
analysis that is only correct if it is complete.

**The general lesson, which is the reusable part:** a design taken from an
article describes the problem the article's author had. Check that it is the
problem you have before building it. Here the honest version was smaller than
the plan, and saying so is cheaper than maintaining the difference.

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

## 7a. Audio: the null backend is the design

Written after implementing 2.5. miniaudio's `ma_engine` supplies decoding,
mixing, streaming, a node graph and distance attenuation, so almost nothing
about audio is a matter of algorithms. What is left is a lifetime problem and a
testability problem, and they have the same answer.

**A sound outlives the thing that started it.** A one-shot fired from a
collision belongs to no entity; the entity may be destroyed before the sound
finishes, and usually should not cut it off when it is. So a playing sound is
addressed by an opaque voice id rather than a pointer, and the engine owns the
lifetime. A component holds a voice id, never a sound.

That immediately produces three places a voice has to be released — the entity
is destroyed, the component is removed, the registry is cleared — which is one
too many to remember. An `on_destroy` signal covers all three and cannot be
forgotten at one of them. This is the same conclusion the script component
reached, for the same reason, which is a sign it is the right shape.

**The interesting decision is what happens with no output device.** The obvious
implementation makes every call a no-op and returns nothing. That means the
engine behaves differently on a machine with no sound card — and, worse, that
the audio code paths are untestable anywhere without one, so the difference
goes unnoticed until someone reports it.

Making the silent path allocate, track and retire voices exactly as the real
one does costs about ten lines and buys two things: behaviour that does not
depend on the hardware, and a suite that can run the *same* checks twice, once
with a device and once without. `--audio=off` exists to make the second path
reachable deliberately rather than only by accident on someone else's machine.

**On buses:** four fixed ones rather than arbitrary named groups. Every game
needs exactly this separation, and a fixed enum is something the inspector, the
serializer and a settings screen can present without first inventing a naming
scheme. Arbitrary buses can be added later; they cannot be taken away.

---

## 7b. The runtime is a test, and so is exiting

Written after 4.2. The roadmap called the standalone runtime a forcing
function. That undersold it.

Three defects turned up in the first hour of running it, none of which any test
in this project could have caught, because all three needed a program that
draws a scene to the swapchain and then exits. The editor does neither: its
scene goes to an offscreen target that a panel samples, and in months of
development no run of it had ever been *closed* rather than killed.

The general shape is worth naming, because it will recur. **A second consumer
of a subsystem finds the assumptions the first one embedded in it.** The
swapchain code assumed one render pass per frame. The UI pass assumed it owned
the backbuffer. `LayerStack` assumed -- silently, through a non-virtual
destructor -- that nothing ever needed a layer's derived state to be destroyed.
Each was true of the editor and only of the editor.

Two things follow for how this project verifies work:

**Exiting is part of the test.** A killed process runs no destructors, so every
teardown bug is invisible. `exit 0` now sits alongside "zero validation lines"
as a condition, and `--screenshot` exists partly so any application has a clean
exit to check.

**Pixels are part of the test.** Draw-call counts cannot distinguish a rendered
frame from one that was cleared immediately afterwards -- which is exactly what
shipped. `RHIDevice::RequestCapture` returns the frame about to be presented,
so what reached the screen can be measured rather than assumed.

---

## 7c. Two backends only pay off if you compare them

The RHI's whole claim is that Vulkan and OpenGL produce the same image. For two
roadmap phases that claim was checked one backend at a time: run each, confirm
no validation lines, confirm `exit 0`, look at a screenshot. Both passed. Both
were wrong.

Every fullscreen post pass reads one render target and writes another. The
comment in each said the backends' conventions cancel — Vulkan's framebuffer
origin is top-left and its textures are top-down, OpenGL's are both bottom-up.
That is true of a texture *drawn* by geometry, and false of one *sampled* by a
fullscreen triangle: a fragment at the top of the destination samples `v = 1`,
which is the source's last row, and the last row is the bottom on Vulkan and
the top on OpenGL. So Vulkan flips and OpenGL does not.

It survived because the number of passes hid it. With anti-aliasing on the
scene goes through tone mapping and then FXAA — two flips, which cancel — and
comes out the right way up. The bloom chain has an odd number, so bloom was
added to the frame mirrored about its middle. In a scene lit to a maximum of
about 2, nothing crossed the bloom threshold hard enough for that to be
visible. The moment metals reflected an HDR sky, every specular highlight also
appeared as a blob floating on the opposite side of the frame.

Three things follow, in increasing order of how much they matter:

1. **A convention that "cancels" deserves a test, not a comment.** The comment
   was written confidently and repeated in six shaders.
2. **Cross-backend comparison is a different check from per-backend
   correctness.** Each backend was internally consistent. The bug was that they
   disagreed, and nothing was looking for disagreement.
3. **A renderer bug can be dormant because the content is too dim.** The scene
   was the test, and the scene was not demanding enough. Adding a bright sky
   found a bug that had been in the build for a phase and a half — which is an
   argument for content that stresses the renderer, not only content that
   demonstrates it.

The same reasoning is why cube faces are converted on the CPU (§5) and why the
one place the backends' row order genuinely differs —
`RHICommandList::CopyToTextureLayer` — is a single function with the derivation
written above it.

---

## 7d. Text and game UI: the design, before the code

*Written 2026-08-11, before any of it was built, because the roadmap's Phase 6
says the design is the first task rather than the code — and because this is
the first system since the asset registry that four other systems have to agree
with.*

**Why it is the next thing.** Everything up to here draws a *scene*. Knockdown
is a complete, packaged, playable game that communicates through one light and
four sounds, because it cannot say "press F to reset", show a score, or put up
a title. That is the only remaining gap a *player* would notice; everything
else left on the roadmap is something a developer would notice.

### The font is baked by a tool, and the tool ships nothing

`tools/rvfont`, beside `rvdoc` and `scenetest`. It reads a `.ttf` and writes an
atlas image plus a metrics table. **Nothing it links enters the engine or a
shipped game** — the runtime loads a PNG and a table of numbers — which is what
makes the dependency question below almost free.

**Outlines from stb_truetype, the distance field from msdfgen's core.** Not
written from scratch: the two parts of MSDF that go subtly wrong are *edge
colouring* at corners and the *error-correction* pass, and "glyph corners are
slightly wrong at some sizes" is not a thing a test suite catches — it is a
thing somebody notices in a screenshot six months later. msdfgen is the
reference implementation and its **core has no dependencies**; FreeType is only
in its font-loading front end, which is the part stb_truetype already replaces.

**The numbers that decide whether this looks good**, and they are not
adjustable taste:

- **`screenPxRange` must never fall below 1, and below 2 the antialiasing
  fails** — colour bleeds across the whole glyph quad rather than resolving to
  an edge. It is `(quadPixelsOnScreen / atlasGlyphPixels) * pxRange`, so it
  depends on the atlas *and* on how large the text is drawn. This is the single
  most common way an MSDF integration looks broken.
- **Distance range 6 px in the atlas.** Higher is not safer; too large produces
  its own artifacts.
- **At least 32 px per em in the atlas, more for a thin face.** MSDF breaks
  where two edges of the same channel come within 2 field pixels, which is
  exactly what a thin stroke is.
- **Below roughly 12 px on screen the antialiasing reads as blur.** That is a
  property of the technique, not a bug to chase; a bitmap atlas beats it there.

**So the invariant is that the shader computes `screenPxRange` from the atlas
metadata and the real quad size**, and `rvfont` prints the smallest on-screen
size its atlas actually supports. A number the tool refuses to let you not
know, rather than a soft-looking build nobody can explain.

### One pipeline, two kinds of quad

A UI batch interleaves sprites and glyphs in z order — a label on a panel on a
bar. Two shaders means a batch break at every one of those transitions. So one
pipeline with a per-vertex flag: plain texture, or MSDF median. **A branch per
fragment costs nothing; a batch break per widget costs everything.**

This is why UI does not go through `Renderer2D`. That renderer is lit,
world-space, and built around a 32-slot texture array for scene quads. Sharing
it would mean bending a lit scene renderer around an unlit screen-space one.

### The canvas is entities, and it is deliberately not ImGui

ImGui is the *editor's* UI and is immediate-mode, which is right for tools and
wrong for a game: a game's UI is authored in a scene, serialized, driven by
scripts, skinned, and must run with **no editor present**. The editor's design
tokens are worth reading for the reasoning; none of that code is reusable here.

**It reuses the entity hierarchy** — `RelationshipComponent` is already
serialized, already drawn in the hierarchy panel, already works with prefabs
and with the reveal-on-select. A second parent/child system would be a second
thing to keep in step.

**It does not reuse `TransformComponent`.** Position, rotation and scale cannot
express "twenty pixels in from the top-right corner, at every resolution",
which is the entire problem UI layout exists to solve. So `UIRectComponent`
carries anchors, offsets and a pivot, with its own propagation pass driven by
the same parent links the transforms use.

The components:

| Component | Holds |
|---|---|
| `UICanvasComponent` | the root: reference resolution and scale mode |
| `UIRectComponent` | anchor min/max, offsets, pivot, sort order |
| `UIImageComponent` | texture handle, tint |
| `UITextComponent` | string, font handle, size, colour, alignment, wrapping |
| `UIButtonComponent` | normal/hover/pressed tints, and the event |

**Anchors are the Unity model on purpose.** It is the one that actually solves
resolution independence, and it is the one anybody arriving at this engine has
already learned. Inventing a simpler scheme here would save a week and cost
every author who has to discover that the simple scheme cannot pin a corner.

### Z order is explicit, because draw order is not a design

An explicit sort key on the rect, with hierarchy order as the tiebreak.
Painter's algorithm, no depth buffer. A UI whose layering depends on the order
somebody happened to create entities is a UI that rearranges itself when a
prefab is re-saved.

### Where the pass goes, and what that costs

**After tonemap and after FXAA**, writing the output target in LDR.

The consequences, stated rather than discovered later: UI is **not** tonemapped
and **not** bloomed, and FXAA never softens text. That is why an overlay canvas
stays crisp, it is what every engine does, and the price is that a game cannot
bloom its HUD. Named here so that when somebody wants a glowing health bar,
they find the reason rather than a bug.

### Input has exactly one rule that matters

Hit-test the topmost rect under the pointer, front to back, and then **say so**.
`UI::WantsPointer()` — because a UI that silently swallows a click is a game
that fires a weapon when you press the pause button. The action map stays what
it is for gameplay; this sits in front of it.

### A click reaches game code two ways, and both are wanted

*Decided 2026-08-12, reversing an earlier call to ship only the first.*

**Polling** — a script asks its own button `WasButtonClicked()`. One `if`, no
machinery, and the natural shape for a manager reading five buttons in one
place.

**Binding** — the button stores a target entity and a method name, and the
engine calls it. The Unity shape, and the one somebody arriving at this engine
looks for.

The first draft shipped only polling, on the argument that the second needed
method reflection C++ does not have and would introduce a name that no compiler
checks. Both halves of that are true and neither is a reason to skip it: the
registration is six lines of template, and the unchecked name is a *reporting*
problem, not a correctness one.

**So the trap is answered rather than avoided.** A binding that resolves to
nothing warns on **every click** — naming the button, the target and the method.
Once-per-button reads better and is worse: clicks two through ten then look
exactly like the click never registered, which is the harder bug to report.

Three things follow that are worth stating because they are not obvious:

- **`EntityRef` is a struct wrapping a UUID, not an alias for one.** Reflection
  deduces a field's kind from its member type, and `AssetHandle` is *already*
  `using AssetHandle = UUID` — so an alias would be indistinguishable from an
  asset, and every entity slot would come up with the content browser's drop
  target instead of the hierarchy's.
- **The languages are asymmetric here, and it cannot be helped.** C# finds
  bindable methods by reflection, so writing the method is the whole job; C++
  has none, so `ScriptRegistry::Method<&C::M>("Name")` has to name it from
  outside the class, exactly as `Field<>` does. Both guides say so rather than
  leaving somebody to discover it when their C++ handler is missing from the
  dropdown.
- **`void()` only.** Arguments would need the scene file to store them and the
  inspector to edit them, which is a small expression language nobody asked for;
  a return value would have nowhere to go. Enforced by having no other template
  specialisation to match, so a wrong signature fails at the registration line
  naming the method — rather than at runtime, as a button that does nothing.

**What the existing event system contributes: nothing, and that is not a
missed reuse.** `Events/Event.h` is the Hazel-lineage platform dispatcher.
`EventDispatcher` holds one `Event&` and type-switches on it — there is no
subscriber list to join — its `EventType` is a closed window/key/mouse enum, its
events are pumped outside the fixed step, and `Event` is an `RV_API` C++ class
that **no C# script can subscribe to**, which fails half the requirement before
any of the rest matters.

The real precedent was elsewhere: **`Scene::DeliverContact`**, which already
takes an engine-detected occurrence, finds the script instance on an entity, and
calls into it in both languages. `Scene::InvokeScriptMethod` is that shape with
a named method instead of a compile-time-known one.

**Built, and three things were decided while building it.**

**What blocks the pointer defaults to nothing.** Unity makes every graphic a
raycast target unless told otherwise, and the failure that produces is a score
label across the middle of the screen quietly eating the clicks aimed through
it — reported as *"I cannot shoot when the crosshair is over my score"*, which
points nowhere near the label. A HUD is overwhelmingly decoration, so
`UIRectComponent::BlocksPointer` is **off** by default and a `UIButtonComponent`
blocks regardless of it. That leaves exactly one case needing the checkbox — a
modal's backdrop — chosen by somebody already thinking about blocking input.

**A press is captured, so it can be cancelled.** The button a press started on
is held until the release, and the release only clicks if it lands on that same
button. Sliding off un-presses it; coming back re-presses it. Every desktop
toolkit does this and it is the difference between a button and a tripwire.
Capture is also why `WantsPointer` stays true for the whole of a press dragged
clear of the button — otherwise one gesture is delivered to the menu *and* the
world.

**A click is an edge with the same contract an action press has.** `Clicked` is
true for one simulation step and is consumed by `UI::EndFixedStep`, called at
the end of `Scene::OnFixedUpdateRuntime` exactly as `InputMap::EndFixedStep` is
called by the loop. A frame running three steps must not fire a button three
times; a frame running none must not swallow it. Both halves are checked.

The pointer is fed once a frame by whoever owns the layer, because only they can
map it: the runtime's layer *is* the window, and the editor's is an image inside
a docked panel that can be moved, resized and scaled — and there are two panels
showing it. Both spend it at the same point in the frame, before the scripts.

### Text in the world is in, and one decision is what makes it cheap

*Scope confirmed 2026-08-11: the phase covers screen-space UI **and**
world-space text. The rest below is deferred to the end of the phase.*

Two features were being confused under one name, and they cost very different
amounts:

- **World-space text** — a nameplate over a character, damage numbers, a
  number painted on a door. The atlas, the glyph quads and the shader are the
  same; what differs is a depth-tested pipeline into the HDR target (so
  geometry occludes it, and it goes through the tone curve), optional
  billboarding, and ordering with the other transparent content. Modest. **In.**
- **A world-space *canvas*** — an interactive panel on a wall that you point at
  and click. Rendering it is the easy half; *input* is the other half, and it
  means raycasting into the world, hitting the plane, converting to a point on
  it, and resolving what is in front. That is a second input path. **Deferred.**

**Built 2026-08-12, and the hedge paid.** `WorldTextComponent`, drawn by
`UI::DrawWorldText` through the *same* shader, the same atlas and the same
`UI::Build`. What it cost beyond that: one attribute widened from `vec2` to
`vec3`, a second pipeline differing only in depth state and target formats, and
`BillboardAxes`. The fragment stage was not touched.

Three things worth stating because they are choices rather than consequences:

- **Depth tested, depth *not* written.** Testing is what lets geometry occlude
  a nameplate, which is the point. Writing would have each glyph quad occlude
  the transparent things behind it — including the rest of its own string,
  wherever two quads overlap.
- **Drawn before the particles**, for the reason the grid is: a label is part of
  the scene a blended particle blends *against*. The other order paints a
  nameplate over the smoke standing in front of it.
- **Billboarding is the caller's policy, not the renderer's.** `DrawWorldText`
  takes the two axes the quads are laid out along; passing the camera's makes it
  face the viewer, passing the entity's leaves it in its own plane. `Upright` is
  the default because `Full` tips the text back when the camera looks down, and
  a row of tipped nameplates reads as a bug.

The camera's axes are **normalised** and the entity's are not — a scaled sign
should have bigger letters, a scaled camera rig should not resize every label in
the world.

**The decision that makes the first one nearly free is in the shader, and it is
made now.** `screenPxRange` is computed from **screen-space derivatives**, not
from a quad size known at layout time:

```glsl
vec2 unitRange     = vec2(pxRange) / vec2(textureSize(msdf, 0));
vec2 screenTexSize = vec2(1.0) / fwidth(vTexCoord);
float range        = max(0.5 * dot(unitRange, screenTexSize), 1.0);
```

That is transform-agnostic: it holds under perspective, at any distance, on a
rotated quad. Computing it CPU-side would work perfectly for a HUD and quietly
make world-space text impossible without rewriting the shader. The second half
of the same hedge: **turning a string into positioned glyph quads is a separate
step from canvas layout**, so a world-space label reuses it untouched.

(That shader takes an `fwidth`, so §5's derivative rules — written for the
grid — apply to it too.)

### Measuring a change you cannot see directly

*Learned building 6.11, 2026-08-12, at the cost of three tuned thresholds.*

The GPU alpha sort had an obvious-looking test: render one emitter on the CPU
and on the GPU, diff the images, and the difference is the ordering. It rests
on the two paths simulating the *same particles*, which everything about them
suggests -- one deterministic xorshift, one seed, one set of draws from it.

**They do not.** An identical burst scene rendered each way differs by 6.5 of
255 under *additive* blending, where order cannot matter at all. The two agree
statistically, not particle for particle, so the comparison was measuring the
difference between two plumes the whole time. A completely unsorted build
passed one of the thresholds tuned to accommodate it.

Two rules came out of it, both general:

- **A control that should read zero, and a threshold derived from it.** The
  additive comparison *was* the control here, and it was shouting -- 6.5 where
  the claim needed ~0. Tuning past a loud control is how a measurement becomes
  decoration.
- **Prefer an instrument that reads the thing rather than a consequence of
  it.** Dispatching the sort on a buffer built by hand and checking the output
  order is exact, deterministic, needs no camera, and catches an inverted
  comparison that the pixels waved through.

And one engine change fell out: **particles simulate on frame time**, so two
captures of one scene were never the same picture -- the same measurement swung
0.78 to 0.23 between runs of an identical build. `--frame-time=<seconds>` pins
the frame clock, and any screenshot comparison should use it.

### Deferred to the end of the phase

Each is a real feature with a real cost, and each is deliberately after the
parts a game cannot ship without: a **world-space canvas** (above), **rich text
markup** (a parser plus per-run styling through the whole layout path), **input
fields** (carets, selection, clipboard, IME — a project in itself), **layout
groups and scroll views**, **localisation**, and **complex script shaping /
RTL**, which is HarfBuzz and is not a weekend.

**One limit worth knowing rather than discovering.** The atlas is baked ahead
of time, so Latin, Cyrillic and Greek are free, and **CJK works only if the
glyphs actually used are subset** — all of it at once is an enormous texture.
`rvfont` takes a character set for exactly this reason.

**And one that had to be discovered.** A distance field measures distance to a
glyph's *outline*, which only means anything if the outline is the boundary of
the shape. In a great many fonts it is not: **variable fonts build letters from
overlapping, often self-intersecting contours**, because overlap removal cannot
be done once for shapes that change with an axis. RobotoFlex draws its `e` as a
*single* contour where Arial, Segoe UI and Open Sans all use two — the counter
is formed by the path crossing itself.

A rasteriser does not care; non-zero winding fills a self-crossing path
correctly, which is why nothing else in a font pipeline notices. A distance
field faithfully reports the edge it finds inside the glyph, and the letter
bakes with a bite taken out of the junction. It is invisible at small sizes and
unmissable on a title.

msdfgen's own answer is an optional **Skia** dependency for geometry
preprocessing, which is not vendored here. Instead
`tools/scripts/prepare_font.py` resolves the geometry with fontTools and
skia-pathops — instantiate the variable font, union the contours, write a
static copy — and reports which letters were affected. It preserves kerning,
because it rewrites outlines and not layout tables: Roboto keeps all 3284 of
its pairs. **Run it before rvfont on any font you did not bake before.**

### The order to build it, and why the last step is the point

1. `rvfont` and the atlas format, checked by rendering the atlas itself.
2. The UI renderer and its shader — no components yet, one hardcoded string.
3. Rect layout and propagation. **Anchors are pure arithmetic, so this is
   testable on the CPU with no GPU at all**, which is where most of the real
   bugs will be.
4. Components, serialization, inspector.
5. Hit-testing and the button.
6. **World-space text** — the same glyph builder, a depth-tested pipeline, and
   billboarding.
7. **Knockdown gets a title, a score and "press F to reset".**

Step 6 is the acceptance test rather than a victory lap. The roadmap's own
recommendation is that an engine improved from a game grows the features that
were in the way, and this whole phase exists because a game ran into their
absence.

---

## 7e. Per-object reflection probes (7.7)

*Designed 2026-08-12, before writing any of it, because the two viable shapes
differ in what they cost forever rather than in how long they take to type.*

### What is wrong today

`Scene::ResolveEnvironment` picks **one** probe for the whole pass, by distance
from the *camera*, and hands it to `Renderer3D::BeginScene`. It lands in the
scene descriptor set (set 0, binding 1 for the radiance cube and 5 for
irradiance), which is bound once per pipeline change and never per draw.

With one probe that is exactly right. With several it means every reflective
surface in the scene reflects whichever probe is nearest the viewer: walk from
the hallway into the kitchen and the hallway mirror starts showing the kitchen.

**This is a correctness problem, not a performance one**, and an earlier note
here said otherwise on the strength of a benchmark that measured a *realtime*
probe in a scene that had opted into one. A baked probe -- the default -- costs
0.066 ms CPU and 0.001 ms GPU. See HANDOFF §8.

### The two shapes, and why the cheap one wins here

**A. One scene set per distinct probe, with the probe in the sort key.**

The scene set already exists per frame slot and is cheap to build. Build one per
distinct probe in view instead, differing only in bindings 1 and 5; add the
probe to `PendingDraw` and to the sort key so runs do not interleave; bind the
matching set per run.

- Costs one extra descriptor set per probe *in view*, and at most one extra
  bind per run.
- **Batching survives.** Objects near each other share a probe, which is the
  same spatial coherence the mesh and material keys already exploit, so the run
  count grows with the number of probes rather than with the number of objects.
- Needs no RHI feature that does not exist and no shader change at all.

**B. A cube array indexed per instance.**

The textbook answer: every probe in one array, an index per instance, one bind
for the pass.

- Needs cube *array* support in the RHI -- creation, views, upload -- on both
  backends, and a shader change to sample an array.
- Forces every probe to share a resolution, because an array has one. The
  component exposes `Resolution` per probe today, so that is a feature removed
  to enable a feature.
- Wins only when the number of probes is large enough that A's extra binds
  matter, and this renderer draws 60 batches for 1000 meshes.

**Decision: B**, taken by the user on 2026-08-12 against my recommendation of A.

I argued for A on cost, and the argument does not survive contact with what the
engine is for. A is one scene descriptor set per probe *in view* plus a sort key
that fragments runs -- both of which get worse exactly as scenes get bigger,
which is the direction this engine is going. B costs a feature in the RHI once
and is then free forever: one bind for the pass, an index per instance, and a
run that never splits because two objects wanted different reflections.

The resolution objection was the real one, and it has an answer better than
either option I first wrote down: **group probes into one array per resolution.**
A scene using 128 everywhere gets one array and one bind, which is the common
case; a scene mixing 128 and 512 gets two.

### What shipped, and the one place the design above was wrong

B, in `ProbeArray` plus two new entry points on `EnvironmentIBL`. Everything in
the ordered list below was built as written except item 0, which is a correction
to the paragraph immediately above it.

**One array per resolution does not work, and the reason is the whole point of
B.** Two arrays means two bindings, and an object in the second array would have
to select *which array* as well as which slice -- which is a per-draw decision
again, and therefore the sort key again, and therefore the exact fragmentation B
exists to avoid. The index-per-instance shape only holds if there is exactly one
array.

So the array has **one face size: the largest `Resolution` any probe in the
scene asks for**. A smaller probe is resampled up into its slice on the way in.
That costs it nothing real -- prefiltering is a resampling operation already,
and its capture was going to be convolved to a lobe regardless -- and the common
case, where every probe agrees, is exact. The per-probe field survives and now
means what it says: how detailed the *capture* is.

That mistake is worth keeping written down because it is the same mistake in a
new place. "One array per resolution" reads like a compromise that keeps
everything, and it quietly reintroduces the per-draw decision that made shape A
lose.

### What B needs, in order

1. **Cube arrays in the RHI.** Creation, view, and upload, on Vulkan and
   OpenGL. This is the only genuinely new capability, and it is the reason the
   cheap option was tempting. *It turned out to be nearly free: `TextureCubeArray`
   was already in the enum and both backends already switched on it, and
   `CopyToTextureLayer` was already generic in layer and mip. Four real gaps:
   OpenGL allocated a cube array's storage from `Layers` rather than from six
   layers per cube, both backends wrote the **source's** rectangle into the
   destination (fine while nothing resampled, silent corruption the moment
   something did), a cube-array binding needs a cube-array stand-in --
   `TextureLoader::BlackCube` is a different descriptor type -- and Vulkan
   **never enabled the `imageCubeArray` device feature**, which both the SPIR-V
   `SampledCubeArray` capability and a `VK_IMAGE_VIEW_TYPE_CUBE_ARRAY` view
   require.*

   *That last one is the one worth remembering, because of how it presented:
   this driver created the view and ran the shader anyway. Every picture in this
   section was correct while the whole feature was undefined behaviour, and the
   only thing that said so was a validation line. It is now rejected at device
   selection rather than merely enabled, because there is no fallback -- a
   device without it cannot create the lit pipeline at all, and a clear failure
   at startup beats a wrong picture later.*
2. **The sky occupies slot 0 of every array.** "No probe within influence" then
   costs no branch and no sentinel -- it is an index like any other, and the
   shader samples the array the same way for every surface. A `-1` meaning sky
   would put a conditional in the hot path for the most common case.
3. **An index per instance**, alongside the model matrix that is already there.
   *It went in `InstanceData.Skin.y`, which is why that field is now called
   `Indices`: x is the bone offset, y is the probe. A vec4 with one lane used
   and a name describing that lane is how a struct starts lying.*
4. **Irradiance has the same shape and the same problem.** *Decided: a second
   array, indexed identically. Doing it needed a GPU convolution that did not
   exist -- `IrradianceFromCube` is a CPU routine that runs at asset load and
   takes 200 ms, which is fine for a cubemap off disk and impossible for a probe
   whose capture never touches the CPU. So probes had **no** diffuse
   contribution at all before this, and a metal object beside one reflected the
   probe while the diffuse half of the same surface was still lit by the sky.
   `irradiance.rvshader` is the fix, deliberately normalised to match the CPU
   routine exactly -- the cosine-weighted average, not the integral, which under
   cosine-importance sampling is the plain mean of the samples.*
5. **`samplerCubeArray` in the PBR shader**, and in the skinned variant.

### The trap A had, and B does not

A needed the probe *sorted on* rather than merely compared, or two objects
wanting different probes would land in one instanced draw. B has no such rule:
the index rides the instance, so a run can hold objects using any mix of probes.
That is the clearest statement of why B is the better shape, and it is worth
keeping because it is the kind of constraint that is invisible until it is
violated.

### How it is verified, and the one property no test covers

Two instruments, because neither is sufficient alone.

`CheckProbeSelection` and `CheckProbeArraySize` in scenetest ask which slot a
point selects, which is the actual output of the feature and is checkable
without a picture. Mutations confirmed: selection ignoring `Influence` (2
failures), selection against a fixed point rather than the object -- the shape
this replaced -- (4), sizing the array from the first probe instead of the
largest (2).

That last mutation **survived two earlier versions of the check**, and the
reason is worth keeping. With two probes, "the largest" and "whichever one the
registry hands back first" are the same answer whenever the view happens to
visit the larger one first -- and a view's iteration order is not something a
test gets to choose. The check passed for the wrong reason and reported nothing.
`CheckProbeArraySize` now uses three probes with the largest in the middle,
where neither end of the pool is the answer.

`SampleProject/assets/scenes/probes2.rage` is the visual half: two emissive
rooms, red and green, a mirror sphere in each, and **the camera placed nearer
the red one**. Per object, the left mirror is red and the right is green. Under
the old camera-based selection both go *sky* -- the camera is outside both
influence radii, so nothing in the scene gets a probe however close an object is
to one, which is a sharper statement of the old bug than the hallway-and-kitchen
one above.

The same scene carries **three matte spheres**, which is what verifies the
diffuse half: one in each room and one outside both influences. The scene has no
lights at all, so ambient is the only illumination and the tint has nowhere else
to come from. Red room reads pink, green room reads green, the third stays
neutral. A probe irradiance that silently fell back to the sky would leave all
three neutral.

`irradiance_uniform.rage` checks the convolution's *normalisation*, which is the
one thing a two-colour picture cannot: a white matte sphere under a sky that is
the same colour in every direction must be **the same brightness as the sky
behind it**, because the cosine-weighted average of a constant is that constant.
It reads 203 against 205, a ratio of 0.990, identically on both backends -- the
1% being the Fresnel split between the diffuse and specular terms.

That is worth more than the baseline comparison it replaced. The obvious check
was "render the old build and the new one and diff", which needs a second build
of reverted code and answers only "did this change". The uniform sky has a
**known-correct answer**, so it answers "is this right" -- and a convolution
returning the integral rather than the average would read 3.14 (a sphere blown
to white) rather than merely different.

And the process lesson, which cost a near-miss: **grep for `[Vulkan]`, not for
your own guesses.** I checked those runs for `error` and for `FAIL` and found
neither, because the validation layer says neither -- it says
`vkCreateImageView(): ... imageCubeArray feature is not enabled`. HANDOFF §2
already says every `[Vulkan]` line so far has been a real defect; this is the
second time that sentence has paid for itself, and the first time it nearly did
not because nobody read it.

**Not covered:** whether a slot's *contents* are refreshed when a realtime probe
re-captures. `SetProbe` compares the source pointer and the capture generation
to decide; a mutation that returns early for any filled slot is invisible to
every check here, because catching it needs to read the texture and the RHI has
no readback. The generation counter exists precisely because the pointer alone
cannot see a realtime probe re-capturing into a texture it already owned. If
that path breaks, the symptom is a realtime probe that freezes on its first
capture.

### Two things to get right, both of which look like bugs when they are wrong

**Selection is per object and must not be per *draw call*.** Instanced draws
share one instance buffer and one bind; if two objects in the same instance run
wanted different probes, the run has to split. That is what putting the probe in
the sort key does, and it is why the key must be sorted on rather than merely
compared.

**The influence radius already exists and already means this.**
`ReflectionProbeComponent::Influence` is documented as "how far from this probe
its capture is still a reasonable answer". Per-camera selection quietly reads it
against the camera; per-object selection reads it against the object, which is
what it always said. Falling back to the sky outside every probe's influence is
the existing behaviour and should stay.

### How it gets verified

Not by a screenshot of one probe, which looks identical either way. Two probes
capturing visibly different surroundings, a reflective object beside each, and a
camera positioned nearer to the *wrong* one -- so the current code shows both
objects reflecting the same thing and the fixed code does not. That scene is the
test, and it is worth generating rather than hand-authoring so the two probes
are provably different.

---

## 7f. Materials as assets (7.3), and the line that was already drawn

A material is a `Ref<Material>` living inside `MeshComponent`, written into the
scene file as a nested map of five scalars. Two consequences, and the second is
the serious one.

**Nothing can share a material.** Every entity deserialises its own `Material`
object, so "make these forty crates the same" is forty inspectors.

**And texture maps do not survive being saved.** `Material` has all five --
base colour, normal, metallic-roughness, occlusion, emissive -- with `MapFlags`
and the sampling code in `pbr_fragment.glsl`. `AssetManager::InstantiateModel`
fills them from a glTF import, so an imported model looks right. Then
`SerializeExtra` writes BaseColor, Emissive, Metallic, Roughness and Occlusion,
and **nothing else**. Save the scene, reopen it, and the textures are gone.
`NormalScale` goes with them. This is not a missing feature; it is a finished
feature with no way to store it, which is worse, because it works exactly once.

### Where to cut, which is not a new decision

The tempting shape is "a material is an asset, full stop". It is wrong here, and
the scene in the repository says why: `demo.rage` uses **four** colours across
**ten distinct roughness values** -- a PBR roughness sweep, which is what a demo
scene is for. Pure material assets turn that into thirteen `.rmat` files
describing what a person would call "blue plastic, varying roughness".

The right cut is already in the renderer, in `Material::GetBatchKey`:

- **The descriptor set** holds the five maps and the sampler. Two materials with
  the same key bind identically, so their objects can be one draw. *That* is
  what has to be shared, and sharing it is the entire point of the feature.
- **The instance stream** holds the scalars -- `BaseColor`, `EmissiveColor` and
  `Surface` are already per instance in `InstanceData`, precisely so a thousand
  cubes differing only in colour stay one draw.

So: **the asset is the maps; the overrides are the scalars.** Per-entity scalar
overrides are not a concession to convenience, they are free by construction --
they ride in a stream that is per entity already and cannot split a batch. A
design that made colour shared would be *more* work at runtime, not less.

### What that buys for free

Every legacy inline material is scalars and nothing else. Scalars are exactly
what an override holds. So an old scene's inline block converts **losslessly**
into per-entity overrides with no material handle at all -- no migration script,
no `.rmat` files, no scene edits, and `demo.rage` renders identically before and
after. Backward compatibility falls out of cutting in the right place rather
than being bolted on beside it.

### The pieces

1. `.rmat`, YAML beside `.rcurve`, holding the scalars **and five texture
   handles**. `MapFlags` is *derived* from which handles resolve and never
   stored -- a stored copy can disagree with the maps, and the flags are what
   the shader branches on.
2. `MaterialSerializer`, and `AssetType::Material` wired to the extension. The
   enum entry has existed since phase 1 with nothing producing one.
3. `Assets::Manager::GetMaterial`, cached by handle like every other type,
   resolving texture handles through `GetTexture`.
4. `MeshComponent`: `AssetHandle Material` plus the overrides. The
   `SerializeExtra`/`DeserializeExtra` hook goes away -- its own comment has
   said "this hook goes away with them" since phase 1.
5. Inspector: an asset slot, and an override row per scalar.
6. **glTF import writes `.rmat` files.** This is the part that fixes the defect
   rather than the inconvenience: an imported material becomes a real asset, so
   its textures are still there next time the scene opens.

### The bug this shipped with for an hour, and the trap behind it

`MaterialDesc`'s five handles were left default-constructed, and **a
default-constructed `UUID` is random, never zero** -- which is right for an
identity and catastrophic for a *reference*. Every material claimed all five
maps and wrote five invented handles to its file. It rendered perfectly, because
an unresolvable handle clears the slot on load, so the only thing wrong was the
data on disk -- until one of those random numbers happened to name a real asset.

Every other asset field in `Components.h` spells `AssetHandle::Invalid()` out.
The two new ones did not, and that is the whole bug. It is the same shape as
`EntityRef` needing to be its own type rather than an alias for `UUID`: **a
field whose zero value means "none" cannot use a type whose default is
"something new".**

What found it was reading the generated `.rmat`, not a test. The test that now
holds it is a *negative* one -- `!desc.OcclusionMap.IsValid()`, for a model that
has no occlusion map. Asserting what a thing does not have is the half of a
contract that gets skipped, and it is the half that catches a wrong default.

### What to verify, and the check that would fail

Sharing is easy to check and easy to fake. The one that matters:
**import a textured model, save the scene, reopen it, and compare the pixels.**
That is the case that is broken today and the only one that proves the asset is
carrying the maps rather than the import path being re-run.

And a batching check, because this is where it would silently regress: two
entities with the same material asset and *different* colour overrides must
still be **one draw**. If overrides ever reach the descriptor set instead of the
instance stream, that number doubles and nothing else changes.

---

## 7g. "Is the PBR even working?", and the instrument that answers it

The demo scene read as un-lit texture paste, and the accusation deserved a
better answer than a rendered opinion. The method that settled it, kept here
because the question will come back:

**`ibl_check.rage` is a scene with no analytic light at all** -- ambient zero,
no sun -- holding a dark glossy dielectric sphere and a white rough one.
Environment specular is then the *only* illumination, so it cannot hide behind
diffuse. Under the gradient sky the glossy sphere measured centre (32,34,46)
against grazing rim (203,196,199) -- the fresnel curve from 4% to full sky
brightness -- while the rough sphere read 181 centre, 182 rim: flat, as
roughness 0.95 should be. Under the 512px HDR sky the reflected horizon line is
sharp across the glossy sphere. The physics works.

What made the demo look broken was three scene facts and one regression:

1. **The gradient sky is a 32px featureless gradient.** A mirror of it is soft
   mush at any roughness, because there is nothing in it to mirror. Judging
   reflection sharpness against a gradient sky judges nothing.
2. **A 3.4-intensity sun pushed every bright surface into the ACES shoulder**,
   where an added 4% reflection compresses to ~1/255 -- measured by ablating
   dielectric F0 to zero and diffing: mean 1.2/255 on the white spheres.
3. **White-on-white cannot show fresnel.** 4% of the sky added to a bright
   white diffuse is invisible by arithmetic. The demo now keeps one dark glossy
   dielectric (the smooth sphere), which is the classic showcase for a reason.
4. **Real 7.7 regression, now fixed: reflections were capped at the probe
   array's face size.** The array defaulted to 128, so a 512 HDR sky's
   reflections lost three-quarters of their resolution relative to pre-7.7.
   `Scene::ProbeFaceSize` now counts the sky as a source, and the slot count is
   dynamic -- 1 + the scene's probe count rather than sixteen always -- because
   sixteen fixed slots at 512 would have been a quarter gigabyte of cube array.
   A one-probe scene under a 512 sky is 2 slots, ~33 MB.

**Resolved: backend parity of the derivative tangent frame.** VK and GL
differed by mean 14.7/255 on normal-mapped, parallaxed surfaces, and the
suspect was right: under Vulkan's negative-height viewport `dFdy` is the
negative of GL's, both terms of T and of B in the screen-derivative
construction flip, and the frame comes out **rotated 180 degrees about N** on
exactly one backend -- bumps lit from the wrong side, parallax marching the
wrong way. Latent since phase 3 (`ApplyNormalMap` was the same construction);
invisible until 2026-08-13 because no *compared* scene carried a normal map.

Three things worth keeping from the resolution:

1. **The planned fix could not have worked, and algebra said so before any
   render.** The plan was handedness correction, `if (dot(cross(T, B), N) < 0)
   B = -B;`. But negating both T and B is a *rotation*, and `cross(T, B)` does
   not move under it -- the handedness test reads identically on both
   backends. The discriminant that does detect the flip is the screen-space
   orientation `det(dp1, dp2, N) = dot(dp1, cross(dp2, N))`: positive under
   GL's derivative convention, negative under Vulkan's flipped one (and on
   backfaces, where the flip is also correct). `TangentFrame` now divides that
   sign out, which makes the frame a property of the surface rather than of
   the rasterizer. UV mirroring is untouched -- its sign lives in the UV
   determinant, which the construction already handles.
2. **The verdict came from a stated answer, not a backend diff.**
   `tools/scripts/make_parity_fixture.py` writes `parity_frame.rage`: a plane
   whose normal map tilts 45 degrees toward +u (= world +X, by the Plane
   primitive's construction) beside a flat control, lit from +X. Truth: tilt
   plane brighter than control (N.L 0.97 vs 0.5); a rotated frame gives N.L =
   0 and near-black. `check_tangent_frame.py` measures a render and answers
   CORRECT or FLIPPED. Before the fix: GL CORRECT, VK FLIPPED. After: both
   CORRECT, the two backends **bit-identical** on the fixture, and GL
   bit-identical to its pre-fix self -- the fix is provably a no-op on the
   backend that was already right. One instrument calibration lesson: at sun
   intensity 3 both planes sat on the ACES shoulder and the 1.9x linear gap
   compressed to five percent; the fixture uses a low dim light (30 degrees,
   1.5) so the gap survives into the pixels.
3. **The residual after the fix is filtering, and it was decomposed, not
   waved away.** material_closeup dropped 14.7 -> 1.28/255. Ablating
   HeightScale to zero: 0.79; ablating shadows as well: 0.79 (shadows
   innocent). The heatmap shows full-width horizontal zero-diff bands on the
   receding ground -- trilinear LOD fractions crossing integer levels, where
   drivers round differently at minification. Magnification-dominant scenes
   (textured.rage) diff at 6 subpixels in 1.5M. So: ~13.4/255 was the frame,
   ~0.8 is driver LOD rounding, ~0.5 more comes from parallax-displaced UVs
   feeding implicit-derivative fetches. Nothing directional remains.

Two process notes. The `Specular` F0 rework was the obvious suspect (it touched
exactly dielectric-vs-metal, and metals still worked); a pixel-identical diff
against the old constant exonerated it in one render. And a regex that edits a
scene file by matching `Tag:` then skipping "some lines" matched across an
entity boundary and blackened the pedestal instead of the sphere -- scene edits
want anchored, asserted matches or the editor, not patterns with `(?:.*?
)*?`
in the middle.

---

## 7h. The pak and the VFS (7.1): design first

ROADMAP said it plainly: packing without a virtual file system is worthless,
because every asset path in the engine goes through `std::filesystem`. So the
VFS is the feature and the archive is its payload.

**The design in one sentence: a mount shadows a directory.** Every reader in
the engine builds absolute paths through `Project::AssetPath`, and rewriting
that convention would touch every call site twice -- once to thread a new path
type through, once to fix what the first pass broke. Instead the VFS answers
*the same paths the filesystem would*: mounting `content.pak` over
`<install>/content` means a request for `<install>/content/scenes/menu.rage`
is answered from the archive, and a request nothing shadows falls through to
the real filesystem untouched. A development run mounts nothing and costs
nothing; a shipped run mounts one pak; the call sites cannot tell and do not
care. Precedence when both exist: **the pak wins, loose files are fallthrough
for entries the pak lacks** -- a stale loose file silently overriding shipped
content is the harder bug to see, and additive patch paks can layer by mount
order later.

**The API is deliberately small.** `Mount`/`Unmount`, `ReadBytes`, `ReadText`,
`Exists`, `Enumerate` (recursive, files only, merged across pak and loose),
`Origin` (pak / loose / missing -- the registry needs to know), and
`OpenStream` for the one consumer that cannot take a byte blob: audio, which
reads a file in pieces over the sound's lifetime from its own thread. Streams
are why the reader holds no shared mutable state after mounting -- the entry
table is immutable and every open stream owns its own handle to the pak file,
so thread-safety is by construction rather than by mutex.

**The format (`.rvpak`, magic `RVPK`, version 1)** is a header, the raw entry
blobs, then a table: path hash (FNV-1a of the normalized relative path),
offset, size, flags, and a string table holding the paths themselves so hash
collisions are checked rather than trusted. Paths are normalized to lowercase
with forward slashes on both store and lookup, because the loose filesystem
this replaces was case-insensitive and a pak that suddenly is not would break
scenes that worked for months. **No compression in version 1, and that is a
decision, not an omission**: the heavy payloads are PNGs, which are already
compressed, and cooking (7.2) will replace those bytes wholesale -- choosing a
codec now optimizes bytes that are scheduled to disappear. The per-entry flags
field is where a method goes when one is worth having.

**What routes through it:** everything under the project's asset root.
Textures (stb's `_from_memory` variants), models (cgltf's file callbacks,
which also cover the .bin a .gltf references), every YAML serializer (scene,
material, curve, font -- they all read whole files), the asset registry's scan
(via `Enumerate`), and audio (a `ma_vfs_callbacks` bridge into `OpenStream`;
the vendored miniaudio takes it via `ma_engine_config.pResourceManagerVFS`).

**What deliberately does not:** engine assets -- shaders and fonts stay a
loose tree beside the executable, because shader iteration in development is
precious, the tree is small, and the runtime needs it before any project
opens. The `.rvproject`, `ragev.ini`, the game module DLL and the managed/
assemblies are real files by nature: the OS and the CLR load them by path.
The editor never mounts a pak at all -- it edits loose projects, and its
writes (scenes, .rmat, .meta) stay ordinary file writes.

**The registry over a read-only mount trusts the pak.** Hashing every entry
at boot would read the whole archive to answer a question that cannot come up
-- a pak's content cannot have changed since it was written. Entries whose
origin is a pak keep their stored `SourceHash`, are never re-hashed, and never
get their sidecar rewritten; a pak entry missing its `.meta` mints a handle in
memory with a warning, because the packager's job is to make that impossible.

**Packaging writes the pak.** `PackageProject` walks the asset root into
`content.pak` instead of copying the tree (`--loose` keeps the old behaviour
as a debugging escape hatch), and `Project::Load` mounts `content.pak` over
the asset root whenever the file exists beside the project. Nothing else in
the shipped layout changes: exe, `RageV.dll`, module DLLs, `managed/`, engine
`assets/`, one archive where `content/` used to be.

**Verification:** pak round-trip byte-identical through the writer and
reader; normalization (mixed case, backslashes) resolves; the precedence rule;
enumeration merging; a scene loaded from a pak; registry handles identical
loose-vs-pak; and the end-to-end that matters -- package Knockdown, run the
folder standalone on both backends, and the screenshots must be
pixel-identical to the same game packaged loose.

**Built, and the E2E earned its keep (2026-08-13).** The first pak run
differed from the loose run by 9733 subpixels -- deterministically, the same
count on both backends -- and the diff heatmap was *legible*: it spelled out
the HUD text. One `std::filesystem::exists` had not gone through the VFS (the
font's atlas, AssetManager), so the packaged game silently fell back to the
default font. The control that made the verdict trustworthy: two runs of the
same loose package differ by 12 subpixels (particle noise under a pinned
frame clock), so 9733 was signal. After the fix: 12 on both backends -- the
noise floor exactly. The lesson generalises: **when a subsystem grows a
routing layer, grep for every direct existence check, not just every read** --
a read that misses fails loudly, an existence check that misses quietly picks
the fallback path. Suite: 1197 checks, both backends, Release included.

---

## 7i. Asset cooking (7.2): design first

The measurement that justifies the feature: ~6 seconds of a
material_closeup boot is the 4K texture pipeline -- PNG decode, upload,
GPU mip generation -- and none of it is block-compressed, so each 4K
RGBA8 map with its chain is ~89 MB of VRAM and the closeup's ten maps
are roughly a gigabyte. Cooking removes the decode and, more
importantly, puts BCn on the GPU: 4-8x less VRAM and bandwidth.

**Cooking is a content transform inside the pak build, invisible to
every reference.** Cooked bytes replace source bytes *under the same
path*: the pak entry `materials/soil_color.png` simply holds `.rvtex`
bytes instead of PNG bytes, and loaders sniff the magic before handing
anything to stb or cgltf. Nothing else can tell: `.meta` handles,
scenes and materials keep the names they had, the editor and every
development run keep loading source files, and `rvpack --raw` ships
source bytes for debugging. No reference rewriting exists because no
references change.

**`.rvtex` (RVTX v1):** width, height, mip count, pixel format --
RGBA8, BC1, BC3, BC4, BC5 -- and per-mip payload blobs. Mips are built
at cook time on the CPU (box filter; a normal map's mips are
renormalized per texel, because averaging unit vectors shortens them).
A cooked texture is never `GenerateMips`ed at load. sRGB stays a
*load-time view decision*, as it already is for PNGs -- the same bytes
upload as UNORM or SRGB depending on which slot asks -- so the cooker
does not need to know what a texture is for.

**The encode is chosen by name convention, and only alpha by content:**
`*_normal*` cooks to BC5 (xy only); the data-map suffixes --
`_roughness`, `_metallic`, `_ao`, `_occlusion`, `_height`,
`_displacement` -- cook to BC4 from red (the shader already reads all
of them from `.r`); alpha that varies cooks to BC3; everything else
BC1. **Content alone cannot pick BC4, and the first draft of this
design had that wrong**: "grey and opaque" describes a smoke sprite as
well as a roughness map, and a grey sprite cooked BC4 samples back
pure red. The suffixes are how every pipeline in this repository --
the fetchers, the applier, the glTF splitter -- already names its
maps, so the convention was already load-bearing before cooking leaned
on it. The encoder is the vendored `stb_dxt`, in the same spirit as
`stb_image`.

**BC5 forces one shader decision, made globally rather than per
format: `PerturbNormal` reconstructs Z from XY unconditionally.** For a
unit-length source normal the reconstruction is exactly the stored Z,
so RGB maps do not change; a map that was not unit length gets
implicitly renormalized, which is a correction rather than a
regression. One path for PNG and BC5 alike -- a shader that branched on
the texture's format would be the drift this file exists to prevent.
Verified with the parity fixture (stated answer, both backends) and the
closeup ablations before any RHI work starts.

**The RHI grows compressed formats, and uploads that take a chain.**
BC1/BC3 in UNORM and SRGB, BC4/BC5 in UNORM; block-aligned size math;
an upload path that accepts every mip pre-built, because compressed
targets cannot be blitted into existence. Vulkan has BC in core;
desktop GL has S3TC as the ubiquitous extension and RGTC in core.
Support is checked at device selection -- the imageCubeArray lesson --
and a device without it fails the cooked texture loudly rather than
quietly showing the fallback.

**`.rvmesh` (RVMS v1)** is the serialized `ImportedModel`: primitives
with names, vertex kind, vertices and indices; the skeleton; the clips.
The cooker runs the same `GltfImporter` the editor uses -- one parser,
no drift -- and the loader sniffs the magic before cgltf ever sees the
bytes. `.hdr` skies ship raw in v1: one per scene, loaded once, and
cooking cube faces plus irradiance is its own feature.

**Verification:** the raw pak keeps the pixel-identical E2E from 7.1
unchanged. The cooked pak is lossy by design, so its acceptance is a
*bounded* diff against loose -- the bound stated from measurement, not
hope -- plus the load-time before/after that motivated the feature.
Unit checks: rvtex and rvmesh round trips, the encode selection, mip
chain sizes, and renormalized normal mips.

**Built, measured (2026-08-13, Release, SampleProject with the 4K
set).** The cooked mesh path is *exact*: textured.rage renders 0
differing subpixels cooked-vs-loose on both backends, because the
.rvmesh geometry is bit-identical to a fresh import and the fixture's
block-flat textures are BC-representable exactly. The 4K materials
land at **mean 0.89/255 (max 77)** against loose -- under the 1.28/255
cross-backend residual that already exists on this scene, so the BC
quantization is smaller than the noise the two backends disagree by;
cooked VK-vs-GL parity is 1.22/255, unchanged from loose. The
material_closeup run drops **3.7s to 2.0s** wall (whole run, engine
init included -- the entire decode share). Disk barely moves (181 vs
200 MB pak; PNG was already entropy-coded) because disk was never the
point: the VRAM arithmetic is 32 bits per texel RGBA8 against 4 for
BC1/BC4 and 8 for BC3/BC5 -- the closeup's ten 4K maps go from ~0.9 GB
resident to ~0.15. Packaging the 198 MB project cooks in ~16s. The
Vulkan validation layer stays silent with every cooked format bound.

---

## 7j. Viewport gizmo icons (7.4)

A light, a camera, a reflection probe and an audio source have no mesh and
no collider. `PickEntity` tested exactly those two things, so **none of them
could be clicked at all** -- the hierarchy panel was the only way to select
one. The mark is what makes the entity visible; the mark's radius is what
makes it clickable, and the second half is the feature.

**No new pipeline.** `UIRenderer`'s world layer already draws textured quads
that are depth-tested and do not write depth, which is exactly what a
billboard wants; it gained `DrawWorldSprite` beside `DrawWorldText`.
Billboarding is the camera's own right and up vectors spanning the quad --
no per-mark rotation to build, nothing to get wrong at the poles. Renderer2D
was the other candidate and was rejected: it writes depth, so the
transparent corners of every mark would punch rectangular holes in the
particles behind it.

**Editor only, structurally.** `OnRenderEditor` takes an `EditorIconSettings*`
exactly as it takes a `ViewportGridSettings*`, and for the same reason: the
runtime and the game view go through `OnRenderRuntime`, which has nowhere to
put one, so a mark cannot reach a picture meant to be what a player sees.

**One `Collect`, one `Radius`, two callers.** The drawer and the picker share
both functions rather than each walking the registry and each sizing the
mark. This is the failure this codebase keeps finding -- two derivations
agree until one grows a condition -- and here it would show as a mark whose
hit area is not where it is drawn, which reads as "clicking sometimes does
nothing".

**Constant angular size is what makes that sharing possible.** The radius is
`kAngularRadius * distance from the camera`, which needs the camera's
*position* and nothing else -- and a screen-point ray already starts at the
camera position, so the picker computes the same number without being handed
a camera at all. Constant *pixel* size would have needed the FOV and the
viewport height on both sides. The cost is that a mark tracks zoom the way a
real object does instead of pinning itself to a pixel count, which is the
honest trade rather than a limitation.

**Picking is ray-versus-sphere, and for a camera-facing disc that is exact.**
The perpendicular distance from the centre to the ray *is* the screen-space
test; a ray-versus-quad test would need the billboard's axes. The distance
reported is the closest approach, so the mark sits at the depth of the point
it marks -- reporting the sphere's near face would put it a radius closer and
let a mark beat geometry genuinely in front of it. Marks compete with
geometry by distance like everything else, so a wall in front of a light wins
and the mark behind it is not selectable, which is what the depth-tested
picture already says.

**The art carries a dark outline, and that is not decoration.** A mark is
drawn against the scene -- sky, ground, whatever is being built -- so a light
one disappears over a bright sky and a dark one over a shadow. The first
version was white with a shaped alpha and washed out against the demo scene's
sky. Because the tint *multiplies*, a white interior takes the tint and a
black ring stays black, so one atlas still serves both the normal and the
selected colour. `tools/scripts/make_editor_icons.py` generates it with zlib
and struct alone, for the reason `make_icon.py` gives.

### The toolbar's grid mark, and measuring instead of looking

Reported twice as "uneven", and both times the geometry was blamed. It was
symmetric to three decimal places throughout. The cause: the outline went
through ImGui's **polyline** rasterizer while the rails and rung went through
its **line** rasterizer, and the two resolve a stroke about axes half a pixel
apart -- so the mark had two mirror axes at once and every interior line's
bright core sat against its neighbour's dim edge.

Two attempts at redrawing it moved geometry that was already correct and
barely moved the number. What settled it was **measuring the mark against its
own mirror**: 9.4/255 mean before, 0.7 after, once the outline was drawn as
four ordinary lines so every stroke shared one rasterizer. `DrawIcon` also
snaps its canvas to whole pixels at an even size, which every icon wanted
anyway -- callers compute that rect from proportions, so it arrived
fractional.

**The generalisable part: "it looks wrong" is a report about pixels, and
pixels can be measured.** Two rounds were spent redrawing a shape on the
strength of looking at it. A mirror diff would have said in one minute that
the shape was fine and the rasterizer was not.

---

## 7k. The two debts skeletal animation left (7.5, 7.6)

Both were recorded when skinning shipped and both are the same shape: a
piece of machinery that exists, is tested, and **is not called**.

### Cross-fading (7.5)

`BlendPoses` had been written and tested since phase 3. Nothing drove it, so
a character snapped between clips. The fix is not the blend -- it is the
animator noticing.

**A transition starts when `Clip` stops matching the runtime `Active`.** No
`Play(clip, time)` call: the inspector, a script and a deserialized scene all
set one field and get the same easing, and none of them has to know a blend
exists. `Started` handles the first update, which adopts whatever is authored
rather than fading in from a clip that never played.

**The outgoing clip keeps its own clock and keeps running.** Freezing it is
cheaper and visibly wrong -- a walk stops mid-stride and slides for the
length of the blend, which reads as a hitch in the *new* clip rather than as
the old one ending.

### Bounds that cover the animation (7.6)

A skinned mesh was culled by the box its bind-pose vertices made, and the
bind pose is the one arrangement a limb is guaranteed to leave. A thing that
vanishes when you look slightly away from it reads as a rendering bug.

**Per bone, not per vertex.** One pass over the vertices reduces them to a
box per bone in that bone's own space; after that a pose costs one
transformed box per bone rather than a transform per vertex. It stays
conservative because a skinned position is a weighted average of the same
vertex placed by each of its bones, so it cannot leave the union of those
bones' transformed boxes. All eight corners are transformed -- a rotated
box's extent is not recoverable from two of them, which is the classic way to
produce bounds that are too small in exactly the poses that needed them.

Sampled rather than solved, because a clip's extreme is not analytic once
rotations interpolate; the result is padded 2% against stepping over one.

### Where animation runs, and how a clip is chosen

Two changes at the owner's direction, made after the pair above and worth
recording because both are about the *editor's* relationship to animation
rather than about animation itself.

**Animation is something a running game does.** Animators no longer advance
in edit mode; `AnimatorComponent::RunInEditor` opts one in, and it is off by
default. An editor whose characters are all mid-stride has nothing standing
still to place things against, no two screenshots of a scene alike, and no
frame that corresponds to anything. Previewing is a deliberate, per-animator
act. `UpdateAnimators` takes `editing`, and a skipped animator has its pose
**cleared rather than left alone** -- the renderer draws an empty pose as the
bind pose, so unticking the box returns the character to the shape it was
modelled in instead of freezing it wherever the preview stopped. A stale pose
would be the one thing on screen corresponding to nothing.

Scripts and physics were already excluded from edit mode; the reason an
animator may opt back in where they may not is that **an animation changes
nothing anyone can save**.

**A clip is chosen by name.** The inspector offers the model's own clips in a
searchable dropdown instead of asking for an index: "2" says nothing about
which animation it is. The list is the entity's own model's clips and not
every animation in the project, because a clip is authored against a skeleton
-- offering another model's would be offering something that cannot play. The
search box earns its place on rigs whose clips share prefixes
(`run_forward`, `run_left`, `run_stop`), where a list scrolled past its own
naming convention is a list that gets the wrong entry picked. An index the
model no longer has stays visible and marked, for the reason the method
binding does: opening a dropdown to look must not be a way to lose a value.

> [!NOTE]
> **Animation is unfinished by agreement, and the scene format is the part to
> settle first.** `Clip` is stored as an *index*, so re-exporting a model with
> a clip inserted silently repoints every scene that used it. Storing the name
> is the robust choice and is a file-format decision, which is the kind that
> gets more expensive the longer it waits. Also absent: a state machine and
> per-transition blend times (the blend length is currently per animator),
> events, root motion, layers and masks, and any scrubbing in the preview.

### Running somebody else's model

The fixtures here are generated, which makes them exact and makes them
agreeable. `SampleProject/assets/models/fox.glb` -- Khronos' sample fox, CC
BY 4.0, attribution beside it -- is the opposite: twenty-four bones, three
clips and a rig nobody here authored. It is now what the blend check runs
against, because **a fixture with one clip can only ever exercise the
transition to the bind pose**, and clip-to-clip is the thing 7.5 added.

It paid for itself immediately by finding a gap in something else:
**`GltfImporter` only read a texture when the image had a `uri`**, so images
embedded in a `.glb`'s buffer views were skipped and every GLB imported
untextured. The fox was white for that reason and not because anything in
7.5 or 7.6 was wrong.

**Fixed 2026-08-13.** glTF carries an image three ways and only one of them
is a path: a sibling file, a buffer view inside a `.glb`, or a `data:` URI.
The importer handled the first, dropped the second, and stored the third's
entire base64 blob *as a filename*. Both of the latter now get written out
beside the model — the same answer the metallic-roughness split already
gives, and for the same reason: **a material stores a handle, the registry
mints handles for files, and an image that exists only inside a model file
can never have one.** Extracting at import also makes it an ordinary asset,
so packaging and cooking need no special case for it.

Two details that are not incidental. The extracted name comes from the
image's *index*, not its glTF name, because it has to be identical on every
re-import or the asset gets a fresh handle and every material referring to it
breaks. And the file extension is sniffed from the magic bytes rather than
taken from `mimeType`, because the magic is what the loader will sniff — an
exporter that mislabels one would otherwise produce a `.png` that stb reads
as a JPEG.

**The import cache version went to `v2` in the same change**, and that is the
transferable part: the model's source hash did not move, so a `v1` `.rvmesh`
would have kept serving the empty texture list it was cooked with, and the
fix would have looked like it had not worked. *The importer is an encode rule
as much as the cooker is.*

---

## 7l. Boot: why the window hangs, and what actually costs the time

The report was "launching with a project shows Not Responding for a
while before anything appears". The fix people reach for is "load the
project on a thread". **Measure first, because that thread would have
bought 0.18 of the 4.8 seconds.**

Editor cold start, Release, Vulkan, SampleProject with the 4K set:

| | wall |
|---|---|
| `--project=` pointing at nothing (device + `Renderer::Init` only) | 1.44 s |
| a light scene (adds project load, game DLL, CoreCLR, registry scan) | 1.62 s |
| the demo scene | **4.79 s** |

So `Project::Load` -- YAML, the game module, booting .NET -- is ~0.18 s
and is *not* the problem. **3.2 s is the scene's textures**: thirteen
maps, 198 MB of PNG, decoded to roughly 800 MB of RGBA on one thread.
And they are not even loaded by "loading" -- they are pulled in lazily
by the first draw that needs them, which is why the stall lands *after*
every "ready" line in the log.

That is also the only part that grows with the project, which decides
what the work is.

### Three problems wearing one coat

They are independent, and each needs its own fix:

1. **The window exists before anything can draw in it.** It is created
   at the top of `Application`'s constructor and not pumped until the
   first frame completes, so Windows paints it white and marks it
   unresponsive for the whole boot. → Create it **hidden**; `Show()` it
   once the renderer can present. A window that is not there yet cannot
   look broken.
2. **Nothing pumps the message loop while loading.** → A **boot loop**:
   the main thread pumps, drains GPU work and draws a loading screen
   while a worker does the loading.
3. **Assets load on first draw, not during loading.** A progress bar
   over work that happens after the bar is gone measures nothing. →
   **Preload** what the scene references, then hand over a scene whose
   first frame has nothing left to fetch.

### The cooker already exists; it just never ran here

7.2 built `.rvtex` and `.rvmesh` and measured exactly this saving --
the material closeup dropped 3.7 s to 2.0 s, "the entire decode share".
But `TextureCook::Cook` has exactly one caller: the **packager**. A
shipped game boots from cooked bytes and the editor, where somebody
opens the project twenty times a day, decodes 198 MB of PNG every
single time.

**So the largest win is not threading at all -- it is not repeating
work whose answer cannot have changed.** Cook on import, into a
per-project cache, and the decode disappears from every launch after
the first. Threading still earns its place: the *first* open is slower
than a decode (BC encoding costs more than unpacking a PNG), and a
large project's warm boot is still worth showing progress for.

**Where the cache goes, and why not under `assets/`:** a per-project
`Cache/` folder, deliberately *outside* the asset tree. Cooked files
under `assets/` would be walked by `Registry::ScanDirectory`, indexed
as assets, and minted `.meta` sidecars -- the cache would become
content, and content that shadows its own source.

**The key is the hash that already exists for this.** `AssetMetadata`
carries `SourceHash` with the comment "an importer compares it against
what it cached last time" -- written for an importer that had not been
built yet. This is that importer. Content hash rather than mtime,
because a checkout or a copy moves the timestamp without changing
anything, and re-cooking 198 MB after every `git pull` is precisely the
thing that makes a pipeline feel slow.

**Existence is validity: the hash goes in the *filename*.**
`Cache/v1/materials/soil_normal.png.<hash>.rvtex`. Checking the cache
is then a `stat`, not a parse -- no reading a file to discover it is
stale. The name is also readable, so a cache can be inspected by
looking at it. Writing an entry sweeps its siblings, or a project
edited all week accumulates every version of every texture. The `v1/`
level invalidates the whole cache when the cooked format or the encode
rules change, which a per-file check could not do.

The encode choice keys on the *name* (§7i), so the name must be part of
the key: two files with identical bytes and different suffixes cook
differently, and hashing bytes alone would hand one the other's
encoding. Mirroring the asset path inside the cache gives that for
free.

**The cache is never committed.** It is derived data -- every byte of
it is reproducible from the source asset beside it -- and it is larger
than the sources it came from. Worse than the size: a cooked file
arriving over a checkout carries *someone else's* hash in its name, so
it would be treated as valid for a source it was never cooked from.
`Cache/` is ignored in this repository and in the `.gitignore` every
new project is scaffolded with, because a game developer's project is
where this actually accumulates. The source asset and its `.meta` stay
in version control; the `.meta` is identity, which is the opposite of
derived.

### One thread rule

The RHI is not thread-safe and an OpenGL context belongs to one thread.
So there is a single rule, and it is worth stating as a rule because
every future addition to loading will have to obey it:

> **Decode on the worker. Touch the device only on the main thread.**

Enforced by a small queue: the worker posts a job and blocks on it, the
boot loop drains it between pumps. Call sites do not change --
`TextureLoader::Load2D` still returns a texture -- because the
marshalling lives inside the loader rather than at each of its callers.
The invariant that makes this safe is that during boot the main thread
draws *only* the loading screen, so it never loads an asset
concurrently with the worker.

### A bar that tells the truth

Counting assets would be a lie: this scene's assets range from 596 KB
to 40 MB, so an evenly-stepping bar would cross 90% and then sit there
through the largest texture. **Weight each asset by its size on disk.**
Wrong in detail -- a megabyte of `.glb` and a megabyte of PNG do not
cost the same -- but right in shape, which is the entire job of a
progress bar. It also degrades honestly on a cache hit, where a cooked
file is both smaller and cheaper.

The screen lives in the engine, not the editor: a packaged game boots
with the same delay for the same reasons, and the runtime is not a
lesser citizen. It shows the phase and the current action, because
"Loading..." for four seconds and "Decoding soil_normal.png" for four
seconds are very different experiences of the same four seconds.

### What stays slow, and is admitted rather than hidden

`Registry::Init` hashes **every** loose asset in the project on every
launch -- FNV-1a, a byte at a time, over 198 MB here. It moves onto the
worker with everything else and gets its own progress phase, but it
remains O(project) work at every boot, and it will be the next thing to
hurt. Named here so the next person measuring a slow boot does not have
to rediscover it.

The 1.44 s of device and `Renderer::Init` is untouched: it is pipeline
and shader creation, it must precede anything that can draw a loading
screen, and there is no window on screen during it.

### The cache, measured (2026-08-13, Release, Vulkan, SampleProject)

| | wall |
|---|---|
| editor, before any of this | 4.79 s |
| editor, first open (cooking all 18 maps) | 18.92 s |
| editor, every open after that | **2.70 s** |
| runtime `--import-cache=off` | 7.50 s |
| runtime `--import-cache=on` | **3.38 s** |

Cache on disk: 175 MB against 198 MB of source. Disk was never the
point (PNG is already entropy-coded) -- the load is now a read and an
upload instead of a read, an inflate, a mip build and an upload, and
the VRAM arithmetic of §7i applies unchanged.

**18.9 s to cook is the cost that has to come down**, and it is
one-time-per-asset rather than per-launch. It is also the reason the
loading screen is not optional: three seconds with a progress bar is a
different thing from nineteen with a frozen window.

**`--import-cache=off` exists so this stays measurable.** Cooking is
lossy, so its acceptance test is a bounded pixel diff rather than
equality, and a bound means nothing without the noise floor beside it.
`tools/scripts/check_import_cache.py` renders three frames -- cooked,
cooked again, and from source -- and reports both.

**The control earned its place on the first run.** Cooked-vs-source
came back at mean 5.178/255, six times the bound, which read exactly
like a broken cooker. It was not: the demo scene has falling cubes and
an animated fox, the two runs took 7.5 s and 3.4 s to reach frame 30,
and the physics had settled differently. With `--frame-time` pinned the
control is **0.000/255** -- bit-identical between runs -- and the real
answer is **mean 0.540/255, max 68 on Vulkan; 0.553 and 76 on OpenGL**,
against a 2.0/255 bound. The lesson is the one §7i already recorded and
this rediscovered: *a difference measured without a control is not a
measurement.*

### The boot loop, measured

| | wall | frames drawn while loading |
|---|---|---|
| editor, first open, serial cook | 17.2 s | 1037 |
| editor, first open, **4 workers** | **4.9 s** | 249 |
| editor, every open after | 0.62 s | 13 |
| runtime | 0.82 s | 20 |

**The frame count is the result, not the seconds.** Cooking a project
for the first time still takes seventeen seconds; what changed is that
1037 frames were drawn during them, so the window pumped throughout,
the bar moved, and the whole thing could be cancelled by closing it.
Every boot logs both numbers for that reason -- a boot reporting
single-digit frames means something has gone back to blocking the main
thread, and that is the regression worth catching.

Warm boot went 0.29 s to 0.95 s when the uploads moved *into* the loop,
and total wall 2.78 s to 3.07 s. That is not a regression: the 0.29 s
version was followed by a frozen second while the first frame pulled in
every texture. The extra 0.3 s is the vsync'd frames between upload
slices -- the price of the window being alive, paid deliberately.

`--loading-screenshot=<file>` captures a boot frame, because a screen
that is gone before the first ordinary frame can never be caught by
`--screenshot`, and a screen nothing can capture cannot be checked on
two backends. It fires on the first frame that has *something on every
line*: a fixed frame number reliably photographed the gap between a
phase beginning and its first asset being named, which is to say it
photographed the half worth checking as blank.

### Cooking in parallel: bounded by memory, not by cores

Every asset is an independent decode and encode, and the cookers hold
nothing but `constexpr` state, so the cold path parallelises directly.
**The cap is four workers on a 24-thread machine, and that is not
timidity.** Cooking one 4K map holds its mip 0 as float --
4096×4096×4 floats, 256 MB -- plus the byte buffer it encodes from.
One worker per hardware thread would ask for several gigabytes to save
a few seconds, and a machine that starts swapping is slower than the
serial version it replaced.

Two changes came out of looking at that number rather than at the
thread count:

- **`TextureCook::Cook` copied every mip level and used the copy
  once.** The copy exists so a normal map can be renormalized without
  feeding shortened-then-relengthened vectors into the next downsample
  -- but it was made unconditionally, so a quarter of a gigabyte was
  memcpy'd per 4K colour map for nothing. Now only normals copy.
- **Largest first.** With several workers on one queue the finish time
  is set by whatever is still running at the end, and the worst case is
  a 40 MB map handed out last while three workers idle. Sorting big to
  small left 4.93 s where round-robin left 5.20 s -- 5% for two lines.

17.2 s → 4.9 s, and **the cooked bytes are unchanged**: the pixel check
reports mean 0.540/255 on Vulkan and 0.553 on OpenGL, identical to the
serial numbers to three decimal places. That is the result worth
having from a concurrency change -- not that it is faster, but that it
computed the same thing faster.

The peak, not the count, is the next thing to attack if this needs to
be quicker again.

### Making the cook faster, and the three wrong turns on the way

The question was "should the worker pool size itself from the CPU?".
The answer turned out to be no, and getting there cost three mistakes
worth more than the code.

**Where the time goes** (mains power, `rvpack`, whole project):
packaging with `--raw` — no cooking at all — is **0.72 s** against
**12.84 s** cooking, so cooking is 95% of it. Inside the texture path,
11.38 s of that: **cook 76%, PNG decode 23%, writing out 1%**. So
decoding is not the problem; the mip-and-compress pass is.

**Wrong turn one: the profile was taken on battery.** Broken into
phases under power saver, the two per-texel conversion loops read as
72% of cooking and block compression as 20%. Under power saver the CPU
throttles and memory does not, so arithmetic's share is inflated. On
mains the same loops are a much smaller slice. **Profile in the power
state you care about** — this one pointed straight at the wrong work,
and the "40% faster" it produced evaporated the moment the laptop was
plugged in.

**Wrong turn two: `pow` was replaced with a binary search.** The
conversion had `std::pow` per channel and `std::lround` four times per
texel (67 million libm calls for one 4K map). Replacing the reverse
transfer with a 255-entry threshold table and `upper_bound` measured as
**exactly nothing** on mains: 12.84 s before, 12.88 s after. Eight
unpredictable branches cost about what a `pow` costs, so the win in
`ToFloat` was handed straight back in `ToBytes`. *A lookup table is not
automatically faster than the maths it replaces.*

**What worked was removing the branches.** `SrgbCandidates` is a
4096-entry direct-indexed table holding the answer for the lowest value
in each bucket, plus one branchless comparison to correct it. The
bucket size is the whole argument: entry k is never above the true
answer because the function only increases, and never more than one
below because a bucket (2.44e-4) is narrower than the closest two
thresholds ever come (1/3294.6 ≈ 3.04e-4, in the darks where sRGB is a
straight line). **12.84 s → 9.70 s**, and the editor's first open
3.90 s → 3.47 s.

**Wrong turn three: "bit-identical" was claimed before it was true.**
The first threshold table was built by carrying the midpoint
`(i + 0.5)/255` back through `SrgbToLinear` — correct in real
arithmetic, wrong in floating point, because the two transfers are not
exact inverses and a value sitting on a rounding boundary lands on the
far side. It moved **2 bytes in 190 MB** of cooked output: invisible
under any pixel tolerance, and still a silent change to shipped
content. The thresholds are now found by *bisecting the float bit
pattern against the reference function itself*, which is exact by
construction. The whole project's pak now hashes identically before and
after (`e8405c2f`), verified under identical full builds.

**The check that makes this safe is a sweep, not a rendering.**
`CheckSrgbEncode` compares the fast path against `pow`-and-round over
221,800 values — a dense uniform sweep, both float neighbours of every
byte boundary, the linear segment where the margin is thinnest, and
out-of-range inputs. Falsified by deleting the correction: it fails,
first at 0.000155, exactly in the darks the bucket argument is tightest
about. A pixel diff would not have caught two bytes in 190 MB; this
does.

**Parallelism inside one asset was built and then removed.** Every loop
here is per-row or per-block with disjoint outputs, so one asset can use
the whole machine at one asset's memory — the axis that scales with the
CPU without scaling memory with it. Measured on mains: **zero**
(12.88 s with it, 12.86 s with it forced off). These loops are not
short of cores. It is recorded rather than kept, because the honest
version of "should this scale with the CPU?" is that on this machine it
already has all the CPU it can use.

### The bug the guard found on the way past

The packager cooked **every** `.png` it could decode, and a font's MSDF
atlas is a `.png`. So every packaged build has been block-compressing
and mip-chaining a distance field -- destroying exactly the two
properties `GetFontAtlas` has a paragraph explaining it must preserve.
The only symptom is text that looks slightly soft, with nothing to
point at, which is the same silent shape as reading an atlas as sRGB.

Fixed on both sides, deliberately: `CookPolicy::IsFontAtlas` stops the
producers (packager and cache) from cooking one, and `Load2D` refuses
to take a cooked chain when the caller asked for no mips, uploading
only the top level and saying why. A rule this quiet deserves a guard
at both ends, and the loader's half also covers packages built before
the fix.

`.hdr` environment maps are still converted to cube faces and convolved
for irradiance on every load. Cooking those is its own format and its
own feature; the cache is built so adding a third cooked kind is a case
in one switch rather than a redesign.

---

## 7m. Depth sorting (7.8), and why the obvious version loses

The roadmap row demanded a measurement before any code, because *two
"obviously worth it" optimisations in this renderer have measured as
worth nothing*. This one is worth a great deal — and the measurement
rewrote the design twice.

### The ceiling, and how to see it

Early-z only skips shading a pixel once something nearer has written
depth, so the entire value of ordering is a property of how much a
scene overlaps itself. `tools/scripts/make_overdraw_scene.py` builds
the worst case anyone could hand the renderer: 200 full-screen slabs
along the view axis, depth complexity 200. Nearest-first against
furthest-first there is **0.32 ms against 33.9 ms — a factor of about
100.** On the 1500-mesh stress scene, which spreads its objects out,
the same comparison is worth ~15%.

So the answer is "yes, and it depends entirely on the content" — which
is why `--depth-sort=off` exists rather than a remembered number.

### Two wrong designs, both measured

**A global sort by depth loses.** It orders perfectly and dissolves the
mesh+material grouping instancing depends on: 1500 meshes measured
**0.567 ms fully depth-sorted against 0.548 ms in the existing grouped
order**. The draw calls lost cost more than the overdraw saved.

**Sorting only the batches does nothing where it matters.** The obvious
repair — keep the runs intact, order the runs by their nearest member —
was built, and moved the slab scene from 33.9 ms to 33.5. The reason is
worth keeping: those 200 slabs share one mesh and one material, so they
are *a single batch*. Reordering batches cannot reorder anything inside
one.

**What works is sorting inside each run, and then the runs.** Instances
within a run share a mesh and a material by definition, so reordering
them changes nothing about the draw — same batch, same instance count,
same state — and collects the whole early-z effect. Ordering the runs
afterwards helps a scene made of many distinct meshes rather than many
copies of one. Both together: 0.32 ms on the slabs, and 0.546 against
0.607 on the stress scene.

### What it costs, and what it must not change

CPU goes up ~0.085 ms on 1500 objects — a sort per run plus one pass.
Worth noting alongside that: sorted GPU time is also far *steadier*
(spread 0.010 ms against 0.119), because unsorted overdraw depends on
registry order, which moves as culling changes.

Reordering opaque, depth-tested draws cannot change the image, and
`check_depth_sort.py` asserts exactly that rather than a tolerance:
**0 of 4,320,000 subpixels differ on both backends**, and the draw count
is identical either way — which is the evidence that the grouping
survived, since a broken batch would show up as more draws.

### The trap that cost the most here

The first full set of measurements showed **no difference at all**
between nearest-first and furthest-first, on any scene. That looked
like a real finding and was nearly written up as one.

It was a stale DLL. **Each executable's directory holds its own staged
copy of `RageV.dll`, and `cmake --build --target RageV` does not
refresh them** — only building the *application* target does. So three
"different" runs were the same binary. The tell should have been
obvious: the build output never named `Renderer3D.cpp`.

The fix that made it visible was a control, not a rerun: log the first
and last draw's depth once per mode. `first=6.00 … last=404.00` against
`first=404.00 … last=6.00` proves the ordering changed; without it,
"the two orderings measure the same" and "the probe never ran" are the
same observation. **When an experiment reports no difference, prove the
independent variable actually moved before believing it.**

---

## 7n. SMAA (7.9): the algorithm, minus the lookup tables

FXAA looks at one pixel's neighbourhood and guesses. Morphological
anti-aliasing does something categorically different: it **reconstructs
the silhouette**. It finds the run of pixels a near-horizontal edge
spans, works out from the ends of that run which way the real line was
sloping, and computes how much of each pixel the line actually covered.
That is why it is sharper — it is not blurring an edge it found, it is
computing the coverage the rasterizer should have produced.

Three passes, and they are cheap for a reason worth stating: **only edge
pixels do any work.** The first pass rejects the flat majority of the
frame, and the second runs a search only where the first found
something.

| Pass | Reads | Writes | What it decides |
|---|---|---|---|
| Edges | the tone-mapped image | RG8 | is there a discontinuity on my west side, on my north side |
| Weights | the edge map | RGBA8 | given the run this edge belongs to, what fraction of each pixel the true line covered |
| Blend | the image + the weights | the output | two bilinear taps, offset by those fractions |

Like FXAA this runs **after** the tone curve, for the same reason: it
thresholds on perceived brightness, and perceived brightness is what the
transfer function produces.

### The deviation: no AreaTex, no SearchTex

Reference SMAA ships two precomputed lookup textures, and the ROADMAP
row for 7.9 said this would need them vendored in. It does not, and the
reason is worth recording because it is the same reason twice.

**AreaTex** answers "given a run of length *L*, my position in it, and
how the line terminates at each end, what fraction of my pixel is
covered?" That is a **trapezoid area**, and it is about fifteen lines of
arithmetic:

```
L  = d1 + d2 + 1                  // run length in pixels
y0 = yLeft + (yRight - yLeft) * d1     / L    // where the line enters this pixel
y1 = yLeft + (yRight - yLeft) * (d1+1) / L    // and where it leaves
```

with `yLeft`/`yRight` ∈ {−½, 0, +½} read off the crossing edges. If `y0`
and `y1` share a sign the coverage is the trapezoid `(y0+y1)/2`; if they
do not, the line crosses inside the pixel and the two triangles either
side of the crossing are the two weights. Sign says which neighbour the
coverage belongs to: **negative means the line sits above the row
boundary, so the pixel above is the one that is partly covered.**

A 179 KB table exists to avoid that arithmetic on 2008 hardware. On
anything current a dependent texture fetch costs more than the maths it
replaces, and the table costs a blob in the repository whose contents
nobody in this project can check.

**SearchTex** is an acceleration for the run search: it lets one
bilinear tap cover two pixels and still resolve which of the two ended
the run. That ambiguity only exists in the general case. Searching
outward from a known edge, **the run is contiguous by construction** —
if a two-pixel tap averages ½, the nearer one is the edge and the
further one is not, because the other way round is not reachable. So
the table answers a question this loop cannot ask.

The v1 search is a plain point-sampled loop anyway, 16 steps each way,
because the two-at-a-time version is an optimisation and this repository
has been wrong about two "obviously worth it" optimisations already. It
gets measured before it gets clever.

What this costs, honestly: the areas are computed to float precision
rather than quantised to the table's 8 bits, so **this will not match a
reference SMAA implementation pixel for pixel.** It is not trying to. It
is trying to be the algorithm, legibly.

### The trap this feature has that FXAA did not: y is not the same way up

`PostProcess::Dispatch` already fills a `FlipY` push constant, because a
fullscreen pass reads a texture and writes a framebuffer and the two
backends do not agree on which end of the image row zero is. For every
pass so far that was the whole story — flip the sample coordinate and
the image comes out the right way up.

**That is not sufficient here, and the reason is subtle.** After the
flip, a fragment at the top of the destination does read the top of the
source, on both backends. What differs is the *direction* the y axis
runs in sample space: to move one pixel towards the top of the image you
add `−texel.y` on Vulkan and `+texel.y` on OpenGL.

FXAA never noticed because FXAA is symmetric — it treats north and south
identically, so getting them the wrong way round changes nothing. This
filter is not symmetric anywhere. `edges.g` means *specifically* "there
is a discontinuity between me and the pixel above me", the weight pass
searches perpendicular to that, and the blend pass offsets towards one
named neighbour. Get the direction wrong on one backend and the image
still looks anti-aliased — every edge is simply smoothed towards the
wrong side by half a pixel.

So the shaders take a `DirY` of ±1 and every vertical offset is
multiplied by it. And the acceptance test is built on exactly this:
**the two backends must agree**, on a scene chosen to have edges at
angles where being off by one row is visible. A silent half-pixel shift
is the failure mode, and only a cross-backend diff catches it.

### The acceptance test, which is a measurement rather than a look

Three claims, three pieces of evidence, all in `check_smaa.py` against a
scene that is one straight edge at a known angle (`make_aa_scene.py`):

1. **It must not touch what is not an edge.** Bit-identical to
   `--aa=none` more than four pixels out. Exact, not a tolerance.
2. **Both backends must agree.** The `DirY` check above.
3. **It must actually straighten edges.** The scene's geometry is known,
   so the exact coverage of every pixel is computable, and the metric is
   the RMS error against it.

**And the measurement carries its own control.** With no filter, every
pixel is wholly one side or the other, so the column sums are a
staircase — and a staircase's deviation from the line it approximates is
the uniform quantisation error, **1/√12 = 0.2887 px at any angle**. The
unfiltered image measures 0.2885. A measurement that does not reproduce
a number known in advance is not evidence of anything, and that is the
lesson §7m paid for.

### The numbers (2026-08-14, Release, 1600×900, RTX 5070 Ti Laptop)

Coverage error against the exact edge, RMS, lower better:

| Edge | none | FXAA | SMAA |
|---|---|---|---|
| 8° | 0.1291 | 0.1072 | **0.0210** (6.1× none, 5.1× FXAA) |
| 43° | 0.1391 | 0.1127 | **0.0849** (1.6× none, 1.3× FXAA) |

Vulkan and OpenGL agree to four decimal places on every one of those, and
**0 of 1,427,200 pixels** away from the edge were touched.

Cost, as the render graph's own GPU span on the demo scene, three runs
each: 0.440 ms with no filter, 0.459 with FXAA, **0.498 with SMAA**. So
SMAA costs 0.058 ms against FXAA's 0.019 — three passes for three times
the price, which is the least surprising result here.

**The diagonal question, answered twice, and the first answer was
wrong.** The design above expected the orthogonal-only version to be
weakest exactly where FXAA is strongest, and said the measurement would
decide whether a diagonal pass was needed. The first measurement, taken
at exactly 45°, said it was not — 9.6×, better than the shallow case.

That was an artifact of the angle. **At exactly 45° the edge advances one
row per column**, so the staircase lands on a perfect line and the
control reads 0.0000: a property of the angle, not of the renderer. Every
supersample grid is symmetric about that diagonal too, so it is also the
one angle at which supersampling can measure as buying nothing — which is
how it was caught, when SSAA scored 1.0× there and 2.95× elsewhere.

Re-measured at **43°**, which is near-diagonal without being degenerate:
SMAA is **1.6×**, against 6.1× on a shallow edge. That is the original
prediction, and it is the diagonal pass SMAA does not have. `ANGLES` in
the check is 8 and 43 for this reason, with the reason written beside it.

**A degenerate case is the worst possible thing to build an acceptance
test on**, because it does not fail — it reports a number, and the number
is flattering.

**Corner detection is also absent.** Reference SMAA attenuates the weight
near a sharp 90° corner so it does not get rounded off; without it, the
"both ends step the same way" pattern reconstructs as a flat line half a
pixel off and over-blends a short run. Ten lines when a case is found
that shows it.

### What this found in FXAA, which is the more useful result

Measuring SMAA against FXAA needs FXAA to work. It did not.

The check said FXAA changed **zero pixels** on a clean straight edge, on
both backends. Two candidate explanations — the pass never ran, or it ran
and did nothing — and they are not distinguishable by staring. The
control was to make the FXAA shader write pure red: 0.22% of the frame
came back red, about 3,200 pixels, which is exactly the two rows either
side of a 1600-pixel edge. **So it ran, on precisely the right pixels,
and returned every one of them unchanged.**

Two independent sign errors, both of which had been there since the
shader was written:

- **The orientation test was inverted.** `edgeHorizontal` was built from
  second differences *along rows*, which detects a **vertical** edge, and
  `edgeVertical` from differences down columns. So every edge was
  classified as the opposite of what it was.
- **The step direction was inverted.** `N` and `W` are sampled at *minus*
  one texel, so reaching the more-different neighbour needs a negative
  step; the code started positive and flipped only when the *other* side
  was more different.

Either alone is a wrong-looking blur. Together they are a no-op: the
filter stepped along the edge instead of across it, where the two taps
either side are the same colour, so the bilinear tap returned the pixel
itself. It only ever appeared to work because a busy scene has enough
broken and diagonal edges to leave marks anyway — which is exactly how it
survived a phase of screenshots.

Repaired, FXAA is worth 1.2× at 8° and 1.7× at 45°. That is a plausible
figure for FXAA, and it is now a figure rather than an assumption.

**The general lesson is the mirror of §7m's.** There the trap was
believing a null result; here it was that a feature which had shipped,
been screenshotted on two backends, and been described in three
documents had never once been asked to produce a number. **A default
nobody has measured is a default nobody has tested.**

---

## 7o. SSAA (7.12), and the two definitions of correct

Render the scene N times larger on each axis and average it down. There
is no cleverness in it, and that is the appeal: no edge detection to get
wrong, no direction to confuse, no pattern to misclassify. It is also the
only mode here that anti-aliases **shading** rather than geometry — a
specular highlight sparkling across a curved surface, or a texture
folding into moire, is detail the frame never sampled finely enough, and
no filter working on the finished image can invent what was never
captured.

The resolve is a box filter, deliberately. Each output pixel covers
exactly N by N source pixels with no overlap and no gaps, so their mean
*is* its coverage-weighted colour. A Gaussian would reach outside that
footprint and trade correctness for a look.

### Where it goes in the frame, which is the whole design

**Before bloom and before tone mapping.** Two reasons, both of the kind
that produce a subtly wrong image rather than an obviously broken one:

- Averaging is only meaningful where the numbers add up. Two samples of
  0.05 and 0.60 describe a pixel that received 0.325 of the light. After
  the tone curve they describe two *display* values, and halfway between
  those is a different and wrong number.
- Bloom thresholding the supersampled image would let one bright
  subsample light a whole output pixel — the firefly SSAA exists to
  remove.

So the frame is one pass *shorter* than either morphological filter: the
resolve happens in linear HDR and tone mapping writes the output
directly.

### The finding: the scoreboard has two columns

`check_smaa.py` originally measured every mode against coverage
interpolated between two *display* values, and SSAA scored badly — 1.24×
where theory said 2×. The tempting reading was a broken resolve.

It was the yardstick. SSAA mixes in linear light and then tone maps;
FXAA and SMAA mix after the curve, because they must — they threshold on
perceived brightness, which does not exist before it. **Each was being
graded on the other's exam.** The check now computes both ideals, judges
each mode against the one it can actually reach, and prints the other so
the gap stays visible:

| 8° edge | none | FXAA | SMAA | SSAA 2× |
|---|---|---|---|---|
| mixed after the curve | 0.1291 | 0.1072 | **0.0210** | 0.1038 |
| mixed in linear light | 0.1730 | 0.1555 | 0.1006 | **0.0707** |

| 43° edge | none | FXAA | SMAA | SSAA 2× |
|---|---|---|---|---|
| mixed after the curve | 0.1391 | 0.1127 | **0.0849** | 0.1147 |
| mixed in linear light | 0.1912 | 0.1688 | 0.1451 | **0.0648** |

Read the row each mode belongs to and both are working. Read one row
across and either can be made to look broken. And worth saying plainly:
**the linear-space answer is the physically correct one**, and SSAA is
the only mode that can produce it. SMAA is the best available answer to a
strictly harder question.

MSAA (7.13) lands in the linear row too, for the same reason — it
resolves the scene target before the curve — so the second column is what
to compare it against when it arrives.

### What it costs, which is the entire objection to it

Render graph GPU on the demo scene at 1600×900: **0.591 ms unfiltered,
0.669 with SMAA, 1.706 with SSAA at 2×, 6.881 at 4×.** Almost exactly the
square of the factor, which is what it has to be — 2× shades four times
the pixels and 4× shades sixteen.

That is not a default. It is the quality setting a person turns on
knowing the price, which is why the factor sits in the inspector next to
the mode rather than baked into it, and why it is clamped to 4: at a 4K
output, 4× asks for a 16K scene target.

---

## 7p. SMAA's diagonal pass (7.14): design first

The orthogonal reconstruction is 6.1× on a shallow edge and **1.6× on a
43° one**. That gap is not a tuning problem, and it is worth being
precise about why, because the fix follows from it.

On a near-diagonal edge every run is **one pixel long**. So `d1 = d2 = 0`,
the run length is 1, both ends carry a crossing edge, and `Coverage`
lands in its two-triangle branch with the line crossing the pixel's
middle: two triangles of ⅛ each, for every pixel on the edge, regardless
of where the real line actually sits. The reconstruction has no
information left to use — the pattern it reads is the same for every
offset.

**The information is there; it is just along the other axis.** Walk
*diagonally* instead and the run is long again.

### The two staircases, which are not the same shape

Take a region boundary and rasterize it. Which flags a boundary pixel
carries depends on which way the diagonal leans, and the two cases need
different searches.

**Anti-diagonal, "/"** — light where `col + row > K`. The pixel at
`col + row = K + 1` has a dark pixel to its north (`row − 1` gives `K`)
*and* a dark pixel to its west (`col − 1` gives `K`), so **both flags
land on one pixel**.

**Main diagonal** — light where `row > col`. The pixel `(c, c+1)` is
light with a dark pixel above it, so it carries a north edge and no west
edge; `(c, c)` is dark with a light pixel to its west, so it carries a
west edge and no north edge. **The flags alternate.**

### The correction only rasterizing found

That is the ideal case, and the ideal case is **exactly 45° only**.
Everything this feature exists for is not 45°.

Rasterizing the 43° edge the check actually uses: **zero of its boundary
pixels carry both flags.** Twenty-six carry a north edge alone. The row
advances by one per column forty-five times and by zero three times — a
diagonal run of about fifteen broken by a double step, exactly as the
model says. But the "both flags" signature is gone, because at 43° the
west neighbour is on the same side of the line.

So there is one search rather than two special cases: **step diagonally
and require the flag the pixel already had.** Both orientations, both
diagonals, one test. The symmetry above is real and useless, and it was
worth ten lines of Python to find that out before building a shader
around it.

### What the run length tells you, and the one honest ambiguity

For an *exactly* 45° edge the staircase is perfect and the run never
ends. That is not a defect of the search — it is a real ambiguity in the
image: **every sub-pixel offset of a true 45° line produces the identical
staircase.** There is nothing to recover, and the maximum-entropy answer
is that the line passes through the pixel centre, which is coverage ½.
Any implementation claiming better than ½ there is inventing it.

Away from 45° the run is finite, and its length is the measurement. Over
one run the line drifts by exactly one pixel across the diagonal — that
drift is what ends the run — so position within the run gives the offset.

### The coverage, exactly

A near-diagonal line in the pixel's own frame is `y = s·x + k` with
`s ≈ ±1`. For the anti-diagonal, `s = −1` and `k` ∈ [0, 2] carries the
line from one corner to the other. The fraction of the pixel above it is

```
A(k) = ∫₀¹ clamp(k − x, 0, 1) dx = G(k) − G(k − 1)
```

with `G(t) = 0` for `t < 0`, `t²/2` on `[0, 1]`, and `t − ½` above — the
integral of the clamp, and exact rather than sampled. It checks out at
the three cases anyone can verify by eye: `A(1) = ½` (corner to corner),
`A(½) = ⅛` (one corner triangle), `A(1½) = ⅞`. The main diagonal is
`G(k + 1) − G(k)`, the same function mirrored.

And `k` comes from the run, exactly as `yStart`/`yEnd` do in the
orthogonal case. With `d1` steps behind and `d2` ahead, `L = d1 + d2 + 1`:

```
k = ½ + (d1 + ½) / L        // drifting one way
k = 1½ − (d1 + ½) / L       // the other
k = 1                        // both ends hit the search limit
```

The third line is the 45° ambiguity above, and it falls out rather than
being special-cased: with no end information the line is centred, and
`A(1) = ½`.

**This is the same shape of argument as the orthogonal pass** — walk the
run, read the ends, integrate a clamped line — which is the point. It is
one more coverage function, not a second algorithm, and it needs no table
for the same reason the first one did not.

### What has to be true afterwards

`check_smaa.py` already has the instrument. **43° must move off 1.6×, and
8° must not regress from 6.1×** — a diagonal search that fires on shallow
edges would trade one for the other, and that is the failure mode to
watch for rather than a crash. Both backends must still agree, and flat
regions must still be bit-identical to `--aa=none`.

### What it measured, and the rule the fan scene forced

Coverage error against the exact edge, RMS, lower better. `--aa=smaa`
before and after, everything else unchanged:

| Edge | none | orthogonal only | with diagonals |
|---|---|---|---|
| 8° | 0.1291 | 0.0210 | **0.0210** |
| 43° | 0.1391 | 0.0849 | **0.0403** |
| 47° | 0.1318 | — | **0.0387** |
| 77° | 0.2004 | — | **0.0342** |

43° more than doubles, and **8° does not move at all** — which was the
failure mode to watch for, since a diagonal search that fires on shallow
edges buys one and sells the other. 47° is in the set because it is the
same edge leaning the other way, where the *west*-edge diagonal runs;
without it half the new code was never executed once.

**Then the fan scene said the opposite.** Thin bars at a dozen angles,
scored against a 4× supersampled render, is content that turns every few
pixels — and with the bare "longest run wins" rule SMAA went from 3.933
to **4.313**, worse than not having the pass at all. A single straight
edge cannot show that, which is exactly why the design said to build the
scene.

The cause is that a short run is not information. A diagonal run of three
resolves coverage to a third, and on content that turns constantly the
rule fired on those everywhere, replacing a mediocre answer with a worse
one. Requiring the diagonal to win **by a margin** says the necessary
thing twice over, since the orthogonal run is never negative: the
diagonal has to be long, *and* it has to be the longer of the two.

| margin | fan vs 4× reference | 43° edge |
|---|---|---|
| pass off | 3.933 | 0.0849 |
| 0 | 4.313 | 0.0414 |
| 2 | 4.104 | 0.0414 |
| 4 | 3.944 | 0.0414 |
| **8** | **3.933** | **0.0403** |

Eight is where the harm reaches exactly zero — the same score as
switching the pass off — while the edge it exists for is untouched all
the way up and marginally better at 8 than at 0. A measured constant, not
a chosen one, and the sweep is in the shader comment beside it.

On the finished fan, with two near-45° bars added so it tests the pass
firing and not only declining: **none 6.469, FXAA 5.598, SMAA 4.092,
SSAA 4× 2.268.**

### The reference that made the fan measurable

There is no formula for the correct image of a fan of bars, so the
reference is *rendered*: SSAA at 4×, sixteen samples a pixel, the most
correct image this engine can produce. It resolves in linear light
(§7o), so no post filter can reach it exactly — but every post filter is
handicapped identically, so ranking them against it is fair even where
the absolute numbers are not.

**This is the instrument MSAA gets judged with too**, and it is worth
more than the feature that prompted it: it measures arbitrary content,
where the analytic metric only ever measured a straight line.

### Cost

Render graph GPU, demo scene, 1600×900, all in one batch so the numbers
are comparable: **0.605 ms no filter, 0.677 orthogonal only, 0.717 with
diagonals.** The pass adds 0.040 ms — about half again on SMAA's own
overhead, and still a tenth of what SSAA at 2× costs.

Four extra searches per edge pixel and no extra passes, targets or
bandwidth, which is why it is cheap: everything it needed was already in
the edge map.

---

## 7q. MSAA (7.13): design first

The last of the four, and the only one that is an *RHI* feature rather
than a pass. Everything before it worked on images the renderer had
already finished; this changes how the scene is rasterized.

**What it actually does, and what it does not.** The rasterizer takes N
coverage samples per pixel and keeps a depth value for each, but runs the
fragment shader **once** per pixel per triangle. So geometry edges get N
levels of coverage for close to the cost of one shaded pixel — and
shading aliasing gets nothing at all. A specular highlight sparkling on a
curved surface is one shaded sample either way. That is the whole
difference from SSAA (§7o), which shades every sample and therefore fixes
both and costs the square.

### Where the sample count has to reach

Three places, and the third is the one that bites:

1. **The images.** Vulkan already honours `TextureDesc::Samples` — it has
   since the field existed. OpenGL ignores it entirely and needs
   `GL_TEXTURE_2D_MULTISAMPLE`.
2. **The resolve.** Vulkan uses dynamic rendering, so
   `VkRenderingAttachmentInfo` carries `resolveImageView` and
   `resolveMode` directly and the hardware resolves when the pass ends.
   OpenGL blits between two framebuffers.
3. **Every pipeline drawn into the target.** `rasterizationSamples` must
   equal the attachment's count. A mismatch is undefined behaviour rather
   than an error, which is the worst kind. `Renderer::SetTargetFormats`
   already fans the colour and depth formats out to all six renderers
   that draw the scene, so the count goes with them.

### The trick that keeps it out of the frame graph

A multisampled image cannot be read by an ordinary `sampler2D`. Taken
literally that infects everything downstream — bloom, tone mapping, the
transparency composite — with a second code path.

It does not have to. **`RHIRenderTarget::GetColorTexture` returns the
*resolved* image when the target is multisampled**, and the resolve is
part of ending the pass. Every existing caller keeps saying
`context.Color(sceneHDR)` and keeps getting something it can sample. The
frame graph's only change is a number on one target description.

Two consequences worth stating rather than discovering:

- The resolve runs at the end of **every** pass that writes the target,
  not once at the end of the frame. On the scene target that is up to
  four resolves where one would do. Correct, wasteful, and measurable —
  which is the right order to do those in.
- The multisampled image keeps its contents between passes, so
  `RGLoad::Preserve` still means what it meant.

### Transparency, which is the reason this is an L

Weighted-blended OIT (§ phase 6) puts its accumulation and revealage
buffers on the *scene's* target so they share one depth buffer. Under
MSAA all three colour attachments and the depth become multisampled
together — there is no mixing sample counts in one pass.

The composite then samples accumulation and revealage. With the rule
above it samples their *resolved* copies, which is exactly right: it is a
fullscreen pass with no coverage of its own, so it would write the same
value to every sample anyway. **Transparency is composited at one sample
and that is not a compromise** — the alpha it carries is not a coverage
mask, so there is nothing per-sample about it to preserve.

That also keeps the pass order the frame already has: scene, transparent,
composite into the multisampled colour, overlay, resolve. A collider
wireframe still lands over the smoke it describes.

### What has to be true afterwards

MSAA resolves in **linear light, before the tone curve**, exactly as SSAA
does — so it belongs in the second column of §7o's table, and comparing
it against the first would repeat the mistake that section documents.

The instruments already exist. Four angles, both backends, flat regions
untouched, and the fan of thin bars against a 4× supersampled reference
(§7p) — which is the interesting one, because it is the test where MSAA
should beat every post filter on geometry and still lose to SSAA on the
bars' shaded interiors. If it does not, something in the sample count did
not reach one of the three places above.

**And one prediction worth writing down before measuring**, because it is
the claim that decides whether MSAA is worth its complexity: at equal
sample count MSAA should cost far less than SSAA and score close to it on
geometry. If it lands near SSAA's *cost*, the fragment shader is running
per sample and the pipeline state is wrong.

### What it measured

Coverage error against the exact edge, RMS, on the **linear-space** ideal
— the one MSAA and SSAA can reach, because both resolve before the tone
curve. Lower is better:

| Edge | none | FXAA | SMAA | SSAA 2× | MSAA 4× |
|---|---|---|---|---|---|
| 8° | 0.1730 | 0.1555 | 0.1006 | 0.0707 | **0.0434** |
| 43° | 0.1912 | 0.1689 | 0.1156 | 0.0648 | **0.0597** |
| 47° | 0.1812 | 0.1601 | 0.1099 | **0.0610** | 0.0609 |
| 77° | 0.2691 | 0.2343 | 0.1565 | 0.0954 | **0.0817** |

And on the fan of thin bars, against a 4× supersampled render (§7p):
none 6.469, FXAA 5.598, SMAA 4.092, SSAA 2× 2.268, **MSAA 4× 1.932** —
the closest thing to the reference that is not the reference.

**The prediction held, which is the part that matters.** §7q said before
any of this was measured: at equal sample count MSAA should cost far less
than SSAA and score close to it, and *if it lands near SSAA's cost the
fragment shader is running per sample and the pipeline state is wrong*.
Render graph GPU on the demo scene, one batch:

| | GPU | over none |
|---|---|---|
| none | 0.618 ms | — |
| MSAA 4× | 0.671 ms | **+0.053** |
| SMAA | 0.719 ms | +0.101 |
| SSAA 2× | 1.771 ms | +1.153 |

Four coverage samples for **+0.053 ms** against four shaded samples for
+1.153 — twenty-two times cheaper, and better on every angle. It is also
cheaper than SMAA while beating it, which makes MSAA the mode to reach
for on anything with a GPU budget, and leaves SMAA as the one that costs
nothing in memory and works on content MSAA cannot see.

**What MSAA still cannot do** is the whole of what SSAA is for: shading
aliasing. A specular highlight sparkling across a curved surface is one
shaded sample under MSAA at any sample count. That is not a gap to close;
it is the definition of the technique.

### The bug, and the four fixes that were not it

Five hypothesis-driven fixes with one confirmation is the pattern §7m and
§7n both warn about, and this went four rounds of it before stopping to
*diagnose* instead. Worth recording in that order, because the four wrong
turns were all real defects:

- the resolve images were missing from the pass-end layout transition, so
  every later pass sampled an image still in `COLOR_ATTACHMENT_OPTIMAL`
  — **validation said so immediately**, which is why this one was cheap;
- `UIRenderer`'s **world** layer draws inside the scene pass and needs the
  scene's sample count, while its screen layer must not have it;
- that layer rebuilt its pipeline whenever the count moved, and rebuilding
  **flushes the batch buffers**;
- a probe's scratch face has to follow the count, as it already follows
  the resolution.

None of them was the crash. What found it was giving up on hypotheses and
bisecting: `demo.rage` down to **six entities — a camera, a light, a
reflection probe and a particle emitter**, both backends, first frame,
validation silent.

The bug was one word:

```cpp
if   (s_Data->BatchCursor >= batches.size())   // grows the pool by one
Batch& batch = batches[s_Data->BatchCursor++]; // then indexes the cursor
```

`if` is only safe when the cursor is exactly at the end. The pool is
**cleared whenever the pipelines are rebuilt**, and a rebuild lands
mid-frame under MSAA: `BuildFrame` sets the sample count *after* a probe
has already captured six faces and advanced that cursor. Clear the vector,
add one back, index four — out of range.

`UIRenderer` already said `while`. Six other pooled-resource sites said
`if`, and every one of them is now `while`, because the post-condition a
grow owes its caller is `cursor < size` and only one of those spellings
guarantees it.

**MSAA did not cause this; it is the first thing that ever cleared a pool
mid-frame.** Which is the general shape of the whole exercise: the crash
was in code four years of frames had never stressed, and the new feature
was the stress.

---

## 7r. TAA (7.10): design first, because it breaks the test suite

The last item in Phase 7, and the only one that is not a function of the
frame it runs in. FXAA, SMAA, SSAA and MSAA all take one frame's worth of
information and make an image out of it. **TAA takes the last thirty.**

That is where its quality comes from — a different sub-pixel offset every
frame, accumulated, converges on a supersampled image for the cost of one
sample — and it is also the source of every problem it has. Three of
those problems are the implementation; the fourth is this repository.

### The three prerequisites, and which of them is shared

**1. Motion vectors.** Where each pixel was last frame, in screen space.
Without them the history can only be sampled at the same pixel, which is
correct for a static camera and smears everything else. This means every
instance carrying its **previous** world transform and the scene writing
a velocity attachment.

That is the expensive prerequisite, and it is also **not TAA's alone**:
9.5 motion blur wants exactly the same buffer, and so does any temporal
upscaler. It should land, and be verified, on its own — before any
temporal filter exists to hide whether it is right.

**2. A jittered projection.** A sub-pixel offset per frame, from a low
discrepancy sequence (Halton 2,3) so the samples spread evenly rather
than clumping. Without jitter there is nothing new to accumulate and TAA
is just a blur with extra steps.

**3. A history buffer.** Last frame's resolved output, which the render
graph cannot supply: its targets are pooled by shape and reused within a
frame, so nothing in it has identity *across* frames. History has to be
owned outside the graph and handed in, the way the editor's viewport
image already is.

### What rejects the history, which is the whole difficulty

Reprojection alone ghosts. A pixel that was hidden last frame has no
history to fetch, and fetching one anyway drags a smear behind every
moving silhouette. The standard answer is **neighbourhood clamping**:
build the colour AABB of the current pixel's 3×3 neighbours and clamp the
reprojected history into it. History that cannot be explained by anything
nearby is history that does not belong to this pixel.

It is a heuristic and it is worth saying so. It trades ghosting for
flicker, and where it is set is a taste decision that should be a number
in the inspector rather than a constant somebody chose once.

### The two things jitter must not reach

**Shadow maps and reflection probes.** Both render the scene through
their own projections, and both are *reused across frames* — a shadow
cascade jittered per frame would shimmer along every shadow edge, and a
probe captured over six frames would assemble six differently-offset
faces into one cube.

The engine already has the hook for this: probe capture and the shadow
pass build their own cameras. The jitter belongs to the scene camera at
the point the frame graph is built, and nowhere else. §7q learned the
same lesson about sample counts one item ago — **global render state set
for the scene pass leaks into every other pass that reuses the same
renderers**, and the fix both times is to be explicit at the boundary.

### And the thing that breaks

**Every screenshot check in this repository assumes a frame is a pure
function of the scene.** `--screenshot-frame=30` exists so the scene has
settled; `--frame-time` exists so nothing depends on how fast this
machine ran. Both assume frame 30 would be identical if you rendered it
alone.

TAA makes frame 30 depend on frames 1 to 29. That is fine — but only if
the jitter sequence is indexed by **frame number**, not by elapsed time.
Drive it from a clock and every check in `tools/scripts` becomes
irreproducible, and the failure looks like noise rather than like a
mistake. This is the single most important line in this section.

The acceptance bar then has an honest gap. A static scene lets TAA
accumulate thirty jittered samples and converge, so it should score
*better than every other mode* on `check_smaa.py`'s angles — and that
number will be close to meaningless, because it measures TAA's best case
and the one nobody complains about. **Its only real failure mode is
motion**, and seeing it needs a moving scene and a reference the moving
scene can be compared against. That check does not exist yet and TAA is
not finished until it does.

### Order

Motion vectors, verified alone. Then jitter, verified by the checks
staying reproducible. Then history and rejection. Anything else is
building three unverified things and finding out which is wrong by
looking at ghosting.

### The jitter, as built

**One camera, offset once.** The scene pass draws through meshes, sky,
grid, world text, editor icons and particles, and every one of them takes
its view-projection from the camera `Scene::OnRender` was handed. So the
offset is applied there, to a local copy, and everything follows. The
alternative — each renderer jittering its own projection — is a rule the
next renderer somebody writes has to be told about, and the symptom of
forgetting is a half-pixel misregistration that reads as *the temporal
filter is soft on particles* rather than as a missing line.

Particles are the case that makes this worth stating. They are *drawn*
two passes later, in the transparent pass, but their view-projection is
captured during the scene pass — so they jitter with the geometry
without the transparent pass knowing the jitter exists.

**Where it must not reach**, and how it is kept out. The offset lives on
`Renderer` and is set only for the duration of the scene pass. Reflection
probe captures run outside the graph entirely, earlier in the frame, so
they see zero; `Scene::OnRender` also tests `m_CapturingProbes` directly,
which is belt and braces, and is the version a reader looking at that
function can see. Shadow cascades are safe by construction: they never
see the scene camera at all, because a cascade's view-projection is
folded into each caster's model matrix.

**The count is not the process's.** Loading draws frames too — through
`Renderer::BeginFrame`, deliberately, so there is only one way to get a
picture onto the swapchain — and *how many* depends on whether the import
cache was warm. Indexing the jitter by a process-wide count would
therefore land on a different offset on a cold run, which is the same
irreproducibility a clock would cause, arriving through a different door.
So the main loop resets the count, and the screenshot's "frame 30" is
read from the same counter: two counters meant to agree are one more
thing that can drift.

**Motion vectors have to come back out of it.** Both projections in the
scene block carry the offset of the frame that built them, so the
velocity attachment would otherwise report half a pixel of movement on a
scene standing perfectly still — and half a pixel is precisely the error
that ruins a history fetch. The block carries both offsets, this frame's
and last frame's, and the fragment subtracts each from its own term.

Which leaves a residual, and it is worth being exact about: two matrices
that agree mathematically, multiplied out separately, differ by around
1e-7 in NDC. That is a ten-thousandth of a pixel, below what the half
float the velocity is stored in can represent, so it stores as zero. The
*exactly zero* property the unjittered path has is not quite preserved;
what is preserved is that nothing downstream can tell.

**Eight offsets, and why not more.** A temporal filter that has to throw
its history away — a cut, a silhouette moving — starts from nothing, and
the shorter the phase the sooner it has covered the pixel evenly again.
Longer sequences converge finer and recover slower. Eight is the usual
answer, and it also bounds the index, which turns *frame 30 and frame 38
are drawn with the same offset* into something a check can assert — and
that assertion is the one a clock cannot accidentally pass, because a
time-driven jitter can be reproducible under `--frame-time` and still
have no period at all.

### The jitter as two settings, not two constants

Both of the numbers above were constants until somebody looked at a
corrected frame and said *it is less blurry than the characteristic TAA
and I am not sure it is even on*. It was on; what had gone was the ghost
7u describes, and the softness that came with it. But the question
underneath is a real one — **there was no dial for how soft the filter
is** — and feedback is not that dial. Feedback decides how much of the
past survives. How far apart the samples being averaged were taken is a
different quantity, and it is the one that sets the width of the
reconstruction filter.

So `TemporalJitterScale` and `TemporalJitterPhase` join `TemporalFeedback`
on the project, under the rule 7s sets: cost and quality belong to the
project, because they are a judgement about the machine.

| | what it changes | wider / longer | narrower / shorter |
|---|---|---|---|
| `TemporalFeedback` | how much history survives | cleaner still, more ghosting | sharper moving, noisier still |
| `TemporalJitterScale` | how wide the filter is | more of the pixel covered, softer | sharper, more left unsampled |
| `TemporalJitterPhase` | how long the sequence runs | converges finer | recovers sooner from a cut |

1.0 is the pixel's own area, which is what makes the accumulation converge
on the supersampled image the mode is aiming at, and it stays the default:
this exposes the knob, it does not move the setting. Above 1 samples
outside the pixel, which is a deliberate blur and the reason the range
does not stop there.

**The implementation risk is one line, and it is invisible.** The offset
is centred and *then* scaled. Scaling a point that had not been centred
first shrinks the offsets towards the pixel's corner rather than its
middle — a filter biased half a pixel down and to the left, everywhere,
in an image that still looks like a correctly anti-aliased image. So the
check asserts the property rather than the values: halving the width
halves the offset, doubling doubles it, and a width of zero is *exactly*
zero on both axes. Scaling before centring fails all three, which is how
it was confirmed the check can fail.

The phase has the same shape of hazard at the bottom of its range rather
than in its arithmetic: zero is a modulo by zero, and a `.rvproject` is a
file a person can type into. It is clamped in `TemporalJitterPhase`, one
place, and the check walks 1 to 16 asserting each repeats after exactly
that many frames — so *frame N and frame N + phase are the same picture*
holds at whatever the project asks for, not only at 8.

### The history, and the three things that reject it

**Where it lives.** Outside the graph, handed in. The graph pools its
targets by shape and hands out whichever is free, so a target has no
identity from one frame to the next — "the image I wrote last frame" is
not a thing it can express. `TemporalHistory` is a pair the caller owns,
ping-ponged, and `FrameDesc::History` is how it gets in. A caller that
supplies none gets **no jitter either**: jittering without somewhere to
accumulate is a wobble and strictly worse than not jittering, so the
absence of a history turns the whole mode off rather than leaving half
of it running.

One per *frame chain*, not one per process. The editor has two, because
its viewport and the game's are different sizes showing different
cameras, and a shared history would have each dragging the other's image
behind it.

The target written this frame **is** next frame's history, and the rest
of the chain reads it — so bloom and tone mapping consume the
accumulated image rather than the jittered one, and nothing is copied
anywhere. Bloom in particular matters: a threshold applied to a frame
wobbling by half a pixel flickers along every bright edge, and a glow
that shimmers is more obvious than the aliasing it was hiding.

**Where it runs.** On the linear HDR scene, in the same slot as the SSAA
resolve, and mutually exclusive with it. Same argument as §7o: averaging
is only meaningful where the numbers add up. This puts TAA on the
*supersampling* side of §7n's two definitions of correct, reaching for
the same ideal MSAA and SSAA reach for rather than the one FXAA and SMAA
are stuck with.

**What rejects the history**, in the order it matters:

1. **Reprojection**, or the filter is only correct for a still camera.
2. **Neighbourhood clipping** in YCoCg. The 3×3 box around the current
   pixel, and the history pulled into it. RGB would be worse and not
   marginally: the neighbourhood of an edge is a *line* through colour
   space, and an RGB bounding box around a red-to-blue transition
   contains magenta — a colour no pixel nearby has, and one the history
   is then free to keep. And **clipping, not clamping**: clamping each
   channel independently moves the colour to the nearest corner of the
   box, which can be nowhere near the line; clipping walks the segment
   from the box centre and stops at the face it leaves through, giving
   up only what it has to.
3. **A weighted mean.** Each side weighted by 1/(1+luma) before mixing.
   A single very bright sample dominates a linear average and flickers
   as it enters and leaves a pixel. No matching unweight step, and that
   is not an omission: with two samples the division by the sum of the
   weights *is* the unweighting.

The blend weight is a number in the inspector — `TemporalFeedback` —
because it is the ghosting-versus-flicker dial and there is no correct
value. A slow architectural fly-through and a first-person shooter
disagree about it and both are right. Clamped short of 1, which would be
a filter that never accepts a new frame: the image would freeze on
whatever it first accumulated and look, from outside, exactly like the
resolve having stopped.

### What TAA.4 does not yet do, and one number that says why

**The sky still writes zero velocity**, and the fix turned out not to be
a line of shader. The sky is infinitely far away so it has no motion of
its own, but it does move across the screen when the camera turns, and a
filter told otherwise reprojects it onto itself. Fixing that needs the
previous rotation-only view-projection in the sky shader — and that
block is **112 bytes of the 128 every Vulkan implementation guarantees**,
with a `static_assert` already guarding the same budget for the grid. A
mat4 is 64 more. So the fix is a uniform buffer for the sky's
parameters, which is its own change with its own risk, and it belongs
next to the moving-camera check that can see whether it worked.

**The vertical direction of the reprojection is the other one.** Velocity
is a difference of *clip* coordinates, whose y axis runs the same way on
both backends, while the resolve's v runs downward on one and upward on
the other — so the y component is negated on exactly the backend that
flips, by the same reasoning as every other fullscreen pass. That is
reasoning, not measurement: **the sign is unobservable on a static
scene**, because velocity is zero everywhere. It is measured by the
moving check in 7.10's last step, and until that lands this is the
weakest claim in the section.

Depth-based velocity dilation — taking the velocity of the nearest of
the neighbouring pixels, so a thin foreground object drags its own
motion rather than the background's — is a known improvement and
deliberately absent. It needs the scene's depth sampled by the resolve,
and it is a refinement of something not yet verified.

### The moving check, and the three answers it gave

`make_motion_scene.py` builds what 7r asked for: a textured patch falling
16 px a frame with an identical one standing still beside it, judged
against a 4x supersampled render of the same frame. Deterministic —
the block falls under the fixed-step solver, so frame 30 is frame 30 on
any machine.

**The first version of that scene could not have found anything.** It
was one uniform bright block on a dark sky, and it showed *no ghost at
all* with any reprojection, on either backend. The neighbourhood clip
rejects wrong-place history whenever the neighbourhood is uniform:
history from the wrong side of a flat bright block is outside the box,
history from the wrong side of flat background is outside the box, and
the clip discards both. **The defence masks exactly the mistake being
hunted.** A patch of small cells at different brightnesses has no
uniform neighbourhood anywhere, so wrong-place history is locally
plausible, survives the clip, and lands in the image.

**1. The reprojection's vertical sign is right.** 19.8 RMS as shipped
against 29.9 with the sign inverted, on both backends. That was the last
claim in TAA resting on argument.

**2. Catmull-Rom history sampling buys nothing here, and was reverted.**
The textbook argument is sound — moving content reprojects between
texels, so the history is resampled every frame, and bilinear compounds
into softness. Implemented, nine taps for one, measured: **19.830
against 19.774 moving, 3.817 against 3.816 still.** At this speed the
clip is already discarding the history the sharper kernel would have
preserved. Reverted, because a nine-times more expensive fetch has to
buy something, and the theory is not the evidence. It is worth
revisiting on *slow* motion — a pixel a frame or less — and that scene
does not exist yet.

**3. The default feedback was wrong, and that was the real finding.**

|            | moving | still |
|---|---|---|
| no filter  | 16.08 | 16.73 |
| 0.0        | 17.15 | 17.95 |
| 0.3        | **15.38** | 10.14 |
| 0.6        | 16.32 |  5.39 |
| 0.9        | 19.83 | **3.82** |

Still content improves all the way up; moving content bottoms out near
0.3 and is *worse than no filter at all* by 0.9. The default was 0.9 —
picked when every scene here was static, which is the one regime that
number is best in. It is 0.6 now: within a quarter of a unit of no
filter under motion, three times better than it standing still.

**And the guard written from it could not fail.** `check_taa_motion.py`
allowed TAA to be 1.25x worse than no filter under motion before it
complains -- a figure taken from measuring the inverted sign at a
feedback of 0.9, where it costs 1.86x. But the default is 0.6 now, less
history is used, and a backwards reprojection is correspondingly
cheaper: 1.21x. The check passed a build whose reprojection ran
backwards. Caught by flipping the sign again and re-running rather than
by reasoning, and the threshold is 1.10 now -- measured on both sides,
1.01 correct against 1.21 inverted. **A threshold is only worth having
at the setting it actually runs at**, and every one of them here should
be re-measured when the value it was derived from changes.

Worth naming the shape of this. Two of the three things measured were
hypotheses I was confident in — a biased blend weight, then a resampling
kernel — and both were wrong about *this* symptom. The one that was
right came from a sweep rather than a theory. The static scene had made
every one of these invisible for three steps of work.

### The sky's velocity, and a premise that was wrong

The sky reported no motion. It has none of its own — it is infinitely
far away — but it sweeps across the screen when the camera turns, so the
stated consequence was that a temporal filter reprojects it onto itself
and smears it.

Fixing it needed the previous rotation-only view-projection, which does
not fit: the sky's push-constant block is 112 bytes of the guaranteed
128 and a mat4 is 64 more. So it came from a small uniform buffer
alongside — 80 bytes, the matrix and the jitter pair — rather than by
moving the whole block, since the existing fields were fine where they
were.

**And then it measured nothing.** On a yawing camera with a cubemap sky,
against a supersampled reference: computing the velocity and forcing it
to zero produce **byte-identical frames**. So does forcing it to a wild
constant seventy-five pixels long. Not "nearly": zero subpixels differ,
on both backends.

The reason is the neighbourhood clip, for the third time in this
section. A sky is smooth from one pixel to the next, so the 3×3 box
around any sky pixel is nearly a point, and `ClipToBox` collapses
history fetched from *anywhere* back onto the current value. **The
defence was already preventing the smear the fix was written for.** The
premise — that the sky visibly smears under TAA — was never tested
before it was written down as a known gap, and it appears to be false
for any sky smooth at pixel scale.

Kept rather than reverted, and the distinction from the Catmull-Rom
revert is worth being explicit about. That one was nine times the fetch
cost for no measurable gain. This is one matrix multiply and an 80-byte
buffer, and it is *correct*: the attachment now says what it means. It
earns its place on **9.5 motion blur**, which reads the same velocities
and has no neighbourhood clip to save it — a sky reporting no motion is
a sky that stays sharp while everything else blurs, and that is not
subtle. Better a velocity that is right and unused than one that is
wrong and waiting.

Three times now, the same lesson in three costumes: **the neighbourhood
clip hides reprojection errors wherever the neighbourhood is locally
uniform.** A flat block hid the sign. A smooth sky hid the sky. Only
content with per-pixel detail can show either, which is why the falling
patch is textured and why any future check here has to be.

### The defect none of this caught, and why

Everything above was measured, on both backends, against computed ideals.
All of it passed. And the feature was still broken in a way anybody
using the editor would hit in the first minute: **changing the
anti-aliasing mode while the application is running corrupted the
frame.** Dark and enormously bloomed on Vulkan; on OpenGL, a mirrored
ghost of another image behind the scene.

One cause. `PostProcess` pools its descriptor sets by *position* — slot
four is whichever dispatch happens to be fourth — and the shaders here
do not share a binding layout: most declare one texture, tone mapping
and SMAA's blend declare two, the temporal resolve declares three. The
number of passes before tone mapping depends on the mode, so on the
frame the mode changes, slot four is a different shader than it was, and
a set built for a one-texture layout is handed to a two-texture one. It
silently drops the binding the old layout did not have. Tone mapping
loses its bloom texture and samples whatever is still bound there.

The code carried a comment saying the set "is rebuilt when the pipeline
it was made for is not this one". It did not do that. The comment
described the intent and the code implemented `if (!set)`. A comment is
not a test.

**Why every check missed it.** Each one starts the application in a
mode, renders, and exits. `check_smaa.py` runs six modes — as six
separate processes. `check_taa_jitter.py` runs twenty-six — all of them
separate processes. The pooled state that breaks only exists *within*
one process, across a change. Nothing in this repository had ever
changed a render setting while running, so a whole class of state
transitions had no coverage at all, and the number of checks said
nothing about it.

`CheckAntiAliasingSwitch` now renders every ordered pair of the six
modes into a real frame through one pool, and counts warnings. Verified
the way a check has to be: **reverted the fix, and watched it fail with
48 warnings** — because a check that has never failed is a check nobody
has tested.

Two smaller things fell out of the same report. The dropdown recorded no
undo command, so choosing a mode was invisible to the save system — the
scene was never marked changed, nothing prompted, nothing saved, and the
next launch read FXAA out of the file. And a switch between backends is
a *restart*, which is why it looked as though the backend was resetting
the filter: it was, by reloading the scene. The mode is now written to
`ragev.ini` on the click, beside vsync and the backend, which are the
same kind of judgement — about this machine rather than about the scene.

### Two lists of the same settings

`TemporalFeedback` was described by `RenderSettingsRegistry` and absent
from `SceneSerializer`'s hand-written environment block. So it was
editable in the inspector, reachable from a script, and **reset to its
default every time the scene was reopened** — and worse for the person
debugging, a scene file that set it was silently ignored, which made an
experiment testing three different values produce three identical
images. That wasted a diagnosis: the numbers said the blend weight had
no effect, and the reason was that the blend weight never arrived.

There was already a check saying "every described name has to be a key
the serializer writes". It named three fields by hand. A fourth was
invisible to it.

It is driven off the registry now: every scalar field is nudged off its
default, saved, reloaded and compared, so a setting added tomorrow is
covered without anybody choosing to cover it. Verified by deleting the
emitter line again and watching it name the casualty —
`TemporalFeedback (0.375000 -> 0.900000)`.

The deeper point is that two hand-written lists of the same thing drift,
always, and the drift is silent in both directions. The registry is the
one that knows what a setting *is*; anything else describing the same
settings should be generated from it or checked against it.

### A near-miss worth more than the feature

The way to prove a change is inert is to render the same frame with the
binary from before it and diff. The Release binaries happened to predate
the edit, so the reference could be taken for free — and it came back
**0 of 21,600,000 subpixels different on Vulkan and 100% different on
OpenGL, in all five modes, by up to 165.**

That reads as a catastrophic backend regression. It was not one. The
OpenGL reference images were 97% a single flat grey: `--screenshot-frame=30`
had caught the **loading screen**, because that was the first OpenGL run
of the day and a cold driver program cache had not finished compiling by
frame 30. There was nothing in those files to compare against.

The lesson is not "wait longer". It is that **a screenshot comparison has
to prove it captured the thing it claims to compare.** `check_smaa.py`
and `check_taa_jitter.py` are both safe, and not by luck: they fit the
edge and require the unfiltered staircase to measure 1/sqrt(12), which a
flat grey screen cannot do — the control that exists to validate the
*metric* also happens to validate the *capture*. The by-hand comparison
had no such control and would have reported a regression that was not
there. Any future before/after diff should assert something about the
content of the image before diffing it.

For the record the OpenGL evidence was obtained another way, which is
what it should have been from the start: `check_smaa.py`'s numbers are
unchanged to four decimals from the ones recorded in 7n and 7p, on both
backends, and those numbers are absolute — measured against computed
coverage, not against a stored image.

### And a defect this found

The velocity write from 7.10's first step had landed **inside
`ClusterIndexFor`**, the function that maps a fragment to its light
cluster. It worked, because that function is called exactly once per
fragment — and it would have gone on working until somebody added an
early-out to it, at which point the velocity attachment would hold
whatever the target's memory held, on some fragments, some frames.

It got there because the edit was made by a script matching on a comment,
and the comment it matched was in the wrong function; it also left that
function's own comment spliced into the middle of the velocity one, which
is the tell. Moved to the top of `main`, unconditionally, which is where
an attachment every fragment must write belongs.

---

## 7s. Where a setting lives (9.0): the project, the profile and the scene

Design first, and this one is entirely about **ownership**. Nothing here
changes a pixel. What it changes is which file a value is stored in, and
that decides how many places a person has to edit to change one thing.

### The state it replaces

`SceneEnvironment` is one struct with twenty-six fields, serialized into
every `.rage`. It holds three unrelated kinds of thing:

- **Cost knobs** — anti-aliasing and its samples, shadow cascades,
  resolution and distance. What this *hardware* can afford.
- **Look knobs** — exposure and the four bloom parameters. Authored
  content, chosen by whoever is grading the game.
- **Place** — ambient colour, the sky and its rotation. What this
  particular scene *is*.

All three were per scene, so a project with forty scenes stored its
exposure forty times and changed it forty times. Anti-aliasing was worse:
it was per scene **and** in `ragev.ini` as a machine-wide override, because
a viewing preference has to survive a restart without saving a scene asset.
Two homes, and the ini quietly won.

### What the roadmap proposed, and what the owner changed

The roadmap's 9.0 called for two assets, `.rvrenderprofile` and
`.rvpostprofile`, each a *sparse* layer over project defaults. The owner
revised it before any of it was built:

> Render setting should be per project basis (and no I am dropping the idea
> of render profile) and post profile should be optional and it should be an
> asset that can be attachable to camera component.

**Both changes remove work rather than adding it, and the second removes a
whole class of bug.** A sparse layer needs a per-field "is this set?" bit,
a UI that can render "inherited" distinctly from "happens to equal the
default", and a merge step; the roadmap said so and called it the part to
design for. A profile that is *attached to a camera* needs none of that,
because there is nothing underneath it to inherit from — a camera either
has a profile, and uses all of it, or has none, and uses the engine
defaults. Sparseness was only ever forced by the layering, and the layering
is gone.

Render settings stop being an asset for a simpler reason: there is exactly
one machine running the editor and one quality level being authored. A
project field plus the existing `ragev.ini` override covers it, and a
quality *preset* — the thing an asset would have bought — is a player-facing
feature nothing here has asked for.

### The three homes

| Home | Holds | Read by |
|---|---|---|
| `.rvproject` → `RenderSettings` | AA and its three parameters, shadows | The frame graph, once per frame |
| `.rvpostprofile` → `PostSettings` | Exposure, bloom, **and all of 9.1–9.7** | The camera that names it |
| `.rage` → `SceneEnvironment` | Ambient, sky, sky rotation, sky texture | The scene pass |

And two override chains, both short:

    render:  project  →  ragev.ini / --aa=
    post:    engine default  →  the camera's profile, if it has one

The post chain has no project layer on purpose. "No profile" already means
something perfectly definite — the struct's defaults, which is what every
scene renders with today — and a project-wide post block would be a second
answer to the same question, discoverable only by whoever knew to look.

### Why the camera and not the scene

Because a post profile is *how this view is graded*, and a scene can hold
several views. The editor already renders two cameras of the same scene at
once — the viewport's and the game's — and a scene-level grade cannot tell
them apart. Attaching it to `CameraComponent` also lands on the thing that
eventually blends: a post *volume* is a trigger that swaps or lerps the
camera's profile as it moves, and that is a component reading a field
rather than a redesign. This is the roadmap's own open question about
volumes, answered by putting the field where a volume would write to it.

**The editor viewport resolves through the scene's primary camera.** Not a
separate editor-only setting: the viewport is meant to show what the game
shows, and a grade you cannot see while authoring is a grade you author
blind.

### The part that decides whether this is maintainable

`RenderSettingsRegistry` already described every one of these fields — name,
type, range, help — and the scene serializer wrote them out by hand anyway.
They drifted: `TemporalFeedback` was in the registry, in the inspector and
in the struct, and **not** in the serializer, so it silently reset to its
default on every load and three scene files that set it were all rendering
the same picture. That cost a wasted diagnosis (§7r).

Splitting one struct into three multiplies the number of hand-written lists
by three, which is that bug three more times. So:

**Every one of these blocks is read and written through its registry.**
`SettingsRegistry::Fields()` returns a `FieldDesc` list per block, and one
pair of functions emits and reads a YAML map from any of them. The scene
serializer, the project file, the `.rvpostprofile` and the C# bridge all
go through it. A field added to a struct and its registry is on disk, in
the inspector, and reachable from C# with nothing else touched — and a
field added to the struct and *not* the registry is invisible everywhere,
which is a failure that shows up on the first save rather than months later.

The checks enforce the shape rather than the list: for each block, set
every registered field to a non-default value, round-trip, compare. A field
the serializer cannot carry fails that, whatever it is called.

### What happens to the scenes already on disk

Scene version 5 → 6. A version-5 scene still carries the render and post
keys, and the loader still reads them — but only to **report** them:

    scene 'x.rage' (v5) sets AntiAliasing, Exposure, BloomIntensity, which
    moved in version 6. AntiAliasing is now in the .rvproject; Exposure and
    BloomIntensity are now on a .rvpostprofile attached to a camera. The
    values in this file were not applied.

Reported rather than migrated, and reported rather than dropped. Migrating
would mean a scene load silently editing the project file or minting an
asset, which is the kind of thing that is impossible to undo and unpleasant
to discover; dropping is what `TemporalFeedback` did. Only keys whose value
*differs from the default* are named, so the dozens of generated test scenes
that write `AntiAliasing: 0` because the generator always has do not produce
a warning that means nothing.

### The one thing that had to change outside the engine

`tools/scripts/make_motion_scene.py` writes `BloomEnabled: false`, because
bloom spreads a moving block's edges across exactly the pixels the TAA
measurement is looking at. After this change that key is inert and bloom
would be **on** — so the check would still run, still produce numbers, and
the numbers would be measuring something else. A generator that has to
disable an effect now writes a `.rvpostprofile` beside the scene, with its
own `.meta` so the handle is fixed rather than minted on first scan, and
points the camera at it. That is the same path a person uses, exercised by
the test suite on every run.

### What splitting the ownership cost, and what pays for it

Reported after 9.1 shipped, as *"I pick the post profile for the scene
camera, apply a LUT to it, restart the editor, and it is gone."*

Both halves of that gesture had done exactly what they were built to do.
The LUT was written to the `.rvpostprofile` the instant it was set — the
file on disk carried it. The camera's *reference* to the profile is scene
data and was waiting for Ctrl+S, which never came. On restart the camera
had no profile, so the grade drawn underneath it disappeared with it, and
the whole thing read as "neither was saved".

**One gesture, two persistence schedules.** That is the price of the
ownership split on this page, and it is not a bug in either half. The
tempting fix — save the scene too, on the edit — is worse than the problem:
a scene that writes itself is a scene you cannot experiment in, and play
mode is snapshot-then-restore precisely so that trying things is free.

What was actually missing is that **nothing said so**. The editor's
unsaved-changes mark asked `CommandStack::CanUndo()`, which is true from
the first edit of a session until the scene is closed — so it stayed lit
through every save and carried no information; and closing the window took
the scene with it without a word.

So the mark now asks a real question and the window has a door:

- `CommandStack` tracks a **position**, not a flag: how many scene-touching
  commands are on the undo stack, against what that count was at the last
  save. A flag cannot tell that undoing back to the save point has made the
  scene clean again. The saved position goes to −1 — permanently dirty —
  when it becomes unreachable, which happens two ways: a new edit discards
  the redo branch the save point was on, and a command ageing off the
  bottom of the bounded stack. Both are "no sequence of undos reaches the
  saved scene", and guessing *clean* there is how an editor loses work.
- `EditorCommand::TouchesScene()` exists because one thing on that stack is
  not a scene edit. The render settings live on the `.rvproject` and are
  written the moment they change; counting them would light the mark for
  something already on disk.
- Closing, New Scene and Open Scene all go through one prompt —
  Save / Discard / Cancel. `WindowCloseEvent` had to move to **after** the
  layer walk in `Application::OnEvent` for this to be possible at all:
  `EventDispatcher::Dispatch` marks the event handled from its callback's
  return value, and `OnWindowClose` returns true, so the loop broke at the
  topmost overlay and the editor layer was never asked. Anything a layer
  may need to veto has to reach the layers before it is acted on.

The prompt also names the split rather than hiding it: entities, components
and the environment are in the scene file; profiles and materials are
assets and have already saved themselves. That is the one sentence that
makes the two schedules make sense instead of look broken.

`CheckSceneDirty` in `tools/scenetest` pins the cases a boolean would get
wrong — undo back to the save point, redo past it, and a new edit from
behind it.

---

## 7t. Colour grading (9.1): design first, because one of the decisions is invisible

A 3D LUT is a lookup: the frame's colour is a coordinate, and the table says
what that colour becomes. It is how every grading tool in the world exports a
look, which is the whole reason it is the largest look-per-cost item in phase
9 — the work is not inventing a colour transform, it is *accepting one*
somebody else authored, unchanged.

Three decisions, and the third is the one that will otherwise cost a week.

### 1. Where in the chain it applies: **after the tone curve**

The frame reaches the tonemap pass in linear HDR, is compressed by ACES, and
becomes display-referred. A LUT can go on either side of that, and the two
are not interchangeable:

- **After** — the LUT is fed values in [0, 1] that mean what a display shows.
  This is what a `.cube` exported from Resolve, Lightroom or Photoshop was
  authored against, so any LUT from any tool does here what it did there.
- **Before** — the LUT is fed unbounded linear values, which have to be
  squeezed into [0, 1] by a *shaper* first. That is a real workflow (it is
  what "log LUT" means) and it is strictly better for highlights, because
  the LUT can still see detail the tone curve is about to crush. It also
  requires whoever authored the LUT to have used the same shaper the engine
  uses, and to know that they did.

**After.** The point of this item is that a look authored elsewhere arrives
intact; a pipeline where two thirds of the LUTs on the internet come out
wrong is not that. The cost is stated rather than hidden: **a LUT here cannot
recover highlight detail ACES has already compressed.** A shaper-based path
is a later addition, not a correction of this one — they are different
features with different inputs.

### 2. The format: `.cube`, and 16-bit float on the GPU

Adobe/IRIDAS `.cube` is text, ubiquitous, and takes about forty lines to
parse: `LUT_3D_SIZE n`, optional `DOMAIN_MIN`/`DOMAIN_MAX`, then n³ RGB
triples with **red changing fastest** — index `r + g*n + b*n*n`. Getting that
order backwards produces a LUT that is a plausible-looking wrong grade, so it
is asserted rather than assumed.

The texture is `R16G16B16A16_SFLOAT` rather than RGBA8. A LUT is sampled
trilinearly and then written to an 8-bit target, so 8-bit table entries
quantise *before* interpolation and band in smooth gradients — the exact
place a grade is looked at. At 33³ the float version is 143 KB, which is not
a number worth optimising.

### 3. The invisible one: **an identity LUT has to be a no-op, exactly**

A 3D LUT is sampled at texel *centres*, so a colour c in [0, 1] maps to

    uv = (c * (N - 1) + 0.5) / N

and not to `c`. The difference is half a texel — at N = 32, about 1.5% of the
range.

**Nothing about a wrong scale looks broken.** The image is still graded, still
smooth, still plausible; it is just not the grade that was authored, by a
little, everywhere, forever. There is no artefact to notice and no frame to
point at. It is the same shape of defect as the reprojection sign in §7r,
which a static scene could not show: the failure hides inside a result that
looks fine.

So the check is not "does grading change the picture". It is:

> **An identity LUT must leave the frame byte-identical to no LUT at all.**

That is a property with one right answer, it fails loudly on any scale or
offset mistake, on either backend, and it costs one generated file and one
comparison. Everything else about this item is ordinary work; this is the
part that decides whether the ordinary work is correct.

A second check measures a LUT with a *known* transform — a channel swap,
which is exact under trilinear interpolation because it moves table entries
rather than blending them — so "the LUT is sampled at all" and "the LUT is
sampled correctly" are two separate failures rather than one.

### What it costs in the RHI

A third texture type. `TextureDesc` grows a `Depth`, read only for
`Texture3D`: reusing `Layers` would have been fewer lines and is wrong for a
reason worth writing down — **an array's layers are not filtered across and a
3D texture's depth is.** Conflating them yields a LUT with no interpolation
along blue, which is, again, a grade that looks fine and is not the one that
was authored.

OpenGL is mostly free: `GL_TEXTURE_2D_ARRAY` already goes through
`glTexImage3D`, so only the target and the R-axis wrap differ. Vulkan needs
`VK_IMAGE_TYPE_3D` and `VK_IMAGE_VIEW_TYPE_3D`, and its depth extent is the
`extent.depth` that array layers deliberately do not use.

---

## 7u. One process, two frame chains: the ghost over the Game view

Reported as *"a faint second copy of the whole scene over the game view, and
I can zoom it with the scroll wheel"*, and cracked by the owner's next
observation: **it only happens with TAA**.

### What it was

`Renderer3D` kept **one** previous view-projection for the whole process,
written at the end of every `BeginScene` and read at the start of the next.
The comment defending that said, in its own words:

> The scene pass is the last caller in a frame, so what the scene pass reads
> is what the scene pass wrote — which is the property that makes this
> correct without tracking whose camera it was.

That property is true of the runtime, which draws one scene per frame. It is
false of the editor, which draws the **viewport** and the **game view** in
one frame from two different cameras. The order is viewport first, game
second, so:

| | reads as "last frame" | should have read |
|---|---|---|
| Viewport chain | the game camera, last frame | itself, last frame |
| Game chain | **the editor camera, this frame** | itself, last frame |

Every pixel of the game view therefore carried a velocity equal to the *gap
between the two cameras*, and the TAA resolve did exactly what it is told to
do: fetch the history from where the velocity points. A whole frame,
displaced, blended in at feedback 0.6. Faint, complete, and sliding about
whenever the editor camera moved — which is why it answered to the scroll
wheel. Nothing in the game view should answer to that input, and that alone
was enough to say the picture was being built from the wrong camera.

### The rule it broke

`TemporalHistory` already carries the argument, written when it was built:

> One of these per *frame chain*, not one per process. The editor builds two
> frames from the same scene, its viewport and the game's, and they are
> different sizes showing different cameras; a shared history would have each
> dragging the other's image behind it.

The matrix that **reprojects into** that history has exactly the same scope.
Splitting the image per chain and leaving the matrix global is half a fix,
and the half that was missing is invisible in every still frame — the ghost
only separates from the picture once the two cameras differ.

So `CameraMotion` (view-projection + jitter) moved into `TemporalHistory`,
and the frame graph hands it to the scene draw on the same edges it already
sets the jitter on, for the same reason: only the code drawing *this* chain's
scene may difference a camera against last frame's. Anything else gets null
— a probe capture's six faces, a shadow cascade, a chain with temporal
filtering off — which differences against itself and yields the zero velocity
the attachment is already cleared to. That also retires the fragile ordering
property the old comment leaned on: nothing now depends on who called last.

### Why no check caught it

`check_taa_motion.py` renders a moving scene in the **runtime**, and the
runtime has one frame chain. A single chain cannot express this failure —
the same shape of blind spot 7r describes for a static scene and the
reprojection sign, one level up: there, one frame could not show it; here,
one *chain* cannot.

The measurement that does show it needs two chains and two cameras, and it is
a strong one because it is exact: render the editor twice, changing **only
the Viewport panel's size** — which changes the editor camera's projection
and nothing else in the frame — and compare the Game panel.

    before   max 87/255 over 7,534 subpixels
    after    0 differing subpixels of 267,575

Byte-identical is the right bar. The game view is a different camera looking
at the same scene; no amount of editor-side layout may perturb one pixel of
it. A tolerance here would accept exactly the leak this exists to catch.

### The other thing this turned up

The first run of `check_smaa.py` after the fix reported TAA at 1.0x, 0.7x,
0.7x, 0.6x — "either the history is not being blended in, or it is being
rejected everywhere". It was neither. `SampleProject.rvproject` had
`TemporalFeedback: 0` in it, and those four numbers are the **f = 0.0 row of
the table in that check's own source**, reproduced to two decimals.

Which is to say the check was right, its diagnosis was right, and it was
describing the configuration rather than the code. The feedback lives in the
project, there is no `--feedback=`, and so the check measures whatever the
`.rvproject` happens to say on the day. It now reads that value, prints it,
and refuses to run if it is not the one the 1.25x threshold was calibrated
at. 7r's rule — "a threshold is only worth having at the setting it actually
runs at" — applies to the setting as much as to the threshold.

**How the zero got there is not known, and the guessing is worth resisting.**
Every commit has 0.6, so it was written into the working tree and never
committed. Ten attempts to reproduce it all left the file byte-identical: a
plain editor run, `--aa=none`, `--aa=taa`, a run at a smaller window size,
each of the two AA checks alone, and both of them concurrently. The only code
that writes a `.rvproject` at all is the editor's Render Settings panel,
which saves the moment `DrawFields` reports a change — so the shape of the
suspect is "something made a widget report a change that nobody made", and
that is as far as the evidence goes. Recorded in HANDOFF as open rather than
written up here as understood.

The guard above is the part that does not depend on knowing: whatever writes
it next, the check announces it instead of reporting a broken renderer.

---

## 7v. Authoring a look: the `.rvlut` recipe, and why a `.cube` stays read-only

9.1 could *use* a grade and not make one. Reported as "there is no way to make
a new LUT or even edit it", which is exactly right: the only route to a look
was to author it in another program and import the file.

### Why a `.cube` cannot simply be made editable

A `.cube` is a **baked table** — 33³ = 35,937 RGB triples, one per cell of the
colour space. There is nothing to put in an inspector. "Editing" one means
editing the *transform that produced it*, and that transform is not in the
file: it left the building when the table was baked.

So the two things are separated, because they genuinely are two things:

| | `.cube` | `.rvlut` |
|---|---|---|
| Is | a baked table somebody authored elsewhere | a recipe: the knobs and their values |
| Editable | no — read-only imported data | yes, and that is its whole purpose |
| Becomes a texture by | being parsed | being **baked**, at load |

Both are `AssetType::ColorLut`, deliberately. The *thing* a camera's profile
points at is "a colour lookup", and whether it arrived baked or is baked on
the way in is not the profile's business. One picker lists both, one field
holds either, and `GetColorLut` dispatches on the extension. The alternative —
two asset types — would push that distinction into every place that names a
LUT, to no benefit.

### The order of operations, and why it is stated

The recipe is applied **display-referred**, after the tone curve, because that
is where the LUT is sampled (7t). So the operations are the ones that mean
something on encoded values in [0, 1], in this order:

1. **White balance** — temperature and tint, as a per-channel multiply.
2. **Lift, gamma, gain** — per channel, the standard three-band form.
3. **Contrast** — about a 0.5 pivot.
4. **Saturation** — toward Rec.709 luma.

Order matters and is not a matter of taste: contrast before saturation and
after gain is what makes the knobs behave the way a colourist expects, and
swapping any pair produces a different look from the same numbers. Written
down here because the next person to add a knob has to know where it goes.

### The property that makes it checkable

**A recipe at its defaults must bake the identity table, exactly.** Not
nearly: byte-for-byte, so that attaching a fresh `.rvlut` to a camera changes
nothing at all until a knob is moved.

That is not automatic. `pow(x, 1.0f)`, `(x - 0.5f) * 1.0f + 0.5f` and
`mix(luma, x, 1.0f)` are all *mathematically* the identity and none is
guaranteed to return `x` bit-exactly. So each stage **early-outs at its
default value** rather than computing a no-op, and the identity holds by
construction instead of by hoping the rounding goes the right way.

**And the table size decides whether that matters — which nearly made the
check worthless.** Written first at the default size of 33, it passed with the
early-out removed. At 33 the step is 1/32, so every coordinate is a dyadic
rational and the contrast arithmetic *is* exact by luck. Measured across
sizes:

| size | step | coordinates that fail the round trip |
|---|---|---|
| 33 | 1/32 | 0 of 33 |
| 17 | 1/16 | 0 of 17 |
| 20 | 1/19 | 2 of 20 |
| 25 | 1/24 | 3 of 25 |
| 64 | 1/63 | 11 of 64 |

So `CheckLutRecipe` bakes at 33, 20 **and** 64. With the early-out removed it
now passes at 33 and fails at the other two — which is the difference between
a check and a reassurance, and the same lesson as 7r's threshold and 7t's
half-texel: a property has to be tested where it can break, not where it
happens to hold.

### Every setting persists, and that is a rule now

See 7s for *where* each one lives. What 7s did not say, and what this item
made worth saying, is that **there is no such thing as a setting that only
exists at runtime**:

- render settings → the `.rvproject`, written the moment they change;
- post profiles and LUT recipes → their own asset files, written on edit;
- environment → the `.rage`, on Ctrl+S, with the prompt that 7s describes;
- panel layout, theme, window size, backend, vsync → `ragev.ini` and
  `panels.ini`, on exit.

A control that a person can move and the engine then forgets is a bug, not a
design choice. If a new knob has no home in that list, it is not finished.

---

## 7w. Lens and film (9.3): three effects, and the one that breaks the checks

Vignette, chromatic aberration and film grain. The roadmap calls this small
and says "the risk is taste, not time", which is true of two of them.

### Where each one goes, and why it is not a matter of taste

They are three different things pretending to be one item, and each belongs at
a different point in the chain:

| | Models | Runs |
|---|---|---|
| Chromatic aberration | a lens dispersing wavelengths *before* the sensor | first, on the linear HDR sample |
| Vignette | less light reaching the corner of the frame | still linear, before the tone curve |
| Film grain | the texture of the recording medium | last, after the curve **and** after the LUT |

**Vignette before the curve** is the one worth arguing. Applied afterwards it
multiplies display values and reads as a painted-on shadow; applied before, it
is less light arriving, which is what a vignette *is* — so the corner rolls
off through the same response curve the rest of the frame does, and darkens
the way a real underexposure darkens.

**Grain after the LUT**, because grain is not a colour anybody graded. Put it
before and the LUT re-maps the noise: a heavy grade then amplifies it in the
range it stretches and crushes it in the range it compresses, so the grain
changes character with the look rather than sitting on top of it.

**Chromatic aberration does not disperse the bloom.** It needs three taps of
the scene at three offsets, and bloom is a separate texture; dispersing it too
would cost two more taps to shift something already blurred wider than the
offset. Stated because "why is bloom not aberrated" is a reasonable question
with a boring answer.

### The one that breaks the test suite

Grain is **animated noise**, which makes the frame a function of *when* it was
drawn. That is the same hazard 7r hit with TAA's jitter and 9.2 will hit with
auto exposure: every screenshot comparison in this repository assumes that
rendering frame 30 of a scene twice produces the same bytes.

So grain is seeded from the **frame number**, exactly as the jitter is, and
for exactly the same reason — a clock is reproducible under `--frame-time`
right up until the machine is busy, and then it is not. `Renderer::GetFrameCount()`
is already reset by the main loop so the count does not depend on how many
frames loading took.

Two consequences worth writing down:

- Frame 30 and frame 30 are the same picture; frame 30 and frame 31 are not.
  Both are asserted, because only the pair together says "animated *and*
  deterministic".
- Grain lands after the tone curve, and TAA resolves before it, so the
  accumulation never sees the noise. That is the right order — grain averaged
  over eight frames is grain nobody can see — and it is a property of where
  the passes sit rather than a decision this item made.

### Defaults are exactly off

All three default to zero and each **branches out** rather than computing a
no-op — `mix(1.0, v, 0.0)`, `uv + vec2(0.0)` and `+ 0.0 * noise` are all
mathematically nothing and none is a guaranteed bit-exact nothing on every
compiler and both backends.

That keeps the property 7t and 7v already rest on: a profile with these three
untouched renders the same bytes as a build without them, so
`check_lens_effects.py` can assert byte-identity against `--aa=none` and every
existing screenshot check stays valid unchanged.

---

## 7x. The grain again: what "unnatural" turned out to be

9.3 shipped grain that was correct in every way the checks could see and
looked wrong anyway. The report was one sentence — *"a bit unnatural, maybe
Perlin noise would help"* — and the instinct was right about the biggest of
**three** separate defects, only one of which was about the noise function.

| | Was | Is |
|---|---|---|
| Shape | `hash(floor(coord / size))` | two octaves of value noise |
| Response | `1.0 - luma * 0.5` | `sqrt(4L(1-L))` |
| Colour | one value for all three channels | finest octave per channel |

### The response curve was upside down

`1.0 - luma * 0.5` is **1.0 at black** and 0.5 at white, under a comment
saying it existed "so the darkest parts of the frame stay clean". The comment
described the intent and the arithmetic did the opposite, and the result —
loud shadows, quiet highlights — is precisely the signature of digital sensor
noise rather than film.

Film grain peaks in the midtones and vanishes at both ends, because there is
no variation left to show once nothing is exposed or everything is. The square
root of the parabola rather than the parabola itself: `4L(1-L)` alone is too
narrow and leaves a visibly clean band either side of mid grey.

**Nothing in the suite could have caught this**, and that is the part worth
keeping. Every screenshot check builds on the AA scene, which is deliberately
two flat levels — that is what makes an edge measurable. Two levels cannot
show that an effect varies with brightness, so an inverted response looks
completely normal on it. The fix was a scene with an actual tone ramp in it:
eight emissive slabs whose *rendered* luma spans 0.05 to 1.00, with the
emissive values chosen by inverting ACES rather than by spacing them evenly,
because evenly spaced emitters all land in the top half.

### Value noise, and specifically not Perlin

Gradient noise is the obvious reading of "use Perlin" and is the wrong choice
here for a concrete reason: **Perlin noise is exactly zero at every lattice
point**, so a field of it carries a regular grid of grain-free dots — the
artefact being replaced, rotated 45 degrees. Value noise interpolates *between*
lattice values and has no such structure.

Two octaves at a frequency ratio of 2.17 rather than 2, so the two lattices
never line up and hand the grid back. A film stock is a distribution of
crystal sizes, not one size, and that is the whole reason for a second octave.

The finest octave is **per channel** and the clumping is shared. Colour
negative has three emulsions that do not grain together, but they do not clump
independently either — and doing all three octaves per channel would have
tripled the cost of the pass for something that measures as a channel
correlation of 0.95 either way.

### An integer hash, because two backends have to agree

`fract(sin(x) * 43758.5453)` is the usual one-liner and it is not
reproducible: the precision of `sin` is implementation-defined, the arguments
are large, and the answer lives entirely in the low bits. lowbias32 is
integer multiplies and shifts, which wrap identically everywhere. The lattice
value is taken from the top 24 bits so that the conversion to float is exact
rather than rounded — hashing in integers and then throwing away exactness on
the last line would waste the previous six.

### The amplitude had to be measured, not guessed

Interpolating between lattice points narrows the distribution and stacking
octaves narrows it again, so the field's standard deviation is **0.1392**, not
the 0.2887 of the uniform hash it replaced. That number came out of a numpy
replica of the exact construction before any shader was written, and it is
what the normalising constant divides by.

The amplitude was then set *lower* than the old field's on purpose. Noise with
structure reads as considerably stronger than noise without at the same RMS,
because the eye is far more sensitive at the size of a clump than at the size
of a pixel.

`FilmGrainSize` changed meaning slightly and its default moved from 1 to 2. It
is now the lattice period rather than a quantisation step, so fractional
values work where they previously rounded; and at 1 the finest octave is past
what the pixel grid can resolve, so it sharpens into noise instead of showing
specks — which is the look this item existed to leave behind.

### Two things this cost that were not the feature

**Both `shared` and `common` are reserved words in GLSL**, and glslang reports
the error on the line *after* the offending one. Two rebuilds.

**A tonemap that fails to compile makes "off is off to the byte" pass.** Every
variant is equally broken, so they are equally identical. The suite caught it
regardless — through the "each effect does something" half, which exists as
the mirror of the byte-identity claim for exactly this reason. It is the same
pairing as animates/reproduces and, now, clumps/decorrelates: **each of these
claims is passable alone by a specific failure, and only the pair is not.**

| Pair | What passes one alone |
|---|---|
| off is exact / each effect lands | a shader that does not compile |
| animates / reproduces | a clock; a constant pattern |
| adjacent pixels differ / neighbours correlate | white noise; a blur |

The third pair is new, and the second half of it does not do the work: a
2-pixel block grid correlates **+0.49** at one pixel, indistinguishable from
value noise's +0.51. What separates them is that a block field is piecewise
constant — 51% of horizontally adjacent pixels are *exactly* equal, against
5.7% for value noise. Both halves were verified by putting the old grain back
and watching the new assertions fail.

---

## 7y. Auto exposure (9.2): the design, before any of the code

The roadmap says this one "makes the tonemap frame-dependent, so every
screenshot comparison in the repo needs a way to pin it", and the handoff
listed three candidate pins. **All three were answers to the wrong question.**

### The pin already exists, and it is not a new flag

`--frame-time` substitutes a fixed delta for the measured one, and its own
comment states the consequence: *"Everything downstream of this — particles,
OnFrame, the interpolation alpha — becomes a function of the frame number
rather than of how busy this machine was."* An adaptation driven by **that**
delta is already a function of the frame number. Every screenshot check in
this repository passes `--frame-time=0.016666` today.

So the rule is one line, and it is a rule about where the number comes from
rather than a feature: **the adaptation reads the frame time the loop hands
down, never a clock of its own and never real elapsed time.** A new
`--exposure=fixed` flag would have been a second thing every check had to
remember, to solve a problem the first flag already solves.

The other two candidates were worse for concrete reasons. Adapting as a pure
function of the *frame number* makes the adaptation twice as fast at 120 fps
as at 60, which is wrong for a game and would have had to be undone later. A
project setting the checks opt into is the `--exposure=fixed` flag with extra
steps and a file to keep in sync.

### And the real protection is that it is off

`AutoExposure` defaults to **false**, and off is *exact*: no compute is
dispatched, and the tonemap takes the manual exposure unchanged. That is the
same guarantee 9.3 rests on (7w) and it is what keeps `check_smaa`,
`check_color_grading`, `check_taa_*` and `check_lens_effects` valid without a
line of change in any of them. The hazard in the roadmap entry is real and it
is entirely confined to scenes that ask for the feature.

### The state is per frame chain. This is 7u again

The adapted luminance is **one value per frame chain**, living beside
`TemporalHistory` and owned by whoever owns that.

This is not a precaution, it is the same bug 7u already cost a day to: the
editor draws the viewport and the game view in one frame from two different
cameras. A process-wide adapted value would be written by whichever chain drew
last and read by both, so a bright game view would darken the viewport and the
viewport would brighten it back — the two panels pulling one number in
opposite directions, every frame, forever. The ghost was that shape and so is
this. **Anything in the renderer that remembers something about "last frame"
belongs to a chain, not to the process.**

### A histogram, not a log average

A log-average luminance is one dot product and is dragged around by a handful
of pixels: the sun in frame, a specular highlight on wet metal, one emissive
sign. The image then breathes whenever the camera turns past something bright,
which is the artefact people mean when they say auto exposure "pumps".

So: 256 bins over a log2 luminance range, and the exposure comes from the
average of the bins between a low and a high percentile. Discarding the tails
is the whole point — it is what makes the measurement about the *scene*
rather than about its brightest object. The bin count is a compromise nobody
will notice either side of; the percentile window is a control worth exposing.

### The manual slider becomes compensation

`Exposure` does not become dead when auto is on — it **multiplies** the
computed value, which turns it into exposure compensation, the same control a
camera has for the same reason. A scene the metering gets consistently wrong
is a scene somebody wants to push a stop, and taking the slider away would
mean the only fix was switching the feature off.

### Convergence starts converged

The first frame of a chain, with nothing remembered, **adopts the measured
luminance** rather than adapting toward it from a default. Two reasons, and
the second is the one that matters here:

- A level that opens two stops wrong and slides into place over a second is a
  bug that every player sees and nobody asked for.
- It makes a screenshot stable at *any* frame rather than only well past the
  time constant, which is what keeps the check simple. Frame 30 is not
  mid-transient; it is converged, because frame 0 was.

Adaptation is then only visible when the scene changes, which is when it is
supposed to be visible.

### Where each claim gets tested

The split follows what kind of thing each claim is, and it is the split this
repository already uses everywhere else:

| Claim | Tested in | Why there |
|---|---|---|
| The adaptation law: framerate independence, rate, clamps | `scenetest` | It is arithmetic. A pixel is a terrible way to measure `1 - exp(-dt * rate)` |
| Off is byte-identical; a bright scene darkens; a dark one brightens; a shot reproduces | a check script | It is plumbing, and only pixels prove the value reached the shader |

**Framerate independence is the assertion worth writing first**, because it
is the one that fails silently: one step of 0.1 s and ten steps of 0.01 s must
land on the same value, and a naive `lerp(current, target, rate * dt)` does
not — it is a first-order approximation that diverges exactly when the frame
rate does.

### What it costs elsewhere: the graph learns compute

The histogram reads the scene HDR target, which the render graph owns and
pools, and **every graph pass today calls `BeginRenderPass` unconditionally**
— so there is nowhere to put a dispatch. Compute inside a render pass is
illegal on Vulkan, and reading a pooled graph target from outside the graph is
the thing the graph exists to prevent.

So the graph gets a pass kind with no attachments that skips the render pass.
That is a prerequisite rather than a detour: **9.5 motion blur, 9.6 SSAO and
9.7 SSR all want the same thing**, and the alternative — running the histogram
against *last* frame's image from outside the graph — buys one frame of
staleness and an image the graph is not allowed to pool, to avoid work that
three later items would have to do anyway.

The RHI half already exists from 6.7a and is proven on both backends by the
GPU particle path, barriers included: `BufferBarrier` with
`ComputeWrite -> ShaderRead` is exactly the edge this needs.

### No readback in the render path

The adapted value stays in a GPU buffer that the tonemap pass reads. Nothing
is mapped back to the CPU to decide what to draw, because a readback is either
a stall or a value whose age depends on how far ahead the GPU is — and the
second one would put the frame's appearance back under the scheduler, which is
the entire thing this section is about avoiding.

The editor may read it back **for display**, one frame stale, because a number
in a panel is allowed to be a frame behind and the render is not.

---

## 7z. Depth of field (9.4): where it sits, and what it is allowed to read

The roadmap calls this "a circle of confusion from depth, then a separable
bokeh blur" and adds that "the depth buffer is already a graph resource". Two
of those three claims needed revising before any code.

### The depth buffer was a graph resource nobody could read

`RGTargetDesc` has a `SampleDepth` flag and the scene target has never set it,
so the depth attachment is written and then unreachable. Colour is always
sampleable; depth costs an extra usage flag and until now nothing wanted it.

It is now **on unconditionally**, rather than only when depth of field is
enabled, and that follows the rule the velocity attachment already established
one item earlier: *a target whose shape depends on a setting is a target every
pipeline and every reflection probe has to agree with about that setting too*,
and 7q is the record of how that goes. The cost is one usage flag and, on some
hardware, giving up a depth compression mode. The alternative is a target that
reallocates when a checkbox moves.

### It runs after the anti-aliasing resolve, not before

The tempting place is immediately after the scene pass, where the colour and
the depth are the same target at the same size. It is the wrong place, and the
reason is TAA: reprojection follows motion vectors that describe where the
*sharp geometry* went, and running it over an already-defocused image asks the
neighbourhood clamp to reconcile a blur with a history that was blurred
differently. Blurring after the accumulation is what every temporal pipeline
does, and for this reason.

So depth of field sits between the resolve and bloom — which also means bloom
blooms the defocused image, which is the right way round: a bright out-of-focus
highlight should glow as the disc it has become, not as the point it was.

That leaves it reading depth from a target it is no longer the same size as.
Under SSAA the depth is supersampled and the colour is not; under TAA the depth
carries the frame's sub-pixel jitter. Both are handled by sampling depth with
**normalised coordinates** rather than texel indices — the resolution mismatch
disappears, and half a pixel of jitter is half a pixel of error in a radius
measured in whole ones.

### A real lens, because the controls are the ones people already know

The circle of confusion comes from the thin-lens equation rather than from a
`FocusRange` slider somebody tunes by eye:

```
CoC = |z - d| / z  *  f² / (N * (d - f))
```

with `z` the linear depth of the pixel, `d` the focus distance, `f` the focal
length and `N` the f-number — then divided by the sensor height and multiplied
by the frame's height to land in pixels.

The argument for the physical form is the same one the vignette made in 7w:
the controls become the ones a photographer already has, f/1.4 does what f/1.4
does, and the relationship between them is not something anybody has to
rediscover per scene. The argument against — that it is four numbers where two
would do — is real, and is why the sensor height is a constant rather than a
fifth.

### The artefact to design against is bleeding, not cost

The failure that makes depth of field look like a filter rather than a lens is
a blurred *background* leaking over a sharp foreground edge: a gather at an
in-focus pixel reaches out, finds defocused pixels behind, and averages them
in, so the subject grows a halo of the wall behind it.

The fix is that a tap contributes only if its **own** circle of confusion is
large enough to have reached the pixel doing the gathering. That is the
physical statement — light spreads from where it landed, not to where it is
wanted — and it costs one comparison per tap.

Foreground over background is the opposite case and is *supposed* to bleed: an
out-of-focus object in front genuinely does spread over what is behind it. The
two are not symmetric and a gather that treats them the same is wrong in one
direction or the other.

### Half resolution, and a fixed pattern

The gather runs at half resolution — the same trade the bloom chain already
makes, and for the same reason: a blur is a low-frequency thing and paying full
rate for it buys detail that the blur then removes. The composite is full
resolution, so sharp regions stay sharp.

The sample pattern is a **fixed golden-angle spiral**, not a random or
frame-varying one. Worth stating explicitly after 9.2 and 9.3: **depth of field
introduces no new determinism hazard.** Grain had to be seeded from the frame
number and auto exposure had to be driven by the loop's frame time; this one is
a pure function of the image and the settings, so `--screenshot-frame` needs
nothing new from it.

### Off is exactly off

`DepthOfField` defaults to false, no pass is added, and the chain is the one
that ran before it existed — the same guarantee 7w and 7y rest on, and the
third time it has been what keeps every recorded threshold in the repository
valid across a phase-9 item.

---

## 7aa. Declaration-site script fields: the generator, and why the wrapper includes the .cpp

Asked for 2026-08-15: a C++ script field should be declared once, where it
lives, instead of a second time in a trailing registration block --

```cpp
RVShowInEditor
float Swing = 0.34f;
```

C++ cannot do this in the language. A macro in a class body does not know its
enclosing type and the preprocessor cannot find out, so `RVShowInEditor`
expands to nothing and a **generator** (`rvgen`, over `ScriptGen` in the
engine) reads the source text and writes the registration a person would
have. Three markers: `RVShowInEditor` on a field, `RVCallable` on a method,
`RVScript` on a class with nothing else marked. `RV_REGISTER_SCRIPT` still
works; both on one class is a build error, because the registry's answer to a
duplicate is first-wins over link order.

**The generated code cannot live in a free-standing file.** `Bell` and
`Anvil` are whole classes in a .cpp -- the normal shape of a script here --
and no other translation unit can name the members of a class it cannot see.
So the emitted TU *#includes the marked source file* and appends the
registrations, and the build compiles the file only through its wrapper. One
wrapper per marked file, so file-local statics stay file-local; anonymous
namespaces work for the same reason. A marked *header* gets a wrapper too but
stays in the glob -- listed, not compiled.

**Generation runs at configure time, not build time.** Which files are
wrapped changes the *source list*, and only a configure can change the source
list. `CMAKE_CONFIGURE_DEPENDS` on every script is the other half: without
it, the first marker added to a file registers nothing until somebody happens
to reconfigure -- a silent nothing, the exact failure this design exists to
refuse. The cost is a reconfigure per script edit, a few seconds on a module
this size.

**Refusal is the design.** The scanner tracks braces, namespaces, classes and
access levels -- enough to attribute a marker -- and errors on every marked
declaration it does not fully understand: wrong type, private, static, const,
parameters, templates, nesting, two declarators under one marker. The errors
are MSVC-shaped (`file(line): error RVGEN1:`), so `ModuleBuild`'s existing
diagnostic parser lands them in the editor's build panel. A generator that
guesses, or ships what it understood and skips the rest, produces an
inspector with a field quietly missing -- the worst outcome available,
because nothing anywhere says why.

### The pair, again

`TemplateProbe.cpp` (engine, no generator) proves the markers are **inert**:
it must compile untouched and register nothing. scenetest's
`fixtures/rvgen/MarkerProbe.cpp` goes through the real rvgen at build time
and proves the same shape **registers**, fields, method and bare class alike.
Each alone is passable by a specific failure -- a marker macro that leaked
code would fail the first; a generator that emitted nothing would fail the
second. The demo scene is the third leg: Bell and Anvil migrated to markers,
so `CheckDemoButtons` passes only if generated registrations actually reach a
button.

### The stale-CMakeLists guard

A project whose `Source/CMakeLists.txt` predates rvgen compiles marked
scripts perfectly and registers none of them. `ModuleBuild::Build` scans for
markers before every build and warns -- in the build panel, naming the file
and the fix -- whenever markers exist and the CMakeLists never mentions
rvgen. That is the one silent path the configure-time hook cannot close on
its own, because the hook lives in the file that is stale.

---

## 7ab. Motion blur (9.5): a gather along a velocity nobody has to invent

Everything hard about this pass was built by other items, on purpose. 7.10
left an RG16F velocity attachment carrying real screen-space motion in UV
units per frame, jitter-free by construction; the sky reports the camera's
rotation (kept in 7r explicitly "for 9.5, which has no clip to save it");
9.4 left the scene depth sampleable and a worked example of a gather that
respects depth ordering. What 9.5 adds is the reconstruction filter.

### The naive version fails outward, not inward

Blurring each pixel along its *own* velocity blurs the inside of a moving
object and stops dead at its silhouette: the background pixel one texel
outside has zero velocity, gathers nothing, and the object tears past a
razor edge instead of smearing over it. The blur has to happen where the
motion *lands*, not where it started -- the same physical statement as
7z's bleeding rule, pointed the other way.

The standard answer (McGuire's reconstruction filter) is three passes:

1. **Tile max.** The velocity image downsampled to tiles the size of the
   largest blur allowed, keeping each tile's largest velocity.
2. **Neighbour max.** Each tile takes the largest of its 3x3 neighbours,
   so a fast object one tile over is known here too -- this is what lets
   a stationary pixel receive the smear of something passing it.
3. **Gather**, full resolution. Each pixel walks taps along the
   neighbourhood's dominant velocity, both directions, and weights each
   tap by whether it *could* be here: a moving tap spreads over a static
   centre (the object smears outward), a static tap holds against a
   moving centre only if it is in front (depth decides), and the centre's
   own motion blurs it over whatever it crosses.

A tile whose neighbourhood max is under half a pixel takes none of that:
the gather early-outs into a plain copy, which is what makes **zero
velocity a no-op to the byte** -- an RGBA16F copy of an RGBA16F image is
exact, and the check asserts it rather than trusting the reasoning.

### Where in the chain

**After depth of field, before bloom.** After the temporal resolve for
7z's reason -- the motion vectors describe where the *sharp* geometry
went, and a temporal filter must not run over an already-smeared image.
After DoF because both model the same exposure and the order has to be
picked: the defocused disc smearing along the motion is nearer the truth
than a smear being defocused, and it is also the cheap order, since DoF's
composite hands over a full-resolution image this reads directly. Before
bloom so a bright streak glows as the streak it became.

Velocity and depth both come from the scene target, which by this point
is a different size under SSAA and carries the frame's jitter under TAA
-- handled the way 7z handled depth: normalised coordinates, and the
jitter residual is below what the half-float velocity stores. The
velocity's vertical convention is the one taa_resolve documents; every
shader here that steps UVs by a velocity flips its Y with the pass's
FlipY, because these are exactly the passes that sample render targets
and write others.

### Shutter, in fractions of a frame

`MotionBlurShutter` scales the smear: 0.5 means the virtual shutter was
open half the frame, which is the 180-degree default every camera
defaults to for the same reason. The velocity buffer is *per frame*, so
the scaling is one multiply and stays honest at any frame rate -- a
slower frame has longer per-frame velocities and gets the longer smear a
slower shutter really produces. `MotionBlurMaxRadius` bounds the walk in
pixels and is also the tile size, because a blur that can reach further
than its tiles can see is a blur that tears at tile boundaries.

### No new determinism hazard, and the one place that tempts one

The gather offsets its taps per pixel to trade banding for noise, and
the dither is the grain's integer hash of the **pixel coordinate alone**
-- never the frame number. Seeded per frame it would sparkle under TAA
and cost `--screenshot-frame` its meaning; seeded per pixel the pass
stays what DoF is, a pure function of the image and the settings, and
every screenshot comparison in the repository keeps working unchanged.

What the blur shows is only as true as the velocity underneath, so 7r's
two recorded gaps surface here undamped by any clip: **skinned meshes
smear by the object's motion, not the limb's** (bones are not
double-buffered -- a memory decision), and particles and UI write zero
velocity and never smear. Both are documented behaviour, not bugs to
chase when a running fox's legs look crisper than its body.

### Off is exactly off

`MotionBlur` defaults to false, no pass is added, and the chain is
byte-identical to the one that ran before this existed -- the fourth
phase-9 item in a row to rest on that guarantee.

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
| Contacts | "route them into scripts" | the routing is easy; sleep, removal and sub-shape granularity are not (§3a) |
| Audio | "add miniaudio" | the null backend is the design, not a fallback (§7a) |
| Runtime | "prove nothing leaked into the editor" | it found three defects nothing else could reach (§7b) |
| Verification | zero validation lines | plus exit 0, plus the pixels (§7b) |
