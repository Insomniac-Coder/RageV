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
**`GltfImporter` only reads a texture when the image has a `uri`**, so images
embedded in a `.glb`'s buffer views are skipped and every GLB imports
untextured. The fox is white for that reason and not because anything in
7.5 or 7.6 is wrong. Recorded as a papercut rather than fixed here.

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
