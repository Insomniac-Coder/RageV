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
case; a scene mixing 128 and 512 gets two. That keeps the per-probe field, keeps
the single bind for any sane scene, and degrades to A's bind count only if
somebody uses a different resolution for every probe.

### What B needs, in order

1. **Cube arrays in the RHI.** Creation, view, and upload, on Vulkan and
   OpenGL. This is the only genuinely new capability, and it is the reason the
   cheap option was tempting.
2. **The sky occupies slot 0 of every array.** "No probe within influence" then
   costs no branch and no sentinel -- it is an index like any other, and the
   shader samples the array the same way for every surface. A `-1` meaning sky
   would put a conditional in the hot path for the most common case.
3. **An index per instance**, alongside the model matrix that is already there.
   Selection stays exactly what A would have done -- nearest probe whose
   `Influence` reaches this object, else 0 -- and that logic is shared, which is
   why the design work above was not wasted.
4. **Irradiance has the same shape and the same problem.** A probe's diffuse
   contribution is per probe too; today there is one irradiance cube for the
   scene. Either a second array indexed identically, or accept scene-wide
   irradiance and say so. **Do not leave this undecided** -- a scene whose
   reflections move per object while its ambient does not is a bug that reads
   as a lighting artefact.
5. **`samplerCubeArray` in the PBR shader**, and in the skinned variant.

### The trap A had, and B does not

A needed the probe *sorted on* rather than merely compared, or two objects
wanting different probes would land in one instanced draw. B has no such rule:
the index rides the instance, so a run can hold objects using any mix of probes.
That is the clearest statement of why B is the better shape, and it is worth
keeping because it is the kind of constraint that is invisible until it is
violated.

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
